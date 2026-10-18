// ============================================================================
// M8 / M8.5 后处理：全屏三角形 + 三个 PS 变体
//
// VSMain 共用；PS 入口：
//   CopyPS          = 原样拷贝（Off）
//   GrayscalePS     = 灰度 + 暗角（影响全场景）
//   MaskedDarkenPS  = 整体压暗（stencil 蒙版由固定功能测试完成）
//
// 三个模式不再用 shader 内 if 选择，而是由 CPU 绑定不同的 PS 变体。
// 这个 shader 不接收顶点缓冲；VS 用 SV_VertexID 直接生成覆盖屏幕的三角形。
// ============================================================================

cbuffer PostProcessCB : register(b0)
{
    float4 gParams;   // x 暂时未用；y = 暗角强度；z = 压暗系数
};

Texture2D    gSceneColor : register(t0);
SamplerState gSampler    : register(s0);

struct VSOutput
{
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

// 三个顶点覆盖整个屏幕：
//   id 0 → (-1, 1)   左上
//   id 1 → ( 3, 1)   右上（屏幕外）
//   id 2 → (-1,-3)   左下（屏幕外）
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

// PS 变体 1：原样拷贝
float4 CopyPS(VSOutput input) : SV_TARGET
{
    return gSceneColor.Sample(gSampler, input.uv);
}

// PS 变体 2：灰度 + 暗角，影响整个场景
float4 GrayscalePS(VSOutput input) : SV_TARGET
{
    const float4 scene = gSceneColor.Sample(gSampler, input.uv);

    const float gray = dot(scene.rgb, float3(0.299f, 0.587f, 0.114f));
    float3 color = float3(gray, gray, gray);

    // UV 中心是 (0.5,0.5)，越靠边缘 dot(d,d) 越大，颜色越暗。
    const float2 d = input.uv - 0.5f;
    const float vignette = saturate(1.0f - dot(d, d) * gParams.y);
    color *= vignette;

    return float4(color, scene.a);
}

// PS 变体 3：整体压暗
// “场景几乎变黑但保留细节”：乘系数 + 一点点冷色底，避免暗部死黑。
// stencil==1 的 CubeB 区域由固定功能 Stencil Test 跳过，不进入这个 PS。
float4 MaskedDarkenPS(VSOutput input) : SV_TARGET
{
    const float4 scene = gSceneColor.Sample(gSampler, input.uv);
    const float3 darkTint = float3(0.008f, 0.010f, 0.016f);
    const float3 color = scene.rgb * gParams.z + darkTint;
    return float4(color, scene.a);
}
