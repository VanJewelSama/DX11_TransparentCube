// ============================================================================
// M16 风格化 shader（Toon）：一个文件两对入口（≈ Unity 单 shader 多 Pass）
//
//   VSMain / PSMain             本体 Pass：阶梯量化漫反射（扁平色块）
//   OutlineVSMain / OutlinePSMain 描边 Pass：inverted hull（背面外扩 + 纯色）
//
// 与 Cube.hlsl 的差异：N·L 量化成 3 档、无高光、不采样法线贴图；
// 贴图/染色/阴影采样/点光源衰减与 Lit 路径同一约定（光照环境一致，只是量化）。
// ============================================================================

// ----------------------------------------------------------------------------
// 每帧常量（register b0）：与 C++ 端 PerFrameCB 布局完全一致
// ----------------------------------------------------------------------------
struct PointLightData
{
    float4 PosRange;   // xyz = 世界位置, w = 影响半径
    float4 Color;      // rgb = 光色, a = 强度
};

cbuffer PerFrameCB : register(b0)
{
    float4x4 gViewProj;
    float4   gCameraPos;
    float4   gLightDir;
    float4   gLightColor;
    float4   gAmbientColor;
    PointLightData gPointLights[4];
    float4   gLightCounts;    // x = 生效的点光源数量
};

// ----------------------------------------------------------------------------
// 每物体常量（register b1）：与 C++ 端 PerObjectCB 布局完全一致
// ----------------------------------------------------------------------------
cbuffer PerObjectCB : register(b1)
{
    float4x4 gWorld;
    float4x4 gNormalMatrix;
    float4   gBaseColor;
    float4   gMaterialParams; // x = roughness（未用）, y = spec（未用）, z = tintStrength
    float4   gToonParams;     // xyz = 描边颜色, w = 描边宽度（世界空间）
};

// 阴影常量（register b2）：与 C++ 端 ShadowMap::ShadowCB 布局一致
cbuffer ShadowCB : register(b2)
{
    float4x4 gLightViewProj;
    float4   gShadowParams;   // x = 1/shadowMapSize, y = depthBias
};

Texture2D        gTexture       : register(t0);
SamplerState     gSampler       : register(s0);
Texture2D<float> gShadowMap     : register(t1);
SamplerState     gShadowSampler : register(s1);

struct VSInput
{
    float3 pos       : POSITION;
    float3 normal    : NORMAL;
    float3 tangent   : TANGENT;    // 声明但不使用（输入布局与 Cube 共用）
    float3 bitangent : BITANGENT;  // 声明但不使用
    float2 uv        : TEXCOORD0;
};

struct VSOutput
{
    float4 pos      : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float3 normal   : NORMAL;
};

// ============================================================================
// 本体 Pass
// ============================================================================
VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float4 worldPos = mul(gWorld, float4(input.pos, 1.0f));
    output.pos      = mul(gViewProj, worldPos);
    output.uv       = input.uv;
    output.worldPos = worldPos.xyz;
    output.normal   = mul((float3x3)gNormalMatrix, input.normal);
    return output;
}

// Shadow Map 查询：与 Cube.hlsl 相同的 3×3 PCF（复制，保持两个文件自洽）
float ComputeShadow(float3 worldPos, float ndotl)
{
    const float4 lightClip = mul(gLightViewProj, float4(worldPos, 1.0f));
    const float3 lightNDC  = lightClip.xyz / lightClip.w;

    const float2 shadowUV = float2(
        lightNDC.x * 0.5f + 0.5f,
        0.5f - lightNDC.y * 0.5f);

    const float currentDepth = lightNDC.z;

    if (any(shadowUV < 0.0f) || any(shadowUV > 1.0f) ||
        currentDepth < 0.0f || currentDepth > 1.0f)
    {
        return 1.0f;
    }

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
            shadow += (currentDepth - bias <= storedDepth) ? 1.0f : 0.0f;
        }
    }

    return shadow / 9.0f;
}

// 阶梯量化：连续的 N·L → 3 档（扁平化的核心）。
// 背光面也给 0.18 而不是 0：轮廓不至于死黑，是 toon 的常见风格选择。
float QuantizeBand(float ndotl)
{
    if (ndotl > 0.6f) return 1.0f;
    if (ndotl > 0.2f) return 0.55f;
    return 0.18f;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    const float3 N = normalize(input.normal);
    const float3 L = normalize(gLightDir.xyz);

    // 贴图 + 染色（与 Cube.hlsl 同一约定）
    const float4 texel = gTexture.Sample(gSampler, input.uv);
    const float  tintStrength = saturate(gMaterialParams.z);
    const float3 baseColor = lerp(texel.rgb, texel.rgb * gBaseColor.rgb, tintStrength);

    // 平行光：量化后的 band × 连续阴影。
    // 想要更硬的卡通风影子，可以对 shadow 再做一次 step(0.5, shadow)。
    const float ndotl  = dot(N, L);
    const float band   = QuantizeBand(ndotl);
    const float shadow = ComputeShadow(input.worldPos, saturate(ndotl));
    float3 color = baseColor * gAmbientColor.rgb
                 + baseColor * gLightColor.rgb * gLightColor.a * band * shadow;

    // 点光源：同一套衰减 + 同一套量化（光照环境一致，只是响应曲线不同）
    const int numPointLights = (int)gLightCounts.x;
    for (int i = 0; i < numPointLights; ++i)
    {
        const float3 toLight = gPointLights[i].PosRange.xyz - input.worldPos;
        const float  dist    = length(toLight);
        const float3 Lp      = toLight / max(dist, 1e-5);
        const float  range   = gPointLights[i].PosRange.w;

        float atten = saturate(1.0f - dist / max(range, 1e-3));
        atten *= atten;
        atten /= (1.0f + dist * dist);

        const float bandP = QuantizeBand(dot(N, Lp));
        color += baseColor * gPointLights[i].Color.rgb * gPointLights[i].Color.a
                 * atten * bandP;
    }

    return float4(color, texel.a * gBaseColor.a);
}

// ============================================================================
// 描边 Pass（inverted hull）：
// 顶点沿世界空间法线外扩 gToonParams.w，CULL_FRONT 只画背面并输出纯色。
// 先画描边、再画本体：外扩 hull 在轮廓处探出本体形成黑边。
// ============================================================================
struct OutlineVSOutput
{
    float4 pos : SV_POSITION;
};

OutlineVSOutput OutlineVSMain(VSInput input)
{
    OutlineVSOutput output;
    const float4 worldPos    = mul(gWorld, float4(input.pos, 1.0f));
    const float3 worldNormal = normalize(mul((float3x3)gNormalMatrix, input.normal));
    output.pos = mul(gViewProj, worldPos + float4(worldNormal * gToonParams.w, 0.0f));
    return output;
}

float4 OutlinePSMain(OutlineVSOutput input) : SV_TARGET
{
    return float4(gToonParams.rgb, 1.0f);
}
