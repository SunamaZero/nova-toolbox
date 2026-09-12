// Tab5 工具箱 — 日志系统（设备端）
//
// 目的：让「不插 USB 也能定位问题」成立。
//   - 捕获所有 ESP_LOG 输出（esp_log_set_vprintf 钩子）
//   - 环形缓冲保留最近 N 条（默认 200 条）
//   - 起 HTTP server，PC/浏览器可拉：GET /log（纯文本）、GET /（HTML 查看页）
//   - 定期向 OTA 服务器上报自己的 IP（这样不用猜 DHCP 分到哪个地址）
//
// 全部在应用层，不依赖 HAL/WiFi 内部结构。

#pragma once

#include <string>
#include <vector>

namespace logsys {

// 挂日志钩子 + 启动 HTTP server + 后台上报任务
void init();

// 最近 n 条日志（从旧到新）
std::string tail(int n = 200);

// 最近 n 行（不含统计头），UI 逐行渲染用 —— 要按等级上色，纯字符串做不到
std::vector<std::string> tail_lines(int n);

// 缓冲容量（供 UI 显示 "555/600"）
int capacity();

// 已记录的条数
int count();

// 清空缓冲
void clear();

}  // namespace logsys
