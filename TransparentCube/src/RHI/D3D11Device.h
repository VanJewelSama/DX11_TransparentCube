#pragma once

#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// DX11 设备层最小封装（≈ UE FD3D11DynamicRHI 的单线程朴素版）
// 拥有：Device（资源工厂）/ Context（命令下发者）/ SwapChain（前后台缓冲）
//       后台缓冲 RTV（颜色画布）/ 深度缓冲 + DSV / 视口
class D3D11Device
{
public:
    bool Initialize(HWND hwnd, int width, int height);

    // 窗口客户区变化后调用：重建后台缓冲与深度缓冲（0 尺寸 = 最小化，忽略）
    void OnResize(int width, int height);

    // 直接渲染到后台缓冲时的便捷函数（颜色 + 深度 + 主视口）。
    // M8 之后主路径改为 SceneColor + 后处理，但保留它作为调试/简化路径。
    void ClearBackBuffer(const float color[4]);

    // 垂直同步 Present：前后台缓冲交换
    void Present();

    ID3D11Device*        GetDevice()  const { return m_device.Get(); }
    ID3D11DeviceContext* GetContext() const { return m_context.Get(); }

    // 后处理 Pass 需要把最终结果写回后台缓冲。
    ID3D11RenderTargetView* GetBackBufferRTV() const { return m_backBufferRTV.Get(); }
    int GetWidth()  const { return m_width; }
    int GetHeight() const { return m_height; }

private:
    bool CreateFrameResources();   // Initialize / OnResize 共用

    ComPtr<ID3D11Device>           m_device;
    ComPtr<ID3D11DeviceContext>    m_context;
    ComPtr<IDXGISwapChain>         m_swapChain;
    ComPtr<ID3D11Texture2D>        m_depthBuffer;
    ComPtr<ID3D11RenderTargetView> m_backBufferRTV;
    ComPtr<ID3D11DepthStencilView> m_depthDSV;

    int m_width  = 0;
    int m_height = 0;
};
