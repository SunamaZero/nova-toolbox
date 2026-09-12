/*
 * Nova Toolbox shell（参照 Slave_I AppShell 结构）
 * 布局：顶栏 / 左导航栏 / 内容区 / 底栏
 */
#pragma once
#include <cstdint>
#include <memory>
#include <lvgl.h>
#include <apps/utils/ui/window.h>

namespace launcher_view {

class ToolboxHome : public ui::Window {
public:
    ToolboxHome();

    void onOpen() override;
    void onUpdate() override;
    void onClose() override;

    // 导航回调需要调用
    void showPage(int page);
    void openTool(int id);

    // 渲染安全时机的 UI 变更（lv_async_call 回调需要）
    void rebuildHome();
    void setStatusText(const char* text);
    uint32_t getUptimeSec() const { return _tick / 60; }

private:
    lv_obj_t* _content     = nullptr;  // 内容区容器
    lv_obj_t* _status_text = nullptr;
    lv_obj_t* _wifi_text = nullptr;   // 顶栏：WiFi 状态
    lv_obj_t* _batt_text = nullptr;   // 顶栏：电量  // 顶栏右侧状态

    std::unique_ptr<ui::Window> _tool_window;
    int _opened_tool = -1;
    uint32_t _tick   = 0;
};

// 调试用：直接切到指定页（桌面模拟器无法点击导航）
void toolboxDebugShowPage(int page);

}  // namespace launcher_view
