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
#include "../app_launcher/view/toolbox_theme.h"   // tb::bg()：屏幕底色要用设计 token
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

    // 【屏幕底色】M5 的开机动画 (app_startup_anim) 把 screen 底设成了纯白，
    // 而且退出时没有还原 -> 它一直留着。我们的窗口是圆角的、边缘有留白，
    // 于是四周露出这块白底：用户看到"取景框似的白色直角线条"，界面重绘时还会闪。
    // 开机动画本身不动（白底 + M5 logo 是它的设计），我们接手时改回深色即可。
    // 注：模拟器看不到这问题，因为它的默认主题 screen 底色本来就是深色。
    lv_obj_set_style_bg_color(lv_screen_active(), lv_color_hex(tb::bg()), 0);
    lv_obj_set_style_bg_opa(lv_screen_active(), LV_OPA_COVER, 0);

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
