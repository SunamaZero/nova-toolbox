/*
 * Nova Toolbox for M5Stack Tab5
 * 软键盘 helper：给 TextArea 挂 LVGL 内置键盘（lv_keyboard）
 * 用法：tool_kbd::attach(textarea) —— 点击该输入框时自动弹出键盘
 */
#pragma once
#include <lvgl.h>

namespace tool_kbd {

/**
 * @brief 给 textarea 绑定键盘：点击获得焦点时弹出 lv_keyboard
 * 键盘显示期间点其他绑定过的 textarea 会切换输入目标
 */
void attach(lv_obj_t* textarea);

/**
 * @brief 把实体键盘的一个字符喂给"最后聚焦的输入框"
 *        （绕开 LVGL keypad indev 路由，直接 add_char，最可控）
 * @return true = 已消费；false = 当前没有聚焦的输入框
 */
bool feedChar(uint8_t c);

// 特殊键内部编码（与驱动 hal_keyboard.cpp 的 KB_KEY_* 一致）
constexpr uint16_t KeyEnter     = 0x101;
constexpr uint16_t KeyBackspace = 0x102;
constexpr uint16_t KeyDelete    = 0x103;
constexpr uint16_t KeyTab       = 0x104;
constexpr uint16_t KeyEsc       = 0x105;
constexpr uint16_t KeySpace     = 0x106;

/**
 * @brief 喂一个键值给聚焦输入框（ASCII 或特殊键码）
 * @return true = 已消费
 */
bool feedKey(uint16_t k);

/**
 * @brief 是否有输入框正处于聚焦状态（供 UI 显示提示）
 */
bool hasFocus();

/**
 * @brief 收起屏幕软键盘（不影响输入框焦点）
 *        实体键盘敲键时自动调用 —— 有物理键盘就没必要占半屏
 */
void hide();

/**
 * @brief 屏幕软键盘当前是否可见
 */
bool isVisible();

}  // namespace tool_kbd
