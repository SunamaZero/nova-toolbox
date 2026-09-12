/*
 * Nova Toolbox for M5Stack Tab5
 * 日志页
 *
 * 布局（与串口页同构 —— 所有工具页一套骨架）：
 *   左列 —— 日志区（滚动容器 + 逐行 label）
 *   右列 —— 状态 / 等级 / 自动滚动 / 缓冲 / 远程
 *           + 跳到最新 + 清空·关闭
 *
 * 为什么不用 TextArea 装日志：
 *   1. TextArea 只能一种文字颜色，日志要按等级上色（E 红 / W 黄 / I 奶白）
 *   2. 它每次 set_text 都要重排整段文本，200 行刷一次很费力
 * 改成「容器 + 每行一个 label」后：增量追加（只画新增的行）、能上色、滚动天然可用。
 */
#include "tool_log.h"

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include <string>
#include <vector>
#include "tool_logsys.h"
#include "toolbox_layout.h"   // tl::MsgHNoTx 等骨架常量
#include <apps/utils/audio/audio.h>

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-log";

namespace {

// ---------------- 布局（照抄串口页的骨架常量，保证五页同构）----------------
constexpr int ToolW    = 1164;
constexpr int Margin   = 16;
constexpr int Gap      = 12;
constexpr int LeftX    = Margin;
constexpr int RightW   = 280;
constexpr int RightX   = ToolW - Margin - RightW;   // 868
constexpr int LeftW    = RightX - Margin - Gap;     // 840
constexpr int MsgY     = Margin;
// 日志页没有"发送行"，面板直接填到底部 —— 否则 526..606 会留一条空洞
constexpr int MsgH     = tl::MsgHNoTx;              // 590（606 - 16）
constexpr int RowH     = 56;
constexpr int RowPitch = 68;
constexpr int RowY0    = 62;
constexpr int StatusY  = Margin;
constexpr int StatusH  = 36;
constexpr int StatusDot = 14;
constexpr int StatusDotZone = tb::SpaceMd + StatusDot + tb::SpaceSm;
constexpr int OpenY    = 470;
constexpr int OpenH    = 72;
constexpr int ActY     = 554;
constexpr int ActH     = 52;
constexpr int ActBtnW  = (RightW - Gap) / 2;        // 134
constexpr int InfoY    = RowY0 + RowPitch * 3;      // 266：远程（纯展示行）

constexpr int MaxLines = 120;   // 面板最多留多少行 label（滚上去也够看）

const char* _filter_names[4] = {"全部", "信息", "警告", "错误"};

// 日志行的等级色。
// 【坑】不能取 s[0]：本页每行前面加了 "[HH:MM:SS] " 时间戳，首字符永远是 '['，
// 那样所有行都会落到 default 变成 textMute(灰)——整个日志区看着就是一片灰。
// 正确做法：在整行里找 IDF 的 "<空格><等级><空格>(" 模式（I (2211) TAG: ...）。
char level_of(const std::string& s)
{
    for (size_t i = 1; i + 2 < s.size(); ++i) {
        if (s[i - 1] == ' ' && s[i + 1] == ' ' && s[i + 2] == '(') {
            switch (s[i]) {
            case 'E': case 'W': case 'I': case 'D': case 'V':
                return s[i];
            default:
                break;
            }
        }
    }
    return 0;   // 认不出
}

uint32_t level_color(const std::string& s)
{
    switch (level_of(s)) {
    case 'E': return tb::danger();
    case 'W': return tb::warning();
    case 'I': return tb::text();
    default:  return tb::text();   // D/V 与认不出的都用正文色兜底，绝不让整屏变灰
    }
}

void row_name(lv_obj_t* btn, const char* text)
{
    lv_obj_t* n = lv_label_create(btn);
    lv_label_set_text(n, text);
    lv_obj_set_style_text_font(n, tb::fontBody(), 0);
    lv_obj_set_style_text_color(n, lv_color_hex(tb::textDim()), 0);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, tb::SpaceMd, 0);
}

}  // namespace

namespace launcher_view {

LogToolWindow::LogToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, ToolW, 622, 255};
    config.bgColor  = tb::bg();
}

void LogToolWindow::onOpen()
{
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    mclog::tagInfo(_tag, "on open");

    _rendered    = 0;
    _filter      = 0;
    _auto_scroll = true;

    // ==================== 左列：日志区 ====================
    // 注意：不要 clear_flag(CLICKABLE) —— 去掉它手指就没法上下滚动了
    _log_box = std::make_unique<Container>(_window->get());
    _log_box->setSize(LeftW, MsgH);
    _log_box->align(LV_ALIGN_TOP_LEFT, LeftX, MsgY);
    // 底色/描边与各工具页的报文面板保持一致（surface + 1px border），
    // 之前用 sunken 近黑，跟其它页放一起像另一套设计
    lv_obj_set_style_bg_color(_log_box->get(), lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_border_width(_log_box->get(), 1, 0);
    lv_obj_set_style_border_color(_log_box->get(), lv_color_hex(tb::border()), 0);
    lv_obj_set_style_bg_opa(_log_box->get(), LV_OPA_COVER, 0);
    lv_obj_set_style_radius(_log_box->get(), tb::RadiusMd, 0);
    lv_obj_set_style_pad_all(_log_box->get(), tb::SpaceMd - 4, 0);
    lv_obj_set_style_pad_row(_log_box->get(), 2, 0);
    lv_obj_set_flex_flow(_log_box->get(), LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(_log_box->get(), LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_add_flag(_log_box->get(), LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(_log_box->get(), LV_DIR_VER);
    lv_obj_set_scrollbar_mode(_log_box->get(), LV_SCROLLBAR_MODE_AUTO);
    tb::makeReadOnly(_log_box->get());

    // ==================== 右列 ====================
    // 状态行：一律用统一的 tl::makeStatus ——
    // 以前这里手写了一份（高度 StatusH + 贴顶 + 没设文字色），于是 toolbox_layout.h
    // 里"一行字高 + 对齐圆点中心线 + 正文色"那套修复根本到不了这一页：
    // 设备上文字比圆点高约 10px、颜色还掉成 LVGL 默认的中性灰。
    // 教训：布局代码不许各页复制，几何与配色一律走 tl::。
    _status_label = tl::makeStatus(_window->get(), &_status_dot);
    lv_label_set_long_mode(_status_label->get(), LV_LABEL_LONG_DOT);
    _status_label->setText("0 条");

    // 设置行构造器（统一：整行按钮 + 左名 + 右值）
    auto make_row = [&](int y, const char* name) {
        auto b = std::make_unique<Button>(_window->get());
        b->setSize(RightW, RowH);
        b->align(LV_ALIGN_TOP_LEFT, RightX, y);
        b->setBgColor(lv_color_hex(tb::raised()));
        b->setRadius(tb::RadiusSm);
        b->label().setTextFont(tb::fontBody());
        b->label().setTextColor(lv_color_hex(tb::accent()));
        b->label().align(LV_ALIGN_RIGHT_MID, -tb::SpaceMd, 0);
        row_name(b->get(), name);
        return b;
    };

    _row_level = make_row(RowY0 + RowPitch * 0, "等级");
    _row_level->label().setText(_filter_names[0]);
    _row_level->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _filter = (_filter + 1) % 4;
        _row_level->label().setText(_filter_names[_filter]);
        rebuild();          // 过滤条件变了，整表重建
        refresh_status();
    });

    _row_auto = make_row(RowY0 + RowPitch * 1, "自动滚动");
    _row_auto->label().setText("开");
    _row_auto->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _auto_scroll = !_auto_scroll;
        _row_auto->label().setText(_auto_scroll ? "开" : "关");
        if (_auto_scroll) {
            lv_obj_scroll_to_y(_log_box->get(), LV_COORD_MAX, LV_ANIM_OFF);
        }
    });

    // 缓冲（纯展示行，不可点 —— 不做假的"看着能按"）
    {
        lv_obj_t* box = lv_obj_create(_window->get());
        lv_obj_set_size(box, RightW, RowH);
        lv_obj_align(box, LV_ALIGN_TOP_LEFT, RightX, RowY0 + RowPitch * 2);
        lv_obj_set_style_bg_color(box, lv_color_hex(tb::surface()), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(box, tb::RadiusSm, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(tb::border()), 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        tb::makeReadOnly(box);
        row_name(box, "缓冲");
        lv_obj_t* v = lv_label_create(box);
        lv_label_set_text_fmt(v, "%d/%d", logsys::count(), logsys::capacity());
        lv_obj_set_style_text_font(v, tb::fontBody(), 0);
        lv_obj_set_style_text_color(v, lv_color_hex(tb::textDim()), 0);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, -tb::SpaceMd, 0);
    }

    // 远程地址（纯展示行）
    {
        lv_obj_t* box = lv_obj_create(_window->get());
        lv_obj_set_size(box, RightW, RowH);
        lv_obj_align(box, LV_ALIGN_TOP_LEFT, RightX, InfoY);
        lv_obj_set_style_bg_color(box, lv_color_hex(tb::surface()), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(box, tb::RadiusSm, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lv_obj_set_style_border_color(box, lv_color_hex(tb::border()), 0);
        lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        tb::makeReadOnly(box);
        row_name(box, "远程");
        _info_ip = lv_label_create(box);
        lv_label_set_text(_info_ip, "--");
        lv_obj_set_style_text_font(_info_ip, tb::fontBody(), 0);
        lv_obj_set_style_text_color(_info_ip, lv_color_hex(tb::textDim()), 0);
        lv_obj_align(_info_ip, LV_ALIGN_RIGHT_MID, -tb::SpaceMd, 0);
    }

    // 跳到最新（主操作）
    _btn_latest = std::make_unique<Button>(_window->get());
    _btn_latest->setSize(RightW, OpenH);
    _btn_latest->align(LV_ALIGN_TOP_LEFT, RightX, OpenY);
    _btn_latest->setRadius(tb::RadiusMd);
    _btn_latest->setBgColor(lv_color_hex(tb::raised()));
    _btn_latest->setBorderWidth(1);
    _btn_latest->setBorderColor(lv_color_hex(tb::accent()));
    _btn_latest->label().setTextFont(tb::fontBody());
    _btn_latest->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_latest->label().setText("跳到最新");
    _btn_latest->onClick().connect([&]() {
        audio::play_next_tone_progression();
        lv_obj_scroll_to_y(_log_box->get(), LV_COORD_MAX, LV_ANIM_OFF);
    });

    // 清空 / 关闭
    _btn_clear = std::make_unique<Button>(_window->get());
    _btn_clear->setSize(ActBtnW, ActH);
    _btn_clear->align(LV_ALIGN_TOP_LEFT, RightX, ActY);
    _btn_clear->setBgColor(lv_color_hex(tb::neutral()));
    _btn_clear->setRadius(tb::RadiusSm);
    _btn_clear->label().setTextFont(tb::fontBody());
    _btn_clear->label().setTextColor(lv_color_hex(tb::text()));
    _btn_clear->label().setText("清空");
    _btn_clear->onClick().connect([&]() {
        audio::play_next_tone_progression();
        logsys::clear();
        rebuild();            // 直接重建（不能只调 onUpdate —— 它会被节流挡掉，看着像没生效）
        refresh_status();
    });

    _btn_close = std::make_unique<Button>(_window->get());
    _btn_close->setSize(ActBtnW, ActH);
    _btn_close->align(LV_ALIGN_TOP_LEFT, RightX + ActBtnW + Gap, ActY);
    _btn_close->setBgColor(lv_color_hex(tb::danger()));
    _btn_close->setRadius(tb::RadiusSm);
    _btn_close->label().setTextFont(tb::fontBody());
    _btn_close->label().setTextColor(lv_color_hex(tb::text()));
    _btn_close->label().setText("关闭");
    _btn_close->onClick().connect([&]() {
        audio::play_next_tone_progression();
        close();
    });

    rebuild();
    refresh_status();
}

// 当前过滤条件是否收下这一行
bool LogToolWindow::line_match(const std::string& s) const
{
    if (_filter == 0 || s.empty()) {
        return true;
    }
    // 同 level_of 的理由：不能在 s[0] 上判断等级（行首是时间戳的 '['）
    switch (_filter) {
    case 1: return level_of(s) == 'I';
    case 2: return level_of(s) == 'W';
    case 3: return level_of(s) == 'E';
    default: return true;
    }
}

void LogToolWindow::append_line(const std::string& s)
{
    lv_obj_t* l = lv_label_create(_log_box->get());
    lv_label_set_text(l, s.c_str());
    lv_obj_set_width(l, LeftW - (tb::SpaceMd - 4) * 2 - tb::SpaceMd);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);   // 长行折行，不截断
    lv_obj_set_style_text_font(l, tb::fontBody(), 0);
    lv_obj_set_style_text_color(l, lv_color_hex(level_color(s)), 0);
    lv_obj_set_style_pad_all(l, 0, 0);
}

void LogToolWindow::prune()
{
    lv_obj_t* box = _log_box->get();
    while (lv_obj_get_child_count(box) > MaxLines) {
        lv_obj_delete(lv_obj_get_child(box, 0));
    }
}

void LogToolWindow::rebuild()
{
    if (!_log_box) {
        return;
    }
    lv_obj_clean(_log_box->get());   // 清掉所有行
    _rendered = 0;

    const int total = logsys::count();
    int       n     = total > MaxLines ? MaxLines : total;
    if (n > 0) {
        std::vector<std::string> lines = logsys::tail_lines(n);
        for (const auto& l : lines) {
            if (line_match(l)) {
                append_line(l);
            }
        }
    }
    _rendered = total;
    lv_obj_scroll_to_y(_log_box->get(), LV_COORD_MAX, LV_ANIM_OFF);
}

void LogToolWindow::refresh_status()
{
    if (!_status_label) {
        return;
    }
    char b[64];
    const int total = logsys::count();
    if (GetHAL()->wifiIsStaConnected()) {
        snprintf(b, sizeof(b), "%d 条 · %s", total, GetHAL()->wifiGetStaIp().c_str());
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::success()), 0);
    } else {
        snprintf(b, sizeof(b), "%d 条 · 未联网", total);
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::textDim()), 0);
    }
    _status_label->setText(b);

    if (_info_ip) {
        lv_label_set_text(_info_ip, GetHAL()->wifiIsStaConnected() ? ":8090/log" : "--");
    }
}

void LogToolWindow::onUpdate()
{
    _tick++;
    if (!_log_box) {
        return;
    }
    if (_tick % 5 != 1) {
        return;   // ~0.5s 一次，够跟手又不至于每帧重排
    }

    const int total = logsys::count();

    // 被清空过（或重新开始记录）：整表重建
    if (total < _rendered) {
        rebuild();
        refresh_status();
        return;
    }
    if (total == _rendered) {
        refresh_status();
        return;
    }

    // 只在用户本来就贴着底部时才自动跟随；
    // 人家正往上翻日志就别硬拽回去（之前每 2 秒 scroll_to_y 到底，根本翻不动）
    const bool stick = _auto_scroll && (lv_obj_get_scroll_bottom(_log_box->get()) <= 8);

    int n = total - _rendered;
    if (n > MaxLines) {
        n = MaxLines;
    }
    std::vector<std::string> lines = logsys::tail_lines(n);
    for (const auto& l : lines) {
        if (line_match(l)) {
            append_line(l);
        }
    }
    _rendered = total;
    prune();
    if (stick) {
        lv_obj_scroll_to_y(_log_box->get(), LV_COORD_MAX, LV_ANIM_OFF);
    }
    refresh_status();
}

void LogToolWindow::onClose() {}

}  // namespace launcher_view
#endif
