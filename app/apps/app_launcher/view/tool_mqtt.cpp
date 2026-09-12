/*
 * Nova Toolbox for M5Stack Tab5
 * MQTT 测试工具：连 broker、订阅主题、发布
 *
 * 布局（与串口页同构 —— 坐标全部来自 toolbox_layout.h，本文件不写死任何 x/y）：
 *   左列 —— 报文区 + 发布输入框 + 发布按钮
 *   右列 —— 状态 / 服务器 / 订阅 / 发布主题 / QoS / 消息数
 *           + 连接·断开 + 清空·关闭
 *
 * 与 v1 的区别：
 *   1. 发布主题不再写死：_row_pub 可编辑，发布时取界面当前值
 *   2. 写死载荷的演示按钮（Apply cfg / Subscribe / Publish 固定串）全部删除，
 *      能力并入「设置行 + 发送行」，没有"看着能按其实没用"的死按钮
 *   3. 未连接时不假装发出去了 —— 报文区如实写一条「[未连接] xxx」
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
#include <cstring>

#include "mqtt_client.h"

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-mqtt";

// 界面初始值（成员为空时兜底）。改这里就能改默认 broker / 主题。
static constexpr const char* _MQTT_URI_INIT = "mqtt://broker.emqx.io:1883";
// 界面里显示用（右列输入框只有 184px，带 scheme 的完整 URI 会被截断）
static constexpr const char* _MQTT_HOST_INIT = "broker.emqx.io";
static constexpr const char* _MQTT_SUB_INIT = "tab5/#";
static constexpr const char* _MQTT_PUB_INIT = "tab5/test";

static constexpr int _RX_MAX_CHARS = 3000;

namespace {

// 从 mqtt://broker.emqx.io:1883 里抠出 broker.emqx.io —— 状态行只有 242px，
// 带 scheme/端口的全串会被 LV_LABEL_LONG_DOT 截成 "已连接 mqtt://broker.e…"
std::string host_of(const std::string& uri)
{
    std::string s = uri;
    size_t p = s.find("://");
    if (p != std::string::npos) {
        s = s.substr(p + 3);
    }
    p = s.find('@');           // 去掉 user:pass@
    if (p != std::string::npos) {
        s = s.substr(p + 1);
    }
    p = s.find_first_of(":/");  // 去掉 :port 和 /path
    if (p != std::string::npos) {
        s = s.substr(0, p);
    }
    return s;
}

}  // namespace

MqttToolWindow::MqttToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, tl::W, tl::PageH, 255};
    config.bgColor  = tb::bg();
}

void MqttToolWindow::pushEvent(const std::string& info)
{
    // 收下一条订阅消息就算一条（DATA 事件的格式是 "\n[sub <topic>] <data>"）
    if (info.compare(0, 5, "\n[sub ") == 0) {
        _msg_count++;
    }
    // 时间戳务必加在 _msg_count 判断之后：前缀会顶掉行首，compare(0,5) 就废了
    std::lock_guard<std::mutex> lock(_rx_mutex);
    _rx_packets.push(tl::stamp(info));
    if (_rx_packets.size() > 50) {
        _rx_packets.pop();
    }
}

void MqttToolWindow::setStatus(const char* s)
{
    if (_status_label && s) {
        _status_label->setText(s);
    }
}

// ---- esp-mqtt 事件回调（跑在 MQTT 任务上下文）----
// 这里只做两件事：压队列 + 记连接标志。界面刷新一律留给 LVGL 线程的
// onUpdate / refreshUi —— 在回调里碰 lv_obj 是跨线程写，迟早崩。
void MqttToolWindow::mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id,
                                      void* event_data)
{
    MqttToolWindow* self = static_cast<MqttToolWindow*>(handler_args);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        self->_connected = true;   // 事件推动状态行 / 主操作按钮
        self->pushEvent("\n[broker connected]");
        // 连接后自动订阅
        esp_mqtt_client_subscribe((esp_mqtt_client_handle_t)self->_client, self->_sub_topic.c_str(), 1);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
        self->_connected = false;
        self->pushEvent("\n[broker disconnected]");
        break;
    case MQTT_EVENT_DATA: {
        std::string msg;
        msg += "\n[sub ";
        msg.append(event->topic, event->topic_len);
        msg += "] ";
        msg.append(event->data, event->data_len);
        self->pushEvent(msg);
        break;
    }
    case MQTT_EVENT_ERROR:
        self->pushEvent("\n[mqtt error]");
        break;
    default:
        break;
    }
}

void MqttToolWindow::onOpen()
{
    // 中文默认字体：子控件继承，避免未设字体的控件画成方框
    lv_obj_set_style_text_font(_window->get(), tb::fontBody(), 0);
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    mclog::tagInfo(_tag, "on open");

    _msg_count = 0;
    // 丢掉上次留下的旧事件，免得一开页就涌出一堆历史
    {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        while (!_rx_packets.empty()) {
            _rx_packets.pop();
        }
    }

    // ==================== 左列 ====================
    // 报文区（只读显示，不挂软键盘）
    _rx_panel = tl::makePanel(_window->get());

    // 发送行：输入框 + 发布按钮
    tl::SendRow send       = tl::makeSendRow(_window->get(), "发布内容…");
    _tx_input              = std::move(send.input);
    _btn_send_tx           = std::move(send.button);
    _btn_send_tx->label().setText("发布");
    _btn_send_tx->onClick().connect([&]() {
        audio::play_next_tone_progression();
        const char* txt = lv_textarea_get_text(_tx_input->get());
        if (txt && txt[0]) {
            publish(txt);
            lv_textarea_set_text(_tx_input->get(), "");
        }
    });

    // ==================== 右列 ====================
    // 状态：圆点 + 文字（圆点是画出来的，不依赖字体字形）
    _status_label = tl::makeStatus(_window->get(), &_status_dot);
    _status_label->setText("未连接");

    // 设置行 0/1/2：可编辑输入（服务器 / 订阅 / 发布主题），值要手打就用这三个
    // 界面显示短地址；完整 URI（补 scheme）在连接时拼
    const std::string uri0 = _broker_uri.empty() ? _MQTT_HOST_INIT : _broker_uri;
    const std::string sub0 = _sub_topic.empty() ? _MQTT_SUB_INIT : _sub_topic;
    const std::string pub0 = _pub_topic.empty() ? _MQTT_PUB_INIT : _pub_topic;
    // 三个都是长值（host:port / topic），标签与输入框各占一行
    int ry = tl::rowY(0);
    _row_broker = tl::makeRowInputTall(_window->get(), ry, "服务器", uri0.c_str());
    ry = tl::nextY(ry, tl::RowHTall);
    _row_sub    = tl::makeRowInputTall(_window->get(), ry, "订阅", sub0.c_str());
    ry = tl::nextY(ry, tl::RowHTall);
    _row_pub    = tl::makeRowInputTall(_window->get(), ry, "发布主题", pub0.c_str());
    ry = tl::nextY(ry, tl::RowHTall);

    // 设置行 3：QoS（0/1/2 循环）
    _row_qos = tl::makeRow(_window->get(), ry, "QoS");
    _row_qos->label().setText("1");
    _row_qos->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _qos = (_qos + 1) % 3;
        refreshUi();
    });

    // 设置行 4：消息数（纯展示，不可点 —— 不做假的"看着能按"）
    // 「消息数」不单开一行（行高变大后会和 QoS 撞），已并进状态行显示

    // 主操作：连接 / 断开
    _btn_run = tl::makePrimary(_window->get(), "连接");
    _btn_run->onClick().connect([&]() {
        audio::play_next_tone_progression();
        setConnected(_client == nullptr);
    });

    // 清空 / 关闭
    _btn_clear = tl::makeAction(_window->get(), 0, "清空", tb::neutral(), tb::text());
    _btn_clear->onClick().connect([&]() {
        audio::play_next_tone_progression();
        _rx_panel->setText("");
        _msg_count = 0;
        refreshUi();
    });

    _btn_close = tl::makeAction(_window->get(), 1, "关闭", tb::danger(), tb::text());
    _btn_close->onClick().connect([&]() {
        audio::play_next_tone_progression();
        close();
    });

    refreshUi();
    mclog::tagInfo(_tag, "layout ready");
}

// 连接 / 断开（主操作按钮的全部逻辑，原来散在按钮回调里的挪到这里）
void MqttToolWindow::setConnected(bool on)
{
    if (!on) {
        if (_client) {
            esp_mqtt_client_disconnect((esp_mqtt_client_handle_t)_client);
            esp_mqtt_client_destroy((esp_mqtt_client_handle_t)_client);
            _client = nullptr;
        }
        _connected = false;
        pushEvent("[已断开]");
        refreshUi();
        mclog::tagInfo(_tag, "client stopped");
        return;
    }

    if (_client) {
        return;   // 已经有一个 client 在跑，不重入
    }

    // 每次启动都重新读界面值：停下后改了服务器/主题，再启动就该用新值
    const std::string bs = tl::textOf(_row_broker.get());
    const std::string ss = tl::textOf(_row_sub.get());
    const std::string ps = tl::textOf(_row_pub.get());
    const char* bv = bs.c_str();
    const char* sv = ss.c_str();
    const char* pv = ps.c_str();
    {
        std::string v = (bv && bv[0]) ? bv : _MQTT_HOST_INIT;
        // 没写 scheme 就补上 mqtt://（用户只需填 host 或 host:port）
        if (v.find("://") == std::string::npos) {
            v = "mqtt://" + v;
        }
        _broker_uri = v;
    }
    _sub_topic  = (sv && sv[0]) ? sv : _MQTT_SUB_INIT;
    _pub_topic  = (pv && pv[0]) ? pv : _MQTT_PUB_INIT;

    esp_mqtt_client_config_t cfg = {};
    cfg.broker.address.uri        = _broker_uri.c_str();
    esp_mqtt_client_handle_t c    = esp_mqtt_client_init(&cfg);
    if (c == nullptr) {
        pushEvent("[mqtt 初始化失败]");
        refreshUi();
        return;
    }

    // _client 必须在 start 之前赋值 —— CONNECTED 回调要用它去 subscribe
    _client    = c;
    _connected = false;
    esp_mqtt_client_register_event(c, MQTT_EVENT_ANY, MqttToolWindow::mqttEventHandler, this);
    esp_mqtt_client_start(c);

    char m[160];
    snprintf(m, sizeof(m), "[正在连接 %s]", _broker_uri.c_str());
    pushEvent(m);
    refreshUi();
}

// 发布：主题取界面当前值，QoS 用 _qos
void MqttToolWindow::publish(const char* payload)
{
    if (payload == nullptr || payload[0] == '\0') {
        return;
    }

    if (_row_pub) {
        const std::string t = tl::textOf(_row_pub.get());
        if (!t.empty()) {
            _pub_topic = t;   // 改了主题不用重连就能生效
        }
    }

    if (_client == nullptr || !_connected) {
        // 没连上就不装作发出去了，报文区如实写一条
        std::string ev = "[未连接] ";
        ev += payload;
        pushEvent(ev);
        refreshUi();
        return;
    }

    esp_mqtt_client_publish((esp_mqtt_client_handle_t)_client, _pub_topic.c_str(), payload, 0, _qos, 0);

    std::string ev = "\n[pub ";
    ev += _pub_topic;
    ev += "] ";
    ev += payload;
    pushEvent(ev);
}

void MqttToolWindow::onUpdate()
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
    if (batch.empty()) {
        return;
    }

    _rx_panel->addText(batch.c_str());
    // 防 TextArea 无限增长：超长只留尾部
    const char* cur = lv_textarea_get_text(_rx_panel->get());
    if (cur && strlen(cur) > _RX_MAX_CHARS) {
        _rx_panel->setText(cur + strlen(cur) - _RX_MAX_CHARS);
    }
    lv_obj_scroll_to_y(_rx_panel->get(), LV_COORD_MAX, LV_ANIM_OFF);

    // 事件推动状态显示（已连接 / 连接中… / 未连接）+ 消息数
    refreshUi();
}

void MqttToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    if (_client) {
        esp_mqtt_client_disconnect((esp_mqtt_client_handle_t)_client);
        esp_mqtt_client_destroy((esp_mqtt_client_handle_t)_client);
        _client = nullptr;
    }
    _connected = false;
    {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        while (!_rx_packets.empty()) {
            _rx_packets.pop();
        }
    }
}

// 右列状态 / 值统一在这里刷新（QoS 值、消息数、状态行、主按钮文字与配色）
void MqttToolWindow::refreshUi()
{
    if (!_row_qos || !_status_label || !_btn_run) {
        return;
    }

    char b[160];

    snprintf(b, sizeof(b), "%d", _qos);
    _row_qos->label().setText(b);

    if (_client == nullptr) {
        _status_label->setText("未连接");
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::textDim()), 0);

        _btn_run->label().setText("连接");
        _btn_run->setBgColor(lv_color_hex(tb::raised()));
        _btn_run->label().setTextColor(lv_color_hex(tb::accent()));
        return;
    }

    // 有 client：要么已连上，要么正在连（esp-mqtt 掉线会自动重连，
    // 所以"client 还在但没连上"显示成「连接中…」是诚实的）
    _btn_run->label().setText("断开");
    _btn_run->setBgColor(lv_color_hex(tb::danger()));
    _btn_run->label().setTextColor(lv_color_hex(tb::text()));

    if (_connected) {
        // 状态行不再重复服务器地址（右列"服务器"行已有），改为显示已收消息条数
        snprintf(b, sizeof(b), "已连接 · %lu 条", (unsigned long)_msg_count);
        _status_label->setText(b);
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::success()), 0);
    } else {
        _status_label->setText("连接中…");
        // 状态文字统一用正文米色（与 UDP 页一致）——状态由圆点颜色表达，文字不跟着变色
        if (_status_dot) lv_obj_set_style_bg_color(_status_dot, lv_color_hex(tb::warning()), 0);
    }
}

#endif  // CONFIG_IDF_TARGET_ESP32P4
