# 第三方许可证与声明 / Third-Party Notices

本项目（Nova Toolbox）在他人工作之上二次开发。按**分发方式**分三类列明：

1. **随仓库分发** —— 代码/资源在本仓库里，许可证原文附在下面
2. **构建时拉取** —— 由 ESP-IDF Component Manager 从上游下载，**不随本仓库分发**
3. **生成产物** —— 由上游资源生成的中间产物，其许可证随来源

---

## 1. 随仓库分发

### 1.1 M5Tab5-UserDemo（项目基础）— MIT

本项目的应用框架、启动动画、HAL 抽象、上游工具面板与 `ibm_plex_mono_*.c` 字体等，
均来自 **M5Stack 官方 [M5Tab5-UserDemo](https://github.com/m5stack/M5Tab5-UserDemo)**（桌面端已由本项目改为 X11 后端 + IDF 头文件桩），
按 MIT 许可证使用。原始许可证原文见仓库根目录 [`LICENSE`](LICENSE)：

```
MIT License

Copyright (c) 2025 M5Stack Technology CO LTD

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

### 1.2 字体 — SIL Open Font License 1.1

仓库中 `app/apps/app_launcher/view/fonts/tb_cn_16.c` 是由 **Maple Mono NF CN** 字体
经 [lv_font_conv](https://github.com/lvgl/lv_font_conv) 生成的点阵字体（衍生作品），
注意 OFL 要求：**衍生作品仍受 OFL 约束，且不得使用保留字体名（Reserved Font Name）销售**。
本项目仅将其作为固件资源随附，未修改字体名、未单独销售。

```
Copyright (c) 2022, subframe7536 (https://github.com/subframe7536),
with Reserved Font Name Maple Mono.
```

`ibm_plex_mono_14/16/18/20/32.c` 由 **IBM Plex Mono** 生成，随上游 M5Tab5-UserDemo 提供：

```
Copyright (c) 2017 IBM Corp. with Reserved Font Name "Plex"
```

导航栏图标字形来自 **Font Awesome 5 Free**（字体 SIL OFL 1.1 / 图标 CC BY 4.0），
同样随上游项目提供。

**SIL Open Font License 1.1 全文：**

```
-
SIL OPEN FONT LICENSE Version 1.1 - 26 February 2007
-----------------------------------------------------------

PREAMBLE
The goals of the Open Font License (OFL) are to stimulate worldwide
development of collaborative font projects, to support the font creation
efforts of academic and linguistic communities, and to provide a free and
open framework in which fonts may be shared and improved in partnership
with others.

The OFL allows the licensed fonts to be used, studied, modified and
redistributed freely as long as they are not sold by themselves. The
fonts, including any derivative works, can be bundled, embedded,
redistributed and/or sold with any software provided that any reserved
names are not used by derivative works. The fonts and derivatives,
however, cannot be released under any other type of license. The
requirement for fonts to remain under this license does not apply
to any document created using the fonts or their derivatives.

DEFINITIONS
"Font Software" refers to the set of files released by the Copyright
Holder(s) under this license and clearly marked as such. This may
include source files, build scripts and documentation.

"Reserved Font Name" refers to any names specified as such after the
copyright statement(s).

"Original Version" refers to the collection of Font Software components as
distributed by the Copyright Holder(s).

"Modified Version" refers to any derivative made by adding to, deleting,
or substituting -- in part or in whole -- any of the components of the
Original Version, by changing formats or by porting the Font Software to a
new environment.

"Author" refers to any designer, engineer, programmer, technical
writer or other person who contributed to the Font Software.

PERMISSION & CONDITIONS
Permission is hereby granted, free of charge, to any person obtaining
a copy of the Font Software, to use, study, copy, merge, embed, modify,
redistribute, and sell modified and unmodified copies of the Font
Software, subject to the following conditions:

1) Neither the Font Software nor any of its individual components,
in Original or Modified Versions, may be sold by itself.

2) Original or Modified Versions of the Font Software may be bundled,
redistributed and/or sold with any software, provided that each copy
contains the above copyright notice and this license. These can be
included either as stand-alone text files, human-readable headers or
in the appropriate machine-readable metadata fields within text or
binary files as long as those fields can be easily viewed by the user.

3) No Modified Version of the Font Software may use the Reserved Font
Name(s) unless explicit written permission is granted by the corresponding
Copyright Holder. This restriction only applies to the primary font name as
presented to the users.

4) The name(s) of the Copyright Holder(s) or the Author(s) of the Font
Software shall not be used to promote, endorse or advertise any
Modified Version, except to acknowledge the contribution(s) of the
Copyright Holder(s) and the Author(s) or with their explicit written
permission.

5) The Font Software, modified or unmodified, in part or in whole,
must be distributed entirely under this license, and must not be
distributed under any other license. The requirement for fonts to
remain under this license does not apply to any document created
using the Font Software.

TERMINATION
This license becomes null and void if any of the above conditions are
not met.

DISCLAIMER
THE FONT SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO ANY WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT
OF COPYRIGHT, PATENT, TRADEMARK, OR OTHER RIGHT. IN NO EVENT SHALL THE
COPYRIGHT HOLDER BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
INCLUDING ANY GENERAL, SPECIAL, INDIRECT, INCIDENTAL, OR CONSEQUENTIAL
DAMAGES, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF THE USE OR INABILITY TO USE THE FONT SOFTWARE OR FROM
OTHER DEALINGS IN THE FONT SOFTWARE.

```

---

## 2. 构建时由 IDF Component Manager 拉取（不随本仓库分发）

以下组件由 `platforms/tab5/main/idf_component.yml` 声明，编译时从
[components.espressif.com](https://components.espressif.com/) 下载，**不在本仓库中**。
完整清单以该文件为准，主要包括：

| 组件 | 许可 |
|---|---|
| espressif/esp-bsp（`m5stack_tab5`）、espressif/esp_hosted、espressif/esp_wifi_remote、espressif/esp_lcd_*、espressif/led_strip、espressif/esp_codec_dev、espressif/esp_h264、espressif/esp_serial_slave_link | Apache-2.0 |
| chmorgan/esp-audio-player、chmorgan/esp-file-iterator、chmorgan/esp-libhelix-mp3 | Apache-2.0 |
| espressif/usb_host_vcp、usb_host_cdc_acm、usb_host_ch34x_vcp、usb_host_cp210x_vcp、usb_host_ftdi_vcp、usb_host_hid | Apache-2.0 |
| lvgl/lvgl | MIT |
| Forairaaaaa/smooth_ui_toolkit、Forairaaaaa/mooncake | MIT |
| espressif/esp-idf（框架本体） | Apache-2.0 |

Apache-2.0 许可证原文：<https://www.apache.org/licenses/LICENSE-2.0>

---

## 3. 参考资料（仅作事实依据，未包含其代码）

- [WCH 官方 ch343ser_linux](https://github.com/WCHSoftGroup/ch343ser_linux) —— CH343/CH344/CH9102/CH347 的 USB PID 表、CDC-ACM 兼容性说明（依据其文档与源码中的事实，自行实现的驱动注册逻辑）
- [0day1day/Slave_I](https://github.com/0day1day/Slave_I) —— 设计系统组织方式的思路参考
- [M5Stack Tab5 官方文档](https://docs.m5stack.com/en/products/sku/k145)、[M5Stack 社区论坛](https://community.m5stack.com/topic/7702/tab5-power-up-sequence) —— 硬件规格与 IO 扩展器用途
- [lv_font_conv](https://github.com/lvgl/lv_font_conv) —— 生成字体的工具（构建期使用，未分发）

---

## 本项目自身

新增代码沿用 **MIT**，与上游 M5Tab5-UserDemo 一致。见 [`LICENSE`](LICENSE)。
