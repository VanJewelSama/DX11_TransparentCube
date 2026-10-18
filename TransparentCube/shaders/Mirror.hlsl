// ============================================================================
// M10 平面镜 Shader
//
// 镜面几何用主相机的 ViewProj 绘制；
// 反射纹理 UV 用镜像相机的 ViewProj 计算。
// 反射 RT 由 PlanarMirror 在单独 Pass 中渲染，镜面自身不进入反射 Pass。
// ============================================================================

// 与主渲染共用的帧/物体常量（这里只声明用到的前缀字段）
cbuffer PerFrameCB : register(b0)
{
    float4x4 gViewProj;   // 主相机 View * Proj
};

cbuffer PerObjectCB : register(b1)
{
    float4x4 gWorld;      // 镜面本地 → 世界
};

cbuffer MirrorCB : register(b3)
{
    float4x4 gMirrorViewProj;   // 镜像相机 View * Proj
    float4   gMirrorTint;       // rgb = 淡白色，a = 与反射的混合权重
};

Texture2D    gReflection        : register(t2);
SamplerState gReflectionSampler : register(s2);

struct VSInput
{
    float3 pos : POSITION;
};

struct VSOutput
{
    float4 pos      : SV_POSITION;
    float3 worldPos : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    const float4 worldPos = mul(gWorld, float4(input.pos, 1.0f));

    output.pos      = mul(gViewProj, worldPos);
    output.worldPos = worldPos.xyz;
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    // 世界坐标 → 镜像相机裁剪空间 → 反射纹理 UV
    const float4 mirrorClip = mul(gMirrorViewProj, float4(input.worldPos, 1.0f));
    const float2 uv = float2(
        mirrorClip.x / mirrorClip.w * 0.5f + 0.5f,
        0.5f - mirrorClip.y / mirrorClip.w * 0.5f);   // D3D 纹理 v 轴朝下

    const float3 reflected = gReflection.Sample(gReflectionSampler, uv).rgb;

    // 淡白色镜面：反射为主，tint 只做轻微混合
    const float3 color = lerp(reflected, gMirrorTint.rgb, gMirrorTint.a);
    return float4(color, 1.0f);
}
