/*
 * Nova Toolbox for M5Stack Tab5
 * 软键盘 helper 实现
 */
#include "tool_kbd.h"
extern "C" {
extern const lv_font_t tb_cn_16;
}
#include <hal/hal.h>
#include <mooncake_log.h>

namespace tool_kbd {

// 当前激活键盘与输入框
static lv_obj_t* s_kb  = nullptr;
static lv_obj_t* s_ime = nullptr;   // 拼音输入法（中文输入）
static lv_obj_t* s_ta  = nullptr;
static lv_obj_t* s_focus_ta = nullptr;   // 最后聚焦的输入框（实体键盘输入目标）

static const char* _tag = "tool-kbd";

static void kb_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    // READY = 完成键 / CANCEL = 取消键 → 关闭键盘
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_t* kb = (lv_obj_t*)lv_event_get_user_data(e);
        if (s_ime && lv_obj_is_valid(s_ime)) {
            lv_obj_delete(s_ime);
            s_ime = nullptr;
        }
        if (kb && lv_obj_is_valid(kb)) {
            lv_obj_delete(kb);
        }
        if (s_kb == kb) {
            s_kb = nullptr;
            s_ta = nullptr;
        }
    }
}

// 输入框被删（工具窗口关闭）时，必须同步删掉键盘，
// 否则键盘持有已释放 textarea 的指针，后续事件访问 → 崩溃
static void ta_delete_cb(lv_event_t* e)
{
    lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
    if (s_focus_ta == ta) {
        s_focus_ta = nullptr;
    }
    if (s_ta == ta) {
        if (s_ime && lv_obj_is_valid(s_ime)) {
            lv_obj_delete(s_ime);
            s_ime = nullptr;
        }
        if (s_kb && lv_obj_is_valid(s_kb)) {
            lv_obj_delete(s_kb);
        }
        s_kb = nullptr;
        s_ta = nullptr;
        mclog::tagInfo(_tag, "keyboard cleaned on textarea delete");
    }
}

static void ta_event_cb(lv_event_t* e)
{
    lv_event_code_t code = lv_event_get_code(e);
    // 点击/聚焦时把焦点交给该输入框（实体键盘输入的目标）
    if (code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
        if (lv_obj_is_valid(ta)) {
            s_focus_ta = ta;   // 记住：实体键盘的输入目标
        }
        lv_group_t* g = (lv_group_t*)GetHAL()->keyboardGetGroup();
        if (g && ta) {
            lv_group_focus_obj(ta);
        }
    }
    lv_obj_t* ta         = (lv_obj_t*)lv_event_get_user_data(e);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED) {
        if (!ta || ta == s_ta) {
            return;
        }
        // 已有键盘：切换目标；没有：创建
        if (s_kb && lv_obj_is_valid(s_kb)) {
            s_ta = ta;
            lv_keyboard_set_textarea(s_kb, ta);
            return;
        }
        // 键盘残留（已被删但指针未清）→ 清掉重建
        s_kb = nullptr;
        // 严格照官方例程 lv_example_ime_pinyin_1.c 的顺序：
        // ① 先建拼音输入法（设中文字体）→ ② 再建键盘 → ③ 绑定 → ④ 设模式 → ⑤ 摆候选词面板
        s_ime = lv_ime_pinyin_create(lv_screen_active());
        lv_obj_set_style_text_font(s_ime, &tb_cn_16, 0);

        lv_obj_t* kb = lv_keyboard_create(lv_screen_active());
        lv_keyboard_set_textarea(kb, ta);
        lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_READY, kb);
        lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_CANCEL, kb);

        // 输入框与键盘都用中文字体（候选词/汉字显示）
        lv_obj_set_style_text_font(ta, &tb_cn_16, 0);
        lv_obj_set_style_text_font(kb, &tb_cn_16, 0);

        lv_ime_pinyin_set_keyboard(s_ime, kb);
        lv_ime_pinyin_set_mode(s_ime, LV_IME_PINYIN_MODE_K26);

        lv_obj_t* cand = lv_ime_pinyin_get_cand_panel(s_ime);
        if (cand) {
            lv_obj_set_size(cand, lv_pct(100), lv_pct(10));   // 官方：100% x 10%
            lv_obj_align_to(cand, kb, LV_ALIGN_OUT_TOP_MID, 0, 0);
            lv_obj_set_style_text_font(cand, &tb_cn_16, 0);
        }

        s_kb = kb;
        s_ta = ta;
        mclog::tagInfo(_tag, "keyboard + pinyin IME shown (official order)");
    }
}

void attach(lv_obj_t* textarea)
{
    if (!textarea) {
        return;
    }
    // 点击/聚焦弹键盘
    lv_obj_add_flag(textarea, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_event_cb(textarea, ta_event_cb, LV_EVENT_CLICKED, textarea);
    // 输入框被删时清理键盘（防野指针崩溃）
    lv_obj_add_event_cb(textarea, ta_delete_cb, LV_EVENT_DELETE, nullptr);

    // 实体键盘（keypad indev）接入：把输入框加入 LVGL 默认 group，
    // 这样 Tab5 官方键盘的字符会自动送进聚焦的输入框（无需手动路由）
    lv_group_t* g = (lv_group_t*)GetHAL()->keyboardGetGroup();
    if (g && !lv_obj_get_group(textarea)) {
        lv_group_add_obj(g, textarea);
    }
    lv_obj_add_flag(textarea, LV_OBJ_FLAG_CLICK_FOCUSABLE);

}


bool feedKey(uint16_t k)
{
    // 实体键盘敲键 → 自动收起屏幕软键盘（有人用物理键盘时，软键盘白占半屏）
    // 注意要放在焦点判断之前：即使当前没有聚焦输入框，也应把软键盘收掉
    if (isVisible()) {
        hide();
    }

    if (!s_focus_ta || !lv_obj_is_valid(s_focus_ta)) {
        s_focus_ta = nullptr;
        return false;
    }

    // 普通 ASCII 字符
    if (k >= 0x20 && k < 0x7F) {
        lv_textarea_add_char(s_focus_ta, (uint32_t)k);
        return true;
    }

    // 特殊键 → 对应的 textarea 操作
    switch (k) {
    case KeyBackspace:
    case KeyDelete:
        lv_textarea_delete_char(s_focus_ta);
        return true;
    case KeyEnter:
        lv_textarea_add_char(s_focus_ta, '\n');
        return true;
    case KeySpace:
        lv_textarea_add_char(s_focus_ta, ' ');
        return true;
    case KeyTab: {
        // 在组内的输入框之间切换焦点
        lv_group_t* g = (lv_group_t*)GetHAL()->keyboardGetGroup();
        if (g) {
            lv_group_focus_next(g);
        }
        return true;
    }
    case KeyEsc:
        return true;
    default:
        return false;
    }
}

bool feedChar(uint8_t c)
{
    return feedKey((uint16_t)c);
}

bool hasFocus()
{
    return s_focus_ta && lv_obj_is_valid(s_focus_ta);
}

bool isVisible()
{
    return s_kb && lv_obj_is_valid(s_kb);
}

void hide()
{
    if (s_ime && lv_obj_is_valid(s_ime)) {
        lv_obj_delete(s_ime);
    }
    s_ime = nullptr;
    if (s_kb && lv_obj_is_valid(s_kb)) {
        lv_obj_delete(s_kb);
    }
    s_kb = nullptr;
    s_ta = nullptr;   // 焦点输入框保持（s_focus_ta 不动），下次点输入框会重新弹出
}

}  // namespace tool_kbd