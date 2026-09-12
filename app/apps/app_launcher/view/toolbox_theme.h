/*
 * Nova Toolbox 设计系统 v2（Field Armor）
 *
 * 设计原则（照 Slave_I tokens.hpp 的体系化做法）：
 *   1. 一切尺寸走 4pt 网格：坐标/宽高必须是 4 的倍数
 *   2. 间距只用阶梯值：4 / 8 / 16 / 24 / 32
 *   3. 圆角只用阶梯值：6 / 12 / 18
 *   4. 可点击目标高度 >= 48（触摸）
 *   5. 颜色只用语义 token，禁止硬编码 hex
 *   6. 页面结构固定三段：标题带 / 内容卡 / 操作条
 */
#pragma once
#include <cstdint>
#include <lvgl.h>

// 字体（app/apps/app_launcher/view/fonts/*.c，含 ASCII + FontAwesome 图标）
extern "C" {
extern const lv_font_t ibm_plex_mono_14;
extern const lv_font_t ibm_plex_mono_16;
extern const lv_font_t ibm_plex_mono_18;
extern const lv_font_t ibm_plex_mono_20;
extern const lv_font_t ibm_plex_mono_32;
// 完整中文字库（GB2312 一级 3755 字 + 标点，由官方 SimSun.woff 经 lv_font_conv 生成）
extern const lv_font_t tb_cn_16;
}

namespace tb {

// ==================== 色彩 token（Field Armor）====================
// 层级：bg < surface < raised < border —— 越靠上的元素越亮
inline uint32_t bg()       { return 0x14120D; }  // 窗口底
inline uint32_t surface()  { return 0x1E1A12; }  // 卡片/面板/输入框底
inline uint32_t raised()   { return 0x2A261C; }  // 按钮/浮起元素
inline uint32_t border()   { return 0x3A3326; }  // 分隔线/描边
inline uint32_t text()     { return 0xE8E2D2; }  // 主文字（奶油白）
inline uint32_t textDim()  { return 0xA39B84; }  // 次文字（风化棕）
inline uint32_t textMute() { return 0x6B6455; }  // 弱文字（禁用/提示）
inline uint32_t accent()   { return 0xD4A017; }  // 强调（琥珀金）——主操作/标题
inline uint32_t success()  { return 0x9BB24A; }  // 成功/已连接
inline uint32_t warning()  { return 0xCE8E1E; }  // 警告/连接中
inline uint32_t danger()   { return 0xB8472F; }  // 危险/关闭
inline uint32_t info()     { return 0x6E7A45; }  // 信息（暗橄榄）
inline uint32_t neutral()  { return 0x3A3326; }  // 中性按钮（清空/重置等次要操作）
inline uint32_t sunken()   { return 0x0E0C08; }  // 凹陷底（日志/终端内容区）
inline uint32_t successDim() { return 0x22301A; }  // 成功色暗调（状态胶囊底）

// ---- 工具页骨架（所有工具页共用，保证同构）----
// 之前各页各写一套坐标（UART 按钮间距 185、UDP 225），放在一起就不像一套产品。
constexpr int ToolTitleY = 12;                 // 标题行
constexpr int ToolRowY   = 56;                 // 配置行 Y
constexpr int ToolRowH   = 48;                 // 配置行控件统一高度
constexpr int ToolRxY    = 112;                // 接收区 Y
constexpr int ToolRxH    = 332;                // 接收区高
constexpr int ToolRxW    = 1132;               // 接收区宽（内容区宽 - 2*24）
constexpr int ToolTxY    = 456;                // 发送行 Y
constexpr int ToolTxH    = 52;                 // 发送行高
constexpr int ToolPadX   = 24;                 // 页面左右内边距

// 底部操作条：按钮等宽等距，Close 固定最右
constexpr int BtnW        = 168;
constexpr int BtnGap      = 16;
constexpr int BtnX0       = 24;
constexpr int BtnBottomY  = -24;               // 距页面底
constexpr int BtnXRight   = 1180 - ToolPadX - BtnW;   // 988
// 没有"发送行"的工具页（TCP/HTTP），面板直接填到按钮行上方 —— 否则中间空一大片
constexpr int ToolRxHNoTx = 414;
inline int btnX(int i) { return BtnX0 + i * (BtnW + BtnGap); }   // 24,208,392,576,760

// ==================== 间距 token（4pt 网格）====================
constexpr int SpaceXs = 4;
constexpr int SpaceSm = 8;
constexpr int SpaceMd = 16;
constexpr int SpaceLg = 24;
constexpr int SpaceXl = 32;

// ==================== 圆角 token ====================
constexpr int RadiusSm = 6;   // 输入框/小标签
constexpr int RadiusMd = 12;  // 卡片/按钮
constexpr int RadiusLg = 18;  // 面板/窗口

// ==================== 触摸规范 ====================
constexpr int TouchMin = 96;        // 可点击目标最小高度（Tab5: 294PPI → 48px 仅 4.1mm，手指需 7-9mm ≈ 81-104px）
constexpr int TouchGap = 8;         // 相邻触摸目标最小间距

// ==================== 布局尺寸（Tab5 1280x720）====================
constexpr int ScreenW   = 1280;
constexpr int ScreenH   = 720;
constexpr int TopBarH   = 52;
constexpr int NavRailW  = 76;
constexpr int TaskBarH  = 34;
constexpr int ContentW  = ScreenW - NavRailW - SpaceMd - SpaceXs * 2;  // 1180
constexpr int ContentH  = ScreenH - TopBarH - TaskBarH - SpaceXs * 3;  // 622

// 页面三段式：标题带 / 内容卡 / 操作条
constexpr int PagePad     = SpaceLg;   // 内容区四周留白 24
constexpr int TitleBandH  = 48;        // 标题带高度
constexpr int ActionBarH  = 56;        // 操作条高度

// ==================== 字体阶梯 ====================
inline const lv_font_t* fontSm()    { return &tb_cn_16; }  // 小字（中文）
inline const lv_font_t* fontBody()  { return &tb_cn_16; }  // 正文/按钮（中文）
inline const lv_font_t* fontMid()   { return &tb_cn_16; }
inline const lv_font_t* fontTitle() { return &tb_cn_16; }  // 标题（中文，靠颜色区分层次）
inline const lv_font_t* fontBig()   { return &tb_cn_16; }
// 导航标签：14px 等宽拉丁（窄，保证 SETTINGS 这类长标签不截断）
inline const lv_font_t* fontLabel() { return &ibm_plex_mono_14; }

// ==================== 通用样式助手 ====================
// 卡片：surface 底 + border 描边 + RadiusMd
inline void styleCard(lv_obj_t* o)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(surface()), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, RadiusMd, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border()), 0);
    lv_obj_set_style_pad_all(o, SpaceMd, 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
}

// 输入框：surface 底 + border + RadiusSm + 内边距 12
// 只读显示区（报文 / 日志）：去掉"点击获得焦点"。
// 否则触摸点一下就会聚焦 → 出现光标、状态变化，看着像可以输入。
// 保留 CLICKABLE，手指拖动照样能滚。
inline void makeReadOnly(lv_obj_t* o)
{
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_state(o, LV_STATE_FOCUSED);
    lv_group_remove_obj(o);   // 不在实体键盘的 tab 焦点链里
}

inline void styleInput(lv_obj_t* o)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(surface()), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, RadiusSm, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border()), 0);
    lv_obj_set_style_pad_hor(o, SpaceMd - 4, 0);
    // 垂直居中：靠 pad_top 把单行文本推到框的垂直中间（不设就贴顶边）
    // 水平：左对齐 —— 输入框内容应按阅读习惯从左起，居中会让人以为没对齐
    lv_obj_set_style_text_align(o, LV_TEXT_ALIGN_LEFT, 0);
    // 必须先把刚设的尺寸真正应用，否则 get_height 拿到的是样式默认高（偏大），
    // 算出的 pad_top 会把文字推到框外、压住下边框。
    lv_obj_update_layout(o);
    const lv_font_t* f = lv_obj_get_style_text_font(o, LV_PART_MAIN);
    int line_h = f ? lv_font_get_line_height(f) : 20;
    int h = lv_obj_get_height(o);
    int pad = (h - line_h) / 2;
    lv_obj_set_style_pad_top(o, pad > 0 ? pad : 0, 0);
    lv_obj_set_style_pad_bottom(o, 0, 0);
    // 文字：主文字色（对比度 13.4:1）
    lv_obj_set_style_text_color(o, lv_color_hex(text()), 0);
    lv_obj_set_style_text_color(o, lv_color_hex(textDim()), LV_PART_TEXTAREA_PLACEHOLDER);
    // 光标：LVGL 把光标画成 LV_PART_CURSOR 的矩形（lv_textarea.c 用 bg_opa 判定是否画），
    // 只设 border_color + border_width=0 + bg_opa=0 → 光标完全看不见。
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_CURSOR);
    lv_obj_set_style_bg_color(o, lv_color_hex(accent()), LV_PART_CURSOR);
    lv_obj_set_style_border_width(o, 0, LV_PART_CURSOR);
}

// 主操作按钮：accent 描边 + raised 底
inline void stylePrimary(lv_obj_t* o)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(raised()), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, RadiusMd, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(accent()), 0);
}

// 次操作按钮：raised 底 + border 描边
inline void styleSecondary(lv_obj_t* o)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(raised()), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, RadiusMd, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_border_color(o, lv_color_hex(border()), 0);
}

// 危险按钮
inline void styleDanger(lv_obj_t* o)
{
    lv_obj_set_style_bg_color(o, lv_color_hex(danger()), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, RadiusMd, 0);
    lv_obj_set_style_border_width(o, 0, 0);
}

// 标题文字
inline void styleTitleText(lv_obj_t* o)
{
    lv_obj_set_style_text_color(o, lv_color_hex(accent()), 0);
    lv_obj_set_style_text_font(o, fontTitle(), 0);
}

// 次要文字
inline void styleDimText(lv_obj_t* o)
{
    lv_obj_set_style_text_color(o, lv_color_hex(textDim()), 0);
    lv_obj_set_style_text_font(o, fontBody(), 0);
}

}  // namespace tb
