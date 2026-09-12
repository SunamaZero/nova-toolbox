/*
 * Nova Toolbox — 工具页两列骨架（所有工具页共用）
 *
 * 为什么要有这个文件：
 *   之前 5 个工具页各写一套坐标 —— 串口页按钮间距 185、UDP 是 225、日志页又不一样，
 *   摆在一起就不像一套产品。这里把串口页定稿的几何**固化成唯一来源**，
 *   新页面只写业务逻辑，布局自动同构。
 *
 * 几何（与 tool_serial.cpp 完全一致）：
 *   左列 x 16..856   报文区 y 16..542 ；发送行 y 554..606（输入框 + 发送按钮）
 *   右列 x 868..1148 状态 y 16..52 ；设置行从 y 62 起，行高 56、行距 68
 *                    主操作按钮 y 470..542 ；底部 清空/关闭 y 554..606
 */
#pragma once

#include "toolbox_theme.h"
#include "tool_kbd.h"
#include <lvgl.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <memory>

namespace tl {

using smooth_ui_toolkit::lvgl_cpp::Button;
using smooth_ui_toolkit::lvgl_cpp::Container;
using smooth_ui_toolkit::lvgl_cpp::Label;
using smooth_ui_toolkit::lvgl_cpp::TextArea;

// ==================== 几何常量（唯一来源）====================
constexpr int W       = 1164;                     // 工具窗口宽（= 父内容区宽，绝不能更宽）
constexpr int PageH   = 622;
constexpr int Margin  = 16;                       // 四周边距
constexpr int Gap     = 12;

constexpr int LeftX   = Margin;                   // 16
constexpr int RightW  = 280;
constexpr int RightX  = W - Margin - RightW;      // 868
constexpr int LeftW   = RightX - Margin - Gap;    // 840

constexpr int MsgY    = Margin;                   // 16
constexpr int MsgH    = 542 - Margin;             // 526
constexpr int SendY   = 554;
constexpr int SendH   = 52;
constexpr int SendBtnW = 164;
constexpr int SendBtnX = LeftX + LeftW - SendBtnW;   // 704

constexpr int RowY0    = 62;
constexpr int RowH     = 56;
constexpr int RowPitch = 68;
inline int rowY(int i) { return RowY0 + RowPitch * i; }

constexpr int StatusY = Margin;
constexpr int StatusH = 36;
constexpr int StatusDot = 14;
constexpr int StatusDotZone = tb::SpaceMd + StatusDot + tb::SpaceSm;

constexpr int OpenY   = 470;                      // 主操作按钮（开始/停止之类）
constexpr int OpenH   = 72;
constexpr int ActY    = 554;                      // 底部两按钮
constexpr int ActH    = 52;
constexpr int ActBtnW = (RightW - Gap) / 2;       // 134
inline int actX(int i) { return RightX + i * (ActBtnW + Gap); }

// 没有发送行的页面（如 HTTP 服务端）：报文区直接延伸到底部按钮上方
constexpr int MsgHNoTx = 606 - Margin;            // 590

// ==================== 控件构造（统一写法，避免各页各造）====================

// 只读显示区（报文/日志）：可滚动、点一下不出现光标
inline std::unique_ptr<TextArea> makePanel(lv_obj_t* parent, int h = MsgH)
{
    auto p = std::make_unique<TextArea>(parent);
    tb::makeReadOnly(p->get());
    p->setSize(LeftW, h);
    p->align(LV_ALIGN_TOP_LEFT, LeftX, MsgY);
    p->setMaxLength(8192);
    p->setCursorClickPos(false);
    p->setPasswordMode(false);
    p->setOneLine(false);
    lv_obj_set_style_pad_all(p->get(), tb::SpaceMd, 0);
    lv_obj_set_style_border_width(p->get(), 1, 0);
    lv_obj_set_style_border_color(p->get(), lv_color_hex(tb::border()), 0);
    lv_obj_set_style_bg_color(p->get(), lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_radius(p->get(), tb::RadiusMd, 0);
    p->setTextFont(tb::fontBody());
    p->setTextColor(lv_color_hex(tb::text()));
    p->setScrollbarMode(LV_SCROLLBAR_MODE_AUTO);
    return p;
}

// 单行输入框（已接屏幕软键盘）
inline std::unique_ptr<TextArea> makeInput(lv_obj_t* parent, int x, int y, int w, const char* placeholder)
{
    auto t = std::make_unique<TextArea>(parent);
    t->setSize(w, SendH);
    tb::styleInput(t->get());
    t->align(LV_ALIGN_TOP_LEFT, x, y);
    t->setOneLine(true);
    t->setSize(w, SendH);   // 【必须】setOneLine 会把高度改成内容自适应，之后要重新固定
    t->setBorderWidth(1);
    t->setBorderColor(lv_color_hex(tb::border()));
    t->setBgColor(lv_color_hex(tb::surface()));
    t->setTextFont(tb::fontBody());
    t->setTextColor(lv_color_hex(tb::text()));
    if (placeholder) {
        lv_textarea_set_placeholder_text(t->get(), placeholder);
    }
    tool_kbd::attach(t->get());
    return t;
}

// 设置行：整行按钮，左名称（暗）+ 右值（琥珀）
inline std::unique_ptr<Button> makeRow(lv_obj_t* parent, int y, const char* name)
{
    auto b = std::make_unique<Button>(parent);
    b->setSize(RightW, RowH);
    b->align(LV_ALIGN_TOP_LEFT, RightX, y);
    b->setBgColor(lv_color_hex(tb::raised()));
    b->setRadius(tb::RadiusSm);
    b->label().setTextFont(tb::fontBody());
    b->label().setTextColor(lv_color_hex(tb::accent()));
    b->label().align(LV_ALIGN_RIGHT_MID, -tb::SpaceMd, 0);
    lv_obj_t* n = lv_label_create(b->get());
    lv_label_set_text(n, name);
    lv_obj_set_style_text_font(n, tb::fontBody(), 0);
    lv_obj_set_style_text_color(n, lv_color_hex(tb::textDim()), 0);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, tb::SpaceMd, 0);
    return b;
}

// 带输入框的设置行：左名称 + 右侧可编辑输入框（值要手打时用这个，不要用 makeRow）
inline std::unique_ptr<TextArea> makeRowInput(lv_obj_t* parent, int y, const char* name, const char* init)
{
    // 行底（非按钮，避免"看着能按其实是输入"的误导）
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, RightW, RowH);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, RightX, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(tb::raised()), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, tb::RadiusSm, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    tb::makeReadOnly(box);

    lv_obj_t* n = lv_label_create(box);
    lv_label_set_text(n, name);
    lv_obj_set_style_text_font(n, tb::fontBody(), 0);
    lv_obj_set_style_text_color(n, lv_color_hex(tb::textDim()), 0);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, tb::SpaceMd, 0);

    auto t = std::make_unique<TextArea>(box);
    t->setSize(RightW - 96, 40);
    tb::styleInput(t->get());
    t->align(LV_ALIGN_RIGHT_MID, -tb::SpaceSm, 0);
    t->setOneLine(true);
    t->setSize(RightW - 96, 40);
    t->setBorderWidth(1);
    t->setBorderColor(lv_color_hex(tb::border()));
    t->setBgColor(lv_color_hex(tb::surface()));
    t->setTextFont(tb::fontBody());
    t->setTextColor(lv_color_hex(tb::text()));
    if (init) {
        t->setText(init);
    }
    tool_kbd::attach(t->get());
    return t;
}

// 纯展示行：左名称 + 右值（不可点 —— 不做"看着能按"的死按钮）
inline void makeInfoRow(lv_obj_t* parent, int y, const char* name, const char* value, lv_obj_t** out_value = nullptr)
{
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, RightW, RowH);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, RightX, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, tb::RadiusSm, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(tb::border()), 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    tb::makeReadOnly(box);

    lv_obj_t* n = lv_label_create(box);
    lv_label_set_text(n, name);
    lv_obj_set_style_text_font(n, tb::fontBody(), 0);
    lv_obj_set_style_text_color(n, lv_color_hex(tb::textDim()), 0);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, tb::SpaceMd, 0);

    lv_obj_t* v = lv_label_create(box);
    lv_label_set_text(v, value ? value : "--");
    lv_obj_set_style_text_font(v, tb::fontBody(), 0);
    lv_obj_set_style_text_color(v, lv_color_hex(tb::textDim()), 0);
    lv_obj_align(v, LV_ALIGN_RIGHT_MID, -tb::SpaceMd, 0);
    if (out_value) {
        *out_value = v;
    }
}

// 状态行：画出来的圆点 + 文本（圆点不依赖字体字形，绝不会变豆腐块）
inline std::unique_ptr<Label> makeStatus(lv_obj_t* parent, lv_obj_t** out_dot)
{
    auto l = std::make_unique<Label>(parent);
    lv_obj_set_size(l->get(), RightW - StatusDotZone, StatusH);
    l->align(LV_ALIGN_TOP_LEFT, RightX + StatusDotZone, StatusY);
    l->setTextFont(tb::fontBody());
    l->setTextColor(lv_color_hex(tb::text()));
    lv_label_set_long_mode(l->get(), LV_LABEL_LONG_DOT);
    l->setText("--");

    lv_obj_t* dot = lv_obj_create(parent);
    lv_obj_set_size(dot, StatusDot, StatusDot);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, RightX + tb::SpaceMd, StatusY + (StatusH - StatusDot) / 2);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(dot, 0, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(tb::textDim()), 0);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);
    tb::makeReadOnly(dot);
    if (out_dot) {
        *out_dot = dot;
    }
    return l;
}

// 主操作按钮（开始/停止之类），撑满右列宽度
inline std::unique_ptr<Button> makePrimary(lv_obj_t* parent, const char* text)
{
    auto b = std::make_unique<Button>(parent);
    b->setSize(RightW, OpenH);
    b->align(LV_ALIGN_TOP_LEFT, RightX, OpenY);
    b->setRadius(tb::RadiusMd);
    b->setBgColor(lv_color_hex(tb::raised()));
    b->setBorderWidth(1);
    b->setBorderColor(lv_color_hex(tb::accent()));
    b->label().setTextFont(tb::fontBody());
    b->label().setTextColor(lv_color_hex(tb::accent()));
    b->label().setText(text);
    return b;
}

// 底部操作按钮：idx 0=左（清空）1=右（关闭）
inline std::unique_ptr<Button> makeAction(lv_obj_t* parent, int idx, const char* text, uint32_t bg, uint32_t fg)
{
    auto b = std::make_unique<Button>(parent);
    b->setSize(ActBtnW, ActH);
    b->align(LV_ALIGN_TOP_LEFT, actX(idx), ActY);
    b->setRadius(tb::RadiusSm);
    b->setBgColor(lv_color_hex(bg));
    b->label().setTextFont(tb::fontBody());
    b->label().setTextColor(lv_color_hex(fg));
    b->label().setText(text);
    return b;
}

// 发送行：左输入框 + 右发送按钮（复用串口页的比例：输入框 = 左列宽 - 按钮宽 - 间距）
struct SendRow {
    std::unique_ptr<TextArea> input;
    std::unique_ptr<Button>   button;
};

inline SendRow makeSendRow(lv_obj_t* parent, const char* placeholder)
{
    SendRow r;
    r.input = makeInput(parent, LeftX, SendY, LeftW - SendBtnW - Gap, placeholder);
    r.button = std::make_unique<Button>(parent);
    r.button->setSize(SendBtnW, SendH);
    r.button->align(LV_ALIGN_TOP_LEFT, SendBtnX, SendY);
    r.button->setRadius(tb::RadiusMd);
    r.button->setBgColor(lv_color_hex(tb::raised()));
    r.button->setBorderWidth(1);
    r.button->setBorderColor(lv_color_hex(tb::accent()));
    r.button->label().setTextFont(tb::fontBody());
    r.button->label().setTextColor(lv_color_hex(tb::accent()));
    r.button->label().setText("发送");
    return r;
}

}  // namespace tl
