// 桌面端 FreeRTOS 兼容层（仅用于 PC 上预览 UI）
// 提供 vTaskDelay / pdMS_TO_TICKS 等最小实现，让 UI 代码能编过
#pragma once

#include <thread>
#include <chrono>
#include <cstdint>

#define pdMS_TO_TICKS(ms) (static_cast<uint32_t>(ms))
#define pdPASS            (1)
#define pdFAIL            (0)
#define pdTRUE            (1)
#define pdFALSE           (0)
#define portMAX_DELAY     (0xFFFFFFFFu)

typedef int      BaseType_t;
typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef void (*TaskFunction_t)(void*);
inline int xTaskCreate(TaskFunction_t, const char*, uint32_t, void*, uint32_t, TaskHandle_t*) { return 1; }
inline int xTaskCreatePinnedToCore(TaskFunction_t, const char*, uint32_t, void*, uint32_t, TaskHandle_t*, int) { return 1; }
inline void vTaskDelayUntil(TickType_t*, TickType_t) {}

inline void vTaskDelay(uint32_t ticks) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}
inline void vTaskDelete(void*) {}
inline uint32_t xTaskGetTickCount() {
    static auto t0 = std::chrono::steady_clock::now();
    auto d = std::chrono::steady_clock::now() - t0;
    return (uint32_t)std::chrono::duration_cast<std::chrono::milliseconds>(d).count();
}
