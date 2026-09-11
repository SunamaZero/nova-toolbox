/*
 * Nova Toolbox for M5Stack Tab5
 * 占位工具窗口（未实现的工具显示"开发中"）
 */
#include "toolbox_windows.h"
#include "toolbox_theme.h"
#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/audio/audio.h>

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

PlaceholderToolWindow::PlaceholderToolWindow(const char* title) : _title(title)
{
    config.kfClosed = {20, 100, 90, 60, 0};
    config.kfOpened = {40, 30, 1200, 660, 255};
    config.bgColor  = tb::bg();
}

void PlaceholderToolWindow::onOpen()
{
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);

    _title_label = std::make_unique<Label>(_window->get());
    _title_label->align(LV_ALIGN_TOP_LEFT, 30, 20);
    _title_label->setText(_title.c_str());
    _title_label->setTextFont(tb::fontTitle());
    _title_label->setTextColor(lv_color_hex(0xE0E0F0));

    auto dev_label = std::make_unique<Label>(_window->get());
    dev_label->align(LV_ALIGN_CENTER, 0, -60);
    dev_label->setText("This tool is under development");
    dev_label->setTextFont(tb::fontBody());
    dev_label->setTextColor(lv_color_hex(0x8080A0));
    _dev_label = std::move(dev_label);

    _btn_close = std::make_unique<Button>(_window->get());
    _btn_close->setSize(150, 52);
    _btn_close->align(LV_ALIGN_CENTER, 0, 120);
    _btn_close->setBgColor(lv_color_hex(tb::danger()));
    _btn_close->setRadius(14);
    _btn_close->label().setTextFont(tb::fontTitle());
    _btn_close->label().setText("Close");
    _btn_close->onClick().connect([&] {
        audio::play_next_tone_progression();
        close();
    });
}

void PlaceholderToolWindow::onUpdate()
{
    // 无动态内容
}

void PlaceholderToolWindow::onClose()
{

}
