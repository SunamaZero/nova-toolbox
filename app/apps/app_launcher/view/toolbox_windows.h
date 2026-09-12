/*
 * Nova Toolbox for M5Stack Tab5
 * 工具窗口声明（无 IDF 依赖部分：串口 + 占位）
 * 网络工具（UDP/TCP/HTTP/MQTT/WiFi）在 net_tools.h（IDF 条件保护）
 */
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <lvgl.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/ui/window.h>

namespace launcher_view {

/**
 * @brief 串口调试工具窗口（RS485 UART 监视：HEX/ASCII 显示 + 预设发送）
 */
// ---- 串口工具：左列报文/发送，右列端口与串口设置 ----
class SerialToolWindow : public ui::Window {
public:
    SerialToolWindow();

    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    // 显示条目：RX 字节流 或 一条 TX 记录（保证 ASCII/HEX 切换后内容不丢）
    struct Item {
        bool                     tx = false;
        std::vector<uint8_t>     bytes;
        std::string              text;
    };

    void clearRx();
    void sendText(const char* text);
    void render();
    void setHexMode(bool hex);
    void setOpened(bool open);
    void applyUartConfig();
    void refreshRows();
    bool portReady() const;
    void addSystemLine(const char* text);

    int  _baud_idx = 4;   // 115200
    int  _data_idx = 3;   // 8 bit
    int  _par_idx  = 0;   // N
    int  _stop_idx = 0;   // 1
    int  _port_idx = 1;   // 默认 USB 串口（板载 RS485 手边没转换器时一键切回）

    bool _hex_mode = false;
    bool _opened   = false;
    bool _usb_ready   = false;   // USB 适配器已真正打开（异步结果）
    bool _usb_pending = false;   // 正在后台找 USB 设备

    std::vector<Item> _items;
    size_t            _rx_bytes = 0;      // 累计 RX 字节（裁剪用）
    static constexpr size_t _RX_MAX = 1024;

    // 左列
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _tx_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>   _btn_send;

    // 右列
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label>  _status_label;
    lv_obj_t*                                            _status_dot = nullptr;   // 状态灯（画出来的圆点，不依赖字体）
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _row_port;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _row_baud;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _row_data;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _row_stop;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _row_par;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _row_disp;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_open;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_close;
};

/**
 * @brief 占位工具窗口（未实现工具显示"开发中"）
 */
class PlaceholderToolWindow : public ui::Window {
public:
    PlaceholderToolWindow(const char* title);

    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    std::string _title;
    uint32_t _time_count = 0;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _dev_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_close;
};

}  // namespace launcher_view
