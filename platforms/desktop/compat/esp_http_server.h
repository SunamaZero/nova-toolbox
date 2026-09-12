// 桌面桩：esp_http_server.h（不起真实服务）
#pragma once
#include <cstddef>
#include <cstdint>
#include "esp_err.h"

typedef struct httpd_req { void* handle; const char* uri; int method; void* user_ctx; size_t content_len; } httpd_req_t;
typedef struct httpd_handle* httpd_handle_t;
typedef esp_err_t (*httpd_uri_handler_t)(httpd_req_t*);

typedef struct {
    const char* uri;
    int method;
    httpd_uri_handler_t handler;
    void* user_ctx;
} httpd_uri_t;

typedef struct {
    unsigned server_port;
    size_t   stack_size;
    unsigned lru_purge_enable;
    unsigned max_uri_handlers;
    unsigned task_priority;
    void*    uri_match_fn;
} httpd_config_t;

#define HTTPD_DEFAULT_CONFIG() httpd_config_t{ .server_port = 80, .stack_size = 4096, .lru_purge_enable = 0, .max_uri_handlers = 8, .task_priority = 5, .uri_match_fn = nullptr }
#define HTTP_GET    1
#define HTTP_POST   3
#define HTTP_PUT    4
#define HTTP_DELETE 0
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
#define HTTPD_RESP_USE_STRLEN ((size_t)-1)

inline esp_err_t httpd_start(httpd_handle_t* h, const httpd_config_t*) { if (h) *h = nullptr; return ESP_FAIL; }
inline esp_err_t httpd_stop(httpd_handle_t) { return ESP_OK; }
inline esp_err_t httpd_register_uri_handler(httpd_handle_t, const httpd_uri_t*) { return ESP_OK; }
inline esp_err_t httpd_resp_send(httpd_req_t*, const char*, size_t) { return ESP_OK; }
inline esp_err_t httpd_resp_sendstr(httpd_req_t*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_set_type(httpd_req_t*, const char*) { return ESP_OK; }
inline esp_err_t httpd_resp_set_hdr(httpd_req_t*, const char*, const char*) { return ESP_OK; }
inline int       httpd_req_recv(httpd_req_t*, char*, size_t) { return 0; }
inline esp_err_t httpd_req_get_url_query_str(httpd_req_t*, char*, size_t) { return ESP_FAIL; }
inline esp_err_t httpd_req_get_hdr_value_str(httpd_req_t*, const char*, char*, size_t) { return ESP_FAIL; }
