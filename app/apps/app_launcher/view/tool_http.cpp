/*
 * Nova Toolbox for M5Stack Tab5
 * HTTP 调试工具：Tab5 起 HTTP server :80，实时显示收到的请求
 * 配合 WiFi AP/STA 使用：浏览器/设备访问 Tab5 的 IP，请求细节上屏
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

#include "esp_http_server.h"

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-http";

static constexpr int _RX_MAX_CHARS = 3000;

HttpToolWindow::HttpToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, 1180, 622, 255};
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

void HttpToolWindow::onOpen()
{
    // 中文默认字体：子控件继承，避免未设字体的控件画成方框
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    mclog::tagInfo(_tag, "on open");
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _rx_count = 0;

    _title_label = std::make_unique<Label>(_window->get());
    _title_label->align(LV_ALIGN_TOP_LEFT, 24, 12);
    _title_label->setText("网页抓包");
    _title_label->setTextFont(tb::fontTitle());
    _title_label->setTextColor(lv_color_hex(tb::text()));

    _status_label = std::make_unique<Label>(_window->get());
    _status_label->align(LV_ALIGN_TOP_RIGHT, -24, 16);
    _status_label->setText("Listen :8080");
    _status_label->setTextFont(tb::fontBody());
    _status_label->setTextColor(lv_color_hex(tb::textDim()));

    // ---- 参数配置行 ----
    auto cfgl = std::make_unique<Label>(_window->get());
    cfgl->align(LV_ALIGN_TOP_LEFT, 24, 64);
    cfgl->setText("端口");
    cfgl->setTextFont(tb::fontBody());
    cfgl->setTextColor(lv_color_hex(tb::textDim()));

    _port_input = std::make_unique<TextArea>(_window->get());
    _port_input->setSize(160, 48);
    _port_input->align(LV_ALIGN_TOP_LEFT, 112, 56);
    _port_input->setOneLine(true);
    _port_input->setBorderWidth(1);
    _port_input->setBorderColor(lv_color_hex(tb::border()));
    _port_input->setBgColor(lv_color_hex(tb::surface()));
    _port_input->setTextFont(tb::fontBody());
    _port_input->setTextColor(lv_color_hex(tb::text()));
    _port_input->setText("8080");
    tool_kbd::attach(_port_input->get());

    _btn_apply_port = std::make_unique<Button>(_window->get());
    _btn_apply_port->setSize(152, 48);
    _btn_apply_port->align(LV_ALIGN_TOP_LEFT, 292, 56);
    _btn_apply_port->setBgColor(lv_color_hex(tb::raised()));
    _btn_apply_port->setRadius(tb::RadiusMd);
    _btn_apply_port->label().setTextFont(tb::fontBody());
    _btn_apply_port->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_apply_port->label().setText("Connect");
    _btn_apply_port->onClick().connect([&]() {
        audio::play_next_tone_progression();
        int p = atoi(lv_textarea_get_text(_port_input->get()));
        if (p > 0 && p < 65536) {
            _http_port = (uint16_t)p;
            char buf[80];
            snprintf(buf, sizeof(buf), "监听 :%u（重启生效）", (unsigned)_http_port);
            _status_label->setText(buf);
        }
    });

    _rx_panel = std::make_unique<TextArea>(_window->get());
    _rx_panel->setSize(1132, 332);
    _rx_panel->align(LV_ALIGN_TOP_LEFT, 24, 112);
    _rx_panel->setMaxLength(4096);
    _rx_panel->setCursorClickPos(false);
    _rx_panel->setText("Waiting for HTTP requests...\n");
    _rx_panel->setPasswordMode(false);
    _rx_panel->setOneLine(false);
    _rx_panel->setBorderWidth(1);
    _rx_panel->setBorderColor(lv_color_hex(tb::border()));
    _rx_panel->setBgColor(lv_color_hex(tb::surface()));
    _rx_panel->setTextFont(tb::fontBody());
    _rx_panel->setTextColor(lv_color_hex(tb::text()));
    _rx_panel->setScrollbarMode(LV_SCROLLBAR_MODE_AUTO);

    _btn_clear = std::make_unique<Button>(_window->get());
    _btn_clear->setSize(208, 56);
    _btn_clear->align(LV_ALIGN_BOTTOM_LEFT, 24, -18);
    _btn_clear->setBgColor(lv_color_hex(tb::danger()));
    _btn_clear->setRadius(tb::RadiusMd);
    _btn_clear->label().setTextFont(tb::fontBody());
    _btn_clear->label().setText("Clear");
    _btn_clear->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _rx_panel->setText("");
    });

    _btn_close = std::make_unique<Button>(_window->get());
    _btn_close->setSize(208, 56);
    _btn_close->align(LV_ALIGN_BOTTOM_RIGHT, -24, -18);
    _btn_close->setBgColor(lv_color_hex(tb::danger()));
    _btn_close->setRadius(tb::RadiusMd);
    _btn_close->label().setTextFont(tb::fontBody());
    _btn_close->label().setText("Close");
    _btn_close->onClick().connect([&]() {
        audio::play_next_tone_progression();
        close();
    });

    // 启动 HTTP server（8080 避开官方 wifi AP 自带的 :80 hello 页）
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port     = _http_port;
    cfg.lru_purge_enable = true;
    cfg.stack_size      = 8192;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &cfg) == ESP_OK) {
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
        mclog::tagInfo(_tag, "http server started :80");
    } else {
        mclog::tagError(_tag, "http server start failed");
        _status_label->setText(":80 FAILED");
    }
}

void HttpToolWindow::onUpdate()
{
    if (_state != Opened) {
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
    }
}

void HttpToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    if (_server) {
        httpd_stop((httpd_handle_t)_server);
        _server = nullptr;
    }

}

#endif  // CONFIG_IDF_TARGET_ESP32P4