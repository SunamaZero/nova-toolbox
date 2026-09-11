// Tab5 工具箱 OTA 组件实现（ESP-IDF 官方 esp_https_ota）
#include "ota_client.h"

#include <cstdio>
#include <cstring>
#include "esp_log.h"
#include "esp_app_desc.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "cJSON.h"

static const char* TAG = "ota_client";

namespace ota_client {

std::string currentVersion() {
    const esp_app_desc_t* d = esp_app_get_description();
    return d ? std::string(d->version) : std::string("unknown");
}

esp_err_t check(const std::string& base_url, std::string& out_version) {
    char body[512] = {0};
    esp_http_client_config_t cfg = {};
    cfg.url = base_url.c_str();
    cfg.timeout_ms = 6000;
    esp_http_client_handle_t h = esp_http_client_init(&cfg);
    if (!h) return ESP_FAIL;

    esp_err_t e = esp_http_client_open(h, 0);
    if (e != ESP_OK) { esp_http_client_cleanup(h); return e; }
    esp_http_client_fetch_headers(h);
    if (esp_http_client_get_status_code(h) != 200) {
        esp_http_client_cleanup(h); return ESP_FAIL;
    }
    int n = esp_http_client_read(h, body, sizeof(body) - 1);
    if (n > 0) body[n] = 0;
    esp_http_client_cleanup(h);

    cJSON* root = cJSON_Parse(body);
    if (!root) return ESP_FAIL;
    cJSON* v = cJSON_GetObjectItem(root, "version");
    bool ok = v && cJSON_IsString(v);
    if (ok) out_version = v->valuestring;
    cJSON_Delete(root);
    ESP_LOGI(TAG, "check: remote=%s current=%s", out_version.c_str(), currentVersion().c_str());
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t upgrade(const std::string& base_url, void (*progress_cb)(int)) {
    std::string url = base_url;
    if (url.empty() || url.back() != '/') url += '/';
    url += "firmware.bin";

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 20000;
    cfg.keep_alive_enable = true;
    esp_https_ota_config_t ocfg = {};
    ocfg.http_config = &cfg;

    ESP_LOGI(TAG, "upgrade start: %s", url.c_str());
    esp_https_ota_handle_t h = nullptr;
    esp_err_t e = esp_https_ota_begin(&ocfg, &h);
    if (e != ESP_OK || !h) { ESP_LOGE(TAG, "begin failed: %s", esp_err_to_name(e)); return e; }

    int total = esp_https_ota_get_image_size(h);
    ESP_LOGI(TAG, "image size = %d bytes", total);
    while (true) {
        e = esp_https_ota_perform(h);
        if (e != ESP_ERR_HTTPS_OTA_IN_PROGRESS) break;
        int rd = esp_https_ota_get_image_len_read(h);
        if (total > 0 && progress_cb) progress_cb(rd * 100 / total);
    }
    if (!esp_https_ota_is_complete_data_received(h)) {
        ESP_LOGE(TAG, "incomplete download");
        esp_https_ota_abort(h);
        return ESP_FAIL;
    }
    e = esp_https_ota_finish(h);
    if (e != ESP_OK) { ESP_LOGE(TAG, "finish failed: %s", esp_err_to_name(e)); return e; }
    ESP_LOGI(TAG, "upgrade OK — rebooting");
    return ESP_OK;
}

}  // namespace ota_client
