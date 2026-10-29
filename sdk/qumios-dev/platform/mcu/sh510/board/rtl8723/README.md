# linux/board/rtl8723

RTL8723DS（WiFi+BT 合封）相关的 Linux 板级适配，当前主要用于本仓库 SH510 / llw_hd_bt。

| 路径 | 说明 |
|------|------|
| `qm_ble/` | 配网 BLE：`hcitool` 广播 + L2CAP ATT；含 RTK 天线/广播覆盖 workaround |
| `hal/ble/` | `qm_ble_gap`（多数 stub；读地址走 `hciconfig hci0`） |

依赖：BlueZ 工具链（`hciconfig` / `hcitool`）、`rtk_hciattach` 拉起 HCI。

通用 Linux HAL（socket / pthread / wpa_cli WiFi 等）仍在 `platform/mcu/linux/` 上层目录。
