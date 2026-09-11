/*
 * Nova Toolbox shell（参照 Slave_I AppShell 结构）
 * 布局：顶栏 StatusBar / 左导航栏 / 内容区 / 底栏 TaskBar
 * 页面：HOME（工具卡片网格）+ 6 个工具（全屏浮层）
 */
#include "toolbox_home.h"
#include "toolbox_theme.h"
#include "toolbox_windows.h"
#include "tool_kbd.h"
#include "tool_settings.h"
#include <hal/hal.h>
#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "net_tools.h"
#endif

#include <cstdint>
#include <cstdio>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mooncake_log.h>

using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

namespace launcher_view {

static const char* _tag = "toolbox-home";
static ToolboxHome* s_instance = nullptr;

namespace {
constexpr int NavRailWidth = 76;
constexpr int TopBarHeight = 52;
constexpr int TaskBarHeight = 34;

struct ToolDef {
    const char* nav;   // 导航短名
    const char* name;  // 卡片标题
    const char* desc;
    uint32_t accent;
};

const ToolDef _tools[6] = {
    {"UART", "UART", "RS485 monitor  HEX/ASCII", tb::accent()},
    {"UDP", "UDP", "broadcast / loopback", tb::info()},
    {"TCP", "TCP", "server :8888", tb::info()},
    {"HTTP", "HTTP", "capture :8080", tb::warning()},
    {"MQTT", "MQTT", "broker pub/sub", tb::success()},
    {"SETTINGS", "SETTINGS", "WiFi / OTA / power", tb::success()},
};

lv_obj_t* s_nav_btns[7] = {nullptr};  // 0=HOME, 1..6=工具（与 _tools 数量一致）
int s_active_page = 0;
bool s_home_rebuild_pending = false;
bool s_debug_auto_paged = false;   // DEBUG_AUTO_PAGE  // 防 async 重入
bool s_status_pending = false;
}  // namespace

static void async_show_page(void* user_data);
static void async_rebuild_home(void* user_data);
static void async_set_status(void* user_data);

static int s_pending_page = -1;

static void async_show_page(void* user_data)
{
    auto* self = static_cast<ToolboxHome*>(user_data);
    int page = s_pending_page;
    s_pending_page = -1;
    if (self && page >= 0) {
        self->showPage(page);
    }
}

// 事件回调里绝不直接改 UI（LVGL 渲染期 invalidate 会死锁）——只发异步请求
static void nav_click_cb(lv_event_t* e)
{
    int page = (int)(intptr_t)lv_event_get_user_data(e);
    if (!s_instance || page < 0) return;
    if (s_pending_page >= 0) return;  // 已有待处理请求
    s_pending_page = page;
    lv_async_call(async_show_page, s_instance);
}

ToolboxHome::ToolboxHome()
{
    config.kfClosed     = {640, 360, 4, 4, 0};
    config.kfOpened     = {0, 0, 1280, 720, 255};
    config.bgColor      = tb::bg();
    config.borderColor  = tb::border();
}

// ---- 导航栏按钮样式 ----
static lv_obj_t* make_nav_btn(lv_obj_t* parent, const char* text, int page)
{
    lv_obj_t* btn = lv_obj_create(parent);
    lv_obj_set_size(btn, NavRailWidth - 12, 56);
    lv_obj_set_style_bg_color(btn, lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(btn, lv_color_hex(tb::raised()), LV_STATE_PRESSED);

    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_font(lbl, tb::fontSm(), 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(tb::textDim()), 0);
    lv_obj_center(lbl);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_add_event_cb(btn, nav_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)page);
    return btn;
}

static void style_nav_active(int page)
{
    for (int i = 0; i < 6; ++i) {
        lv_obj_t* b = s_nav_btns[i];
        if (!b) continue;
        bool active = (i == page);
        lv_obj_set_style_bg_color(b, lv_color_hex(active ? tb::raised() : tb::surface()), 0);
        lv_obj_t* lbl = lv_obj_get_child(b, 0);
        if (lbl) {
            lv_obj_set_style_text_color(lbl, lv_color_hex(active ? tb::accent() : tb::textDim()), 0);
        }
    }
    s_active_page = page;
}

// ---- HOME 卡片网格 ----
static void build_home_cards(lv_obj_t* content)
{
    lv_obj_t* grid = lv_obj_create(content);
    lv_obj_set_size(grid, 1280 - NavRailWidth - 12 - 12, 720 - TopBarHeight - TaskBarHeight - 12);
    lv_obj_set_style_bg_opa(grid, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(grid, 0, 0);
    lv_obj_set_style_pad_all(grid, tb::SpaceLg, 0);
    lv_obj_set_style_pad_row(grid, tb::SpaceMd, 0);
    lv_obj_set_style_pad_column(grid, tb::SpaceMd, 0);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);

    for (int i = 0; i < 6; ++i) {
        lv_obj_t* card = lv_obj_create(grid);
        lv_obj_set_size(card, 340, 180);
        lv_obj_set_style_bg_color(card, lv_color_hex(tb::raised()), 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 12, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(tb::border()), 0);
        lv_obj_set_style_pad_all(card, tb::SpaceMd, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_color(card, lv_color_hex(tb::surface()), LV_STATE_PRESSED);

        lv_obj_t* accent = lv_obj_create(card);
        lv_obj_set_size(accent, 40, 4);
        lv_obj_align(accent, LV_ALIGN_TOP_LEFT, 0, 0);
        lv_obj_set_style_radius(accent, 2, 0);
        lv_obj_set_style_border_width(accent, 0, 0);
        lv_obj_set_style_bg_color(accent, lv_color_hex(_tools[i].accent), 0);
        lv_obj_set_style_bg_opa(accent, LV_OPA_COVER, 0);
        lv_obj_clear_flag(accent, LV_OBJ_FLAG_CLICKABLE);

        lv_obj_t* name = lv_label_create(card);
        lv_label_set_text(name, _tools[i].name);
        lv_obj_set_style_text_color(name, lv_color_hex(tb::text()), 0);
        lv_obj_set_style_text_font(name, tb::fontBig(), 0);
        lv_obj_align(name, LV_ALIGN_LEFT_MID, 0, -10);

        lv_obj_t* desc = lv_label_create(card);
        lv_label_set_text(desc, _tools[i].desc);
        lv_obj_set_style_text_color(desc, lv_color_hex(tb::textDim()), 0);
        lv_obj_set_style_text_font(desc, tb::fontSm(), 0);
        lv_obj_align(desc, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        // 点卡片 = 打开工具（page = i+1）
        lv_obj_add_event_cb(card, nav_click_cb, LV_EVENT_CLICKED, (void*)(intptr_t)(i + 1));
    }
}

void ToolboxHome::onOpen()
{
    s_instance = this;
    mclog::tagInfo(_tag, "shell open");

    // DEBUG_AUTO_PAGE 已关（免得每次开机跳页）

    lv_obj_t* win = _window->get();
    lv_obj_clean(win);
    lv_obj_set_style_pad_all(win, 0, 0);
    lv_obj_set_style_bg_color(win, lv_color_hex(tb::bg()), 0);
    // 中文默认字体：整棵树继承，避免未设字体的控件画成方框
    lv_obj_set_style_text_font(win, tb::fontBody(), 0);
    lv_obj_clear_flag(win, LV_OBJ_FLAG_SCROLLABLE);

    // ---- 顶栏 ----
    lv_obj_t* bar = lv_obj_create(win);
    lv_obj_set_size(bar, 1280, TopBarHeight);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(tb::border()), 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* brand = lv_label_create(bar);
    lv_label_set_text(brand, "NOVA TOOLBOX");
    lv_obj_set_style_text_color(brand, lv_color_hex(tb::accent()), 0);
    lv_obj_set_style_text_font(brand, tb::fontTitle(), 0);
    lv_obj_align(brand, LV_ALIGN_LEFT_MID, tb::SpaceLg, 0);

    // ---- 顶栏右侧：电量 + WiFi 状态 ----
    // 电量：INA226 读的母线电压（NP-F550 两颗锂电串联：6.0V 空 — 8.4V 满）
    _batt_text = lv_label_create(bar);
    lv_label_set_text(_batt_text, "--");
    lv_obj_set_style_text_color(_batt_text, lv_color_hex(tb::textDim()), 0);
    lv_obj_set_style_text_font(_batt_text, tb::fontSm(), 0);
    lv_obj_align(_batt_text, LV_ALIGN_RIGHT_MID, -tb::SpaceLg, 0);

    _wifi_text = lv_label_create(bar);
    lv_label_set_text(_wifi_text, "WiFi: --");
    lv_obj_set_style_text_color(_wifi_text, lv_color_hex(tb::textDim()), 0);
    lv_obj_set_style_text_font(_wifi_text, tb::fontSm(), 0);
    lv_obj_align(_wifi_text, LV_ALIGN_RIGHT_MID, -tb::SpaceLg - 150, 0);

    _status_text = lv_label_create(bar);
    lv_label_set_text(_status_text, "SYS READY");
    lv_obj_set_style_text_color(_status_text, lv_color_hex(tb::textDim()), 0);
    lv_obj_set_style_text_font(_status_text, tb::fontSm(), 0);
    lv_obj_align(_status_text, LV_ALIGN_RIGHT_MID, -tb::SpaceLg - 330, 0);

    // ---- 中间行：导航栏 + 内容区 ----
    lv_obj_t* middle = lv_obj_create(win);
    lv_obj_set_size(middle, 1280, 720 - TopBarHeight - TaskBarHeight);
    lv_obj_align(middle, LV_ALIGN_TOP_LEFT, 0, TopBarHeight);
    lv_obj_set_style_bg_opa(middle, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(middle, 0, 0);
    lv_obj_set_style_border_width(middle, 0, 0);
    lv_obj_set_style_pad_all(middle, 6, 0);
    lv_obj_set_style_pad_column(middle, 6, 0);
    lv_obj_set_flex_flow(middle, LV_FLEX_FLOW_ROW);
    lv_obj_clear_flag(middle, LV_OBJ_FLAG_SCROLLABLE);

    // 导航栏
    lv_obj_t* rail = lv_obj_create(middle);
    lv_obj_set_size(rail, NavRailWidth, 720 - TopBarHeight - TaskBarHeight - 12);
    lv_obj_set_style_bg_color(rail, lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_bg_opa(rail, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rail, 12, 0);
    lv_obj_set_style_border_width(rail, 0, 0);
    lv_obj_set_style_pad_all(rail, 6, 0);
    lv_obj_set_style_pad_row(rail, 6, 0);
    lv_obj_set_flex_flow(rail, LV_FLEX_FLOW_COLUMN);
    lv_obj_clear_flag(rail, LV_OBJ_FLAG_SCROLLABLE);

    s_nav_btns[0] = make_nav_btn(rail, "HOME", 0);
    for (int i = 0; i < 6; ++i) {
        s_nav_btns[i + 1] = make_nav_btn(rail, _tools[i].nav, i + 1);
    }

    // 内容区
    _content = lv_obj_create(middle);
    lv_obj_set_size(_content, 1280 - NavRailWidth - 12 - 12, 720 - TopBarHeight - TaskBarHeight - 12);
    lv_obj_set_style_bg_color(_content, lv_color_hex(tb::bg()), 0);
    lv_obj_set_style_bg_opa(_content, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_content, 12, 0);
    lv_obj_set_style_border_width(_content, 1, 0);
    lv_obj_set_style_border_color(_content, lv_color_hex(tb::border()), 0);
    lv_obj_set_style_pad_all(_content, 0, 0);
    lv_obj_clear_flag(_content, LV_OBJ_FLAG_SCROLLABLE);

    // ---- 底栏 ----
    lv_obj_t* taskbar = lv_obj_create(win);
    lv_obj_set_size(taskbar, 1280, TaskBarHeight);
    lv_obj_align(taskbar, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(taskbar, lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_bg_opa(taskbar, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(taskbar, 0, 0);
    lv_obj_set_style_border_width(taskbar, 1, 0);
    lv_obj_set_style_border_side(taskbar, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_color(taskbar, lv_color_hex(tb::border()), 0);
    lv_obj_set_style_pad_hor(taskbar, tb::SpaceMd, 0);
    lv_obj_clear_flag(taskbar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* ver = lv_label_create(taskbar);
    lv_label_set_text(ver, "Tab5 Toolbox  v1.0  |  field-armor theme");
    lv_obj_set_style_text_color(ver, lv_color_hex(tb::textDim()), 0);
    lv_obj_set_style_text_font(ver, tb::fontSm(), 0);
    lv_obj_align(ver, LV_ALIGN_LEFT_MID, 0, 0);

    // 默认显示 HOME
    build_home_cards(_content);
    style_nav_active(0);
}

void ToolboxHome::showPage(int page)
{
    mclog::tagInfo(_tag, "show page {}", page);

    if (page == 0) {
        // 回 HOME：先关掉工具窗口（触发 onClose 清理后台任务）
        if (_tool_window) {
            _tool_window->close(true, true);
            vTaskDelay(pdMS_TO_TICKS(300));  // 等后台任务退出（recv 超时 100ms + 收尾）
            _tool_window.reset();
            _opened_tool = -1;
        }
        // 重建卡片（延后到渲染安全时机）
        if (!s_home_rebuild_pending) {
            s_home_rebuild_pending = true;
            lv_async_call(async_rebuild_home, this);
        }
        style_nav_active(0);
        return;
    }

    // 工具页：开全屏浮层（关闭后回 shell）
    openTool(page - 1);
    style_nav_active(page);
}

void ToolboxHome::openTool(int id)
{
    if (_tool_window) {
        // 关键：triggerCallback=true 才会调 onClose()，那里停后台任务/关 socket/停 server
        _tool_window->close(true, true);
        // 等后台任务真正退出（UDP/TCP rx task 循环周期 50ms）
        vTaskDelay(pdMS_TO_TICKS(300));  // 等后台任务退出（recv 超时 100ms + 收尾）
        _tool_window.reset();
    }

    // 清空内容区（卡片/上一个工具），工具窗口嵌在内容区里
    if (_content) {
        lv_obj_clean(_content);
    }

    switch (id) {
    case 0: _tool_window = std::make_unique<SerialToolWindow>(); break;
#ifdef CONFIG_IDF_TARGET_ESP32P4
    case 1: _tool_window = std::make_unique<UdpToolWindow>(); break;
    case 2: _tool_window = std::make_unique<TcpToolWindow>(); break;
    case 3: _tool_window = std::make_unique<HttpToolWindow>(); break;
    case 4: _tool_window = std::make_unique<MqttToolWindow>(); break;
    case 5: _tool_window = std::make_unique<SettingsToolWindow>(); break;
#endif
    default: return;
    }

    _opened_tool = id;
    _tool_window->init(_content ? _content : lv_screen_active());
    _tool_window->open(true);
}

// ---- 渲染安全时机的 UI 变更（不能在 update/render 期间直接改 UI）----
static void async_rebuild_home(void* user_data)
{
    auto* self = static_cast<ToolboxHome*>(user_data);
    s_home_rebuild_pending = false;
    if (self && self->getState() == ui::Window::State_t::Opened) {
        self->rebuildHome();
    }
}

static void async_set_status(void* user_data)
{
    auto* self = static_cast<ToolboxHome*>(user_data);
    s_status_pending = false;
    if (!self) return;
    static char buf[64];
    snprintf(buf, sizeof(buf), "UPTIME %lus  |  %d TOOLS", (unsigned long)self->getUptimeSec(), 6);
    self->setStatusText(buf);
}

void ToolboxHome::onUpdate()
{
    // ---- 顶栏状态刷新（每 ~1s：电量 + WiFi）----
    static uint32_t _st_tick = 0;
    if (++_st_tick % 60 == 0) {   // onUpdate 约 16ms/次 → 60 次约 1s
        GetHAL()->updatePowerMonitorData();
        float v = GetHAL()->powerMonitorData.busVoltage;
        if (_batt_text) {
            char b[64];
            if (v > 0.5f) {
                int pct = (int)((v - 6.0f) / (8.4f - 6.0f) * 100.0f);
                if (pct < 0) { pct = 0; }
                if (pct > 100) { pct = 100; }
                snprintf(b, sizeof(b), "BAT %d%%  %.1fV", pct, v);
                lv_obj_set_style_text_color(_batt_text, lv_color_hex(pct < 20 ? tb::danger() : tb::success()), 0);
            } else {
                snprintf(b, sizeof(b), "BAT --  (USB)");
                lv_obj_set_style_text_color(_batt_text, lv_color_hex(tb::textDim()), 0);
            }
            lv_label_set_text(_batt_text, b);
        }
        if (_wifi_text) {
            char b[96];
            if (GetHAL()->wifiIsStaConnected()) {
                std::string ip = GetHAL()->wifiGetStaIp();
                snprintf(b, sizeof(b), "WiFi: %s", ip.empty() ? "connected" : ip.c_str());
                lv_obj_set_style_text_color(_wifi_text, lv_color_hex(tb::success()), 0);
            } else {
                snprintf(b, sizeof(b), "WiFi: off");
                lv_obj_set_style_text_color(_wifi_text, lv_color_hex(tb::textDim()), 0);
            }
            lv_label_set_text(_wifi_text, b);
        }
    }

    if (_state != Opened) {
        return;
    }

    // 实体键盘（Tab5 官方键盘）：把读到的字符直接喂给最后聚焦的输入框。
    // 不走 LVGL 的 keypad indev 路由（v9 + esp_lvgl_port 组合下 read_cb 不触发）。
    {
        uint16_t kc = 0;
        int guard = 0;
        while ((kc = GetHAL()->keyboardReadKey()) && ++guard < 16) {
            bool ok = tool_kbd::feedKey(kc);
            if (!ok) {
                mclog::tagInfo(_tag, "kb key 0x{:04X} dropped (no focused input)", kc);
            }
        }
    }

    ++_tick;

    if (_tool_window) {
        _tool_window->update();
        if (_tool_window->getState() == ui::Window::State_t::Closed) {
            _tool_window.reset();
            _opened_tool = -1;
            mclog::tagInfo(_tag, "tool closed, back to shell");
            style_nav_active(0);
            // 重建 HOME 卡片：延后到渲染安全时机
            if (!s_home_rebuild_pending) {
                s_home_rebuild_pending = true;
                lv_async_call(async_rebuild_home, this);
            }
        }
    }
}

void ToolboxHome::rebuildHome()
{
    if (!_content) return;
    lv_obj_clean(_content);
    build_home_cards(_content);
}

void ToolboxHome::setStatusText(const char* text)
{
    if (_status_text) {
        lv_label_set_text(_status_text, text);
    }
}

void ToolboxHome::onClose()
{
    mclog::tagInfo(_tag, "shell close");
    s_instance = nullptr;
    if (_tool_window) {
        _tool_window->close(true, true);  // 触发工具 onClose：停任务/关 socket
        vTaskDelay(pdMS_TO_TICKS(300));  // 等后台任务退出（recv 超时 100ms + 收尾）
        _tool_window.reset();
    }
    _opened_tool = -1;
    _content     = nullptr;
    _status_text = nullptr;
    for (auto& b : s_nav_btns) {
        b = nullptr;
    }
}

}  // namespace launcher_view
