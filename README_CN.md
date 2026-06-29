* [English Version](./README.md)

### Ameba RTL8721F 仪表盘监控（FreeRTOS）

🚀 一个 PC 硬件资源监控器，通过 MQTT 订阅 `pc/stats`（PC 状态）和 `humiture/measurement`（SHT3X 传感器数据）主题，接收实时系统状态和环境数据。在 **Ameba RTL8721F** 微控制器上解析 JSON，并通过 **LVGL 9.3** 驱动 **ST7262 TFT**（默认，800×480）或 **DBL070 TFT**（可选）彩色屏幕实时显示仪表盘。

MCU 仅作为订阅者 — 只接收数据，不发布任何消息。

- 📄 [芯片与模块信息](https://aiot.realmcu.com/cn/module/index.html)

---

### ✨ 功能特点

- ✅ **MQTT 订阅** — 通过 TLS 8883 加密连接，订阅 `pc/stats` 和 `humiture/measurement` 主题。
- ✅ **ST7262（默认）或 DBL070（可选）TFT 仪表盘** — 800×480，基于 LVGL 9.3，双缓冲 + VBlank 页翻转（无撕裂）。
- ✅ **CPU / 内存 / 磁盘** — 彩色进度条，支持可配置阈值闪烁告警。
- ✅ **GPU 监控** — 使用率、显存、温度及 GPU 型号名称。
- ✅ **网络** — 上传/下载速度（KB/s），带箭头图标。
- ✅ **CPU 温度** — 当 PC 端采集器支持时显示。
- ✅ **CPU 频率** — 当前 / 最小 / 最大 MHz。
- ✅ **交换分区（Swap）** — 使用百分比、总量和已用量。
- ✅ **SHT3X 温湿度** — 通过 MQTT 主题获取，无需本地传感器。
- ✅ **电池** — 电量百分比 + 充电状态指示。
- ✅ **系统信息** — 用户名、进程数、物理/逻辑 CPU 核心数、主机名、操作系统平台。
- ✅ **时钟** — 启动时间和当前时间（从 Unix 时间戳转换，支持 UTC+8 偏移）。
- ✅ **磁盘 I/O** — 总读取/写入字节数及 I/O 利用率百分比。
- ✅ **Wi-Fi 自动连接** — 支持可配置重试次数。
- ✅ **MQTT TLS 加密连接**。
- ✅ **3 种仪表盘布局** — 通过 GPIO 按键循环切换（TRIAD → VORTEX → PULSE）。
- ✅ **3 种颜色主题** — 通过 GPIO 按键循环切换（COBALT 蓝色 / INFERNO 红色 / SILICON 银色）。
- ✅ **淡入淡出动画** — 布局/主题切换时 200ms 透明度过渡。
- ✅ **可配置阈值闪烁系统** — 当指标超过告警级别时，卡片边框和进度条闪烁提醒。

---

### 🧠 工作原理概述

1. 系统启动后，初始化 ST7262 LCD、LVGL 9.3 界面和 Wi-Fi 连接。
2. Wi-Fi 连接成功后，MQTT 客户端订阅 `pc/stats` 和 `humiture/measurement` 主题。
3. 两个数据源向 MQTT Broker 发布数据：
   - **PC 状态** — Python 脚本通过 `psutil` 采集硬件信息，发布到 `pc/stats`。
   - **SHT3X 传感器** — 另一块 Ameba MCU 读取温湿度数据，发布到 `humiture/measurement`。
4. 仪表盘 MCU 按主题路由收到的 JSON 数据，解析后实时更新 LVGL 显示。
5. 物理 GPIO 按键循环切换 3 种布局和 3 种主题。

```text
Windows PC (psutil) ──MQTT──►  pc/stats              ┌──────────────────────────┐
                               MQTT Broker (TLS 8883) │  Ameba RTL8721F         │
SHT3X MCU ──────────MQTT──►  humiture/measurement    │  • 订阅 pc/stats         │
                                                     │  • 订阅 humiture/..      │
                                                     │  • 按主题路由            │
                                                     │  • 3 种布局 / 3 种主题   │
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
| **ST7262**（默认） | 800×480 | RGB-565 并行 | `app_example/drv/lcd/st7262_cfg.c` | 默认，无需操作 |
| **DBL070** | 800×480 | RGB-565 并行 | `app_example/drv/lcd/dbl070_cfg.c` | 取消注释 `app_example/CMakeLists.txt` 中的 `add_definitions(-DUSE_DBL070)` |

引脚配置见 `app_example/drv/lcd/st7262_cfg.c` 和 `dbl070_cfg.c`。

> `-DUSE_DBL070` 宏会调整帧缓冲区基地址和 LCDC 时序参数以适配 DBL070 模块。两个驱动都会被编译，宏决定运行时激活哪一个。

3️⃣ **GPIO 按键映射**

| 操作 | ST7262 引脚 | DBL070 引脚 |
|------|------------|-------------|
| 切换布局 | PB_0 | PB_16 |
| 切换主题 | PA_31 | PB_14 |

自动配置上拉/下拉电阻。基于中断触发，硬件消抖时间 250ms。

---

### 🚀 快速开始

1️⃣ **初始化 SDK 环境**

```bash
# 编辑 env.sh，设置 ameba-rtos SDK 的实际路径，然后执行：
source env.sh
```

在 Windows 下使用 `env.ps1`，需要 SDK 的 `env.bat` 文件及环境变量 `AMEBA_ENV_PATH`：
```powershell
.\env.ps1
```

⚡ **需要 SDK 版本 release/v1.2**。该 SDK 版本已包含 LVGL 9.3 支持。

---

2️⃣ **编译示例**

```bash
python ameba.py build
# 或使用别名：bb（编译）、bp（并行编译）
```

---

3️⃣ **配置 MQTT 凭证**

编辑 `app_example/pc_dashboard.h`：

```c
#define MQTT_BROKER_ADDRESS     "你的Broker地址.emqxsl.cn"
#define MQTT_CLIENT_ID          "PC_DASHBOARD_001"
#define MQTT_USERNAME           "你的用户名"
#define MQTT_PASSWORD           "你的密码"
```

编辑 `app_example/WiFi_reconnect.h`：

```c
#define SSID                "你的WiFi名称"
#define PASSWORD            "你的WiFi密码"
```

---

4️⃣ **配置阈值告警**

编辑 `app_example/threshold_config.h` 调整各指标的闪烁触发阈值：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `cpu_pct` | 70.0% | CPU 使用率闪烁阈值 |
| `cpu_temp_c` | 60.0°C | CPU 温度阈值 |
| `ram_pct` | 80.0% | 内存使用率阈值 |
| `disk_pct` | 90.0% | 磁盘使用率阈值 |
| `gpu_pct` | 80.0% | GPU 使用率阈值 |
| `bat_low_pct` | 90.0% | 电池低电量阈值（调试值） |
| `env_temp_c` | 28.0°C | 环境温度阈值 |
| `flash_interval_ms` | 150ms | 闪烁间隔时间 |

---

5️⃣ **运行 PC 端采集器**

```bash
cd PC
pip install -r requirements.txt
python pc_to_emqx.py
```

编辑 `PC/pc_to_emqx.py` 中的 MQTT Broker 设置，与 MCU 端保持一致(your id/password and broker ...)。

使用 `-d` 或 `--debug` 参数可运行调试模式（仅打印 JSON 到终端，不连接 MQTT）：
```bash
python pc_to_emqx.py --debug
```

---

6️⃣ **烧录与串口监视**

```bash
python ameba.py flash --p COMx \
  --image boot.bin 0x08000000 0x8014000 \
  --image app.bin 0x08014000 0x8200000

python ameba.py monitor --port COMx --b 1500000
```

---

### 🎨 布局与主题

#### 布局

| 布局 | 说明 | 视觉效果 |
|------|------|----------|
| **TRIAD** | 三列矩阵：CPU/内存/磁盘/电池（左）、GPU+磁盘I/O（中）、网络+系统（右） | 传统桌面仪表盘 |
| **VORTEX** | CPU 居中：大型 CPU 环形画布搭配粒子动画，RAM/磁盘/电池侧边栏，GPU 在环下方 | CPU 聚焦型 HUD |
| **PULSE** | 2×3 HUD 网格：第一行 CPU/内存/磁盘，第二行 GPU/电池/网络，底部全宽系统信息栏 | 紧凑型平视显示 |

#### 主题

| 主题 | 颜色 | 背景 |
|------|------|------|
| **COBALT** | Intel 蓝色系 | 平铺 Intel 标志水印 |
| **INFERNO** | AMD 红色系 | 平铺 AMD 标志水印 |
| **SILICON** | Apple 银灰色系 | 居中 Apple 标志水印 |

---

### 📝 日志示例

```text
=== PC Dashboard ===
FB base1=0x0C000000 base2=0x0C096000 driver=st7262
GPIO buttons initialized
LVGL UI ready, starting main loop...
Wi-Fi connected.
MQTT start
Connect Network "your-broker.emqxsl.cn"
Received PC stats (412 bytes)
PC stats updated: CPU=12.5%, MEM=62.3%, DISK=47.9%, NET=↑1.9/↓28.6 KB/s
Received SHT3X data (68 bytes)
SHT3X updated: 24.5 C, 48.2 %
```

> 实际日志输出可能因 SDK 版本和运行环境而异。

---

---

### 💻 PC 端采集器

Python 脚本（`PC/pc_to_emqx.py`）负责采集本地 PC 硬件状态并通过 MQTT 发布。

#### 采集指标

| 指标 | 数据来源 | MQTT Key |
|------|----------|----------|
| CPU 使用率 (%) | `psutil.cpu_percent()` | `pc/cpu/pct` |
| CPU 温度 (°C) | `psutil.sensors_temperatures()` | `pc/cpu/temp_c` |
| CPU 频率 (MHz) | `psutil.cpu_freq()` | `pc/cpu/freq_cur`, `freq_min`, `freq_max` |
| 内存用量/百分比 | `psutil.virtual_memory()` | `pc/ram/used`, `pc/ram/pct` |
| 磁盘用量/百分比 | `psutil.disk_usage()` | `pc/disk/used`, `pc/disk/pct` |
| GPU 用量/显存/温度 | `nvidia-smi`（子进程） | `pc/gpu/...` |
| 网络速度 | `psutil.net_io_counters()`（差值计算） | `pc/net/sent_kbps`, `pc/net/recv_kbps` |
| 磁盘 I/O | `psutil.disk_io_counters()` | `pc/disk_io/...` |
| 交换分区（Swap） | `psutil.swap_memory()` | `pc/swap/used`, `pc/swap/pct` |
| 电池 | `psutil.sensors_battery()` | `pc/bat/pct`, `pc/bat/power_plugged` |
| 系统信息 | `platform.*`, `psutil.*` | `pc/sys/...` |

#### 自动虚拟环境（Auto-Venv）

脚本启动时会自动创建并使用名为 `venv_mqtt` 的 Python 虚拟环境：

- 首次运行时自动创建 `./venv_mqtt/` 并从 `requirements.txt` 安装依赖。
- 后续运行直接复用已有环境，无需手动执行 `venv activate`。
- 如需重新安装，删除 `venv_mqtt/` 目录后重新启动即可。

#### 依赖

- Python 3.7+
- `psutil` — 系统状态采集
- `paho-mqtt` — MQTT 客户端

手动安装：`pip install -r PC/requirements.txt`

#### GPU 支持

GPU 监控通过子进程调用 `nvidia-smi` 实现。非 NVIDIA 显卡会记录警告信息。GPU 监控失败不影响其他指标的采集。

#### MQTT 主题

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

### 🔧 配置参考

| 文件 | 用途 |
|------|------|
| `prj.conf` | SDK Kconfig 配置（由 `menuconfig.py -s prj.conf` 生成），启用 LVGL 9.3、JPEG、DHCP |
| `lv_conf_project.h` | LVGL 配置覆盖：32 位 ARGB8888 色彩、128KB 堆内存、完整 Montserrat 字体族（8–48） |
| `threshold_config.h` | 所有监控指标的闪烁告警阈值 |
| `sdk_compat.h` | SDK 版本兼容宏（`COMPAT_CHECK_CONNECTIVITY` / `COMPAT_REQUEST_IP`） |

### 项目结构

```
.
├── app_example/
│   ├── app_main.c              # 入口函数，创建任务线程
│   ├── pc_dashboard.c/h   # MQTT 客户端、JSON 解析、PC_Stats_t 数据结构
│   ├── pc_dashboard_ui.c/h     # UI 生命周期、定时器回调
│   ├── pc_dashboard_layout.c/h # V3 布局系统（TRIAD/VORTEX/PULSE）
│   ├── pc_dashboard_theme.c/h  # 颜色主题（COBALT/INFERNO/SILICON）
│   ├── gpio_control.c/h        # GPIO 按键中断 → 延迟切换
│   ├── WiFi_reconnect.c/h # Wi-Fi 自动连接
│   ├── threshold_config.h      # 告警闪烁阈值
│   ├── sdk_compat.h            # SDK 版本兼容层
│   ├── lv_conf_project.h       # LVGL 配置覆盖
│   ├── drv/lcd/                # LCD 驱动（ST7262、DBL070、LCDC 核心）
│   ├── img_icons/              # 22 个 LVGL 图标资源（C 数组）
│   ├── img_bg/                 # 3 个主题背景图片
│   ├── scripts/                # PNG 转 LVGL C 数组脚本
├── PC/                         # PC 端 Python 采集器
├── env.sh                      # Linux/macOS 环境配置
├── env.ps1                     # Windows PowerShell 环境配置
├── env.bat                     # Windows cmd 环境配置
├── CMakeLists.txt              # 顶层 CMake
├── prj.conf                    # SDK Kconfig
└── Kconfig                     # SDK 配置参考
```
