# Linux 平台适配

pthread + BSD socket 的 QUMI OS/HAL 移植，面向运行 Linux 的 ARM 设备（如亿智方案）。

## 目录

| 路径 | 说明 |
| --- | --- |
| `qmos/` | OS 抽象（线程、队列、定时器、内存） |
| `qm_db/` | 文件 KV |
| `hal/net/` | TCP、UDP、TLS |
| `hal/wifi/` | WiFi：get_mac / 连接状态 / RSSI；connect/disconnect/scan 通过 `wpa_cli` |
| `hal/ota/` | OTA 写入 `/data/qumi_ota.bin`，需手动替换可执行文件 |
| `hal/soc/` | GPIO（sysfs）、UART（`/dev/ttyS*`）、SPI（`/dev/spidev0.*`）、RTC |
| `board/rtl8723/` | **RTL8723DS 板级**：BLE（`qm_ble/` + `hal/ble/`），见该目录 README |
| `qm_linux_stub.h` | 未实现接口统一告警宏 |
| `thirdparty/openssl/` | **编译期** OpenSSL 1.1 头文件 |

未挪入 `board/rtl8723` 的说明：`/app`、`/data` 等属产品路径习惯；WiFi 走通用 `wpa_cli`，与 RTK BLE workaround 无关。

## thirdparty/openssl

- **用途**：交叉编译 `hal/net/qm_tls.c` 时使用，仅提供头文件。
- **运行时**：链接设备上的 `libssl.so.1.0.0` / `libcrypto.so.1.0.0`（见 demo 工程 `sysroot/lib/`）。
- **说明**：头文件为 1.1，运行库为 1.0，`qm_tls.c` 内已做 API 兼容处理。

## 接入

在应用 Makefile 中编译 `platform/mcu/linux/` 下源文件，并 `-I$(LINUX_PLAT)/thirdparty/openssl/include`。参考 `demo/qumi.mk`。
