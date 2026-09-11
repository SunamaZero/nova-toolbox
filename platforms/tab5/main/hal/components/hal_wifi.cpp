/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
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
#include "esp_wifi_default.h"   // esp_netif_attach_wifi_station
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_wifi_default.h>
#include <esp_wifi_netif.h>
#include <esp_private/wifi.h>
#include <esp_http_server.h>

#define TAG "wifi"

#define WIFI_SSID    "M5Tab5-UserDemo-WiFi"
#define WIFI_PASS    ""
#define MAX_STA_CONN 4

static bool s_wifi_ap_netif_started = false;
static esp_netif_t* s_ap_netif = nullptr;   // 供 STA 连接时拆掉 AP

static void wifi_remote_ap_start_handler(void* arg, esp_event_base_t base, int32_t event_id, void* data)
{
    auto* netif = static_cast<esp_netif_t*>(arg);
    if (s_wifi_ap_netif_started || esp_netif_is_netif_up(netif)) {
        ESP_LOGW(TAG, "ignore duplicate Wi-Fi AP start event");
        return;
    }

    auto driver = static_cast<wifi_netif_driver_t>(esp_netif_get_io_driver(netif));
    uint8_t mac[6];
    esp_err_t ret = esp_wifi_get_if_mac(driver, mac);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_get_if_mac failed: %s", esp_err_to_name(ret));
        return;
    }

    if (esp_wifi_is_if_ready_when_started(driver)) {
        ret = esp_wifi_register_if_rxcb(driver, esp_netif_receive, netif);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_register_if_rxcb failed: %s", esp_err_to_name(ret));
            return;
        }
    }

    ret = esp_wifi_internal_reg_netstack_buf_cb(esp_netif_netstack_buf_ref, esp_netif_netstack_buf_free);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "netstack cb register failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_netif_set_mac(netif, mac);
    esp_netif_action_start(netif, base, event_id, data);
    s_wifi_ap_netif_started = true;
}

static void wifi_remote_ap_stop_handler(void* arg, esp_event_base_t base, int32_t event_id, void* data)
{
    auto* netif = static_cast<esp_netif_t*>(arg);
    if (!s_wifi_ap_netif_started && !esp_netif_is_netif_up(netif)) {
        ESP_LOGW(TAG, "ignore duplicate Wi-Fi AP stop event");
        return;
    }

    esp_netif_action_stop(netif, base, event_id, data);
    s_wifi_ap_netif_started = false;
}

static esp_netif_t* create_wifi_remote_ap_netif()
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_AP();
    esp_netif_t* netif     = esp_netif_new(&cfg);
    assert(netif);
    s_ap_netif = netif;

    ESP_ERROR_CHECK(esp_netif_attach_wifi_ap(netif));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_START, wifi_remote_ap_start_handler, netif));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STOP, wifi_remote_ap_stop_handler, netif));

    return netif;
}

// STA netif：照 AP 的写法（手动 new + attach），remote 架构下不能用 esp_netif_create_default_wifi_sta
// （那个内部会再 add 一次 → assert "netif already added"）
// 正确 API 是 esp_netif_attach_wifi_station()，定义在 esp_wifi_default.h
static esp_netif_t* create_wifi_remote_sta_netif()
{
    esp_netif_config_t cfg = ESP_NETIF_DEFAULT_WIFI_STA();
    esp_netif_t* netif     = esp_netif_new(&cfg);
    assert(netif);

    ESP_ERROR_CHECK(esp_netif_attach_wifi_station(netif));
    return netif;
}

// HTTP 处理函数
esp_err_t hello_get_handler(httpd_req_t* req)
{
    const char* html_response = R"rawliteral(
        <!DOCTYPE html>
        <html>
        <head>
            <title>Hello</title>
            <style>
                body {
                    display: flex;
                    flex-direction: column;
                    justify-content: center;
                    align-items: center;
                    height: 100vh;
                    margin: 0;
                    font-family: sans-serif;
                    background-color: #f0f0f0;
                }
                h1 {
                    font-size: 48px;
                    color: #333;
                    margin: 0;
                }
                p {
                    font-size: 18px;
                    color: #666;
                    margin-top: 10px;
                }
            </style>
        </head>
        <body>
            <h1>Hello World</h1>
            <p>From M5Tab5</p>
        </body>
        </html>
    )rawliteral";

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, html_response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// URI 路由
httpd_uri_t hello_uri = {.uri = "/", .method = HTTP_GET, .handler = hello_get_handler, .user_ctx = nullptr};

// 启动 Web Server
httpd_handle_t start_webserver()
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = nullptr;

    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_register_uri_handler(server, &hello_uri);
    }
    return server;
}

// 初始化 Wi-Fi AP 模式
void wifi_init_softap()
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    create_wifi_remote_ap_netif();
    create_wifi_remote_sta_netif();   // STA 接口（连路由器必需），必须在 esp_wifi_init 之前

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t wifi_config = {};
    std::strncpy(reinterpret_cast<char*>(wifi_config.ap.ssid), WIFI_SSID, sizeof(wifi_config.ap.ssid));
    std::strncpy(reinterpret_cast<char*>(wifi_config.ap.password), WIFI_PASS, sizeof(wifi_config.ap.password));
    wifi_config.ap.ssid_len       = std::strlen(WIFI_SSID);
    wifi_config.ap.max_connection = MAX_STA_CONN;
    wifi_config.ap.authmode       = WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi-Fi AP started. SSID:%s password:%s", WIFI_SSID, WIFI_PASS);
}

static void wifi_ap_test_task(void* param)
{
    wifi_init_softap();
    start_webserver();

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    vTaskDelete(NULL);
}

bool HalEsp32::wifi_init()
{
    mclog::tagInfo(TAG, "wifi init");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    xTaskCreate(wifi_ap_test_task, "ap", 4096, nullptr, 5, nullptr);
    return true;
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

void HalEsp32::startWifiAp()
{
    wifi_init();
}

// ==================== STA 连接（客户端模式）====================
static bool s_sta_connected       = false;
static char s_sta_ip[32]          = "-";
static bool s_sta_events_registered = false;

static void wifi_sta_event_handler(void* arg, esp_event_base_t base, int32_t id, void* data)
{
    // 诊断：把 STA 生命周期事件全打出来（reason 码是定位连接失败的关键）
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
        ESP_LOGW(TAG, "[evt] STA_DISCONNECTED reason=%d ssid_len=%d", d ? (int)d->reason : -1, d ? (int)d->ssid_len : -1);
        s_sta_connected = false;
        strncpy(s_sta_ip, "-", sizeof(s_sta_ip));
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        auto* e = static_cast<ip_event_got_ip_t*>(data);
        s_sta_connected = true;
        snprintf(s_sta_ip, sizeof(s_sta_ip), IPSTR, IP2STR(&e->ip_info.ip));
        ESP_LOGI(TAG, "STA got IP: %s", s_sta_ip);
    }
}

bool HalEsp32::wifiConnectSta(const char* ssid, const char* pass)
{
    ESP_LOGI(TAG, ">>> wifiConnectSta ENTER ssid=%s", ssid ? ssid : "(null)");
    if (!ssid || !ssid[0]) {
        ESP_LOGE(TAG, "STA connect: empty ssid");
        return false;
    }

    // 照 Slave_I（同硬件开源项目）的做法：AP 必须彻底拆掉，STA 才连得出去。
    // 之前保留 AP 切 APSTA，C6 侧 STA 会一直 reason=210 (NO_AP_FOUND) —— scan 扫得到却连不上。
    ESP_LOGI(TAG, "STA: tearing down AP first...");
    esp_wifi_stop();
    vTaskDelay(pdMS_TO_TICKS(200));
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(200));
    if (s_ap_netif) {
        esp_netif_destroy(s_ap_netif);
        s_ap_netif = nullptr;
    }
    s_wifi_ap_netif_started = false;

    // 全新初始化（纯 STA）—— 照 Slave_I：netif 栈也要重新 init，否则 esp_netif_new 返回 NULL
    esp_netif_init();
    esp_event_loop_create_default();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t e = esp_wifi_init(&cfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "STA: wifi_init failed: %s", esp_err_to_name(e)); return false; }

    if (!s_sta_events_registered) {
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, nullptr, nullptr);
        esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_sta_event_handler, nullptr, nullptr);
        s_sta_events_registered = true;
    }

    // 照 Slave_I 的 do_verify：assoc-only 阶段不建 netif（它的注释明确写 "No netif needed"）。
    // 先验证 AP 拆掉后 STA 能不能连上，再解决拿 IP 的问题。

    wifi_config_t wcfg = {};
    strncpy(reinterpret_cast<char*>(wcfg.sta.ssid), ssid, sizeof(wcfg.sta.ssid) - 1);
    if (pass) strncpy(reinterpret_cast<char*>(wcfg.sta.password), pass, sizeof(wcfg.sta.password) - 1);
    wcfg.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    e = esp_wifi_set_mode(WIFI_MODE_STA);
    if (e != ESP_OK) ESP_LOGE(TAG, "STA: set_mode failed: %s", esp_err_to_name(e));
    e = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    if (e != ESP_OK) { ESP_LOGE(TAG, "STA: set_config failed: %s", esp_err_to_name(e)); return false; }
    e = esp_wifi_start();
    if (e != ESP_OK) ESP_LOGE(TAG, "STA: start failed: %s", esp_err_to_name(e));
    e = esp_wifi_connect();
    if (e != ESP_OK) { ESP_LOGE(TAG, "STA: connect failed: %s", esp_err_to_name(e)); return false; }

    ESP_LOGI(TAG, "STA connecting to SSID:%s (AP torn down)", ssid);
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
