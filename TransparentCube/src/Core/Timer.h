#pragma once

#include <chrono>

// 帧间隔计时（≈ UE FApp::GetDeltaTime 的最小版）
// 头文件实现即可，无需 .cpp
class Timer
{
public:
    Timer() { Reset(); }

    void Reset() { m_last = Clock::now(); }

    // 返回距上次 Tick 经过的秒数
    float Tick()
    {
        const auto now = Clock::now();
        const float dt = std::chrono::duration<float>(now - m_last).count();
        m_last = now;
        return dt;
    }

private:
    using Clock = std::chrono::steady_clock;
    Clock::time_point m_last;
};
