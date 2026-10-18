#pragma once

#include "Shader.h"

#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// ============================================================================
// 全屏 Pass（≈ UE 后处理/全屏 Draw 的最小版）
//
// VS 使用 SV_VertexID 生成一个覆盖屏幕的三角形，不需要 VB/IB。
// 这里只负责“绑定全屏 Shader + 采样器 + 输入纹理 + Draw(3)”，
// 输出 RTV、混合/深度/光栅化状态由调用者（PostProcess）管理。
// ============================================================================
class FullscreenPass
{
public:
    // pixelEntry：同一份全屏 shader 文件里的 PS 入口，例如 "CopyPS" / "GrayscalePS"
    bool Initialize(ID3D11Device* device, const wchar_t* shaderPath, const char* pixelEntry);

    void Bind(ID3D11DeviceContext* context) const;
    void Draw(ID3D11DeviceContext* context, ID3D11ShaderResourceView* inputSRV) const;

private:
    ShaderSet m_shader;
    ComPtr<ID3D11SamplerState> m_sampler;
};
