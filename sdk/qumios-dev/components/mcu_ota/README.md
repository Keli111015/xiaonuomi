# MCU OTA Receiver

`mcu_ota.c` / `mcu_ota.h` 是 MCU 侧接收端的最小实现，按 `PROTOCOL.md` 解析完整串口协议帧并处理 `0x35` 到 `0x39` 命令。

这个实现不依赖 Qumi 组件接口，不包含 `serial_protocol.h`、`qm_types.h`、`qm_utils_timer.h` 等头文件，只使用标准 C 的 `stdint.h` 和 `string.h`。

`example/mcu_ota_example.c` 提供了最小接入示例。

内部校验算法：

- `mcu_ota_crc32.c` / `mcu_ota_crc32.h`：CRC32 计算。
- `mcu_ota_md5.c` / `mcu_ota_md5.h`：MD5 计算。
- 默认只启用 CRC32，只需要编译 `mcu_ota.c` 和 `mcu_ota_crc32.c`。
- 如需 MD5，在编译参数中加入 `-DMCU_OTA_USE_CRC32=0 -DMCU_OTA_USE_MD5=1`，并编译 `mcu_ota.c` 和 `mcu_ota_md5.c`。
- 如果两个宏都设为 1，则 CRC32 和 MD5 都支持。
- `OTA_BEGIN` 里的 `check_type` 必须是已启用的算法，否则组件会回复 `UNSUPPORTED`。
- `mcu_ota_info_t.check_value` 的数组长度会随宏变化：CRC32-only 为 4 字节，启用 MD5 时为 16 字节。所有包含 `mcu_ota.h` 的源文件必须使用同一组宏。

组件会自动计算并校验固件整包 CRC32 或 MD5，业务代码不需要自己维护校验上下文。

## 职责划分

- 组件负责：串口协议帧解析、帧校验和、大小端转换、OTA 状态、固件整包校验、`OTA_ACK` 和 `OTA_NOTIFY` 封包发送。
- MCU 平台负责：串口实际发送、进入 OTA 模式、擦除/准备升级区域、写 flash、确认或切换固件。

## 接入方式

选择校验算法：

```c
/* 默认 CRC32，无需额外配置。 */

/* 使用 MD5 时，在编译参数中加入：
 * -DMCU_OTA_USE_CRC32=0 -DMCU_OTA_USE_MD5=1
 */

/* 同时支持 CRC32 和 MD5 时，在编译参数中加入：
 * -DMCU_OTA_USE_CRC32=1 -DMCU_OTA_USE_MD5=1
 */

/* 注意：所有包含 mcu_ota.h 的 .c 文件都要使用同一组宏。 */
```

```c
#include "mcu_ota.h"

static int ota_uart_send(const uint8_t *data, uint16_t len, void *user_data)
{
    (void)user_data;
    /* uart_write(data, len); */
    return 0;
}

static int ota_begin(const mcu_ota_info_t *info, void *user_data)
{
    (void)user_data;
    /* erase_upgrade_area(info->image_size); */
    return 0;
}

static int ota_write(uint32_t offset, const uint8_t *data, uint16_t len, void *user_data)
{
    (void)user_data;
    /* flash_write(offset, data, len); */
    return 0;
}

static int ota_finish(const mcu_ota_info_t *info, void *user_data)
{
    (void)info;
    (void)user_data;
    /*
     * 到这里时 CRC32/MD5 已经由组件校验通过。
     * 在这里确认固件或设置启动分区。
     */
    return 0;
}

void ota_init(void)
{
    mcu_ota_config_t cfg = {0};

    cfg.send_cb = ota_uart_send;
    cfg.begin_cb = ota_begin;
    cfg.write_cb = ota_write;
    cfg.finish_cb = ota_finish;

    mcu_ota_init(&cfg);
}

void ota_uart_rx_byte(uint8_t byte)
{
    mcu_ota_feed(byte);
}
```

如果 MCU App 收到 `OTA_ENTER` 后需要重启到 bootloader，`enter` 回调里执行本地准备动作即可。bootloader 启动并初始化串口后调用：

```c
mcu_ota_notify_ready();
```

## 回调返回值

回调返回 `0` 表示成功。返回 `MCU_OTA_ACK_*` 中的正数值时，组件会原样作为 ACK 状态码返回给模组端；其他非 0 值会映射为当前阶段的默认错误。

常用返回值：

- `MCU_OTA_ACK_BUSY`
- `MCU_OTA_ACK_PARAM_ERROR`
- `MCU_OTA_ACK_WRITE_ERROR`
- `MCU_OTA_ACK_CHECK_ERROR`
- `MCU_OTA_ACK_UNSUPPORTED`
