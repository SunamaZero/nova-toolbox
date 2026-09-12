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
#include <string>

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
// 高行：标签一行 + 输入框一行（输入框能吃到整行宽度，长值不用挤）
constexpr int RowHTall = 88;
constexpr int RowPitch = 68;
inline int rowY(int i) { return RowY0 + RowPitch * i; }
constexpr int RowGapY = 12;                            // 行与行之间的间距
// 依次排布：上一行的 y + 该行高度 + 间距（行高不一致时用它算下一行）
inline int nextY(int y, int h) { return y + h + RowGapY; }

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

// ---------------------------------------------------------------------------
// 让输入框里的文字垂直居中。
//
// LVGL 的 textarea 文字默认贴顶，只能靠 pad_top 调；而固定值必然顾此失彼：
//   - 按单行算 → 多行内容会被挤出框外
//   - 按双行算 → 单行内容贴在顶上（之前的 bug：文字高了 10px）
// 所以按"内容实际高度"动态算：单行居中、多行上下均分。
// ---------------------------------------------------------------------------
inline void applyVerticalCenter(lv_obj_t* ta)
{
    if (ta == nullptr) {
        return;
    }
    const lv_font_t* f      = lv_obj_get_style_text_font(ta, LV_PART_MAIN);
    const int        line_h = f ? lv_font_get_line_height(f) : 20;

    // 【坑】不要读内部 label 的高度：此刻布局可能还没算出来，拿到的是旧值，
    // 于是 pad 算成 0、文字贴顶（和之前 lv_obj_get_height 返回默认值是同一类坑）。
    // 直接用 lv_txt_get_size 按"文本 + 可用宽度"算换行后的真实高度，不依赖时序。
    const char* txt     = lv_textarea_get_text(ta);
    const int   inner_w = lv_obj_get_content_width(ta);
    int         content = line_h;
    if (txt != nullptr && txt[0] != 0 && inner_w > 8) {
        lv_point_t sz = {0, 0};
        lv_txt_get_size(&sz, txt, f, 0, 0, inner_w, LV_TEXT_FLAG_NONE);
        if (sz.y > content) {
            content = sz.y;
        }
    }
    const int box_h = lv_obj_get_height(ta);
    int       pad   = (box_h - content) / 2;
    if (pad < 0) {
        pad = 0;
    }
    lv_obj_set_style_pad_top(ta, pad, 0);
    lv_obj_set_style_pad_bottom(ta, pad, 0);
}

inline void bindVerticalCenter(lv_obj_t* ta)
{
    lv_obj_add_event_cb(
        ta,
        [](lv_event_t* e) { applyVerticalCenter(static_cast<lv_obj_t*>(lv_event_get_target(e))); },
        LV_EVENT_VALUE_CHANGED, nullptr);
    applyVerticalCenter(ta);   // 初始也要算一次
}


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
    t->setOneLine(false);   // 长内容换行显示，不截断
    t->setSize(w, SendH);
    bindVerticalCenter(t->get());   // 文字垂直居中（按内容动态算）
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

// 【推荐】标签与输入框各占一行的高行。
// 右列只有 280px：标签和输入框挤同一行时，输入框实宽只剩 184px，
// 而 `255.255.255.255` 要 ~190px、`broker.emqx.io:1883` 更长 → 必然被切。
// 上下两行之后输入框吃满 248px，长值单行就放得下，更长的自动换行。
inline std::unique_ptr<TextArea> makeRowInputTall(lv_obj_t* parent, int y, const char* name, const char* init)
{
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, RightW, RowHTall);
    lv_obj_align(box, LV_ALIGN_TOP_LEFT, RightX, y);
    lv_obj_set_style_bg_color(box, lv_color_hex(tb::raised()), 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, tb::RadiusSm, 0);
    lv_obj_set_style_border_width(box, 0, 0);
    lv_obj_set_style_pad_all(box, 0, 0);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    tb::makeReadOnly(box);

    // 标签占第一行
    lv_obj_t* n = lv_label_create(box);
    lv_label_set_text(n, name);
    lv_obj_set_style_text_font(n, tb::fontBody(), 0);
    lv_obj_set_style_text_color(n, lv_color_hex(tb::textDim()), 0);
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, tb::SpaceMd, tb::SpaceSm);

    // 输入框占第二行，吃满宽度
    auto t = std::make_unique<TextArea>(box);
    t->setSize(RightW - tb::SpaceMd * 2, 48);
    t->align(LV_ALIGN_BOTTOM_LEFT, tb::SpaceMd, -tb::SpaceSm);
    tb::styleInput(t->get());
    t->setOneLine(false);          // 自动换行
    t->setSize(RightW - tb::SpaceMd * 2, 48);
    t->setTextFont(tb::fontBody());
    t->setTextColor(lv_color_hex(tb::text()));
    if (init) {
        t->setText(init);
    }
    bindVerticalCenter(t->get());  // 单行居中 / 多行上下均分（放在 setText 之后）
    tool_kbd::attach(t->get());
    return t;
}

// 【推荐】标签与值各占一行的高信息行（值很长时用，避免和标签重叠）
inline void makeInfoRowTall(lv_obj_t* parent, int y, const char* name, const char* value, lv_obj_t** out_value = nullptr)
{
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_set_size(box, RightW, RowHTall);
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
    lv_obj_align(n, LV_ALIGN_TOP_LEFT, tb::SpaceMd, tb::SpaceSm);

    lv_obj_t* v = lv_label_create(box);
    lv_label_set_text(v, value ? value : "--");
    lv_obj_set_style_text_font(v, tb::fontBody(), 0);
    lv_obj_set_style_text_color(v, lv_color_hex(tb::text()), 0);
    lv_obj_set_width(v, RightW - tb::SpaceMd * 2);
    lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);   // 长值换行，不跟标签挤
    lv_obj_align(v, LV_ALIGN_TOP_LEFT, tb::SpaceMd, tb::SpaceSm + 24);
    if (out_value) {
        *out_value = v;
    }
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
    // 双行显示：右列只有 184px，`255.255.255.255` 这种值单行放不下（需要 ~190px），
    // 单行会被截断，两行就能完整看到。
    t->setSize(RightW - 96, 44);
    tb::styleInput(t->get());
    t->align(LV_ALIGN_RIGHT_MID, -tb::SpaceSm, 0);
    t->setOneLine(false);
    t->setSize(RightW - 96, 44);
    bindVerticalCenter(t->get());
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
    // 高度就取"一行字高"，然后整块垂直对齐到圆点中心线 ——
    // 这样文字中心和圆点中心精确重合。之前用魔法 pad_top(9) 凑，
    // 结果不同内容（带 ·、带中文数字）偏了 5px，肉眼看得出来。
    const lv_font_t* f = tb::fontBody();
    const int        line_h = lv_font_get_line_height(f);
    auto l = std::make_unique<Label>(parent);
    lv_obj_set_size(l->get(), RightW - StatusDotZone, line_h);
    l->align(LV_ALIGN_TOP_LEFT, RightX + StatusDotZone, StatusY + (StatusH - line_h) / 2);
    l->setTextFont(f);
    l->setTextColor(lv_color_hex(tb::text()));
    lv_label_set_long_mode(l->get(), LV_LABEL_LONG_DOT);
    // 状态文字不能有自己的底色：否则文字后面会凭空多出一块深色矩形
    lv_obj_set_style_bg_opa(l->get(), LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(l->get(), 0, 0);
    lv_obj_set_style_pad_right(l->get(), tb::SpaceSm, 0);
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
// 从输入框取值，顺带去掉首尾空白与换行。
// 输入框改成双行后，用户可能误按回车 → 值里混进 '\n'，直接拿去连服务器会失败。
inline std::string textOf(TextArea* t)
{
    if (!t) {
        return std::string();
    }
    const char* raw = lv_textarea_get_text(t->get());
    std::string v   = raw ? raw : "";
    std::string out;
    out.reserve(v.size());
    for (char c : v) {
        if (c != '\n' && c != '\r') {
            out += c;
        }
    }
    const size_t b = out.find_first_not_of(" \t");
    const size_t e = out.find_last_not_of(" \t");
    return (b == std::string::npos) ? std::string() : out.substr(b, e - b + 1);
}

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
