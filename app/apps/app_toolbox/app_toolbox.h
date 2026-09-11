/*
 * Nova Toolbox 主 App —— 替代官方 Launcher 作为开机主界面
 * 直接承载全屏工具箱主页（ToolboxHome）
 */
#pragma once
#include "apps/app_launcher/view/toolbox_home.h"
#include <mooncake.h>
#include <memory>

class AppToolbox : public mooncake::AppAbility {
public:
    AppToolbox();

    // 生命周期回调
    void onCreate() override;
    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    std::unique_ptr<launcher_view::ToolboxHome> _home;
};
