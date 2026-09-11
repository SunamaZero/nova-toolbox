/*
 * Nova Toolbox for M5Stack Tab5
 * TCP Server 调试工具：监听 8888 等客户端连接，收数据显示 + 预设回复
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
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-tcp";

static constexpr uint16_t _TCP_PORT = 8888;
static constexpr int _RX_MAX_CHARS  = 3000;

TcpToolWindow::TcpToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, 1180, 622, 255};
    config.bgColor  = tb::bg();
}

void TcpToolWindow::tcpSrvTask(void* arg)
{
    TcpToolWindow* self = static_cast<TcpToolWindow*>(arg);

    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_sock < 0) {
        self->_task_running = false;
        vTaskDelete(nullptr);
        return;
    }
    self->_listen_sock = listen_sock;

    int reuse = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_port        = htons(self->_tcp_port);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(listen_sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        mclog::tagError(_tag, "bind failed");
        lwip_close(listen_sock);
        self->_listen_sock = -1;
        self->_task_running = false;
        vTaskDelete(nullptr);
        return;
    }
    if (listen(listen_sock, 1) < 0) {
        mclog::tagError(_tag, "listen failed");
        lwip_close(listen_sock);
        self->_listen_sock = -1;
        self->_task_running = false;
        vTaskDelete(nullptr);
        return;
    }

    struct timeval tv = {0, 100000};
    setsockopt(listen_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    mclog::tagInfo(_tag, "tcp server listening on {}", self->_tcp_port);

    char buf[1024];
    while (self->_task_running) {
        // 等待/接受客户端
        if (self->_client_sock < 0) {
            struct sockaddr_in cli;
            socklen_t cli_len = sizeof(cli);
            int c = accept(listen_sock, (struct sockaddr*)&cli, &cli_len);
            if (c >= 0) {
                self->_client_sock = c;
                self->_client_connected = true;
                struct timeval ctv = {0, 100000};  // 100ms：保证能及时看到停止标志，避免窗口析构后访问野指针
                setsockopt(c, SOL_SOCKET, SO_RCVTIMEO, &ctv, sizeof(ctv));
                mclog::tagInfo(_tag, "client connected");
            }
            continue;
        }

        // 收客户端数据
        int len = recv(self->_client_sock, buf, sizeof(buf) - 1, 0);
        if (len > 0) {
            std::string packet;
            packet.reserve(len + 16);
            packet += "[cli] ";
            packet.append(buf, len);
            {
                std::lock_guard<std::mutex> lock(self->_rx_mutex);
                self->_rx_packets.push(std::move(packet));
                if (self->_rx_packets.size() > 50) {
                    self->_rx_packets.pop();
                }
            }
            self->_rx_count++;
        } else if (len == 0) {
            // 客户端断开
            mclog::tagInfo(_tag, "client disconnected");
            lwip_close(self->_client_sock);
            self->_client_sock = -1;
            self->_client_connected = false;
            {
                std::lock_guard<std::mutex> lock(self->_rx_mutex);
                self->_rx_packets.push("\n[client disconnected]\n");
            }
        }
    }

    mclog::tagInfo(_tag, "tcp task exit");
    if (self->_client_sock >= 0) {
        lwip_close(self->_client_sock);
        self->_client_sock = -1;
    }
    lwip_close(listen_sock);
    self->_listen_sock = -1;
    self->_client_connected = false;
    vTaskDelete(nullptr);
}

void TcpToolWindow::onOpen()
{
    // 中文默认字体：子控件继承，避免未设字体的控件画成方框
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    mclog::tagInfo(_tag, "on open");
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _task_running = true;
    _rx_count     = 0;

    _title_label = std::make_unique<Label>(_window->get());
    _title_label->align(LV_ALIGN_TOP_LEFT, 24, 12);
    _title_label->setText("TCP 服务");
    _title_label->setTextFont(tb::fontTitle());
    _title_label->setTextColor(lv_color_hex(tb::text()));

    _status_label = std::make_unique<Label>(_window->get());
    _status_label->align(LV_ALIGN_TOP_RIGHT, -24, 16);
    _status_label->setText("Listen :8888 | waiting");
    _status_label->setTextFont(tb::fontBody());
    _status_label->setTextColor(lv_color_hex(tb::textDim()));

    _rx_panel = std::make_unique<TextArea>(_window->get());
    // ---- 端口配置 ----
    auto plbl = std::make_unique<Label>(_window->get());
    plbl->align(LV_ALIGN_TOP_LEFT, 24, 64);
    plbl->setText("监听端口");
    plbl->setTextFont(tb::fontBody());
    plbl->setTextColor(lv_color_hex(tb::textDim()));

    _port_input = std::make_unique<TextArea>(_window->get());
    _port_input->setSize(160, 48);
    _port_input->align(LV_ALIGN_TOP_LEFT, 200, 56);
    _port_input->setOneLine(true);
    _port_input->setBorderWidth(1);
    _port_input->setBorderColor(lv_color_hex(tb::border()));
    _port_input->setBgColor(lv_color_hex(tb::surface()));
    _port_input->setTextFont(tb::fontBody());
    _port_input->setTextColor(lv_color_hex(tb::text()));
    _port_input->setText("8888");
    tool_kbd::attach(_port_input->get());

    _btn_apply_port = std::make_unique<Button>(_window->get());
    _btn_apply_port->setSize(152, 48);
    _btn_apply_port->align(LV_ALIGN_TOP_LEFT, 380, 56);
    _btn_apply_port->setBgColor(lv_color_hex(tb::raised()));
    _btn_apply_port->setRadius(tb::RadiusMd);
    _btn_apply_port->label().setTextFont(tb::fontBody());
    _btn_apply_port->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_apply_port->label().setText("Connect");
    _btn_apply_port->onClick().connect([&]() {
        audio::play_next_tone_progression();
        int p = atoi(lv_textarea_get_text(_port_input->get()));
        if (p > 0 && p < 65536) {
            _tcp_port = (uint16_t)p;
            char buf[64];
            snprintf(buf, sizeof(buf), "端口=%u（重启生效）", (unsigned)_tcp_port);
            _status_label->setText(buf);
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

    _btn_echo = make_btn(30, "Echo Back", tb::raised());
    _btn_echo->onClick().connect([&]() {
        audio::play_next_tone_progression();
        sendToClient("echo from Tab5\r\n");
    });

    _btn_hello = make_btn(255, "Send Hello", tb::raised());
    _btn_hello->onClick().connect([&]() {
        audio::play_next_tone_progression();
        sendToClient("Hello from Tab5 TCP!\r\n");
    });

    _btn_probe = make_btn(480, "Send Probe", tb::raised());
    _btn_probe->onClick().connect([&]() {
        audio::play_next_tone_progression();
        sendToClient("Tab5-TCP-Probe\r\n");
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

    xTaskCreate(tcpSrvTask, "tcp_srv", 4096, this, 5, (TaskHandle_t*)&_task_handle);
}

void TcpToolWindow::onUpdate()
{
    if (_state != Opened) {
        return;
    }

    // 连接状态刷新
    if (_client_connected) {
        _status_label->setText("Listen :8888 | client ON ");
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
}

void TcpToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    _task_running = false;
    // 等后台任务退出：任务循环里每次 delay 50ms，这里给足 300ms
    vTaskDelay(pdMS_TO_TICKS(300));

}

void TcpToolWindow::sendToClient(const char* text)
{
    if (_client_sock < 0) {
        _rx_panel->addText("\n[no client connected]\n");
        return;
    }
    int len = send(_client_sock, text, strlen(text), 0);
    if (len < 0) {
        _rx_panel->addText("\n[send failed]\n");
        return;
    }
    std::string echo = "\n[TX] ";
    echo += text;
    _rx_panel->addText(echo.c_str());
}

#endif  // CONFIG_IDF_TARGET_ESP32P4