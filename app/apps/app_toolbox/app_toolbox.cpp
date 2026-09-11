/*
 * Nova Toolbox 主 App 实现
 * 注意：LVGL 跑在独立任务（esp_lvgl_port），所有 UI 操作必须持 LvglLockGuard
 */
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "app_toolbox.h"
#include "../app_launcher/view/tool_screenshot.h"
#include <hal/hal.h>
#include <mooncake.h>
#include <mooncake_log.h>
#include <smooth_lvgl.h>
#include <lvgl.h>

using namespace mooncake;

AppToolbox::AppToolbox()
{
    setAppInfo().name = "AppToolbox";
}


// 开发期：开机后自动抓一张屏（无需人工点按钮），便于自检 UI
static void auto_shot_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(6000));
    vTaskDelay(pdMS_TO_TICKS(4000));
    ESP_LOGI("app-toolbox", "auto screenshot (dev)");
    screenshot::captureAndSend(4);
    vTaskDelete(nullptr);
}

// 自测：用扫到的第一个 AP 触发连接（只为观察 event/reason，不需要正确密码）
static void connect_probe_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(25000));
    ESP_LOGI("sta-probe", ">>> test connect to '902' (deliberately no password)");
    esp_err_t e = GetHAL()->wifiConnectSta("902", "");
    ESP_LOGI("sta-probe", ">>> wifiConnectSta('902') -> %d", (int)e);
    vTaskDelete(nullptr);
}


// 诊断：开机后扫一次 AP，验证 STA 侧是否真的可用（只读，不改 netif）
static void sta_probe_task(void*) {
    vTaskDelay(pdMS_TO_TICKS(12000));
    wifi_mode_t m = WIFI_MODE_NULL;
    esp_wifi_get_mode(&m);
    ESP_LOGI("sta-probe", "current mode = %d (1=STA 2=AP 3=APSTA)", (int)m);
    // 关键验证：切到 APSTA 看能不能成功（之前 STA netif 缺失时这里会失败）
    esp_err_t me = esp_wifi_set_mode(WIFI_MODE_APSTA);
    ESP_LOGI("sta-probe", "set_mode(APSTA) -> %s", esp_err_to_name(me));
    vTaskDelay(pdMS_TO_TICKS(600));

    uint16_t n = 0;
    esp_err_t e = esp_wifi_scan_start(nullptr, true);
    ESP_LOGI("sta-probe", "scan_start -> %s", esp_err_to_name(e));
    if (e == ESP_OK) {
        esp_wifi_scan_get_ap_num(&n);
        ESP_LOGI("sta-probe", "found %d APs (STA works!)", (int)n);
        if (n > 0) {
            wifi_ap_record_t recs[5] = {};
            uint16_t cnt = 5;
            if (esp_wifi_scan_get_ap_records(&cnt, recs) == ESP_OK) {
                for (int i = 0; i < cnt && i < 5; ++i)
                    ESP_LOGI("sta-probe", "  AP[%d] ssid='%s' rssi=%d ch=%d", i, recs[i].ssid, recs[i].rssi, recs[i].primary);
            }
        }
    }
    vTaskDelete(nullptr);
}

void AppToolbox::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    open();
}

void AppToolbox::onOpen()
{
    xTaskCreate(sta_probe_task, "sta_probe", 8192, nullptr, 4, nullptr);
    xTaskCreate(connect_probe_task, "connect_probe", 8192, nullptr, 4, nullptr);
    // auto_shot 暂时关闭（snapshot 崩溃未解决）
    // xTaskCreate(auto_shot_task, "auto_shot", 8192, nullptr, 4, nullptr);
    mclog::tagInfo(getAppInfo().name, "on open");

    LvglLockGuard lock;  // LVGL 在独立任务里跑，必须加锁

    _home = std::make_unique<launcher_view::ToolboxHome>();
    _home->init(lv_screen_active());
    _home->open(true);  // 立即全屏
}

void AppToolbox::onRunning()
{
    LvglLockGuard lock;

    if (_home) {
        _home->update();
    }
}

void AppToolbox::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    LvglLockGuard lock;

    _home.reset();
}
