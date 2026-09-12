// Tab5 工具箱 — 屏幕截图实现
//
// 依赖：CONFIG_LV_USE_SNAPSHOT=y（LVGL 官方抓屏 API）
// 流程：lv_snapshot_take → 逐像素缩放采样 → base64 → 串口分行输出

#if defined(__has_include)
#  if __has_include("sdkconfig.h")
#    include "sdkconfig.h"     // CONFIG_IDF_TARGET_ESP32P4 必须在 #ifdef 前可见
#  endif
#endif
#include "tool_screenshot.h"

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "shot";

namespace {
const char kB64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// 逐 3 字节 → 4 字符 base64，边算边写（不占大缓冲）
void emitBase64Line(const uint8_t* src, int n, char* out) {
    int o = 0;
    for (int i = 0; i < n; i += 3) {
        uint32_t v = src[i] << 16;
        if (i + 1 < n) v |= src[i + 1] << 8;
        if (i + 2 < n) v |= src[i + 2];
        out[o++] = kB64[(v >> 18) & 63];
        out[o++] = kB64[(v >> 12) & 63];
        out[o++] = (i + 1 < n) ? kB64[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < n) ? kB64[v & 63] : '=';
    }
    out[o] = 0;
}
}  // namespace

namespace screenshot {

void captureAndSend(int scale) {
    if (scale < 1) scale = 1;

    lv_obj_t* scr = lv_screen_active();
    if (!scr) { ESP_LOGE(TAG, "no active screen"); return; }

    const int full_w = lv_obj_get_width(scr);
    const int full_h = lv_obj_get_height(scr);
    const size_t stride = (size_t)full_w * 3;
    const size_t need = stride * full_h;

    // 手工构造 draw_buf（LVGL 默认用内部 RAM，2.7MB 必然失败 → 走 PSRAM）
    lv_draw_buf_t* snap = (lv_draw_buf_t*)heap_caps_calloc(1, sizeof(lv_draw_buf_t),
                                                           MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    uint8_t* px = (uint8_t*)heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!snap || !px) {
        ESP_LOGE(TAG, "PSRAM alloc failed (%u bytes)", (unsigned)need);
        if (snap) free(snap);
        if (px) free(px);
        return;
    }
    snap->handlers      = lv_draw_buf_get_handlers();   // 关键：不填的话内部 alloc/free 会崩
    snap->header.magic  = LV_IMAGE_HEADER_MAGIC;
    snap->header.cf     = LV_COLOR_FORMAT_RGB888;
    snap->header.w      = full_w;
    snap->header.h      = full_h;
    snap->header.stride = stride;
    snap->data_size     = need;
    snap->data          = px;

    if (lv_snapshot_take_to_draw_buf(scr, LV_COLOR_FORMAT_RGB888, snap) != LV_RESULT_OK) {
        ESP_LOGE(TAG, "snapshot take failed");
        free(px); free(snap);
        return;
    }

    const int sw = snap->header.w, sh = snap->header.h, sstride = snap->header.stride;
    const int ow = sw / scale, oh = sh / scale;
    uint8_t* src = (uint8_t*)snap->data;

    // 关键：传输期间静音所有日志 —— 任何插进 stdout 的日志都会破坏 base64 流
    esp_log_level_set("*", ESP_LOG_NONE);
    ESP_LOGI(TAG, "snapshot %dx%d -> %dx%d", sw, sh, ow, oh);

    // 逐行缩放 + 输出（每行 RGB888 打包成 base64 行）
    const int row_bytes = ow * 3;
    char* b64line = (char*)malloc(row_bytes / 3 * 4 + 8);
    uint8_t* rowbuf = (uint8_t*)malloc(row_bytes);
    if (!b64line || !rowbuf) {
        ESP_LOGE(TAG, "no mem");
        free(b64line); free(rowbuf);
        free(px); free(snap);
        return;
    }

    int total = row_bytes * oh;
    printf("SHOT_BEGIN %d %d %d\n", ow, oh, total);
    fflush(stdout);

    for (int y = 0; y < oh; ++y) {
        int sy = y * scale;
        const uint8_t* srow = src + (size_t)sy * sstride;
        for (int x = 0; x < ow; ++x) {
            const uint8_t* p = srow + (size_t)(x * scale) * 3;
            rowbuf[x * 3 + 0] = p[0];   // R
            rowbuf[x * 3 + 1] = p[1];   // G
            rowbuf[x * 3 + 2] = p[2];   // B
        }
        emitBase64Line(rowbuf, row_bytes, b64line);
        printf("%s\n", b64line);
        if ((y & 3) == 0) vTaskDelay(1);   // 让出 CPU，避免看门狗超时
    }
    printf("SHOT_END\n");
    fflush(stdout);

    free(b64line);
    free(rowbuf);
    free(px); free(snap);
    esp_log_level_set("*", ESP_LOG_INFO);
    printf("SHOT_DONE" "\n");
    fflush(stdout);
}

}  // namespace screenshot

#endif  // CONFIG_IDF_TARGET_ESP32P4
