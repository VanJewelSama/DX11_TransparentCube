#include "D3D11Device.h"
#include <cstdio>

namespace
{
    bool CheckHR(HRESULT hr, const char* what)
    {
        if (FAILED(hr))
        {
            printf("[RHI] %s 失败, hr = 0x%08X\n", what, static_cast<unsigned int>(hr));
            return false;
        }
        return true;
    }
}

bool D3D11Device::Initialize(HWND hwnd, int width, int height)
{
    m_width  = width;
    m_height = height;

    // ---- 1. 设备 + 上下文 + 交换链，一步创建 ----
    DXGI_SWAP_CHAIN_DESC scDesc = {};
    scDesc.BufferDesc.Width   = width;
    scDesc.BufferDesc.Height  = height;
    scDesc.BufferDesc.Format  = DXGI_FORMAT_R8G8B8A8_UNORM;
    scDesc.SampleDesc.Count   = 1;      // 不开 MSAA
    scDesc.BufferUsage        = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount        = 2;      // 双缓冲：前台显示 + 后台绘制
    scDesc.OutputWindow       = hwnd;
    scDesc.Windowed           = TRUE;
    scDesc.SwapEffect         = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;   // Present 时翻转

    UINT createFlags = 0;
#ifdef _DEBUG
    createFlags |= D3D11_CREATE_DEVICE_DEBUG;   // 调试层：把 GPU 侧错误打到调试输出
#endif

    const D3D_FEATURE_LEVEL kFeatureLevels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL outLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,                        // 默认适配器
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        createFlags,
        kFeatureLevels, 1,
        D3D11_SDK_VERSION,
        &scDesc,
        &m_swapChain,
        &m_device,
        &outLevel,
        &m_context);

#ifdef _DEBUG
    if (FAILED(hr))
    {
        // 系统没装"图形工具"可选功能时，带调试层创建设备会失败 → 优雅降级
        printf("[RHI] 调试层不可用（未安装图形工具？），改用普通设备重试。\n");
        hr = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            kFeatureLevels, 1, D3D11_SDK_VERSION,
            &scDesc, &m_swapChain, &m_device, &outLevel, &m_context);
    }
#endif
    if (!CheckHR(hr, "D3D11CreateDeviceAndSwapChain"))
        return false;

    printf("[RHI] 设备创建成功, FeatureLevel = 0x%04X\n", static_cast<unsigned int>(outLevel));

    // ---- 2~4. 后台缓冲 RTV + 深度缓冲 DSV + 视口 ----
    return CreateFrameResources();
}

bool D3D11Device::CreateFrameResources()
{
    // 后台缓冲 → RTV：拿到"可以画上去的颜色画布"的句柄
    ComPtr<ID3D11Texture2D> backBuffer;
    if (!CheckHR(m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)), "GetBuffer(BackBuffer)"))
        return false;
    if (!CheckHR(m_device->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_backBufferRTV),
                 "CreateRenderTargetView"))
        return false;

    // 深度/模板缓冲：一张同尺寸纹理 + 它的 DSV
    D3D11_TEXTURE2D_DESC depthDesc = {};
    depthDesc.Width            = m_width;
    depthDesc.Height           = m_height;
    depthDesc.MipLevels        = 1;
    depthDesc.ArraySize        = 1;
    depthDesc.Format           = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Usage            = D3D11_USAGE_DEFAULT;
    depthDesc.BindFlags        = D3D11_BIND_DEPTH_STENCIL;
    if (!CheckHR(m_device->CreateTexture2D(&depthDesc, nullptr, &m_depthBuffer),
                 "CreateTexture2D(DepthBuffer)"))
        return false;
    if (!CheckHR(m_device->CreateDepthStencilView(m_depthBuffer.Get(), nullptr, &m_depthDSV),
                 "CreateDepthStencilView"))
        return false;

    // 视口：NDC 坐标 → 屏幕矩形的映射（这里 = 整个客户区）
    D3D11_VIEWPORT vp = {};
    vp.Width    = static_cast<float>(m_width);
    vp.Height   = static_cast<float>(m_height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &vp);

    return true;
}

void D3D11Device::OnResize(int width, int height)
{
    if (width == 0 || height == 0)   // 最小化，无事可做
        return;
    if (width == m_width && height == m_height)
        return;

    m_width  = width;
    m_height = height;

    // 关键顺序：先解绑渲染目标 → 释放旧视图 → ResizeBuffers → 重建
    // （FLIP 模式下不释放干净，ResizeBuffers 会报 DXGI_ERROR_INVALID_CALL）
    m_context->OMSetRenderTargets(0, nullptr, nullptr);
    m_backBufferRTV.Reset();
    m_depthDSV.Reset();
    m_depthBuffer.Reset();

    if (CheckHR(m_swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0),
                "SwapChain::ResizeBuffers"))
    {
        CreateFrameResources();
    }
}

void D3D11Device::ClearBackBuffer(const float color[4])
{
    // 每帧开头：指定"画到哪"（后台缓冲 + 深度缓冲），然后清掉上一帧的内容
    m_context->OMSetRenderTargets(1, m_backBufferRTV.GetAddressOf(), m_depthDSV.Get());

    // 离屏 Pass（ShadowMap 等）会把视口改成自己的大小；
    // 回到主 Pass 时必须恢复客户区视口，否则画面会被缩放/裁剪。
    D3D11_VIEWPORT viewport = {};
    viewport.Width    = static_cast<float>(m_width);
    viewport.Height   = static_cast<float>(m_height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;
    m_context->RSSetViewports(1, &viewport);

    m_context->ClearRenderTargetView(m_backBufferRTV.Get(), color);
    // 深度清成 1.0 = 最远（配合近小远大的 LessEqual 深度测试约定）
    m_context->ClearDepthStencilView(m_depthDSV.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
}

void D3D11Device::Present()
{
    // SyncInterval = 1 → 垂直同步，帧率锁到显示器刷新率
    m_swapChain->Present(1, 0);
}
