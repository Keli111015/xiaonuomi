# MCU OTA Example

`mcu_ota_example.c` 演示 MCU 侧最小接入方式：

1. 调用 `mcu_ota_example_init()` 注册串口发送、擦除、写入、完成确认等回调。
2. 串口每收到 1 字节，调用 `mcu_ota_example_uart_rx_byte(byte)`。
3. MCU bootloader 已准备好接收升级时，调用 `mcu_ota_example_notify_ready()`。

示例里的串口发送、flash 擦写和固件确认位置都用 `TODO` 标出，移植时替换成目标 MCU 平台接口即可。CRC32/MD5 已由组件内部处理。
