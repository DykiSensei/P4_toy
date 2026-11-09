# P4 自然探索玩具 · 自然手帐 UI

WTDKP4C5-S1 开发板，ESP-IDF 5.5.4，实物 ESP32-P4 revision v3.2、16MB Flash。屏幕为资料包中的 320×480 ST7796S SPI 模组。已完成实机点亮：10MHz 硬件 SPI 下，用户确认色条和移动白块显示正常。

当前固件已实现用户确认的“自然手帐”首页，以及拍照、地图、图鉴、对战、设置和说明页的进入与返回；已编译、烧录并通过串口逐页切换验证。用户已确认显示、首次校准和页面切换均正常。触摸采用 XPT2046，四点校准后保存到 NVS。摄像头、定位、图鉴保存和双机对战仍未接入，页面明确显示当前状态。

[界面设计说明](docs/ui/UI_DESIGN.md) · [本轮实现与操作说明](docs/ui/UI_IMPLEMENTATION.md)

## 项目整体规划

产品方向为面向 6–12 岁儿童的自然探索玩具，包含摄像头物种识别、GPS 定位与轨迹、离线地图、本机物种图鉴和双机回合制对战。功能范围、架构、开发阶段及完成标准见 [项目整体大纲](docs/PROJECT_OUTLINE.md)。当前进度为 M0 点屏完成、M1 基础界面和输入验证中。

完整引脚资源见 [GPIO 分配表](docs/GPIO_ASSIGNMENT.md)，包含 P4 全部 GPIO 的供电域、核心板与开发板脚号、屏幕占用、C5 内部连接、摄像头及启动调试预留，并单列 C5 外引 GPIO。

## 接线

按用户提供的顺序，从屏幕 CS 起连续连接 GPIO39～50：

| 屏幕信号 | ESP32-P4 |
| --- | --- |
| CS | GPIO39 |
| RESET | GPIO40 |
| DC/RS | GPIO41 |
| SDI/MOSI | GPIO42 |
| SCK | GPIO43 |
| LED | GPIO44，高有效 |
| SDO/MISO | GPIO45 |
| T_CLK | GPIO46 |
| T_CS | GPIO47 |
| T_DIN | GPIO48 |
| T_DO | GPIO49 |
| T_IRQ | GPIO50 |

首次测试时 VCC 接开发板 3V3，GND 接开发板 GND，用户确认实物 J1 断开。现已改为开发板 5V 给屏幕 VCC 供电，J1 保持断开。模组原理图注明：3.3V 供电时 J1 应短接，5V 供电时 J1 应断开；5V 不能连接任何信号引脚。

## 必须保留的板级供电初始化

在配置 GPIO39～48 之前，申请并保持 LDO4 的 3.3V 输出：

```c
static esp_ldo_channel_handle_t gpio_supply;
const esp_ldo_channel_config_t supply_config = {
    .chan_id = 4,
    .voltage_mv = 3300,
};
ESP_ERROR_CHECK(esp_ldo_acquire_channel(&supply_config, &gpio_supply));
```

这一步是本次白屏问题的关键修复。未初始化时，用户实测 GPIO40 空载约1V、固定输出高的 GPIO47 为0V；启用后两者均约3.3V，接回 RESET 后屏幕显示正常。保持该句柄到应用结束，避免释放后使这组 GPIO 失去供电。屏幕 VCC 仍由开发板5V供电。

官方资料：[WTDKP4C5-S1 资料页](https://wiki.wireless-tag.com/docs/zh/WTDKP4C5-S1/board_resources.html)；[厂家 BSP 的 LDO4 电源控制例程](https://github.com/wireless-tag-com/WTDKP4C5-S1/blob/master/components/wtdkp4c5_s1_board/wtdkp4c5_s1_board.c)。板级供电判断也经过上述实机对照验证。

## 编译和烧录

在项目根目录的 PowerShell 中：

```powershell
. 'D:\Espressif\Initialize-Idf.ps1' -IdfId 'esp-idf-19bf15883d5988831b99b02a2227c33e'
idf.py build
.\tools\flash.ps1 -Port COM12
```

烧录脚本使用项目内 `tools/esptool-4.12.0`。IDF 原有 esptool 4.12.dev1 在该 v3.2 芯片上出现 stub/Flash 识别问题；没有修改全局 IDF 环境。若重新获取源码，需要先安装项目依赖：

```powershell
python -m pip install --no-deps --target tools/esptool-4.12.0 esptool==4.12.0
```

官方修复说明：https://github.com/espressif/esptool/releases/tag/v4.12.0

### 在其他电脑上使用

上面的 ESP-IDF 安装路径、环境 ID 和 COM12 是本机配置。克隆后先安装并启用 ESP-IDF 5.5.4 环境，确认 `python` 和 `idf.py` 来自该环境，再安装项目内的烧录工具并构建。`--no-deps` 使用 IDF Python 环境中已有的依赖。

```powershell
python -m pip install --no-deps --target tools/esptool-4.12.0 esptool==4.12.0
idf.py build
# 将 COM12 替换为实际串口；显式使用当前 IDF 环境的 Python。
.\tools\flash.ps1 -Port COM12 -Python (Get-Command python).Source
```

`main/ui_font_data.c` 已纳入仓库，正常构建无需重新生成字库。修改界面中文文案后，需要安装 Pillow、准备 Noto Sans SC 字体，再通过 `tools/generate_ui_font.py --font <字体文件路径>` 生成字库；fontTools 可选，用于校验字形覆盖。串口辅助脚本需要 pyserial。地图样例可通过 `python tools/build_ui_map.py` 从已保存的 OSM 数据重新生成。

`build/`、`backups/`、`tmp/`、本地安装的 esptool 及原始厂商压缩包不纳入版本控制。文中提到的烧录备份、日志和临时设计预览属于本机记录，不随仓库提供。

## 当前界面行为

- 上电直接显示自然手帐首页。使用中文子集字库与 8 行绘图缓存，不依赖 PSRAM。
- 首次触摸进入四点校准，顺序为左上、右上、右下、左下；每点按住约半秒后松开。第四点用于独立误差检查，成功后写入 NVS。可在设置重新校准。
- 若坐标不准而点不到设置，在任意页面按住屏幕 3 秒进入重新校准；先松手，再点第一个十字。此恢复操作只依赖按下状态，不依赖旧坐标。校准期间也能长按重新开始。
- 首页四个入口分别进入拍照、地图、空图鉴、对战页面，左上返回首页。设置可进入校准和界面说明。
- 串口快捷键用于开发检查：`h`/`0` 首页，`1` 拍照，`2` 地图，`3` 图鉴，`4` 对战，`s` 设置，`a` 说明，`c` 校准，`?` 状态。
- `python tools/ui_console.py --commands '?1234sah?' --seconds 22` 可验证切页并停回首页；无 `--commands` 时仅采集日志。

## 初始点屏测试行为（历史）

- `use_gpio_spi=false`，使用 SPI2、mode 0、10MHz 硬件 SPI。320×480 竖屏，RGB565 高字节先发；软件 SPI 只作为保留的诊断选项。当前 UI 沿用此配置。
- 使用附带厂家示例的初始化参数，并按 IC 手册修正 C2 参数、扩展命令关闭值和 B0 的独立 SDO 输出模式。
- 初始版本上电依次显示红、绿、蓝，然后显示八色色条、白色边框和底部移动白块；当前已替换为 UI。
- USB Serial/JTAG 控制台输出初始化过程和每 5 秒一次的运行日志。LCD ID/寄存器读取仍是辅助诊断，当前回读值未匹配预期，不能据此判断显示失败；显示写入已由实物画面验证。
- 初始版本只使用内部 RAM，触摸片选保持高电平、不采集触摸，不启用 C5 网络功能。当前 UI 增加触摸采集，仍未启用 C5。

`backups/before_lcd_bringup_16MB.bin` 保存首次烧录前的 Flash 完整备份（本地文件，不纳入版本控制）。

## 实机验证记录

- ESP-IDF 5.5.4 编译通过；bootloader、分区表和应用均通过 Flash hash 校验。
- USB 日志确认 ESP32-P4 v3.2、16MB Flash、400MHz CPU，LDO4 初始化成功，测试任务持续运行。
- 软件 SPI 对照版及最终 10MHz 硬件 SPI 版本，均由用户确认色条与底部移动白块正常。
- 最终启动日志：`build/monitor_final.log`。历史诊断日志保存在 `build/monitor_*.log`。
- LCD 寄存器读取和触摸采集尚未完成验证；本轮验证范围为屏幕显示及背光。

### 自然手帐 UI（2026-11-09）

- 7 个页面与 4 个校准目标的本机 C 渲染检查、点击区域与返回路径检查通过。
- ESP-IDF 5.5.4 构建成功；加入长按重校后的应用 610,464 字节，1 MiB 应用分区剩余约 42%。烧录三个区域均通过 hash 校验。
- 串口实际切换全部页面，刷新约 282–358 ms；观测可用内部堆 559,208 字节，未发现崩溃或看门狗复位。
- 日志：`build/flash_ui.log`、`build/monitor_ui_navigation.log`、`build/monitor_ui_touch.log`。显示观感和实屏触摸由用户核验。
- 用户已回复“都正常”，确认首页显示、四点校准、入口与返回；随后要求增加便于恢复的重新校准功能。
- 重新校准入口及全局长按恢复已再次烧录；重启后串口确认 `calibrated=1`，原校准数据成功保留。对应日志：`build/flash_ui_recalibrate.log`、`build/monitor_ui_recalibrate.log`。
- 上一版点屏源码及应用备份：`backups/before_home_ui/`；首次烧录前完整 Flash 备份仍保留。
