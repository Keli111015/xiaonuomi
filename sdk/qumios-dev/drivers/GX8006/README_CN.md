# GX8006 UART 协议驱动

该目录提供与 MCU 无关的 GX8006 UART 协议：`include/nc_audio.h` 是公开协议接口，
`src/nc_audio.c` 负责命令组包和增量校验模块帧，`qm_kconfig` 提供协议与 ESP32-S3
绑定所需配置。

它不实现 `qm_audio`，也不选择板级管脚。ESP32-S3 设备绑定位于
`platform/mcu/esp32s3/qm_audio/qm_audio_device_gx8006.c`；供电、GPIO 路由和
功放策略仍属于板级代码。

## 原生音频 Profile

当前绑定只公开一个固定的全双工 Profile：Opus、16 kHz、单声道、S16 解码几何、40 ms
包长、80 字节固定 CBR payload。PCM 不会直接送入模块；只有在需要软件解码或
Opus 编码时，`qm_audio` 才使用这些 PCM 几何参数。

采集方向交付完整 Opus 包；播放方向只接受完整包，V1 协议每个包发送一条 SPK DATA。
包不完整或 profile 不匹配时直接失败，不猜测格式，也不任意拆包。

ESP32-S3 绑定采用 PUSH 采集，在 `protocol_event()` 中直接
调用 `qm_audio_capture_feed(&input, QM_EOK)` 提交音频；UART 循环通过
`qm_audio_capture_feed(NULL, error)` 上报不可恢复的源故障，feed 入口由组件实现。
适配层没有采集模式分支、read 接口或采集环形缓存。
stop 成功前会等待当前采集回调整体结束以及故障提交返回；正常停止不作为源故障上报。

正常播放完成是 drain 屏障：输入结束后，绑定发送 SPK STOP，并等待模块 `0x12/0x03`
完成状态；`0x12/0x02` 只表示暂时播空。STOP 会 drain 已接收音频，不承诺立即静音。
播放位置、pause/resume 仍不支持。MIC_OFF 或播放停止失败时，设备路由保持可重试状态，
不能当作已经安全关闭。

## 配置与使用

启用 `qm audio` 后，在 `qm audio device` 中选择 `GX8006 UART Opus device (ESP32-S3)`。
该选择会 select `QM_GX8006_SUPPORT`，并显示 UART、GPIO、增益和正常停止超时配置。
通用 `qm_audio` 采集模式配置选择 PUSH，与该绑定保持一致。选择 GX8006 时，
OpenCPU JoyInside 示例会自动使用这个原生 Profile。

也可以脱离 `qm_audio` 单独使用协议驱动；此时应用负责 UART 初始化、调用
`nc_audio_recv_push()` 的接收循环以及完整生命周期。不得让该独立路径与 `qm_audio`
绑定同时占用同一个物理模块。

UART 波特率、采集聚包方式、目标固件行为和实际声学结束时刻仍须在目标板确认。
