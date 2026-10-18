#include "PlanarMirror.h"
#include "RHI/D3D11State.h"

#include "Core/PathUtil.h"
#include "Scene/Actor.h"
#include "Scene/CameraViewInfo.h"

#include <cstddef>
#include <cstdio>
#include <string>

using namespace DirectX;

namespace
{
    // 与 shaders/Mirror.hlsl 的 cbuffer 布局一致
    struct MirrorCB
    {
        XMFLOAT4X4 MirrorViewProj;
        XMFLOAT4   MirrorTint;
    };
    static_assert(sizeof(MirrorCB) % 16 == 0, "MirrorCB 必须 16 字节对齐");
}

bool PlanarMirror::Initialize(ID3D11Device* device, UINT width, UINT height)
{
    if (!device || width == 0 || height == 0)
        return false;

    // ---- Reflection RT ----
    RenderTargetDesc desc;
    desc.Width           = width;
    desc.Height          = height;
    desc.ColorFormat     = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DepthFormat     = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.bCreateColorSRV = true;
    desc.bCreateDepthSRV = false;
    if (!m_reflectionTarget.Create(device, desc))
        return false;

    // ---- 镜面 shader（只用 POSITION；顶点缓冲仍按完整 Vertex 步长读取）----
    const D3D11_INPUT_ELEMENT_DESC layout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          static_cast<UINT>(offsetof(Vertex, Position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    const std::wstring shaderPath = PathUtil::ResolveContentPath(L"shaders/Mirror.hlsl");
    if (!m_mirrorShader.LoadFromFile(device, shaderPath.c_str(), layout, _countof(layout)))
        return false;

    // ---- 镜面常量 ----
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(MirrorCB);
    cbDesc.Usage     = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_mirrorCB)))
    {
        printf("[PlanarMirror] 创建 MirrorCB 失败\n");
        return false;
    }

    // ---- 反射纹理采样器 ----
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD         = 0.0f;
    samplerDesc.MaxLOD         = 0.0f;
    if (FAILED(device->CreateSamplerState(&samplerDesc, &m_sampler)))
    {
        printf("[PlanarMirror] 创建 SamplerState 失败\n");
        return false;
    }

    // ---- 反射 Pass 的光栅化状态 ----
    // 镜像矩阵行列式为负，三角形绕序翻转，所以 CULL_FRONT 才能画出正确的面。
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode              = D3D11_FILL_SOLID;
    rasterDesc.CullMode              = D3D11_CULL_FRONT;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthClipEnable       = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, &m_reflectionRasterizer)))
    {
        printf("[PlanarMirror] 创建反射 RasterizerState 失败\n");
        return false;
    }

    return true;
}

void PlanarMirror::OnResize(ID3D11Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        m_reflectionTarget.Release();
        return;
    }

    m_reflectionTarget.Resize(device, width, height);
}

bool PlanarMirror::BeginReflection(ID3D11DeviceContext* context,
                                   const float clearColor[4],
                                   const StaticMeshActor& mirrorActor,
                                   const CameraViewInfo& camera,
                                   float aspect)
{
    if (!context || !m_reflectionTarget.IsValid())
        return false;

    // ---- 镜面平面：局部 +Z 经过 Actor 旋转后的世界法线 ----
    const XMMATRIX world = mirrorActor.GetWorldMatrix();
    const XMVECTOR worldNormal = XMVector3Normalize(
        XMVector3TransformNormal(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), world));
    const XMVECTOR center = XMLoadFloat3(&mirrorActor.Location);

    // 平面方程：n·p + d = 0
    const float nx = XMVectorGetX(worldNormal);
    const float ny = XMVectorGetY(worldNormal);
    const float nz = XMVectorGetZ(worldNormal);
    const float d  = -XMVectorGetX(XMVector3Dot(worldNormal, center));

    // 行向量约定下，关于平面的仿射反射矩阵：
    //   p' = p - 2(n·p + d)n
    // 3x3 部分是 I - 2nn^T，平移放在第 4 行。
    // 注意不要直接用 XMMatrixReflect：它的齐次矩阵包含额外的 w 项，
    // 直接乘进 View 会污染投影的 w 分量，导致反射画面错位。
    XMMATRIX reflection;
    reflection.r[0] = XMVectorSet(1.0f - 2.0f * nx * nx, -2.0f * nx * ny,       -2.0f * nx * nz,       0.0f);
    reflection.r[1] = XMVectorSet(-2.0f * nx * ny,       1.0f - 2.0f * ny * ny, -2.0f * ny * nz,       0.0f);
    reflection.r[2] = XMVectorSet(-2.0f * nx * nz,       -2.0f * ny * nz,       1.0f - 2.0f * nz * nz, 0.0f);
    reflection.r[3] = XMVectorSet(-2.0f * nx * d,        -2.0f * ny * d,        -2.0f * nz * d,        1.0f);

    // 镜像场景：p * R * V * P（行向量约定）
    const XMMATRIX mainView = camera.GetViewMatrix();
    const XMMATRIX mainProj = camera.GetProjMatrix(aspect);
    m_mirrorViewProj = reflection * mainView * mainProj;

    // 镜像相机位置：用于反射 Pass 里的高光视线方向
    const XMVECTOR offset = XMLoadFloat3(&camera.Position) - center;
    const XMVECTOR reflectedOffset = XMVector3Reflect(offset, worldNormal);
    XMStoreFloat3(&m_mirrorCameraPos, center + reflectedOffset);

    // ---- 绑定 Reflection RT ----
    // 上一帧镜面绘制可能把 Reflection RT 的 SRV 留在 t2；
    // 它马上要作为 RTV 使用，先清空 PS 输入槽。
    UnbindPixelShaderResources(context, 0, 4);

    m_reflectionTarget.Bind(context);

    D3D11_VIEWPORT viewport = {};
    viewport.Width    = static_cast<float>(m_reflectionTarget.GetWidth());
    viewport.Height   = static_cast<float>(m_reflectionTarget.GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    m_reflectionTarget.Clear(context, clearColor, 1.0f);
    return true;
}

void PlanarMirror::EndReflection(ID3D11DeviceContext* context)
{
    if (context)
        context->OMSetRenderTargets(0, nullptr, nullptr);
}

void PlanarMirror::DrawMirror(ID3D11DeviceContext* context,
                              const StaticMeshActor& mirrorActor)
{
    if (!context || !mirrorActor.MeshPtr)
        return;

    MirrorCB cb = {};
    XMStoreFloat4x4(&cb.MirrorViewProj, m_mirrorViewProj);
    cb.MirrorTint = mirrorActor.MirrorTint;
    context->UpdateSubresource(m_mirrorCB.Get(), 0, nullptr, &cb, 0, 0);
    context->PSSetConstantBuffers(3, 1, m_mirrorCB.GetAddressOf());

    // VS 使用 b0 的主相机 ViewProj + b1 的 World；
    // PS 使用 b3 的镜像 ViewProj + t2 的反射纹理。
    m_mirrorShader.Bind(context);

    ID3D11ShaderResourceView* reflectionSRV = m_reflectionTarget.GetColorSRV();
    context->PSSetShaderResources(2, 1, &reflectionSRV);
    context->PSSetSamplers(2, 1, m_sampler.GetAddressOf());

    context->DrawIndexed(mirrorActor.MeshPtr->IndexCount, 0, 0);

    // Reflection RT 在后续 Pass 会再次作为 RTV 绑定；
    // 镜面画完立即解绑 t2，避免输入/输出资源冲突警告。
    UnbindPixelShaderResources(context, 2, 1);
}
