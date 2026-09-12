/*
 * Nova Toolbox for M5Stack Tab5
 * HTTP 调试工具：Tab5 起 httpd 服务，实时显示收到的请求
 *
 * 布局（与串口页同构，几何全部走 toolbox_layout.h 的骨架）：
 *   左列 —— 报文区（HTTP 是纯服务端、没有「发送」动作 → 加高版面板，不带发送行）
 *   右列 —— 状态 / 监听端口 / 本机地址 / 请求数
 *           + 启动·停止服务 + 清空·关闭
 *
 * 服务不由页面打开自动拉起：由「启动服务」真正 httpd_start，停止时 httpd_stop；
 * 改动监听端口 = 先停止再启动（端口只在 httpd_start 时生效）。
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
#include <string>

#include "esp_http_server.h"

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-http";

static constexpr int _RX_MAX_CHARS = 3000;   // 报文区最多留多少字符（超出裁掉最旧的）
static constexpr int _BODY_MAX     = 256;    // 请求体最多回显多少字节（大 body 不往内存里灌）

// 监听端口候选：点一下换下一个
static const uint16_t _http_ports[4] = {8080, 8000, 9000, 12345};

HttpToolWindow::HttpToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, tl::W, tl::PageH, 255};
    config.bgColor  = tb::bg();
}

esp_err_t HttpToolWindow::httpHandler(httpd_req_t* req)
{
    HttpToolWindow* self = static_cast<HttpToolWindow*>(req->user_ctx);

    const char* method = "?";
    switch (req->method) {
    case HTTP_GET: method = "GET"; break;
    case HTTP_POST: method = "POST"; break;
    case HTTP_PUT: method = "PUT"; break;
    case HTTP_DELETE: method = "DELETE"; break;
    default: break;
    }

    std::string info;
    info += "[";
    info += method;
    info += "] ";
    info += req->uri;

    // 尝试读 query string
    char query[128];
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        info += "?";
        info += query;
    }

    // 常见请求头（前 3 个关键头做展示）
    char hdr[128];
    static const char* want_headers[] = {"Host", "Content-Type", "User-Agent"};
    for (const char* h : want_headers) {
        if (httpd_req_get_hdr_value_str(req, h, hdr, sizeof(hdr)) == ESP_OK) {
            info += "\n  ";
            info += h;
            info += ": ";
            info += hdr;
        }
    }

    // 请求体：只在长度可控时才读（超大 body 只报字节数，不读进内存；
    // 栈上最多 _BODY_MAX 字节的临时缓冲，读完即丢）
    int body_len = -1;
    char cl[16];
    if (httpd_req_get_hdr_value_str(req, "Content-Length", cl, sizeof(cl)) == ESP_OK) {
        body_len = atoi(cl);
    }
    if (body_len > 0 && body_len <= _BODY_MAX) {
        char body[_BODY_MAX + 1];
        const int got = httpd_req_recv(req, body, body_len);
        if (got > 0) {
            // 控制字符会打乱报文区的排版：换行/制表压成空格，其余不可见字符点掉
            for (int i = 0; i < got; i++) {
                const unsigned char c = (unsigned char)body[i];
                if (c == '\n' || c == '\r' || c == '\t') {
                    body[i] = ' ';
                } else if (c < 0x20 || c == 0x7F) {
                    body[i] = '.';
                }
            }
            body[got] = '\0';
            info += "\n  Body: ";
            info += body;
        } else {
            info += "\n  Body: (读取失败)";
        }
    } else if (body_len > _BODY_MAX) {
        char nb[64];
        snprintf(nb, sizeof(nb), "\n  Body: %d 字节（过大，未显示）", body_len);
        info += nb;
    }

    self->pushRequest(info);

    // 简单响应
    const char* resp = "<html><body><h1>Tab5 HTTP Tool</h1><p>Request captured.</p></body></html>";
    httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

void HttpToolWindow::pushRequest(const std::string& info)
{
    {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        _rx_packets.push(info);
        if (_rx_packets.size() > 50) {
            _rx_packets.pop();
        }
    }
    _rx_count++;
}

// ---------------- 启 / 停 ----------------
// 端口改成新的才需要重启：httpd_start 时就绑定了端口
void HttpToolWindow::setRunning(bool on)
{
    if (on == _running) {
        return;
    }

    if (on) {
        httpd_config_t cfg    = HTTPD_DEFAULT_CONFIG();
        cfg.server_port       = _http_port;
        cfg.lru_purge_enable  = true;
        cfg.stack_size        = 8192;

        httpd_handle_t server = nullptr;
        if (httpd_start(&server, &cfg) != ESP_OK) {
            mclog::tagError(_tag, "http server start failed");
            _server  = nullptr;
            _running = false;
            // 失败原因（端口被占用等）直接甩到报文区，别让按钮变成没反应的死按钮
            char m[80];
            snprintf(m, sizeof(m), "· 端口 %u 启动失败（端口被占用？）\n", (unsigned)_http_port);
            if (_rx_panel) {
                _rx_panel->addText(m);
            }
            refreshUi();
            return;
        }
        _server = (void*)server;

        httpd_uri_t uri_get = {
            .uri       = "/*",
            .method    = HTTP_GET,
            .handler   = httpHandler,
            .user_ctx  = this,
        };
        httpd_uri_t uri_post = {
            .uri       = "/*",
            .method    = HTTP_POST,
            .handler   = httpHandler,
            .user_ctx  = this,
        };
        httpd_uri_t uri_put = {
            .uri       = "/*",
            .method    = HTTP_PUT,
            .handler   = httpHandler,
            .user_ctx  = this,
        };
        httpd_uri_t uri_del = {
            .uri       = "/*",
            .method    = HTTP_DELETE,
            .handler   = httpHandler,
            .user_ctx  = this,
        };
        httpd_register_uri_handler(server, &uri_get);
        httpd_register_uri_handler(server, &uri_post);
        httpd_register_uri_handler(server, &uri_put);
        httpd_register_uri_handler(server, &uri_del);

        _running = true;
        mclog::tagInfo(_tag, "http server started");
    } else {
        if (_server) {
            httpd_stop((httpd_handle_t)_server);
            _server = nullptr;
        }
        _running = false;
        mclog::tagInfo(_tag, "http server stopped");
    }

    refreshUi();
}

void HttpToolWindow::onOpen()
{
    // 中文默认字体：子控件继承，避免未设字体的控件画成方框
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    mclog::tagInfo(_tag, "on open");

    _rx_count = 0;
    _running  = false;
    _server   = nullptr;
    _port_idx = 0;
    _http_port = _http_ports[0];
    {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        while (!_rx_packets.empty()) {
            _rx_packets.pop();
        }
    }

    // ==================== 左列 ====================
    // 报文区：HTTP 没有发送动作，直接用加高版面（一直铺到底部按钮上沿）
    _rx_panel = tl::makePanel(_window->get(), tl::MsgHNoTx);
    _rx_panel->setMaxLength(4096);
    _rx_panel->setText("等待 HTTP 请求…\n");

    // ==================== 右列 ====================
    // 状态：圆点 + 文字（圆点是画出来的，不依赖字体字形）
    _status_label = tl::makeStatus(_window->get(), &_status_dot);

    // 监听端口（整行可点，循环换值）
    _row_port = tl::makeRow(_window->get(), tl::rowY(0), "监听端口");
    _row_port->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _port_idx = (_port_idx + 1) % (int)(sizeof(_http_ports) / sizeof(_http_ports[0]));
        _http_port = _http_ports[_port_idx];
        if (_running) {
            setRunning(false);   // 端口只在 httpd_start 时生效 → 先停
            setRunning(true);    // 再按新端口起
        } else {
            refreshUi();
        }
    });

    // 纯展示行：不可点（不做"看着能按其实没用"的死按钮）
    tl::makeInfoRow(_window->get(), tl::rowY(1), "本机地址", "--", &_info_ip);
    tl::makeInfoRow(_window->get(), tl::rowY(2), "请求数", "0", &_info_req);
    if (_info_ip) {
        // http://192.168.x.x:12345 比行宽长：限定宽度 + 超出省略，别越出卡片
        lv_obj_set_width(_info_ip, tl::RightW - 96);
        lv_label_set_long_mode(_info_ip, LV_LABEL_LONG_DOT);
    }

    // 主操作：启动 / 停止服务
    _btn_run = tl::makePrimary(_window->get(), "启动服务");
    _btn_run->onClick().connect([&]() {
        audio::play_next_tone_progression();
        setRunning(!_running);
    });

    // 清空 / 关闭
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

    refreshUi();
    mclog::tagInfo(_tag, "layout ready");
}

// ---------------- 状态 / 按钮文字 ----------------
void HttpToolWindow::refreshUi()
{
    if (!_btn_run || !_status_label) {
        return;
    }

    char b[64];

    // 监听端口行
    snprintf(b, sizeof(b), "%u", (unsigned)_http_port);
    _row_port->label().setText(b);

    // 主操作按钮 + 状态行
    if (_running) {
        _btn_run->label().setText("停止服务");
        _btn_run->setBgColor(lv_color_hex(tb::danger()));
        _btn_run->setBorderColor(lv_color_hex(tb::danger()));
        _btn_run->label().setTextColor(lv_color_hex(tb::text()));

        snprintf(b, sizeof(b), "运行中 :%u", (unsigned)_http_port);
        _status_label->setText(b);
        _status_label->setTextColor(lv_color_hex(tb::success()));
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::success()), 0);
    } else {
        _btn_run->label().setText("启动服务");
        _btn_run->setBgColor(lv_color_hex(tb::raised()));
        _btn_run->setBorderColor(lv_color_hex(tb::accent()));
        _btn_run->label().setTextColor(lv_color_hex(tb::accent()));

        _status_label->setText("已停止");
        _status_label->setTextColor(lv_color_hex(tb::textDim()));
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::textDim()), 0);
    }

    // 本机地址：http://<设备IP>:<端口>；没联网就 --
    if (_info_ip) {
        std::string ip = GetHAL()->wifiIsStaConnected() ? GetHAL()->wifiGetStaIp() : std::string();
        if (!ip.empty() && ip != "-") {
            snprintf(b, sizeof(b), "http://%s:%u", ip.c_str(), (unsigned)_http_port);
            lv_label_set_text(_info_ip, b);
        } else {
            lv_label_set_text(_info_ip, "--");
        }
    }

    // 请求数
    if (_info_req) {
        snprintf(b, sizeof(b), "%lu", (unsigned long)_rx_count);
        lv_label_set_text(_info_req, b);
    }
}

void HttpToolWindow::onUpdate()
{
    if (_state != Opened || !_rx_panel) {
        return;
    }

    std::string batch;
    {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        while (!_rx_packets.empty()) {
            batch += _rx_packets.front();
            batch += "\n---\n";
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
        lv_obj_scroll_to_y(_rx_panel->get(), LV_COORD_MAX, LV_ANIM_OFF);
        refreshUi();   // 请求数 / 本机地址跟着请求一起更新
    }

    // 联网 / 断网后 IP 会变，隔一会儿对一次（约 2s 一次）
    static uint32_t tick = 0;
    if (++tick % 20 == 0) {
        refreshUi();
    }
}

void HttpToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    if (_server) {
        httpd_stop((httpd_handle_t)_server);
        _server = nullptr;
    }
    _running = false;
}

#endif  // CONFIG_IDF_TARGET_ESP32P4
