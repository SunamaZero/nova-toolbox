/*
 * USB 串口服务实现 —— 薄封装官方 usb_host_vcp / usb_host_*_vcp 驱动
 *
 * 设计要点：
 *  1. VCP::open() 会阻塞等待设备出现 → 必须放独立任务，不能占 UI 线程
 *  2. 波特率等参数设在**适配器**上（line coding），不是 ESP32 的 UART
 *  3. 拔插要能被上层感知（take_disconnected），否则工具会卡在"已打开"的假象里
 */
#include "usb_serial.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <new>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "usb/vcp.hpp"
#include "usb/vcp_ch34x.hpp"
#include "usb/vcp_cp210x.hpp"
#include "usb/vcp_ftdi.hpp"

using namespace esp_usb;

// ---------------------------------------------------------------------------
// 通用 CDC-ACM 设备驱动（沁恒 WCH 新系列）
//
// 依据（WCH 官方 ch343ser_linux 仓库 README + CH344 数据手册）：
//   CH342/CH343/CH344/CH346/CH347/CH910x 等**完全符合 CDC-ACM 标准**，
//   用标准 CDC-ACM 驱动即可通信。
//   注意：它们**不能**用 CH340/CH341 那套 vendor 协议（寄存器/波特率算法不同），
//   所以不能简单把 PID 塞进 CH34x 驱动的表里 —— 必须走标准 CDC-ACM 通路。
//
// PID 表来源：WCH 官方 Linux 驱动 ch343.c 的 id table（实锤）
//   CH344 = 0x55D5 / CH343 = 0x55D3 / CH9102 = 0x55D4 / CH347 = 0x55DB / CH342 = 0x55D2
// ---------------------------------------------------------------------------
class WchCdcAcm : public CdcAcmDevice {
public:
    WchCdcAcm(uint16_t pid, const cdc_acm_host_device_config_t* dev_config, uint8_t interface_idx = 0)
    {
        const esp_err_t err = this->open(vid, pid, interface_idx, dev_config);   // 标准 CDC-ACM，不是 vendor specific
        if (err != ESP_OK) {
            throw err;   // VCP::open 会接住 esp_err_t
        }
    }

    static constexpr uint16_t               vid  = 0x1A86;   // 南京沁恒
    static constexpr std::array<uint16_t, 5> pids = {0x55D5, 0x55D3, 0x55D4, 0x55DB, 0x55D2};
};

static const char* TAG = "usb-serial";

namespace {

using usb_serial::EventCallback;   // 这两个类型在 usb_serial 命名空间里
using usb_serial::RxCallback;

RxCallback    s_rx  = nullptr;
EventCallback s_ev  = nullptr;

CdcAcmDevice* s_dev          = nullptr;
volatile int  s_state        = usb_serial::kIdle;
volatile bool s_disconnected = false;
TaskHandle_t  s_task         = nullptr;
uint8_t       s_itf          = 0;      // 要打开的 USB 接口号
bool          s_drv_registered = false;

// 默认 115200 8N1，打开后由 set_line_coding 覆盖
cdc_acm_line_coding_t s_lc = {115200, 0, 0, 8};

bool on_data(const uint8_t* data, size_t len, void* /*arg*/)
{
    if (s_rx != nullptr && data != nullptr && len > 0) {
        s_rx(data, len);
    }
    return true;   // true = 数据已处理，驱动可以复用缓冲
}

void on_event(const cdc_acm_host_dev_event_data_t* ev, void* /*ctx*/)
{
    if (ev == nullptr) {
        return;
    }
    switch (ev->type) {
    case CDC_ACM_HOST_DEVICE_DISCONNECTED:
        ESP_LOGW(TAG, "设备已拔出");
        s_disconnected = true;
        s_state        = usb_serial::kFailed;
        if (s_ev) s_ev(2);
        break;
    case CDC_ACM_HOST_ERROR:
        ESP_LOGW(TAG, "USB 错误 %d", ev->data.error);
        break;
    default:
        break;
    }
}

void open_task(void* /*arg*/)
{
    cdc_acm_host_device_config_t cfg = {};
    cfg.connection_timeout_ms        = 4000;   // 最多等 4 秒找设备
    cfg.out_buffer_size              = 512;
    cfg.in_buffer_size               = 512;
    cfg.event_cb                     = on_event;
    cfg.data_cb                      = on_data;
    cfg.user_arg                     = nullptr;

    CdcAcmDevice* dev = nullptr;
    // 这个组件要求开 CONFIG_COMPILER_CXX_EXCEPTIONS
    try {
        dev = VCP::open(&cfg, s_itf);   // 按 VID/PID 自动挑驱动；s_itf 选第几路
    } catch (const std::bad_alloc&) {
        ESP_LOGE(TAG, "内存不足");
    } catch (...) {
        ESP_LOGE(TAG, "打开时抛出异常");
    }

    if (dev != nullptr) {
        if (dev->line_coding_set(&s_lc) != ESP_OK) {
            ESP_LOGW(TAG, "line coding 下发失败（设备可能不支持）");
        }
        s_dev   = dev;
        s_state = usb_serial::kOpen;
        ESP_LOGI(TAG, "USB 串口已打开");
        if (s_ev) s_ev(1);
    } else {
        s_state = usb_serial::kFailed;
        ESP_LOGW(TAG, "没找到受支持的 USB 串口设备");
    }

    s_task = nullptr;
    vTaskDelete(nullptr);
}

}  // namespace

namespace usb_serial {

void init(RxCallback rx, EventCallback ev)
{
    s_rx = rx;
    s_ev = ev;
}

bool open_async(uint8_t interface_idx)
{
    if (s_state == kOpen || s_state == kOpening) {
        return true;
    }
    if (s_task != nullptr) {
        return false;
    }

    if (!s_drv_registered) {
        // 官方用法：先把驱动注册进 VCP 服务，之后 VCP::open 自动挑
        VCP::register_driver<WchCdcAcm>();   // WCH 新系列（CH343/344/9102/347/342）走标准 CDC-ACM
        VCP::register_driver<CH34x>();       // CH340/CH341（vendor 协议）
        VCP::register_driver<CP210x>();
        VCP::register_driver<FT23x>();
        s_drv_registered = true;
        ESP_LOGI(TAG, "已注册驱动: WCH-CDC(CH343/344/9102/347) / CH340-341 / CP210x / FT23x");
    }

    s_disconnected = false;
    s_itf          = interface_idx;
    // 独立任务：VCP::open 阻塞等设备，不能占 UI 线程
    s_state = kOpening;
    if (xTaskCreate(open_task, "usb_ser_open", 4096, nullptr, 5, &s_task) != pdTRUE) {
        s_state = kFailed;
        return false;
    }
    return true;
}

void close(void)
{
    if (s_dev != nullptr) {
        delete s_dev;   // 析构里会 close 句柄
        s_dev = nullptr;
    }
    s_state = kIdle;
}

int state(void)
{
    return s_state;
}

bool take_disconnected(void)
{
    const bool v   = s_disconnected;
    s_disconnected = false;
    return v;
}

bool set_line_coding(uint32_t baud, int data_bits, int parity, int stop_bits)
{
    s_lc.dwDTERate   = baud;
    s_lc.bDataBits   = (uint8_t)data_bits;
    s_lc.bParityType = (uint8_t)parity;
    s_lc.bCharFormat = (stop_bits >= 2) ? 2 : 0;   // 0=1 位, 2=2 位

    if (s_dev == nullptr) {
        return false;   // 未打开：只记住参数，打开时下发
    }
    const esp_err_t e = s_dev->line_coding_set(&s_lc);
    if (e != ESP_OK) {
        ESP_LOGW(TAG, "line_coding_set 失败: %s", esp_err_to_name(e));
        return false;
    }
    return true;
}

bool send(const uint8_t* data, size_t len)
{
    if (s_dev == nullptr || data == nullptr || len == 0) {
        return false;
    }
    // tx_blocking 形参非 const，分块拷贝下发（单次不超过 out_buffer_size）
    uint8_t      buf[256];
    size_t       off = 0;
    while (off < len) {
        const size_t n = (len - off > sizeof(buf)) ? sizeof(buf) : (len - off);
        memcpy(buf, data + off, n);
        const esp_err_t e = s_dev->tx_blocking(buf, n, 200);
        if (e != ESP_OK) {
            ESP_LOGW(TAG, "tx 失败: %s", esp_err_to_name(e));
            return false;
        }
        off += n;
    }
    return true;
}

}  // namespace usb_serial
