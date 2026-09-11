/*
 * Nova Toolbox for M5Stack Tab5
 * UDP 调试工具：广播/回环收发测试
 * 后台任务持 UDP socket 收包 -> 队列 -> UI 显示；预设消息发送
 */
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "toolbox_windows.h"
#include "toolbox_theme.h"
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

static constexpr uint16_t _UDP_PORT    = 8888;
static constexpr int _RX_MAX_CHARS     = 3000;

// 目标模式：广播 / 本机回环（自测）
static volatile bool _target_broadcast = true;

UdpToolWindow::UdpToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, 1180, 622, 255};
    config.bgColor  = tb::bg();
}

void UdpToolWindow::udpRxTask(void* arg)
{
    UdpToolWindow* self = static_cast<UdpToolWindow*>(arg);

    // 创建 UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        mclog::tagError(_tag, "socket create failed");
        self->_task_running = false;
        vTaskDelete(nullptr);
        return;
    }
    self->_sock = sock;

    // 允许广播发送
    int broadcast = 1;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

    // 绑定本地端口
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(_UDP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        mclog::tagError(_tag, "bind failed");
        lwip_close(sock);
        self->_sock = -1;
        self->_task_running = false;
        vTaskDelete(nullptr);
        return;
    }

    // 接收超时（100ms），便于检查停止标志
    struct timeval tv = {0, 100000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    mclog::tagInfo(_tag, "udp rx task started on port {}", _UDP_PORT);

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
    self->_sock = -1;
    vTaskDelete(nullptr);
}

void UdpToolWindow::onOpen()
{
    // 中文默认字体：子控件继承，避免未设字体的控件画成方框
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    mclog::tagInfo(_tag, "on open");
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _task_running = true;
    _rx_count     = 0;
    _target_broadcast = true;

    _title_label = std::make_unique<Label>(_window->get());
    _title_label->align(LV_ALIGN_TOP_LEFT, 24, 12);
    _title_label->setText("UDP Tool");
    _title_label->setTextFont(tb::fontTitle());
    _title_label->setTextColor(lv_color_hex(tb::text()));

    _status_label = std::make_unique<Label>(_window->get());
    _status_label->align(LV_ALIGN_TOP_RIGHT, -24, 16);
    _status_label->setText(":8888 -> bcast:8888");
    _status_label->setTextFont(tb::fontBody());
    _status_label->setTextColor(lv_color_hex(tb::textDim()));

    _rx_panel = std::make_unique<TextArea>(_window->get());
    // ---- 目标地址配置行 ----
    auto cfg_lbl = std::make_unique<Label>(_window->get());
    cfg_lbl->align(LV_ALIGN_TOP_LEFT, 24, 64);
    cfg_lbl->setText("目标");
    cfg_lbl->setTextFont(tb::fontBody());
    cfg_lbl->setTextColor(lv_color_hex(tb::textDim()));

    _dst_ip_input = std::make_unique<TextArea>(_window->get());
    _dst_ip_input->setSize(320, 48);
    _dst_ip_input->align(LV_ALIGN_TOP_LEFT, 100, 56);
    _dst_ip_input->setOneLine(true);
    _dst_ip_input->setBorderWidth(1);
    _dst_ip_input->setBorderColor(lv_color_hex(tb::border()));
    _dst_ip_input->setBgColor(lv_color_hex(tb::surface()));
    _dst_ip_input->setTextFont(tb::fontBody());
    _dst_ip_input->setTextColor(lv_color_hex(tb::text()));
    _dst_ip_input->setText("255.255.255.255");
    tool_kbd::attach(_dst_ip_input->get());

    auto port_lbl = std::make_unique<Label>(_window->get());
    port_lbl->align(LV_ALIGN_TOP_LEFT, 440, 64);
    port_lbl->setText("端口");
    port_lbl->setTextFont(tb::fontBody());
    port_lbl->setTextColor(lv_color_hex(tb::textDim()));

    _dst_port_input = std::make_unique<TextArea>(_window->get());
    _dst_port_input->setSize(140, 48);
    _dst_port_input->align(LV_ALIGN_TOP_LEFT, 512, 56);
    _dst_port_input->setOneLine(true);
    _dst_port_input->setBorderWidth(1);
    _dst_port_input->setBorderColor(lv_color_hex(tb::border()));
    _dst_port_input->setBgColor(lv_color_hex(tb::surface()));
    _dst_port_input->setTextFont(tb::fontBody());
    _dst_port_input->setTextColor(lv_color_hex(tb::text()));
    _dst_port_input->setText("8888");
    tool_kbd::attach(_dst_port_input->get());

    _btn_apply = std::make_unique<Button>(_window->get());
    _btn_apply->setSize(152, 48);
    _btn_apply->align(LV_ALIGN_TOP_LEFT, 672, 56);
    _btn_apply->setBgColor(lv_color_hex(tb::raised()));
    _btn_apply->setRadius(tb::RadiusMd);
    _btn_apply->label().setTextFont(tb::fontBody());
    _btn_apply->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_apply->label().setText("Connect");
    _btn_apply->onClick().connect([&]() {
        audio::play_next_tone_progression();
        const char* ip = lv_textarea_get_text(_dst_ip_input->get());
        const char* pt = lv_textarea_get_text(_dst_port_input->get());
        int port = atoi(pt);
        if (ip && ip[0]) {
            _dst_ip = ip;
        }
        if (port > 0 && port < 65536) {
            _dst_port = (uint16_t)port;
        }
        if (_status_label) {
            char buf[96];
            snprintf(buf, sizeof(buf), "TX -> %s:%u", _dst_ip.c_str(), (unsigned)_dst_port);
            _status_label->setText(buf);
        }
        if (_rx_panel) {
            _rx_panel->addText("\n[cfg] target set\n");
        }
    });

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

    // TX 自定义输入行（点击弹软键盘）
    _tx_input = std::make_unique<TextArea>(_window->get());
    _tx_input->setSize(840, 52);
    _tx_input->align(LV_ALIGN_TOP_LEFT, 24, 456);
    _tx_input->setPlaceholderText("custom payload...");
    lv_textarea_set_placeholder_text(_tx_input->get(), "custom payload...");
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
    _btn_send_tx->label().setText("Send Custom");
    _btn_send_tx->onClick().connect([&]() {
        audio::play_next_tone_progression();
        const char* txt = lv_textarea_get_text(_tx_input->get());
        if (txt && strlen(txt) > 0) {
            sendText(txt);
        }
    });

    auto make_btn = [&](int x, const char* text, uint32_t color) {
        auto b = std::make_unique<Button>(_window->get());
        b->setSize(208, 56);
        b->align(LV_ALIGN_BOTTOM_LEFT, x, -18);
        b->setBgColor(lv_color_hex(color));
        b->setRadius(tb::RadiusMd);
        b->label().setTextFont(tb::fontBody());
        b->label().setTextColor(lv_color_hex(tb::accent()));
        b->label().setText(text);
        return b;
    };

    _btn_hello = make_btn(30, "Send Hello", tb::raised());
    _btn_hello->onClick().connect([&]() {
        audio::play_next_tone_progression();
        sendText("Hello from Tab5!");
    });

    _btn_probe = make_btn(255, "Send Probe", tb::raised());
    _btn_probe->onClick().connect([&]() {
        audio::play_next_tone_progression();
        sendText("Tab5-UDP-Probe");
    });

    _btn_loopback = make_btn(480, "Self-Test", tb::raised());
    _btn_loopback->onClick().connect([&]() {
        audio::play_next_tone_progression();
        // 回环自测：临时把目标换成 127.0.0.1
        std::string saved = _dst_ip;
        _dst_ip           = "127.0.0.1";
        sendText("loopback-test");
        _dst_ip = saved;
    });

    _btn_clear = make_btn(705, "Clear", 0x4A3A3A);
    _btn_clear->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _rx_panel->setText("");
    });

    _btn_close = make_btn(960, "Close", tb::danger());
    _btn_close->onClick().connect([&]() {
        audio::play_next_tone_progression();
        close();
    });

    // 启动收包任务
    xTaskCreate(udpRxTask, "udp_rx", 4096, this, 5, (TaskHandle_t*)&_task_handle);
}

void UdpToolWindow::onUpdate()
{
    if (_state != Opened) {
        return;
    }

    // 搬运收包队列到 UI
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
        // 防 TextArea 无限增长
        const char* cur = lv_textarea_get_text(_rx_panel->get());
        if (cur && strlen(cur) > _RX_MAX_CHARS) {
            // 截断显示：保留尾部
            const char* tail = cur + strlen(cur) - _RX_MAX_CHARS;
            _rx_panel->setText(tail);
        }
    }
}

void UdpToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    _task_running = false;
    // 等后台任务退出：任务循环里每次 delay 50ms，这里给足 300ms
    vTaskDelay(pdMS_TO_TICKS(300));

}

void UdpToolWindow::sendText(const char* text)
{
    if (_sock < 0) {
        mclog::tagWarn(_tag, "socket not ready");
        return;
    }

    struct sockaddr_in dst;
    memset(&dst, 0, sizeof(dst));
    dst.sin_family = AF_INET;
    dst.sin_port   = htons(_dst_port);
    if (inet_pton(AF_INET, _dst_ip.c_str(), &dst.sin_addr) != 1) {
        dst.sin_addr.s_addr = htonl(INADDR_BROADCAST);  // IP 解析失败则广播
    }

    int len = sendto(_sock, text, strlen(text), 0, (struct sockaddr*)&dst, sizeof(dst));
    if (len < 0) {
        mclog::tagWarn(_tag, "sendto failed errno={}", errno);
        _rx_panel->addText("\n[TX FAILED]\n");
        return;
    }

    char echo[160];
    snprintf(echo, sizeof(echo), "\n[TX -> %s:%u] %s\n", _dst_ip.c_str(), (unsigned)_dst_port, text);
    _rx_panel->addText(echo);
}

#endif  // CONFIG_IDF_TARGET_ESP32P4