/*
 * USB 串口服务（外接 USB↔485/232 适配器）
 *
 * 基于官方组件栈：
 *   usb_host_vcp        —— VCP 服务（按 VID/PID 自动选驱动）
 *   usb_host_cdc_acm    —— CDC-ACM 设备驱动
 *   usb_host_ch34x_vcp  —— CH340/CH341
 *   usb_host_cp210x_vcp —— CP2102/CP2104
 *   usb_host_ftdi_vcp   —— FT232
 *
 * 注意：这是一层薄封装，真正的收发/枚举都由官方驱动做，不要在这里重造。
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace usb_serial {

enum State {
    kIdle = 0,   // 未打开
    kOpening,    // 正在找设备（异步进行）
    kOpen,       // 已打开，可收发
    kFailed,     // 打开失败（没插设备 / 不支持的芯片）
};

/** 收到数据。在 USB 驱动任务上下文调用，实现方自行保证线程安全。 */
typedef void (*RxCallback)(const uint8_t* data, size_t len);

/** 事件：1 = 已连接可用；2 = 已断开 */
typedef void (*EventCallback)(int event);

/** 注册回调（只需一次） */
void init(RxCallback rx, EventCallback ev);

/**
 * 异步打开：VCP::open 会阻塞等待设备出现，绝不能占 UI 线程
 * @param interface_idx USB 接口号。多串口芯片（如 CH344）每个 UART 是独立接口：
 *                      CH344 的 4 路分别是 0 / 2 / 4 / 6
 */
bool open_async(uint8_t interface_idx);

/** 关闭并释放设备 */
void close(void);

/** 当前状态 */
int state(void);

/** 取走"设备刚被拔出"标志（取完清零），供 UI 提示并自动关闭 */
bool take_disconnected(void);

/**
 * 串口参数设在**适配器**上（USB CDC line coding），
 * 不是 ESP32 自己的 UART —— 所以这里不能用 uart_param_config。
 * @param parity 0=none 1=odd 2=even（与板载 RS485 的编号不同，注意换算）
 */
bool set_line_coding(uint32_t baud, int data_bits, int parity, int stop_bits);

/** 发送数据（内部按 out_buffer_size 分块） */
bool send(const uint8_t* data, size_t len);

}  // namespace usb_serial
