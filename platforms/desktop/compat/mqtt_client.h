// 桌面桩：mqtt_client.h（不连 broker，仅供 UI 预览编译）
#pragma once
#include <cstdint>
#include "esp_err.h"

typedef const char* esp_event_base_t;
typedef struct esp_mqtt_client* esp_mqtt_client_handle_t;

typedef struct {
    const char* uri;
    const char* username;
    const char* password;
    int keepalive;
    bool disable_auto_reconnect;
    struct { struct { const char* uri; } address; const char* cert_pem; } broker;
} esp_mqtt_client_config_t;

typedef enum {
    MQTT_EVENT_ANY = -1,
    MQTT_EVENT_ERROR,
    MQTT_EVENT_CONNECTED,
    MQTT_EVENT_DISCONNECTED,
    MQTT_EVENT_SUBSCRIBED,
    MQTT_EVENT_UNSUBSCRIBED,
    MQTT_EVENT_PUBLISHED,
    MQTT_EVENT_DATA,
    MQTT_EVENT_BEFORE_CONNECT,
} esp_mqtt_event_id_t;

// 事件结构 + 句柄（注意：句柄是指针类型）
typedef struct esp_mqtt_event_t {
    esp_mqtt_event_id_t event_id;
    void* client;
    char* data;
    int data_len;
    int total_data_len;
    int current_data_offset;
    char* topic;
    int topic_len;
    int msg_id;
    int error_handle;
} *esp_mqtt_event_handle_t;

typedef void (*esp_event_handler_t)(void*, const char*, int32_t, void*);

#define MQTT_QOS0 0
#define MQTT_QOS1 1

inline esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t*) { return nullptr; }
inline esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t, esp_mqtt_event_id_t, esp_event_handler_t, void*) { return ESP_OK; }
inline esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t) { return ESP_FAIL; }
inline esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t) { return ESP_OK; }
inline esp_err_t esp_mqtt_client_disconnect(esp_mqtt_client_handle_t) { return ESP_OK; }
inline esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t) { return ESP_OK; }
inline int esp_mqtt_client_publish(esp_mqtt_client_handle_t, const char*, const char*, int, int, int) { return -1; }
inline int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t, const char*, int) { return -1; }
inline int esp_mqtt_client_unsubscribe(esp_mqtt_client_handle_t, const char*) { return -1; }
