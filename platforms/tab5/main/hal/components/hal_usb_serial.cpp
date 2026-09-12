/*
 * HAL 桥接层：把 usb_serial 组件（USB 主机 VCP 驱动）接到 HAL 的串口抽象上
 *
 * 分工：
 *   usb_serial 组件  —— 只管 USB 侧的枚举/打开/收发（官方驱动）
 *   本文件           —— 收数据入队、状态透传、参数换算（HAL 语义）
 *   工具页（UI）     —— 只认 HAL，不关心底下是板载 RS485 还是 USB 适配器
 *
 * 线程边界：on_usb_rx 在 USB 驱动任务上下文执行 → 只做入队，绝不碰 UI。
 */
#include "hal/hal_esp32.h"

#include "esp_log.h"
#include "usb_serial.h"

static const char* TAG = "hal-usb-ser";

// RX 队列上限，防止上游比 UI 快太多把内存吃光
static constexpr size_t kRxMax = 8192;

static void on_usb_rx(const uint8_t* data, size_t len)
{
    if (data == nullptr || len == 0) {
        return;
    }
    auto& q = GetHAL()->usbSerialData;
    std::lock_guard<std::mutex> lock(q.mutex);
    for (size_t i = 0; i < len; ++i) {
        q.rxQueue.push(data[i]);
    }
    while (q.rxQueue.size() > kRxMax) {
        q.rxQueue.pop();
    }
}

static void on_usb_event(int event)
{
    // 1 = 已连接可用；2 = 已拔出
    ESP_LOGI(TAG, "event=%d", event);
}

bool HalEsp32::usbSerialOpenAsync(int channel)
{
    // 多串口芯片（CH344 等）每路 UART 是独立 CDC-ACM 接口，接口号 0/2/4/6
    static const uint8_t itf_of_channel[4] = {0, 2, 4, 6};
    if (channel < 1 || channel > 4) {
        return false;
    }
    // 排障三连（省得每次靠猜"是没插好还是没识别"）：
    //   检测=无 + 5V=关 → 开关面板里把 USB-A 5V 关了
    //   检测=无 + 5V=开 → 适配器没插好 / 线材问题 / 供电不足
    //   检测=有        → 供电和物理连接没问题，问题在协议/驱动识别
    ESP_LOGW(TAG, "USB-A 口检测=%s, USB-A 5V=%s",
             GetHAL()->usbADetect() ? "有设备" : "无",
             getUsb5vEnable() ? "开" : "关");

    usb_serial::init(on_usb_rx, on_usb_event);
    return usb_serial::open_async(itf_of_channel[channel - 1]);
}

void HalEsp32::usbSerialClose()
{
    usb_serial::close();
    auto& q = GetHAL()->usbSerialData;
    std::lock_guard<std::mutex> lock(q.mutex);
    while (!q.rxQueue.empty()) {
        q.rxQueue.pop();
    }
}

int HalEsp32::usbSerialState()
{
    return usb_serial::state();
}

bool HalEsp32::usbSerialTakeDisconnected()
{
    return usb_serial::take_disconnected();
}

bool HalEsp32::usbSerialSetLineCoding(uint32_t baud, int data_bits, int parity, int stop_bits)
{
    // 注意编号体系不同：
    //   板载 RS485（uart_param_config）：0=none 2=even 3=odd
    //   USB CDC line coding           ：0=none 1=odd  2=even
    int usb_parity = 0;
    switch (parity) {
    case 2: usb_parity = 2; break;   // even → even
    case 3: usb_parity = 1; break;   // odd  → odd
    default: usb_parity = 0; break;  // none
    }
    return usb_serial::set_line_coding(baud, data_bits, usb_parity, stop_bits);
}

void HalEsp32::usbSerialSend(const std::string& msg, bool newLine)
{
    std::string out = msg;
    if (newLine) {
        out += '\n';
    }
    if (!usb_serial::send(reinterpret_cast<const uint8_t*>(out.data()), out.size())) {
        ESP_LOGW(TAG, "发送失败（未打开或设备已拔出）");
    }
}
