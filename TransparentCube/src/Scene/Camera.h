#pragma once

#include "CameraViewInfo.h"

class CameraManager;
class Input;

// ============================================================================
// 相机接口（≈ UE 中 PCM 对 ViewTarget 的约定）
// 任何类型的相机都要实现这组固定函数；PCM 只通过这组接口取数据
// ============================================================================
class ICamera
{
public:
    virtual ~ICamera() = default;

    // PCM 每帧调用：把自己的参数填进 outView（≈ CalcCamera）
    virtual void GetCameraView(CameraViewInfo& outView) const = 0;

    // 每帧行为（输入驱动等）；无行为相机不实现它
    virtual void Tick(float dt, const Input& input) {}

    virtual const char* GetName() const = 0;

    // 激活：把自己绑定为 PCM 的 ViewTarget（≈ 相机向 PCM 注册"现在看我"）
    void BindTo(CameraManager& pcm);
};

// 自由相机：右键环顾 + WASD/QE 飞行（≈ 引擎编辑器飞行相机）
class FreeCamera : public ICamera
{
public:
    FreeCamera();

    void GetCameraView(CameraViewInfo& outView) const override { outView = m_view; }
    void Tick(float dt, const Input& input) override;
    const char* GetName() const override { return "FreeCamera"; }

private:
    CameraViewInfo m_view;   // 自己持有的视角参数（位置/朝向/FOV...）
};

// 固定机位：复刻 M2 的视角，无任何行为（验证"多类型相机切换"的最小例子）
class FixedCamera : public ICamera
{
public:
    FixedCamera();

    void GetCameraView(CameraViewInfo& outView) const override { outView = m_view; }
    const char* GetName() const override { return "FixedCamera"; }

private:
    CameraViewInfo m_view;
};
