#include "Shader.h"
#include <d3dcompiler.h>
#include <cstdio>

namespace
{
    // 调 D3DCompiler 把一段 HLSL 源码编成字节码；失败时把编译器错误打印出来
    ComPtr<ID3DBlob> CompileShader(const wchar_t* file, const char* entry, const char* target)
    {
        UINT flags = D3DCOMPILE_ENABLE_STRICTNESS;
#ifdef _DEBUG
        flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

        ComPtr<ID3DBlob> code, errors;
        const HRESULT hr = D3DCompileFromFile(
            file, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
            entry, target, flags, 0, &code, &errors);
        if (FAILED(hr))
        {
            printf("[Shader] 编译失败 %ls [%s]:\n%s",
               file, entry,
               errors ? static_cast<const char*>(errors->GetBufferPointer()) : "(无错误信息)\n");
            return nullptr;
        }
        return code;
    }
}

bool ShaderSet::LoadFromFile(ID3D11Device* device, const wchar_t* file,
                             const D3D11_INPUT_ELEMENT_DESC* layoutDesc, UINT layoutCount)
{
    return LoadFromFile(device, file, layoutDesc, layoutCount, "VSMain", "PSMain");
}

bool ShaderSet::LoadFromFile(ID3D11Device* device, const wchar_t* file,
                             const D3D11_INPUT_ELEMENT_DESC* layoutDesc, UINT layoutCount,
                             const char* vsEntry, const char* psEntry)
{
    // 同一 .hlsl 文件编译两次：不同入口函数、不同 shader 目标
    ComPtr<ID3DBlob> vsBlob = CompileShader(file, vsEntry, "vs_5_0");
    ComPtr<ID3DBlob> psBlob = CompileShader(file, psEntry, "ps_5_0");
    if (!vsBlob || !psBlob)
        return false;

    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                          nullptr, &m_vs)) ||
        FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                         nullptr, &m_ps)))
    {
        printf("[Shader] 创建 VS/PS 对象失败\n");
        return false;
    }

    // 输入布局 = "顶点缓冲里每个字节怎么解释"，必须拿 VS 字节码做校验
    if (FAILED(device->CreateInputLayout(layoutDesc, layoutCount,
                                         vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                         &m_layout)))
    {
        printf("[Shader] 创建输入布局失败（顶点结构与 HLSL 签名不匹配？）\n");
        return false;
    }

    printf("[Shader] %ls [%s + %s] 编译成功\n", file, vsEntry, psEntry);
    return true;
}

bool ShaderSet::LoadVSFromFile(ID3D11Device* device, const wchar_t* file,
                               const D3D11_INPUT_ELEMENT_DESC* layoutDesc, UINT layoutCount)
{
    // Shadow Pass 只需要 VS，不需要 PS；先清掉可能存在的旧 PS。
    m_ps.Reset();

    ComPtr<ID3DBlob> vsBlob = CompileShader(file, "VSMain", "vs_5_0");
    if (!vsBlob)
        return false;

    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                          nullptr, &m_vs)))
    {
        printf("[Shader] 创建 VS 对象失败\n");
        return false;
    }

    if (FAILED(device->CreateInputLayout(layoutDesc, layoutCount,
                                         vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                         &m_layout)))
    {
        printf("[Shader] 创建 VS 输入布局失败（顶点结构与 HLSL 签名不匹配？）\n");
        return false;
    }

    printf("[Shader] %ls [VSMain] 编译成功\n", file);
    return true;
}

bool ShaderSet::LoadFullscreenFromFile(ID3D11Device* device, const wchar_t* file,
                                       const char* pixelEntry)
{
    if (!device || !file || !pixelEntry)
        return false;

    // 全屏 VS 用 SV_VertexID 生成三角形，不需要任何顶点输入。
    m_layout.Reset();

    ComPtr<ID3DBlob> vsBlob = CompileShader(file, "VSMain", "vs_5_0");
    ComPtr<ID3DBlob> psBlob = CompileShader(file, pixelEntry, "ps_5_0");
    if (!vsBlob || !psBlob)
        return false;

    if (FAILED(device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(),
                                          nullptr, &m_vs)) ||
        FAILED(device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(),
                                         nullptr, &m_ps)))
    {
        printf("[Shader] 创建全屏 VS/PS 失败\n");
        return false;
    }

    printf("[Shader] %ls [VSMain + %s] 编译成功\n", file, pixelEntry);
    return true;
}

void ShaderSet::Bind(ID3D11DeviceContext* context) const
{
    context->IASetInputLayout(m_layout.Get());
    context->VSSetShader(m_vs.Get(), nullptr, 0);
    context->PSSetShader(m_ps.Get(), nullptr, 0);
}

void ShaderSet::BindVS(ID3D11DeviceContext* context) const
{
    context->IASetInputLayout(m_layout.Get());
    context->VSSetShader(m_vs.Get(), nullptr, 0);
}
