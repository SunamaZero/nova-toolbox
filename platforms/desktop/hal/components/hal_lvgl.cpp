/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "../hal_desktop.h"
#include "../hal_config.h"
#include "hal/hal.h"
#include <mooncake_log.h>
#include <lvgl.h>
#include <mutex>
#include <thread>
#include <cstdio>
#include <cstdlib>
#include <assets/assets.h>

static const std::string _tag = "lvgl";
static std::mutex _lvgl_mutex;

// 中文字体（与真机一致：3755 汉字 + FontAwesome）。真机在 hal_esp32.cpp 里设置。
extern const lv_font_t tb_cn_16;

void HalDesktop::lvgl_init()
{
    mclog::tagInfo(_tag, "lvgl init");

    lv_init();

    lv_group_set_default(lv_group_create());

    // ------------------------------------------------------------------
    // 显示后端：X11（走 XWayland）
    //
    // 为什么不用 SDL：本机 SDL2 是 sdl2-compat（SDL3 模拟层），窗口能创建、
    //   渲染器里内容也对，但 Present 到屏幕这一步失效 —— 窗口全黑。
    //   而 LVGL 自带的 Wayland 后端在 niri 下会卡死在 lv_wayland_window_create。
    //   X11 后端最简单可靠，且 XWayland 已经在跑（DISPLAY=:0）。
    //   注意：输入设备（鼠标/键盘）由 x11 驱动内部接管，不需要手动注册 indev。
    // ------------------------------------------------------------------
    lv_display_t* display = lv_x11_window_create("LVGL Simulator", HAL_SCREEN_WIDTH, HAL_SCREEN_HEIGHT);
    if (!display) {
        mclog::tagError(_tag, "x11 window create failed (DISPLAY={})",
                        getenv("DISPLAY") ? getenv("DISPLAY") : "(null)");
        return;
    }
    lv_display_set_default(display);

    // ★ 必须显式创建输入设备：LVGL 的 X11 驱动不会自动建，
    //   少了这行鼠标/键盘完全没反应（窗口能看不能点）。
    //   文档：lv_x11_inputs_create(disp, mouse_img) —— 传 NULL 用默认光标。
    lv_x11_inputs_create(display, NULL);

    // 屏幕默认字体：不设的话文本走 LV_FONT_DEFAULT（montserrat_14，纯拉丁），中文渲染成方框
    lv_theme_t* th = lv_theme_default_init(display,
                                           lv_palette_main(LV_PALETTE_BLUE),
                                           lv_palette_main(LV_PALETTE_RED),
                                           true, &tb_cn_16);
    lv_display_set_theme(display, th);
    lv_obj_set_style_text_font(lv_screen_active(), &tb_cn_16, 0);
    lv_obj_set_style_text_font(lv_layer_top(), &tb_cn_16, 0);
    lv_obj_set_style_text_font(lv_layer_sys(), &tb_cn_16, 0);
    mclog::tagInfo(_tag, "default font -> tb_cn_16 (中文)");

#if not defined(__APPLE__) && not defined(__MACH__)
    std::thread([]() {
        while (!hal::Check()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        while (true) {
            GetHAL()->lvglLock();
            auto time_till_next = lv_timer_handler();
            GetHAL()->lvglUnlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(time_till_next));
        }
    }).detach();
#endif
}

void HalDesktop::lvglLock()
{
    _lvgl_mutex.lock();
}

void HalDesktop::lvglUnlock()
{
    _lvgl_mutex.unlock();
}
