// Tab5 工具箱 — 网络类工具窗口（UDP / TCP / HTTP / MQTT）
//
// 四个页面统一走 toolbox_layout.h 的两列骨架：
//   左列 —— 报文区（+ 需要发送的页面带发送行）
//   右列 —— 状态 / 设置行 / 本机信息 / 主操作（开始·停止）/ 清空·关闭
//
// 命名约定：
//   _row_xxx   = 右列设置行（Button 循环值 或 TextArea 可编辑）
//   _info_xxx  = 右列纯展示值（lv_obj_t*，指向那条 Label）
//   _btn_run   = 主操作按钮（开始/停止监听、连接/断开）
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

// ============================== UDP ==============================
class UdpToolWindow : public ui::Window {
public:
    UdpToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    void sendText(const char* text);
    void setListening(bool on);
    void refreshUi();
    // bind 失败：屏上一行（带下一步）+ 日志一行（机器可读，供 /log 抓）
    void reportBindFail(int err);
    static void udpRxTask(void* arg);

    bool  _task_running = false;
    void* _task_handle  = nullptr;
    int   _sock         = -1;
    bool  _listening    = false;
    int   _bind_err     = 0;   // 最近一次 bind 失败的 errno（0 = 没失败过）

    std::string _dst_ip     = "255.255.255.255";
    uint16_t    _dst_port   = 8888;
    uint16_t    _local_port = 8888;
    int         _port_idx   = 0;

    std::queue<std::string> _rx_packets;
    std::mutex              _rx_mutex;
    uint32_t                _rx_count = 0;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label>    _status_label;
    lv_obj_t*                                              _status_dot  = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _row_dst_ip;    // 目标地址（可编辑）
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _row_dst_port;  // 目标端口（可编辑）
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _row_local;     // 本机端口（循环）
    lv_obj_t*                                              _info_rx     = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _tx_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_send_tx;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_run;      // 开始/停止监听
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_close;
};

// ============================== TCP ==============================
// 服务端：监听端口，接受 1 个客户端；收了能回，也能自定义发送
class TcpToolWindow : public ui::Window {
public:
    TcpToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    void sendToClient(const char* text);
    void setListening(bool on);
    void refreshUi();
    static void tcpSrvTask(void* arg);

    bool  _task_running = false;
    void* _task_handle  = nullptr;
    int   _listen_sock  = -1;
    int   _client_sock  = -1;
    bool  _listening    = false;
    uint16_t _tcp_port  = 8888;
    int   _port_idx     = 0;

    std::queue<std::string> _rx_packets;
    std::mutex              _rx_mutex;
    uint32_t                _rx_count = 0;
    bool                    _client_connected = false;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label>    _status_label;
    lv_obj_t*                                              _status_dot = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _row_port;      // 监听端口（循环）
    lv_obj_t*                                              _info_client = nullptr;
    lv_obj_t*                                              _info_rx     = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _tx_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_send_tx;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_run;      // 开始/停止监听
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_close;
};

// ============================== HTTP ==============================
// 服务端：起 httpd，记录进来的请求（方法 / URI / 头）
class HttpToolWindow : public ui::Window {
public:
    HttpToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    void pushRequest(const std::string& info);
    void setRunning(bool on);
    void refreshUi();
    static esp_err_t httpHandler(httpd_req_t* req);

    uint16_t _http_port = 8080;
    int      _port_idx  = 0;
    bool     _running   = false;
    void*    _server    = nullptr;

    std::queue<std::string> _rx_packets;
    std::mutex              _rx_mutex;
    uint32_t                _rx_count = 0;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label>    _status_label;
    lv_obj_t*                                              _status_dot = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _row_port;        // 监听端口（循环）
    lv_obj_t*                                              _info_ip     = nullptr;
    lv_obj_t*                                              _info_req    = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_run;        // 启动/停止
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_close;
};

// ============================== MQTT ==============================
// 客户端：连 broker、订阅主题、发布（发布主题可改，不再写死）
class MqttToolWindow : public ui::Window {
public:
    MqttToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

    static void mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id,
                                 void* event_data);

private:
    void pushEvent(const std::string& info);
    void setStatus(const char* s);
    void setConnected(bool on);
    void refreshUi();
    void publish(const char* payload);

    std::string _broker_uri = "mqtt://broker.emqx.io:1883";
    std::string _sub_topic  = "tab5/#";
    std::string _pub_topic  = "tab5/test";
    int         _qos        = 1;

    void* _client    = nullptr;
    bool  _connected = false;

    std::queue<std::string> _rx_packets;
    std::mutex              _rx_mutex;
    uint32_t                _msg_count = 0;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label>    _status_label;
    lv_obj_t*                                              _status_dot = nullptr;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _row_broker;    // 服务器（可编辑）
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _row_sub;       // 订阅主题（可编辑）
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _row_pub;       // 发布主题（可编辑）
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _row_qos;       // QoS（循环）
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _tx_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_send_tx;   // 发布
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_run;       // 连接/断开
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_close;
};

}  // namespace launcher_view

#endif  // CONFIG_IDF_TARGET_ESP32P4
