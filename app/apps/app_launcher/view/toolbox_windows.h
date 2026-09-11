/*
 * Nova Toolbox for M5Stack Tab5
 * 工具窗口声明（无 IDF 依赖部分：串口 + 占位）
 * 网络工具（UDP/TCP/HTTP/MQTT/WiFi）在 net_tools.h（IDF 条件保护）
 */
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <lvgl.h>
#include <smooth_ui_toolkit.h>
#include <smooth_lvgl.h>
#include <apps/utils/ui/window.h>

namespace launcher_view {

/**
 * @brief 串口调试工具窗口（RS485 UART 监视：HEX/ASCII 显示 + 预设发送）
 */
class SerialToolWindow : public ui::Window {
public:
    SerialToolWindow();

    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    void clearRx();
    void sendText(const char* text);
    void toggleHex();

    bool _hex_mode = false;
    std::string _hex_buf;
    int _hex_col = 0;
    uint32_t _time_count = 0;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _title_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _rx_panel;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::TextArea> _tx_input;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_baud;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_data;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_par;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_stop;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label> _cfg_label;
    int _baud_idx = 4;  // 115200
    int _data_idx = 3;  // 8 bit
    int _par_idx  = 0;  // none
    int _stop_idx = 0;  // 1 bit
    void applyUartConfig();
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_hex;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_send_tx;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_send_at;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_send_hello;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button> _btn_send_scan;
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
