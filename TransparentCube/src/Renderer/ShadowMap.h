#pragma once

#include "RHI/RenderTarget.h"
#include "Shader.h"

#include <DirectXMath.h>

#include <vector>

class StaticMeshActor;
class DirectionalLightActor;

// ============================================================================
// 平行光阴影贴图（≈ UE 单个 FProjectedShadowInfo / ShadowMap2D 的最小版）
//
// 职责：
//   - 拥有 depth-only RenderTarget（D32）
//   - 用光源正交相机渲染 caster 的深度
//   - 向主 Pass 提供 ShadowMap SRV / 采样器 / LightViewProj 常量
//
// 说明：
//   - 当前只支持一盏平行光；
//   - ShadowMap 固定分辨率，不随窗口 Resize；
//   - 默认 2048×2048，正交范围 20×20，覆盖当前 10×10 地面。
// ============================================================================
class ShadowMap
{
public:
    bool Initialize(ID3D11Device* device, UINT size = 2048);

    // 渲染深度图。casters 由 Renderer 提前筛好（不透明 + bCastShadow）。
    void Render(ID3D11DeviceContext* context,
                const std::vector<const StaticMeshActor*>& casters,
                const DirectionalLightActor& light,
                const DirectX::XMFLOAT3& sceneCenter);

    // 主 Pass 采样时绑定：t1 = ShadowMap, s1 = 比较/点采样器, b2 = ShadowCB
    void BindForSampling(ID3D11DeviceContext* context) const;

    // 解绑 t1，避免下一帧 Shadow Pass 把同一资源同时作为 DSV/SRV 绑定
    void Unbind(ID3D11DeviceContext* context) const;

    const DirectX::XMMATRIX& GetLightViewProj() const { return m_lightViewProj; }
    ID3D11ShaderResourceView* GetSRV() const { return m_target.GetDepthSRV(); }
    UINT GetSize() const { return m_size; }

private:
    void UpdateLightCamera(const DirectionalLightActor& light,
                           const DirectX::XMFLOAT3& sceneCenter);

    RenderTarget          m_target;      // depth-only
    ShaderSet             m_shader;      // 只编译 Shadow.hlsl 的 VSMain
    ComPtr<ID3D11Buffer>  m_shadowCB;    // b2：LightViewProj + 采样参数
    ComPtr<ID3D11Buffer>  m_objectCB;    // b1：每个 caster 的 World
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11RasterizerState> m_rasterizer;
    ComPtr<ID3D11DepthStencilState> m_depthState;

    DirectX::XMMATRIX m_lightViewProj = DirectX::XMMatrixIdentity();
    UINT m_size = 0;
};
