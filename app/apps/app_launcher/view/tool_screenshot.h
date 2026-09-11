// Tab5 工具箱 — 屏幕截图回传（让我能真正"看"UI，而不是靠推理猜）
//
// 原理：LVGL 抓当前屏幕 → 缩放到 1/4 → base64 → 串口输出
//   PC 端 scripts/grab_shot.py 解析后存 PNG，我再用视觉能力检查布局。
//
// 输出格式（行式，便于 PC 端解析）：
//   SHOT_BEGIN <w> <h> <bytes>
//   <base64 数据，每行 512 字符>
//   SHOT_END

#pragma once

#ifdef CONFIG_IDF_TARGET_ESP32P4

namespace screenshot {

// 抓屏并输出到串口（阻塞约 2-4 秒）。scale: 1=原图 2=1/2 4=1/4（推荐 4）
void captureAndSend(int scale = 4);

}  // namespace screenshot

#endif
