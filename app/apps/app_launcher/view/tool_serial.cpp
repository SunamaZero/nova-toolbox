/*
 * Nova Toolbox for M5Stack Tab5
 * 串口调试（RS485 UART）
 *
 * 布局：
 *   左列 —— 报文区 + 发送输入框 + 发送按钮
 *   右列 —— 状态 / 端口 / 波特率 / 数据位 / 停止位 / 校验位 / 显示模式
 *           + 打开·关闭串口 + 清屏·关闭
 *
 * 报文与发送共用同一份条目流（RX 字节段 + TX 记录），
 * 所以 ASCII / HEX 切换不会丢内容 —— 同一批数据换种看法而已。
 */
#include "toolbox_windows.h"
#include "toolbox_theme.h"
#include "toolbox_layout.h"   // tl::makeStatus 等：布局/配色唯一来源
#include "tool_kbd.h"
#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/audio/audio.h>
#include <cstdio>
#include <cstring>

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-serial";

// ---------------- 布局（4pt 网格，坐标只走下面这些常量）----------------
// 工具窗口宽 = 父内容区宽（1280 - 导航栏92 - 两侧12）。窗口绝不能比父容器宽，
// 否则右侧被裁 → 右边距变 0，四周边距就不一致了（"上下和左右留白不一样"就是这么来的）。
static constexpr int ToolW    = 1164;
static constexpr int Margin   = 16;                        // 四周边距统一用它
static constexpr int Gap      = 12;                        // 通用间距
static constexpr int LeftX    = Margin;
static constexpr int RightW   = 280;                       // 右列缩窄，让位给报文/发送
static constexpr int RightX   = ToolW - Margin - RightW;   // 868
static constexpr int LeftW    = RightX - Margin - Gap;     // 840
static constexpr int MsgY     = Margin;                    // 16
static constexpr int MsgH     = 542 - Margin;              // 16..542
static constexpr int SendY    = 554;                       // 554..606（与右列"清屏/关闭"同基线）
static constexpr int SendH    = 52;
static constexpr int SendBtnW = 164;
static constexpr int SendBtnX = LeftX + LeftW - SendBtnW;  // 704
static constexpr int RowH     = 56;
static constexpr int RowPitch = 68;                        // 行间距 12（原来只有 4，挤在一起）
static constexpr int RowY0    = 62;
static constexpr int StatusY  = Margin;
static constexpr int StatusH  = 36;
static constexpr int StatusDot = 14;                       // 状态灯直径
static constexpr int StatusDotZone = tb::SpaceMd + StatusDot + tb::SpaceSm;   // 圆点占的横向空间
static constexpr int OpenY    = 470;
static constexpr int OpenH    = 72;
static constexpr int ActY     = 554;
static constexpr int ActH     = 52;
static constexpr int ActBtnW  = (RightW - Gap) / 2;        // 134

// 串口参数候选
static const uint32_t _bauds[8]      = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
static const int      _datas[4]      = {5, 6, 7, 8};
static const int      _pars[3]       = {0, 2, 3};
static const char*    _par_names[3]  = {"N", "E", "O"};
static const int      _stops[2]      = {1, 2};
static const char*    _port_names[5] = {"RS485", "USB 通道 1", "USB 通道 2", "USB 通道 3", "USB 通道 4"};
static const char*    _port_short[5] = {"RS485", "USB1", "USB2", "USB3", "USB4"};   // 状态行用短名，免得超宽被省略

SerialToolWindow::SerialToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, ToolW, 622, 255};
    config.bgColor  = tb::bg();
}

// 设置行：整行一个按钮，左侧名称（暗）、右侧值（琥珀），点一下换下一个值
static void row_name(lv_obj_t* btn, const char* text)
{
    lv_obj_t* n = lv_label_create(btn);
    lv_label_set_text(n, text);
    lv_obj_set_style_text_font(n, tb::fontBody(), 0);
    lv_obj_set_style_text_color(n, lv_color_hex(tb::textDim()), 0);
    lv_obj_align(n, LV_ALIGN_LEFT_MID, tb::SpaceMd, 0);
}

void SerialToolWindow::onOpen()
{
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    mclog::tagInfo(_tag, "on open");

    _items.clear();
    _rx_bytes = 0;
    _opened   = false;
    _hex_mode = false;

    // ==================== 左列 ====================
    // 报文区（只读显示，不挂软键盘）
    _rx_panel = std::make_unique<TextArea>(_window->get());
    tb::makeReadOnly(_rx_panel->get());   // 只读：点一下不该出现光标
    _rx_panel->setSize(LeftW, MsgH);
    _rx_panel->align(LV_ALIGN_TOP_LEFT, LeftX, MsgY);
    _rx_panel->setMaxLength(8192);
    _rx_panel->setCursorClickPos(false);
    _rx_panel->setText("");
    _rx_panel->setPasswordMode(false);
    _rx_panel->setOneLine(false);
    lv_obj_set_style_pad_all(_rx_panel->get(), tb::SpaceMd, 0);
    lv_obj_set_style_border_width(_rx_panel->get(), 1, 0);
    lv_obj_set_style_border_color(_rx_panel->get(), lv_color_hex(tb::border()), 0);
    lv_obj_set_style_bg_color(_rx_panel->get(), lv_color_hex(tb::surface()), 0);
    lv_obj_set_style_radius(_rx_panel->get(), tb::RadiusMd, 0);
    _rx_panel->setTextFont(tb::fontBody());
    _rx_panel->setTextColor(lv_color_hex(tb::text()));
    _rx_panel->setScrollbarMode(LV_SCROLLBAR_MODE_AUTO);

    // 发送输入框
    _tx_input = std::make_unique<TextArea>(_window->get());
    lv_textarea_set_one_line(_tx_input->get(), true);      // 必须在 setSize 之前（否则高度被改成内容自适应）
    _tx_input->setSize(LeftW - SendBtnW - 12, SendH);
    _tx_input->align(LV_ALIGN_TOP_LEFT, LeftX, SendY);
    tb::styleInput(_tx_input->get());
    lv_textarea_set_placeholder_text(_tx_input->get(), "输入要发送的数据…");
    tool_kbd::attach(_tx_input->get());

    // 发送按钮
    _btn_send = std::make_unique<Button>(_window->get());
    _btn_send->setSize(SendBtnW, SendH);
    _btn_send->align(LV_ALIGN_TOP_LEFT, SendBtnX, SendY);
    tb::stylePrimary(_btn_send->get());
    _btn_send->label().setTextFont(tb::fontBody());
    _btn_send->label().setText("发送");
    _btn_send->onClick().connect([&]() {
        audio::play_next_tone_progression();
        const char* txt = lv_textarea_get_text(_tx_input->get());
        if (txt && strlen(txt) > 0) {
            sendText(txt);
            lv_textarea_set_text(_tx_input->get(), "");
        }
    });

    // ==================== 右列 ====================
    // 状态行：一律用统一的 tl::makeStatus（字体 / 对齐 / 正文色 / 圆点 全给你）
    // 【为什么必须统一】这页原来是手写的，颜色只靠 refresh 里几行 setTextColor 撑着；
    // 上一版把六页状态文字统一成正文色、注释掉那些 setTextColor 之后，
    // 这一页就一点颜色都没有了 —— 直接掉回 LVGL 默认灰。
    // 布局/配色代码各页复制 = 迟早漏掉某一页（日志页已栽过一次，这是第二次）。
    _status_label = tl::makeStatus(_window->get(), &_status_dot);
    lv_label_set_long_mode(_status_label->get(), LV_LABEL_LONG_DOT);
    _status_label->setText("已关闭");

    // 设置行（统一构造：整行按钮 + 左名 + 右值）
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

    _row_port = make_row(RowY0 + RowPitch * 0, "端口");
    _row_port->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _port_idx = (_port_idx + 1) % (int)(sizeof(_port_names) / sizeof(_port_names[0]));
        refreshRows();
    });

    _row_baud = make_row(RowY0 + RowPitch * 1, "波特率");
    _row_baud->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _baud_idx = (_baud_idx + 1) % 8;
        if (_opened) applyUartConfig(); else refreshRows();
    });

    _row_data = make_row(RowY0 + RowPitch * 2, "数据位");
    _row_data->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _data_idx = (_data_idx + 1) % 4;
        if (_opened) applyUartConfig(); else refreshRows();
    });

    _row_stop = make_row(RowY0 + RowPitch * 3, "停止位");
    _row_stop->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _stop_idx = (_stop_idx + 1) % 2;
        if (_opened) applyUartConfig(); else refreshRows();
    });

    _row_par = make_row(RowY0 + RowPitch * 4, "校验位");
    _row_par->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _par_idx = (_par_idx + 1) % 3;
        if (_opened) applyUartConfig(); else refreshRows();
    });

    _row_disp = make_row(RowY0 + RowPitch * 5, "显示");
    _row_disp->onClick().connect([&]() {
        audio::play_next_tone_progression();
        setHexMode(!_hex_mode);
    });

    // 打开 / 关闭串口
    _btn_open = std::make_unique<Button>(_window->get());
    _btn_open->setSize(RightW, OpenH);
    _btn_open->align(LV_ALIGN_TOP_LEFT, RightX, OpenY);
    _btn_open->setRadius(tb::RadiusMd);
    _btn_open->label().setTextFont(tb::fontBody());
    _btn_open->onClick().connect([&]() {
        audio::play_next_tone_progression();
        setOpened(!_opened);
    });

    // 清屏 / 关闭工具
    _btn_clear = std::make_unique<Button>(_window->get());
    _btn_clear->setSize(ActBtnW, ActH);
    _btn_clear->align(LV_ALIGN_TOP_LEFT, RightX, ActY);
    _btn_clear->setBgColor(lv_color_hex(tb::neutral()));
    _btn_clear->setRadius(tb::RadiusSm);
    _btn_clear->label().setTextFont(tb::fontBody());
    _btn_clear->label().setTextColor(lv_color_hex(tb::text()));
    _btn_clear->label().setText("清屏");
    _btn_clear->onClick().connect([&]() {
        audio::play_next_tone_progression();
        clearRx();
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

    refreshRows();
    mclog::tagInfo(_tag, "layout ready");
}

void SerialToolWindow::onUpdate()
{
    if (_state != Opened) {
        return;
    }

    // ---- USB 端口：异步打开结果 / 中途被拔出 ----
    if (_port_idx >= 1) {
        if (_usb_pending) {
            const int st = GetHAL()->usbSerialState();
            if (st == 2) {                     // 已打开
                _usb_pending = false;
                _usb_ready   = true;
                applyUartConfig();             // 参数下发到适配器
                addSystemLine("USB 串口已打开");
                refreshRows();
            } else if (st == 3) {              // 没找到 / 不支持
                _usb_pending = false;
                _usb_ready   = false;
                _opened      = false;
                {
                char fm[64];
                snprintf(fm, sizeof(fm), "%s：没找到设备", _port_names[_port_idx]);
                addSystemLine(fm);
                addSystemLine("（确认适配器插好、芯片受支持）");
            }
                refreshRows();
            }
        }
        if (_usb_ready && GetHAL()->usbSerialTakeDisconnected()) {
            _usb_ready = false;
            _opened    = false;
            GetHAL()->usbSerialClose();
            addSystemLine("USB 串口已断开（设备被拔出）");
            refreshRows();
        }
    }

    // ---- 取数据：按当前端口选队列。未真正打开 → 丢弃，免得一开就涌出旧数据 ----
    const bool ready = portReady();
    bool dirty = false;
    {
        auto& q = (_port_idx >= 1) ? GetHAL()->usbSerialData : GetHAL()->uartMonitorData;
        std::lock_guard<std::mutex> lock(q.mutex);
        while (!q.rxQueue.empty()) {
            uint8_t c = q.rxQueue.front();
            q.rxQueue.pop();
            if (!ready) {
                continue;
            }
            if (_items.empty() || _items.back().tx) {
                _items.emplace_back();
            }
            _items.back().bytes.push_back(c);
            _rx_bytes++;
            dirty = true;
        }
    }

    // 裁剪：只保留最近 _RX_MAX 字节
    while (_rx_bytes > _RX_MAX && !_items.empty()) {
        Item& first = _items.front();
        if (first.tx) {
            _items.erase(_items.begin());
            continue;
        }
        if (first.bytes.size() <= (_rx_bytes - _RX_MAX)) {
            _rx_bytes -= first.bytes.size();
            _items.erase(_items.begin());
        } else {
            size_t drop = _rx_bytes - _RX_MAX;
            first.bytes.erase(first.bytes.begin(), first.bytes.begin() + drop);
            _rx_bytes -= drop;
        }
    }

    // 刷新节流：约 100ms 一次，避免高频数据每帧重绘。
    // pending 用 static 保存：数据停了也要把最后一包渲染出来。
    static bool     pending = false;
    static uint32_t tick    = 0;
    if (dirty) {
        pending = true;
    }
    if (pending && ++tick >= 6) {
        tick    = 0;
        pending = false;
        render();
    }
}

void SerialToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    _opened = false;
    _items.clear();
    _rx_bytes = 0;
}

// ---------------- 报文渲染 ----------------
void SerialToolWindow::render()
{
    static std::string out;
    out.clear();
    out.reserve(_rx_bytes * 4 + 256);

    int hex_col = 0;
    char b[8];

    for (const auto& it : _items) {
        if (it.tx) {
            out += "\n[TX] ";
            out += it.text;
            out += "\n";
            hex_col = 0;
            continue;
        }
        if (_hex_mode) {
            for (uint8_t c : it.bytes) {
                snprintf(b, sizeof(b), "%02X ", c);
                out += b;
                if (++hex_col >= 16) {
                    out += '\n';
                    hex_col = 0;
                }
            }
        } else {
            for (uint8_t c : it.bytes) {
                if (c == '\r' || c == '\n') {
                    out += (char)c;
                } else if (c >= 0x20 && c < 0x7F) {
                    out += (char)c;
                } else {
                    out += '.';
                }
            }
        }
    }
    if (_hex_mode && hex_col != 0) {
        out += '\n';
    }

    _rx_panel->setText(out.c_str());
    lv_obj_scroll_to_y(_rx_panel->get(), LV_COORD_MAX, LV_ANIM_OFF);
}

void SerialToolWindow::clearRx()
{
    _items.clear();
    _rx_bytes = 0;
    _rx_panel->setText("");
}

void SerialToolWindow::sendText(const char* text)
{
    if (!portReady()) {
        // 端口没开就别装作发出去了
        Item it;
        it.tx   = true;
        it.text = "[端口未打开] ";
        it.text += text;
        _items.push_back(it);
        render();
        return;
    }
    if (_port_idx >= 1) {
        GetHAL()->usbSerialSend(std::string(text), true);
    } else {
        GetHAL()->uartMonitorSend(text);
    }

    Item it;
    it.tx   = true;
    it.text = text;
    _items.push_back(it);
    render();
}

// 往报文区插一条系统提示（打开 / 断开 / 找不到设备）
void SerialToolWindow::addSystemLine(const char* text)
{
    if (text == nullptr) {
        return;
    }
    Item it;
    it.tx   = true;
    it.text = std::string("· ") + text;
    _items.push_back(it);
    render();
}

// ---------------- 状态 / 配置 ----------------
void SerialToolWindow::setHexMode(bool hex)
{
    if (_hex_mode == hex) {
        return;
    }
    _hex_mode = hex;
    refreshRows();
    render();          // 用同一份数据重画，不清屏
}

void SerialToolWindow::setOpened(bool open)
{
    if (open == _opened) {
        return;
    }
    _opened = open;

    if (open) {
        if (_port_idx >= 1) {
            // USB：异步打开（后台任务等设备），结果由 onUpdate 轮询
            if (GetHAL()->usbSerialOpenAsync(_port_idx)) {
                _usb_pending = true;
                char m[64];
                snprintf(m, sizeof(m), "正在查找 %s …", _port_names[_port_idx]);
                addSystemLine(m);
            } else {
                _opened = false;
                addSystemLine("USB 串口启动失败");
            }
        } else {
            // 丢掉打开前积压的旧数据，避免一开串口就涌出一堆历史
            {
                std::lock_guard<std::mutex> lock(GetHAL()->uartMonitorData.mutex);
                while (!GetHAL()->uartMonitorData.rxQueue.empty()) {
                    GetHAL()->uartMonitorData.rxQueue.pop();
                }
            }
            _items.clear();
            _rx_bytes = 0;
            render();
            applyUartConfig();
        }
        mclog::tagInfo(_tag, "port opened");
    } else {
        if (_port_idx >= 1 && (_usb_ready || _usb_pending)) {
            GetHAL()->usbSerialClose();
            _usb_ready   = false;
            _usb_pending = false;
        }
        mclog::tagInfo(_tag, "port closed");
    }
    refreshRows();
}

void SerialToolWindow::applyUartConfig()
{
    if (_port_idx >= 1) {
        // USB 适配器：波特率等参数下发到**适配器**（USB CDC line coding），
        // 不是 ESP32 自己的 UART —— 走 setRs485Config 对它毫无作用。
        GetHAL()->usbSerialSetLineCoding(_bauds[_baud_idx], _datas[_data_idx], _pars[_par_idx], _stops[_stop_idx]);
    } else {
        GetHAL()->setRs485Config(_bauds[_baud_idx], _datas[_data_idx], _pars[_par_idx], _stops[_stop_idx]);
    }
    refreshRows();
}

// 端口是否真的能收发了（USB 是异步打开，不能只看 _opened）
bool SerialToolWindow::portReady() const
{
    if (!_opened) {
        return false;
    }
    if (_port_idx >= 1) {
        return GetHAL()->usbSerialState() == 2;   // 2 = 已打开
    }
    return true;
}

void SerialToolWindow::refreshRows()
{
    if (!_row_baud) {
        return;
    }

    char b[64];

    // 设置行
    _row_port->label().setText(_port_names[_port_idx]);

    snprintf(b, sizeof(b), "%lu", (unsigned long)_bauds[_baud_idx]);
    _row_baud->label().setText(b);

    snprintf(b, sizeof(b), "%d", _datas[_data_idx]);
    _row_data->label().setText(b);

    snprintf(b, sizeof(b), "%d", _stops[_stop_idx]);
    _row_stop->label().setText(b);

    _row_par->label().setText(_par_names[_par_idx]);

    _row_disp->label().setText(_hex_mode ? "HEX" : "ASCII");

    // 串口开关按钮 + 状态胶囊
    // 参数之间用 " · " 明确分隔 —— 直接空格连排会糊成 "RS4859600 8N1" 这种读不出来的一串
    if (_opened && _port_idx >= 1 && _usb_pending) {
        // USB 还在后台找设备
        _btn_open->label().setText("取消");
        _btn_open->setBgColor(lv_color_hex(tb::raised()));
        _btn_open->label().setTextColor(lv_color_hex(tb::accent()));
        _status_label->setText("正在查找 USB 设备…");
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::warning()), 0);
    } else if (_opened) {
        _btn_open->label().setText("关闭串口");
        _btn_open->setBgColor(lv_color_hex(tb::danger()));
        _btn_open->label().setTextColor(lv_color_hex(tb::text()));

        // 端口名下面那行已经显示了；USB 要额外标一下是哪个口（否则分不清）
        if (_port_idx >= 1) {
            snprintf(b, sizeof(b), "已打开 \xC2\xB7 %s \xC2\xB7 %lu \xC2\xB7 %d%s%d",
                     _port_short[_port_idx],
                     (unsigned long)_bauds[_baud_idx],
                     _datas[_data_idx], _par_names[_par_idx], _stops[_stop_idx]);
        } else {
            snprintf(b, sizeof(b), "已打开 \xC2\xB7 %lu \xC2\xB7 %d%s%d",
                     (unsigned long)_bauds[_baud_idx],
                     _datas[_data_idx], _par_names[_par_idx], _stops[_stop_idx]);
        }
        _status_label->setText(b);
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::success()), 0);
    } else {
        _btn_open->label().setText("打开串口");
        _btn_open->setBgColor(lv_color_hex(tb::raised()));
        _btn_open->label().setTextColor(lv_color_hex(tb::accent()));

        _status_label->setText("已关闭");
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::textDim()), 0);
    }
}
