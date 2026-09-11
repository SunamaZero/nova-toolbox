#pragma once
// Tab5 工具箱 OTA 组件 —— 只做固件升级逻辑，不含 UI（UI 在 app 层）
// 协议：GET <base>/ → {"version":"x.y.z"}   GET <base>/firmware.bin → 固件
#include <string>
#include "esp_err.h"

namespace ota_client {

// 当前运行固件版本（来自 esp_app_get_description，与 PROJECT_VER 一致）
std::string currentVersion();

// 查询服务器版本。返回 ESP_OK 时 out_version 有效。
esp_err_t check(const std::string& base_url, std::string& out_version);

// 执行 OTA（阻塞）。progress_cb 可空，收到 0..100。成功后设备重启，不返回。
esp_err_t upgrade(const std::string& base_url, void (*progress_cb)(int));

}  // namespace ota_client
