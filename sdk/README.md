# 灵机 XJWB03 组合开发板 SDK

基于 **ESP32-S3** 的公版组合设备 SDK，硬件由 **XJWB03-DevKit-V1 核心底板** 与 **LJX40-Audio-KIT-V1 语音模块开发板** 组成。

通用能力（联网、配网、语音、音频、闹钟、播放）沉淀在 [qumios-dev](qumios-dev) 组件层，应用侧以插件形式集成在 `apps/main/lj_components`，每个插件由 `apps/main/qm_config/qm_config.h` 里的宏开关决定是否编入固件。

- 主控：ESP32-S3（Xtensa 双核，Wi-Fi + BLE 5），16 MB Flash
- 框架：ESP-IDF v5.4.1（源码已内置在本仓库 `esp-idf/` 目录）+ ESP-ADF 工程结构 + QUMIOS
- 应用工程：[apps](apps)，固件名 `vb701n.bin`
- 音频：VB701N 音频芯片，UART 通信，Opus 16 kHz / 单声道 / 20 ms

## 功能特性

| 功能 | 说明 |
| --- | --- |
| 语音对话 | 语音模块上报唤醒词 → Agora 会话，Opus 上行 / 下行，支持打断与退出 |
| 闹钟 | `qm_alarm` 组件 + 应用层 `app_alarm`；整表上报、下行 create/modify/delete、对时后自动重排 |
| 设备 IOT 控制 | 云端物模型属性下发，按 siid / piid 查表执行，当前为音量 |
| 番茄时钟 | `qm_pomodoro`，云端 `pomodoro/start` 等方法控制（默认宏关闭） |
| 资源播放 | MQTT 元数据 → `qm_player` 播放，PCM 解码适配，与对话共用音频通路（默认宏关闭） |
| 姿态识别 | 本地 YOLO11n-pose 模型，独立 `coco_pose` 分区（默认宏关闭） |
| 配网与联网 | BLE 配网 + Wi-Fi + 动态注册 + MQTT 物模型 |
| 调试命令行 | 内核 CLI，UART0 115200，提供 `alarm` 等命令 |
| 开机音效 | `boot_sound.pcm` 编入固件，上电后编码成 Opus 播放 |
| 固件升级 | 预留双 OTA 分区（`ota_0` / `ota_1`），`CONFIG_QM_IOT_OTA_SUPPORT` 当前为 0 |

## 仓库结构

```text
esp32_combo/
├── apps/                      应用工程
│   ├── main/
│   │   ├── main.c             启动顺序：外设 → 语音 → 云 → 配网 → agent → 插件
│   │   ├── qm_config/         宏开关与产品配置
│   │   ├── cloud/             连云、动态注册、上下行方法、配网、对时
│   │   ├── voice/             音频创建与开机音效
│   │   ├── agent/             语音会话封装
│   │   ├── lj_components/     业务插件：alarm / iot_control / playback / pomodoro
│   │   ├── peripheral/        板级外设驱动（灯带、舵机、马达、MPU6050、摄像头）
│   │   └── pose/              姿态识别示例与模型
│   ├── partitions.csv         分区表
│   ├── sdkconfig              目标 esp32s3 / 16 MB / DIO 80 MHz
│   └── build/                 编译产物（vb701n.bin）
├── qumios-dev/                QUMIOS 组件层（内置源码）
├── esp-idf/                   ESP-IDF v5.4.1 源码（内置）
├── install.bat                首次安装工具链
├── build.bat                  传统一键：编译 + 烧录 + 串口监视
├── dev.bat                    编译 / 烧录入口（推荐日常使用）
├── export.bat / export.ps1    导出 IDF 与 ADF 环境
├── DEV_BAT_CN.md              dev.bat 命令说明
└── WINDOWS_BUILD_CN.md        Windows 编译说明（已并入本 README）
```

## 快速开始

### 1. 前置条件

- Windows 10 / 11，建议用 Git 工作树获取代码（便于更新）；解压 ZIP 包时需保证 `esp-idf/` 等目录完整。
- Git for Windows，`git --version` 可执行。
- Python 3，`python` 可执行；如果指向 Microsoft Store 的应用别名，请先在「应用执行别名」中关闭。
- 能访问主仓库（Gitee），以及 ESP-IDF 工具链的下载地址。
- 首次安装需要网络下载 CMake、Ninja、Xtensa 工具链。

### 2. 获取代码

```bat
git clone https://gitee.com/xj-open/ljyd_xjwb03_agent_public_developer.git
cd ljyd_xjwb03_agent_public_developer
```

### 3. 安装工具链（只需一次）

根目录执行：

```bat
install.bat
```

它直接复用仓库内置的 `esp-idf` 源码，调用 `esp-idf\install.bat` 准备 CMake、Ninja、Python 虚拟环境和 Xtensa 工具链。工具目录默认在 `%USERPROFILE%\.espressif`，可用环境变量 `IDF_TOOLS_PATH` 指定。

### 4. 编译与烧录

日常使用 `dev.bat`（不依赖 `export.bat`，自己设置 `ADF_PATH` / `IDF_PATH` 和工具链 PATH）：

| 命令 | 作用 |
| --- | --- |
| `dev.bat build` | 增量编译 |
| `dev.bat rebuild` | 删除 `apps\build` 后全量编译（改宏、换 PID、换分区表后用） |
| `dev.bat clean` | 删除整个编译缓存 |
| `dev.bat clean obj` | 只清对象文件，保留 `sdkconfig` |
| `dev.bat flash -p COMx` | 只烧应用分区（`idf.py app-flash`） |
| `dev.bat flashall -p COMx` | 按 `apps\build\flash_args` 烧写全部镜像 |
| `dev.bat flashall -p COMx erase` | 先整片擦除再全烧（会清掉配网信息与 KV） |
| `dev.bat help` | 显示帮助 |

说明：

- 串口必须用 `-p COMx` 指定，也可以先设置环境变量 `DEV_PORT`。烧录波特率 921600。
- `flashall` 读取 `flash_args` 中的全部条目，共 6 个镜像，包含 `0x810000` 处的姿态模型。**首次烧录、整片擦除、换分区表后必须用 `flashall`**，只烧应用会留下空的模型分区，导致启动崩溃重启。
- 编译产物在 `apps\build\`，应用固件为 `vb701n.bin`。
- `dev.bat` 的 Python 环境目录按本机 ESP-IDF 安装结果写死为 `python_env\idf5.4_py3.14_env`。如果报 `python env not found`，用 `IDF_TOOLS_PATH` 指向实际工具目录，或把 `dev.bat` 里的环境名改成 `python_env` 下的真实目录。
- 仓库整体搬动后，`apps\dependencies.lock` 里记录的本机绝对路径会失效；删掉该文件后重新 `dev.bat build`，组件管理器会按当前目录重新生成。

也可以使用传统的 `build.bat`：编译 → 烧录固件（`idf.py flash`）→ 打开串口监视器；它不擦除 Flash，退出监视用 `Ctrl+]`。

### 5. 串口日志

UART0，115200 8N1。未配网时上电可见：

```text
qm_audio ready, opus 20ms
opus ready ... 16000 Hz mono 20 ms ...
ble provision start ret=0
cloud connecting
iot start ok
cloud connected
```

`iot start ok` 表示云端会话任务已创建；若这里失败，常见原因是内部 RAM 不足（见「插件与宏开关」中的姿态识别说明）。

## Windows 编译说明

本节由 `WINDOWS_BUILD_CN.md` 整理并入。

### 一键安装与编译

首次使用或更换工具链，先在仓库根目录执行 `install.bat`，再执行 `build.bat`。`build.bat` 会自动调用 `export.bat`、编译 `apps`、烧录固件并启动串口监视器。

PowerShell：

```powershell
Set-Location C:\esp32_combo
.\install.bat
.\build.bat
```

`cmd.exe`：

```bat
cd /d C:\esp32_combo
call install.bat
call build.bat
```

注意：`build.bat` 内部会 `call export.bat`。如果本机因 openocd / gdb 版本不匹配导致 `export.bat` 报 `Activation script failed`，直接用 `dev.bat build` / `dev.bat flashall -p COMx`，它不依赖 export 脚本。

### 手动流程（PowerShell，推荐）

每个新开的 PowerShell 窗口都要初始化一次：

```powershell
Set-Location C:\esp32_combo
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
.\export.ps1
```

成功时输出 `ESP-IDF environment setup completed successfully.`，然后在同一窗口编译：

```powershell
Set-Location .\apps
idf.py build
```

`export.ps1` 会强制使用本仓库内置的 `esp-idf` 与 `qumios-dev`，并应用仓库中的 IDF 补丁。环境只在当前窗口有效，不能换个窗口再执行 `idf.py`。

### cmd.exe

```bat
cd /d C:\esp32_combo
call export.bat
cd /d apps
idf.py build
```

批处理里调用 `export.bat` 必须加 `call`；PowerShell 中应使用 `export.ps1`，直接运行 `.bat` 时环境变量只存在于子进程。

### VS Code ESP-IDF 插件

1. 用 VS Code 打开 `apps` 目录（这是 ESP-IDF 工程目录）。
2. 插件使用本仓库的 ESP-IDF：`<仓库根目录>\esp-idf`。
3. `apps\sdkconfig` 已配置 `esp32s3`，串口选择实际 COM 口。
4. 使用插件的 Build / Flash / Monitor。

插件需要读取 `ADF_PATH`，建议从已初始化的终端启动：

```powershell
code .\apps
```

或在 `apps\.vscode\settings.json` 中写入：

```json
{
  "idf.customExtraVars": {
    "ADF_PATH": "C:\\esp32_combo",
    "IDF_PATH": "C:\\esp32_combo\\esp-idf"
  }
}
```

路径不同时只替换 `ADF_PATH` 与 `IDF_PATH`。

### 手动烧录与监视

```powershell
Set-Location C:\esp32_combo\apps
idf.py -p COMx flash monitor
```

退出监视器：`Ctrl+]`。

### 常见问题

| 现象 | 原因 | 处理 |
| --- | --- | --- |
| 提示无法识别 `idf.py` | 当前窗口未导出环境 | 回到仓库根目录执行 `.\export.ps1`（PowerShell）或 `call export.bat`（cmd） |
| `install.bat` 找不到 Python | 命令不在 PATH，或 Python 指向应用别名 | 安装后重开终端；关闭 Microsoft Store 的 Python 别名 |
| `esp-idf` 目录缺失或不完整 | 工作树不完整（例如用了裁剪过的压缩包） | 重新完整 clone 主仓库，再执行 `install.bat` |
| `build.bat` 提示 IDF 或 qumios-dev 未初始化 | 未执行 `install.bat`，或目录是空的 | 先完整执行 `install.bat`，确认存在 `esp-idf\tools\idf.py` 与 `qumios-dev\components\qm_agent\include\qm_agent.h` |
| `Activation script failed` | openocd / riscv32 gdb 版本不匹配 | 使用 `dev.bat` |
| 仓库不在 `C:\esp32_combo` | 脚本按仓库相对路径工作 | 命令与配置中的路径替换为实际路径，含空格时加引号 |

## 默认驱动使用的引脚

下表是当前固件的默认分配，定义位置均在 `apps/main` 下。

| GPIO | 用途 | 方向 | 默认启用 | 定义位置 | 备注 |
| --- | --- | --- | --- | --- | --- |
| 0 | 芯片 BOOT / strapping | 输入 | 芯片默认 | — | 底板引出 BOOT 键时，烧录失败可按住再上电 |
| 1 | WS2812 灯带 DI | 输出 | 否 | `peripheral/bsp_ws2812.h` | 默认 8 颗；灯带只接 DI，DO 空悬 |
| 4 | 舵机 PWM | 输出 | 否 | `peripheral/bsp_servo.h` | 与摄像头 CS 复用，二选一 |
| 5 | 振动马达 | 输出 | 否 | `peripheral/bsp_vibrator.h` | 经三极管 / MOS 驱动，马达电源独立并与板子共地 |
| 8 | MPU6050 SDA / 摄像头 DATA0 | 双向 | 否 | `bsp_mpu6050.c` / `bsp_camera.h` | 复用，二选一；两者默认都未启用 |
| 9 | MPU6050 SCL | 输出 | 否 | `peripheral/bsp_mpu6050.c` | I2C 时钟 |
| 13 | 摄像头 PCLK | 输入 | 否 | `peripheral/bsp_camera.h` | 摄像头由 `CONFIG_APP_CAMERA_TEST` 控制 |
| 14 | 复位 / 解绑按键 | 输入 | 是 | `peripheral/bsp_io.c` | 低电平长按 3 秒：在线则解绑，离线则清配网并重启 |
| 21 | 摄像头 SDA | 双向 | 否 | `peripheral/bsp_camera.h` | 摄像头未启用 |
| 38 | VB701N 硬件流控 | 输出 | 是 | `qm_config/qm_config.h` | 音频 UART 流控 |
| 39 | VB701N UART RX | 输入 | 是 | `qm_config/qm_config.h` | 音频数据接收 |
| 40 | VB701N UART TX | 输出 | 是 | `qm_config/qm_config.h` | 音频数据发送 |
| 41 | 摄像头 PWDN | 输出 | 否 | `peripheral/bsp_camera.h` | 摄像头未启用 |
| 43 | 日志 / CLI TX | 输出 | 是 | `qm_config/qm_config.h` | UART0，115200 |
| 44 | 日志 / CLI RX | 输入 | 是 | `qm_config/qm_config.h` | UART0，115200 |
| 47 | 摄像头 SCL | 输出 | 否 | `peripheral/bsp_camera.h` | 摄像头未启用 |

注意事项：

- **GPIO39 / 40 / 38** 为音频芯片专用，**GPIO43 / 44** 为日志与命令行专用，不要再外接其它设备。
- **GPIO4** 舵机与摄像头 CS 复用，**GPIO8** MPU6050 与摄像头 DATA0 复用；当前摄像与传感器功能默认都未启用，同时使用时需改引脚。
- 舵机底层 `qm_pwm` 支持多路，当前只封装了一路（GPIO4）。
- 改引脚：音频与调试口改 `qm_config.h` 中的 `CONFIG_QM_VB701N_*` / `CONFIG_QM_DEBUG_UART_*`，外设改对应 `bsp_*.h` 中的宏。

## 分区表

`apps/partitions.csv`，16 MB Flash：

| 分区 | 偏移 | 大小 | 用途 |
| --- | --- | --- | --- |
| `nvs` | 0x11000 | 48 KB | 配网凭据、音量、闹钟等 KV 数据 |
| `otadata` | 0x1D000 | 8 KB | OTA 启动选择 |
| `phy_init` | 0x1F000 | 4 KB | PHY 初始化数据 |
| `ota_0` | 0x20000 | 4032 KB | 应用固件 |
| `ota_1` | 0x410000 | 4032 KB | OTA 备份固件 |
| `model` | 0x800000 | 64 KB | 模型数据 |
| `coco_pose` | 0x810000 | 3200 KB | 姿态识别模型（`.espdl`） |
| `spiffs_data` | 0xB30000 | 1024 KB | 文件系统 |
| `lvgl` | 0xC30000 | 3072 KB | 界面资源预留 |

## 插件与宏开关

开关统一在 `apps/main/qm_config/qm_config.h`。`apps/main/CMakeLists.txt` 用 `file(READ ...)` + 正则读取这些宏，并登记 `CMAKE_CONFIGURE_DEPENDS`，改宏后重新编译会自动 reconfigure。

| 插件 | 位置 | 开关宏 | 当前默认 | 置 0 的影响 |
| --- | --- | --- | --- | --- |
| 闹钟 | `lj_components/alarm` | `CONFIG_QM_ALARM_SUPPORT` | 1 | 应用层与平台 `qm_alarm` 都不编译 |
| 资源播放 | `lj_components/playback` | `CONFIG_LJYD_PLAYBACK_SUPPORT` | 0 | 播放器不编译、不初始化 |
| 番茄时钟 | `lj_components/pomodoro` | `CONFIG_QM_POMODORO_SUPPORT` | 0 | 不编译；自动开始另由 `CONFIG_APP_POMODORO_AUTO_START` 控制 |
| 姿态识别 | `pose` | `CONFIG_QM_POSE_SUPPORT` | 0 | 不编译姿态示例与模型推理 |
| 摄像头取流 | `peripheral` | `CONFIG_APP_CAMERA_TEST` | 0 | 不编译取流代码 |
| 语音后端 | `agent` | `CONFIG_VOICE_BACKEND_AGORA` | 1 | 切换语音链路后端 |
| 设备 IOT 控制 | `lj_components/iot_control` | 无（常开） | 开启 | 当前没有开关宏，需要时手改 `main.c` |

关于姿态识别：模型在初始化时会占用大块内部 RAM，开机同时运行会导致云端 session 任务（8 KB 栈）创建失败，表现为小程序配网卡在 99%。因此默认关闭；需要调试姿态时再打开，并用 `dev.bat flashall` 烧录 `coco_pose` 分区。

新增插件：在 `qm_config.h` 加开关宏 → 在 `CMakeLists.txt` 条件追加源文件 → 在 `main.c` 用同名宏包住 include 与初始化调用。只包调用点不够，组件源文件整体在宏内会编成空文件，必须一起控制。

## 产品配置

产品身份只在一处配置：`apps/main/cloud/ljyd_cloud.h`。

```c
/* 灵机平台产品身份。换产品改这里。 */
#define CONFIG_XJIANG_CLOUD_PID    11665
#define CONFIG_XJIANG_CLOUD_SECERT "F7B6033204064B698FEDE033605DDDBA"
```

- 示例 PID `11665` 属于测试服，换产品改这两行后需要 `dev.bat rebuild` 并重新烧录。
- BLE 广播名为 `xj-<PID>-<MAC 后两字节>`，PID 与平台产品不一致时小程序搜不到设备。
- 动态注册环境在配网时写入 KV（`cloud/env`），未配置时默认使用 PROD。

## 相关文档

| 文档 | 内容 |
| --- | --- |
| [DEV_BAT_CN.md](DEV_BAT_CN.md) | `dev.bat` 命令与注意事项 |
| [WINDOWS_BUILD_CN.md](WINDOWS_BUILD_CN.md) | Windows 编译说明（已并入本 README） |
| [apps/README_CN.md](apps/README_CN.md) | 应用层目录、启动顺序、上下行方法 |
| [apps/main/lj_components/alarm/README_CN.md](apps/main/lj_components/alarm/README_CN.md) | 闹钟模块与响铃链路 |
| [apps/main/lj_components/iot_control/README_CN.md](apps/main/lj_components/iot_control/README_CN.md) | 物模型与音量控制 |
| [ESP-IDF 文档](https://docs.espressif.com/projects/esp-idf/zh_CN/latest/esp32s3/) | ESP32-S3 开发框架 |
| [ESP-ADF 文档](https://docs.espressif.com/projects/esp-adf/en/latest/) | 音频框架（本仓库为 ADF 工程结构） |
