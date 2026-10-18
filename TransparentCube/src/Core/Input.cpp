#include "Input.h"
#include <cstring>

void Input::NewFrame()
{
    // ---- 键盘：滚动一帧，重新采样 256 个虚拟键 ----
    std::memcpy(m_prev, m_curr, sizeof(m_curr));
    for (int k = 0; k < 256; ++k)
        m_curr[k] = (GetAsyncKeyState(k) & 0x8000) ? 0x80 : 0;

    // ---- 鼠标：右键按住 = 视角模式（隐藏光标、锁定在窗口中心、上报相对位移）----
    m_mouseDX = 0.0f;
    m_mouseDY = 0.0f;

    const bool rmbDown = IsKeyDown(VK_RBUTTON);
    if (rmbDown)
    {
        if (!m_looking)
        {
            // 进入视角模式：隐藏光标并钉到客户区中心（首帧不报位移，防止跳变）
            m_looking = true;
            RECT rc;
            GetClientRect(m_hwnd, &rc);
            POINT center = { (rc.right - rc.left) / 2, (rc.bottom - rc.top) / 2 };
            ClientToScreen(m_hwnd, &center);
            m_lockCenter = center;
            SetCursorPos(center.x, center.y);
            ShowCursor(FALSE);
        }
        else
        {
            POINT p;
            GetCursorPos(&p);
            m_mouseDX = static_cast<float>(p.x - m_lockCenter.x);
            m_mouseDY = static_cast<float>(p.y - m_lockCenter.y);
            SetCursorPos(m_lockCenter.x, m_lockCenter.y);   // 甩回中心，等效无限鼠标区
        }
    }
    else if (m_looking)
    {
        m_looking = false;
        ShowCursor(TRUE);
    }
}
