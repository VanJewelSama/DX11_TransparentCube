#pragma once

#include <DirectXMath.h>
#include <cmath>

enum class EProjectionMode
{
    Perspective,
    Orthographic,
};

// 一帧的相机视角信息（≈ UE FMinimalViewInfo 的最小版）
// 纯数据：任何相机类型都把参数填成这个结构交给 PCM；
// 渲染侧只认这份数据，由它派生 View/Proj 矩阵
struct CameraViewInfo
{
    DirectX::XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
    float Yaw   = 0.0f;   // 弧度，绕世界 Y 轴（0 = 朝 +Z 方向）
    float Pitch = 0.0f;   // 弧度，绕相机右轴（正值 = 抬头）
    float FovY  = DirectX::XMConvertToRadians(60.0f);
    float NearZ = 0.1f;
    float FarZ  = 100.0f;

    EProjectionMode ProjectionMode = EProjectionMode::Perspective;
    float OrthoWidth = 10.0f;   // 正交模式的视野宽度（备用）

    // 前向向量（Yaw=0 朝 +Z，Pitch 正值朝上）
    DirectX::XMVECTOR GetForward() const
    {
        const float cp = std::cos(Pitch);
        return DirectX::XMVectorSet(std::sin(Yaw) * cp, std::sin(Pitch), std::cos(Yaw) * cp, 0.0f);
    }

    DirectX::XMMATRIX GetViewMatrix() const
    {
        const DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&Position);
        return DirectX::XMMatrixLookToLH(pos, GetForward(),
                                         DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
    }

    // aspect 是视口属性不是相机属性，由渲染侧传入
    DirectX::XMMATRIX GetProjMatrix(float aspect) const
    {
        if (ProjectionMode == EProjectionMode::Orthographic)
            return DirectX::XMMatrixOrthographicLH(OrthoWidth, OrthoWidth / aspect, NearZ, FarZ);
        return DirectX::XMMatrixPerspectiveFovLH(FovY, aspect, NearZ, FarZ);
    }
};
