#include "Texture.h"
#include "Core/PathUtil.h"

#include <wincodec.h>

#include <climits>
#include <cstdio>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
    // 把 CPU 内存里的 RGBA8 像素上传成 GPU 纹理。
    // 这里统一使用 DXGI_FORMAT_R8G8B8A8_UNORM：
    //   R,G,B,A 各 8 位，排列顺序与 WIC 的 GUID_WICPixelFormat32bppRGBA 一致。
    //
    // 有 context 且尺寸大于 1×1 时，使用 GPU GenerateMips 生成完整 mip 链：
    //   - MipLevels = 0 让 D3D11 计算完整链；
    //   - 纹理必须是 DEFAULT + RENDER_TARGET + GENERATE_MIPS；
    //   - UpdateSubresource level 0 后，CreateSRV，再 GenerateMips。
    //
    // 说明：当前 demo 还没有做线性空间渲染，所以这里用 UNORM 而不是 UNORM_SRGB。
    // 以后如果启用 sRGB 后备缓冲，可以改成 R8G8B8A8_UNORM_SRGB。
    Texture CreateTextureFromPixels(ID3D11Device* device, ID3D11DeviceContext* context,
                                    UINT width, UINT height,
                                    const BYTE* pixels, UINT stride)
    {
        Texture result;

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = width;
        desc.Height           = height;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;

        const bool generateMips = context != nullptr && (width > 1 || height > 1);

        if (generateMips)
        {
            desc.MipLevels = 0;   // 完整 mip 链
            desc.Usage     = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
            desc.MiscFlags = D3D11_RESOURCE_MISC_GENERATE_MIPS;

            if (FAILED(device->CreateTexture2D(&desc, nullptr, &result.Resource)))
            {
                printf("[Texture] CreateTexture2D(mipmapped) 失败\n");
                return {};
            }

            context->UpdateSubresource(result.Resource.Get(), 0, nullptr, pixels, stride, 0);

            if (FAILED(device->CreateShaderResourceView(result.Resource.Get(), nullptr, &result.SRV)))
            {
                printf("[Texture] CreateShaderResourceView(mipmapped) 失败\n");
                return {};
            }

            context->GenerateMips(result.SRV.Get());
            printf("[Texture] 已生成 mip 链 (%ux%u)\n", width, height);
        }
        else
        {
            desc.MipLevels = 1;
            desc.Usage     = D3D11_USAGE_IMMUTABLE;
            desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

            D3D11_SUBRESOURCE_DATA init = {};
            init.pSysMem     = pixels;
            init.SysMemPitch = stride;

            if (FAILED(device->CreateTexture2D(&desc, &init, &result.Resource)))
            {
                printf("[Texture] CreateTexture2D 失败\n");
                return {};
            }

            if (FAILED(device->CreateShaderResourceView(result.Resource.Get(), nullptr, &result.SRV)))
            {
                printf("[Texture] CreateShaderResourceView 失败\n");
                return {};
            }
        }

        result.Width  = width;
        result.Height = height;
        return result;
    }
}

Texture Texture::LoadFromFile(ID3D11Device* device, ID3D11DeviceContext* context,
                              const wchar_t* path)
{
    const std::wstring resolved = PathUtil::ResolveContentPath(path);

    // ---- 1. WIC 工厂 ----
    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr))
    {
        printf("[Texture] 创建 WIC 工厂失败 (hr=0x%08X)，COM 是否已初始化？\n",
               static_cast<unsigned int>(hr));
        return {};
    }

    // ---- 2. 打开文件并取第一帧 ----
    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(resolved.c_str(), nullptr, GENERIC_READ,
                                            WICDecodeMetadataCacheOnDemand, &decoder);
    if (FAILED(hr))
    {
        printf("[Texture] 无法打开图片: %ls (hr=0x%08X)\n",
               resolved.c_str(), static_cast<unsigned int>(hr));
        return {};
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr))
    {
        printf("[Texture] 读取图片帧失败: %ls (hr=0x%08X)\n",
               resolved.c_str(), static_cast<unsigned int>(hr));
        return {};
    }

    // ---- 3. 统一转换成 RGBA8 ----
    // 源文件可能是 24bpp BGR、索引色、16bpp 等；先转成固定格式，
    // 后面创建 D3D11 纹理时就不用再关心源格式了。
    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr))
    {
        printf("[Texture] 创建 WIC 格式转换器失败 (hr=0x%08X)\n", static_cast<unsigned int>(hr));
        return {};
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr))
    {
        printf("[Texture] WIC 像素格式转换失败 (hr=0x%08X)\n", static_cast<unsigned int>(hr));
        return {};
    }

    UINT width  = 0;
    UINT height = 0;
    hr = converter->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0)
    {
        printf("[Texture] 获取图片尺寸失败或尺寸为 0\n");
        return {};
    }

    // ---- 4. 解码到 CPU 内存 ----
    // 32bpp RGBA 每像素 4 字节，行间距 = width * 4。
    // 这里的 stride 同时用于 CopyPixels 和 D3D11_SUBRESOURCE_DATA。
    const UINT stride = width * 4;
    const size_t byteCount = static_cast<size_t>(stride) * height;
    if (byteCount > UINT_MAX)
    {
        printf("[Texture] 图片过大: %ux%u\n", width, height);
        return {};
    }

    std::vector<BYTE> pixels(byteCount);
    hr = converter->CopyPixels(nullptr, stride, static_cast<UINT>(byteCount), pixels.data());
    if (FAILED(hr))
    {
        printf("[Texture] CopyPixels 失败 (hr=0x%08X)\n", static_cast<unsigned int>(hr));
        return {};
    }

    // ---- 5. 上传 GPU（并生成 mip 链）----
    Texture texture = CreateTextureFromPixels(device, context, width, height,
                                              pixels.data(), stride);
    if (texture.IsValid())
        printf("[Texture] 已加载 %ls (%ux%u)\n", resolved.c_str(), width, height);

    return texture;
}

Texture Texture::CreateSolidColor(ID3D11Device* device, ID3D11DeviceContext* context,
                                  BYTE r, BYTE g, BYTE b, BYTE a)
{
    const BYTE pixel[4] = { r, g, b, a };
    return CreateTextureFromPixels(device, context, 1, 1, pixel, 4);
}

Texture Texture::CreateCheckerboard(ID3D11Device* device, ID3D11DeviceContext* context,
                                    UINT size)
{
    if (size == 0)
        return {};

    // 8×8 个格子，颜色刻意做得明显一些，方便一眼看出这是 fallback。
    const UINT cellSize = (size + 7) / 8;
    std::vector<BYTE> pixels(static_cast<size_t>(size) * size * 4);

    for (UINT y = 0; y < size; ++y)
    {
        for (UINT x = 0; x < size; ++x)
        {
            const bool light = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            BYTE* p = &pixels[(static_cast<size_t>(y) * size + x) * 4];
            if (light)
            {
                p[0] = 230; p[1] = 230; p[2] = 230; p[3] = 255;
            }
            else
            {
                p[0] = 40;  p[1] = 40;  p[2] = 40;  p[3] = 255;
            }
        }
    }

    return CreateTextureFromPixels(device, context, size, size, pixels.data(), size * 4);
}
