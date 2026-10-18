#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// 输入状态（≈ UE UPlayerInput 的超简化版）
// 每帧 NewFrame() 采样一次：键盘"按住/本帧刚按下"、鼠标右键视角模式与位移增量
class Input
{
public:
    void Initialize(HWND hwnd) { m_hwnd = hwnd; }

    // 每帧开头调用：滚动 prev/curr，采样键盘与鼠标
    void NewFrame();

    bool IsKeyDown(int vk) const     { return (m_curr[vk & 0xFF] & 0x80) != 0; }   // 当前按住
    bool WasKeyPressed(int vk) const                                                 // 本帧刚按下（边沿）
    {
        return (m_curr[vk & 0xFF] & 0x80) != 0 && (m_prev[vk & 0xFF] & 0x80) == 0;
    }

    bool  IsMouseLookActive() const { return m_looking; }   // 右键按住（视角模式）中
    float GetMouseDeltaX() const { return m_mouseDX; }      // 本帧鼠标位移增量（像素）
    float GetMouseDeltaY() const { return m_mouseDY; }

private:
    HWND          m_hwnd = nullptr;
    unsigned char m_curr[256] = {};
    unsigned char m_prev[256] = {};
    bool          m_looking = false;   // 右键视角模式状态
    POINT         m_lockCenter = {};   // 锁定期间光标的固定位置（客户区中心）
    float         m_mouseDX = 0.0f;
    float         m_mouseDY = 0.0f;
};
