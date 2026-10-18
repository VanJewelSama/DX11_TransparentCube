#include "FullscreenPass.h"

#include "Core/PathUtil.h"

#include <cstdio>
#include <string>

bool FullscreenPass::Initialize(ID3D11Device* device, const wchar_t* shaderPath,
                                const char* pixelEntry)
{
    if (!device || !shaderPath || !pixelEntry)
        return false;

    const std::wstring resolved = PathUtil::ResolveContentPath(shaderPath);
    if (!m_shader.LoadFullscreenFromFile(device, resolved.c_str(), pixelEntry))
        return false;

    // 后处理通常对 SceneColor 做线性放大/缩小，这里用双线性 + Clamp。
    D3D11_SAMPLER_DESC desc = {};
    desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
    desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    desc.MinLOD         = 0.0f;
    desc.MaxLOD         = 0.0f;
    if (FAILED(device->CreateSamplerState(&desc, &m_sampler)))
    {
        printf("[FullscreenPass] 创建 SamplerState 失败\n");
        return false;
    }

    return true;
}

void FullscreenPass::Bind(ID3D11DeviceContext* context) const
{
    // ShaderSet::Bind 会设置 null 输入布局（全屏 VS 不需要顶点输入）。
    m_shader.Bind(context);
    context->PSSetSamplers(0, 1, m_sampler.GetAddressOf());
}

void FullscreenPass::Draw(ID3D11DeviceContext* context,
                          ID3D11ShaderResourceView* inputSRV) const
{
    ID3D11ShaderResourceView* srv = inputSRV;
    context->PSSetShaderResources(0, 1, &srv);

    // 全屏 VS 只用 SV_VertexID，不需要顶点/索引缓冲。
    context->IASetVertexBuffers(0, 0, nullptr, nullptr, nullptr);
    context->IASetIndexBuffer(nullptr, DXGI_FORMAT_UNKNOWN, 0);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->Draw(3, 0);
}
