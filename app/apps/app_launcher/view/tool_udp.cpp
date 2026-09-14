/*
 * Nova Toolbox for M5Stack Tab5
 * UDP 调试工具
 *
 * 布局（与串口页同构，走 toolbox_layout.h 的骨架）：
 *   左列 —— 报文区 + 发送行
 *   右列 —— 状态 / 目标地址 / 目标端口 / 本机端口 / 收包数
 *           + 开始·停止监听 + 清空·关闭
 *
 * 后台任务只负责收包：socket + bind 在 setListening(true) 里**同步**做完 ——
 * bind 失败必须当场知道（按钮回「开始监听」、屏上给下一步），而不是先显示
 * 「监听中」再让后台任务悄悄死掉。errno → 文案走 tool_udp_bind_err.h 的查表。
 * 启动链三步（socket / bind / xTaskCreate）**任一步失败都留失败态**（_fail_kind），
 * 状态行只说哪一步挂了 —— 一句话概括：失败不许被显示成「已停止」。
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
#include "tool_udp_bind_err.h"   // bind 失败 errno → 文案（纯 C++，上位机自测共用同一份）
#include "net_tools.h"
#include "tool_kbd.h"
#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/audio/audio.h>
#include "esp_log.h"   // 机器可读日志通道：UDP_BIND_FAIL / UDP_BIND_OK 走串口（/log 可抓）
#include <cerrno>
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
// ESP_LOG 的 tag 得是编译期字符串（宏会反复展开），所以另留一份 const char*
static constexpr const char* _esp_tag = "tool-udp";

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

// bind 失败：一条给屏（回显 addr:port + 下一步），一条给日志（机器可读，/log 可抓）
void UdpToolWindow::reportBindFail(int err)
{
    char line[160];
    udp_bind_err::format_line(line, sizeof(line), err, "0.0.0.0", (unsigned)_local_port);
    if (_rx_panel) {
        _rx_panel->addText((tl::stamp(line) + "\n").c_str());
    }
    ESP_LOGE(_esp_tag, "UDP_BIND_FAIL errno=%d(%s) port=%u addr=0.0.0.0",
             err, udp_bind_err::name_of(err), (unsigned)_local_port);
}

void UdpToolWindow::udpRxTask(void* arg)
{
    UdpToolWindow* self = static_cast<UdpToolWindow*>(arg);

    // fd 已经在 setListening(true) 里建好并 bind 成功 —— 这里只管收，
    // 不再自己建 socket/bind（那样失败要跨线程再报一次，中途状态还会骗人）。
    const int sock = self->_sock;
    if (sock < 0) {
        ESP_LOGE(_esp_tag, "UDP_RX_NO_FD 收包任务拿不到 socket，直接退出");
        self->_task_running = false;
        vTaskDelete(nullptr);
        return;
    }

    ESP_LOGI(_esp_tag, "UDP_RECV_START fd=%d port=%u", sock, (unsigned)self->_local_port);

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
                self->_rx_packets.push(tl::stamp(packet));
                if (self->_rx_packets.size() > 50) {
                    self->_rx_packets.pop();
                }
            }
            self->_rx_count++;
        }
    }

    ESP_LOGI(_esp_tag, "UDP_RECV_STOP fd=%d port=%u（socket 一并关闭，不留半开）",
             sock, (unsigned)self->_local_port);
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
            const std::string ip = tl::textOf(_row_dst_ip.get());
            if (!ip.empty()) _dst_ip = ip;
        }
        if (_row_dst_port) {
            int p = atoi(tl::textOf(_row_dst_port.get()).c_str());
            if (p > 0 && p < 65536) _dst_port = (uint16_t)p;
        }

        // ---- 建 socket + bind：同步做完，失败当场回滚（不建收包任务）----
        int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (sock < 0) {
            const int e = errno;   // 先抓：后面的调用可能改写 errno
            char line[160];
            snprintf(line, sizeof(line), "[启动失败] 建不了 socket（errno=%d）→ 关掉别的工具页再试", e);
            if (_rx_panel) {
                _rx_panel->addText((tl::stamp(line) + "\n").c_str());
            }
            // 这一路连 bind 都没走到，但也**不是**「已停止」——失败态得留住，
            // 否则状态行又一次把失败说成没开始。
            _fail_kind = Step::Socket;
            _bind_err  = e;
            ESP_LOGE(_esp_tag, "UDP_SOCKET_FAIL errno=%d port=%u", e, (unsigned)_local_port);
            refreshUi();
            return;
        }

        int broadcast = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof(broadcast));

        struct sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family      = AF_INET;
        addr.sin_port        = htons(_local_port);
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
            const int e = errno;   // 【坑】必须在 close 之前抓：close 会把 errno 改掉
            lwip_close(sock);      // 不关就是 fd 泄漏，下次重试还会接着失败
            _sock         = -1;
            _task_running = false;
            _listening    = false;
            _fail_kind    = Step::Bind;
            _bind_err     = e;
            reportBindFail(e);     // 屏上一行 + 日志一行
            refreshUi();           // 状态行/按钮当场回「未监听」，不等下一次轮询
            return;                // 收包任务不建
        }

        struct timeval tv = {0, 100000};
        setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        _sock         = sock;
        _fail_kind    = Step::None;   // 起来了才算把上一次的失败清掉
        _bind_err     = 0;
        _listening    = true;
        _task_running = true;
        ESP_LOGI(_esp_tag, "UDP_BIND_OK port=%u addr=0.0.0.0 fd=%d", (unsigned)_local_port, sock);

        if (xTaskCreate(udpRxTask, "udp_rx", 4096, this, 5, (TaskHandle_t*)&_task_handle) != pdPASS) {
            // 任务起不来：fd 得自己收，否则端口一直被自己占着
            _task_running = false;
            _listening    = false;
            lwip_close(sock);
            _sock = -1;
            // 状态一样不能落回「已停止」：socket 建了、bind 也成功了，是**起任务**这步挂的。
            // errno 这里没有意义（xTaskCreate 不置 errno），真实返回值是 pdFAIL ——
            // IDF 里 pdFAIL 只在一处产生：pvPortMalloc 拿不到 TCB / 栈
            //（esp_additions/freertos_tasks_c_additions.h L273-276 errCOULD_NOT_ALLOCATE_REQUIRED_MEMORY），
            // 所以记 ENOMEM = 归类「内存不足」，日志里同时保留原始事实 pdFAIL，不假报 errno。
            _fail_kind = Step::Task;
            _bind_err  = ENOMEM;
            char line[160];
            snprintf(line, sizeof(line),
                     "[启动失败] 收包任务起不来（内存不足）:%u → 关掉别的工具页再试",
                     (unsigned)_local_port);
            if (_rx_panel) {
                _rx_panel->addText((tl::stamp(line) + "\n").c_str());
            }
            ESP_LOGE(_esp_tag, "UDP_TASK_FAIL port=%u ret=pdFAIL(heap) errno=ENOMEM",
                     (unsigned)_local_port);
        } else {
            char line[96];
            snprintf(line, sizeof(line), "[开始监听] 0.0.0.0:%u（本机全部网卡）", (unsigned)_local_port);
            _rx_panel->addText((tl::stamp(line) + "\n").c_str());
        }
    } else {
        _task_running = false;
        vTaskDelay(pdMS_TO_TICKS(300));   // 等收包任务的 recvfrom 超时退出
        _rx_panel->addText((tl::stamp("[停止监听]") + "\n").c_str());
    }
    refreshUi();
}

void UdpToolWindow::refreshUi()
{
    if (!_status_label) {
        return;
    }
    const bool live   = _listening && _sock >= 0;
    const bool failed = (_fail_kind != Step::None);

    // 文案出自 udp_bind_err::status_line()（与上位机自测同一份实现，别在这儿另写一份）：
    // 失败是独立状态，只说「已停止」等于把失败藏起来，用户会以为没点着；
    // 而且停在哪一步就说哪一步 —— bind 失败和 socket/起任务失败不是一回事。
    char b[80];
    udp_bind_err::status_line(b, sizeof(b), live, _fail_kind, (unsigned)_local_port);
    _status_label->setText(b);
    if (_status_dot) {
        const uint32_t dot = live ? tb::success() : (failed ? tb::danger() : tb::textDim());
        lv_obj_set_style_bg_color(_status_dot, lv_color_hex(dot), 0);
    }
    if (_btn_run) {
        // 失败后按钮回「开始监听」——点一下就是原地重试，不用重启设备
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
    _fail_kind    = Step::None;
    _bind_err     = 0;
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

    // 标签与输入框各占一行（输入框吃满整行宽度，长 IP 不用挤）
    int ry = tl::rowY(0);
    _row_dst_ip   = tl::makeRowInputTall(_window->get(), ry, "目标地址", _dst_ip.c_str());
    ry = tl::nextY(ry, tl::RowHTall);
    _row_dst_port = tl::makeRowInputTall(_window->get(), ry, "目标端口", "8888");
    ry = tl::nextY(ry, tl::RowHTall);

    _row_local = tl::makeRow(_window->get(), ry, "本机端口");
    ry = tl::nextY(ry, tl::RowH);
    _row_local->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _port_idx   = (_port_idx + 1) % 4;
        _local_port = _local_ports[_port_idx];
        _bind_err   = 0;   // 换了端口，上一次的失败提示不再适用（否则会显示"绑定失败 :新端口"）
        _fail_kind  = Step::None;
        if (_listening || _task_running) {
            // 端口改了得重新绑：先停，再确保状态复位后重新开始
            setListening(false);
            _listening = false;
            setListening(true);
        } else {
            refreshUi();
        }
    });

    tl::makeInfoRow(_window->get(), ry, "收包数", "0", &_info_rx);

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
        _rx_panel->addText((tl::stamp("[未开始监听] ") + text + "\n").c_str());
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
        _rx_panel->addText((tl::stamp("[发送失败]") + "\n").c_str());
        return;
    }

    char echo[192];
    snprintf(echo, sizeof(echo), "[TX → %s:%u] %s\n", _dst_ip.c_str(), (unsigned)_dst_port, text);
    _rx_panel->addText(tl::stamp(echo).c_str());
}

#endif  // CONFIG_IDF_TARGET_ESP32P4
