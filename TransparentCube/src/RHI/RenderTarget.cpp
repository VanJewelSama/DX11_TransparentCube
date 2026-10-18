#include "RenderTarget.h"

#include <cstdio>

namespace
{
    // 深度纹理如果要同时作为 SRV 采样，纹理本身通常要用 typeless 格式：
    //   Texture: R32_TYPELESS
    //   DSV    : D32_FLOAT
    //   SRV    : R32_FLOAT
    // 这样同一个资源才能合法地创建深度视图和着色器资源视图。
    struct DepthViewFormats
    {
        DXGI_FORMAT TextureFormat = DXGI_FORMAT_UNKNOWN;
        DXGI_FORMAT SrvFormat     = DXGI_FORMAT_UNKNOWN;
    };

    DepthViewFormats GetDepthViewFormats(DXGI_FORMAT depthFormat, bool bCreateSRV)
    {
        DepthViewFormats formats;
        formats.TextureFormat = depthFormat;

        if (!bCreateSRV)
            return formats;

        switch (depthFormat)
        {
        case DXGI_FORMAT_D32_FLOAT:
            formats.TextureFormat = DXGI_FORMAT_R32_TYPELESS;
            formats.SrvFormat     = DXGI_FORMAT_R32_FLOAT;
            break;

        case DXGI_FORMAT_D24_UNORM_S8_UINT:
            formats.TextureFormat = DXGI_FORMAT_R24G8_TYPELESS;
            formats.SrvFormat     = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
            break;

        case DXGI_FORMAT_D16_UNORM:
            formats.TextureFormat = DXGI_FORMAT_R16_TYPELESS;
            formats.SrvFormat     = DXGI_FORMAT_R16_UNORM;
            break;

        default:
            // 不支持的深度格式：保持原格式，不创建 SRV
            break;
        }

        return formats;
    }

    bool CreateColorResources(ID3D11Device* device,
                              UINT width, UINT height,
                              DXGI_FORMAT format,
                              bool bCreateSRV,
                              ComPtr<ID3D11Texture2D>& texture,
                              ComPtr<ID3D11RenderTargetView>& rtv,
                              ComPtr<ID3D11ShaderResourceView>& srv)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = width;
        desc.Height           = height;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = format;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        desc.BindFlags        = D3D11_BIND_RENDER_TARGET;
        if (bCreateSRV)
            desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)))
        {
            printf("[RenderTarget] 创建颜色纹理失败 (%ux%u, format=%u)\n",
                   width, height, static_cast<unsigned int>(format));
            return false;
        }

        if (FAILED(device->CreateRenderTargetView(texture.Get(), nullptr, &rtv)))
        {
            printf("[RenderTarget] 创建 RTV 失败\n");
            return false;
        }

        if (bCreateSRV && FAILED(device->CreateShaderResourceView(texture.Get(), nullptr, &srv)))
        {
            printf("[RenderTarget] 创建颜色 SRV 失败\n");
            return false;
        }

        return true;
    }

    bool CreateDepthResources(ID3D11Device* device,
                              UINT width, UINT height,
                              DXGI_FORMAT depthFormat,
                              bool bCreateSRV,
                              ComPtr<ID3D11Texture2D>& texture,
                              ComPtr<ID3D11DepthStencilView>& dsv,
                              ComPtr<ID3D11ShaderResourceView>& srv)
    {
        DepthViewFormats formats = GetDepthViewFormats(depthFormat, bCreateSRV);
        const bool createSrv = bCreateSRV && formats.SrvFormat != DXGI_FORMAT_UNKNOWN;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = width;
        desc.Height           = height;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = formats.TextureFormat;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        desc.BindFlags        = D3D11_BIND_DEPTH_STENCIL;
        if (createSrv)
            desc.BindFlags |= D3D11_BIND_SHADER_RESOURCE;

        if (FAILED(device->CreateTexture2D(&desc, nullptr, &texture)))
        {
            printf("[RenderTarget] 创建深度纹理失败 (%ux%u, format=%u)\n",
                   width, height, static_cast<unsigned int>(depthFormat));
            return false;
        }

        // 纹理可能是 typeless，所以 DSV 必须显式指定深度格式。
        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format             = depthFormat;
        dsvDesc.ViewDimension      = D3D11_DSV_DIMENSION_TEXTURE2D;
        dsvDesc.Texture2D.MipSlice = 0;
        if (FAILED(device->CreateDepthStencilView(texture.Get(), &dsvDesc, &dsv)))
        {
            printf("[RenderTarget] 创建 DSV 失败\n");
            return false;
        }

        if (createSrv)
        {
            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = formats.SrvFormat;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels       = 1;
            if (FAILED(device->CreateShaderResourceView(texture.Get(), &srvDesc, &srv)))
            {
                printf("[RenderTarget] 创建深度 SRV 失败\n");
                return false;
            }
        }

        return true;
    }
}

// ---------------------------------------------------------------------------
// RenderTarget
// ---------------------------------------------------------------------------

bool RenderTarget::Create(ID3D11Device* device, const RenderTargetDesc& desc)
{
    Release();
    m_desc = desc;

    if (!device || desc.Width == 0 || desc.Height == 0)
        return false;
    if (desc.ColorFormat == DXGI_FORMAT_UNKNOWN && desc.DepthFormat == DXGI_FORMAT_UNKNOWN)
        return false;

    if (desc.ColorFormat != DXGI_FORMAT_UNKNOWN)
    {
        if (!CreateColorResources(device, desc.Width, desc.Height, desc.ColorFormat,
                                  desc.bCreateColorSRV,
                                  m_colorTexture, m_rtv, m_colorSRV))
        {
            Release();
            return false;
        }
    }

    if (desc.DepthFormat != DXGI_FORMAT_UNKNOWN)
    {
        if (!CreateDepthResources(device, desc.Width, desc.Height, desc.DepthFormat,
                                  desc.bCreateDepthSRV,
                                  m_depthTexture, m_dsv, m_depthSRV))
        {
            Release();
            return false;
        }
    }

    return IsValid();
}

void RenderTarget::Resize(ID3D11Device* device, UINT width, UINT height)
{
    if (width == 0 || height == 0)
    {
        Release();
        return;
    }

    if (width == m_desc.Width && height == m_desc.Height)
        return;

    RenderTargetDesc desc = m_desc;
    desc.Width  = width;
    desc.Height = height;
    Create(device, desc);
}

void RenderTarget::Release()
{
    m_colorTexture.Reset();
    m_rtv.Reset();
    m_colorSRV.Reset();
    m_depthTexture.Reset();
    m_dsv.Reset();
    m_depthSRV.Reset();
    m_desc = {};
}

void RenderTarget::Bind(ID3D11DeviceContext* context) const
{
    ID3D11RenderTargetView* rtv = m_rtv.Get();
    context->OMSetRenderTargets(rtv ? 1 : 0, rtv ? &rtv : nullptr, m_dsv.Get());
}

void RenderTarget::Clear(ID3D11DeviceContext* context, const float color[4], float depth) const
{
    if (color && m_rtv)
        context->ClearRenderTargetView(m_rtv.Get(), color);

    if (m_dsv)
        context->ClearDepthStencilView(m_dsv.Get(),
                                       D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                       depth, 0);
}

bool RenderTarget::IsValid() const
{
    const bool hasColor = m_desc.ColorFormat != DXGI_FORMAT_UNKNOWN;
    const bool hasDepth = m_desc.DepthFormat != DXGI_FORMAT_UNKNOWN;

    if (!hasColor && !hasDepth)
        return false;
    if (hasColor && !m_rtv)
        return false;
    if (hasDepth && !m_dsv)
        return false;

    return true;
}

// ---------------------------------------------------------------------------
// RenderTargetGroup（MRT）
// ---------------------------------------------------------------------------

bool RenderTargetGroup::Create(ID3D11Device* device,
                               const std::vector<DXGI_FORMAT>& colorFormats,
                               DXGI_FORMAT depthFormat,
                               UINT width, UINT height,
                               bool bCreateColorSRVs,
                               bool bCreateDepthSRV)
{
    Release();

    m_width  = width;
    m_height = height;

    if (!device || width == 0 || height == 0)
        return false;
    if (colorFormats.empty() && depthFormat == DXGI_FORMAT_UNKNOWN)
        return false;

    m_colorTextures.resize(colorFormats.size());
    m_rtvs.resize(colorFormats.size());
    m_colorSRVs.resize(colorFormats.size());

    for (size_t i = 0; i < colorFormats.size(); ++i)
    {
        if (colorFormats[i] == DXGI_FORMAT_UNKNOWN)
            return false;

        if (!CreateColorResources(device, width, height, colorFormats[i],
                                  bCreateColorSRVs,
                                  m_colorTextures[i], m_rtvs[i], m_colorSRVs[i]))
        {
            Release();
            return false;
        }
    }

    if (depthFormat != DXGI_FORMAT_UNKNOWN)
    {
        if (!CreateDepthResources(device, width, height, depthFormat,
                                  bCreateDepthSRV,
                                  m_depthTexture, m_dsv, m_depthSRV))
        {
            Release();
            return false;
        }
    }

    return IsValid();
}

void RenderTargetGroup::Release()
{
    m_colorTextures.clear();
    m_rtvs.clear();
    m_colorSRVs.clear();
    m_depthTexture.Reset();
    m_dsv.Reset();
    m_depthSRV.Reset();
    m_width  = 0;
    m_height = 0;
}

void RenderTargetGroup::Bind(ID3D11DeviceContext* context) const
{
    std::vector<ID3D11RenderTargetView*> rtvs;
    rtvs.reserve(m_rtvs.size());
    for (const auto& rtv : m_rtvs)
        rtvs.push_back(rtv.Get());

    context->OMSetRenderTargets(static_cast<UINT>(rtvs.size()),
                                rtvs.empty() ? nullptr : rtvs.data(),
                                m_dsv.Get());
}

void RenderTargetGroup::Clear(ID3D11DeviceContext* context, const float color[4], float depth) const
{
    if (color)
    {
        for (const auto& rtv : m_rtvs)
        {
            if (rtv)
                context->ClearRenderTargetView(rtv.Get(), color);
        }
    }

    if (m_dsv)
        context->ClearDepthStencilView(m_dsv.Get(),
                                       D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                       depth, 0);
}

bool RenderTargetGroup::IsValid() const
{
    if (m_rtvs.empty() && !m_dsv)
        return false;

    for (const auto& rtv : m_rtvs)
    {
        if (!rtv)
            return false;
    }

    if (m_depthTexture && !m_dsv)
        return false;

    return true;
}

ID3D11ShaderResourceView* RenderTargetGroup::GetColorSRV(UINT index) const
{
    if (index >= m_colorSRVs.size())
        return nullptr;
    return m_colorSRVs[index].Get();
}
