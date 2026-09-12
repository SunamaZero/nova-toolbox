/*
 * Nova Toolbox for M5Stack Tab5
 * UDP 调试工具
 *
 * 布局（与串口页同构，走 toolbox_layout.h 的骨架）：
 *   左列 —— 报文区 + 发送行
 *   右列 —— 状态 / 目标地址 / 目标端口 / 本机端口 / 收包数
 *           + 开始·停止监听 + 清空·关闭
 *
 * 后台任务持 UDP socket 收包 → 队列 → UI 显示。
 * 「开始/停止监听」是真的建 socket / 关 socket，不是摆设。
 */
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "toolbox_windows.h"
#include "toolbox_theme.h"
#include "toolbox_layout.h"
#include "net_tools.h"
#include "tool_kbd.h"
#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/audio/audio.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-udp";

static constexpr int _RX_MAX_CHARS = 3000;

// 本机监听端口候选（与串口页"波特率"一样：点一下换下一个）
static const uint16_t _local_ports[4]  = {8888, 9999, 12345, 5000};
static const char*    _local_port_str[4] = {"8888", "9999", "12345", "5000"};

UdpToolWindow::UdpToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, tl::W, tl::PageH, 255};
    config.bgColor  = tb::bg();
}

void UdpToolWindow::udpRxTask(void* arg)
{
    UdpToolWindow* self = static_cast<UdpToolWindow*>(arg);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        mclog::tagError(_tag, "socket create failed");
        self->_task_running = false;
        self->_listening    = false;
        vTaskDelete(nullptr);
        return;
    }
    self->_sock = sock;

    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(self->_local_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        mclog::tagError(_tag, "bind failed on port {}", self->_local_port);
        lwip_close(sock);
        self->_sock         = -1;
        self->_task_running = false;
        self->_listening    = false;
        vTaskDelete(nullptr);
        return;
    }

    struct timeval tv = {0, 100000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    self->_listening = true;
    mclog::tagInfo(_tag, "udp rx task started on port {}", self->_local_port);

    char buf[1024];
    while (self->_task_running) {
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int len = recvfrom(sock, buf, sizeof(buf) - 1, 0, (struct sockaddr*)&from, &from_len);
        if (len > 0) {
            std::string packet;
            packet.reserve(len + 40);
            packet += "[";
            packet += inet_ntoa(from.sin_addr);
            packet += ":";
            packet += std::to_string(ntohs(from.sin_port));
            packet += "] ";
            packet.append(buf, len);
            {
                std::lock_guard<std::mutex> lock(self->_rx_mutex);
                self->_rx_packets.push(std::move(packet));
                if (self->_rx_packets.size() > 50) {
                    self->_rx_packets.pop();
                }
            }
            self->_rx_count++;
        }
    }

    mclog::tagInfo(_tag, "udp rx task exit");
    lwip_close(sock);
    self->_sock      = -1;
    self->_listening = false;
    vTaskDelete(nullptr);
}

// 开始 / 停止监听（真建 socket / 真关 socket）
void UdpToolWindow::setListening(bool on)
{
    if (on == _listening || on == _task_running) {
        return;
    }
    if (on) {
        // 目标地址与端口从界面读一次
        if (_row_dst_ip) {
            const char* ip = lv_textarea_get_text(_row_dst_ip->get());
            if (ip && ip[0]) _dst_ip = ip;
        }
        if (_row_dst_port) {
            int p = atoi(lv_textarea_get_text(_row_dst_port->get()));
            if (p > 0 && p < 65536) _dst_port = (uint16_t)p;
        }
        _task_running = true;
        if (xTaskCreate(udpRxTask, "udp_rx", 4096, this, 5, (TaskHandle_t*)&_task_handle) != pdPASS) {
            _task_running = false;
            _rx_panel->addText("[启动失败：任务创建失败]\n");
        } else {
            _rx_panel->addText("[开始监听]\n");
        }
    } else {
        _task_running = false;
        vTaskDelay(pdMS_TO_TICKS(300));   // 等收包任务的 recvfrom 超时退出
        _rx_panel->addText("[停止监听]\n");
    }
    refreshUi();
}

void UdpToolWindow::refreshUi()
{
    if (!_status_label) {
        return;
    }
    const bool live = _listening && _sock >= 0;

    char b[80];
    if (live) {
        snprintf(b, sizeof(b), "监听中 :%u", (unsigned)_local_port);
    } else {
        snprintf(b, sizeof(b), "已停止");
    }
    _status_label->setText(b);
    if (_status_dot) {
        lv_obj_set_style_bg_color(_status_dot, lv_color_hex(live ? tb::success() : tb::textDim()), 0);
    }
    if (_btn_run) {
        _btn_run->label().setText(live ? "停止监听" : "开始监听");
        _btn_run->label().setTextColor(lv_color_hex(live ? tb::danger() : tb::accent()));
        _btn_run->setBorderColor(lv_color_hex(live ? tb::danger() : tb::accent()));
    }
    if (_row_local) {
        _row_local->label().setText(_local_port_str[_port_idx]);
    }
    if (_info_rx) {
        lv_label_set_text_fmt(_info_rx, "%u", (unsigned)_rx_count);
    }
}

void UdpToolWindow::onOpen()
{
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    mclog::tagInfo(_tag, "on open");

    _task_running = false;
    _task_handle  = nullptr;
    _sock         = -1;
    _listening    = false;
    _rx_count     = 0;
    _port_idx     = 0;
    _local_port   = _local_ports[0];

    // ==================== 左列 ====================
    _rx_panel = tl::makePanel(_window->get());

    tl::SendRow sr = tl::makeSendRow(_window->get(), "自定义内容…");
    _tx_input    = std::move(sr.input);
    _btn_send_tx = std::move(sr.button);
    _btn_send_tx->onClick().connect([&]() {
        audio::play_next_tone_progression();
        const char* txt = lv_textarea_get_text(_tx_input->get());
        if (txt && strlen(txt) > 0) {
            sendText(txt);
        }
    });

    // ==================== 右列 ====================
    _status_label = tl::makeStatus(_window->get(), &_status_dot);

    _row_dst_ip   = tl::makeRowInput(_window->get(), tl::rowY(0), "目标地址", _dst_ip.c_str());
    _row_dst_port = tl::makeRowInput(_window->get(), tl::rowY(1), "目标端口", "8888");

    _row_local = tl::makeRow(_window->get(), tl::rowY(2), "本机端口");
    _row_local->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _port_idx   = (_port_idx + 1) % 4;
        _local_port = _local_ports[_port_idx];
        if (_listening || _task_running) {
            // 端口改了得重新绑：先停，再确保状态复位后重新开始
            setListening(false);
            _listening = false;
            setListening(true);
        } else {
            refreshUi();
        }
    });

    tl::makeInfoRow(_window->get(), tl::rowY(3), "收包数", "0", &_info_rx);

    _btn_run = tl::makePrimary(_window->get(), "开始监听");
    _btn_run->onClick().connect([&]() {
        audio::play_next_tone_progression();
        setListening(!(_listening && _sock >= 0));
    });

    _btn_clear = tl::makeAction(_window->get(), 0, "清空", tb::neutral(), tb::text());
    _btn_clear->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _rx_panel->setText("");
        _rx_count = 0;
        refreshUi();
    });

    _btn_close = tl::makeAction(_window->get(), 1, "关闭", tb::danger(), tb::text());
    _btn_close->onClick().connect([&]() {
        audio::play_next_tone_progression();
        close();
    });

    refreshUi();
}

void UdpToolWindow::onUpdate()
{
    if (_state != Opened) {
        return;
    }

    std::string batch;
    {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        while (!_rx_packets.empty()) {
            batch += _rx_packets.front();
            batch += "\n";
            _rx_packets.pop();
        }
    }
    if (!batch.empty()) {
        _rx_panel->addText(batch.c_str());
        const char* cur = lv_textarea_get_text(_rx_panel->get());
        if (cur && strlen(cur) > _RX_MAX_CHARS) {
            const char* tail = cur + strlen(cur) - _RX_MAX_CHARS;
            _rx_panel->setText(tail);
        }
    }

    // 状态与计数低频刷新（监听状态可能被后台任务改成停止）
    static uint32_t tick = 0;
    if (++tick % 5 == 0) {
        refreshUi();
    }
}

void UdpToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    _task_running = false;
    vTaskDelay(pdMS_TO_TICKS(300));
    _listening = false;
}

void UdpToolWindow::sendText(const char* text)
{
    if (_sock < 0 || !_listening) {
        // 没监听就发不出去 —— 如实说，不假装发出去了
        _rx_panel->addText("[未开始监听] ");
        _rx_panel->addText(text);
        _rx_panel->addText("\n");
        return;
    }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(_dst_port);
    if (inet_pton(AF_INET, _dst_ip.c_str(), &dst.sin_addr) != 1) {
        dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);
    }

    int len = sendto(_sock, text, strlen(text), 0, (struct sockaddr*)&dst, sizeof(dst));
    if (len < 0) {
        mclog::tagWarn(_tag, "sendto failed errno={}", errno);
        _rx_panel->addText("[发送失败]\n");
        return;
    }

    char echo[192];
    snprintf(echo, sizeof(echo), "[TX → %s:%u] %s\n", _dst_ip.c_str(), (unsigned)_dst_port, text);
    _rx_panel->addText(echo);
}

#endif  // CONFIG_IDF_TARGET_ESP32P4
