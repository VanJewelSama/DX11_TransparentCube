#include "PostProcess.h"
#include "RHI/D3D11State.h"

#include <cstdio>

using namespace DirectX;

namespace
{
    // 与 shaders/PostProcess.hlsl 的 cbuffer 布局一致
    struct PostProcessCB
    {
        XMFLOAT4 Params;   // x 未用, y = 暗角强度, z = 整体压暗系数
    };
    static_assert(sizeof(PostProcessCB) % 16 == 0, "PostProcessCB 必须 16 字节对齐");

    // 与 shaders/FXAA.hlsl 的 cbuffer 布局一致
    struct FXAACB
    {
        XMFLOAT4 Params;   // x = 1/width, y = 1/height, z = 边缘阈值
    };
    static_assert(sizeof(FXAACB) % 16 == 0, "FXAACB 必须 16 字节对齐");
}

bool PostProcess::Initialize(ID3D11Device* device, UINT width, UINT height)
{
    if (!device || width == 0 || height == 0)
        return false;

    // ---- SceneColor：颜色 + 深度 ----
    // 当前是 LDR；以后做 HDR/Bloom 时改成 R16G16B16A16_FLOAT。
    RenderTargetDesc desc;
    desc.Width           = width;
    desc.Height          = height;
    desc.ColorFormat     = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.DepthFormat     = DXGI_FORMAT_D24_UNORM_S8_UINT;
    desc.bCreateColorSRV = true;
    desc.bCreateDepthSRV = false;
    if (!m_sceneColor.Create(device, desc))
        return false;

    // ---- FXAA 输出 RT：颜色即可，不需要深度 ----
    RenderTargetDesc fxaaDesc;
    fxaaDesc.Width           = width;
    fxaaDesc.Height          = height;
    fxaaDesc.ColorFormat     = DXGI_FORMAT_R8G8B8A8_UNORM;
    fxaaDesc.DepthFormat     = DXGI_FORMAT_UNKNOWN;
    fxaaDesc.bCreateColorSRV = true;
    if (!m_fxaaTarget.Create(device, fxaaDesc))
        return false;

    // ---- 全屏 PS 变体 ----
    // 同一份 PostProcess.hlsl 里三个 PS 入口；CPU 按模式绑定不同变体。
    if (!m_copyPass.Initialize(device, L"shaders/PostProcess.hlsl", "CopyPS"))
        return false;
    if (!m_grayscalePass.Initialize(device, L"shaders/PostProcess.hlsl", "GrayscalePS"))
        return false;
    if (!m_darkenPass.Initialize(device, L"shaders/PostProcess.hlsl", "MaskedDarkenPS"))
        return false;
    if (!m_fxaaPass.Initialize(device, L"shaders/FXAA.hlsl", "FXAAPSMain"))
        return false;

    // ---- 效果参数常量缓冲 ----
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(PostProcessCB);
    cbDesc.Usage     = D3D11_USAGE_DEFAULT;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_paramsCB)))
    {
        printf("[PostProcess] 创建参数常量缓冲失败\n");
        return false;
    }

    cbDesc.ByteWidth = sizeof(FXAACB);
    if (FAILED(device->CreateBuffer(&cbDesc, nullptr, &m_fxaaCB)))
    {
        printf("[PostProcess] 创建 FXAA 常量缓冲失败\n");
        return false;
    }

    // ---- 全屏 Pass 专用状态 ----
    // 关闭混合：后处理结果直接覆盖后台缓冲。
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable           = FALSE;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blendDesc, &m_blendDisabled)))
    {
        printf("[PostProcess] 创建 BlendState 失败\n");
        return false;
    }

    // 全屏拷贝/灰度：深度和模板都关闭。
    D3D11_DEPTH_STENCIL_DESC depthDesc = {};
    depthDesc.DepthEnable    = FALSE;
    depthDesc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
    depthDesc.DepthFunc      = D3D11_COMPARISON_ALWAYS;
    depthDesc.StencilEnable  = FALSE;
    if (FAILED(device->CreateDepthStencilState(&depthDesc, &m_depthCopy)))
    {
        printf("[PostProcess] 创建 Copy DepthStencilState 失败\n");
        return false;
    }

    // 模板蒙版效果：深度关闭，模板测试 NOT_EQUAL 1。
    // 即 stencil == 1 的像素（CubeB）不写效果，保留 Copy Pass 的原色。
    D3D11_DEPTH_STENCIL_DESC maskDesc = depthDesc;
    maskDesc.StencilEnable      = TRUE;
    maskDesc.StencilReadMask    = 0xFF;
    maskDesc.StencilWriteMask   = 0x00;   // 只测试，不修改模板
    maskDesc.FrontFace.StencilFunc           = D3D11_COMPARISON_NOT_EQUAL;
    maskDesc.FrontFace.StencilPassOp         = D3D11_STENCIL_OP_KEEP;
    maskDesc.FrontFace.StencilFailOp         = D3D11_STENCIL_OP_KEEP;
    maskDesc.FrontFace.StencilDepthFailOp    = D3D11_STENCIL_OP_KEEP;
    maskDesc.BackFace = maskDesc.FrontFace;
    if (FAILED(device->CreateDepthStencilState(&maskDesc, &m_depthMaskedEffect)))
    {
        printf("[PostProcess] 创建 Masked DepthStencilState 失败\n");
        return false;
    }

    // 全屏三角形不需要按绕序剔除；线框模式也不能影响后处理，所以固定为实体 + CULL_NONE。
    D3D11_RASTERIZER_DESC rasterDesc = {};
    rasterDesc.FillMode              = D3D11_FILL_SOLID;
    rasterDesc.CullMode              = D3D11_CULL_NONE;
    rasterDesc.FrontCounterClockwise = FALSE;
    rasterDesc.DepthClipEnable       = FALSE;
    if (FAILED(device->CreateRasterizerState(&rasterDesc, &m_rasterizer)))
    {
        printf("[PostProcess] 创建 RasterizerState 失败\n");
        return false;
    }

    return true;
}

void PostProcess::OnResize(ID3D11Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        m_sceneColor.Release();
        m_fxaaTarget.Release();
        return;
    }

    m_sceneColor.Resize(device, width, height);
    m_fxaaTarget.Resize(device, width, height);
}

bool PostProcess::BeginScene(ID3D11DeviceContext* context, const float clearColor[4])
{
    if (!context || !m_sceneColor.IsValid())
        return false;

    // SceneColor 马上要作为 RTV；上一帧后处理可能把它的 SRV 留在 t0。
    // 同一资源不能同时作为输入和输出，先清空 PS 输入槽。
    UnbindPixelShaderResources(context, 0, 4);

    m_sceneColor.Bind(context);

    D3D11_VIEWPORT viewport = {};
    viewport.Width    = static_cast<float>(m_sceneColor.GetWidth());
    viewport.Height   = static_cast<float>(m_sceneColor.GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    m_sceneColor.Clear(context, clearColor, 1.0f);
    return true;
}

void PostProcess::EndScene(ID3D11DeviceContext* context,
                           ID3D11RenderTargetView* backBufferRTV,
                           UINT width, UINT height,
                           EPostProcessMode mode,
                           bool fxaaEnabled)
{
    if (!context || !backBufferRTV || !m_sceneColor.IsValid())
        return;

    // 后处理颜色源：默认 SceneColor；FXAA 开启时先写到 FXAA 目标。
    ID3D11ShaderResourceView* colorSRV = m_sceneColor.GetColorSRV();

    // ---- 可选：FXAA Pass（SceneColor → FXAA Target）----
    if (fxaaEnabled && m_fxaaTarget.IsValid())
    {
        // FXAA Target 即将被写成 RTV；它和 SceneColor 都可能还挂在 PS 输入槽。
        UnbindPixelShaderResources(context, 0, 4);

        ID3D11RenderTargetView* fxaaRTV = m_fxaaTarget.GetRTV();
        context->OMSetRenderTargets(1, &fxaaRTV, nullptr);

        D3D11_VIEWPORT fxaaViewport = {};
        fxaaViewport.Width    = static_cast<float>(width);
        fxaaViewport.Height   = static_cast<float>(height);
        fxaaViewport.MinDepth = 0.0f;
        fxaaViewport.MaxDepth = 1.0f;
        context->RSSetViewports(1, &fxaaViewport);

        context->OMSetBlendState(m_blendDisabled.Get(), nullptr, 0xFFFFFFFF);
        context->OMSetDepthStencilState(m_depthCopy.Get(), 0);
        context->RSSetState(m_rasterizer.Get());

        FXAACB fxaaCB = {};
        fxaaCB.Params = XMFLOAT4(1.0f / static_cast<float>(width),
                                 1.0f / static_cast<float>(height),
                                 0.063f,  // 边缘阈值，越大越少处理
                                 0.0f);
        context->UpdateSubresource(m_fxaaCB.Get(), 0, nullptr, &fxaaCB, 0, 0);
        context->PSSetConstantBuffers(0, 1, m_fxaaCB.GetAddressOf());

        m_fxaaPass.Bind(context);
        m_fxaaPass.Draw(context, m_sceneColor.GetColorSRV());

        colorSRV = m_fxaaTarget.GetColorSRV();
    }

    // ---- 颜色后处理：输出到后台缓冲 ----
    // 绑定 SceneColor 的 DSV 是为了 MaskedDarken 的 stencil 测试；
    // 颜色源可能是 SceneColor 或 FXAA Target。
    // 切换输出前清空输入槽，避免 FXAA Target / SceneColor 同时输入输出。
    UnbindPixelShaderResources(context, 0, 4);

    ID3D11RenderTargetView* rtv = backBufferRTV;
    context->OMSetRenderTargets(1, &rtv, m_sceneColor.GetDSV());

    D3D11_VIEWPORT viewport = {};
    viewport.Width    = static_cast<float>(width);
    viewport.Height   = static_cast<float>(height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    // 后处理必须使用自己的状态：不能继承主 Pass 的半透明混合/线框/深度状态。
    context->OMSetBlendState(m_blendDisabled.Get(), nullptr, 0xFFFFFFFF);
    context->RSSetState(m_rasterizer.Get());

    // 三个 PS 变体共享同一组参数；CopyPS 不用参数，GrayscalePS 用 y，MaskedDarkenPS 用 z。
    PostProcessCB params = {};
    params.Params = XMFLOAT4(0.0f, 1.0f, 0.05f, 0.0f);
    context->UpdateSubresource(m_paramsCB.Get(), 0, nullptr, &params, 0, 0);
    context->PSSetConstantBuffers(0, 1, m_paramsCB.GetAddressOf());

    // 根据模式选择绑定哪一个 FullscreenPass（也就是哪一个 PS 变体）。
    const auto drawPass = [&](FullscreenPass& pass, bool useStencilMask)
    {
        context->OMSetDepthStencilState(
            useStencilMask ? m_depthMaskedEffect.Get() : m_depthCopy.Get(),
            useStencilMask ? 1 : 0);

        pass.Bind(context);
        pass.Draw(context, colorSRV);
    };

    switch (mode)
    {
    case EPostProcessMode::Grayscale:
        // 灰度 + 暗角影响整个场景，单 Pass 覆盖即可。
        drawPass(m_grayscalePass, false);
        break;

    case EPostProcessMode::MaskedDarken:
        // 1) 先把后处理前的颜色源拷到后台缓冲（保护区域靠它保留原色）
        drawPass(m_copyPass, false);
        // 2) 只在 stencil != 1 的区域压暗
        drawPass(m_darkenPass, true);
        break;

    case EPostProcessMode::Off:
    default:
        // 关闭后处理：只做全屏拷贝，仍然走同一条离屏管线。
        drawPass(m_copyPass, false);
        break;
    }

    // 颜色源（SceneColor 或 FXAA Target）的 SRV 必须在下一帧绑定其 RTV 前解绑。
    // 同时清掉主 Pass 可能残留的 t1~t3，保证后处理结束时 PS 输入状态干净。
    UnbindPixelShaderResources(context, 0, 4);
}
