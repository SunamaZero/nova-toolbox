// Tab5 工具箱 — 日志页
// 设备端查看最近日志；联网时 PC 也能远程拉（http://<ip>:8090/log）
//
// 布局与串口页同构：
//   左列 —— 日志区（滚动容器，逐行上色）
//   右列 —— 状态 / 等级过滤 / 自动滚动 / 缓冲 / 远程地址
//           + 跳到最新 + 清空·关闭

#pragma once

#include "toolbox_windows.h"
#include "toolbox_theme.h"
#include <lvgl.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <smooth_ui_toolkit.h>
#include <string>

namespace launcher_view {

class LogToolWindow : public ui::Window {
public:
    LogToolWindow();
    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

private:
    void rebuild();                          // 按当前等级过滤整表重建
    void append_line(const std::string& s);  // 追加一行（按等级上色）
    void prune();                            // 只留最近 N 行，防止对象越堆越多
    void refresh_status();
    bool line_match(const std::string& s) const;

    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Container> _log_box;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Label>     _status_label;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>    _row_level;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>    _row_auto;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>    _btn_latest;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>    _btn_clear;
    std::unique_ptr<smooth_ui_toolkit::lvgl_cpp::Button>    _btn_close;
    lv_obj_t* _status_dot = nullptr;
    lv_obj_t* _info_ip    = nullptr;   // 右列"远程"信息行（纯展示，不是按钮）

    int      _rendered    = 0;    // 已渲染到第几条（绝对序号，用于增量追加）
    int      _filter      = 0;    // 0=全部 1=信息 2=警告 3=错误
    bool     _auto_scroll = true; // 贴底时自动跟随最新
    uint32_t _tick        = 0;
};

}  // namespace launcher_view
