#pragma once

#include "Camera.h"

// ============================================================================
// 相机管理器（≈ APlayerCameraManager 的单线程朴素版）
// 只持有 ViewTarget 指针 + 当前/上一帧两份视角缓存，不认具体相机类型；
// 渲染流程从它这里取视角信息，而不是从具体相机对象取
// ============================================================================
class CameraManager
{
public:
    // 由相机对象自己调用（ICamera::BindTo）：绑定为当前 ViewTarget
    void SetViewTarget(ICamera* cam) { m_viewTarget = cam; }
    ICamera* GetViewTarget() const { return m_viewTarget; }

    // 每帧一次：上一帧缓存滚到 Prev，再从 ViewTarget 拉取本帧参数
    // （≈ UE 每帧 UpdateCamera：LastFrameCameraCache = CameraCache; 重新 CalcCamera）
    void UpdateCache()
    {
        m_prevView = m_view;
        if (m_viewTarget)
            m_viewTarget->GetCameraView(m_view);
    }

    const CameraViewInfo& GetViewInfo() const     { return m_view; }      // 当前帧缓存
    const CameraViewInfo& GetPrevViewInfo() const { return m_prevView; }  // 上一帧缓存（插值/特效备用）

private:
    ICamera*       m_viewTarget = nullptr;
    CameraViewInfo m_view;       // 当前帧
    CameraViewInfo m_prevView;   // 上一帧
};
