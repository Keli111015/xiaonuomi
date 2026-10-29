# BK7258 (Beken Armino)

QumIOS 在博通 BK7258 / Armino SDK 上提供 OS、KV、WiFi、
TCP/UDP/TLS 和 BLE HAL 适配，供配网与连云使用。

## 目录

- 工程侧 `main/qm_config/qm_config.h` — 产品宏（OS/WiFi/BLE/Cloud…），由 `qumios` 组件引用
- `qmos/qmos_bk.c` — `qm_kernel` 对接 Armino `rtos_*`
- `qm_db/qm_hal_kv.c` — EasyFlash KV
- `hal/wifi/qm_wifi.c` — `bk_wifi` / `bk_event`
- `hal/net/qm_tcp.c` / `qm_udp.c` / `qm_tls.c` — lwIP + mbedTLS
- `hal/ble/qm_ble_gap.c`、`qm_ble/qm_ble_hal_*.c` — BLE GATT/配网
- `qumios/CMakeLists.txt` — Armino 组件 `qumios`（源文件与依赖）

## 构建

外层工程根 `CMakeLists.txt` 设置：

```cmake
set(EXTRA_COMPONENTS_DIRS "${CMAKE_CURRENT_LIST_DIR}/qumios-dev/platform/mcu/bk7258")
```

`main` 使用 `REQUIRES qumios`。SDK 侧建议打开 `CONFIG_BLUETOOTH` / `CONFIG_BLE` / `CONFIG_EASY_FLASH_V4`（`bk_demo/config/bk7258/config` 已加）。
