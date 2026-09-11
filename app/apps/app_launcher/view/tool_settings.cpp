// Tab5 工具箱 — 设置页实现（WiFi 连接 + OTA 升级）
//
// UI 规范（触摸优先，内容区 1180×622）：
//   卡片分组：WiFi 卡 / OTA 卡，每张卡内 "标题 + 行 + 操作"
//   输入框高 56、按钮高 56（>= 48 触摸下限）；按钮宽 >= 160
//   行间距 16、卡内边距 20、卡间距 20（4pt 网格）
//   状态用颜色区分（成功/警告/危险/次要），不靠字号层次（只有 16px 中文字库）

#include "tool_settings.h"

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include <cstdio>
#include <cstring>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "ota_client.h"
#include "tool_kbd.h"
#include "tool_screenshot.h"

using namespace smooth_ui_toolkit::lvgl_cpp;

namespace launcher_view {

static const std::string _tag = "tool-settings";

namespace {
const char* kDefaultOtaUrl = "http://192.168.31.214:8093/";
constexpr int W = 1180;              // 内容区宽
constexpr int H = 622;               // 内容区高
constexpr int CardPad = 20;          // 卡内边距
// 触摸尺寸按 Tab5 实际 PPI 算：5" 1280x720 → 294 PPI → 1mm≈11.6px
// 手指舒适区 7-9mm = 81-104px，取 96px（≈8.3mm）；48px 只有 4.1mm，太小
constexpr int CtrlH = 96;            // 输入框/按钮高
constexpr int LabelW = 88;           // 行内标签宽

// OTA 后台状态（onUpdate 轮询）
volatile int  g_ota_state    = 0;
volatile int  g_ota_progress = -1;
char          g_ota_msg[128] = {0};
char          g_ota_url[128] = {0};

void task_check(void*) {
    if (!GetHAL()->wifiIsStaConnected()) {
        snprintf(g_ota_msg, sizeof(g_ota_msg), "未连 WiFi — 请先在上方连接");
        g_ota_state = 2; g_ota_progress = -2; vTaskDelete(nullptr); return;
    }
    std::string v;
    if (ota_client::check(std::string(g_ota_url), v) == ESP_OK) {
        if (v == ota_client::currentVersion())
            snprintf(g_ota_msg, sizeof(g_ota_msg), "已是最新版本 %s", v.c_str());
        else
            snprintf(g_ota_msg, sizeof(g_ota_msg), "发现新版本 %s", v.c_str());
        g_ota_state = 1;
    } else {
        snprintf(g_ota_msg, sizeof(g_ota_msg), "检查失败 — 服务器不可达？");
        g_ota_state = 2;
    }
    g_ota_progress = -2;
    vTaskDelete(nullptr);
}

void task_upgrade(void*) {
    if (!GetHAL()->wifiIsStaConnected()) {
        snprintf(g_ota_msg, sizeof(g_ota_msg), "未连 WiFi — 请先在上方连接");
        g_ota_state = 2; g_ota_progress = -2; vTaskDelete(nullptr); return;
    }
    snprintf(g_ota_msg, sizeof(g_ota_msg), "下载中…");
    esp_err_t e = ota_client::upgrade(std::string(g_ota_url), [](int p) { g_ota_progress = p; });
    if (e == ESP_OK) {
        snprintf(g_ota_msg, sizeof(g_ota_msg), "升级成功 — 重启中");
        g_ota_progress = 100; g_ota_state = 1;
        vTaskDelay(pdMS_TO_TICKS(900));
        esp_restart();
    } else {
        snprintf(g_ota_msg, sizeof(g_ota_msg), "升级失败: %s", esp_err_to_name(e));
        g_ota_state = 2; g_ota_progress = -2;
    }
    vTaskDelete(nullptr);
}

// ---------- 卡片（带标题的分组容器）----------
lv_obj_t* makeCard(lv_obj_t* parent, int x, int y, int w, int h, const char* title) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, w, h);
    lv_obj_align(card, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, tb::RadiusLg, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(tb::border()), 0);
    lv_obj_set_style_pad_all(card, CardPad, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, tb::fontBody(), 0);
    lv_obj_set_style_text_color(t, lv_color_hex(tb::accent()), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);
    return card;
}

// ---------- 行内标签 ----------
void makeLabel(lv_obj_t* parent, int x, int y, const char* text) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, tb::fontBody(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(tb::textDim()), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, x, y + CtrlH / 2 - 10);
}

// ---------- 输入框（56 高，触摸友好）----------
std::unique_ptr<TextArea> makeInput(lv_obj_t* parent, int x, int y, int w, const char* text, const char* hint) {
    auto ta = std::make_unique<TextArea>(parent);
    ta->align(LV_ALIGN_TOP_LEFT, x, y);
    ta->setSize(w, CtrlH);
    ta->setOneLine(true);
    ta->setTextFont(tb::fontBody());
    if (text) ta->setText(text);
    if (hint) ta->setPlaceholderText(hint);
    tb::styleInput(ta->get());
    lv_obj_set_style_pad_left(ta->get(), tb::SpaceLg, 0);
    lv_obj_set_style_border_width(ta->get(), 2, 0);        // 描边加粗，框更显眼
    lv_obj_set_style_border_color(ta->get(), lv_color_hex(tb::border()), 0);
    // 单行文本在 72px 高框里垂直居中（否则贴着顶边，看着像"高度没设计"）
    lv_obj_set_style_pad_top(ta->get(), (CtrlH - 26) / 2, 0);
    lv_obj_set_style_pad_bottom(ta->get(), 0, 0);
    tool_kbd::attach(ta->get());   // 点击聚焦 + 弹软键盘（缺这行就是"点了没反应"）
    return ta;   // 点击聚焦 + 弹软键盘（缺这行就是"点了没反应"）
    return ta;
}

// ---------- 按钮（56 高）----------
std::unique_ptr<Button> makeBtn(lv_obj_t* parent, int x, int y, int w, const char* text, bool primary) {
    auto b = std::make_unique<Button>(parent);
    b->align(LV_ALIGN_TOP_LEFT, x, y);
    b->setSize(w, CtrlH);
    b->setTextFont(tb::fontBody());
    if (primary) tb::stylePrimary(b->get()); else tb::styleSecondary(b->get());
    lv_obj_t* l = lv_label_create(b->get());
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, tb::fontBody(), 0);
    lv_obj_center(l);
    return b;
}
}  // namespace

SettingsToolWindow::SettingsToolWindow() {
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, W, H, 255};
    config.bgColor  = tb::bg();
}

void SettingsToolWindow::onOpen() {
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    mclog::tagInfo(_tag, "on open");
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);

    // ---- 标题带 ----
    _title_label = std::make_unique<Label>(_window->get());
    _title_label->align(LV_ALIGN_TOP_LEFT, tb::PagePad, 10);
    _title_label->setText("设置");
    _title_label->setTextFont(tb::fontTitle());
    _title_label->setTextColor(lv_color_hex(tb::accent()));

    const int card_x = tb::PagePad;
    const int card_w = W - tb::PagePad * 2;
    const int wifi_y = 56;                          // 标题带 56
    const int wifi_h = CardPad * 2 + 24 + CtrlH * 2 + tb::SpaceSm;   // 标题 + 两行输入
    const int ota_y  = wifi_y + wifi_h + tb::SpaceSm;
    const int ota_h  = H - ota_y - tb::PagePad;   // 余下全给 OTA 卡

    // 先画卡片（背景层），再画控件（上层）—— LVGL 后创建的在上面
    makeCard(_window->get(), card_x, wifi_y, card_w, wifi_h, "WiFi 连接（连路由器）");
    makeCard(_window->get(), card_x, ota_y, card_w, ota_h, "OTA 固件升级");
    buildWifiSection(_window->get(), wifi_y);
    buildOtaSection(_window->get(), ota_y);

    snprintf(g_ota_url, sizeof(g_ota_url), "%s", kDefaultOtaUrl);
}

// ==================== WiFi 区 ====================
void SettingsToolWindow::buildWifiSection(lv_obj_t* parent, int card_y) {
    const int x0 = tb::PagePad + CardPad;
    const int w  = W - tb::PagePad * 2 - CardPad * 2;

    int row1 = card_y + CardPad + 24 + tb::SpaceSm;      // 标题下第一行
    int row2 = row1 + CtrlH + tb::SpaceSm;

    // 账号
    makeLabel(parent, x0, row1, "账号");
    _ssid_ta = makeInput(parent, x0 + LabelW, row1, w - LabelW - 300 - tb::SpaceMd, nullptr, "WiFi 名称");
    // Connect 按钮（同行右侧）
    _btn_connect = makeBtn(parent, x0 + w - 280, row1, 280, "Connect", true);
    _btn_connect->onClick().connect([&]() {
        ESP_LOGI("tool-settings", ">>> Connect clicked");
        const char* ssid = lv_textarea_get_text(_ssid_ta->get());
        const char* pass = lv_textarea_get_text(_pass_ta->get());
        ESP_LOGI("tool-settings", ">>> ssid='%s' pass_len=%d", ssid ? ssid : "(null)", pass ? (int)strlen(pass) : -1);
        if (!ssid || !*ssid) {
            _wifi_state->setText("请先填写 WiFi 名称");
            _wifi_state->setTextColor(lv_color_hex(tb::danger()));
            return;
        }
        _wifi_state->setText("连接中…");
        _wifi_state->setTextColor(lv_color_hex(tb::warning()));
        ESP_LOGI("tool-settings", ">>> calling wifiConnectSta...");
        bool ok = GetHAL()->wifiConnectSta(ssid, pass);
        ESP_LOGI("tool-settings", ">>> wifiConnectSta returned %d", (int)ok);
    });

    // 密码
    makeLabel(parent, x0, row2, "密码");
    _pass_ta = makeInput(parent, x0 + LabelW, row2, w - LabelW - 300 - tb::SpaceMd, nullptr, "WiFi 密码");
    lv_textarea_set_password_mode(_pass_ta->get(), true);

    _wifi_state = std::make_unique<Label>(parent);
    _wifi_state->align(LV_ALIGN_TOP_LEFT, x0 + w - 280, row2 + CtrlH / 2 - 10);
    _wifi_state->setText("未连接");
    _wifi_state->setTextFont(tb::fontBody());
    _wifi_state->setTextColor(lv_color_hex(tb::textDim()));
}

// ==================== OTA 区 ====================
void SettingsToolWindow::buildOtaSection(lv_obj_t* parent, int card_y) {
    const int x0 = tb::PagePad + CardPad;
    const int w  = W - tb::PagePad * 2 - CardPad * 2;

    int row1 = card_y + CardPad + 24 + tb::SpaceSm;
    int row2 = row1 + CtrlH + tb::SpaceSm;
    int row3 = row2 + CtrlH + tb::SpaceSm;

    // 服务器地址
    makeLabel(parent, x0, row1, "服务器");
    _url_ta = makeInput(parent, x0 + LabelW, row1, w - LabelW, kDefaultOtaUrl, nullptr);
    lv_obj_add_event_cb(_url_ta->get(), [](lv_event_t* e) {
        const char* t = lv_textarea_get_text((lv_obj_t*)lv_event_get_target(e));
        if (t && *t) snprintf(g_ota_url, sizeof(g_ota_url), "%s", t);
    }, LV_EVENT_VALUE_CHANGED, nullptr);

    // Check Update / Upgrade
    _btn_check = makeBtn(parent, x0 + w - 560, row2, 260, "Check Update", true);
    _btn_check->onClick().connect([&]() {
        if (g_ota_state == 3) return;
        g_ota_state = 3; g_ota_progress = -1;
        snprintf(g_ota_msg, sizeof(g_ota_msg), "检查中…");
        xTaskCreate(task_check, "ota_check", 8192, nullptr, 5, nullptr);
    });
    _btn_upgrade = makeBtn(parent, x0 + w - 280, row2, 260, "Upgrade", true);
    _btn_upgrade->onClick().connect([&]() {
        if (g_ota_state == 3) return;
        g_ota_state = 3; g_ota_progress = 0;
        snprintf(g_ota_msg, sizeof(g_ota_msg), "准备中…");
        xTaskCreate(task_upgrade, "ota_upgrade", 12288, nullptr, 5, nullptr);
    });

    // 当前版本
    _ota_state = std::make_unique<Label>(parent);
    _ota_state->align(LV_ALIGN_TOP_LEFT, x0, row2 + CtrlH / 2 - 10);
    {
        char b[128]; snprintf(b, sizeof(b), "当前版本 %s", ota_client::currentVersion().c_str());
        _ota_state->setText(b);
    }
    _ota_state->setTextFont(tb::fontBody());
    _ota_state->setTextColor(lv_color_hex(tb::textDim()));

    // 进度条
    _bar = lv_bar_create(parent);
    lv_obj_set_size(_bar, w, 12);
    lv_obj_align(_bar, LV_ALIGN_TOP_LEFT, x0, row3);
    lv_bar_set_range(_bar, 0, 100);
    lv_bar_set_value(_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(_bar, lv_color_hex(tb::bg()), LV_PART_MAIN);
    lv_obj_set_style_bg_color(_bar, lv_color_hex(tb::accent()), LV_PART_INDICATOR);
    lv_obj_set_style_radius(_bar, tb::RadiusSm, LV_PART_MAIN);
    lv_obj_set_style_radius(_bar, tb::RadiusSm, LV_PART_INDICATOR);
}

// ==================== 周期刷新 ====================
void SettingsToolWindow::onUpdate() {
    if (++_tick % 10 == 0) {
        bool conn = GetHAL()->wifiIsStaConnected();
        std::string ip = GetHAL()->wifiGetStaIp();
        if (conn && !ip.empty()) {
            _wifi_state->setText(("已连接 " + ip).c_str());
            _wifi_state->setTextColor(lv_color_hex(tb::success()));
        } else if (!conn) {
            _wifi_state->setText("未连接");
            _wifi_state->setTextColor(lv_color_hex(tb::textDim()));
        }
    }
    char b[160];
    if (g_ota_progress >= 0 && g_ota_progress < 100)
        snprintf(b, sizeof(b), "%s %d%%", g_ota_msg, g_ota_progress);
    else if (g_ota_msg[0])
        snprintf(b, sizeof(b), "%s", g_ota_msg);
    else
        return;
    _ota_state->setText(b);
    _ota_state->setTextColor(lv_color_hex(g_ota_state == 2 ? tb::danger()
                                       : g_ota_state == 1 ? tb::success() : tb::textDim()));
    if (g_ota_progress >= 0) lv_bar_set_value(_bar, g_ota_progress, LV_ANIM_OFF);
}

void SettingsToolWindow::onClose() {
    mclog::tagInfo(_tag, "on close");
}

}  // namespace launcher_view

#endif  // CONFIG_IDF_TARGET_ESP32P4
