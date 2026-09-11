/*
 * Nova Toolbox for M5Stack Tab5
 * 串口调试工具（RS485 UART 监视）
 * 功能：HEX/ASCII 显示切换、清屏、预设消息发送
 */
#include "toolbox_windows.h"
#include "toolbox_theme.h"
#include "tool_kbd.h"
#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/audio/audio.h>
#include <cstdio>

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-serial";

// 显示缓冲区上限（超出截断，防 TextArea 膨胀）
static constexpr int _RX_MAX_CHARS = 3000;

SerialToolWindow::SerialToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, 1180, 622, 255};
    config.bgColor  = tb::bg();
}

void SerialToolWindow::onOpen()
{
    // 中文默认字体：子控件继承，避免未设字体的控件画成方框
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    mclog::tagInfo(_tag, "on open");
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _hex_mode = false;
    _hex_buf.clear();

    // 标题
    auto title = std::make_unique<Label>(_window->get());
    title->align(LV_ALIGN_TOP_LEFT, 24, 12);
    title->setText("串口调试 RS485");
    title->setTextFont(tb::fontTitle());
    title->setTextColor(lv_color_hex(tb::text()));
    _title_label = std::move(title);

    // 状态行
    _status_label = std::make_unique<Label>(_window->get());
    _status_label->align(LV_ALIGN_TOP_RIGHT, -24, 16);
    _status_label->setText("115200 | ASCII");
    _status_label->setTextFont(tb::fontBody());
    _status_label->setTextColor(lv_color_hex(tb::textDim()));

    // ==================== 配置行：波特率 / 数据位 / 校验 / 停止位 ====================
    // 波特率
    _btn_baud = std::make_unique<Button>(_window->get());
    _btn_baud->setSize(140, 48);
    _btn_baud->align(LV_ALIGN_TOP_LEFT, 24, 56);
    _btn_baud->setBgColor(lv_color_hex(tb::raised()));
    _btn_baud->setRadius(tb::RadiusSm);
    _btn_baud->label().setTextFont(tb::fontBody());
    _btn_baud->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_baud->label().setText("115200");
    _btn_baud->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _baud_idx = (_baud_idx + 1) % 8;
        applyUartConfig();
    });

    // 数据位
    _btn_data = std::make_unique<Button>(_window->get());
    _btn_data->setSize(96, 48);
    _btn_data->align(LV_ALIGN_TOP_LEFT, 168, 56);
    _btn_data->setBgColor(lv_color_hex(tb::raised()));
    _btn_data->setRadius(tb::RadiusSm);
    _btn_data->label().setTextFont(tb::fontBody());
    _btn_data->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_data->label().setText("8 bit");
    _btn_data->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _data_idx = (_data_idx + 1) % 4;
        applyUartConfig();
    });

    // 校验位
    _btn_par = std::make_unique<Button>(_window->get());
    _btn_par->setSize(96, 48);
    _btn_par->align(LV_ALIGN_TOP_LEFT, 268, 56);
    _btn_par->setBgColor(lv_color_hex(tb::raised()));
    _btn_par->setRadius(tb::RadiusSm);
    _btn_par->label().setTextFont(tb::fontBody());
    _btn_par->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_par->label().setText("N");
    _btn_par->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _par_idx = (_par_idx + 1) % 3;
        applyUartConfig();
    });

    // 停止位
    _btn_stop = std::make_unique<Button>(_window->get());
    _btn_stop->setSize(104, 48);
    _btn_stop->align(LV_ALIGN_TOP_LEFT, 368, 56);
    _btn_stop->setBgColor(lv_color_hex(tb::raised()));
    _btn_stop->setRadius(tb::RadiusSm);
    _btn_stop->label().setTextFont(tb::fontBody());
    _btn_stop->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_stop->label().setText("1 stop");
    _btn_stop->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _stop_idx = (_stop_idx + 1) % 2;
        applyUartConfig();
    });

    // 配置摘要
    _cfg_label = std::make_unique<Label>(_window->get());
    _cfg_label->align(LV_ALIGN_TOP_LEFT, 488, 68);
    _cfg_label->setText("115200 8N1");
    _cfg_label->setTextFont(tb::fontSm());
    _cfg_label->setTextColor(lv_color_hex(tb::textDim()));

    // RX 显示区
    _rx_panel = std::make_unique<TextArea>(_window->get());
    _rx_panel->setSize(1132, 332);
    _rx_panel->align(LV_ALIGN_TOP_LEFT, 24, 112);
    _rx_panel->setMaxLength(4096);
    _rx_panel->setCursorClickPos(false);
    _rx_panel->setText("");
    _rx_panel->setPasswordMode(false);
    _rx_panel->setOneLine(false);
    _rx_panel->setBorderWidth(1);
    _rx_panel->setBorderColor(lv_color_hex(tb::border()));
    _rx_panel->setBgColor(lv_color_hex(tb::surface()));
    _rx_panel->setTextFont(tb::fontBody());
    _rx_panel->setTextColor(lv_color_hex(tb::text()));
    _rx_panel->setScrollbarMode(LV_SCROLLBAR_MODE_AUTO);

    // ---- TX 自定义发送行（点击弹软键盘输入任意指令）----
    _tx_input = std::make_unique<TextArea>(_window->get());
    _tx_input->setSize(840, 52);
    _tx_input->align(LV_ALIGN_TOP_LEFT, 24, 456);
    lv_textarea_set_placeholder_text(_tx_input->get(), "send raw command...");
    _tx_input->setOneLine(true);
    _tx_input->setBorderWidth(1);
    _tx_input->setBorderColor(lv_color_hex(tb::border()));
    _tx_input->setBgColor(lv_color_hex(tb::surface()));
    _tx_input->setTextFont(tb::fontBody());
    _tx_input->setTextColor(lv_color_hex(tb::text()));
    tool_kbd::attach(_tx_input->get());

    _btn_send_tx = std::make_unique<Button>(_window->get());
    _btn_send_tx->setSize(292, 52);
    _btn_send_tx->align(LV_ALIGN_TOP_LEFT, 880, 456);
    _btn_send_tx->setBgColor(lv_color_hex(tb::raised()));
    _btn_send_tx->setRadius(tb::RadiusMd);
    _btn_send_tx->label().setTextFont(tb::fontBody());
    _btn_send_tx->label().setText("Send");
    _btn_send_tx->onClick().connect([&]() {
        audio::play_next_tone_progression();
        const char* txt = lv_textarea_get_text(_tx_input->get());
        if (txt && strlen(txt) > 0) {
            sendText(txt);
        }
    });

    // ---- 底部按钮排 ----
    auto make_btn = [&](int x, const char* text, uint32_t color) {
        auto b = std::make_unique<Button>(_window->get());
        b->setSize(168, 56);
        b->align(LV_ALIGN_BOTTOM_LEFT, x, -18);
        b->setBgColor(lv_color_hex(color));
        b->setRadius(tb::RadiusMd);
        b->label().setTextFont(tb::fontBody());
        b->label().setTextColor(lv_color_hex(tb::accent()));
        b->label().setText(text);
        return b;
    };

    _btn_hex = make_btn(30, "HEX:OFF", tb::raised());
    _btn_hex->onClick().connect([&]() {
        audio::play_next_tone_progression();
        toggleHex();
    });

    _btn_clear = make_btn(215, "Clear", 0x4A3A3A);
    _btn_clear->onClick().connect([&]() {
        audio::play_next_tone_progression();
        clearRx();
    });

    _btn_send_at = make_btn(400, "Send AT", tb::raised());
    _btn_send_at->onClick().connect([&]() {
        audio::play_next_tone_progression();
        sendText("AT");
    });

    _btn_send_hello = make_btn(585, "Hello", tb::raised());
    _btn_send_hello->onClick().connect([&]() {
        audio::play_next_tone_progression();
        sendText("Hello M5Stack!");
    });

    _btn_send_scan = make_btn(770, "AA 55", tb::raised());
    _btn_send_scan->onClick().connect([&]() {
        audio::play_next_tone_progression();
        // 十六进制探针 AA 55 00 FF（二进制经 std::string 传输）
        std::string probe("\xAA\x55\x00\xFF", 4);
        GetHAL()->uartMonitorSend(probe, false);
    });

    _btn_close = make_btn(975, "Close", tb::danger());
    _btn_close->onClick().connect([&]() {
        audio::play_next_tone_progression();
        close();
    });
}

void SerialToolWindow::onUpdate()
{
    if (_state != Opened) {
        return;
    }

    // 从 RX 队列取数据（官方 RS485 任务灌入）
    std::string ascii_chunk;
    ascii_chunk.reserve(128);

    {
        std::lock_guard<std::mutex> lock(GetHAL()->uartMonitorData.mutex);
        while (!GetHAL()->uartMonitorData.rxQueue.empty()) {
            uint8_t c = GetHAL()->uartMonitorData.rxQueue.front();
            GetHAL()->uartMonitorData.rxQueue.pop();

            if (_hex_mode) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%02X ", c);
                _hex_buf += buf;
                // 每 16 字节换行
                if (++_hex_col >= 16) {
                    _hex_buf += "\n";
                    _hex_col = 0;
                }
            } else {
                // ASCII：可打印 + 常见控制符
                if (c == '\r' || c == '\n') {
                    ascii_chunk += (char)c;
                } else if (c >= 0x20 && c < 0x7F) {
                    ascii_chunk += (char)c;
                } else {
                    ascii_chunk += '.';
                }
            }
        }
    }

    // 刷新显示
    if (_hex_mode) {
        if (!_hex_buf.empty()) {
            if (_hex_buf.size() > _RX_MAX_CHARS) {
                _hex_buf = _hex_buf.substr(_hex_buf.size() - _RX_MAX_CHARS);
            }
            _rx_panel->setText(_hex_buf.c_str());
        }
    } else if (!ascii_chunk.empty()) {
        _rx_panel->addText(ascii_chunk.c_str());
    }
}

void SerialToolWindow::applyUartConfig()
{
    static const uint32_t bauds[8]  = {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600};
    static const int datas[4]       = {5, 6, 7, 8};
    static const int pars[3]        = {0, 2, 3};      // none / even / odd
    static const char* par_names[3] = {"N", "E", "O"};
    static const int stops[2]       = {1, 2};         // 1 / 2 bit

    uint32_t baud = bauds[_baud_idx];
    int data      = datas[_data_idx];
    int par       = pars[_par_idx];
    int stop      = stops[_stop_idx];

    GetHAL()->setRs485Config(baud, data, par, stop);

    char b1[16], b2[16], b3[8], b4[16], sum[48];
    snprintf(b1, sizeof(b1), "%lu", (unsigned long)baud);
    snprintf(b2, sizeof(b2), "%d bit", data);
    snprintf(b3, sizeof(b3), "%s", par_names[_par_idx]);
    snprintf(b4, sizeof(b4), "%d stop", stop);
    if (_btn_baud) _btn_baud->label().setText(b1);
    if (_btn_data) _btn_data->label().setText(b2);
    if (_btn_par) _btn_par->label().setText(b3);
    if (_btn_stop) _btn_stop->label().setText(b4);
    snprintf(sum, sizeof(sum), "%lu %d%s%d", (unsigned long)baud, data, par_names[_par_idx], stop);
    if (_cfg_label) _cfg_label->setText(sum);
    if (_status_label) _status_label->setText(sum);
    if (_rx_panel) {
        _rx_panel->addText("\n[cfg] ");
        _rx_panel->addText(sum);
        _rx_panel->addText("\n");
    }
}

void SerialToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");

}

void SerialToolWindow::clearRx()
{
    _hex_buf.clear();
    _rx_panel->setText("");
}

void SerialToolWindow::sendText(const char* text)
{
    // 回显到 RX 区
    _rx_panel->addText("\r\n[TX] ");
    _rx_panel->addText(text);
    _rx_panel->addText("\r\n");
    GetHAL()->uartMonitorSend(text);
}

void SerialToolWindow::toggleHex()
{
    _hex_mode = !_hex_mode;
    _btn_hex->label().setText(_hex_mode ? "HEX:ON" : "HEX:OFF");
    _status_label->setText(_hex_mode ? "115200 | HEX  " : "115200 | ASCII");
    clearRx();
}
