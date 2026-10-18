#include "Renderer.h"
#include "Core/PathUtil.h"
#include "Core/Window.h"
#include "RHI/D3D11State.h"
#include "Scene/Scene.h"
#include "Scene/Actor.h"
#include "Scene/Light.h"
#include "Scene/Texture.h"
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

using namespace DirectX;

namespace
{
    // 以下两个结构体必须与 shaders/Cube.hlsl 里的 cbuffer 布局逐字段对应。
    // 对齐规则：每个 float4/float4x4 占 16 字节，整个结构体大小必须是 16 的倍数。

    // 点光源的常量缓冲打包格式（与 Cube.hlsl 的 PointLightData 一致）：
    // 每个点光恰好两个 float4，数组天然满足 HLSL 的 16 字节元素对齐。
    struct PointLightCB
    {
        XMFLOAT4 PositionAndRange;   // xyz = 世界位置, w = 影响半径
        XMFLOAT4 Color;              // rgb = 光色, a = 强度
    };

    // 每帧数据：同一帧内所有物体共享（register b0）
    struct PerFrameCB
    {
        XMFLOAT4X4 ViewProj;         // 世界 → 裁剪
        XMFLOAT4   CameraPosition;   // xyz = 相机世界坐标（PS 算视线方向用）
        XMFLOAT4   LightDirection;   // xyz = 表面 → 光源（世界空间）
        XMFLOAT4   LightColor;       // rgb = 光色, a = 强度
        XMFLOAT4   AmbientColor;     // rgb = 环境光颜色
        PointLightCB PointLights[kMaxPointLights];   // M13 点光源列表（≈ 光源列表最小版）
        XMFLOAT4   LightCounts;      // x = 生效的点光源数量
    };
    static_assert(sizeof(PerFrameCB) % 16 == 0, "常量缓冲必须 16 字节对齐");

    // 每物体数据：每个 DrawCall 前更新（register b1）
    struct PerObjectCB
    {
        XMFLOAT4X4 World;            // 本地 → 世界
        XMFLOAT4X4 NormalMatrix;     // 世界矩阵的逆转置（正确变换法线）
        XMFLOAT4   BaseColor;        // 材质基础色（rgb + alpha）
        XMFLOAT4   MaterialParams;   // x = roughness, y = spec, z = tintStrength
        XMFLOAT4   ToonParams;       // xyz = 描边颜色, w = 描边宽度（M16，仅 Toon）
    };
    static_assert(sizeof(PerObjectCB) % 16 == 0, "常量缓冲必须 16 字节对齐");

}

bool Renderer::Initialize(Window& window)
{
    if (!m_rhi.Initialize(window.GetHandle(), window.GetWidth(), window.GetHeight()))
        return false;

    OnResize(window.GetWidth(), window.GetHeight());   // 记录初始宽高比

    // ---- 编译 shader + 建输入布局（顶点结构 ↔ HLSL 签名的"解释器"）----
    // 输入布局描述的是"顶点缓冲里的字节怎么解释"：
    //   POSITION / NORMAL / TANGENT / BITANGENT / TEXCOORD
    // 用 offsetof 而不是手写偏移，以后给 Vertex 加字段时不会悄悄错位。
    const D3D11_INPUT_ELEMENT_DESC layoutDesc[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          static_cast<UINT>(offsetof(Vertex, Position)),  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          static_cast<UINT>(offsetof(Vertex, Normal)),    D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TANGENT",  0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          static_cast<UINT>(offsetof(Vertex, Tangent)),   D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "BITANGENT",0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          static_cast<UINT>(offsetof(Vertex, Bitangent)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0,
          static_cast<UINT>(offsetof(Vertex, UV)),        D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    const std::wstring shaderPath = PathUtil::ResolveContentPath(L"shaders/Cube.hlsl");
    if (!m_cubeShader.LoadFromFile(m_rhi.GetDevice(), shaderPath.c_str(),
                                   layoutDesc, _countof(layoutDesc)))
        return false;

    // ---- M15 光源指示器 shader（unlit 纯色；只用 POSITION，步长仍按完整 Vertex）----
    const D3D11_INPUT_ELEMENT_DESC unlitLayout[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0,
          static_cast<UINT>(offsetof(Vertex, Position)), D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    const std::wstring unlitPath = PathUtil::ResolveContentPath(L"shaders/Unlit.hlsl");
    if (!m_unlitShader.LoadFromFile(m_rhi.GetDevice(), unlitPath.c_str(),
                                    unlitLayout, _countof(unlitLayout)))
        return false;

    // ---- M16 Toon shader：一个文件两对入口（本体 + 描边），输入布局与 Cube 共用 ----
    const std::wstring toonPath = PathUtil::ResolveContentPath(L"shaders/Toon.hlsl");
    if (!m_toonShader.LoadFromFile(m_rhi.GetDevice(), toonPath.c_str(),
                                   layoutDesc, _countof(layoutDesc)))
        return false;   // 默认入口 VSMain/PSMain = 本体
    if (!m_toonOutlineShader.LoadFromFile(m_rhi.GetDevice(), toonPath.c_str(),
                                          layoutDesc, _countof(layoutDesc),
                                          "OutlineVSMain", "OutlinePSMain"))
        return false;

    // ---- 常量缓冲：帧级数据每帧写一次，物体级数据每次 DrawCall 前写 ----
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.Usage     = D3D11_USAGE_DEFAULT;   // GPU 读写、CPU 用 UpdateSubresource 写
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    cbDesc.ByteWidth = sizeof(PerFrameCB);
    if (FAILED(m_rhi.GetDevice()->CreateBuffer(&cbDesc, nullptr, &m_perFrameCB)))
    {
        printf("[Renderer] 创建每帧常量缓冲失败\n");
        return false;
    }

    cbDesc.ByteWidth = sizeof(PerObjectCB);
    if (FAILED(m_rhi.GetDevice()->CreateBuffer(&cbDesc, nullptr, &m_perObjectCB)))
    {
        printf("[Renderer] 创建每物体常量缓冲失败\n");
        return false;
    }

    // ---- 光栅化状态：实体 / 线框 两套，运行时切换 ----
    // 与默认状态等价的部分显式写出，便于看清每个字段的含义
    D3D11_RASTERIZER_DESC rsDesc = {};
    rsDesc.FillMode              = D3D11_FILL_SOLID;      // 实体填充
    rsDesc.CullMode              = D3D11_CULL_BACK;       // 剔除背面
    rsDesc.FrontCounterClockwise = FALSE;                 // 顺时针 = 正面
    rsDesc.DepthClipEnable       = TRUE;
    if (FAILED(m_rhi.GetDevice()->CreateRasterizerState(&rsDesc, &m_rsSolid)))
        return false;

    // 线框：不剔除（正反面边线都画，12 条边 + 6 条对角线全可见，避免深度歧义）
    rsDesc.FillMode = D3D11_FILL_WIREFRAME;
    rsDesc.CullMode = D3D11_CULL_NONE;
    if (FAILED(m_rhi.GetDevice()->CreateRasterizerState(&rsDesc, &m_rsWireframe)))
        return false;

    // M16 描边：实体 + CULL_FRONT（只画外扩 hull 的背面）
    rsDesc.FillMode = D3D11_FILL_SOLID;
    rsDesc.CullMode = D3D11_CULL_FRONT;
    if (FAILED(m_rhi.GetDevice()->CreateRasterizerState(&rsDesc, &m_rsOutline)))
        return false;

    // ---- 纹理采样器 ----
    // Filter：各向异性过滤，配合完整 mip 链改善掠射角/远距离的纹理走样
    // Address：UV 超出 [0,1] 时重复纹理；立方体 UV 正好在 [0,1]，用 WRAP 是为了以后做平铺地面
    D3D11_SAMPLER_DESC samplerDesc = {};
    samplerDesc.Filter         = D3D11_FILTER_ANISOTROPIC;
    samplerDesc.MaxAnisotropy  = 8;   // 8× 已足够改善掠射角；16× 更贵
    samplerDesc.AddressU       = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressV       = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.AddressW       = D3D11_TEXTURE_ADDRESS_WRAP;
    samplerDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    samplerDesc.MinLOD         = 0.0f;
    samplerDesc.MaxLOD         = D3D11_FLOAT32_MAX;
    if (FAILED(m_rhi.GetDevice()->CreateSamplerState(&samplerDesc, &m_linearSampler)))
    {
        printf("[Renderer] 创建 SamplerState 失败\n");
        return false;
    }

    // ---- 混合状态：不透明 / Alpha 混合 ----
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.AlphaToCoverageEnable  = FALSE;
    blendDesc.IndependentBlendEnable = FALSE;

    D3D11_RENDER_TARGET_BLEND_DESC& rtBlend = blendDesc.RenderTarget[0];
    rtBlend.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

    // 不透明：关闭混合，源颜色直接覆盖目标颜色
    rtBlend.BlendEnable    = FALSE;
    rtBlend.SrcBlend       = D3D11_BLEND_ONE;
    rtBlend.DestBlend      = D3D11_BLEND_ZERO;
    rtBlend.BlendOp        = D3D11_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha  = D3D11_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D11_BLEND_ZERO;
    rtBlend.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    if (FAILED(m_rhi.GetDevice()->CreateBlendState(&blendDesc, &m_blendOpaque)))
    {
        printf("[Renderer] 创建不透明 BlendState 失败\n");
        return false;
    }

    // 半透明：src.rgb * src.a + dst.rgb * (1 - src.a)
    // Alpha 通道用 One / InvSrcAlpha，便于以后做需要累积 alpha 的效果。
    rtBlend.BlendEnable    = TRUE;
    rtBlend.SrcBlend       = D3D11_BLEND_SRC_ALPHA;
    rtBlend.DestBlend      = D3D11_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOp        = D3D11_BLEND_OP_ADD;
    rtBlend.SrcBlendAlpha  = D3D11_BLEND_ONE;
    rtBlend.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    rtBlend.BlendOpAlpha   = D3D11_BLEND_OP_ADD;
    if (FAILED(m_rhi.GetDevice()->CreateBlendState(&blendDesc, &m_blendAlpha)))
    {
        printf("[Renderer] 创建半透明 BlendState 失败\n");
        return false;
    }

    // ---- 深度状态：不透明写深度 / 半透明只读深度 ----
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable    = TRUE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depthDesc.DepthFunc      = D3D11_COMPARISON_LESS_EQUAL;   // 与 ClearDepth=1.0 约定一致
    depthDesc.StencilEnable  = FALSE;
    if (FAILED(m_rhi.GetDevice()->CreateDepthStencilState(&depthDesc, &m_depthOpaque)))
    {
        printf("[Renderer] 创建不透明 DepthStencilState 失败\n");
        return false;
    }

    // 半透明物体参与深度测试（被不透明物体遮挡时不画），
    // 但不写深度（否则会挡住它后面的其他半透明物体）。
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    if (FAILED(m_rhi.GetDevice()->CreateDepthStencilState(&depthDesc, &m_depthTransparent)))
    {
        printf("[Renderer] 创建半透明 DepthStencilState 失败\n");
        return false;
    }

    // M8.5：半透明 + 写 stencil=1。
    // 深度测试和透明 Pass 一样，但通过深度测试的像素会把模板写成 1，
    // 后处理据此保护 CubeB 的屏幕区域。
    D3D11_DEPTH_STENCIL_DESC stencilDesc = depthDesc;
    stencilDesc.StencilEnable            = TRUE;
    stencilDesc.StencilReadMask          = 0xFF;
    stencilDesc.StencilWriteMask         = 0xFF;
    stencilDesc.FrontFace.StencilFunc    = D3D11_COMPARISON_ALWAYS;
    stencilDesc.FrontFace.StencilPassOp  = D3D11_STENCIL_OP_REPLACE;
    stencilDesc.FrontFace.StencilFailOp  = D3D11_STENCIL_OP_KEEP;
    stencilDesc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_KEEP;
    stencilDesc.BackFace = stencilDesc.FrontFace;
    if (FAILED(m_rhi.GetDevice()->CreateDepthStencilState(&stencilDesc, &m_depthTransparentStencil)))
    {
        printf("[Renderer] 创建 Stencil 写入 DepthStencilState 失败\n");
        return false;
    }

    // ---- M7 阴影资源 ----
    if (!m_shadowMap.Initialize(m_rhi.GetDevice()))
        return false;

    // ---- M8 离屏后处理资源 ----
    if (!m_postProcess.Initialize(m_rhi.GetDevice(), window.GetWidth(), window.GetHeight()))
        return false;

    // ---- M10 平面镜反射资源 ----
    if (!m_planarMirror.Initialize(m_rhi.GetDevice(), window.GetWidth(), window.GetHeight()))
        return false;

    return true;
}

void Renderer::OnResize(int width, int height)
{
    // 缩放会释放并重建 SceneColor / Reflection RT。先把 PS 输入槽清空，
    // 避免旧 SRV 仍绑定在即将被释放或重建的纹理上。
    UnbindPixelShaderResources(m_rhi.GetContext(), 0, 4);

    m_rhi.OnResize(width, height);

    if (width > 0 && height > 0)
    {
        m_postProcess.OnResize(m_rhi.GetDevice(), static_cast<UINT>(width), static_cast<UINT>(height));
        m_planarMirror.OnResize(m_rhi.GetDevice(), static_cast<UINT>(width), static_cast<UINT>(height));
    }

    if (height > 0)
        m_aspect = static_cast<float>(width) / static_cast<float>(height);
}

void Renderer::UpdatePerFrameConstants(ID3D11DeviceContext* context,
                                       const XMMATRIX& viewProj,
                                       const XMFLOAT3& cameraPos,
                                       const DirectionalLightActor& dirLight,
                                       const std::vector<const PointLightActor*>& pointLights)
{
    // 存储约定：DirectXMath 行主序、HLSL cbuffer 默认列主序，两者正好互为转置——
    // 直接上传即可，HLSL 看到的矩阵自动是转置，配合 mul(矩阵, 向量) 正确。
    PerFrameCB frameCB = {};
    XMStoreFloat4x4(&frameCB.ViewProj, viewProj);
    frameCB.CameraPosition = XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);
    const XMFLOAT3 lightDir = dirLight.GetLightDirection();
    frameCB.LightDirection = XMFLOAT4(lightDir.x, lightDir.y, lightDir.z, 0.0f);
    frameCB.LightColor     = dirLight.Color;
    frameCB.AmbientColor   = dirLight.Ambient;

    // 点光源列表：打包成 (pos.xyz, range) + (rgb, 强度)。
    // 超出上限的灯被忽略；未启用的槽位保持全零（强度 = 0 → 无贡献）。
    int count = static_cast<int>(pointLights.size());
    if (count > kMaxPointLights)
        count = kMaxPointLights;
    for (int i = 0; i < count; ++i)
    {
        const PointLightActor* pl = pointLights[static_cast<size_t>(i)];
        frameCB.PointLights[i].PositionAndRange =
            XMFLOAT4(pl->Location.x, pl->Location.y, pl->Location.z, pl->Range);
        frameCB.PointLights[i].Color = pl->Color;
    }
    frameCB.LightCounts = XMFLOAT4(static_cast<float>(count), 0.0f, 0.0f, 0.0f);

    context->UpdateSubresource(m_perFrameCB.Get(), 0, nullptr, &frameCB, 0, 0);
    context->VSSetConstantBuffers(0, 1, m_perFrameCB.GetAddressOf());
    context->PSSetConstantBuffers(0, 1, m_perFrameCB.GetAddressOf());
}

void Renderer::CyclePostProcessMode()
{
    switch (m_postProcessMode)
    {
    case EPostProcessMode::Off:
        m_postProcessMode = EPostProcessMode::Grayscale;
        break;
    case EPostProcessMode::Grayscale:
        m_postProcessMode = EPostProcessMode::MaskedDarken;
        break;
    case EPostProcessMode::MaskedDarken:
    default:
        m_postProcessMode = EPostProcessMode::Off;
        break;
    }
}

const char* Renderer::GetPostProcessModeName() const
{
    switch (m_postProcessMode)
    {
    case EPostProcessMode::Grayscale:     return "置灰 + 暗角";
    case EPostProcessMode::MaskedDarken:  return "暗化（CubeB 保持原色）";
    case EPostProcessMode::Off:
    default:                              return "关";
    }
}

void Renderer::Render(const Scene& scene)
{
    ID3D11DeviceContext* ctx = m_rhi.GetContext();

    // 视角只从 PCM 的缓存取（渲染流程不接触任何具体相机对象）
    const CameraViewInfo& view = scene.Cameras.GetViewInfo();
    const XMMATRIX viewProj = view.GetViewMatrix() * view.GetProjMatrix(m_aspect);

    // 渲染收集：从场景里筛出可渲染物（按 Opaque / Translucent 分流）与光源
    std::vector<const StaticMeshActor*> opaqueActors;
    std::vector<const StaticMeshActor*> transparentActors;
    const StaticMeshActor* mirrorActor = nullptr;
    const DirectionalLightActor* dirLight = nullptr;
    std::vector<const PointLightActor*> pointLights;
    std::vector<const LightActor*> indicatorLights;   // M15：需要画位置指示器的光源

    for (const auto& actor : scene.GetActors())
    {
        if (const auto* visual = dynamic_cast<const StaticMeshActor*>(actor.get()))
        {
            if (visual->bIsMirror)
                mirrorActor = visual;

            if (visual->MaterialData.bIsTransparent)
                transparentActors.push_back(visual);
            else
                opaqueActors.push_back(visual);
        }
        else if (const auto* dir = dynamic_cast<const DirectionalLightActor*>(actor.get()))
        {
            // 当前 ShadowMap / 光照常量只支持一盏平行光；多余的忽略
            if (!dirLight)
                dirLight = dir;
        }
        else if (const auto* point = dynamic_cast<const PointLightActor*>(actor.get()))
        {
            pointLights.push_back(point);
        }

        // 光源指示器与光源类型无关（平行光也可开，但位置对它无意义）
        if (const auto* light = dynamic_cast<const LightActor*>(actor.get()))
        {
            if (light->bShowIndicator)
                indicatorLights.push_back(light);
        }
    }

    // 场景里没有平行光时用默认参数兜底，保证 ShadowMap / 常量上传路径不断链。
    static DirectionalLightActor s_defaultSun;
    if (!dirLight)
    {
        static bool s_warnedNoSun = false;
        if (!s_warnedNoSun)
        {
            printf("[Renderer] 场景中没有 DirectionalLightActor，使用默认平行光兜底。\n");
            s_warnedNoSun = true;
        }
        dirLight = &s_defaultSun;
    }

    // 半透明排序要在主 Pass 和反射 Pass 之前完成。
    const XMVECTOR cameraPos = XMLoadFloat3(&view.Position);
    const auto distanceSqToCamera = [&](const StaticMeshActor* actor)
    {
        const XMVECTOR objectPos = XMLoadFloat3(&actor->Location);
        return XMVectorGetX(XMVector3LengthSq(XMVectorSubtract(objectPos, cameraPos)));
    };

    std::sort(transparentActors.begin(), transparentActors.end(),
              [&](const StaticMeshActor* a, const StaticMeshActor* b)
              {
                  return distanceSqToCamera(a) > distanceSqToCamera(b);
              });

    // ---- ① Shadow Pass：从光源视角渲染不透明 caster 的深度 ----
    std::vector<const StaticMeshActor*> shadowCasters;
    for (const StaticMeshActor* actor : opaqueActors)
    {
        if (actor->bCastShadow)
            shadowCasters.push_back(actor);
    }

    const XMFLOAT3 sceneCenter = { 0.0f, 0.0f, 0.0f };
    m_shadowMap.Render(ctx, shadowCasters, *dirLight, sceneCenter);

    // ---- ② Reflection Pass：镜像相机渲染到 Reflection RT ----
    // 镜面自身不进入这个 Pass，避免反馈/递归。
    if (mirrorActor && m_planarMirror.IsValid())
    {
        if (m_planarMirror.BeginReflection(ctx, m_clearColor, *mirrorActor, view, m_aspect))
        {
            UpdatePerFrameConstants(ctx,
                                    m_planarMirror.GetMirrorViewProj(),
                                    m_planarMirror.GetMirrorCameraPos(),
                                    *dirLight, pointLights);
            m_shadowMap.BindForSampling(ctx);

            // Opaque（跳过镜面）
            ctx->OMSetBlendState(m_blendOpaque.Get(), nullptr, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(m_depthOpaque.Get(), 0);
            for (const StaticMeshActor* actor : opaqueActors)
            {
                if (!actor->bIsMirror)
                    DrawObject(*actor, true);
            }
            DrawLightIndicators(indicatorLights, true);

            // Translucent：反射 Pass 不需要 stencil 蒙版，统一用普通透明深度状态
            ctx->OMSetBlendState(m_blendAlpha.Get(), nullptr, 0xFFFFFFFF);
            ctx->OMSetDepthStencilState(m_depthTransparent.Get(), 0);
            for (const StaticMeshActor* actor : transparentActors)
                DrawObject(*actor, true);

            m_planarMirror.EndReflection(ctx);
        }
    }

    // ---- ③ 主 Pass：先绑定 SceneColor（颜色 + 深度/模板）----
    if (!m_postProcess.BeginScene(ctx, m_clearColor))
        return;   // 最小化等导致 SceneColor 无效

    UpdatePerFrameConstants(ctx, viewProj, view.Position, *dirLight, pointLights);
    m_shadowMap.BindForSampling(ctx);

    // Opaque：这里包含镜面；DrawObject 会用专门的镜面 shader 处理
    ctx->OMSetBlendState(m_blendOpaque.Get(), nullptr, 0xFFFFFFFF);
    ctx->OMSetDepthStencilState(m_depthOpaque.Get(), 0);
    for (const StaticMeshActor* actor : opaqueActors)
        DrawObject(*actor, false);
    DrawLightIndicators(indicatorLights, false);

    // Translucent：CubeB 写 stencil=1，供后处理蒙版使用
    ctx->OMSetBlendState(m_blendAlpha.Get(), nullptr, 0xFFFFFFFF);
    for (const StaticMeshActor* actor : transparentActors)
    {
        if (actor->bWriteStencil)
            ctx->OMSetDepthStencilState(m_depthTransparentStencil.Get(), 1);
        else
            ctx->OMSetDepthStencilState(m_depthTransparent.Get(), 0);

        DrawObject(*actor, false);
    }

    // 解绑 ShadowMap SRV，避免下一帧 Shadow Pass 把同一资源同时作为 DSV/SRV 绑定。
    m_shadowMap.Unbind(ctx);

    // ---- ④ 后处理：SceneColor → 后台缓冲 ----
    m_postProcess.EndScene(ctx,
                           m_rhi.GetBackBufferRTV(),
                           static_cast<UINT>(m_rhi.GetWidth()),
                           static_cast<UINT>(m_rhi.GetHeight()),
                           m_postProcessMode,
                           m_fxaaEnabled);

    m_rhi.Present();
}

void Renderer::DrawObject(const StaticMeshActor& obj, bool reflectionPass)
{
    ID3D11DeviceContext* ctx = m_rhi.GetContext();

    // ---- ① 物体级数据：世界矩阵、法线矩阵、材质参数 ----
    const XMMATRIX world = obj.GetWorldMatrix();

    // 法线矩阵 = 世界矩阵的逆转置。
    // 法线不能像位置那样直接用 world 变换：遇到非均匀缩放（比如 x 拉长、y 压扁）
    // 法线会被"拉歪"，不再垂直于表面。逆转置矩阵可以抵消这种畸变。
    const XMMATRIX normalMatrix = XMMatrixTranspose(XMMatrixInverse(nullptr, world));

    PerObjectCB cb = {};
    XMStoreFloat4x4(&cb.World, world);
    XMStoreFloat4x4(&cb.NormalMatrix, normalMatrix);
    cb.BaseColor      = obj.MaterialData.BaseColor;
    cb.MaterialParams = XMFLOAT4(obj.MaterialData.Roughness,
                                 obj.MaterialData.SpecularIntensity,
                                 obj.MaterialData.TintStrength,
                                 0.0f);
    cb.ToonParams     = XMFLOAT4(obj.MaterialData.OutlineColor.x,
                                 obj.MaterialData.OutlineColor.y,
                                 obj.MaterialData.OutlineColor.z,
                                 obj.MaterialData.OutlineWidth);
    ctx->UpdateSubresource(m_perObjectCB.Get(), 0, nullptr, &cb, 0, 0);

    // b1 = 物体级常量；镜面 shader 也只需要其中的 World。
    ctx->VSSetConstantBuffers(1, 1, m_perObjectCB.GetAddressOf());
    ctx->PSSetConstantBuffers(1, 1, m_perObjectCB.GetAddressOf());

    if (!obj.MeshPtr)
        return;

    // ---- ② IA：镜面和普通物体共用同一份网格数据 ----
    const UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, obj.MeshPtr->VertexBuffer.GetAddressOf(), &stride, &offset);
    ctx->IASetIndexBuffer(obj.MeshPtr->IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 反射 Pass 的视图矩阵行列式为负，绕序翻转，因此使用 CULL_FRONT 状态。
    if (m_wireframe)
        ctx->RSSetState(m_rsWireframe.Get());
    else if (reflectionPass)
        ctx->RSSetState(m_planarMirror.GetReflectionRasterizer());
    else
        ctx->RSSetState(m_rsSolid.Get());

    // ---- ③ 镜面：走专门的 Mirror shader ----
    if (obj.bIsMirror)
    {
        // 镜面自身永远不进入反射 Pass，避免递归。
        if (reflectionPass)
            return;

        m_planarMirror.DrawMirror(ctx, obj);
        return;
    }

    // ---- ④ 着色模型分流：Toon 走双 Pass（描边 + 本体）----
    if (obj.MaterialData.ShadingModel == EShadingModel::Toon)
    {
        DrawToonObject(obj, reflectionPass);
        return;
    }

    // ---- ⑤ 普通 Lit 物体：主光照 shader ----
    m_cubeShader.Bind(ctx);   // 输入布局 + VS + PS

    // 纹理资源：t0 = 材质贴图，s0 = 采样器。
    // 约定所有可渲染材质的 TexturePtr 非空；不贴图的材质指向内置默认白纹理。
    ID3D11ShaderResourceView* srv = obj.MaterialData.TexturePtr
        ? obj.MaterialData.TexturePtr->SRV.Get()
        : nullptr;
    ctx->PSSetShaderResources(0, 1, &srv);

    // t3 = 切线空间法线贴图；未设置时应指向内置默认平坦法线。
    ID3D11ShaderResourceView* normalSRV = obj.MaterialData.NormalTexturePtr
        ? obj.MaterialData.NormalTexturePtr->SRV.Get()
        : nullptr;
    ctx->PSSetShaderResources(3, 1, &normalSRV);

    ctx->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());

    ctx->DrawIndexed(obj.MeshPtr->IndexCount, 0, 0);
}

void Renderer::DrawToonObject(const StaticMeshActor& obj, bool reflectionPass)
{
    ID3D11DeviceContext* ctx = m_rhi.GetContext();
    // PerObjectCB（含 ToonParams）、VB/IB、基础光栅化状态已由 DrawObject 备好。

    // 贴图与 Lit 路径同约定：t0 = 材质贴图，s0 = 采样器。
    ID3D11ShaderResourceView* srv = obj.MaterialData.TexturePtr
        ? obj.MaterialData.TexturePtr->SRV.Get()
        : nullptr;
    ctx->PSSetShaderResources(0, 1, &srv);
    ctx->PSSetSamplers(0, 1, m_linearSampler.GetAddressOf());

    // 反射 Pass 绕序翻转：本体与描边的剔除模式要互换角色——
    // 本体平时 CULL_BACK（m_rsSolid），反射里 CULL_FRONT（反射光栅化状态）；
    // 描边平时 CULL_FRONT（m_rsOutline），反射里换回 CULL_BACK。
    ID3D11RasterizerState* bodyState    = reflectionPass
        ? m_planarMirror.GetReflectionRasterizer() : m_rsSolid.Get();
    ID3D11RasterizerState* outlineState = reflectionPass
        ? m_rsSolid.Get() : m_rsOutline.Get();
    if (m_wireframe)
    {
        bodyState    = m_rsWireframe.Get();
        outlineState = m_rsWireframe.Get();
    }

    // Pass 1：描边（背面外扩 hull，纯色）
    m_toonOutlineShader.Bind(ctx);
    ctx->RSSetState(outlineState);
    ctx->DrawIndexed(obj.MeshPtr->IndexCount, 0, 0);

    // Pass 2：本体（量化光照）
    m_toonShader.Bind(ctx);
    ctx->RSSetState(bodyState);
    ctx->DrawIndexed(obj.MeshPtr->IndexCount, 0, 0);
}

void Renderer::DrawLightIndicators(const std::vector<const LightActor*>& lights,
                                   bool reflectionPass)
{
    if (!m_indicatorMesh || lights.empty())
        return;

    ID3D11DeviceContext* ctx = m_rhi.GetContext();

    // unlit shader 不采样纹理、不读光照常量；Blend/DepthStencil 沿用 Opaque Pass 的状态。
    // 指示器不是 StaticMeshActor，永远不会进入 shadowCasters 列表，
    // 因此天然不投阴影（“不遮挡光”）；不透明深度正常（可遮挡/被遮挡）。
    m_unlitShader.Bind(ctx);

    const UINT stride = sizeof(Vertex), offset = 0;
    ctx->IASetVertexBuffers(0, 1, m_indicatorMesh->VertexBuffer.GetAddressOf(),
                            &stride, &offset);
    ctx->IASetIndexBuffer(m_indicatorMesh->IndexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // 光栅化状态与普通不透明物体一致（反射 Pass 需要翻转绕序）
    if (m_wireframe)
        ctx->RSSetState(m_rsWireframe.Get());
    else if (reflectionPass)
        ctx->RSSetState(m_planarMirror.GetReflectionRasterizer());
    else
        ctx->RSSetState(m_rsSolid.Get());

    for (const LightActor* light : lights)
    {
        // 世界矩阵只有缩放 + 平移：指示器是定位标记，不随光源朝向旋转。
        const float s = light->IndicatorSize;
        const XMMATRIX world = XMMatrixScaling(s, s, s) *
            XMMatrixTranslation(light->Location.x, light->Location.y, light->Location.z);

        // unlit shader 只读 gWorld / gBaseColor，其余字段保持零即可。
        // 颜色取光源光色的 RGB；alpha 固定 1（强度不参与显示，
        // 避免强度 >1 时被 LDR 钳成白色，丢失光源的颜色信息）。
        PerObjectCB cb = {};
        XMStoreFloat4x4(&cb.World, world);
        cb.BaseColor = XMFLOAT4(light->Color.x, light->Color.y, light->Color.z, 1.0f);
        ctx->UpdateSubresource(m_perObjectCB.Get(), 0, nullptr, &cb, 0, 0);
        ctx->VSSetConstantBuffers(1, 1, m_perObjectCB.GetAddressOf());
        ctx->PSSetConstantBuffers(1, 1, m_perObjectCB.GetAddressOf());

        ctx->DrawIndexed(m_indicatorMesh->IndexCount, 0, 0);
    }
}
