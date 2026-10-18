#pragma once

#include <d3d11.h>

// ============================================================================
// D3D11 状态切换的小工具
//
// 同一张纹理不能同时作为“着色器输入（SRV）”和“渲染输出（RTV/DSV）”。
// 如果切换渲染目标时旧 SRV 还留在 PS 的输入槽里，D3D11 Runtime 会自动把
// 冲突的槽位解绑，但 Debug Layer 会持续报告：
//   - DEVICE_PSSETSHADERRESOURCES_HAZARD
//   - DEVICE_OMSETRENDERTARGETS_HAZARD
//
// 在 RenderTarget 切换前显式清空输入槽，可以让状态变化可控，也不再刷警告。
// ============================================================================

inline void UnbindPixelShaderResources(ID3D11DeviceContext* context,
                                       UINT startSlot,
                                       UINT count)
{
    if (!context || count == 0)
        return;

    // 当前 Demo 最多使用 t0~t3；这里预留 8 个槽位，避免动态分配。
    constexpr UINT kMaxSlots = 8;
    if (count > kMaxSlots)
        count = kMaxSlots;

    ID3D11ShaderResourceView* nullSRVs[kMaxSlots] = {};
    context->PSSetShaderResources(startSlot, count, nullSRVs);
}
