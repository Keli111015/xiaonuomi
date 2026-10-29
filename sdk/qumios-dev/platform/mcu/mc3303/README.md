# MC3303 平台适配

Fullhan MC3303（SDK 目录名为 MC632）上的 Linux HAL。本机是 combo 架构：
WiFi/BLE 由 combo MCU 管理，Linux 侧通过虚拟网卡 `vnet0` 上网，不走
`wpa_cli` / `wpa_supplicant`。

对照旧 qumi 的 `platform/mcu/linux`，为 qumios-dev 单独做的一套配置。


## 目录

| 路径 | 说明 |
| --- | --- |
| `qmos/` | pthread OS（任务、锁、信号量、事件、队列、定时器） |
| `qm_db/` | 文件 KV，路径 `/start/qm_kv`（与老 linux 一致） |
| `hal/net/` | BSD socket TCP/UDP；TLS 用设备上的 OpenSSL（与老 linux 一致） |
| `hal/wifi/` | 不配网，只读取当前网卡 IPv4 并上报 GOT_IP；MAC 从 `vnet0` 读 |
| `hal/ota/` | 写入 `/mnt/data/audio_main.bin.tmp`，结束时走 combo/本地升级 |
| `hal/soc/` | GPIO 空实现；RTC 用系统时钟；UART/SPI 空实现 |
| `hal/ble/` | GAP 空实现（BLE 在 combo MCU） |

## 接入

在应用 Makefile 中编译 `platform/mcu/mc3303/` 下源文件，头文件用工程自带的
`thirdparty/openssl`，不要包含 `platform/mcu/sh510/thirdparty/openssl`。
