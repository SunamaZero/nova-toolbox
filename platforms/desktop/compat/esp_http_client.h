// 桌面桩：esp_http_client.h（不做真实网络请求）
#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"

typedef struct esp_http_client* esp_http_client_handle_t;
typedef enum { HTTP_METHOD_GET = 0, HTTP_METHOD_POST } esp_http_client_method_t;

typedef struct {
    const char* url;
    int timeout_ms;
    bool keep_alive_enable;
    bool skip_cert_common_name_check;
    esp_http_client_method_t method;
} esp_http_client_config_t;

inline esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t*) { return nullptr; }
inline int  esp_http_client_set_header(esp_http_client_handle_t, const char*, const char*) { return 0; }
inline int  esp_http_client_set_post_field(esp_http_client_handle_t, const char*, int) { return 0; }
inline int  esp_http_client_perform(esp_http_client_handle_t) { return -1; }   // 失败，桌面无网络功能
inline int  esp_http_client_get_status_code(esp_http_client_handle_t) { return 0; }
inline int  esp_http_client_read(esp_http_client_handle_t, char*, int) { return 0; }
inline int  esp_http_client_get_content_length(esp_http_client_handle_t) { return 0; }
inline void esp_http_client_cleanup(esp_http_client_handle_t) {}
