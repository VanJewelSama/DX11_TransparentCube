// ============================================================================
// M10.5 FXAA：全屏后处理抗锯齿
//
// 输入：SceneColor（已经渲染完的 LDR 场景）
// 输出：FXAA 目标 RT，再由 CopyPS/GrayscalePS/MaskedDarkenPS 采样。
//
// FXAA 不改变几何采样，而是检测屏幕空间亮度边缘并做方向性混合。
// 优点是接入简单；缺点是画面会略微变软。
// ============================================================================

cbuffer FXAACB : register(b0)
{
    float4 gFXAAParams;   // x = 1/width, y = 1/height, z = 边缘阈值
};

Texture2D    gSceneColor : register(t0);
SamplerState gSampler    : register(s0);

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// 与 PostProcess.hlsl 相同的全屏三角形，不需要顶点缓冲。
VSOutput VSMain(uint vertexID : SV_VertexID)
{
    const float2 uv = float2((vertexID << 1) & 2, vertexID & 2);

    VSOutput output;
    output.pos = float4(uv.x * 2.0f - 1.0f,
                        1.0f - uv.y * 2.0f,
                        0.0f, 1.0f);
    output.uv = uv;
    return output;
}

static const float3 kLumaWeights = float3(0.299f, 0.587f, 0.114f);

float4 FXAAPSMain(VSOutput input) : SV_TARGET
{
    const float4 center = gSceneColor.Sample(gSampler, input.uv);
    const float2 texel = gFXAAParams.xy;

    // 中心 + 四个对角邻居
    const float3 rgbNW = gSceneColor.Sample(gSampler, input.uv + float2(-1.0f, -1.0f) * texel).rgb;
    const float3 rgbNE = gSceneColor.Sample(gSampler, input.uv + float2( 1.0f, -1.0f) * texel).rgb;
    const float3 rgbSW = gSceneColor.Sample(gSampler, input.uv + float2(-1.0f,  1.0f) * texel).rgb;
    const float3 rgbSE = gSceneColor.Sample(gSampler, input.uv + float2( 1.0f,  1.0f) * texel).rgb;

    const float lumaM  = dot(center.rgb, kLumaWeights);
    const float lumaNW = dot(rgbNW, kLumaWeights);
    const float lumaNE = dot(rgbNE, kLumaWeights);
    const float lumaSW = dot(rgbSW, kLumaWeights);
    const float lumaSE = dot(rgbSE, kLumaWeights);

    const float lumaMin = min(lumaM, min(min(lumaNW, lumaNE), min(lumaSW, lumaSE)));
    const float lumaMax = max(lumaM, max(max(lumaNW, lumaNE), max(lumaSW, lumaSE)));

    // 对比度太低：不是边缘，原样输出。
    if (lumaMax - lumaMin < max(0.0312f, lumaMax * gFXAAParams.z))
        return center;

    // 由亮度梯度估计边缘方向
    float2 dir;
    dir.x = -((lumaNW + lumaNE) - (lumaSW + lumaSE));
    dir.y =  ((lumaNW + lumaSW) - (lumaNE + lumaSE));

    // 归一化并限制采样距离，避免边缘被拉得过长
    const float dirReduce = max((lumaNW + lumaNE + lumaSW + lumaSE) * 0.03125f,
                                1.0f / 128.0f);
    const float rcpDirMin = 1.0f / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, -8.0f, 8.0f) * texel;

    // 沿边缘方向采样两组合，选择不会超出局部亮度范围的结果。
    const float3 rgbA = 0.5f * (
        gSceneColor.Sample(gSampler, input.uv + dir * (1.0f / 3.0f - 0.5f)).rgb +
        gSceneColor.Sample(gSampler, input.uv + dir * (2.0f / 3.0f - 0.5f)).rgb);

    const float3 rgbB = rgbA * 0.5f + 0.25f * (
        gSceneColor.Sample(gSampler, input.uv - dir * 0.5f).rgb +
        gSceneColor.Sample(gSampler, input.uv + dir * 0.5f).rgb);

    const float lumaB = dot(rgbB, kLumaWeights);
    if (lumaB < lumaMin || lumaB > lumaMax)
        return float4(rgbA, center.a);

    return float4(rgbB, center.a);
}
