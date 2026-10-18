#pragma once

#include <d3d11.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

// 一对 VS + PS 及对应的输入布局（≈ UE FMaterialRenderProxy 中 shader 部分的雏形）
// 学习期采用运行时编译：改 .hlsl 后重启程序即生效，编译错误直接打到控制台
class ShaderSet
{
public:
    // 从同一文件编译 VSMain / PSMain 两个入口，并用 VS 字节码创建输入布局
    bool LoadFromFile(ID3D11Device* device, const wchar_t* file,
                      const D3D11_INPUT_ELEMENT_DESC* layoutDesc, UINT layoutCount);

    // 同上，但可指定入口名：一个 shader 文件可含多对 VS/PS
    // （≈ Unity 单 shader 资产多 Pass 的最小版，M16 的 Toon.hlsl 用它）
    bool LoadFromFile(ID3D11Device* device, const wchar_t* file,
                      const D3D11_INPUT_ELEMENT_DESC* layoutDesc, UINT layoutCount,
                      const char* vsEntry, const char* psEntry);

    // 只编译 VSMain：用于 Shadow Pass 这种只需要写深度的 Pass
    bool LoadVSFromFile(ID3D11Device* device, const wchar_t* file,
                        const D3D11_INPUT_ELEMENT_DESC* layoutDesc, UINT layoutCount);

    // 全屏 Pass：VS 使用 SV_VertexID，不需要输入布局；
    // pixelEntry 允许同一文件里编译多个 PS 变体（CopyPS / GrayscalePS / ...）。
    bool LoadFullscreenFromFile(ID3D11Device* device, const wchar_t* file,
                                const char* pixelEntry);

    void Bind(ID3D11DeviceContext* context) const;    // 输入布局 + VS + PS
    void BindVS(ID3D11DeviceContext* context) const;  // 输入布局 + VS（不绑 PS）

private:
    ComPtr<ID3D11VertexShader> m_vs;
    ComPtr<ID3D11PixelShader>  m_ps;
    ComPtr<ID3D11InputLayout>  m_layout;
};
