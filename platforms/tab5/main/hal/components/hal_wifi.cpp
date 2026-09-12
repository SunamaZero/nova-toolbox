/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
/*
 * Tab5 工具箱 WiFi —— 纯 STA（客户端）实现
 *
 * 设计决定（2026-09-11）：移除官方那套 AP 配网。
 *   理由：设备要带出门用，换网络时「手机连热点→开网页填密码」太累赘；
 *         直接在设备设置页输入 SSID/密码，随时可改。
 *
 * AP 与 STA 抢同一个射频，是之前 STA 连不出去（reason=210 NO_AP_FOUND）的根源。
 * 现在只跑 STA，初始化顺序照官方 esp32-p4-wifi-starter：
 *   nvs → esp_netif_init → event_loop → 建 STA netif → esp_wifi_init → set_mode → set_config → start → connect
 *
 * 凭据存 NVS，重启后自动重连（设置页改了就覆盖）。
 */
#include "hal/hal_esp32.h"
#include <mooncake_log.h>
#include <vector>
#include <memory>
#include <string.h>
#include <string>
#include <bsp/m5stack_tab5.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_wifi.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_wifi_default.h>

#define TAG "wifi"

// ---- STA 状态 ----
static bool s_sta_connected        = false;
static bool s_sta_events_registered = false;
static bool s_wifi_inited           = false;
static char s_sta_ip[32]            = "-";

// 凭据的 NVS 键
#define NVS_NS   "wifi_cfg"
#define NVS_SSID "ssid"
#define NVS_PASS "pass"

static void wifi_sta_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    if (base == WIFI_EVENT) {
        ESP_LOGI(TAG, "[evt] WIFI_EVENT id=%d", (int)id);
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "[evt] STA_START -> connecting...");
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG, "[evt] STA_CONNECTED (waiting for IP)");
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        auto* d = static_cast<wifi_event_sta_disconnected_t*>(data);
        ESP_LOGW(TAG, "[evt] STA_DISCONNECTED reason=%d ssid_len=%d",
                 d ? (int)d->reason : -1, d ? (int)d->ssid_len : -1);
        s_sta_connected = false;
        strncpy(s_sta_ip, "-", sizeof(s_sta_ip));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* e = static_cast<ip_event_got_ip_t*>(data);
        s_sta_connected = true;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "STA got IP: %s", s_sta_ip);
    }
}

// ---- NVS 凭据读写 ----
static bool nvs_load_cred(char* ssid, size_t ssid_len, char* pass, size_t pass_len)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return false;
    size_t sl = ssid_len, pl = pass_len;
    bool ok = (nvs_get_str(h, NVS_SSID, ssid, &sl) == ESP_OK) && ssid[0];
    if (ok) nvs_get_str(h, NVS_PASS, pass, &pl);
    nvs_close(h);
    return ok;
}

static void nvs_save_cred(const char* ssid, const char* pass)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_str(h, NVS_SSID, ssid ? ssid : "");
    nvs_set_str(h, NVS_PASS, pass ? pass : "");
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "cred saved to NVS");
}

// ---- 纯 STA 初始化（照官方 esp32-p4-wifi-starter 的顺序）----
bool HalEsp32::wifi_init()
{
    if (s_wifi_inited) return true;
    mclog::tagInfo(TAG, "wifi init (STA only, AP removed)");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "nvs init failed: %s", esp_err_to_name(ret));
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();      // 无 AP，标准 API 不再冲突

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    if (!s_sta_events_registered) {
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, nullptr, nullptr);
        esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, nullptr, nullptr);
        s_sta_events_registered = true;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_inited = true;
    ESP_LOGI(TAG, "wifi init done (pure STA)");

    // 有保存的凭据 → 开机自动重连
    char ssid[64] = {0}, pass[64] = {0};
    if (nvs_load_cred(ssid, sizeof(ssid), pass, sizeof(pass))) {
        ESP_LOGI(TAG, "auto-connecting to saved SSID: %s", ssid);
        this->wifiConnectSta(ssid, pass);
    }
    return true;
}

bool HalEsp32::wifiConnectSta(const char* ssid, const char* pass)
{
    if (!ssid || !ssid[0]) {
        ESP_LOGE(TAG, "STA connect: empty ssid");
        return false;
    }
    if (!s_wifi_inited) {
        wifi_init();
    }
    ESP_LOGI(TAG, "STA connecting to SSID:%s", ssid);

    wifi_config_t wcfg = {};
    strncpy(reinterpret_cast<char*>(wcfg.sta.ssid), ssid, sizeof(wcfg.sta.ssid) - 1);
    if (pass) strncpy(reinterpret_cast<char*>(wcfg.sta.password), pass, sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;   // 照官方示例
    wcfg.sta.scan_method        = WIFI_ALL_CHANNEL_SCAN;

    esp_err_t e = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "STA: set_config failed: %s", esp_err_to_name(e));
        return false;
    }
    esp_wifi_disconnect();      // 换网络时先断开旧的
    e = esp_wifi_connect();
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "STA: connect failed: %s", esp_err_to_name(e));
        return false;
    }

    nvs_save_cred(ssid, pass);  // 记住，重启自动连
    ESP_LOGI(TAG, "STA connect requested");
    return true;
}

bool HalEsp32::wifiIsStaConnected()
{
    return s_sta_connected;
}

std::string HalEsp32::wifiGetStaIp()
{
    return std::string(s_sta_ip);
}

void HalEsp32::setExtAntennaEnable(bool enable)
{
    _ext_antenna_enable = enable;
    mclog::tagInfo(TAG, "set ext antenna enable: {}", _ext_antenna_enable);
    bsp_set_ext_antenna_enable(_ext_antenna_enable);
}

bool HalEsp32::getExtAntennaEnable()
{
    return _ext_antenna_enable;
}

// AP 功能已按设计移除（AP 与 STA 抢射频；设备改为在设置页直接输入 SSID/密码）。
// 这个入口保留，但改为初始化纯 STA（原调用点：开机动画结束时）。
void HalEsp32::startWifiAp()
{
    ESP_LOGI(TAG, "AP removed by design -> init STA instead");
    wifi_init();
}
