# ComTrace 端侧协议 V1

## 1. 目的和范围

本协议用于端侧设备在局域网中发现 ComTrace，并通过 CELP/1 上报日志或通过 CEUP/1 完成固件更新。

协议名称为 `ComTrace Edge Log Protocol`，简称 `CELP/1`。

传输层支持 TCP 和 UDP，两者使用完全相同的应用层帧和 CBOR 消息。TCP 是正式部署时的首选传输方式；UDP 用于可容忍少量丢包的低延迟日志预览。

CELP/1 只用于日志接入，不用于远程控制、升级或配置修改。本文末尾追加的 CEUP/1 是与 CELP/1 并列的 OTA 固件升级协议，使用独立的 TCP 或串口（含 USB CDC）连接和消息类型。

## 2. 规范约定

- 本文中的“必须”表示实现必须满足的要求。
- 所有长度、数值均使用无符号整数；帧长度使用大端序。
- CBOR 编码遵循 [RFC 8949](https://www.rfc-editor.org/rfc/rfc8949.html)。
- 端侧设备主动发现并主动连接 ComTrace；ComTrace 不主动扫描端侧设备。
- 端侧的稳定身份是 `sn`，来源 IP 和端口仅作为网络元数据。

## 3. 总体交互

```text
端侧                         ComTrace
 │                                │
 │── mDNS 查询服务 ───────────────>│
 │<─ 服务地址和端口 ───────────────│
 │                                │
 │── TCP 连接或 UDP REGISTER ────>│
 │<─ REGISTER_ACK ────────────────│
 │                                │
 │── LOG ─────────────────────────>│
 │── HEARTBEAT ───────────────────>│
 │                                │
 │── GOODBYE（可选）──────────────>│
```

## 4. 服务发现

### 4.1 mDNS 服务

ComTrace 在启用端侧日志监听时发布以下服务：

| 传输方式 | mDNS 服务类型 |
|---|---|
| TCP | `_comtrace-log._tcp.local.` |
| UDP | `_comtrace-log._udp.local.` |
| OTA TCP | `_comtrace-update._tcp.local.` |

日志服务实例名建议为：

```text
ComTrace - <主机名称>
```

TCP/UDP 端口由 mDNS SRV 记录提供，不应在 TXT 记录中重复定义。

OTA 服务使用独立的 TCP 端口和服务实例。端侧需要升级时，查询
`_comtrace-update._tcp.local.`，不能把固件数据发送到日志服务端口。

### 4.2 TXT 记录

ComTrace 必须发布：

| 键 | 示例 | 说明 |
|---|---|---|
| `proto` | `celp/1` | 协议名称和版本 |
| `encoding` | `cbor` | 唯一支持的消息编码 |
| `frame` | `u16be+cbor` | 帧格式 |
| `max_frame` | `1200` | 最大 CBOR payload 长度，不含 2 字节长度头 |
| `instance_id` | `ct-7f3a91c2e8b44d1d5a0c3b6e9f102347` | ComTrace 实例标识；首次启动生成并持久保存，格式为 `ct-` 加 32 位小写十六进制字符 |

ComTrace 可以额外发布：

| 键 | 示例 | 说明 |
|---|---|---|
| `name` | `实验室主控机` | 用户可读名称 |

TXT 记录中的 `max_frame` 用于端侧在连接前分配缓冲区；`register_ack.max_frame` 可在该上限内按当前监听实例收缩。两者承担发现期和运行期的不同职责。

### 4.3 发现失败的回退

端侧应支持静态配置的主机地址和端口。mDNS 查询失败、跨网段或网络禁用多播时，端侧使用静态配置建立 TCP 连接或发送 UDP 数据报。

## 5. 公共帧格式

TCP 和 UDP 都传输相同的完整帧：

```text
+-------------------+---------------------------+
| 2 B CBOR 长度     | N B CBOR 消息              |
+-------------------+---------------------------+
```

`CBOR 长度` 为无符号 16 位大端整数，只计算后面的 CBOR 数据，不包含自身。

### 5.1 长度限制

```text
1 <= CBOR 长度 <= 1200
```

`1200` 是 V1 的跨传输上限，不是可协商的扩大值。该值为 UDP 预留了网络头部空间，可避免常见链路上的 IP 分片，也让 TCP 和 UDP 共用同一套缓冲区和解析器。

超过 1200 字节的日志必须拆分为多条独立 `log` 消息；不得使用 IP 分片，也不得在一条日志消息中嵌套其他完整协议帧。

### 5.2 运行时默认值

下表是接收端与端侧均可调整的实现默认值，不属于改变帧格式或消息语义的协议版本兼容项。`register_ack` 中下发的 `max_frame` 和 `heartbeat_interval` 优先；未下发时端侧采用表中默认值。

| 参数 | 默认值 | 可调整范围 | 说明 |
|---|---:|---:|---|
| `max_frame` | `1200` B | `1..1200` B | 单条 CBOR payload 最大长度，不含 2 字节长度头，不能超过协议硬上限 |
| `register_timeout` | `5` s | `1..30` s | TCP 连接等待首条 `register` 的时间 |
| `udp_register_interval` | `60` s | `10..300` s | UDP 端侧重复注册间隔 |
| `heartbeat_interval` | `30` s | `5..300` s | 在线端侧的心跳间隔 |
| `offline_after_intervals` | `3` | `2..10` | 连续未收到消息后判离线的心跳周期数 |
| `connect_timeout` | `3` s | `1..30` s | TCP 建连超时 |
| `reconnect_backoff` | `1..30` s | `1..300` s | TCP 断线重连的指数退避边界 |
| `send_queue` | `256` 条或 `256` KiB | 由端侧资源决定 | TCP 阻塞期间的本地发送队列上限 |

### 5.3 TCP 分帧

TCP 是连续字节流。接收端必须：

1. 累计至少 2 个字节；
2. 读取 `CBOR 长度`；
3. 校验长度范围；
4. 累计 `2 + CBOR 长度` 个字节；
5. 取出一帧并继续解析后续字节。

长度为 0 或大于 1200 时，ComTrace 必须关闭该 TCP 连接。

### 5.4 UDP 分帧

一个 UDP 数据报必须且只能包含一条完整公共帧。

接收端必须校验：

```text
datagram_length == 2 + CBOR 长度
```

不匹配、长度非法或 CBOR 解码失败的数据报直接丢弃，不影响其他端侧。

## 6. CBOR 编码规则

每条消息是一个 CBOR Map。V1 使用 UTF-8 文本键，便于跨语言实现和抓包诊断。

实现必须支持：

- CBOR text string；
- CBOR byte string；
- 无符号整数；
- 布尔值；
- Array；
- Map。

除 `data` 外，不应使用大尺寸二进制字段。`data` 必须使用 CBOR byte string，禁止用 HEX 或 Base64 字符串承载原始串口字节。

未知字段必须忽略；未知 `type` 必须返回 `error`（TCP）或丢弃并记录诊断（UDP）。

## 7. 设备消息公共字段

`register`、`register_ack`、`log`、`heartbeat`、`heartbeat_ack` 和 `goodbye` 使用以下公共字段。`error` 可在注册前发送，因此仅使用第 8.6 节定义的字段。公共身份字段在每条设备消息中重复出现，以保持 TCP 与 UDP 使用同一帧格式，并支持 UDP 数据报独立解析。

| 键 | 类型 | 必须 | 说明 |
|---|---|---:|---|
| `v` | uint | 是 | 固定为 `1` |
| `type` | text | 是 | 消息类型 |
| `sn` | text | 是 | 设备唯一 SN，1 到 64 个 ASCII 可打印字符 |
| `sid` | uint32 | 是 | 端侧每次启动生成的非零随机会话 ID |

`sn` 是 ComTrace 创建和恢复设备通道的主键。`sid` 变化表示端侧重启或重新初始化，ComTrace 必须为该 SN 重置丢包序号状态。日志时间由 ComTrace 在接收时记录。

## 8. 消息类型

### 8.1 `register`

端侧完成发现或 TCP 连接后应立即发送注册消息。接收端可在 `register_timeout` 内未收到注册时关闭 TCP 连接。

```text
{
  "v": 1,
  "type": "register",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "device_name": "一号机柜"
}
```

附加字段说明：

| 键 | 类型 | 必须 | 说明 |
|---|---|---:|---|
| `device_name` | text | 否 | 用户可读名称，最长 64 字符 |

UDP 中，端侧启动时必须发送一次 `register`，随后按 `udp_register_interval` 重复发送，以便 ComTrace 重启后恢复设备信息。即使 `register` 丢失，合法 `log` 消息也可触发设备的临时创建。

### 8.2 `register_ack`

ComTrace 接受注册后必须回复：

```text
{
  "v": 1,
  "type": "register_ack",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "max_frame": 1200,
  "heartbeat_interval": 30
}
```

收到 `register_ack` 即表示注册成功。拒绝注册时，ComTrace 返回 `error`，不发送 `register_ack`。

端侧必须采用回复中的 `max_frame` 和 `heartbeat_interval`，但不得超过第 5.2 节定义的上限。接收端未回复时，端侧使用第 5.2 节的默认值。

### 8.3 `log`

端侧上报原始串口数据或文本事件。

原始字节日志示例：

```text
{
  "v": 1,
  "type": "log",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "sequence": 1024,
  "direction": "rx",
  "level": "info",
  "data": h'AA55010300'
}
```

文本事件示例：

```text
{
  "v": 1,
  "type": "log",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "sequence": 1025,
  "direction": "event",
  "level": "warn",
  "text": "temperature=35.6"
}
```

`log` 附加字段：

| 键 | 类型 | 必须 | 说明 |
|---|---|---:|---|
| `sequence` | uint32 | 是 | 同一 `sn + sid` 内从 1 递增；溢出前端侧必须生成新的 `sid` 并重新注册 |
| `direction` | text | 是 | `rx`、`tx` 或 `event` |
| `level` | text | 否 | `debug`、`info`、`warn` 或 `error`；默认 `info` |
| `data` | bytes | 条件 | 原始字节日志，与 `text` 二选一 |
| `text` | text | 条件 | UTF-8 文本日志，与 `data` 二选一 |
| `tags` | map[text,text] | 否 | 最多 8 个辅助标签 |

`data` 和 `text` 同时存在或同时缺失时，该 `log` 消息无效。

### 8.4 `heartbeat`

端侧按 `heartbeat_interval` 周期发送：

```text
{
  "v": 1,
  "type": "heartbeat",
  "sn": "SN-20260815-001",
  "sid": 305419896
}
```

ComTrace 回复：

```text
{
  "v": 1,
  "type": "heartbeat_ack",
  "sn": "SN-20260815-001",
  "sid": 305419896
}
```

连续 `offline_after_intervals` 个心跳周期没有收到该设备的任何消息时，ComTrace 应将设备标记为离线。

### 8.5 `goodbye`

端侧正常关闭前可发送：

```text
{
  "v": 1,
  "type": "goodbye",
  "sn": "SN-20260815-001",
  "sid": 305419896
}
```

ComTrace 收到后立即将该会话标记为离线。

### 8.6 `error`

TCP 中 ComTrace 发现协议错误后必须尽量发送 `error`，随后关闭连接或忽略当前消息。

```text
{
  "v": 1,
  "type": "error",
  "code": 1
}
```

错误码：

| `code` | 名称 | 含义 |
|---:|---|---|
| `1` | `invalid_frame` | 长度、CBOR 或字段格式错误 |
| `2` | `unsupported_version` | `v` 不等于 1 |
| `3` | `duplicate_device` | 同一 SN 出现冲突会话 |
| `4` | `rate_limited` | 日志速率超过限制 |
| `5` | `unsupported_type` | 不支持的消息类型 |

`code` 必须是无符号整数。ComTrace 应将详细失败原因保存在本地诊断日志中；端侧不得依赖未定义的错误详情字段。

## 9. TCP 行为

### 9.1 连接

端侧必须主动连接 ComTrace 的 `_comtrace-log._tcp.local.` 服务地址。连接超时和重连退避采用第 5.2 节默认值；失败后使用指数退避重连。

每条 TCP 连接只能注册一个 `sn`。端侧断线后必须重新连接并重新发送 `register`。

### 9.2 背压

TCP 可因网络拥塞而阻塞。端侧必须使用有限队列缓存待发送日志，默认上限见第 5.2 节；资源受限设备可降低该上限。

队列满时应丢弃最旧日志，并在下一条 `log` 的 `tags` 中加入：

```text
{"dropped_local": "<count>"}
```

ComTrace 必须将该信息记入日志状态。

## 10. UDP 行为

UDP 使用 `_comtrace-log._udp.local.` 服务，数据报内容仍为第 5 节定义的公共帧。

UDP 日志不进行逐条 ACK、重传或排序等待。ComTrace 按到达顺序显示日志。

### 10.1 丢包统计

对同一 `sn + sid`：

- 收到 `sequence == last_sequence + 1`：正常；
- 收到 `sequence > last_sequence + 1`：累计丢包数；
- 收到 `sequence <= last_sequence`：视为重复或乱序，不重复显示；
- `sid` 改变：重置该设备会话内的序号状态。

端侧可在 UDP 日志包中附加 `tags.dropped_local`，报告本地发送队列溢出的日志数量。

## 11. 设备识别和冲突处理

ComTrace 使用以下主键：

```text
设备：sn
会话：sn + sid
网络来源：IP + UDP/TCP 源端口
```

规则：

1. 同一 SN、同一 `sid` 从新的网络地址到达时，ComTrace 更新网络地址；
2. 同一 SN、不同 `sid` 且旧会话已离线时，ComTrace 切换到新会话；
3. 同一 SN、不同 `sid` 且旧会话仍在线时，ComTrace 标记身份冲突；
4. TCP 中遇到活动 SN 冲突时，ComTrace 返回 `duplicate_device` 并拒绝新连接；
5. 未携带 SN 的设备不符合 CELP/1，不应作为正式端侧接入。

## 12. 版本和兼容性

- `v` 必须为整数，V1 固定为 `1`；
- V1 接收端必须忽略未知的可选字段；
- 新增字段不得改变现有字段的类型或语义；
- 新增必选字段或改变帧格式时必须升级主版本；
- CBOR Map 的键顺序不具有语义；
- 不得依赖特定编程语言的结构体布局、字节对齐或序列化实现。

## 13. 互操作验证示例

CBOR 诊断表示法中的一条完整日志消息：

```text
{
  "v": 1,
  "type": "log",
  "sn": "SN-001",
  "sid": 305419896,
  "sequence": 7,
  "direction": "rx",
  "data": h'AA55010300'
}
```

以下示例采用确定性 CBOR（键按规范顺序编码），可作为端侧与 ComTrace 的字节级联调向量。Map 的键顺序在协议语义上无关，实现不要求使用此顺序。

```text
CBOR payload（66 B）
A761760162736E66534E2D303031637369641A12345678646461746145AA550103
006474797065636C6F676873657175656E63650769646972656374696F6E627278

完整帧（68 B）
0042A761760162736E66534E2D303031637369641A12345678646461746145AA55
0103006474797065636C6F676873657175656E63650769646972656374696F6E62
7278
```

首两个字节 `00 42` 表示后续 CBOR payload 长度为 66。TCP 使用一次或多次 `send()` 发送完整帧；UDP 使用一次 `sendto()` 发送完整帧。

## 14. OTA 更新传输协议 CEUP/1

### 14.1 协议边界

CEUP/1（`ComTrace Edge Update Protocol`）用于端侧设备请求 ComTrace 提供更新内容传输。它与 CELP/1 并列，不改变日志消息的字段、序号和丢包规则。

- CEUP/1 支持 TCP 和本地串口两种有序字节流传输；USB CDC 在系统层枚举为串口，复用本地串口传输；不得使用 UDP 传输升级包分片；
- CEUP/1 使用同样的 `2 字节大端长度 + CBOR Map` 公共帧；
- OTA 控制帧和固件分片不得写入 CELP/1 日志窗口；
- 设备身份仍由 `sn` 标识，会话仍由 `sn + sid` 标识；
- 本版本不定义加密、签名或远程授权字段；端侧必须在接受 `update_begin` 前，
  通过可信连接上下文、已验证的发布清单或等价本地策略授权目标版本和 SHA-256；
- CEUP/1 不提供设备配置修改和任意远程控制命令。

CELP/1 的 `max_frame=1200` 同样适用于 CEUP/1。`update_chunk.data` 的大小必须根据 CBOR 编码后的实际帧长度调整，完整帧不得超过 1200 字节。端侧通过 `max_chunk` 报告自身支持的最大数据片段，ComTrace 仍必须在发送前检查最终帧长度。

### 14.2 服务发现和连接

ComTrace 启用 OTA 监听时发布 `_comtrace-update._tcp.local.` 服务。TXT 记录至少包含：

| 键 | 示例 | 说明 |
|---|---|---|
| `proto` | `ceup/1` | OTA 协议名称和版本 |
| `encoding` | `cbor` | 消息编码 |
| `frame` | `u16be+cbor` | 公共帧格式 |
| `max_frame` | `1200` | CBOR payload 最大长度 |
| `instance_id` | `ct-7f3a91c2e8b44d1d5a0c3b6e9f102347` | ComTrace 实例标识 |

端侧应主动建立 OTA TCP 连接并首先发送 `update_register`。ComTrace 不主动扫描端侧，也不从 CELP/1 日志连接反向发起 OTA。

每条 OTA TCP 连接只允许一个 `sn`。传输阶段连接断开、设备重启或传输超时后，当前任务即失败，设备应清空当前连接的半帧并丢弃临时镜像；重新升级时必须重新注册并从第一个分片开始发送。设备在收到 `update_end` 并报告 `rebooting` 后，可以断开并重新注册；ComTrace 在时限内确认目标版本后才标记成功。

### 14.3 OTA 注册

端侧发送：

```text
{
  "v": 1,
  "type": "update_register",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "current_version": "1.2.0",
  "product_id": "sensor-a1",
  "max_chunk": 768,
  "max_image_size": 8388608
}
```

字段说明：

| 键 | 类型 | 必须 | 说明 |
|---|---|---:|---|
| `v` | uint | 是 | 固定为 `1` |
| `type` | text | 是 | 固定为 `update_register` |
| `sn` | text | 是 | 设备唯一 SN |
| `sid` | uint32 | 是 | 当前启动会话 ID |
| `current_version` | text | 是 | 当前运行固件版本 |
| `product_id` | text | 是 | 设备产品/硬件型号标识，用于更新包兼容性预检 |
| `max_chunk` | uint | 是 | 设备支持的最大 `data` 字节数 |
| `max_image_size` | uint | 是 | 设备允许写入的最大固件字节数 |

ComTrace 接受后回复：

```text
{
  "v": 1,
  "type": "update_register_ack",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "max_frame": 1200
}
```

设备不支持 OTA 时返回 `error`，随后关闭 OTA 连接。

### 14.4 更新包和任务准备

ComTrace 使用 ZIP 更新包；扩展名可以是 `.zip`、`.ctup` 或兼容的 `.ctfw`。包内容不限制具体类型，第一版至少包含：

```text
update.ctup
├── manifest.json
└── <content_file>
```

`manifest.json` 最小字段：

```json
{
  "format": "comtrace-update",
  "version": 1,
  "target_version": "1.3.0",
  "product_id": "sensor-a1",
  "content_file": "release.bin",
  "image_size": 524288,
  "sha256": "<64 个小写十六进制字符>"
}
```

ComTrace 在发送 `update_begin` 前必须完成本地预检：

1. 包、manifest 和 `content_file` 可读取；
2. `image_size` 与 `content_file` 实际大小一致；
3. 本地 SHA-256 与 manifest 一致；
4. 包的 `product_id` 与设备注册的 `product_id` 一致；
5. 目标版本高于当前版本；数字点分版本之外的比较，或降级，必须由用户明确允许；
6. `image_size` 不超过设备上报的 `max_image_size`；
7. 当前没有同一 `sn` 的其他活动 OTA 任务。

兼容旧包时，可使用 `format: "comtrace-firmware"`、`version_name` 和默认的 `firmware.bin`；但仍必须提供 `product_id`。

### 14.5 OTA 消息

#### `update_begin`

ComTrace 发送：

```text
{
  "v": 1,
  "type": "update_begin",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "target_version": "1.3.0",
  "product_id": "sensor-a1",
  "image_size": 524288,
  "sha256": "<64 个小写十六进制字符>",
  "chunk_size": 768
}
```

设备必须已经完成本连接的注册握手，并确认 `product_id` 与自身一致；不一致时回复错误码 `12`。端侧授权策略拒绝版本、摘要或连接身份时回复错误码 `13`。接受后回复 `update_status`，状态为 `prepared`，`received_bytes` 为 `0`。

#### `update_chunk`

```text
{
  "v": 1,
  "type": "update_chunk",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "chunk_index": 0,
  "offset": 0,
  "data": h'...'
}
```

`data` 必须是 CBOR byte string，不得使用 HEX 或 Base64 文本。除最后一片外，
`data` 长度必须等于协商的 `chunk_size`；最后一片长度必须等于镜像剩余字节数。
设备在当前连接内按顺序校验 `chunk_index` 和 `offset`，收到并写入临时区域后回复
`update_status`。ComTrace 收到确认后才发送下一片；连接断开后不恢复当前进度。

#### `update_end`

```text
{
  "v": 1,
  "type": "update_end",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "sha256": "<64 个小写十六进制字符>"
}
```

设备收到后先回复 `verifying`，根据 `image_size` 和 `chunk_size` 校验已接收分片数量，并重新计算完整镜像 SHA-256。校验和镜像提交成功后回复 `rebooting`；只有该状态发送成功后才执行或调度重启。校验失败返回 `failed`，不得切换活动固件。进入 `rebooting` 表示镜像已经提交，此后 `update_cancel`、`error` 或断线不得把任务改写为 `cancelled` 或 `failed`。

#### `update_status`

```text
{
  "v": 1,
  "type": "update_status",
  "sn": "SN-20260815-001",
  "sid": 305419896,
  "state": "transferring",
  "received_bytes": 1536,
  "error_code": 0
}
```

`state` 取值：`prepared`、`transferring`、`verifying`、`rebooting`、`success`、`failed`、`rolled_back`、`cancelled`。`error_code` 为 `0` 时表示没有错误。

只有设备重启后重新注册，并在 `current_version` 中上报与目标版本一致，ComTrace 才能把任务标记为成功；设备也可以通过 `update_status.state=success` 提供明确结果。传输完成不等于升级成功。

#### `update_cancel`

```text
{
  "v": 1,
  "type": "update_cancel",
  "sn": "SN-20260815-001",
  "sid": 305419896
}
```

设备收到后应停止接收新分片，丢弃临时镜像或保持未激活状态，并回复 `cancelled`。取消和断线不得破坏当前可启动固件。

### 14.6 传输失败和重试

- CEUP/1 V1 不支持断点续传，设备不需要持久化未完成任务的进度；
- 当前 TCP 或串口连接内，ComTrace 对未收到确认的 `update_begin`、`update_end`、`update_cancel` 和分片最多重试 3 次，设备必须幂等处理重复消息；
- 分片序号或 `offset` 错误时，设备返回 `offset_invalid`，不写入该分片；
- 传输阶段连接断开、设备重启或任务超时后，当前任务进入 `failed`，设备丢弃临时镜像；设备已报告 `rebooting` 时，ComTrace 等待其重新注册并核对目标版本；
- 重新升级必须从 `chunk_index=0`、`offset=0` 开始；
- ComTrace 重启后不能恢复传输进度，用户需要重新发起升级。

### 14.7 错误码

CEUP/1 使用独立错误码，不复用 CELP/1 的日志错误码：

| `code` | 名称 | 含义 |
|---:|---|---|
| `1` | `invalid_frame` | 长度、CBOR 或字段格式错误 |
| `2` | `unsupported_version` | `v` 不等于 1 |
| `3` | `unsupported_type` | 不支持的消息类型 |
| `10` | `ota_not_supported` | 设备不支持 OTA |
| `11` | `task_busy` | 同一 SN 已存在活动任务 |
| `12` | `product_mismatch` | 固件型号不匹配 |
| `13` | `version_rejected` | 版本或 Bootloader 条件不满足 |
| `14` | `image_invalid` | 固件大小或 SHA-256 不匹配 |
| `15` | `offset_invalid` | 分片 offset 或序号不正确 |
| `16` | `write_failed` | 设备写入失败 |
| `17` | `verify_failed` | 完整镜像校验失败 |
| `18` | `boot_failed` | 新固件启动失败 |
| `19` | `rollback` | 设备已回滚到旧固件 |
| `20` | `task_cancelled` | OTA 任务已取消 |
| `21` | `connection_lost` | 传输连接断开或超时，仅用于端侧本地状态 |

错误消息沿用 `error` Map：

```text
{
  "v": 1,
  "type": "error",
  "code": 14
}
```

### 14.8 设备端升级约束

设备实现必须保证：

- 固件写入非活动分区或临时升级区；
- 不覆盖当前可启动固件；
- 完整镜像校验失败时拒绝切换；
- `update_begin` 必须经过产品定义的可信授权策略；
- 新固件首次启动必须报告启动结果；
- 启动失败或健康检查超时自动回滚；
- 断电、断线和取消任务不能破坏旧固件。

### 14.9 兼容性

- CELP/1 与 CEUP/1 版本独立，升级协议版本变化不修改日志协议版本；
- CELP/1 与 TCP 形式的 CEUP/1 可以使用同一个 ComTrace 实例 ID，但必须使用不同的 mDNS 服务类型和 TCP 端口；
- 未知的可选字段必须忽略；
- 未知的 OTA `type` 必须返回错误码 `3`（`unsupported_type`）；
- CBOR Map 键顺序不具有语义；
- OTA 固件数据不得通过 UDP 发送，也不得嵌套完整协议帧。

### 14.10 串口与 USB CDC 传输

串口与 USB CDC 使用 CEUP/1 的同一套消息、状态机和公共帧格式，不定义
`CEUP/1-S`、起始符、字节转义或额外 CRC。实现只将 TCP 的字节读写替换为
串口读写：

```text
+-------------------+---------------------------+
| 2 B CBOR 长度     | N B CBOR 消息              |
+-------------------+---------------------------+
```

帧长度为无符号 16 位大端整数，规则与第 5.1 节和第 5.3 节完全相同：串口接收端
必须支持半帧和连续多帧；每个完整帧的 CBOR payload 必须为 `1..1200` 字节。
因此 TCP 与串口可以复用同一个 `FrameDecoder`、CBOR 编解码器和
`update_*` 消息校验逻辑。

串口和 USB CDC 不使用 mDNS。用户在 ComTrace 中选择系统枚举的串口或 USB CDC 端口、波特率、数据位、校验位、停止位和流控后打开端口；设备 Bootloader 或升级代理必须主动发送 `update_register`，随后
交互与第 14.3 至 14.6 节完全一致。

串口和 USB CDC 模式的附加约束：

- V1 的发送窗口固定为 `1`：ComTrace 只有收到当前 `update_chunk` 对应的
  `update_status` 后，才能发送下一片；
- 未收到确认时，ComTrace 可以最多重发当前控制帧或分片 3 次；设备必须按
  `chunk_index + offset` 幂等处理重复分片，不得重复写入；
- 建议启用硬件流控（RTS/CTS）。若设备不支持，端侧必须通过 `max_chunk` 选择可在
  接收缓冲区内完整容纳的分片大小；
- 串口端出现帧长度非法、帧接收超时、CBOR 解码失败或收到不符合当前状态的消息时，
  当前 OTA 会话必须失败。ComTrace 必须清空接收缓存，设备必须重新发送
  `update_register` 后才能开始新的任务；
- 串口链路的逐片 `update_status` 确认、`update_end` 的 SHA-256 完整性校验和设备端
  写入校验共同覆盖传输可靠性。需要抗电磁干扰的硬件可在 CEUP/1 之下增加链路层
  CRC，但该 CRC 不属于 CEUP/1，双方必须在串口配置中另行约定。

## 15. OTA 互操作检查清单

端侧和 ComTrace 联调至少验证：

- `update_register` 能力协商；
- 产品型号和版本预检失败；
- `update_begin`、分片确认和 `update_end`；
- 当前连接内的分片确认、超时重试和错误 offset；
- TCP 断开后任务失败并从头重新升级；
- 串口半帧、连续多帧、超时和坏帧后重新注册；
- 串口分片确认、超时重发以及重复分片幂等处理；
- SHA-256 校验失败；
- 设备重启后版本确认；
- 新固件启动失败并回滚；
- 取消任务和 ComTrace 重启后重新发起完整升级；
- OTA 消息不会进入 CELP/1 日志窗口。
