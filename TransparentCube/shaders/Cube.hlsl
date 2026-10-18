// ============================================================================
// M5 贴图 + 光照 shader：WIC 贴图采样 + Lambert 漫反射 + Blinn-Phong 高光
// 同一文件含 VS / PS 两个入口，C++ 端分两次编译
// ============================================================================

// ----------------------------------------------------------------------------
// 每帧常量（register b0）：相机与光源，同一帧内所有物体共享
// 字段顺序必须与 C++ 端 PerFrameCB 完全一致
// ----------------------------------------------------------------------------
// M13 点光源：每个点光两个 float4，与 C++ 端 PointLightCB 布局一致
struct PointLightData
{
    float4 PosRange;   // xyz = 世界位置, w = 影响半径
    float4 Color;      // rgb = 光色, a = 强度
};

cbuffer PerFrameCB : register(b0)
{
    float4x4 gViewProj;       // 世界 → 裁剪
    float4   gCameraPos;      // xyz = 相机世界坐标
    float4   gLightDir;       // xyz = 表面指向光源的方向（世界空间）
    float4   gLightColor;     // rgb = 光色, a = 强度
    float4   gAmbientColor;   // rgb = 环境光颜色
    PointLightData gPointLights[4];   // 光源列表（上限与 C++ 端 kMaxPointLights 一致）
    float4   gLightCounts;    // x = 生效的点光源数量
};

// ----------------------------------------------------------------------------
// 每物体常量（register b1）：变换与材质，每个 DrawCall 前更新
// 字段顺序必须与 C++ 端 PerObjectCB 完全一致
// ----------------------------------------------------------------------------
cbuffer PerObjectCB : register(b1)
{
    float4x4 gWorld;          // 本地 → 世界
    float4x4 gNormalMatrix;   // 世界矩阵的逆转置（专门用来变换法线）
    float4   gBaseColor;      // 基础色（rgb + alpha）
    float4   gMaterialParams; // x = roughness, y = spec, z = tintStrength
};

// ----------------------------------------------------------------------------
// 阴影常量（register b2）：与 C++ 端 ShadowMap::ShadowCB 布局一致
// ----------------------------------------------------------------------------
cbuffer ShadowCB : register(b2)
{
    float4x4 gLightViewProj;   // 世界 → 光源裁剪空间
    float4   gShadowParams;    // x = 1/shadowMapSize, y = depthBias
};

// 纹理与采样器：
//   t0/s0 = 材质颜色贴图
//   t1/s1 = ShadowMap
//   t3/s0 = 切线空间法线贴图（与颜色贴图共用 sampler）
// UV 的 (0,0) 在纹理左上角，(1,1) 在右下角（D3D11 约定）。
Texture2D    gTexture   : register(t0);
Texture2D    gNormalMap : register(t3);
SamplerState gSampler   : register(s0);

Texture2D<float> gShadowMap     : register(t1);
SamplerState     gShadowSampler : register(s1);

struct VSInput
{
    float3 pos       : POSITION;   // 对应 C++ 输入布局里的 POSITION
    float3 normal    : NORMAL;     // 对应 C++ 输入布局里的 NORMAL
    float3 tangent   : TANGENT;    // 切线空间 +U
    float3 bitangent : BITANGENT;  // 切线空间 +V
    float2 uv        : TEXCOORD0;  // 对应 C++ 输入布局里的 TEXCOORD
};

struct VSOutput
{
    float4 pos       : SV_POSITION;   // 裁剪空间位置，直接交给光栅化器
    float2 uv        : TEXCOORD0;     // 纹理坐标，交给 PS 采样
    float3 worldPos  : TEXCOORD1;     // 世界空间位置（PS 算视线方向用）
    float3 normal    : NORMAL;        // 世界空间法线（插值后需重新归一化）
    float3 tangent   : TANGENT;       // 世界空间切线
    float3 bitangent : BITANGENT;     // 世界空间副切线
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;

    // 顶点着色器的核心工作：本地坐标 → 世界 → 裁剪空间。
    // 矩阵约定与 C++ 端成对：DirectXMath 行主序存储 + HLSL 默认列主序，
    // 直接上传后 HLSL 看到的正好是转置，配合 mul(矩阵, 向量) 即正确结果。
    const float4 worldPos = mul(gWorld, float4(input.pos, 1.0f));
    output.pos      = mul(gViewProj, worldPos);
    output.uv       = input.uv;
    output.worldPos = worldPos.xyz;

    // 法线变换必须用法线矩阵（世界矩阵的逆转置），不能直接用 gWorld。
    // 否则物体被非均匀缩放时，法线会歪掉，光照方向就算错了。
    output.normal = mul((float3x3)gNormalMatrix, input.normal);

    // 切线/副切线是切向方向，按世界矩阵变换；PS 里会再对 N 做正交化。
    output.tangent   = mul((float3x3)gWorld, input.tangent);
    output.bitangent = mul((float3x3)gWorld, input.bitangent);

    return output;
}

// ----------------------------------------------------------------------------
// Shadow Map 查询：3×3 PCF
//
// 返回 1 = 完全受光，0 = 完全在阴影里。
// 深度采样故意用点采样（s1 是 POINT/CLAMP），因为“先线性平均深度再比较”
// 没有几何意义；PCF 应该是“先比较，再平均”。
// ----------------------------------------------------------------------------
float ComputeShadow(float3 worldPos, float ndotl)
{
    // 世界空间 → 光源裁剪空间 → NDC
    const float4 lightClip = mul(gLightViewProj, float4(worldPos, 1.0f));
    const float3 lightNDC  = lightClip.xyz / lightClip.w;

    // D3D11 纹理 v=0 在顶部，而 NDC y 向上，所以 y 需要翻转。
    const float2 shadowUV = float2(
        lightNDC.x * 0.5f + 0.5f,
        0.5f - lightNDC.y * 0.5f);

    const float currentDepth = lightNDC.z;   // D3D11 NDC z ∈ [0,1]

    // 超出 ShadowMap 覆盖范围时按“受光”处理，避免边缘出现黑边。
    if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f) ||
        currentDepth < 0.0f || currentDepth > 1.0f)
    {
        return 1.0f;
    }

    // 斜面对深度误差更敏感，所以 bias 随 N·L 变小而增大。
    const float bias = max(gShadowParams.y * (1.0f - ndotl),
                           gShadowParams.y * 0.25f);

    const float2 texelSize = float2(gShadowParams.x, gShadowParams.x);
    float shadow = 0.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 offset = float2(x, y) * texelSize;
            const float storedDepth =
                gShadowMap.SampleLevel(gShadowSampler, shadowUV + offset, 0.0f);

            // 当前表面比 ShadowMap 记录的最近深度更远 → 被挡住。
            shadow += (currentDepth - bias <= storedDepth) ? 1.0f : 0.0f;
        }
    }

    return shadow / 9.0f;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    // ---- 准备三个单位向量 ----
    // N：初始为几何法线，采样法线贴图后会替换成细节法线；
    // L：表面指向光源；V：表面指向相机
    float3 N = normalize(input.normal);
    float3 L = normalize(gLightDir.xyz);
    float3 V = normalize(gCameraPos.xyz - input.worldPos);

    // Blinn-Phong 的半程向量 H = normalize(L + V)。
    // 它和“反射向量 R 与视线 V 的夹角”等价，但少算一次反射，更便宜。
    // 加一个极小值是为了防止 L ≈ -V 时 normalize(0) 产生 NaN。
    float3 H = normalize(L + V + 1e-5);

    // ---- 贴图采样与染色 ----
    // 贴图提供表面的“颜色细节”，BaseColor 作为染色颜色。
    // tintStrength 控制染色程度：
    //   0 = 完全保留贴图颜色；
    //   1 = 贴图颜色 × BaseColor（原来的完全染色行为）。
    // 注意 alpha 仍由 BaseColor.a 控制，不参与这里的 RGB 混合。
    // 不贴图的材质在 C++ 侧指向 1×1 白色纹理，所以这里可以无条件采样。
    // 注意：当前纹理按 UNORM 采样，图片本身是 sRGB 编码；本 demo 暂未做
    // 线性空间/gamma 校正，光照数学是在 sRGB 数值上近似计算的。
    const float4 texel = gTexture.Sample(gSampler, input.uv);
    const float  tintStrength = saturate(gMaterialParams.z);
    const float3 baseColor = lerp(texel.rgb, texel.rgb * gBaseColor.rgb, tintStrength);
    const float  alpha = texel.a * gBaseColor.a;

    // ---- 法线贴图：切线空间 → 世界空间 ----
    // 法线贴图的 RGB 编码的是切线空间法线：x=+U，y=+V，z=几何法线。
    // 顶点着色器送来世界空间的 T/B；插值后的 T/B/N 不一定严格正交，
    // 先用 Gram-Schmidt 把 T、B 投影到 N 的切平面上，再组合成 TBN。
    // 不贴法线图的材质指向 DefaultNormal（1×1 的 (128,128,255)），
    // 解码后约为 (0,0,1)，此时 worldNormal 会退化为几何法线 N。
    float3 T = normalize(input.tangent - N * dot(N, input.tangent));
    float3 B = normalize(input.bitangent
                         - N * dot(N, input.bitangent)
                         - T * dot(T, input.bitangent));
    const float3 tangentNormal =
        gNormalMap.Sample(gSampler, input.uv).xyz * 2.0f - 1.0f;
    N = normalize(T * tangentNormal.x +
                  B * tangentNormal.y +
                  N * tangentNormal.z);

    // ---- 取出材质参数 ----
    float  roughness     = saturate(gMaterialParams.x);
    float  specIntensity = saturate(gMaterialParams.y);

    // ---- Lambert 漫反射 ----
    // N·L 表示表面正对光源的程度：正对 = 1，侧面 = 0，背光 = 负数。
    // saturate 把背光面压到 0，避免出现“负光照”。
    float  ndotl  = saturate(dot(N, L));
    float3 diffuse = baseColor * gLightColor.rgb * gLightColor.a * ndotl;

    // ---- Blinn-Phong 高光 ----
    // N·H 越接近 1，说明半程向量越贴近法线，高光越亮。
    // 指数 shininess 控制高光范围：越大越集中（光滑），越小越发散（粗糙）。
    // 这里把 [0,1] 的 roughness 映射到 [24,4] 的指数，只是教学用近似；
    // 真正的 PBR 会让 roughness 参与 BRDF 计算，而不是简单换指数。
    // （指数上限没有取 128：立方体每个面法线恒定，高光在单个面上是均匀的，
    //   指数太大时高光会窄到几乎看不见；球面/法线贴图才需要更大的指数。）
    float shininess = lerp(24.0, 4.0, roughness);
    float specMask  = step(1e-4, ndotl);   // 背光面不产生高光
    float spec      = pow(saturate(dot(N, H)), shininess) * specIntensity * specMask;
    float3 specular = gLightColor.rgb * gLightColor.a * spec;

    // ---- 环境光 ----
    // 模拟场景中多次反弹的间接光，避免背光面变成纯黑。
    // 这是最廉价的近似：不依赖法线方向，也不区分遮挡。
    float3 ambient = baseColor * gAmbientColor.rgb;

    // ---- 点光源（M13）：前向多光源循环 ----
    // 与方向光共用同一套 Lambert + Blinn-Phong 公式；点光源不投阴影
    // （点光阴影需要 cubemap + 6 个 Pass，是另一个量级的里程碑）。
    // 多光源是帧级环境数据（光源列表），材质参数与 shader 路径都不需要变化。
    float3 pointSum = 0.0f;
    const int numPointLights = (int)gLightCounts.x;
    for (int i = 0; i < numPointLights; ++i)
    {
        const float3 toLight = gPointLights[i].PosRange.xyz - input.worldPos;
        const float  dist    = length(toLight);
        const float3 Lp      = toLight / max(dist, 1e-5);
        const float  range   = gPointLights[i].PosRange.w;

        // 衰减 = 范围平滑截断 × 平方反比（分母 +1 防止零距离爆亮）。
        // demo 级别的教学近似，不是物理光度单位。
        float atten = saturate(1.0f - dist / max(range, 1e-3));
        atten *= atten;
        atten /= (1.0f + dist * dist);

        const float3 radiance = gPointLights[i].Color.rgb * gPointLights[i].Color.a * atten;

        const float  ndotlP    = saturate(dot(N, Lp));
        const float3 Hp        = normalize(Lp + V + 1e-5);
        const float  specMaskP = step(1e-4, ndotlP);   // 背光面不产生高光
        const float  specP     = pow(saturate(dot(N, Hp)), shininess)
                                 * specIntensity * specMaskP;

        pointSum += (baseColor * ndotlP + specP) * radiance;
    }

    // ---- 阴影 ----
    // 只压暗平行光的直接光照（diffuse + specular），环境光保留；
    // 否则阴影内部会变成纯黑，和环境光的视觉逻辑冲突。
    // 点光源无阴影，其贡献不受 shadow 影响。
    const float shadow = ComputeShadow(input.worldPos, ndotl);
    const float3 color = ambient + (diffuse + specular) * shadow + pointSum;

    // alpha = 贴图 alpha × 材质 alpha；M6 会用它配合 BlendState 做半透明混合。
    // JPEG 没有 alpha 通道，WIC 会把它补成 1.0。
    return float4(color, alpha);
}
