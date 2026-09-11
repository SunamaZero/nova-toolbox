/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal/hal_esp32.h"
#include <mooncake_log.h>
#include <vector>
#include <driver/gpio.h>
#include <memory>
#include "driver/uart.h"
#include "esp_log.h"

#define TAG "hal_rs485"

// RS485
#define TAB5_RS485_BUF_SIZE (127)
// Timeout threshold for UART = number of symbols (~10 tics) with unchanged state on receive pin
#define TAB5_RS485_READ_TOUT        (3)  // 3.5T * 8 = 28 ticks, TOUT=3 -> ~24..33 ticks
#define TAB5_RS485_PACKET_READ_TICS (100 / portTICK_PERIOD_MS)
static uart_port_t tab5_rs485_uart_num = UART_NUM_1;

#define TAB5_SYS_RS485_TX_PIN 20
#define TAB5_SYS_RS485_RX_PIN 21
#define TAB5_SYS_RS485_DE_PIN 34

static void tab5_rs485_echo_send(const int port, uint8_t* str, uint8_t length)
{
    if (uart_write_bytes((uart_port_t)port, str, length) != length) {
        ESP_LOGE(TAG, "Send data critical failure.");
        // add your code to handle sending failure here
        abort();
    }
}

static void _rs485_test_task(void* param)
{
    // Allocate buffers for UART
    uint8_t* data  = (uint8_t*)malloc(TAB5_RS485_BUF_SIZE);
    uint8_t* wdata = (uint8_t*)malloc(TAB5_RS485_BUF_SIZE);

    int count               = 0;
    uint64_t last_send_time = 0;
    while (1) {
        // Read data from UART
        memset(data, 0, TAB5_RS485_BUF_SIZE);
        int len = uart_read_bytes(tab5_rs485_uart_num, data, TAB5_RS485_BUF_SIZE, TAB5_RS485_PACKET_READ_TICS);

        // Write data back to UART
        if (len > 0) {
            // printf("Received [%u] : %s\n", len, data);
            // tab5_rs485_echo_send(tab5_rs485_uart_num, data, len);

            std::lock_guard<std::mutex> lock(GetHAL()->uartMonitorData.mutex);
            for (int i = 0; i < len; i++) {
                GetHAL()->uartMonitorData.rxQueue.push(data[i]);
            }
            while (GetHAL()->uartMonitorData.rxQueue.size() > 4096) {
                GetHAL()->uartMonitorData.rxQueue.pop();
            }
        }

        {
            std::lock_guard<std::mutex> lock(GetHAL()->uartMonitorData.mutex);
            while (!GetHAL()->uartMonitorData.txQueue.empty()) {
                uint8_t txData = GetHAL()->uartMonitorData.txQueue.front();
                GetHAL()->uartMonitorData.txQueue.pop();
                uart_write_bytes(tab5_rs485_uart_num, &txData, 1);
            }
        }

        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

void HalEsp32::rs485_init()
{
    mclog::tagInfo(TAG, "rs485 init");

    uart_config_t uart_config;
    uart_config.baud_rate           = 115200;
    uart_config.data_bits           = UART_DATA_8_BITS;
    uart_config.parity              = UART_PARITY_DISABLE;
    uart_config.stop_bits           = UART_STOP_BITS_1;
    uart_config.flow_ctrl           = UART_HW_FLOWCTRL_DISABLE;
    uart_config.rx_flow_ctrl_thresh = 122;
    uart_config.source_clk          = UART_SCLK_DEFAULT;

    // Install UART driver (we don't need an event queue here)
    // In this example we don't even use a buffer for sending data.
    ESP_ERROR_CHECK(uart_driver_install(tab5_rs485_uart_num, TAB5_RS485_BUF_SIZE * 2, 0, 0, NULL, 0));

    // Configure UART parameters
    ESP_ERROR_CHECK(uart_param_config(tab5_rs485_uart_num, &uart_config));

    ESP_LOGI(TAG, "UART set pins, mode and install driver.");

    // Set UART pins as per KConfig settings
    ESP_ERROR_CHECK(uart_set_pin(tab5_rs485_uart_num, TAB5_SYS_RS485_TX_PIN, TAB5_SYS_RS485_RX_PIN,
                                 TAB5_SYS_RS485_DE_PIN, UART_PIN_NO_CHANGE));

    // Set RS485 half duplex mode
    ESP_ERROR_CHECK(uart_set_mode(tab5_rs485_uart_num, UART_MODE_RS485_HALF_DUPLEX));

    // Set read timeout of UART TOUT feature
    ESP_ERROR_CHECK(uart_set_rx_timeout(tab5_rs485_uart_num, TAB5_RS485_READ_TOUT));

    xTaskCreate(_rs485_test_task, "rs485", 2000, NULL, 5, NULL);
}

// ==================== 串口参数配置 ====================
static uint32_t s_rs485_baud = 115200;

bool HalEsp32::setRs485Baudrate(uint32_t baud)
{
    esp_err_t e = uart_set_baudrate(tab5_rs485_uart_num, baud);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "set baudrate %lu failed: %s", (unsigned long)baud, esp_err_to_name(e));
        return false;
    }
    s_rs485_baud = baud;
    ESP_LOGI(TAG, "baudrate -> %lu", (unsigned long)baud);
    return true;
}

uint32_t HalEsp32::getRs485Baudrate()
{
    return s_rs485_baud;
}

// ==================== 串口完整参数配置 ====================
// data_bits: 5/6/7/8   parity: 0=none 2=even 3=odd   stop_bits: 1=1, 1.5=1.5, 2=2
bool HalEsp32::setRs485Config(uint32_t baud, int data_bits, int parity, int stop_bits)
{
    uart_config_t cfg   = {};
    cfg.baud_rate       = baud;
    cfg.data_bits       = (data_bits >= 5 && data_bits <= 8) ? (uart_word_length_t)(data_bits - 5) : UART_DATA_8_BITS;
    cfg.parity          = (uart_parity_t)parity;
    // stop bits 枚举：1=1bit, 2=1.5bit, 3=2bit
    cfg.stop_bits       = (stop_bits == 2) ? UART_STOP_BITS_2 : ((stop_bits == 1) ? UART_STOP_BITS_1 : UART_STOP_BITS_1_5);
    cfg.flow_ctrl       = UART_HW_FLOWCTRL_DISABLE;
    cfg.rx_flow_ctrl_thresh = 122;
    cfg.source_clk      = UART_SCLK_DEFAULT;

    esp_err_t e = uart_param_config(tab5_rs485_uart_num, &cfg);
    if (e != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(e));
        return false;
    }
    ESP_LOGI(TAG, "uart cfg: baud=%lu data=%d parity=%d stop=%d", (unsigned long)baud, data_bits, parity, stop_bits);
    return true;
}
