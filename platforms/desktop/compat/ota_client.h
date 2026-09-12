// 桌面桩：ota_client.h（不真实升级）
#pragma once
#include <string>
#include <functional>
#include "esp_err.h"
namespace ota_client {
inline std::string currentVersion() {
#ifdef NOVA_FW_VER
    return NOVA_FW_VER;    // 桌面预览也显示真实固件版本（CMake 注入）
#else
    return "desktop";
#endif
}
inline esp_err_t check(const std::string&, std::string& out) { out = "desktop"; return ESP_FAIL; }
inline esp_err_t upgrade(const std::string&, std::function<void(int)>) { return ESP_FAIL; }
}
