# AI Passport port (Tasmota fork)

This is a fork of [Tasmota](https://github.com/arendst/Tasmota) that adds a port for the
**FoloToy AI Passport** (ESP32-C3, 8 MB flash, 240x320 ST7789P3 panel, three buttons on one
ADC ladder). Upstream behaviour is unchanged; everything below is additive.

## What this fork adds

- `tasmota/tasmota_xdrv_driver/xdrv_95_aipassport_screen.ino` - self-contained boot screen driver:
  - Wi-Fi setup hint in Chinese while the device has no IP address: which hotspot to join
    (`tasmota-...`) and which page to open (`http://192.168.4.1`).
  - Once online: the network name and the device's own IP address, plus a reminder to open it in
    a browser.
  - Backlight management: after 5 minutes without new screen content the backlight switches off
    (the panel keeps showing the last frame); any button press or a screen update switches it
    back on.
- Chinese interface: the custom build enables `MY_LANGUAGE=zh_CN` (web console and log messages).
- `platformio_override.ini` - build environment `tasmota32c3-aipassport` on top of `tasmota32c3`.
- `tools/gen_aipassport_cjk_font.py` - generates the embedded 24x24 CJK bitmap font subset
  (`tasmota/tasmota_xdrv_driver/aipassport_cjk_font.h`) from Noto Sans SC (SIL OFL).

Hardware details (pins, panel init sequence) follow the AI Passport board support package in
<https://github.com/folotoy/ai-passport> (`components/bsp`).

## Build and flash

```bash
pio run -e tasmota32c3-aipassport                                          # build
pio run -e tasmota32c3-aipassport -t upload_factory --upload-port COMx     # flash merged image at 0x0
```

Artifacts land in `build_output/firmware/tasmota32c3-aipassport.bin` (application image) and
`build_output/firmware/tasmota32c3-aipassport.factory.bin` (complete image for flashing from `0x0`).
To keep NVS settings such as the Wi-Fi configuration when updating from another Tasmota build with
the same partition layout, flash only the application image at the app offset (`0xE0000` with the
default `tasmota32c3` 4 MB layout).

## Documentation and feature limits

Tasmota documentation: **<https://tasmota.github.io/docs/>**

Because of the device's memory and firmware size limits, every Tasmota binary - including this one -
contains only a subset of the available drivers and features (sensors, displays, scripting, ...).
If you need a module that is not included, you have to **compile your own build** with the matching
`USE_*` options (see "Builds" and "Compiling" in the documentation); this repository is the starting
point. On this target the default partition table gives the application 2880 KB of flash, so keep an
eye on the firmware size when enabling more modules.

---

# 中文说明

本仓库是 [Tasmota](https://github.com/arendst/Tasmota) 的 fork，增加了 **FoloToy AI Passport**
（ESP32-C3、8MB Flash、240x320 ST7789P3 屏幕、三个按键共用一路 ADC 阶梯）的移植；上游行为未改动。

## 新增内容

- 开机屏幕驱动 `tasmota/tasmota_xdrv_driver/xdrv_95_aipassport_screen.ino`：
  - 配网时用中文提示"连接热点 → 打开网页"；
  - 联网后显示网络名称与本机地址（IP），并提示在浏览器打开；
  - 5 分钟没有新画面内容时自动熄灭背光（画面保留），按任意键或画面更新时重新点亮。
- 中文界面：自定义构建启用 `MY_LANGUAGE=zh_CN`（网页控制台与日志均为中文）。
- 构建环境 `tasmota32c3-aipassport`（见 `platformio_override.ini`）。
- 中文字库生成脚本 `tools/gen_aipassport_cjk_font.py`（Noto Sans SC，OFL 许可）。

硬件细节（引脚、屏幕初始化序列）来自 AI Passport 的板级支持包：
<https://github.com/folotoy/ai-passport> 的 `components/bsp`。

## 编译与烧录

```bash
pio run -e tasmota32c3-aipassport                                          # 编译
pio run -e tasmota32c3-aipassport -t upload_factory --upload-port COMx     # 整机镜像写入 0x0
```

产物在 `build_output/firmware/`：`tasmota32c3-aipassport.bin`（应用镜像）与
`tasmota32c3-aipassport.factory.bin`（从 `0x0` 烧录的整机镜像）。
从其它分区布局相同的 Tasmota 固件升级、想保留 Wi-Fi 等设置时，只烧应用镜像到 app 偏移
（默认 `tasmota32c3` 4MB 布局为 `0xE0000`）即可。

## 文档与功能限制

Tasmota 官方文档：**<https://tasmota.github.io/docs/>**

受设备内存与固件体积限制，任何 Tasmota 固件（包括本仓库构建的）都只内置常用驱动与功能
（部分传感器、显示屏、脚本等模块不包含）。需要其它模块时，**必须参考文档自行编译固件**
（见文档中的 Builds / Compiling 章节），并以本仓库为起点增删 `USE_*` 选项。本目标的默认分区
给应用 2880 KB，增加模块时请留意固件体积。
