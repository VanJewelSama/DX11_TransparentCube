#include "ShadowMap.h"

#include "Core/PathUtil.h"
#include "Scene/Actor.h"
#include "Scene/Light.h"
#include <cmath>
#include <cstdio>
#include <string>

using namespace DirectX;

namespace
{
    // 与 shaders/Shadow.hlsl 中的 cbuffer 布局一一对应
    struct ShadowCB
    {
        XMFLOAT4X4 LightViewProj;
        XMFLOAT4   Params;         // x = 1/shadowMapSize, y = depthBias
    };
    static_assert(sizeof(ShadowCB) % 16 == 0, "ShadowCB 必须 16 字节对齐");

    struct ShadowObjectCB
    {
        XMFLOAT4X4 World;
    };
    static_assert(sizeof(ShadowObjectCB) % 16 == 0, "ShadowObjectCB 必须 16 字节对齐");

    // 当前场景是 10×10 地面 + 单位立方体；20×20 的正交范围足够覆盖。
    // 以后场景变大，应该根据场景包围盒动态拟合这个范围。
    constexpr float kOrthoSize    = 20.0f;
    constexpr float kLightDistance = 20.0f;
    constexpr float kNearZ        = 0.1f;
    constexpr float kFarZ         = 50.0f;
    constexpr float kDepthBias    = 0.0015f;
}

bool ShadowMap::Initialize(ID3D11Device* device, UINT size)
{
    if (!device || size == 0)
        return false;

    m_size = size;

    // ---- 1. depth-only RenderTarget ----
    RenderTargetDesc desc;
    desc.Width           = size;
    desc.Height          = size;
    desc.ColorFormat     = DXGI_FORMAT_UNKNOWN;   // 只要深度
    desc.DepthFormat     = DXGI_FORMAT_D32_FLOAT;
    desc.bCreateColorSRV = false;
    desc.bCreateDepthSRV = true;                  // 主 Pass 要采样
    if (!m_target.Create(device, desc))
        return false;

    // ---- 2. Shadow Pass shader（只编译 VSMain）----
    const D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
          D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    const std::wstring shaderPath = PathUtil::ResolveContentPath(L"shaders/Shadow.hlsl");
    if (!m_shader.LoadVSFromFile(device, shaderPath.c_str(), layout, _countof(layout)))
        return false;

    // ---- 3. 常量缓冲 ----
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage     = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    cbDesc.ByteWidth = sizeof(ShadowCB);
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_shadowCB)))
    {
        printf("[ShadowMap] 创建 ShadowCB 失败\n");
        return false;
    }

    cbDesc.ByteWidth = sizeof(ShadowObjectCB);
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_objectCB)))
    {
        printf("[ShadowMap] 创建 ShadowObjectCB 失败\n");
        return false;
    }

    // ---- 4. 采样器：点采样 + Clamp ----
    // 深度值不能先做线性平均再比较；这里用点采样，PCF 在 shader 里手动做。
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
    samplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD         = 0.0f;
    samplerDesc.MaxLOD         = 0.0f;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &m_sampler)))
    {
        printf("[ShadowMap] 创建 SamplerState 失败\n");
        return false;
    }

    // ---- 5. Shadow Pass 光栅化状态 ----
    // CULL_BACK + slope-scaled depth bias：减轻阴影 acne（自阴影条纹）。
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode              = D3D11_FILL_SOLID;
    rasterDesc.CullMode              = D3D11_CULL_BACK;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthClipEnable       = TRUE;
    rasterDesc.DepthBias             = 0;
    rasterDesc.SlopeScaledDepthBias  = 1.5f;
    rasterDesc.DepthBiasClamp        = 0.0f;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, &m_rasterizer)))
    {
        printf("[ShadowMap] 创建 RasterizerState 失败\n");
        return false;
    }

    // ---- 6. Shadow Pass 深度状态 ----
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable    = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;
    depthDesc.StencilEnable  = FALSE;
    if (FAILED(device->CreateDepthStencilState(&depthDesc, &m_depthState)))
    {
        printf("[ShadowMap] 创建 DepthStencilState 失败\n");
        return false;
    }

    return true;
}

void ShadowMap::UpdateLightCamera(const DirectionalLightActor& light,
                                  const XMFLOAT3& sceneCenter)
{
    // DirectionalLightActor::GetLightDirection 返回“表面 → 光源”的 L（已归一化），
    // 所以虚拟光源相机的位置在 center + L * distance。
    const XMFLOAT3 lightDir = light.GetLightDirection();
    const XMVECTOR L = XMLoadFloat3(&lightDir);
    const XMVECTOR center = XMLoadFloat3(&sceneCenter);
    const XMVECTOR eye = center + L * kLightDistance;

    // 光线接近垂直时，用 (0,1,0) 做 up 会退化，换成 Z 轴。
    const XMVECTOR up = (std::fabs(XMVectorGetY(L)) > 0.99f)
        ? XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
        : XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    const XMMATRIX view = XMMatrixLookAtLH(eye, center, up);
    const XMMATRIX proj = XMMatrixOrthographicLH(kOrthoSize, kOrthoSize, kNearZ, kFarZ);
    m_lightViewProj = view * proj;
}

void ShadowMap::Render(ID3D11DeviceContext* context,
                       const std::vector<const StaticMeshActor*>& casters,
                       const DirectionalLightActor& light,
                       const XMFLOAT3& sceneCenter)
{
    if (!context || !m_target.IsValid())
        return;

    UpdateLightCamera(light, sceneCenter);

    ShadowCB shadowCB = {};
    XMStoreFloat4x4(&shadowCB.LightViewProj, m_lightViewProj);
    shadowCB.Params = XMFLOAT4(1.0f / static_cast<float>(m_size), kDepthBias, 0.0f, 0.0f);
    context->UpdateSubresource(m_shadowCB.Get(), 0, nullptr, &shadowCB, 0, 0);

    // 关键：ShadowMap 马上要作为 DSV 使用，必须先把它从 PS 的 SRV 槽解绑。
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(1, 1, &nullSRV);

    // ---- 绑定 ShadowMap 作为唯一渲染目标（无颜色附件）----
    m_target.Bind(context);

    D3D11_VIEWPORT viewport = {};
    viewport.Width    = static_cast<float>(m_size);
    viewport.Height   = static_cast<float>(m_size);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    m_target.Clear(context, nullptr, 1.0f);   // 只清深度

    context->RSSetState(m_rasterizer.Get());
    context->OMSetDepthStencilState(m_depthState.Get(), 0);
    context->PSSetShader(nullptr, nullptr, 0);   // 只写深度，不需要 PS

    m_shader.BindVS(context);
    context->VSSetConstantBuffers(2, 1, m_shadowCB.GetAddressOf());

    const UINT stride = sizeof(Vertex), offset = 0;

    for (const StaticMeshActor* actor : casters)
    {
        if (!actor || !actor->MeshPtr)
            continue;

        ShadowObjectCB objectCB = {};
        XMStoreFloat4x4(&objectCB.World, actor->GetWorldMatrix());
        context->UpdateSubresource(m_objectCB.Get(), 0, nullptr, &objectCB, 0, 0);
        context->VSSetConstantBuffers(1, 1, m_objectCB.GetAddressOf());

        context->IASetVertexBuffers(0, 1, actor->MeshPtr->VertexBuffer.GetAddressOf(),
                                    &stride, &offset);
        context->IASetIndexBuffer(actor->MeshPtr->IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
        context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        context->DrawIndexed(actor->MeshPtr->IndexCount, 0, 0);
    }

    // 解绑 ShadowMap 的 DSV，主 Pass 会重新绑定后台缓冲。
    context->OMSetRenderTargets(0, nullptr, nullptr);
}

void ShadowMap::BindForSampling(ID3D11DeviceContext* context) const
{
    ID3D11ShaderResourceView* srv = m_target.GetDepthSRV();
    context->PSSetShaderResources(1, 1, &srv);
    context->PSSetSamplers(1, 1, m_sampler.GetAddressOf());
    context->PSSetConstantBuffers(2, 1, m_shadowCB.GetAddressOf());
}

void ShadowMap::Unbind(ID3D11DeviceContext* context) const
{
    ID3D11ShaderResourceView* nullSRV = nullptr;
    context->PSSetShaderResources(1, 1, &nullSRV);
}
