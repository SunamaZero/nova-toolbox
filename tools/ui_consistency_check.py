#!/usr/bin/env python3
"""
工具页 UI 一致性检查 —— 发版前自动跑（已接进 publish_ota.sh）。

为什么需要它：
同一类 bug 在这个项目里犯了两次，根因都一样 ——
**某一页自己复制了一份布局/解析代码，公共层的修复到不了它。**

  第一次：日志页手写状态行 -> 文字比圆点高 10px + 颜色掉成 LVGL 默认灰
  第二次：串口页手写状态行 -> 统一文字色后，它因为没设过色，直接变灰

光靠"写进技能/记住教训"拦不住（人会长记性，代码会分叉）。
所以把判据落成脚本：不通过就不许发版。

退出码 0 = 通过；1 = 有违规（输出具体文件 + 行号 + 修法）
"""

import re
import sys
from pathlib import Path

VIEW = Path(__file__).resolve().parent.parent / "app" / "apps" / "app_launcher" / "view"
TOOL_FILES = sorted(VIEW.glob("tool_*.cpp"))

errors: list[str] = []
warns: list[str] = []


def add_error(f: Path, line_no: int, msg: str, fix: str) -> None:
    errors.append(f"  ✗ {f.name}:{line_no}  {msg}\n      修法：{fix}")


def check_status_row_unified(f: Path, text: str) -> None:
    """状态行必须走 tl::makeStatus —— 六页统一，禁止各页手写。"""
    uses_unified = "tl::makeStatus" in text
    hand_rolled = re.search(r"_status_label\s*=\s*std::make_unique", text)
    if hand_rolled:
        ln = text[: hand_rolled.start()].count("\n") + 1
        add_error(
            f, ln,
            "状态行是自己手写的（未走统一构造）",
            "改成 `_status_label = tl::makeStatus(_window->get(), &_status_dot);`，"
            "并删掉手写的 label 尺寸/对齐与手工圆点",
        )
    elif not uses_unified and "_status_label" in text:
        add_error(
            f, 0,
            "有状态行但没走 tl::makeStatus",
            "统一用 tl::makeStatus(parent, &out_dot)",
        )


def check_label_has_color(f: Path, text: str) -> None:
    """lv_label_create 出来的标签必须显式设色，否则用 LVGL 默认色（深色主题上就是灰的）。"""
    lines = text.split("\n")
    for i, ln in enumerate(lines):
        m = re.search(r"lv_label_create\s*\(", ln)
        if not m:
            continue
        # 该变量名
        vm = re.match(r"\s*(\w+)\s*=", ln)
        if not vm:
            continue
        var = vm.group(1)
        # 往后 30 行内，逐行看有没有针对这个变量的 text_color 设置
        # （不能整段用正则：中间的 ';' 会把它截断，实测产生过误报）
        found = False
        for ln2 in lines[i : i + 30]:
            if var in ln2 and "text_color" in ln2:
                found = True
                break
        if not found:
            add_error(
                f, i + 1,
                f"标签 {var} 没有显式设色（会走 LVGL 默认色 = 灰）",
                f"加一行 lv_obj_set_style_text_color({var}, lv_color_hex(tb::text()), 0);",
            )


def check_no_first_char_parse(f: Path, text: str) -> None:
    """禁止用行首字符判等级/类型 —— 加了时间戳前缀后首字符就废了。

    只报"和字符字面量比较"（x[0] == 'I'）这种真解析；
    判字符串结尾（x[0] == '\\0'）是正当写法，不算违规。
    """
    pat = re.compile(r"\b(\w+)\s*\[\s*0\s*\]\s*(==|!=)\s*'(?:\\\\0|\\0)'")
    for m in re.finditer(r"\b(\w+)\s*\[\s*0\s*\]\s*(==|!=)\s*'([^']*)'", text):
        if m.group(3) == "\\0":          # '\0' —— 判结尾，放行
            continue
        ln = text[: m.start()].count("\n") + 1
        add_error(
            f, ln,
            f"用 {m.group(1)}[0] 判断内容类型（加了前缀行首就不是它了）",
            "改成模式匹配，例如日志等级用 level_of(s)（在行内找 '<空格><等级><空格>('）",
        )


print("═══ 工具页 UI 一致性检查 ═══")
for f in TOOL_FILES:
    text = f.read_text(encoding="utf-8")
    check_status_row_unified(f, text)
    check_no_first_char_parse(f, text)
    if f.name.startswith("tool_"):
        check_label_has_color(f, text)

# 汇总
n_status = sum(1 for f in TOOL_FILES if "tl::makeStatus" in f.read_text(encoding="utf-8"))
print(f"  状态行走统一构造的页面：{n_status}/{len(TOOL_FILES)}")
for w in warns:
    print(w)
if errors:
    print(f"\n发现 {len(errors)} 处违规：")
    for e in errors:
        print(e)
    print("\n❌ 一致性检查未通过 —— 修完再发版")
    sys.exit(1)
print("  ✓ 全部通过")
sys.exit(0)
