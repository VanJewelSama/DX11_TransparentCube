#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

// Win32 窗口封装（≈ UE 的 FWindowsWindow）
// 职责：注册窗口类、创建窗口、泵消息；把窗口事件翻译成简单标志位给上层
class Window
{
public:
    Window() = default;
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(int width, int height, const wchar_t* title);

    // 处理本帧到达的所有消息；返回 false = 用户要求退出（关窗 / 按 ESC）
    bool PumpMessages();

    // 窗口尺寸是否变化过；读取一次后自动清除（Renderer 据此重建后备缓冲）
    bool ConsumeResizeFlag();

    HWND GetHandle() const { return m_hwnd; }
    int  GetWidth()  const { return m_width; }
    int  GetHeight() const { return m_height; }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    HWND m_hwnd       = nullptr;
    int  m_width      = 0;
    int  m_height     = 0;
    bool m_shouldQuit = false;
    bool m_resized    = false;
};
