# MCU OTA 协议

本文档只描述 MCU 接收端需要关心的串口 OTA 协议。组件 `mcu_ota.c` 已经实现帧解析、ACK/NOTIFY 封包、分片顺序检查和固件整包校验。

## 帧格式

```text
HEAD(2) | VER(1) | CMD(1) | LEN(2) | PAYLOAD(n) | CHKSUM(1)
```

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| HEAD | 2 | 固定 `0xA5 0xA5` |
| VER | 1 | 固定 `0x03` |
| CMD | 1 | 命令字 |
| LEN | 2 | payload 长度，大端序 |
| PAYLOAD | n | 命令负载 |
| CHKSUM | 1 | 从 HEAD 到 PAYLOAD 所有字节累加后的低 8 位 |

所有多字节整数字段均使用大端序。

## 命令

| 命令 | 名称 | 方向 | 说明 |
| --- | --- | --- | --- |
| `0x35` | `OTA_ENTER` | 模组端 -> MCU | 请求 MCU 进入 OTA 接收状态 |
| `0x36` | `OTA_BEGIN` | 模组端 -> MCU | 发起 OTA |
| `0x37` | `OTA_DATA` | 模组端 -> MCU | 发送固件分片 |
| `0x38` | `OTA_END` | 模组端 -> MCU | 结束 OTA |
| `0x39` | `OTA_ABORT` | 模组端 -> MCU | 取消 OTA |
| `0x3A` | `OTA_ACK` | MCU -> 模组端 | MCU 命令处理结果 |
| `0x3B` | `OTA_NOTIFY` | MCU -> 模组端 | MCU 主动上报状态或异常 |

## 校验类型

| 值 | 类型 | check_value |
| ---: | --- | --- |
| `1` | CRC32 | 4 字节，大端序 |
| `2` | MD5 | 16 字节 |

`mcu_ota.c` 会在接收 `OTA_DATA` 时自动累计计算，并在 `OTA_END` 时与 `OTA_BEGIN` 中的 `check_value` 比较。业务回调不需要自己计算 CRC32 或 MD5。

组件编译时可按需启用校验算法：

- 默认 `MCU_OTA_USE_CRC32=1`、`MCU_OTA_USE_MD5=0`，只支持 `check_type=1`。
- 使用 MD5 时定义 `MCU_OTA_USE_CRC32=0`、`MCU_OTA_USE_MD5=1`，只支持 `check_type=2`。
- 两个宏都设为 1 时，同时支持 CRC32 和 MD5。
- 收到未启用的 `check_type` 时，组件回复 `UNSUPPORTED`。
- `mcu_ota_info_t.check_value` 长度会随宏变化：CRC32-only 为 4 字节，启用 MD5 时为 16 字节。所有包含 `mcu_ota.h` 的源文件必须使用同一组宏。

## OTA_ENTER

payload:

```text
flags uint8
```

MCU App 收到后，如果允许升级，返回 `OTA_ACK OK`，然后按产品策略重启或切换到 OTA 接收模式。bootloader 或 OTA 模式准备好后，可调用 `mcu_ota_notify_ready()` 发送 `OTA_NOTIFY READY`。

## OTA_BEGIN

payload:

```text
total_size  uint32
chunk_size  uint16
check_type  uint8
flags       uint8
check_value uint8[4 或 16]
```

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| total_size | 4 | 固件总长度，不能为 0 |
| chunk_size | 2 | 每包最大数据长度，不能超过 `MCU_OTA_MAX_CHUNK_SIZE` |
| check_type | 1 | 必须是 `MCU_OTA_USE_CRC32` 或 `MCU_OTA_USE_MD5` 已启用的算法 |
| flags | 1 | 扩展标志，当前通常填 0 |
| check_value | 4 或 16 | 完整固件校验值 |

参数合法后，组件调用 `begin_cb`。MCU 开发者通常只需要在 `begin_cb` 中擦除或准备升级区。

## OTA_DATA

payload:

```text
offset uint32
seq    uint16
len    uint16
data   uint8[len]
```

组件只接受当前期望的 `seq` 和 `offset`。顺序正确时调用 `write_cb(offset, data, len)`；写入成功后组件更新接收进度和内部校验值。

## OTA_END

payload 长度固定为 0。

组件处理顺序：

1. 确认已接收长度等于 `total_size`。
2. 校验 CRC32 或 MD5。
3. 校验通过后调用 `finish_cb`。
4. `finish_cb` 返回 0 时回复 `OTA_ACK OK`。

MCU 开发者通常只需要在 `finish_cb` 中确认固件、设置启动分区或记录升级成功标志。

## OTA_ABORT

payload 长度固定为 0。组件收到后调用 `abort_cb`，清理内部进度，并回复 `OTA_ACK OK`。临时固件是否擦除由产品策略决定。

## OTA_ACK

payload:

```text
acked_cmd   uint8
code        uint8
next_seq    uint16
next_offset uint32
```

| code | 名称 | 说明 |
| ---: | --- | --- |
| `0` | `OK` | 处理成功 |
| `1` | `ERROR` | 通用错误 |
| `2` | `BUSY` | MCU 正忙 |
| `3` | `PARAM_ERROR` | 参数错误 |
| `4` | `SEQUENCE_ERROR` | `seq` 或 `offset` 不符合期望 |
| `5` | `WRITE_ERROR` | 写入失败 |
| `6` | `CHECK_ERROR` | 最终校验失败 |
| `7` | `UNSUPPORTED` | 不支持的校验类型或能力 |

`next_seq` 和 `next_offset` 始终表示 MCU 期望接收的下一包位置。

## OTA_NOTIFY

payload:

```text
reason      uint8
next_seq    uint16
next_offset uint32
error_code  uint32
```

| reason | 名称 | 说明 |
| ---: | --- | --- |
| `1` | `READY` | MCU 已准备好接收 `OTA_BEGIN` |
| `2` | `ERROR` | MCU 主动上报异常，原因由 `error_code` 表示 |

## 最小流程

```text
module -> MCU App: OTA_ENTER
MCU App -> module: OTA_ACK OK
MCU App: reboot or switch to OTA receive mode
MCU -> module: OTA_NOTIFY READY

module -> MCU: OTA_BEGIN(total_size, chunk_size, check_type, check_value)
MCU -> module: OTA_ACK OK

module -> MCU: OTA_DATA seq=0 offset=0
MCU -> module: OTA_ACK OK

module -> MCU: OTA_DATA seq=1 offset=chunk_len
MCU -> module: OTA_ACK OK

module -> MCU: OTA_END
MCU -> module: OTA_ACK OK
```
