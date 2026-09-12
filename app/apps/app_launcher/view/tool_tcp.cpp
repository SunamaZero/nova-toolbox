/*
 * Nova Toolbox for M5Stack Tab5
 * TCP 服务端调试工具：监听端口 → 接受 1 个客户端 → 收发
 *
 * 布局（与串口页同构，几何全部来自 toolbox_layout.h 的 tl:: 常量）：
 *   左列 —— 报文区 + 发送行（输入框 + 发送按钮）
 *   右列 —— 状态（监听中 :端口 / 已停止 / 客户端已连接）
 *           监听端口（循环切换）/ 客户端（纯展示）/ 收包（纯展示）
 *           + 开始·停止监听 + 清空·关闭
 *
 * 只做服务端：不再有客户端模式 —— 一个页面一种用途，省得两种状态的按钮互相抢。
 * 也删掉了 Echo/Hello/Probe 那三个写死载荷的演示按钮：演示数据是噪音，
 * 真要用就自己在发送框里打。
 */
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "toolbox_windows.h"
#include "toolbox_theme.h"
#include "toolbox_layout.h"   // tl:: 两列骨架（几何唯一来源，别在这里另写坐标）
#include "net_tools.h"
#include "tool_kbd.h"
#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/audio/audio.h>
#include <cstdio>
#include <cstring>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-tcp";

static constexpr int _RX_MAX_CHARS = 3000;

// 监听端口候选（右列设置行循环切换）
static const uint16_t _ports[4] = {8888, 8000, 9000, 12345};
static constexpr int  _port_num  = (int)(sizeof(_ports) / sizeof(_ports[0]));

// socket 真正 bind 成功的端口。监听期间改端口只影响「下一次开始监听」，
// 状态行必须显示这个值 —— 否则显示的端口和实际 socket 不是一个，等于骗人。
static volatile uint16_t _bound_port = 0;

// 报文区追加一行（自带换行与滚到底）。
// 不做成成员函数：TcpToolWindow 的成员表（net_tools.h）已定稿，这里只用公开的成员。
static void panelLine(TextArea* panel, const char* text)
{
    if (panel == nullptr || text == nullptr) {
        return;
    }
    const char* cur = lv_textarea_get_text(panel->get());
    if (cur != nullptr && cur[0] != '\0') {
        panel->addText("\n");
    }
    panel->addText(tl::stamp(text).c_str());
    lv_obj_scroll_to_y(panel->get(), LV_COORD_MAX, LV_ANIM_OFF);
}

TcpToolWindow::TcpToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, tl::W, tl::PageH, 255};
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
        _bound_port = 0;
        vTaskDelete(nullptr);
        return;
    }
    if (listen(listen_sock, 1) < 0) {
        mclog::tagError(_tag, "listen failed");
        lwip_close(listen_sock);
        self->_listen_sock = -1;
        self->_task_running = false;
        _bound_port = 0;
        vTaskDelete(nullptr);
        return;
    }
    _bound_port = self->_tcp_port;

    // 100ms 接收超时：既保证能及时看到停止标志，也让 accept 不会把任务永远卡住
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
                {
                    // 让界面知道有人连上了（状态行之外再留一行痕迹）
                    std::lock_guard<std::mutex> lock(self->_rx_mutex);
                    self->_rx_packets.push(tl::stamp("[客户端已连接]"));
                }
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
                self->_rx_packets.push(tl::stamp(packet));
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
                self->_rx_packets.push(tl::stamp("[客户端已断开]"));
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
    _bound_port = 0;
    vTaskDelete(nullptr);
}

void TcpToolWindow::onOpen()
{
    // 中文默认字体：子控件继承，避免未设字体的控件画成方框
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    mclog::tagInfo(_tag, "on open");

    // ---- 页面状态复位（重开一次就要回到确定的初始态，别沿用上次残留）----
    _listening        = false;
    _task_running     = false;
    _task_handle      = nullptr;
    _listen_sock      = -1;
    _client_sock      = -1;
    _client_connected = false;
    _rx_count         = 0;
    _port_idx         = 0;
    _tcp_port         = _ports[0];
    _bound_port       = 0;
    {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        while (!_rx_packets.empty()) {
            _rx_packets.pop();
        }
    }

    // ==================== 左列 ====================
    _rx_panel = tl::makePanel(_window->get());

    tl::SendRow send_row = tl::makeSendRow(_window->get(), "输入要发送的数据…");
    _tx_input            = std::move(send_row.input);
    _btn_send_tx         = std::move(send_row.button);
    _btn_send_tx->onClick().connect([&]() {
        audio::play_next_tone_progression();
        const char* txt = lv_textarea_get_text(_tx_input->get());
        if (txt == nullptr || txt[0] == '\0') {
            return;
        }
        sendToClient(txt);
        if (_client_sock >= 0) {
            lv_textarea_set_text(_tx_input->get(), "");   // 真发出去了才清空；没连上就留着，方便重发
        }
    });

    // ==================== 右列 ====================
    _status_label = tl::makeStatus(_window->get(), &_status_dot);

    // 设置行：监听端口（点一下换下一个候选值）
    _row_port = tl::makeRow(_window->get(), tl::rowY(0), "监听端口");
    _row_port->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _port_idx = (_port_idx + 1) % _port_num;
        _tcp_port = _ports[_port_idx];
        if (_listening) {
            // 已经 bind 的 socket 不会跟着改 —— 端口要停止后重新开始监听才生效
            refreshUi();
            panelLine(_rx_panel.get(), "端口已改（停止后重新开始监听才生效）");
        } else {
            refreshUi();
        }
    });

    // 纯展示行：客户端 / 收包（不做成按钮 —— 没有可点的动作就别装成能点）
    tl::makeInfoRow(_window->get(), tl::rowY(1), "客户端", "未连接", &_info_client);
    tl::makeInfoRow(_window->get(), tl::rowY(2), "收包", "0", &_info_rx);

    // 主操作：开始 / 停止监听
    _btn_run = tl::makePrimary(_window->get(), "开始监听");
    _btn_run->onClick().connect([&]() {
        audio::play_next_tone_progression();
        setListening(!_listening);
    });

    _btn_clear = tl::makeAction(_window->get(), 0, "清空", tb::neutral(), tb::text());
    _btn_clear->onClick().connect([&]() {
        audio::play_next_tone_progression();
        if (_rx_panel) {
            _rx_panel->setText("");
        }
    });

    _btn_close = tl::makeAction(_window->get(), 1, "关闭", tb::danger(), tb::text());
    _btn_close->onClick().connect([&]() {
        audio::play_next_tone_progression();
        close();
    });

    panelLine(_rx_panel.get(), "点「开始监听」起服务（端口见右列）");
    refreshUi();
    mclog::tagInfo(_tag, "layout ready");
}

void TcpToolWindow::onUpdate()
{
    if (_state != Opened) {
        return;
    }

    // 起监听失败（端口被占 / 网络没起来）：后台任务已自己退出，这里把状态收回来，
    // 否则状态行会一直显示「监听中」而其实什么都没在听。
    if (_listening && !_task_running && _listen_sock < 0) {
        _listening   = false;
        _task_handle = nullptr;
        panelLine(_rx_panel.get(), "监听启动失败（端口被占用或网络未就绪）");
        refreshUi();
    }

    // 收包队列 → 报文区
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
            _rx_panel->setText(cur + strlen(cur) - _RX_MAX_CHARS);   // 只留尾部
        }
        lv_obj_scroll_to_y(_rx_panel->get(), LV_COORD_MAX, LV_ANIM_OFF);
        refreshUi();     // 有新数据：收包数 / 连接状态一起刷
    }

    // 轻量节流的兜底刷新：客户端连上/断开这类没人主动通知的变化靠它同步
    static uint32_t tick = 0;
    if (++tick >= 5) {
        tick = 0;
        refreshUi();
    }
}

void TcpToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    _task_running = false;
    // accept / recv 都带 100ms 超时，最多等 1s 让任务自己把 socket 关干净再走
    for (int i = 0; i < 10 && _listen_sock >= 0; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    _listening  = false;
    _bound_port = 0;
}

void TcpToolWindow::sendToClient(const char* text)
{
    if (text == nullptr) {
        return;
    }
    if (_client_sock < 0) {
        // 没客户端就别装作发出去了
        std::string line = "[未连接客户端] ";
        line += text;
        panelLine(_rx_panel.get(), line.c_str());
        return;
    }
    int len = send(_client_sock, text, strlen(text), 0);
    if (len < 0) {
        mclog::tagError(_tag, "send failed");
        panelLine(_rx_panel.get(), "[发送失败]");
        return;
    }
    std::string echo = "[TX] ";
    echo += text;
    panelLine(_rx_panel.get(), echo.c_str());
}

// 开始 / 停止监听：真正创建、关闭监听 socket（原来 _btn_apply_port 那套逻辑挪到这儿）
void TcpToolWindow::setListening(bool on)
{
    if (on == _listening) {
        return;
    }

    if (on) {
        // 上一个任务没退干净就别再起一个 —— 两个任务抢同一个端口只会互相打架
        if (_listen_sock >= 0) {
            panelLine(_rx_panel.get(), "上一个监听任务还没退出，稍后再试");
            return;
        }
        _task_running = true;
        _bound_port   = 0;
        BaseType_t ok = xTaskCreate(tcpSrvTask, "tcp_srv", 4096, this, 5, (TaskHandle_t*)&_task_handle);
        if (ok != pdPASS) {
            _task_running = false;
            _task_handle  = nullptr;
            panelLine(_rx_panel.get(), "启动监听任务失败（内存不足）");
            return;
        }
        _listening = true;
        panelLine(_rx_panel.get(), "开始监听…");
        mclog::tagInfo(_tag, "listen requested on {}", _tcp_port);
    } else {
        _task_running = false;
        for (int i = 0; i < 10 && _listen_sock >= 0; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        _listening   = false;
        _task_handle = nullptr;   // 任务自己 vTaskDelete 了，句柄别再留着
        panelLine(_rx_panel.get(), (_listen_sock < 0) ? "已停止监听" : "停止监听超时");
        mclog::tagInfo(_tag, "listen stopped");
    }
    refreshUi();
}

void TcpToolWindow::refreshUi()
{
    if (!_btn_run) {
        return;   // 界面还没建好（onOpen 之前的 onUpdate）
    }

    // 主操作按钮：文字与配色跟着监听状态走
    if (_listening) {
        _btn_run->label().setText("停止监听");
        _btn_run->setBgColor(lv_color_hex(tb::danger()));
        _btn_run->setBorderColor(lv_color_hex(tb::danger()));
        _btn_run->label().setTextColor(lv_color_hex(tb::text()));
    } else {
        _btn_run->label().setText("开始监听");
        _btn_run->setBgColor(lv_color_hex(tb::raised()));
        _btn_run->setBorderColor(lv_color_hex(tb::accent()));
        _btn_run->label().setTextColor(lv_color_hex(tb::accent()));
    }

    char b[48];

    if (_row_port) {
        snprintf(b, sizeof(b), "%u", (unsigned)_tcp_port);
        _row_port->label().setText(b);
    }
    if (_info_client) {
        lv_label_set_text(_info_client, _client_connected ? "已连接" : "未连接");
    }
    if (_info_rx) {
        snprintf(b, sizeof(b), "%lu", (unsigned long)_rx_count);
        lv_label_set_text(_info_rx, b);
    }

    if (_status_label == nullptr) {
        return;
    }
    if (_listening && _client_connected) {
        _status_label->setText("客户端已连接");
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::success()), 0);
    } else if (_listening) {
        snprintf(b, sizeof(b), "监听中 :%u", (unsigned)(_bound_port ? _bound_port : _tcp_port));
        _status_label->setText(b);
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::warning()), 0);
    } else {
        _status_label->setText("已停止");
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::textDim()), 0);
    }
}

#endif  // CONFIG_IDF_TARGET_ESP32P4
