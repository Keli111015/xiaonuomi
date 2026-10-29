# macOS 平台适配

该目录按 ESP32 的平台结构提供 macOS 主机后端。QUMI 公共接口和业务代码保持
C；只有调用 Apple 系统框架的底层文件使用 Objective-C。

## 已实现目录

- `qmos/`：基于 pthread 的任务、互斥锁、信号量、事件、队列、定时器、内存和
  时间能力。主机环境忽略任务核绑定。
- `hal/ble/`：CoreBluetooth GAP 和 QMS GATT 外设适配。
- `qm_ble/`：QUMI BLE OS、CommonCrypto AES-CBC、OTA 桥接及 GATT 兼容目录。
- `qm_audio/`：基于 AudioQueue 的双向 linear PCM 驱动，包含麦克风授权、带超时采集、
  有界 ring buffer、固定播放 buffer 池、音量/静音/停止和录音策略控制。
- `hal/net/`：BSD TCP/UDP，以及基于仓库 mbedTLS 的 TLS 1.2。支持系统/指定 CA、
  主机名校验、SNI、ALPN、客户端证书和服务端证书。
- `hal/wifi/`：通过 `networksetup`、`airport` 和系统网卡 API 支持 STA 连接/断开、扫描、
  真实 IPv4/掩码查询、网卡 MAC 读取及事件转换。沙盒或较新的 macOS 可能没有相关工具；适配层会返回
  对应错误，不会把失败的扫描/连接报告为成功。
- `hal/lpc/`：只提供主机安全初始化；休眠、唤醒 GPIO 返回 `-QM_ENOSYS`，因为 macOS
  进程不能模拟 MCU 低功耗状态。
- `hal/modem/`：提供可编译的 modem/PPP 接口桩，返回 `-QM_ENOSYS`。
- `qm_db/`：文件 KV 存储，可用 `QM_MACOS_KV_DIR` 指定目录。
- `hal/ota/`：文件 OTA 暂存（`QM_MACOS_OTA_PATH`，默认 `/tmp/qumi-macos-ota.bin`），
  不修改主机启动目标，也不会重启电脑。
- `hal/soc/`：实现基于系统时间的 RTC；GPIO、UART、SPI、I2C、Flash 等 MCU 外设不在
  本主机适配范围内。

## BLE 限制

CoreBluetooth 不暴露本机和对端蓝牙 MAC。适配层使用 NSUserDefaults 持久化一个
本机专用、随机生成的 6 字节协议身份，供广播设备 ID 和 QMS 读特征使用；它不是
真实蓝牙 MAC，也不用于绕过 BLE 配网鉴权。原始 scan response、扫描 interval/window
保留 QUMI C 接口，但 CoreBluetooth 不允许直接控制。GAP 回调运行在主队列，应用必须
保持 run loop 运转。

## 依赖

- macOS 10.15 或更高版本
- CoreBluetooth.framework、Foundation.framework
- AudioToolbox.framework、AVFoundation.framework
- macOS 自带 CommonCrypto
- 仓库内 mbedTLS
- App 包含蓝牙用途说明并获得用户授权
- 使用语音的 App 包含麦克风用途说明并获得用户授权

通用 QUMI 组件不能依赖 CoreBluetooth 类型；Objective-C 代码只放在本目录，通过现有
QUMI C 回调通信。
