* [English Version](./README.md)

---
<div align="center">

# Ameba RTL8721F 仪表盘监控（FreeRTOS）

</div>

[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-green)](https://freertos.org)
[![SDK-Ver](https://badgen.net/badge/SDK/Ameba%20RTOS/blue)](https://aiot.realmcu.com/en/latest/rtos/index.html)
[![Version](https://badgen.net/github/release/Ameba-AIoT/ameba-rtos)](https://github.com/Ameba-AIoT/ameba-rtos/releases)
[![Platform](https://img.shields.io/badge/platform-RTL8721F-blue)]()
[![License](https://badgen.net/badge/License/Apache%202.0/lightgrey)](LICENSE)
[![Gitee Mirror](https://badgen.net/badge/mirror/Gitee/c71d23?icon=git)](https://gitee.com/yangdavid988/mcu-pc-dashboard)
[![Language](https://badgen.net/badge/language/C/blue)](https://github.com/yangdavid988/mcu-pc-dashboard/search?l=c)
[![UI](https://img.shields.io/badge/LVGL-9.3-brightgreen)](https://lvgl.io)
[![CI Linux](https://badgen.net/badge/Linux/success/green?icon=github)](https://github.com/yangdavid988/mcu-pc-dashboard/actions/workflows/CI_build_check.yml)
[![Last Commit](https://badgen.net/github/last-commit/yangdavid988/mcu-pc-dashboard)](https://github.com/yangdavid988/mcu-pc-dashboard)
[![Status](https://img.shields.io/badge/status-updating-yellow)]()

🚀 基于 **Ameba RTL8721F** MCU 的 PC 硬件资源实时监控器。通过 MQTT 订阅 `pc/stats`、`humiture/measurement`、`pc/event`、`pc/weather` 主题，在 **800×480 TFT** 屏幕上实时展示 CPU、GPU、内存、磁盘、网络、天气等数据，UI 由 **LVGL 9.3** 驱动。

MCU 仅作为订阅者 — 只接收数据，不发布任何消息。

- 📄 [芯片与模块信息](https://aiot.realmcu.com/cn/home.html) | 🌿 [Gitee 镜像](https://gitee.com/yangdavid988/mcu-pc-dashboard)

---
<div align="center">

<img src="VORTEX-COBALT.jpg" width="100%" style="max-width:1365px"
     alt="VORTEX 布局 - COBALT 主题">

</div>

---

### 🛠️ 技术栈

<p>
  <img src="https://img.shields.io/badge/C-A8B9CC?style=flat-square&logo=c&logoColor=black" alt="C" />
  <img src="https://img.shields.io/badge/FreeRTOS-1D7EA8?style=flat-square&logo=freertos" alt="FreeRTOS" />
  <img src="https://img.shields.io/badge/LVGL-5A9E3E?style=flat-square&logo=lvgl" alt="LVGL" />
  <img src="https://img.shields.io/badge/Arm_Cortex-0091BD?style=flat-square&logo=arm&logoColor=white" alt="Arm" />
  <img src="https://img.shields.io/badge/MQTT-660066?style=flat-square&logo=mqtt&logoColor=white" alt="MQTT" />
  <img src="https://img.shields.io/badge/WiFi-1E90FF?style=flat-square&logo=wifi&logoColor=white" alt="WiFi" />
  <img src="https://img.shields.io/badge/Ameba_IoT-00A1DE?style=flat-square&logo=wifi" alt="Ameba" />
  <img src="https://img.shields.io/badge/CMake-064F8C?style=flat-square&logo=cmake&logoColor=white" alt="CMake" />
</p>

### 🔍 Topics & Keywords

<p>
  <a href="https://github.com/topics/embedded"><img src="https://img.shields.io/badge/嵌入式-555555?style=flat-square&logo=github" alt="Embedded" /></a>
  <a href="https://github.com/topics/iot"><img src="https://img.shields.io/badge/IoT-555555?style=flat-square&logo=github" alt="IoT" /></a>
  <a href="https://github.com/topics/freertos"><img src="https://img.shields.io/badge/FreeRTOS-555555?style=flat-square&logo=github" alt="FreeRTOS" /></a>
  <a href="https://github.com/topics/lvgl"><img src="https://img.shields.io/badge/LVGL-555555?style=flat-square&logo=github" alt="LVGL" /></a>
  <a href="https://github.com/topics/mqtt"><img src="https://img.shields.io/badge/MQTT-555555?style=flat-square&logo=github" alt="MQTT" /></a>
  <a href="https://github.com/topics/realtek"><img src="https://img.shields.io/badge/瑞昱-555555?style=flat-square&logo=github" alt="Realtek" /></a>
  <a href="https://github.com/topics/dashboard"><img src="https://img.shields.io/badge/仪表盘-555555?style=flat-square&logo=github" alt="Dashboard" /></a>
  <a href="https://github.com/topics/tft-display"><img src="https://img.shields.io/badge/TFT-555555?style=flat-square&logo=github" alt="TFT Display" /></a>
  <a href="https://github.com/topics/pc-monitoring"><img src="https://img.shields.io/badge/PC监控-555555?style=flat-square&logo=github" alt="PC Monitoring" /></a>
</p>

---

### ✨ 功能特点

- ✅ **MQTT 订阅** — 通过 TLS 8883 加密连接，订阅 `pc/stats`、`humiture/measurement`、`pc/event`、`pc/weather` 主题。
- ✅ **ST7262（默认）或 DBL070（可选）TFT 仪表盘** — 800×480，基于 LVGL 9.3，双缓冲 + VBlank 页翻转（无撕裂）。
- ✅ **CPU / 内存 / 磁盘** — 彩色进度条，支持可配置阈值闪烁告警。
- ✅ **GPU 监控** — 使用率、显存、温度及 GPU 型号名称。
- ✅ **网络** — 上传/下载速度（KB/s），带箭头图标。
- ✅ **CPU 温度 / 频率** — 当前温度、当前/最小/最大 MHz（PC 端采集器支持时显示）。
- ✅ **交换分区（Swap）** — 使用百分比、总量和已用量。
- ✅ **SHT3X 室内温湿度** — 通过 MQTT 主题获取，无需本地传感器。
- ✅ **室外天气** — MCU 每 10 分钟通过 HTTP 获取 OpenWeatherMap 数据（或 PC 端经 MQTT 推送），显示城市、温度、湿度、天气描述。
- ✅ **电池** — 电量百分比 + 充电状态指示。
- ✅ **系统信息** — 用户名、进程数、物理/逻辑 CPU 核心数、主机名、操作系统平台。
- ✅ **时钟** — 启动时间和当前时间（从 Unix 时间戳转换，支持 UTC+8 偏移）。
- ✅ **磁盘 I/O** — 总读取/写入字节数及 I/O 利用率百分比。
- ✅ **WiFi 自动连接** — 支持可配置重试次数，断线自动重连。
- ✅ **MQTT TLS 加密连接**。
- ✅ **3 种仪表盘布局** — 通过 GPIO 按键循环切换（TRIAD → VORTEX → PULSE）。
- ✅ **3 种颜色主题** — 通过 GPIO 按键循环切换（COBALT 蓝色 / INFERNO 红色 / SILICON 银色）。
- ✅ **淡入淡出动画** — 布局/主题切换时 200ms 透明度过渡。
- ✅ **待机模式** — PC 锁屏时自动切换为模拟时钟，Sweep 指针扫入动画，待机自动降低亮度；解锁后淡出恢复监控面板。Standby Manager 集中协调 MQTT 任务与 LVGL UI 任务的切换时序。
- ✅ **PWM 背光控制** — 通过 GPIO 按键调节亮度（短按步进 10%，长按跳至极值），待机自动降至 20%，OSD 弹窗实时显示百分比。
- ✅ **可配置阈值闪烁系统** — 当指标超过告警级别时，卡片边框和进度条闪烁提醒。阈值通过 `threshold_config.h` 配置。

---
### 🏗️ 项目结构

```
.
├── app_example/
│   ├── CMakeLists.txt          # 构建配置
│   ├── main/
│   │   └── app_main.c          # 入口函数，创建任务线程
│   ├── core/                   # 核心业务逻辑
│   │   ├── pc_dashboard.c/h    # MQTT 客户端、JSON 解析、PC_Stats_t 数据结构
│   │   ├── standby_manager.c/h # 待机管理器（锁屏/解锁协调）
│   │   ├── weather.c/h         # 天气数据（HTTP 获取或 MQTT 推送）
│   │   └── wifi_reconnect.c/h  # Wi-Fi 自动连接
│   ├── ui/                     # UI 呈现层
│   │   ├── pc_dashboard_ui.c/h     # UI 生命周期、定时器回调
│   │   ├── pc_dashboard_layout.c/h # V3 布局系统（TRIAD/VORTEX/PULSE）
│   │   ├── pc_dashboard_theme.c/h  # 颜色主题（COBALT/INFERNO/SILICON）
│   │   └── pc_dashboard_lock_screen.c/h  # 鎖定畫面
│   ├── hal/                    # 硬件抽象層
│   │   ├── lcd/                # LCD 驱动（ST7262、DBL070、LCDC 核心）
│   │   ├── backlight_ctrl.c/h  # PWM 背光控制
│   │   └── gpio_control.c/h    # GPIO 按键中断 → 延迟切换
│   ├── config/                 # 配置头文件
│   │   ├── threshold_config.h  # 告警闪烁阈值
│   │   ├── lv_conf_project.h   # LVGL 配置覆盖
│   │   ├── sdk_compat.h        # SDK 版本兼容层
│   │   └── suppress_mqtt_log.h # MQTT 日志抑制
│   ├── assets/                 # 图片/图标资源
│   │   ├── icons/              # 22 个 LVGL 图标资源（C 数组）
│   │   └── backgrounds/        # 3 个主题背景图片 + 时钟
│   └── scripts_tools/          # PNG 转 LVGL C 数组脚本
├── PC/                         # PC 端 Python 采集器
├── env.sh                      # Linux/macOS 环境配置
├── env.ps1                     # Windows PowerShell 环境配置
├── env.bat                     # Windows cmd 环境配置
├── CMakeLists.txt              # 顶层 CMake
├── prj.conf                    # SDK Kconfig
└── Kconfig                     # SDK 配置参考
```
---

### 🧠 工作原理概述

1. 系统启动后，初始化 ST7262 LCD、LVGL 9.3 界面和 Wi-Fi 连接。
2. Wi-Fi 连接成功后，MQTT 客户端订阅 `pc/stats`、`humiture/measurement`、`pc/event` 和 `pc/weather` 主题。
3. 数据源：
   - **PC 状态** — Python 脚本通过 `psutil` 采集硬件信息，发布到 `pc/stats`。
   - **SHT3X 传感器** — 另一块 Ameba MCU 读取温湿度数据，发布到 `humiture/measurement`。
   - **PC 事件** — PC 锁屏/解锁事件发布到 `pc/event`，触发 MCU 进入/退出待机模式。
   - **室外天气** — MCU 每 10 分钟通过 HTTP 直接获取 OpenWeatherMap 数据（或 PC 端经 MQTT 推送）。
4. 仪表盘 MCU 按主题路由收到的 JSON 数据，解析后实时更新 LVGL 显示。
5. GPIO 按键切换布局、主题和背光亮度。

```text
Windows PC (psutil) ──MQTT──►  pc/stats              ┌──────────────────────────┐
                                                     │  Ameba RTL8721F         │
Windows PC ──────────MQTT──►  pc/event               │  • 订阅 pc/stats         │
                               MQTT Broker (TLS 8883) │  • 订阅 humiture/..      │
SHT3X MCU ──────────MQTT──►  humiture/measurement    │  • 订阅 pc/event/weather │
                                                     │  • HTTP 获取天气          │
                                                     │  • 3 种布局 / 3 种主题   │
                                                     │  • 待机时钟 + 背光控制   │
                                                     │  • ST7262 800×480 TFT    │
                                                     └──────────────────────────┘
```

---

### 🔧 搭建硬件环境

1️⃣ **所需组件**

- RTL8721F EVB（含 Wi-Fi 天线 + ST7262 RGB LCD 模块）
- MQTT Broker（支持 TLS 8883 端口，例如 EMQX Cloud）
- Windows PC（Python 3.7+，用于运行数据采集器）
- 另一块带 SHT3X 传感器的 Ameba MCU（可选，用于温湿度数据）

2️⃣ **LCD 模块选择**

项目支持两种 LCD 模块：

| 模块 | 分辨率 | 接口 | 驱动文件 | 启用方式 |
|------|--------|------|----------|----------|
| **ST7262**（默认） | 800×480 | RGB-565 并行 | `app_example/hal/lcd/st7262_cfg.c` | 默认，无需操作 |
| **DBL070** | 800×480 | RGB-565 并行 | `app_example/hal/lcd/dbl070_cfg.c` | 取消注释 `app_example/CMakeLists.txt` 中的 `add_definitions(-DUSE_DBL070)` |

引脚配置见 `app_example/hal/lcd/st7262_cfg.c` 和 `dbl070_cfg.c`。

> ⚠️ `-DUSE_DBL070` 宏会调整帧缓冲区基地址和 LCDC 时序参数以适配 DBL070 模块。两个驱动都会被编译，宏决定运行时激活哪一个。

3️⃣ **GPIO 按键映射**

| 操作 | ST7262 引脚 | DBL070 引脚 | 行为 |
|------|------------|-------------|------|
| 切换布局 | PB_0 | PB_16 | 短按循环切换 TRIAD → VORTEX → PULSE |
| 切换主题 | PA_31 | PB_14 | 短按循环切换 COBALT → INFERNO → SILICON |
| 亮度 ↑ | PA_21 | PB_15 | 短按 +10%，长按 ≥2s 跳至 100% |
| 亮度 ↓ | PA_27 | PB_17 | 短按 -10%，长按 ≥2s 跳至 10% |

自动配置上拉/下拉电阻。基于中断触发，硬件消抖时间 250ms。待机模式下布局/主题按键被禁用，仅保留亮度控制。

---

### 🚀 快速开始

1️⃣ **初始化 SDK 环境**

```bash
# 编辑 env.sh，设置 ameba-rtos SDK 的实际路径，然后执行：
source env.sh # Linux
.\env.bat     # Windows 
```

⚡ **需要 SDK 版本 release/v1.2** （该 SDK 版本已包含 LVGL 9.3 支持）。

2️⃣ **编译示例**

```bash
python ameba.py build
# 或使用别名：bb（编译）、bp（并行编译）
```

3️⃣ **配置参数** — 详见下方 [配置参考](#⚙️-配置参考)
- WiFi 凭证 → `wifi_reconnect.h`
- MQTT 凭证 → `pc_dashboard.h`
- 天气 → `weather.c`
- 告警阈值 → `threshold_config.h`

4️⃣ **运行 PC 端采集器**

> `PC/pc_to_emqx.py` 中的 MQTT Broker 设置需与 MCU 端保持一致。
```bash
cd PC
pip install -r requirements.txt
python pc_to_emqx.py
```
> ⚠️ 使用 `-d` 或 `--debug` 参数可运行调试模式（仅打印 JSON 到终端，不连接 MQTT）：
```bash
> python pc_to_emqx.py --debug
```

5️⃣ **烧录与串口监视**

```bash
python ameba.py flash --p COMx \
  --image boot.bin 0x08000000 0x8014000 \
  --image app.bin 0x08014000 0x8200000
python ameba.py monitor --port COMx --b 1500000
```

---

### 🎨 布局与主题

#### 📐 布局

| 布局 | 说明 | 视觉效果 |
|------|------|----------|
| **TRIAD** | 三列矩阵：CPU/内存/磁盘/电池（左）、GPU+磁盘I/O（中）、网络+系统（右） | 传统桌面仪表盘 |
| **VORTEX** | CPU 居中：大型 CPU 环形画布搭配粒子动画，RAM/磁盘/电池侧边栏，GPU 在环下方 | CPU 聚焦型 HUD |
| **PULSE** | 2×3 HUD 网格：第一行 CPU/内存/磁盘，第二行 GPU/电池/网络，底部全宽系统信息栏 | 紧凑型平视显示 |

#### 🎭 主题

| 主题 | 颜色 | 背景 |
|------|------|------|
| **COBALT** | Intel 蓝色系 | 平铺 Intel 标志水印 |
| **INFERNO** | AMD 红色系 | 平铺 AMD 标志水印 |
| **SILICON** | Apple 银灰色系 | 居中 Apple 标志水印 |

---

### 🔑 核心功能详解


#### 🌤️ 室外天气

仪表盘支持两种天气数据来源，通过 `WEATHER_FETCH_MCU` 宏切换（默认 `1` = MCU 直接获取）：

**MCU 模式（`WEATHER_FETCH_MCU=1`）**
MCU 每 10 分钟通过 HTTP GET 直接访问 OpenWeatherMap API，无需 PC 端配合。

配置项（编辑 `app_example/core/weather.c`）：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `WEATHER_API_KEY` | `0e5a...78ab5` | OpenWeatherMap API 密钥（[免费注册](https://openweathermap.org/api)） |
| `WEATHER_LAT` | `31.34` | 纬度（默认苏州姑苏区） |
| `WEATHER_LON` | `120.61` | 经度 |
| `WEATHER_CITY` | `"Gusu,Jiangsu"` | 城市显示名覆盖（留空 `""` 则使用 API 返回的自动检测名） |

> ⚠️ 建议使用坐标查询（`lat`/`lon`）而非城市名（`q=`），中国大陆城市坐标查询精度更高。

**PC 模式（`WEATHER_FETCH_MCU=0`）**
MCU 不发起 HTTP 请求，天气数据由 PC 端采集器通过 MQTT `pc/weather` 主题推送。MCU 端的 `weather_update_from_mqtt()` 解析 JSON 中的天气字段并更新 UI。此模式下 weather 任务仅保持休眠。

**UI 显示**
天气信息展示在底部环境栏右侧：天气图标 + 天气描述（如 "Clear"）+ 温度/湿度 + 城市名。

---

#### 🕐 待机时钟

PC 锁屏时，仪表盘自动切换到模拟时钟界面：

- **Sweep 指针扫入动画** — 首次获取有效时间后，秒/分/时针从 12 点方向以不同速度（秒针 1s、分针 1.5s、时针 2s）平滑扫到当前位置
- **日期窗口** — 3 点钟位置显示日期和星期，跨日自动更新
- **非正方形像素补偿** — DBL070 像素宽高比 1.078:1，表盘图像预补偿为 400×432，确保物理圆形
- **1Hz 精准刷新** — 每秒更新指针位置，仅重绘指针区域（非全屏），配合 VBlank 同步避免撕裂
- **SNTP 回退** — 若 MQTT 尚未传入时间戳，自动尝试 SNTP 获取时间

---

#### 💡 PWM 背光控制

通过 TIM4 硬件 PWM 驱动背光 MOSFET，支持 0%~100% 无级调节：

- **GPIO 按键控制** — BL_UP / BL_DOWN 按键，短按步进 10%，长按（≥2s）跳至极值（100% / 10%）
- **OSD 弹窗** — 亮度变化时在屏幕底部中央弹出 OSD（1.5s 自动消失），显示当前百分比和进度条
- **待机降亮度** — 进入待机模式自动降至 `BRIGHTNESS_STANDBY_PCT`（默认 20%），解锁恢复
- **伽马校正** — 背光亮度曲线为立方（gamma ≈ 3.0），将敏感的低 PWM 区域分配到更多用户步进，避免低亮度区间不可控
- **待机模式按键策略** — 布局/主题按键中断硬件禁用，仅保留亮度控制可用

#### ⚠️ 阈值告警系统

当监控指标超过 `threshold_config.h` 中设定的阈值时，对应卡片和进度条以 150ms 间隔闪烁红色告警。指标恢复正常后闪烁自动停止。

支持 6 类告警：CPU 使用率、CPU 温度、内存、磁盘、GPU、电池低电量、环境温度。

---


### 💻 PC 端采集器

Python 脚本（`PC/pc_to_emqx.py`）负责采集本地 PC 硬件状态并通过 MQTT 发布。

#### 🔧 主要功能

- **硬件监控** — CPU、内存、磁盘、GPU、网络、电池、Swap、磁盘 I/O
- **Libre Hardware Monitor（LHM）集成** — Windows 下通过 `pythonnet` 加载 `LibreHardwareMonitorLib.dll`，获取全面的传感器数据（CPU Package/核心温度、风扇转速、电压、功耗）。若 LHM 不可用则回退到 `nvidia-smi` / `wmic`。
- **锁屏检测** — 检测 Windows（LogonUI.exe）和 Linux（dbus logind）锁屏事件，通过 `pc/event` 主题发布，使 MCU 进入待机时钟模式。
- **LHM GPU 回填** — 为非 NVIDIA 显卡（Intel/AMD）补充 GPU 使用率、显存用量和温度数据。

#### 📊 采集指标

| 指标 | 数据来源 | MQTT Key |
|------|----------|----------|
| CPU 使用率 (%) | `psutil.cpu_percent()` | `cpu` |
| CPU 温度 (°C) | LHM / `psutil.sensors_temperatures()` | `cpu_temp` |
| CPU 频率 (MHz) | `psutil.cpu_freq()` | `cpu_freq_*` |
| 内存用量/百分比 | `psutil.virtual_memory()` | `mem`, `mem_total`, `mem_used` |
| 磁盘用量/百分比 | `psutil.disk_usage()` | `disk` |
| GPU 用量/显存/温度 | LHM / `nvidia-smi` / `wmic` | `gpu_*` |
| 网络速度 | `psutil.net_io_counters()`（差值） | `net_*_kbps` |
| 磁盘 I/O 利用率 | `psutil.disk_io_counters()` | `disk_io_percent` |
| 磁盘读写总量 | `psutil.disk_io_counters()` | `disk_read_bytes`, `disk_write_bytes` |
| Swap 用量 | `psutil.swap_memory()` | `swap_*` |
| 电池 | `psutil.sensors_battery()` | `battery_*` |
| 系统信息 | `platform.*`, `socket.gethostname()` | `hostname`, `os_platform` |
| 锁屏事件 | 进程/dbus 检测 | `pc/event`（独立主题） |

#### 📦 自动虚拟环境（Auto-Venv）

脚本启动时会自动在 `.venv` 虚拟环境中运行：

- 启动时检测是否已在虚拟环境中；如不在，通过 `.venv/Scripts/python`（Windows）或 `.venv/bin/python`（Linux/macOS）重新执行。
- 初始化方式：`python -m venv .venv && pip install -r PC/requirements.txt`

#### 📋 依赖

- Python 3.7+
- `psutil` — 系统状态采集
- `paho-mqtt` — MQTT 客户端
- `pythonnet`（Windows）— LHM DLL 集成
- `WMI`（Windows）— WMI GPU 检测

安装：`pip install -r PC/requirements.txt`

#### 🎮 GPU 支持

GPU 监控使用三级回退策略：`nvidia-smi`（NVIDIA）→ WMI `Win32_VideoController`（Intel/AMD）→ `wmic` CLI。LHM 数据用于为非 NVIDIA 显卡补充 GPU 使用率、显存和温度。GPU 监控失败不影响其他指标采集。

#### 🖥️ Libre Hardware Monitor（Windows）

如需全面的传感器数据（CPU Package 温度、风扇转速、电压、功耗），请安装 [LibreHardwareMonitor](https://github.com/LibreHardwareMonitor/LibreHardwareMonitor)（便携版，无需安装）。脚本会自动从默认下载路径或运行中的 LHM 进程查找 DLL。

#### 📡 MQTT 主题

| 主题 | 方向 | 载荷 | 间隔 |
|------|------|------|------|
| `pc/stats` | 发布 | 所有指标的平面 JSON | 每 3 秒 |
| `pc/event` | 发布 | `{"event": "lock"}` 或 `{"event": "unlock"}` | 锁屏状态变化时（retained） |

脚本向 `pc/stats` 主题发布平面 JSON 数据。示例：

```json
{
  "pc/cpu/pct": 23.4,
  "pc/cpu/temp_c": 51.0,
  "pc/ram/used": 8492347392,
  "pc/ram/pct": 55.7,
  "pc/disk/pct": 42.1,
  "pc/net/sent_kbps": 1.2,
  "pc/net/recv_kbps": 35.8,
  "pc/gpu/name": "NVIDIA GeForce RTX 3060",
  "pc/gpu/pct": 15.0,
  "pc/bat/power_plugged": true,
  ...
}
```

MCU 通过 `strstr()` 直接解析这些平面键值对，无需外部 JSON 库。

---

### ⚙️ 配置参考

#### 📶 WiFi 连接

编辑 `app_example/core/wifi_reconnect.h`：

```c
#define SSID                "你的WiFi名称"
#define PASSWORD            "你的WiFi密码"
```

#### 🔌 MQTT 连接

编辑 `app_example/core/pc_dashboard.h`：

```c
#define MQTT_BROKER_ADDRESS     "你的Broker地址.emqxsl.cn"
#define MQTT_CLIENT_ID          "PC_DASHBOARD_MCU_1_COM19"  /* 由 USE_DBL070 宏自动选择 */
#define MQTT_USERNAME           "你的用户名"
#define MQTT_PASSWORD           "你的密码"
```

#### 🌤️ 天气配置

编辑 `app_example/core/weather.c`：

| 宏 | 默认值 | 说明 |
|----|--------|------|
| `WEATHER_FETCH_MCU` | `1` | `1`=MCU HTTP 模式，`0`=PC MQTT 推送模式 |
| `WEATHER_API_KEY` | `0e5a...78ab5` | OpenWeatherMap API 密钥 |
| `WEATHER_LAT` | `31.34` | 纬度（默认苏州姑苏区） |
| `WEATHER_LON` | `120.61` | 经度 |
| `WEATHER_CITY` | `"Gusu,Jiangsu"` | 显示名（留空 `""` 自动获取） |

#### 🔔 告警阈值

编辑 `app_example/config/threshold_config.h`：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `cpu_pct` | 80.0% | CPU 使用率 |
| `cpu_temp_c` | 80.0°C | CPU 温度 |
| `ram_pct` | 80.0% | 内存使用率 |
| `disk_pct` | 90.0% | 磁盘使用率 |
| `gpu_pct` | 80.0% | GPU 使用率 |
| `bat_low_pct` | 20.0% | 电池低电量 |
| `env_temp_c` | 35.0°C | 环境温度 |
| `flash_interval_ms` | 150ms | 闪烁间隔 |

#### 📁 其他配置

| 文件 | 用途 |
|------|------|
| `app_example/config/lv_conf_project.h` | LVGL 覆盖 — 32 位 ARGB8888、128KB 堆、Montserrat 字体（8–48） |
| `app_example/config/sdk_compat.h` | SDK 兼容宏 — `COMPAT_CHECK_CONNECTIVITY` / `COMPAT_REQUEST_IP` |
| `app_example/config/suppress_mqtt_log.h` | MQTT 日志抑制 |
| `prj.conf` | SDK Kconfig — 启用 LVGL 9.3、WiFi等，sdk kconfg开关 |