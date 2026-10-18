#include "Window.h"
#include <cstdio>

namespace
{
    const wchar_t kWndClassName[] = L"TransparentCubeWindowClass";
}

bool Window::Create(int width, int height, const wchar_t* title)
{
    m_width  = width;
    m_height = height;

    HINSTANCE hInstance = GetModuleHandleW(nullptr);

    WNDCLASSEXW wc   = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &Window::StaticWndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWndClassName;
    if (!RegisterClassExW(&wc))
    {
        printf("[Window] RegisterClassEx 失败: %lu\n", GetLastError());
        return false;
    }

    // 让"客户区"恰好是 width×height（而不是含边框的窗口外沿尺寸）
    const DWORD style = WS_OVERLAPPEDWINDOW;
    RECT rc = { 0, 0, width, height };
    AdjustWindowRect(&rc, style, FALSE);

    m_hwnd = CreateWindowExW(
        0, kWndClassName, title, style,
        CW_USEDEFAULT, CW_USEDEFAULT,
        rc.right - rc.left, rc.bottom - rc.top,
        nullptr, nullptr, hInstance,
        this);   // 通过创建参数把 this 传给 WM_NCCREATE，绑定到窗口句柄上
    if (!m_hwnd)
    {
        printf("[Window] CreateWindowEx 失败: %lu\n", GetLastError());
        return false;
    }

    ShowWindow(m_hwnd, SW_SHOW);
    return true;
}

Window::~Window()
{
    if (m_hwnd)
    {
        DestroyWindow(m_hwnd);
        m_hwnd = nullptr;
    }
    UnregisterClassW(kWndClassName, GetModuleHandleW(nullptr));
}

bool Window::PumpMessages()
{
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
    {
        if (msg.message == WM_QUIT)
            m_shouldQuit = true;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return !m_shouldQuit;
}

bool Window::ConsumeResizeFlag()
{
    const bool resized = m_resized;
    m_resized = false;
    return resized;
}

LRESULT CALLBACK Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    // 经典技巧：窗口创建时把 this 存进用户数据区，之后每条消息都能取回实例
    Window* self = nullptr;
    if (msg == WM_NCCREATE)
    {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
        self = static_cast<Window*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    else
    {
        self = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (self)
    {
        switch (msg)
        {
        case WM_SIZE:   // 尺寸变化（含最大化/还原）：记录新的客户区大小
            self->m_width   = LOWORD(lParam);
            self->m_height  = HIWORD(lParam);
            self->m_resized = true;
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE)
                DestroyWindow(hwnd);   // 走正常销毁流程（会触发 WM_DESTROY）
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);        // 让 PumpMessages 收到 WM_QUIT，主循环退出
            return 0;
        }
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}
