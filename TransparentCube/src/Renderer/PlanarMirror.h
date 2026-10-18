#pragma once

#include "RHI/RenderTarget.h"
#include "Shader.h"

#include <DirectXMath.h>
#include <d3d11.h>
#include <wrl/client.h>

class StaticMeshActor;
struct CameraViewInfo;

// ============================================================================
// 平面镜（Planar Reflection）
//
// 思路：
//   1. 把主相机关于镜面平面做反射，得到镜像相机；
//   2. 用镜像相机把场景渲染到 Reflection RT（镜面自身排除）；
//   3. 主 Pass 绘制镜面时，用镜像相机的 ViewProj 计算反射纹理 UV，
//      采样 Reflection RT，并与淡白色 tint 混合。
//
// 这是“一个额外场景 Pass + 一张离屏 RT”，不是递归反射。
// ============================================================================
class PlanarMirror
{
public:
    bool Initialize(ID3D11Device* device, UINT width, UINT height);
    void OnResize(ID3D11Device* device, UINT width, UINT height);

    // 计算镜像相机，绑定 Reflection RT 并清屏；返回 false 表示 RT 无效。
    bool BeginReflection(ID3D11DeviceContext* context,
                         const float clearColor[4],
                         const StaticMeshActor& mirrorActor,
                         const CameraViewInfo& camera,
                         float aspect);

    void EndReflection(ID3D11DeviceContext* context);

    // 主 Pass 中绘制镜面：绑定镜面 shader / MirrorCB / Reflection SRV。
    void DrawMirror(ID3D11DeviceContext* context, const StaticMeshActor& mirrorActor);

    // 反射 Pass 需要翻转绕序（镜像矩阵的行列式为负）。
    ID3D11RasterizerState* GetReflectionRasterizer() const { return m_reflectionRasterizer.Get(); }

    const DirectX::XMMATRIX& GetMirrorViewProj() const { return m_mirrorViewProj; }
    const DirectX::XMFLOAT3& GetMirrorCameraPos() const { return m_mirrorCameraPos; }
    bool IsValid() const { return m_reflectionTarget.IsValid(); }

private:
    RenderTarget m_reflectionTarget;

    ShaderSet             m_mirrorShader;
    ComPtr<ID3D11Buffer>  m_mirrorCB;
    ComPtr<ID3D11SamplerState> m_sampler;
    ComPtr<ID3D11RasterizerState> m_reflectionRasterizer;

    DirectX::XMMATRIX m_mirrorViewProj = DirectX::XMMatrixIdentity();
    DirectX::XMFLOAT3 m_mirrorCameraPos = {};
};
