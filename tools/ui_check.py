#!/usr/bin/env python3
"""
Nova Tab5 工具箱 UI 审查器（布局 + 设计规范）

两类检查：
  A. 布局正确性：越界（超出内容区）、同页控件重叠
  B. 设计规范（美观是可检查的）：
     1. 4pt 网格 —— 坐标/宽高必须是 4 的倍数
     2. 触摸目标 —— 可点击控件高度 >= 48
     3. 圆角阶梯 —— Radius 只能用 6 / 12 / 18
     4. 禁止硬编码色 —— 颜色必须走 tb:: token
     5. 字体阶梯 —— 只能用 tb::font*()
     6. 左右留白 —— 内容区四边 >= 24

用法: python3 ui_check.py [目录] [--design-only]
退出码: 0 = 通过, 1 = 有问题
"""
import re
import sys
import glob
import os

CANVAS_W, CANVAS_H = 1180, 622

RE_SIZE = re.compile(r'([A-Za-z_][\w\->\.]*)->setSize\(\s*([\w\s\+\-\*\(\)]+?)\s*,\s*([\w\s\+\-\*\(\)]+?)\s*\)')
RE_ALIGN = re.compile(r'([A-Za-z_][\w\->\.]*)->align\(\s*(LV_ALIGN_\w+)\s*,\s*(-?\d+)\s*,\s*(-?\d+)\s*\)')
RE_MAKEBTN_CALL = re.compile(r'make_btn\(\s*(\d+)\s*,')
RE_RADIUS = re.compile(r'setRadius\(\s*(-?\d+)\s*\)')
RE_HEX_COLOR = re.compile(r'lv_color_hex\(\s*(0x[0-9A-Fa-f]{6})\s*\)')
RE_FONT_DIRECT = re.compile(r'setTextFont\(\s*(&lv_font_\w+)\s*\)')
RE_TEXT = re.compile(r'([A-Za-z_][\w\->\.]*)->setText\("((?:[^"\\]|\\.)*)"\)')
RE_FONT = re.compile(r'([A-Za-z_][\w\->\.]*)->setTextFont\((tb::font\w+\(\)|&lv_font_\w+)\)')
RE_BUTTON = re.compile(r'std::make_unique<Button>\([^)]*\)')
RE_BTN_VAR = re.compile(r'([A-Za-z_][\w]*)\s*=\s*std::make_unique<Button>')

FONT_METRICS = {
    'fontSm()': (8.5, 18), 'fontBody()': (9.6, 22),
    'fontBig()': (12.0, 26), 'fontTitle()': (12.0, 26),
}

VALID_RADIUS = {6, 12, 18}
GRID = 4
TOUCH_MIN = 48
PAGE_PAD = 24


def eval_dim(expr, default=0):
    try:
        return int(eval(expr.strip(), {"__builtins__": {}}, {}))
    except Exception:
        nums = re.findall(r'\d+', expr)
        return int(nums[-1]) if nums else default


def bounds_for(align, x, y, w, h):
    if align == 'LV_ALIGN_TOP_LEFT':
        return x, y, x + w, y + h
    if align == 'LV_ALIGN_TOP_MID':
        return (CANVAS_W - w) // 2 + x, y, (CANVAS_W - w) // 2 + x + w, y + h
    if align == 'LV_ALIGN_TOP_RIGHT':
        return CANVAS_W - w + x, y, CANVAS_W + x, y + h
    if align == 'LV_ALIGN_BOTTOM_LEFT':
        return x, CANVAS_H - h + y, x + w, CANVAS_H + y
    if align == 'LV_ALIGN_BOTTOM_MID':
        return (CANVAS_W - w) // 2 + x, CANVAS_H - h + y, (CANVAS_W - w) // 2 + x + w, CANVAS_H + y
    if align == 'LV_ALIGN_BOTTOM_RIGHT':
        return CANVAS_W - w + x, CANVAS_H - h + y, CANVAS_W + x, CANVAS_H + y
    if align == 'LV_ALIGN_LEFT_MID':
        return x, (CANVAS_H - h) // 2 + y, x + w, (CANVAS_H - h) // 2 + y + h
    if align == 'LV_ALIGN_RIGHT_MID':
        return CANVAS_W - w + x, (CANVAS_H - h) // 2 + y, CANVAS_W + x, (CANVAS_H - h) // 2 + y + h
    if align == 'LV_ALIGN_CENTER':
        return (CANVAS_W - w) // 2 + x, (CANVAS_H - h) // 2 + y, (CANVAS_W - w) // 2 + x + w, (CANVAS_H - h) // 2 + y + h
    return x, y, x + w, y + h


def _label_size(text, font_expr):
    cw, lh = FONT_METRICS.get(font_expr, (9.6, 22))
    lines = text.replace('\\\\n', '\n').split('\n')
    longest = max((len(l) for l in lines), default=0)
    return int(longest * cw), int(lh * max(len(lines), 1))


def check_file(path):
    src = open(path, encoding='utf-8', errors='replace').read()
    sizes, layout_problems, design_problems = {}, [], []

    for var, we, he in RE_SIZE.findall(src):
        sizes[var] = (eval_dim(we, 0), eval_dim(he, 0))
    fonts = dict(RE_FONT.findall(src))
    estimated = set()  # 文本估算出来的尺寸，不参与网格校验
    for var, text in RE_TEXT.findall(src):
        if var not in sizes:
            sizes[var] = _label_size(text, fonts.get(var, 'fontBody()'))
            estimated.add(var)

    items = []
    for m in RE_MAKEBTN_CALL.finditer(src):
        bx = int(m.group(1))
        items.append((f"make_btn#{bx}", "LV_ALIGN_BOTTOM_LEFT", bx, CANVAS_H - 58 - 18, bx + 170, CANVAS_H - 18))
    for var, align, xs, ys in RE_ALIGN.findall(src):
        w, h = sizes.get(var, (0, 0))
        x, y = int(xs), int(ys)
        l, t, r, b = bounds_for(align, x, y, w, h)
        items.append((var, align, l, t, r, b))
        # 设计检查：4pt 网格（只查设计者显式写死的值；负偏移是相对偏移、label 尺寸是估算，跳过）
        for label, val in (("x", x), ("y", y)):
            if val > 0 and val % GRID != 0:
                design_problems.append(f"网格: {var} {label}={val} 不是 {GRID} 的倍数")
        if var not in estimated:
            for label, val in (("w", w), ("h", h)):
                if val and val % GRID != 0:
                    design_problems.append(f"网格: {var} {label}={val} 不是 {GRID} 的倍数")

    # A. 布局问题
    for var, align, l, t, r, b in items:
        if r > CANVAS_W:
            layout_problems.append(f"越界(右): {var} right={r} > {CANVAS_W} (+{r - CANVAS_W}px)")
        if b > CANVAS_H:
            layout_problems.append(f"越界(下): {var} bottom={b} > {CANVAS_H} (+{b - CANVAS_H}px)")
        if l < 0:
            layout_problems.append(f"越界(左): {var} left={l} < 0")
        if t < 0:
            layout_problems.append(f"越界(上): {var} top={t} < 0")
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            a, c = items[i], items[j]
            if a[5] <= c[3] or c[5] <= a[3] or a[4] <= c[2] or c[4] <= a[2]:
                continue
            ov_w = min(a[4], c[4]) - max(a[2], c[2])
            ov_h = min(a[5], c[5]) - max(a[3], c[3])
            if ov_w > 8 and ov_h > 8:
                layout_problems.append(f"重叠: {a[0]} <-> {c[0]} ({ov_w}x{ov_h}px)")

    # B. 设计规范
    for val in RE_RADIUS.findall(src):
        if int(val) not in VALID_RADIUS:
            design_problems.append(f"圆角: Radius={val} 不在阶梯 {sorted(VALID_RADIUS)}")
    hexes = RE_HEX_COLOR.findall(src)
    if hexes:
        design_problems.append(f"硬编码色: {len(hexes)} 处 ({', '.join(sorted(set(hexes))[:4])}) — 应走 tb:: token")
    for f in RE_FONT_DIRECT.findall(src):
        design_problems.append(f"字体: {f} 非设计系统字体（应用 tb::font*()）")
    # 触摸目标：Button 高度 >= 48
    for var, (w, h) in sizes.items():
        if h and h < TOUCH_MIN and ('btn' in var.lower() or 'button' in var.lower()):
            design_problems.append(f"触摸: {var} 高 {h} < {TOUCH_MIN}")
    # 左右留白
    for var, align, l, t, r, b in items:
        if l and l < PAGE_PAD and l != 0:
            design_problems.append(f"留白: {var} left={l} < {PAGE_PAD}")
    return layout_problems, design_problems, items


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    design_only = "--design-only" in sys.argv
    root = args[0] if args else os.path.dirname(os.path.abspath(__file__)) + "/../app/apps/app_launcher/view"
    files = sorted(glob.glob(os.path.join(root, "tool_*.cpp")))
    tl = td = 0
    for f in files:
        lp, dp, items = check_file(f)
        name = os.path.basename(f)
        if lp or dp:
            print(f"\n=== {name} ({len(items)} 控件) ===")
            for p in lp:
                print("  ✗ 布局", p)
            for p in dp:
                print("  ◦ 设计", p)
        else:
            print(f"=== {name}: OK ({len(items)} 控件)")
        tl += len(lp)
        td += len(dp)
    print(f"\n布局问题 {tl} 个 / 设计待改进 {td} 处")
    return 1 if (tl or (td and design_only)) else 0


if __name__ == "__main__":
    sys.exit(main())
