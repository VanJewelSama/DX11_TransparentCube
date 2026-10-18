// ============================================================================
// M7 Shadow Pass：从光源视角写入深度
// 只需要顶点着色器；C++ 端会把 PixelShader 设为 nullptr，只保留深度写入。
// ============================================================================

// 与 C++ 端 ShadowMap 的 ShadowCB 布局一致（register b2）
cbuffer ShadowCB : register(b2)
{
    float4x4 gLightViewProj;   // 光源 View * Proj
    float4   gShadowParams;    // x = 1/shadowMapSize, y = depthBias（本 Pass 暂未使用）
};

// 与 C++ 端 ShadowObjectCB 布局一致（register b1）
cbuffer ShadowObjectCB : register(b1)
{
    float4x4 gWorld;           // 物体本地 → 世界
};

struct VSInput
{
    float3 pos : POSITION;
};

// 本地 → 世界 → 光源裁剪空间。
// 矩阵约定与主 shader 一致：DirectXMath 行主序 + HLSL 默认列主序，
// 直接上传后使用 mul(矩阵, 向量) 即可。
float4 VSMain(VSInput input) : SV_POSITION
{
    const float4 worldPos = mul(gWorld, float4(input.pos, 1.0f));
    return mul(gLightViewProj, worldPos);
}
