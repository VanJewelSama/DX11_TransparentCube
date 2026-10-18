// ============================================================================
// M15 Unlit shader：不受光照的纯色绘制（当前用于光源位置指示器）
// 只读 gViewProj / gWorld / gBaseColor；不需要法线/贴图/光照计算。
// ============================================================================

// 与主渲染共用帧常量布局（这里只声明用到的前缀字段）
cbuffer PerFrameCB : register(b0)
{
    float4x4 gViewProj;   // 世界 → 裁剪
};

// 与 Renderer 的 PerObjectCB 布局一致：gBaseColor 之前的字段必须占位
cbuffer PerObjectCB : register(b1)
{
    float4x4 gWorld;          // 本地 → 世界
    float4x4 gNormalMatrix;   // 占位：本 shader 不使用
    float4   gBaseColor;      // 纯色输出（rgb）
};

struct VSInput
{
    float3 pos : POSITION;
};

struct VSOutput
{
    float4 pos : SV_POSITION;
};

VSOutput VSMain(VSInput input)
{
    VSOutput output;
    output.pos = mul(gViewProj, mul(gWorld, float4(input.pos, 1.0f)));
    return output;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    return float4(gBaseColor.rgb, 1.0f);
}
