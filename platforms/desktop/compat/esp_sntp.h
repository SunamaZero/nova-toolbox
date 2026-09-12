// 桌面端 esp_sntp.h 桩（PC 上没有 SNTP，只需让代码编过）
#pragma once

#include <ctime>
#include <cstring>

typedef enum {
    ESP_SNTP_OPMODE_POLL = 0,
    ESP_SNTP_OPMODE_LISTENONLY = 1,
} esp_sntp_operatingmode_t;

typedef enum {
    SNTP_SYNC_STATUS_RESET = 0,
    SNTP_SYNC_STATUS_COMPLETED = 1,
    SNTP_SYNC_STATUS_IN_PROGRESS = 2,
} sntp_sync_status_t;

inline void esp_sntp_setoperatingmode(esp_sntp_operatingmode_t) {}
inline void esp_sntp_setservername(int, const char*) {}
inline void esp_sntp_init(void) {}
inline void esp_sntp_restart(void) {}
inline sntp_sync_status_t sntp_get_sync_status(void) { return SNTP_SYNC_STATUS_COMPLETED; }
