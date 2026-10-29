# Serial MCU OTA

主控侧 MCU OTA 组件，独立于旧 `serial_ota.c` / `serial_ota.h`，使用 `0x35` 到 `0x3B` 命令发送 MCU 固件。

## 文件

- `serial_mcu_ota.h`：主控侧公开 API。
- `serial_mcu_ota.c`：协议打包/解析、ACK 状态机、超时重发和 Qumi 串口发送。
- `qm_kconfig`：组件开关和默认参数。
- `PROTOCOL.md`：独立 MCU OTA 串口协议说明。

CRC32 或 MD5 校验值由调用方提前计算，并通过 `serial_mcu_ota_config_t::check_value` 传入。组件只把该值作为 `OTA_BEGIN` 协议字段发送给 MCU，不在组件内部读取整包固件计算校验值。

`OTA_BEGIN` 中 CRC32 只发送 4 字节校验值，MD5 发送 16 字节校验值。`OTA_END` 不再携带校验值，payload 长度固定为 0。

协议帧格式、命令、负载字段、ACK 状态码和 MCU 异步通知见 `PROTOCOL.md`。

## 接入

主控侧工程只需要编译本目录的 `serial_mcu_ota.c`，不需要 MCU receiver，也不需要 `xjiang_serial` 或旧 `serial_ota.c` / `serial_ota.h`。

推荐流程：

1. 初始化 Qumi UART，得到 `qm_uart_dev_t *`。
2. 填充 `serial_mcu_ota_config_t`，至少提供 `uart`、`total_size` 和 `read_chunk`。
3. 填入 `check_type` 和调用方提前算好的 `check_value`。
4. 调用 `serial_mcu_ota_start()` 发送 `OTA_ENTER`。
5. 收到 MCU `OTA_NOTIFY reason=READY` 后，组件自动发送 `OTA_BEGIN`。
6. 串口收到 MCU 的完整 `OTA_ACK` 或 `OTA_NOTIFY` 原始帧后，调用 `serial_mcu_ota_on_frame()`。
7. 定时调用 `serial_mcu_ota_process()` 处理 ACK 超时重发。
8. 状态变成 `SERIAL_MCU_OTA_STATE_DONE` 后，认为传输完成。

默认建议：

- `chunk_size`: 512
- `max_chunk_size`: 1024
- `check_type`: `SERIAL_MCU_OTA_CHECK_CRC32`
- `timeout_ms`: 1000
- `retry_max`: 3

`chunk_size`、`check_type`、`timeout_ms` 和 `retry_max` 可以填 0 使用默认值。

最小接入示例：

```c
static int32_t read_firmware(uint32_t offset, uint8_t *data, uint16_t len, void *user)
{
    /* 从固件文件或缓存读取 len 字节到 data。 */
    return 0;
}

void start_mcu_ota(uint32_t firmware_size)
{
    serial_mcu_ota_config_t config = {0};
    uint8_t check_value[SERIAL_MCU_OTA_CHECK_VALUE_LEN] = {0};
    qm_uart_dev_t *uart = get_initialized_uart();

    fill_firmware_crc32_or_md5(check_value);

    config.total_size = firmware_size;
    config.check_type = SERIAL_MCU_OTA_CHECK_CRC32;
    memcpy(config.check_value, check_value, SERIAL_MCU_OTA_CRC32_VALUE_LEN);
    config.uart = uart;
    config.read_chunk = read_firmware;

    serial_mcu_ota_start(&config);
}

void on_uart_frame(const uint8_t *frame, uint16_t frame_len)
{
    serial_mcu_ota_on_frame(frame, frame_len);
}

void ota_tick(void)
{
    serial_mcu_ota_process();
}
```

`serial_mcu_ota.*` 只依赖 Qumi HAL/utility 接口。发送完整帧时直接调用 `qm_uart_write(uart, frame, len, 0)`，ACK 超时比较使用 `qm_utils_time_countdown_ms()` / `qm_utils_time_is_expired()`，大小端转换使用 `qm_types.h` 中的 `cpu_to_be16()` / `be16_to_cpu()` 等接口。组件不会进入 `xjiang_serial` 的命令表，也不会占用旧 `0x30` 到 `0x34` OTA 流程。
