/*
 * Nova Toolbox for M5Stack Tab5
 * MQTT 测试工具：预设 broker 连接 + 订阅/发布
 * v1：固定 broker 候选 + 主题 tab5/#，按钮驱动（无文本输入依赖）
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

#include "mqtt_client.h"

using namespace launcher_view;
using namespace smooth_ui_toolkit;
using namespace smooth_ui_toolkit::lvgl_cpp;

static const std::string _tag = "tool-mqtt";

// 预设 broker（按可达性排序，可在代码里换）
static const char* _MQTT_URI_DEFAULT = "mqtt://broker.emqx.io:1883";
static const char* _MQTT_TOPIC_DEFAULT = "tab5/#";
static const char* _SUB_TOPIC  = "tab5/#";
static const char* _PUB_TOPIC  = "tab5/test";

static constexpr int _RX_MAX_CHARS = 3000;

MqttToolWindow::MqttToolWindow()
{
    config.kfClosed = {500, 280, 90, 60, 0};
    config.kfOpened = {0, 0, 1180, 622, 255};
    config.bgColor  = tb::bg();
}

void MqttToolWindow::pushEvent(const std::string& info)
{
    {
        std::lock_guard<std::mutex> lock(_rx_mutex);
        _rx_packets.push(info);
        if (_rx_packets.size() > 50) {
            _rx_packets.pop();
        }
    }
}

void MqttToolWindow::setStatus(const char* s)
{
    if (_status_label) {
        _status_label->setText(s);
    }
}

// ---- esp-mqtt 事件回调（C 线程上下文，只压队列）----
void MqttToolWindow::mqttEventHandler(void* handler_args, esp_event_base_t base, int32_t event_id,
                                      void* event_data)
{
    MqttToolWindow* self = static_cast<MqttToolWindow*>(handler_args);
    esp_mqtt_event_handle_t event = static_cast<esp_mqtt_event_handle_t>(event_data);

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        self->pushEvent("\n[broker connected]");
        // 连接后自动订阅
        esp_mqtt_client_subscribe((esp_mqtt_client_handle_t)self->_client, self->_sub_topic.c_str(), 1);
        break;
    }
    case MQTT_EVENT_DISCONNECTED:
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
    mclog::tagInfo(_tag, "on open");
    _window->setScrollbarMode(LV_SCROLLBAR_MODE_OFF);
    _msg_count = 0;

    _title_label = std::make_unique<Label>(_window->get());
    _title_label->align(LV_ALIGN_TOP_LEFT, 24, 12);
    _title_label->setText("MQTT Tool");
    _title_label->setTextFont(tb::fontTitle());
    _title_label->setTextColor(lv_color_hex(tb::text()));

    _status_label = std::make_unique<Label>(_window->get());
    _status_label->align(LV_ALIGN_TOP_RIGHT, -24, 16);
    _status_label->setText("idle | broker.emqx.io");
    _status_label->setTextFont(tb::fontBody());
    _status_label->setTextColor(lv_color_hex(tb::textDim()));

    // ---- broker / topic 配置行 ----
    auto bl = std::make_unique<Label>(_window->get());
    bl->align(LV_ALIGN_TOP_LEFT, 24, 64);
    bl->setText("服务器");
    bl->setTextFont(tb::fontBody());
    bl->setTextColor(lv_color_hex(tb::textDim()));

    _broker_input = std::make_unique<TextArea>(_window->get());
    _broker_input->setSize(400, 48);
    _broker_input->align(LV_ALIGN_TOP_LEFT, 120, 56);
    _broker_input->setOneLine(true);
    _broker_input->setBorderWidth(1);
    _broker_input->setBorderColor(lv_color_hex(tb::border()));
    _broker_input->setBgColor(lv_color_hex(tb::surface()));
    _broker_input->setTextFont(tb::fontSm());
    _broker_input->setTextColor(lv_color_hex(tb::text()));
    _broker_input->setText("mqtt://broker.emqx.io:1883");
    tool_kbd::attach(_broker_input->get());

    auto tl = std::make_unique<Label>(_window->get());
    tl->align(LV_ALIGN_TOP_LEFT, 552, 64);
    tl->setText("主题");
    tl->setTextFont(tb::fontBody());
    tl->setTextColor(lv_color_hex(tb::textDim()));

    _topic_input = std::make_unique<TextArea>(_window->get());
    _topic_input->setSize(260, 48);
    _topic_input->align(LV_ALIGN_TOP_LEFT, 640, 56);
    _topic_input->setOneLine(true);
    _topic_input->setBorderWidth(1);
    _topic_input->setBorderColor(lv_color_hex(tb::border()));
    _topic_input->setBgColor(lv_color_hex(tb::surface()));
    _topic_input->setTextFont(tb::fontSm());
    _topic_input->setTextColor(lv_color_hex(tb::text()));
    _topic_input->setText("tab5/#");
    tool_kbd::attach(_topic_input->get());

    _btn_apply_cfg = std::make_unique<Button>(_window->get());
    _btn_apply_cfg->setSize(140, 48);
    _btn_apply_cfg->align(LV_ALIGN_TOP_LEFT, 920, 56);
    _btn_apply_cfg->setBgColor(lv_color_hex(tb::raised()));
    _btn_apply_cfg->setRadius(tb::RadiusMd);
    _btn_apply_cfg->label().setTextFont(tb::fontBody());
    _btn_apply_cfg->label().setTextColor(lv_color_hex(tb::accent()));
    _btn_apply_cfg->label().setText("Connect");
    _btn_apply_cfg->onClick().connect([&]() {
        audio::play_next_tone_progression();
        const char* b = lv_textarea_get_text(_broker_input->get());
        const char* t = lv_textarea_get_text(_topic_input->get());
        if (b && b[0]) _broker_uri = b;
        if (t && t[0]) _sub_topic = t;
        _rx_panel->addText("\n[cfg] broker/topic updated (reconnect to apply)\n");
    });

    _rx_panel = std::make_unique<TextArea>(_window->get());
    _rx_panel->setSize(1132, 332);
    _rx_panel->align(LV_ALIGN_TOP_LEFT, 24, 112);
    _rx_panel->setMaxLength(4096);
    _rx_panel->setCursorClickPos(false);
    _rx_panel->setText("Broker: broker.emqx.io:1883\nTopic: tab5/#\n\nConnect to begin.\n");
    _rx_panel->setPasswordMode(false);
    _rx_panel->setOneLine(false);
    _rx_panel->setBorderWidth(1);
    _rx_panel->setBorderColor(lv_color_hex(tb::border()));
    _rx_panel->setBgColor(lv_color_hex(tb::surface()));
    _rx_panel->setTextFont(tb::fontBody());
    _rx_panel->setTextColor(lv_color_hex(tb::text()));
    _rx_panel->setScrollbarMode(LV_SCROLLBAR_MODE_AUTO);

    // TX 自定义发布输入行
    _tx_input = std::make_unique<TextArea>(_window->get());
    _tx_input->setSize(840, 52);
    _tx_input->align(LV_ALIGN_TOP_LEFT, 24, 456);
    lv_textarea_set_placeholder_text(_tx_input->get(), "payload to publish...");
    _tx_input->setOneLine(true);
    _tx_input->setBorderWidth(1);
    _tx_input->setBorderColor(lv_color_hex(tb::border()));
    _tx_input->setBgColor(lv_color_hex(tb::surface()));
    _tx_input->setTextFont(tb::fontBody());
    _tx_input->setTextColor(lv_color_hex(tb::text()));
    tool_kbd::attach(_tx_input->get());

    // 自定义发布按钮（TX 行右侧）
    _btn_pub_custom = std::make_unique<Button>(_window->get());
    _btn_pub_custom->setSize(292, 52);
    _btn_pub_custom->align(LV_ALIGN_TOP_LEFT, 880, 456);
    _btn_pub_custom->setBgColor(lv_color_hex(tb::raised()));
    _btn_pub_custom->setRadius(tb::RadiusMd);
    _btn_pub_custom->label().setTextFont(tb::fontBody());
    _btn_pub_custom->label().setText("Publish Custom");
    _btn_pub_custom->onClick().connect([&]() {
        audio::play_next_tone_progression();
        if (!_connected) {
            pushEvent("[not connected]");
            return;
        }
        const char* payload = lv_textarea_get_text(_tx_input->get());
        if (payload && strlen(payload) > 0) {
            esp_mqtt_client_publish((esp_mqtt_client_handle_t)_client, _PUB_TOPIC, payload, 0, 1, 0);
            std::string ev = "\n[pub ";
            ev += _PUB_TOPIC;
            ev += "] ";
            ev += payload;
            pushEvent(ev);
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

    _btn_connect = make_btn(30, "连接", tb::raised());
    _btn_connect->onClick().connect([&]() {
        audio::play_next_tone_progression();
        if (_client) {
            esp_mqtt_client_disconnect((esp_mqtt_client_handle_t)_client);
            esp_mqtt_client_destroy((esp_mqtt_client_handle_t)_client);
            _client = nullptr;
            _connected = false;
            setStatus("idle");
            _rx_panel->addText("\n[client destroyed]\n");
            _btn_connect->label().setText("Connect");
            return;
        }
        esp_mqtt_client_config_t cfg = {};
        cfg.broker.address.uri = _broker_uri.c_str();
        esp_mqtt_client_handle_t c = esp_mqtt_client_init(&cfg);
        if (!c) {
            _rx_panel->addText("\n[mqtt init failed]\n");
            return;
        }
        esp_mqtt_client_register_event(c, MQTT_EVENT_ANY, MqttToolWindow::mqttEventHandler, this);
        esp_mqtt_client_start(c);
        _client = c;
        setStatus("connecting...");
        _btn_connect->label().setText("Disconnect");
        _rx_panel->addText("\n[connecting ");
        _rx_panel->addText(_broker_uri.c_str());
        _rx_panel->addText("]\n");
    });

    _btn_sub = make_btn(255, "Subscribe", tb::raised());
    _btn_sub->onClick().connect([&]() {
        audio::play_next_tone_progression();
        if (!_client) {
            _rx_panel->addText("\n[connect first]\n");
            return;
        }
        esp_mqtt_client_subscribe((esp_mqtt_client_handle_t)_client, _sub_topic.c_str(), 1);
        _rx_panel->addText("\n[subscribed ");
        _rx_panel->addText(_SUB_TOPIC);
        _rx_panel->addText("]\n");
    });

    _btn_pub = make_btn(480, "Publish", tb::raised());
    _btn_pub->onClick().connect([&]() {
        audio::play_next_tone_progression();
        if (!_client) {
            _rx_panel->addText("\n[connect first]\n");
            return;
        }
        const char* payload = "hello from Tab5 toolbox";
        int msg_id = esp_mqtt_client_publish((esp_mqtt_client_handle_t)_client, _PUB_TOPIC, payload, 0, 1, 0);
        char buf[96];
        snprintf(buf, sizeof(buf), "\n[pub %s id=%d]\n", _PUB_TOPIC, msg_id);
        _rx_panel->addText(buf);
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
    if (!batch.empty()) {
        _rx_panel->addText(batch.c_str());
        const char* cur = lv_textarea_get_text(_rx_panel->get());
        if (cur && strlen(cur) > _RX_MAX_CHARS) {
            const char* tail = cur + strlen(cur) - _RX_MAX_CHARS;
            _rx_panel->setText(tail);
        }
    }
}

void MqttToolWindow::onClose()
{
    mclog::tagInfo(_tag, "on close");
    if (_client) {
        esp_mqtt_client_disconnect((esp_mqtt_client_handle_t)_client);
        esp_mqtt_client_destroy((esp_mqtt_client_handle_t)_client);
        _client = nullptr;
    }

}

#endif  // CONFIG_IDF_TARGET_ESP32P4