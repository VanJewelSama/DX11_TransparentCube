#pragma once

#include <d3d11.h>
#include <wrl/client.h>

#include <vector>

using Microsoft::WRL::ComPtr;

// ============================================================================
// 离屏渲染目标（≈ UE FRHITexture + RTV/SRV/DSV 的最小封装）
//
// 使用场景：
//   - ShadowMap：只需要深度的 depth-only RT；
//   - 后处理：SceneColor（颜色 + 可选深度），之后作为 SRV 采样；
//   - 镜面：ReflectionColor + ReflectionDepth；
//   - MRT/G-Buffer：RenderTargetGroup 绑定多张颜色 RT + 一个共享深度。
//
// 职责边界：
//   - 本类只管理 GPU 资源和视图，不负责视口/清屏策略/Pass 顺序；
//   - 视口、清屏颜色、状态对象由调用者（Renderer/Pass）决定。
// ============================================================================
struct RenderTargetDesc
{
    UINT Width  = 0;
    UINT Height = 0;

    // UNKNOWN 表示不需要该附件。
    DXGI_FORMAT ColorFormat = DXGI_FORMAT_UNKNOWN;
    DXGI_FORMAT DepthFormat = DXGI_FORMAT_UNKNOWN;

    bool bCreateColorSRV = true;
    bool bCreateDepthSRV = false;
};

class RenderTarget
{
public:
    bool Create(ID3D11Device* device, const RenderTargetDesc& desc);
    void Resize(ID3D11Device* device, UINT width, UINT height);
    void Release();

    // 绑定当前 RT（0 或 1 张颜色 + 可选深度）
    void Bind(ID3D11DeviceContext* context) const;

    // 清颜色/深度；color 传 nullptr 表示跳过颜色清屏
    void Clear(ID3D11DeviceContext* context, const float color[4], float depth = 1.0f) const;

    bool IsValid() const;
    UINT GetWidth()  const { return m_desc.Width; }
    UINT GetHeight() const { return m_desc.Height; }
    const RenderTargetDesc& GetDesc() const { return m_desc; }

    ID3D11RenderTargetView*   GetRTV() const { return m_rtv.Get(); }
    ID3D11DepthStencilView*   GetDSV() const { return m_dsv.Get(); }
    ID3D11ShaderResourceView* GetColorSRV() const { return m_colorSRV.Get(); }
    ID3D11ShaderResourceView* GetDepthSRV() const { return m_depthSRV.Get(); }

    // 便利函数：有颜色 SRV 就返回颜色，否则返回深度 SRV
    ID3D11ShaderResourceView* GetSRV() const
    {
        return m_colorSRV ? m_colorSRV.Get() : m_depthSRV.Get();
    }

private:
    RenderTargetDesc m_desc;

    ComPtr<ID3D11Texture2D>          m_colorTexture;
    ComPtr<ID3D11RenderTargetView>   m_rtv;
    ComPtr<ID3D11ShaderResourceView> m_colorSRV;

    ComPtr<ID3D11Texture2D>          m_depthTexture;
    ComPtr<ID3D11DepthStencilView>   m_dsv;
    ComPtr<ID3D11ShaderResourceView> m_depthSRV;
};

// ============================================================================
// MRT（Multiple Render Targets）容器
//
// 一次 DrawCall 可以写多张颜色纹理，共享一个深度缓冲。典型用途是 G-Buffer：
//   SV_Target0 = Albedo
//   SV_Target1 = Normal
//   SV_Target2 = Roughness/Metallic
//
// 当前 Demo 的主路径仍是前向单 RT；这个类先作为 M8/M9 的基础设施。
// ============================================================================
class RenderTargetGroup
{
public:
    bool Create(ID3D11Device* device,
                const std::vector<DXGI_FORMAT>& colorFormats,
                DXGI_FORMAT depthFormat,
                UINT width, UINT height,
                bool bCreateColorSRVs = true,
                bool bCreateDepthSRV = false);
    void Release();

    void Bind(ID3D11DeviceContext* context) const;
    void Clear(ID3D11DeviceContext* context, const float color[4], float depth = 1.0f) const;

    bool IsValid() const;
    UINT GetWidth()  const { return m_width; }
    UINT GetHeight() const { return m_height; }
    UINT GetColorCount() const { return static_cast<UINT>(m_rtvs.size()); }

    ID3D11ShaderResourceView* GetColorSRV(UINT index) const;
    ID3D11ShaderResourceView* GetDepthSRV() const { return m_depthSRV.Get(); }

private:
    std::vector<ComPtr<ID3D11Texture2D>>          m_colorTextures;
    std::vector<ComPtr<ID3D11RenderTargetView>>   m_rtvs;
    std::vector<ComPtr<ID3D11ShaderResourceView>> m_colorSRVs;

    ComPtr<ID3D11Texture2D>          m_depthTexture;
    ComPtr<ID3D11DepthStencilView>   m_dsv;
    ComPtr<ID3D11ShaderResourceView> m_depthSRV;

    UINT m_width  = 0;
    UINT m_height = 0;
};
