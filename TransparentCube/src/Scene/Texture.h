#pragma once

#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// ============================================================================
// 纹理资产（≈ UE UTexture2D 的最小版）
//
// 一份 Texture = GPU 侧 Texture2D + 给着色器访问的 SRV。
// 加载流程：文件（PNG/JPEG...）→ WIC 解码成 RGBA8 → CreateTexture2D → SRV。
//
// 注意：WIC 是 COM 组件，调用 LoadFromFile 前必须初始化 COM。
// 本 demo 在 DemoMain 里用 ComScope 统一初始化/反初始化。
// ============================================================================
struct Texture
{
    ComPtr<ID3D11Texture2D>          Resource;
    ComPtr<ID3D11ShaderResourceView> SRV;
    UINT Width  = 0;
    UINT Height = 0;

    bool IsValid() const { return SRV != nullptr; }

    // 用 WIC 解码图片并上传 GPU。路径可以是相对路径（assets/...），
    // 内部会通过 PathUtil::ResolveContentPath 处理 F5 / 双击两种工作目录。
    // context 用于 GPU GenerateMips 生成完整 mip 链。
    static Texture LoadFromFile(ID3D11Device* device, ID3D11DeviceContext* context,
                                const wchar_t* path);

    // 1×1 纯色纹理：常用于给“不贴图”的材质提供默认白色贴图，
    // 这样 shader 可以无条件采样，不需要额外分支。
    static Texture CreateSolidColor(ID3D11Device* device, ID3D11DeviceContext* context,
                                    BYTE r, BYTE g, BYTE b, BYTE a = 255);

    // 程序生成的棋盘格：当图片缺失/解码失败时的 fallback，
    // 保证 demo 不依赖外部资源也能运行。
    static Texture CreateCheckerboard(ID3D11Device* device, ID3D11DeviceContext* context,
                                      UINT size = 512);
};
