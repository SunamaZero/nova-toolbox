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






void AppToolbox::onCreate()
{
    mclog::tagInfo(getAppInfo().name, "on create");

    open();
}

void AppToolbox::onOpen()
{
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
