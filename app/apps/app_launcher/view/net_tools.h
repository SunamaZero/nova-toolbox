/*
 * Nova Toolbox for M5Stack Tab5
 * 网络工具窗口声明（仅设备端 IDF 编译；desktop 模拟构建不含）
 * UDP/TCP/HTTP/MQTT/WiFi 工具类
 */
#pragma once

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include <cstdint>
#include <memory>
#include <string>
#include <queue>
#include <mutex>
#include "esp_http_server.h"
#include "mqtt_client.h"
#include <lvgl.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/ui/window.h>

namespace launcher_view {

/**
 * @brief UDP 调试工具（广播/回环收发测试）
 */
class UdpToolWindow : public ui::Window {
public:
    UdpToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    void sendText(const char* text);
    static void udpRxTask(void* arg);

    bool _task_running = false;
    void* _task_handle = nullptr;
    int _sock          = -1;

    // 可配置参数
    std::string _dst_ip   = "255.255.255.255";
    uint16_t _dst_port    = 8888;
    std::string _getDstIp() const { return _dst_ip; }
    uint16_t _getDstPort() const { return _dst_port; }

    std::queue<std::string> _rx_packets;
    std::mutex _rx_mutex;
    uint32_t _rx_count = 0;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _dst_ip_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _dst_port_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_apply;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _tx_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_hello;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_probe;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_send_tx;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_loopback;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_close;
};

/**
 * @brief TCP Server 调试工具：监听 8888
 */
class TcpToolWindow : public ui::Window {
public:
    TcpToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    void sendToClient(const char* text);
    static void tcpSrvTask(void* arg);

    bool _task_running = false;
    void* _task_handle = nullptr;
    int _listen_sock   = -1;
    int _client_sock   = -1;
    uint16_t _tcp_port = 8888;  // 可配置监听端口
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _port_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_apply_port;

    std::queue<std::string> _rx_packets;
    std::mutex _rx_mutex;
    uint32_t _rx_count = 0;
    bool _client_connected = false;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_echo;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_hello;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_probe;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_close;
};

/**
 * @brief HTTP 调试工具：HTTP server :8080 抓请求
 */
class HttpToolWindow : public ui::Window {
public:
    HttpToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    uint16_t _http_port = 8080;  // 可配置监听端口
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _port_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_apply_port;

    void pushRequest(const std::string& info);
    static esp_err_t httpHandler(httpd_req_t* req);
    void* _server = nullptr;

    std::queue<std::string> _rx_packets;
    std::mutex _rx_mutex;
    uint32_t _rx_count = 0;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_close;
};

/**
 * @brief MQTT 测试工具：预设 broker + 订阅/发布
 */
class MqttToolWindow : public ui::Window {
public:
    MqttToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

    static void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id,
                                 void* event_data);

private:
    std::string _broker_uri = "mqtt://broker.emqx.io:1883";
    std::string _sub_topic  = "tab5/#";
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _broker_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _topic_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_apply_cfg;

    void pushEvent(const std::string& info);
    void setStatus(const char* s);

    void* _client   = nullptr;
    bool _connected = false;

    std::queue<std::string> _rx_packets;
    std::mutex _rx_mutex;
    uint32_t _msg_count = 0;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _tx_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_connect;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_sub;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_pub;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_pub_custom;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_close;
};

}  // namespace launcher_view

#endif  // CONFIG_IDF_TARGET_ESP32P4
