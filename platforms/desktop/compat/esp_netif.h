// 桌面桩：esp_netif.h
#pragma once
#include "esp_err.h"
typedef struct esp_netif* esp_netif_t;
inline esp_err_t esp_netif_init(void) { return ESP_OK; }
