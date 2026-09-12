/*
 * Tab5 官方键盘驱动（I2C 0x6D @ ExtPort1 SDA0/SCL1）
 *
 * 协议（源自 m5stack/M5Tab5-Keyboard-UserDemo 官方 IDF 驱动）：
 *   0x00 INT_CFG / 0x01 INT_STA（bit2=字符串事件）/ 0x02 EVENT_NUM（写0清空）
 *   0x10 KEYBOARD_MODE（0=Normal 1=HID 2=STRING）
 *   0x40 CHAR_EVENT_LEN（本次事件长度）/ 0x50 CHAR_EVENT_BASE（1字节修饰键 + 字符）
 * 模式 2（STRING）下键盘直接输出 ASCII 字符，最省事。
 */
#include "hal/hal_esp32.h"
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <cstring>
#include <lvgl.h>
#include <hal/hal.h>

#define TAG "kb"

#define KB_ADDR        0x6D
#define KB_SDA         0
#define KB_SCL         1
#define KB_FREQ_HZ     100000     // 官方默认 100kHz（LP I2C 上限较低，别调高）
#define KB_INT_PIN     50         // 未使用（改用轮询，避免 ISR 复杂度）

#define REG_INT_CFG    0x00
#define REG_INT_STA    0x01
#define REG_EVENT_NUM  0x02
#define REG_MODE       0x10
#define REG_CHAR_LEN   0x40
#define REG_CHAR_BASE  0x50
#define REG_VERSION    0xFE

#define MODE_STRING    2

static i2c_master_bus_handle_t s_kb_bus = nullptr;
static i2c_master_dev_handle_t s_kb_dev = nullptr;
static QueueHandle_t s_kb_q      = nullptr;

// 特殊键内部编码（>0x7F，与 ASCII 区分）
#define KB_KEY_ENTER      0x101
#define KB_KEY_BACKSPACE  0x102
#define KB_KEY_DELETE     0x103
#define KB_KEY_TAB        0x104
#define KB_KEY_ESC        0x105
#define KB_KEY_SPACE      0x106
static bool s_kb_ready           = false;

static bool kb_read_reg(uint8_t reg, uint8_t* val)
{
    if (!s_kb_dev) return false;
    return i2c_master_transmit_receive(s_kb_dev, &reg, 1, val, 1, 100) == ESP_OK;
}

static bool kb_write_reg(uint8_t reg, uint8_t val)
{
    if (!s_kb_dev) return false;
    uint8_t buf[2] = {reg, val};
    return i2c_master_transmit(s_kb_dev, buf, 2, 100) == ESP_OK;
}

static bool kb_read_bytes(uint8_t reg, uint8_t* data, size_t len)
{
    if (!s_kb_dev) return false;
    return i2c_master_transmit_receive(s_kb_dev, &reg, 1, data, len, 100) == ESP_OK;
}

// 轮询任务：50ms 一次查中断状态，有字符串事件就取字符塞进队列
static void kb_poll_task(void*)
{
    uint32_t idle_ticks = 0;
    for (;;) {
        uint8_t sta = 0;
        kb_read_reg(REG_INT_STA, &sta);

        // 双保险：既看中断状态位，也直接问长度寄存器（某些固件版本状态位不更新）
        uint8_t len = 0;
        bool have_len = kb_read_bytes(REG_CHAR_LEN, &len, 1);

        if ((sta & 0x04) || (have_len && len > 0)) {
            if (have_len && len > 0 && len <= 15) {
                uint8_t buf[17] = {};
                // 1 字节修饰键 + len 字节字符
                if (kb_read_bytes(REG_CHAR_BASE, buf, len + 1)) {
                    ESP_LOGI(TAG, "event: sta=0x%02X len=%u mod=0x%02X text='%.*s'",
                             sta, len, buf[0], (int)(len < 16 ? len : 16), (char*)&buf[1]);
                    // 载荷 = 1 字节修饰键 + 内容；内容是"单字符"或"键名"（官方 STRING 模式约定）
                    size_t n = strnlen((char*)&buf[1], len);
                    if (n <= 1) {
                        if (n == 1 && buf[1] >= 0x20 && buf[1] < 0x7F) {
                            uint16_t ch = buf[1];
                            xQueueSend(s_kb_q, &ch, 0);
                        }
                    } else {
                        // 键名 → 特殊键码
                        char name[16] = {};
                        memcpy(name, &buf[1], n < 15 ? n : 15);
                        uint16_t code = 0;
                        // 不同固件版本的键名可能是缩写（日志实测出现 del / bac / ent），
                        // 所以用前缀匹配而不是全等匹配
                        auto starts = [](const char* str, const char* pre) {
                            return strncmp(str, pre, strlen(pre)) == 0;
                        };
                        if (starts(name, "enter"))                              code = KB_KEY_ENTER;
                        else if (starts(name, "back") || starts(name, "bsp"))   code = KB_KEY_BACKSPACE;
                        else if (starts(name, "del"))                           code = KB_KEY_DELETE;
                        else if (starts(name, "tab"))                           code = KB_KEY_TAB;
                        else if (starts(name, "esc"))                           code = KB_KEY_ESC;
                        else if (starts(name, "space"))                         code = KB_KEY_SPACE;
                        else ESP_LOGW(TAG, "unmapped key name: '%s' (len=%u)", name, (unsigned)n);
                        if (code) {
                            xQueueSend(s_kb_q, &code, 0);
                        }
                    }
                }
            }
            kb_write_reg(REG_INT_STA, 0x00);  // 清中断状态
            idle_ticks = 0;
        } else {
            // 空闲时不再每 10 秒刷一行心跳（日志被它淹了，且无信息量）。
            // 只在"状态真的变了"时打一次 —— 键盘挂了/中断没清掉时依然看得出来。
            static uint8_t last_sta = 0xFF;
            static uint32_t last_len = 0xFFFFFFFF;
            if (sta != last_sta || len != last_len) {
                ESP_LOGI(TAG, "poll idle: sta=0x%02X len=%u queue=%u",
                         sta, len, (unsigned)uxQueueMessagesWaiting(s_kb_q));
                last_sta = sta;
                last_len = len;
            }
            idle_ticks = 0;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

bool HalEsp32::keyboardInit()
{
    if (s_kb_ready) return true;

    // 注意：HP I2C 的 NUM_0/NUM_1 已被 BSP 占用（触摸 GPIO31/32、扩展 GPIO53/54），
    // 键盘在 ExtPort1 的 GPIO0/1 —— 属于 LP 域，必须用 LP I2C 控制器（P4 共 3 个 I2C：2 HP + 1 LP）
    i2c_master_bus_config_t bus_cfg = {};
    bus_cfg.i2c_port                = LP_I2C_NUM_0;
    bus_cfg.lp_source_clk           = LP_I2C_SCLK_DEFAULT;
    bus_cfg.sda_io_num              = (gpio_num_t)KB_SDA;
    bus_cfg.scl_io_num              = (gpio_num_t)KB_SCL;
    bus_cfg.glitch_ignore_cnt       = 7;
    bus_cfg.flags.enable_internal_pullup = true;

    if (i2c_new_master_bus(&bus_cfg, &s_kb_bus) != ESP_OK) {
        ESP_LOGW(TAG, "i2c bus init failed (keyboard not present?)");
        return false;
    }

    i2c_device_config_t dev_cfg = {};
    dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
    dev_cfg.device_address      = KB_ADDR;
    dev_cfg.scl_speed_hz        = KB_FREQ_HZ;

    if (i2c_master_bus_add_device(s_kb_bus, &dev_cfg, &s_kb_dev) != ESP_OK) {
        ESP_LOGW(TAG, "add keyboard device failed");
        return false;
    }

    // 验证连接：读 INT_CFG(0x00)
    uint8_t cfg = 0;
    if (!kb_read_reg(0x00, &cfg)) {
        ESP_LOGW(TAG, "keyboard not responding at 0x%02X", KB_ADDR);
        i2c_master_bus_rm_device(s_kb_dev);
        s_kb_dev = nullptr;
        return false;
    }
    uint8_t ver = 0;
    kb_read_reg(REG_VERSION, &ver);
    ESP_LOGI(TAG, "Tab5 Keyboard found: INT_CFG=0x%02X version=0x%02X", cfg, ver);

    // 切 STRING 模式（直接输出 ASCII）
    kb_write_reg(REG_MODE, MODE_STRING);
    // 使能"字符串事件"中断位（bit2），否则 INT_STA 不会置位 → 轮询永远看不到事件
    kb_write_reg(REG_INT_CFG, 0x04);
    uint8_t int_cfg = 0;
    kb_read_reg(REG_INT_CFG, &int_cfg);
    ESP_LOGI(TAG, "INT_CFG set to 0x%02X (bit2=string event)", int_cfg);
    kb_write_reg(REG_EVENT_NUM, 0);   // 清事件队列
    kb_write_reg(REG_INT_STA, 0);     // 清中断状态

    s_kb_q = xQueueCreate(32, sizeof(uint16_t));
    if (!s_kb_q) return false;
    xTaskCreate(kb_poll_task, "kb_poll", 3072, nullptr, 4, nullptr);

    s_kb_ready = true;
    ESP_LOGI(TAG, "Tab5 Keyboard ready (STRING mode)");
    return true;
}

uint8_t HalEsp32::keyboardReadChar()
{
    uint16_t k = keyboardReadKey();
    return (k >= 0x20 && k < 0x7F) ? (uint8_t)k : 0;
}

bool HalEsp32::keyboardIsReady()
{
    return s_kb_ready;
}

// 非阻塞取一个字符；无字符返回 0
// 取一个"键值"：0x20~0x7E = ASCII 字符；0x101+ = 特殊键（见 KB_KEY_*）
uint16_t HalEsp32::keyboardReadKey()
{
    if (!s_kb_ready || !s_kb_q) return 0;
    uint16_t k = 0;
    if (xQueueReceive(s_kb_q, &k, 0) == pdTRUE) {
        return k;
    }
    return 0;
}

// ==================== LVGL 集成 ====================
// 把键盘字符注入 LVGL 的 keypad indev，由 LVGL 自动送给"聚焦的输入框"。
// 无需手动跟踪焦点：输入框只需加入默认 group（见 tool_kbd::attach）。
static uint8_t s_pending_release = 0;
static lv_group_t* s_kb_group = nullptr;

void* HalEsp32::keyboardGetGroup()
{
    return (void*)s_kb_group;
}

static void kb_lvgl_read_cb(lv_indev_t* indev, lv_indev_data_t* data)
{
    (void)indev;
    // 先补上一轮按键的"释放"事件，LVGL 才会认下一次按下
    if (s_pending_release) {
        data->key   = s_pending_release;
        data->state = LV_INDEV_STATE_RELEASED;
        s_pending_release = 0;
        return;
    }
    uint8_t c = GetHAL()->keyboardReadChar();
    if (c) {
        data->key   = c;
        data->state = LV_INDEV_STATE_PRESSED;
        s_pending_release = c;
        ESP_LOGI(TAG, "lvgl indev -> '%c'", c);
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void HalEsp32::keyboardRegisterLvglIndev(void* disp)
{
    // LVGL v9 没有全局默认 group，必须自建一个并显式绑定给 keypad indev，
    // 否则实体键盘的字符无处分发（这正是"按了没反应"的根因）
    s_kb_group = lv_group_create();
    lv_group_set_wrap(s_kb_group, false);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_KEYPAD);
    lv_indev_set_read_cb(indev, kb_lvgl_read_cb);
    lv_indev_set_group(indev, s_kb_group);
    if (disp) {
        lv_indev_set_display(indev, (lv_display_t*)disp);
    }
    ESP_LOGI(TAG, "keyboard LVGL keypad indev registered (group=%p)", (void*)s_kb_group);
}
