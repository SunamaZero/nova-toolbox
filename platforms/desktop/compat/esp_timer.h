// 桌面端 esp_timer.h 桩（PC 上没有 ESP 定时器，用单调时钟模拟开机时间）
#pragma once

#include <chrono>
#include <cstdint>

inline int64_t esp_timer_get_time(void)
{
    static const auto t0 = std::chrono::steady_clock::now();
    const auto d = std::chrono::steady_clock::now() - t0;
    return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
}
