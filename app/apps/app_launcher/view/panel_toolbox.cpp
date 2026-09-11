/*
 * Nova Toolbox 面板入口：一个卡片风格按钮 → 打开全屏工具箱主页
 */
#include "view.h"
#include "toolbox_theme.h"
#include "toolbox_home.h"

#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/audio/audio.h>

using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

using namespace launcher_view;

static const std::string _tag = "panel-toolbox";

void PanelToolbox::init()
{
    mclog::tagInfo(_tag, "init");

    // 入口按钮（琥珀金描边卡片，屏幕左侧中部）
    _btn = std::make_unique<Button>(lv_screen_active());
    _btn->setSize(240, 84);
    _btn->align(LV_ALIGN_LEFT_MID, 24, 0);
    _btn->setBgColor(lv_color_hex(tb::surface()));
    _btn->setBorderWidth(2);
    _btn->setBorderColor(lv_color_hex(tb::accent()));
    _btn->setRadius(12);
    _btn->label().setTextFont(tb::fontTitle());
    _btn->label().setTextColor(lv_color_hex(tb::accent()));
    _btn->label().setText("TOOLBOX");
    _btn->onClick().connect([&]() {
        audio::play_next_tone_progression();
        openHome();
    });

    // 开机直接进工具箱（不走官方 Launcher 界面）
    _auto_open = true;
}

void PanelToolbox::openHome()
{
    mclog::tagInfo(_tag, "open home");

    if (_window) {
        _window.reset();
    }

    _window = std::make_unique<ToolboxHome>();
    _window->init(lv_screen_active());
    _window->open(true);  // 立即全屏（无展开动画）
}

void PanelToolbox::update(bool isStacked)
{
    // 首次 update 时自动打开（Launcher 布局就绪后）
    if (_auto_open) {
        _auto_open = false;
        openHome();
    }

    if (_window) {
        _window->update();
        if (_window->getState() == ui::Window::State_t::Closed) {
            _window.reset();
            mclog::tagInfo(_tag, "home closed");
        }
    }
}
