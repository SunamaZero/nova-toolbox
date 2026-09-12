// Tab5 工具箱 — 日志系统实现
//
// 三件事：
//   1. 钩住 ESP-IDF 的日志输出（esp_log_set_vprintf），既转发到原控制台，也存进环形缓冲
//   2. 起 HTTP server，暴露 /log（纯文本）和 /（HTML 查看页）
//   3. 后台每 30s 向 PC 的 ota_server 上报本机 IP，方便远程拉日志

#if defined(__has_include)
#  if __has_include("sdkconfig.h")
#    include "sdkconfig.h"
#  endif
#endif

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "tool_logsys.h"
#include "toolbox_theme.h"
#include "hal/hal.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdarg>
#include <ctime>
#include <string>
#include "esp_sntp.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_http_client.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char* TAG = "logsys";

namespace logsys {

namespace {
constexpr int kMaxLines = 600;      // 环形缓冲条数（写满绕回，内存恒定）
constexpr int kLineLen  = 220;      // 单条最大长度（超出截断，不动态扩容）

// 关键：600×220≈132KB，放内部 SRAM 会挤爆启动期 heap（实测 abort）；
// Tab5 有 32MB PSRAM，日志缓冲挪过去。
#include "esp_heap_caps.h"
static char (*s_buf)[kLineLen] = nullptr;   // 运行时在 PSRAM 分配
int      s_head = 0;                // 下一个写入位置
int      s_total = 0;               // 累计写入条数
SemaphoreHandle_t s_mtx = nullptr;
vprintf_like_t s_prev_vprintf = nullptr;
httpd_handle_t s_httpd = nullptr;
bool s_inited = false;

// ---------------------------------------------------------------------------
// 校时：设备原先没有时间来源，日志里只有 IDF 的开机毫秒数，没法跟 PC 侧对账。
// 联网后走 SNTP 校准系统时钟，时区固定东八区（中国大陆无夏令时）。
// 校时失败也不影响日志 —— 时间戳退化成"[+开机秒数.十分位]"。
// ---------------------------------------------------------------------------
void time_sync_task(void*)
{
    // 时区不在这里设 —— 见 init()。这里只负责 SNTP 校时。
    // （曾经把 setenv("TZ") 放这里，结果日志钩子在它之前就挂上了，
    //   开机的整段日志都是 UTC 时间戳，用户实测发现"时间不对"。）
    bool started = false;
    int  tries   = 0;
    for (;;) {
        if (time(nullptr) > 1700000000) {   // 2023-11 之后视为已校时
            ESP_LOGI("logsys", "系统时间已同步，日志带时间戳");
            vTaskDelete(nullptr);
        }
        if (!started) {
            esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
            esp_sntp_setservername(0, "ntp.aliyun.com");
            esp_sntp_init();
            started = true;
        } else if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && ++tries % 10 == 0) {
            esp_sntp_restart();             // 首次可能网络还没就绪，每 30s 重试
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

// 自定义 vprintf：先转发给原控制台，再存一份
int log_vprintf(const char* fmt, va_list args)
{
    char line[kLineLen];
    va_list copy;
    va_copy(copy, args);
    vsnprintf(line, sizeof(line), fmt, copy);
    va_end(copy);

    // 加时间戳：校时成功打 [HH:MM:SS]，否则打 [+开机秒.十分位]
    // 比 line 多留 24 字节给时间戳前缀，否则编译器报 format-truncation（-Werror 直接失败）
    char stamped[kLineLen + 24];
    {
        char stamp[16];
        const time_t now = time(nullptr);
        if (now > 1700000000) {
            struct tm tmv;
            localtime_r(&now, &tmv);
            strftime(stamp, sizeof(stamp), "%H:%M:%S", &tmv);
        } else {
            const int64_t us = esp_timer_get_time();
            snprintf(stamp, sizeof(stamp), "+%u.%01u",
                     (unsigned)(us / 1000000), (unsigned)((us / 100000) % 10));
        }
        snprintf(stamped, sizeof(stamped), "[%s] %s", stamp, line);
    }

    if (s_mtx && xSemaphoreTake(s_mtx, pdMS_TO_TICKS(20)) == pdTRUE) {
        size_t n = strlen(stamped);
        // 去掉尾部换行（显示时统一加）
        while (n > 0 && (stamped[n - 1] == '\n' || stamped[n - 1] == '\r')) stamped[--n] = 0;
        if (n > 0) {
            size_t copy = n < (size_t)(kLineLen - 1) ? n : (size_t)(kLineLen - 1);
            memcpy(s_buf[s_head], stamped, copy);
            s_buf[s_head][copy] = 0;
            s_head = (s_head + 1) % kMaxLines;
            s_total++;
        }
        xSemaphoreGive(s_mtx);
    }
    return s_prev_vprintf ? s_prev_vprintf(fmt, args) : vprintf(fmt, args);
}

// ---------- HTTP ----------
esp_err_t log_get_handler(httpd_req_t* req)
{
    std::string body = tail(kMaxLines);
    httpd_resp_set_type(req, "text/plain; charset=utf-8");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t index_get_handler(httpd_req_t* req)
{
    std::string body =
        "<!DOCTYPE html><html><head><meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Tab5 Logs</title>"
        "<style>body{background:#14120D;color:#E8E2D2;font-family:ui-monospace,monospace;"
        "font-size:13px;margin:0;padding:16px}h1{color:#D4A017;font-size:16px;margin:0 0 12px}"
        "pre{white-space:pre-wrap;word-break:break-all;line-height:1.5;margin:0}"
        "a{color:#9BB24A}</style></head><body>"
        "<h1>Tab5 Toolbox — 设备日志</h1>"
        "<p><a href='/log'>/log</a>（纯文本，可 curl 拉取） · 每 5 秒自动刷新</p>"
        "<pre id='l'>loading...</pre>"
        "<script>async function u(){try{const r=await fetch('/log');document.getElementById('l').textContent=await r.text();}catch(e){}"
        "}u();setInterval(u,5000);</script></body></html>";
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, body.c_str(), body.size());
}

httpd_handle_t start_httpd()
{
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.server_port = 8090;       // 8080 被 HTTP 抓包工具占用，必须避开
    cfg.stack_size = 8192;        // 默认 4096 在 P4 上偏紧
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;
    httpd_handle_t h = nullptr;
    if (httpd_start(&h, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed (port 80 busy?)");
        return nullptr;
    }
    httpd_uri_t u_log   = {.uri = "/log", .method = HTTP_GET, .handler = log_get_handler, .user_ctx = nullptr};
    httpd_uri_t u_index = {.uri = "/",    .method = HTTP_GET, .handler = index_get_handler, .user_ctx = nullptr};
    httpd_register_uri_handler(h, &u_log);
    httpd_register_uri_handler(h, &u_index);
    ESP_LOGI(TAG, "log http ready on :8090  ->  http://%s:8090/log", GetHAL()->wifiGetStaIp().c_str());
    return h;
}

// ---------- 上报 IP ----------
void report_ip_task(void*)
{
    const char* ota_base = "http://192.168.31.214:8093/register";
    // 等 WiFi 连上（网络栈就绪前启动 httpd 会 panic）
    while (!GetHAL()->wifiIsStaConnected()) vTaskDelay(pdMS_TO_TICKS(1000));
    vTaskDelay(pdMS_TO_TICKS(2000));
    s_httpd = start_httpd();          // 现在才安全
    ESP_LOGI(TAG, "http ready, ip=%s", GetHAL()->wifiGetStaIp().c_str());
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(30000));
        if (!GetHAL()->wifiIsStaConnected()) continue;
        std::string ip = GetHAL()->wifiGetStaIp();
        if (ip.empty() || ip == "-") continue;

        std::string payload = "{\"ip\":\"" + ip + "\"}";
        esp_http_client_config_t cfg = {};
        cfg.url = ota_base;
        cfg.timeout_ms = 4000;
        cfg.method = HTTP_METHOD_POST;
        esp_http_client_handle_t c = esp_http_client_init(&cfg);
        if (!c) continue;
        esp_http_client_set_header(c, "Content-Type", "application/json");
        esp_http_client_set_post_field(c, payload.c_str(), payload.size());
        esp_err_t e = esp_http_client_perform(c);
        // 只在 IP 变化时打日志（否则每 30s 一条会把有效日志挤出去）
        static std::string last_ip;
        if (e == ESP_OK && ip != last_ip) {
            ESP_LOGI(TAG, "device ip: %s  (远程日志 http://%s:8090/log)", ip.c_str(), ip.c_str());
            last_ip = ip;
        }
        esp_http_client_cleanup(c);
    }
}
}  // namespace

void init()
{
    if (s_inited) return;
    s_inited = true;

    // 【时区必须在这里同步设好】不能丢给异步任务：日志钩子一挂上就开始打时间戳，
    // 若时区还没生效，打的就全是 UTC（用户实测差 8 小时）。
    // CST-8 是 POSIX 写法，语义就是 UTC+8（POSIX 的符号与直觉相反）。
    setenv("TZ", "CST-8", 1);
    tzset();
    {
        // 自证诊断：本地时与 UTC 时若相同，说明时区没生效。
        // 这样下次拉设备日志就能一眼判断，不用再猜。
        const time_t now = time(nullptr);
        struct tm lt {}, gt {};
        localtime_r(&now, &lt);
        gmtime_r(&now, &gt);
        ESP_LOGI(TAG, "TZ=%s | 本地 %02d:%02d:%02d vs UTC %02d:%02d:%02d | %s",
                 getenv("TZ") ? getenv("TZ") : "(null)",
                 lt.tm_hour, lt.tm_min, lt.tm_sec,
                 gt.tm_hour, gt.tm_min, gt.tm_sec,
                 (lt.tm_hour == gt.tm_hour) ? "⚠️ 时区未生效(按UTC打)" : "✓ 时区已生效");
    }

    s_buf = (char (*)[kLineLen])heap_caps_calloc(kMaxLines, kLineLen, MALLOC_CAP_SPIRAM);
    if (!s_buf) {
        ESP_LOGW(TAG, "PSRAM 分配失败，回退内部 RAM");
        s_buf = (char (*)[kLineLen])calloc(kMaxLines, kLineLen);
    }
    s_mtx = xSemaphoreCreateMutex();
    s_prev_vprintf = esp_log_set_vprintf(log_vprintf);   // 先挂钩子，后面的日志都能被记录
    ESP_LOGI(TAG, "log capture started (ring %d lines)", kMaxLines);

    // HTTP server 延后到 WiFi 就绪（见 report_ip_task）
    xTaskCreate(report_ip_task, "log_report", 8192, nullptr, 3, nullptr);
    xTaskCreate(time_sync_task, "log_time", 3072, nullptr, 3, nullptr);   // 联网后 SNTP 校时
}

std::string tail(int n)
{
    std::string out;
    if (n > kMaxLines) n = kMaxLines;
    if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY);
    int avail = s_total < kMaxLines ? s_total : kMaxLines;
    if (n > avail) n = avail;
    int start = (s_head - n + kMaxLines) % kMaxLines;
    char head[96];
    snprintf(head, sizeof(head), "=== Tab5 logs: last %d of %d ===\n", n, s_total);
    out += head;
    for (int i = 0; i < n; ++i) {
        out += s_buf[(start + i) % kMaxLines];
        out += '\n';
    }
    if (s_mtx) xSemaphoreGive(s_mtx);
    return out;
}

std::vector<std::string> tail_lines(int n)
{
    std::vector<std::string> out;
    if (n > kMaxLines) n = kMaxLines;
    if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY);
    int avail = s_total < kMaxLines ? s_total : kMaxLines;
    if (n > avail) n = avail;
    if (n > 0) {
        int start = (s_head - n + kMaxLines) % kMaxLines;
        out.reserve(n);
        for (int i = 0; i < n; ++i) {
            out.emplace_back(s_buf[(start + i) % kMaxLines]);
        }
    }
    if (s_mtx) xSemaphoreGive(s_mtx);
    return out;
}

int capacity() { return kMaxLines; }

int count() { return s_total; }

void clear()
{
    if (s_mtx) xSemaphoreTake(s_mtx, portMAX_DELAY);
    s_head = 0; s_total = 0;
    if (s_mtx) xSemaphoreGive(s_mtx);
}

}  // namespace logsys

#endif  // CONFIG_IDF_TARGET_ESP32P4
