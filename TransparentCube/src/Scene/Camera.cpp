#include "Camera.h"
#include "CameraManager.h"
#include "Core/Input.h"

using namespace DirectX;

void ICamera::BindTo(CameraManager& pcm)
{
    pcm.SetViewTarget(this);
}

// ---------------------------------------------------------------------------
// FreeCamera
// ---------------------------------------------------------------------------
FreeCamera::FreeCamera()
{
    m_view.Position = { 0.0f, 1.5f, -4.0f };
    m_view.Yaw      = 0.0f;
    m_view.Pitch    = -0.36f;   // 略微俯视原点
}

void FreeCamera::Tick(float dt, const Input& input)
{
    // ---- 视角：右键按住期间鼠标驱动 ----
    if (input.IsMouseLookActive())
    {
        constexpr float kSensitivity = 0.003f;   // 弧度/像素
        m_view.Yaw   += input.GetMouseDeltaX() * kSensitivity;
        m_view.Pitch -= input.GetMouseDeltaY() * kSensitivity;

        // 俯仰限位：防止万向锁（LookTo 的 up 与前向共线）
        constexpr float kMaxPitch = 1.55f;   // ≈ 88.8°
        if (m_view.Pitch >  kMaxPitch) m_view.Pitch =  kMaxPitch;
        if (m_view.Pitch < -kMaxPitch) m_view.Pitch = -kMaxPitch;
    }

    // ---- 移动：WASD 前后左右，Q/E 降升，Shift 加速 ----
    const float speed = input.IsKeyDown(VK_SHIFT) ? 12.0f : 3.0f;
    const float moveFwd   = (input.IsKeyDown('W') ? 1.0f : 0.0f) - (input.IsKeyDown('S') ? 1.0f : 0.0f);
    const float moveRight = (input.IsKeyDown('D') ? 1.0f : 0.0f) - (input.IsKeyDown('A') ? 1.0f : 0.0f);
    const float moveUp    = (input.IsKeyDown('E') ? 1.0f : 0.0f) - (input.IsKeyDown('Q') ? 1.0f : 0.0f);
    if (moveFwd == 0.0f && moveRight == 0.0f && moveUp == 0.0f)
        return;

    const XMVECTOR fwd   = m_view.GetForward();
    const XMVECTOR up    = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
    const XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, fwd));

    XMVECTOR pos = XMLoadFloat3(&m_view.Position);
    pos += fwd   * (moveFwd   * speed * dt);
    pos += right * (moveRight * speed * dt);
    pos += up    * (moveUp    * speed * dt);
    XMStoreFloat3(&m_view.Position, pos);
}

// ---------------------------------------------------------------------------
// FixedCamera
// ---------------------------------------------------------------------------
FixedCamera::FixedCamera()
{
    m_view.Position = { 0.0f, 1.5f, -4.0f };
    m_view.Yaw      = 0.0f;
    m_view.Pitch    = -0.36f;
}
