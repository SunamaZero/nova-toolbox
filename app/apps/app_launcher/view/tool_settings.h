// Tab5 工具箱 — 设置页（WiFi 连接 + OTA 固件升级）
// 由原来的 WIFI / OTA 两个独立页合并而来：设备配置类的东西集中在一处。
// 顶栏的状态显示（WiFi/电量）在 toolbox_home.cpp，本页只做配置。

#pragma once

#include "toolbox_windows.h"
#include "toolbox_theme.h"
#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>

namespace launcher_view {

/**
 * @brief 设置页：WiFi STA 连接（连路由器）+ OTA 服务器配置与固件升级
 */
class SettingsToolWindow : public ui::Window {
public:
    SettingsToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    void buildWifiSection(lv_obj_t* parent, int y);
    void buildOtaSection(lv_obj_t* parent, int y);

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;

    // ---- WiFi 区 ----
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _wifi_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _ssid_ta;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _pass_ta;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_connect;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _wifi_state;

    // ---- OTA 区 ----
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _ota_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _url_ta;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_check;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_upgrade;
    lv_obj_t* _bar = nullptr;   // 原生 lv_bar
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _ota_state;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_close;

    uint32_t _tick = 0;
};

}  // namespace launcher_view
