# MCU OTA 协议

本文档描述独立 MCU OTA 通道的串口协议。该协议不替换旧 `serial_ota.c` / `serial_ota.h`，旧 `0x30` 到 `0x34` OTA 命令保持不变；本协议使用 `0x35` 到 `0x3B` 命令。

## 角色

- 模组端：发送端，负责读取固件文件、按分片发送、处理 ACK、超时重发。
- MCU 端：接收端，负责准备升级区域、按 offset 写入分片、结束时校验并切换固件。
- 串口层：负责收发完整帧、校验帧头和校验和，并通过命令表分发 `CMD` 和 `PAYLOAD`。

校验值由调用方提前计算，并作为 `OTA_BEGIN` 的协议字段传输给 MCU。主控侧 OTA 组件不在内部计算 CRC32 或 MD5。

## 默认参数

```text
chunk_size: 512
max_chunk_size: 1024
check_type: CRC32
timeout: 由主控侧调用 serial_mcu_ota_process() 周期处理
```

## 帧格式

所有数据包沿用现有 Qumi 串口帧结构：

```text
HEAD(2) | VER(1) | CMD(1) | LEN(2) | PAYLOAD(n) | CHKSUM(1)
```

字段说明：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| HEAD | 2 | 固定 `0xA5 0xA5` |
| VER | 1 | 固定 `0x03` |
| CMD | 1 | 命令字 |
| LEN | 2 | payload 长度，大端序 |
| PAYLOAD | n | 命令负载 |
| CHKSUM | 1 | 从 HEAD 到 PAYLOAD 所有字节累加后的低 8 位 |

所有多字节整数字段均使用大端序。

在 Qumi 主控侧接入时，完整帧解析由 `xjiang_serial` 完成，`g_qm_serial_cmd_table` 将 `0x3A OTA_ACK` 和 `0x3B OTA_NOTIFY` 的 payload 转给 `serial_mcu_ota_parse()`。MCU OTA 组件发送 `0x35` 到 `0x39` 时只生成命令 payload，最终帧封包仍走现有串口层。

## 命令

| 命令 | 名称 | 方向 | 说明 |
| --- | --- | --- | --- |
| `0x35` | `OTA_ENTER` | 模组端 -> MCU | 请求 MCU 进入可接收 OTA 的状态 |
| `0x36` | `OTA_BEGIN` | 模组端 -> MCU | 发起 OTA |
| `0x37` | `OTA_DATA` | 模组端 -> MCU | 发送固件分片 |
| `0x38` | `OTA_END` | 模组端 -> MCU | 结束 OTA 并触发最终校验 |
| `0x39` | `OTA_ABORT` | 模组端 -> MCU | 主动取消 OTA |
| `0x3A` | `OTA_ACK` | MCU -> 模组端 | MCU 命令处理结果 |
| `0x3B` | `OTA_NOTIFY` | MCU -> 模组端 | MCU 主动上报 OTA 异常或状态变化 |

## 校验类型

| 值 | 类型 | 说明 |
| ---: | --- | --- |
| `1` | CRC32 | `check_value[0..3]` 保存 CRC32，大端序 |
| `2` | MD5 | `check_value[0..15]` 保存完整 MD5 |

`check_value` 字段长度由 `check_type` 决定。CRC32 时为 4 字节，MD5 时为 16 字节。
因此 `OTA_BEGIN` 的 payload 长度为 `8 + check_value_len`。

## OTA_BEGIN

`OTA_BEGIN` 用于发起一次新的 OTA 传输。

负载格式：

```text
total_size  uint32
chunk_size  uint16
check_type  uint8
flags       uint8
check_value uint8[check_value_len]
```

字段说明：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| total_size | 4 | 固件总长度，单位字节，不能为 0 |
| chunk_size | 2 | 每个 `OTA_DATA` 的最大数据长度 |
| check_type | 1 | 校验类型，见“校验类型” |
| flags | 1 | 扩展标志，当前填 0 |
| check_value | 4 或 16 | 完整固件校验值，长度由 `check_type` 决定 |

接收端处理：

- 参数合法时准备升级区域，并返回 `OTA_ACK OK`。
- `total_size` 为 0、`chunk_size` 超过接收端能力、`check_type` 不支持时返回 `PARAM_ERROR` 或 `UNSUPPORTED`。
- 如果接收端正在处理其他 OTA，可按产品策略返回 `BUSY`。

总包数不在协议中单独传输，可由本地计算：

```text
packet_count = (total_size + chunk_size - 1) / chunk_size
```

## OTA_ENTER

`OTA_ENTER` 用于主控请求 MCU 从应用固件切换或重启到可接收 OTA 的状态。

负载格式：

```text
flags uint8
```

字段说明：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| flags | 1 | 扩展标志，当前填 0 |

接收端处理：

- MCU App 收到后，如允许升级，返回 `OTA_ACK OK`，然后重启或切换到 OTA 接收模式。
- MCU 进入 OTA 接收模式后，发送 `OTA_NOTIFY reason=READY`。
- 如果当前条件不允许进入 OTA 模式，返回 `BUSY`、`PARAM_ERROR` 或 `UNSUPPORTED`。

`OTA_ENTER` 不携带 `total_size`、`check_type` 或 `check_value`。这些字段应在 MCU 真正进入 OTA 接收模式后，通过 `OTA_BEGIN` 发送。

## OTA_DATA

`OTA_DATA` 用于传输固件分片。

负载格式：

```text
offset   uint32
seq      uint16
len      uint16
data     uint8[len]
```

字段说明：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| offset | 4 | 当前分片在完整固件中的起始偏移，第一包为 0 |
| seq | 2 | 分片序号，第一包为 0 |
| len | 2 | 本包实际数据长度，不能为 0，不能大于 `chunk_size` |
| data | len | 固件分片数据 |

接收端处理：

- 只接受当前期望的 `seq` 和 `offset`。
- `seq` 或 `offset` 不匹配时返回 `SEQUENCE_ERROR`，并带回期望的 `next_seq` 和 `next_offset`。
- 写入失败时返回 `WRITE_ERROR`，并中止本次 OTA。
- 写入成功后返回 `OTA_ACK OK`。

## OTA_END

`OTA_END` 用于结束本次 OTA 传输，并触发 MCU 使用 `OTA_BEGIN` 中记录的校验值做最终校验。

负载格式：

```text
无 payload
```

`OTA_END` 的 payload 长度固定为 0。

接收端处理：

- 确认已接收长度等于 `total_size`。
- 使用接收数据累计计算出的 CRC32 或 MD5 与 `OTA_BEGIN` 中记录的 `check_value` 比较。
- 校验通过后完成固件确认或分区切换，并返回 `OTA_ACK OK`。
- 校验失败时返回 `CHECK_ERROR`，并中止本次 OTA。

## OTA_ABORT

`OTA_ABORT` 用于主动取消当前 OTA。负载长度为 0。

接收端收到后清理当前 OTA 状态，并返回 `OTA_ACK OK`。已写入的临时固件区域是否擦除由产品策略决定。

## OTA_ACK

`OTA_ACK` 用于 MCU 返回命令处理结果。

负载格式：

```text
acked_cmd   uint8
code        uint8
next_seq    uint16
next_offset uint32
```

字段说明：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| acked_cmd | 1 | 本次 ACK 对应的命令 |
| code | 1 | ACK 状态码 |
| next_seq | 2 | MCU 期望的下一个分片序号 |
| next_offset | 4 | MCU 期望的下一个写入偏移 |

ACK 状态码：

| 值 | 名称 | 说明 |
| ---: | --- | --- |
| `0` | `OK` | 处理成功 |
| `1` | `ERROR` | 通用错误 |
| `2` | `BUSY` | 接收端忙 |
| `3` | `PARAM_ERROR` | 参数错误 |
| `4` | `SEQUENCE_ERROR` | `seq` 或 `offset` 不符合期望 |
| `5` | `WRITE_ERROR` | 写入失败 |
| `6` | `CHECK_ERROR` | 最终校验失败 |
| `7` | `UNSUPPORTED` | 不支持的参数或能力 |

发送端处理：

- `acked_cmd` 必须与最近一次发送的命令一致。
- `code` 为 `OK` 时，按 `next_seq` 和 `next_offset` 继续发送下一步。
- `code` 为 `SEQUENCE_ERROR` 时，按 `next_seq` 和 `next_offset` 回退并重发。
- `code` 为其他错误时，终止本次 OTA 并上报失败原因。

## OTA_NOTIFY

`OTA_NOTIFY` 是 MCU 主动发送给模组端的异步通知，用于上报 OTA 接收状态或异常。该命令不替代 `OTA_ACK`，也不要求对应上一条模组端命令。

负载格式：

```text
reason      uint8
next_seq    uint16
next_offset uint32
error_code  uint32
```

字段说明：

| 字段 | 长度 | 说明 |
| --- | ---: | --- |
| reason | 1 | 通知原因 |
| next_seq | 2 | MCU 当前期望的下一个分片序号 |
| next_offset | 4 | MCU 当前期望的下一个写入偏移 |
| error_code | 4 | MCU 自定义错误码，成功或无错误时可填 0 |

通知原因：

| 值 | 名称 | 说明 |
| ---: | --- | --- |
| `0` | `NONE` | 无特定原因 |
| `1` | `READY` | MCU 已进入可接收 OTA 的状态，主控可发送 `OTA_BEGIN` |
| `2` | `ERROR` | MCU 上报 OTA 异常，具体原因见 `error_code` |

主控侧处理建议：

- 收到 `READY`：主控可发送 `OTA_BEGIN`，并在此时传输 `total_size`、`chunk_size`、`check_type` 和 `check_value`。
- 收到 `ERROR`：停止当前 OTA 并上报 `error_code`，由业务层决定是否重新开始。
- 当前没有 OTA 任务时，仍可把 `OTA_NOTIFY` 作为 MCU 状态事件上报业务层。

## 最小流程

```text
module -> MCU App: OTA_ENTER
MCU App -> module: ACK OK
MCU App: reboot or switch to OTA receive mode
MCU -> module: OTA_NOTIFY reason=READY

module -> MCU: OTA_BEGIN
MCU -> module: ACK OK, next_seq=0, next_offset=0

module -> MCU: OTA_DATA seq=0 offset=0
MCU -> module: ACK OK, next_seq=1, next_offset=chunk_len

module -> MCU: OTA_DATA seq=1 offset=chunk_len
MCU -> module: ACK OK, next_seq=2, next_offset=...

module -> MCU: OTA_END
MCU -> module: ACK OK
```

## 进入 OTA 模式

如果 MCU 需要先从应用固件重启到 OTA 接收模式，不建议在重启前发送 `OTA_BEGIN`。推荐流程如下：

```text
module -> MCU App: OTA_ENTER
MCU App -> module: OTA_ACK OK
MCU App: reboot or switch to OTA receive mode

MCU -> module: OTA_NOTIFY reason=READY
module -> MCU: OTA_BEGIN(total_size, chunk_size, check_type, check_value)
MCU -> module: ACK OK
```

`OTA_BEGIN` 应发送给真正负责接收固件数据的 MCU OTA 模式。这样 `check_value` 不需要跨复位保存，避免 MCU App 重启后丢失校验值或 OTA 状态。

## 重传策略

- 发送端等待 ACK 超时后重发上一包。
- 接收端发现 `seq` 或 `offset` 不符合预期时返回 `SEQUENCE_ERROR`，并带上当前期望的 `next_seq` 和 `next_offset`。
- 发送端收到 `SEQUENCE_ERROR` 后按 ACK 中的位置回退并重发。
- MCU 发生异常复位、状态丢失或存储异常后，应通过 `OTA_NOTIFY reason=ERROR` 主动上报。
- `OTA_END` 只有在所有分片写入完成且最终 CRC32 或 MD5 校验通过后才返回 `OK`。
