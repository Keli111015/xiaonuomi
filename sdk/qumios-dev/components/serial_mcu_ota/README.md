# Serial MCU OTA

主控侧 MCU OTA 组件，独立于旧 `serial_ota.c` / `serial_ota.h`，使用 `0x35` 到 `0x3B` 命令发送 MCU 固件。

## 文件

- `serial_mcu_ota.h`：主控侧公开 API。
- `serial_mcu_ota.c`：协议 payload 打包/解析、ACK 状态机和超时重发。
- `qm_kconfig`：组件开关和默认参数。
- `PROTOCOL.md`：独立 MCU OTA 串口协议说明。

MCU 接收端独立组件见 `components/mcu_ota`，不依赖 Qumi 组件接口。

CRC32 或 MD5 校验值由调用方提前计算，并通过 `serial_mcu_ota_config_t::check_value` 传入。组件只把该值作为 `OTA_BEGIN` 协议字段发送给 MCU，不在组件内部读取整包固件计算校验值。

`OTA_BEGIN` 中 CRC32 只发送 4 字节校验值，MD5 发送 16 字节校验值。`OTA_END` 不再携带校验值，payload 长度固定为 0。

协议帧格式、命令、负载字段、ACK 状态码和 MCU 异步通知见 `PROTOCOL.md`。

## 接入

主控侧工程编译本目录的 `serial_mcu_ota.c`，并依赖 `xjiang_serial` 的 Qumi 串口命令表发送 `0x35` 到 `0x39`、分发 `0x3A OTA_ACK` 和 `0x3B OTA_NOTIFY`。旧 `serial_ota.c` / `serial_ota.h` 不需要修改。

推荐流程：

1. 初始化 Qumi 串口组件。
2. 填充 `serial_mcu_ota_config_t`，至少提供 `total_size`、`read_chunk`、`check_type` 和 `check_value`。
3. 调用 `qm_serial_mcu_ota_start()` 发送 `OTA_ENTER`。
4. 收到 MCU `OTA_NOTIFY reason=READY` 后，组件自动发送 `OTA_BEGIN`。
5. 串口层收到 MCU `OTA_ACK` 或 `OTA_NOTIFY` 后，通过 `g_qm_serial_cmd_table` 调用 `serial_mcu_ota_parse()`。
6. 定时调用 `serial_mcu_ota_process()` 处理 ACK 超时重发。
7. 状态变成 `SERIAL_MCU_OTA_STATE_DONE` 后，认为传输完成。

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

    fill_firmware_crc32_or_md5(check_value);

    config.total_size = firmware_size;
    config.check_type = SERIAL_MCU_OTA_CHECK_CRC32;
    memcpy(config.check_value, check_value, SERIAL_MCU_OTA_CRC32_VALUE_LEN);
    config.read_chunk = read_firmware;

    qm_serial_mcu_ota_start(&config);
}

void ota_tick(void)
{
    serial_mcu_ota_process();
}
```

`serial_mcu_ota.*` 依赖 Qumi utility 接口和 `xjiang_serial` 组件接口。发送命令固定通过 `serial_generate_send()` 进入现有串口层，ACK/NOTIFY 固定通过 `g_qm_serial_cmd_table` 分发到 `serial_mcu_ota_parse()`。ACK 超时比较使用 `qm_utils_time_countdown_ms()` / `qm_utils_time_is_expired()`，大小端转换使用 `qm_types.h` 中的 `cpu_to_be16()` / `be16_to_cpu()` 等接口。新协议只占用 `0x35` 到 `0x3B`，旧 `0x30` 到 `0x34` OTA 流程保持不变。
