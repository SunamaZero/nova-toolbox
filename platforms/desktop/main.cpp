/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/hal_desktop.h"
#include <app.h>
#include <memory>
#include <hal/hal.h>
#include <lvgl.h>
#include <SDL.h>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include "apps/app_launcher/view/toolbox_home.h"

// ---- UI 截图：直接从 LVGL 的 draw buffer 导出 PPM ----
// 这台机器上 SDL 窗口渲染不出来（sdl2-compat + lv_sdl 组合），
// 但 LVGL 的渲染结果本身是好的，所以绕开 SDL，直接把 framebuffer 落盘。
#if LV_USE_SDL
static void dump_screen_ppm(const char* path)
{
    // 从 SDL renderer 直接读回像素。
    // 本机 sdl2-compat 导致窗口显示不出来，但 renderer 里的内容是对的，
    // SDL_RenderReadPixels 能把渲染结果整块取回来。
    lv_display_t* disp = lv_display_get_default();
    if (!disp) { printf("[shot] no display\n"); return; }

    SDL_Renderer* r = static_cast<SDL_Renderer*>(lv_sdl_window_get_renderer(disp));
    if (!r) { printf("[shot] no renderer\n"); return; }

    int w = lv_display_get_horizontal_resolution(disp);
    int h = lv_display_get_vertical_resolution(disp);
    printf("[shot] reading %dx%d from renderer\n", w, h);

    std::vector<uint32_t> px((size_t)w * h, 0);
    if (SDL_RenderReadPixels(r, NULL, SDL_PIXELFORMAT_ARGB8888, px.data(), w * 4) != 0) {
        printf("[shot] RenderReadPixels failed: %s\n", SDL_GetError());
        return;
    }

    FILE* f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint32_t v = px[i];
        fputc((v >> 16) & 0xFF, f);   // R
        fputc((v >> 8) & 0xFF, f);    // G
        fputc(v & 0xFF, f);           // B
    }
    fclose(f);
    printf("[shot] saved %s\n", path);
}
#else
// Wayland 后端：直接开窗口看，不需要截图
static void dump_screen_ppm(const char*) {}
#endif

int main(int argc, char** argv)
{
    // 应用层初始化回调
    app::InitCallback_t callback;

    callback.onHalInjection = []() {
        // 注入桌面平台的硬件抽象
        hal::Inject(std::make_unique<HalDesktop>());
    };

    // 启动应用层
    app::Init(callback);

    // 抓图时机（命令行：./app_desktop_build <等待秒数> <输出路径>）
    // 按墙钟时间而不是帧号——帧率不确定，启动动画约 3-4 秒
    const double shot_after_sec = (argc > 1) ? atof(argv[1]) : 10.0;
    const char* shot_path = (argc > 2) ? argv[2] : "/tmp/lvgl_shot.ppm";
    const int want_page = (argc > 3) ? atoi(argv[3]) : 0;   // 0=HOME, 1..7=工具页
    int shot_done = 0;
    int switched = 0;
    std::chrono::steady_clock::time_point t_switch;

    auto t_start = std::chrono::steady_clock::now();
    while (!app::IsDone()) {
        app::Update();
        if (!shot_done) {
            auto now = std::chrono::steady_clock::now();
            double el = std::chrono::duration<double>(now - t_start).count();
            if (!switched && el >= shot_after_sec) {
                if (want_page > 0) {
                    launcher_view::toolboxDebugShowPage(want_page);
                    t_switch = now;
                    switched = 1;
                } else {
                    dump_screen_ppm(shot_path);
                    shot_done = 1;
                }
            }
            if (switched && !shot_done && std::chrono::duration<double>(now - t_switch).count() > 2.0) {
                dump_screen_ppm(shot_path);
                shot_done = 1;
            }
        }
    }
    app::Destroy();

    return 0;
}
