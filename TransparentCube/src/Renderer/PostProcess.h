#pragma once

#include "FullscreenPass.h"
#include "RHI/RenderTarget.h"

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// ============================================================================
// 离屏后处理链（M8 / M8.5）
//
// 模式：
//   Off          : 全屏拷贝 SceneColor（不做效果）
//   Grayscale    : 全屏灰度 + 暗角（影响所有物体）
//   MaskedDarken : 场景整体压暗，但 stencil=1 的区域（CubeB）保持原色
//
// 流程：
//   1. BeginScene：场景渲染到 SceneColor（颜色 + D24S8 深度/模板）；
//   2. 主 Pass：CubeB 在模板缓冲里写 1；
//   3. EndScene：全屏拷贝 + 可选的模板测试后处理 Pass。
//
// 三种模式由三个 PS 变体实现（CopyPS / GrayscalePS / MaskedDarkenPS），
// CPU 根据模式绑定不同的 FullscreenPass，而不是在 shader 里写 mode if。
// ============================================================================
enum class EPostProcessMode
{
    Off = 0,
    Grayscale = 1,
    MaskedDarken = 2,
};

class PostProcess
{
public:
    bool Initialize(ID3D11Device* device, UINT width, UINT height);
    void OnResize(ID3D11Device* device, UINT width, UINT height);

    // 绑定 SceneColor 并清屏（颜色 + 深度 + 模板）；失败返回 false。
    bool BeginScene(ID3D11DeviceContext* context, const float clearColor[4]);

    // 全屏后处理：SceneColor →（可选 FXAA）→ 颜色后处理 → 后台缓冲。
    void EndScene(ID3D11DeviceContext* context,
                  ID3D11RenderTargetView* backBufferRTV,
                  UINT width, UINT height,
                  EPostProcessMode mode,
                  bool fxaaEnabled);

    bool IsValid() const { return m_sceneColor.IsValid(); }

private:
    RenderTarget   m_sceneColor;    // 颜色 + 深度/模板
    RenderTarget   m_fxaaTarget;    // FXAA 输出（不需要深度）

    // 三个 PS 变体：CPU 按模式选择，shader 内部不再用 if 分支。
    FullscreenPass m_copyPass;
    FullscreenPass m_grayscalePass;
    FullscreenPass m_darkenPass;
    FullscreenPass m_fxaaPass;

    ComPtr<ID3D11Buffer>            m_paramsCB;
    ComPtr<ID3D11Buffer>            m_fxaaCB;
    ComPtr<ID3D11BlendState>        m_blendDisabled;
    ComPtr<ID3D11DepthStencilState> m_depthCopy;          // 深度/模板都关闭
    ComPtr<ID3D11DepthStencilState> m_depthMaskedEffect;  // 只做模板测试，不写深度/模板
    ComPtr<ID3D11RasterizerState>   m_rasterizer;
};
