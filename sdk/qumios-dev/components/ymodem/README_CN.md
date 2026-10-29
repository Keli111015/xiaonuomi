# YMODEM 串口固件升级组件技术方案

[English](README.md)

## 1. 文档目的

本文定义 PC 通过 UART 向运行中的 ESP32/QUMI 应用传输固件镜像的实现方案，明确协议处理、OTA 存储接口、会话生命周期、异常处理及集成要求。

## 2. 适用范围与约束

本组件用于应用态串口升级。PC 端使用 YMODEM-CRC 发送 ESP App 镜像，设备端接收并写入 OTA 分区。ESP ROM 下载模式及 `esptool` 使用的 ROM 下载协议不在本方案范围内。

发送文件必须是与目标 OTA 分区匹配的 App 镜像。bootloader、分区表和 App 合并镜像应按产品既定烧录流程处理，不作为普通 App 镜像直接发送。

升级串口在会话期间必须独占。调用方应停止该串口上的业务流量和日志，并在会话开始前清理 RX 缓冲区。

## 3. 设计目标与原则

- 协议处理与 UART、OTA 平台实现解耦，通用代码只依赖 QUMI 抽象接口。
- 数据经块号、块号反码和 CRC-16/XMODEM 校验后，才允许写入存储后端。
- 存储提交与传输确认保持明确顺序，`finish` 成功后才发送最终 ACK。
- 传输失败、存储失败或用户取消时，释放未提交的存储会话。
- 接口保持同步调用，回调的所有权和上下文生命周期由调用方管理。

## 4. 系统架构与职责

```text
PC YMODEM 工具
        │ UART 字节流
        ▼
ymodem：帧解析、CRC、ACK/NAK、重传、结束握手
        │ ymodem_config_t
        ▼
应用适配层：UART 读写、OTA begin/write/finish/abort
        │
        ▼
QUMI OTA / 固件存储后端
```

| 模块 | 职责 |
| --- | --- |
| PC YMODEM 工具 | 建立发送会话，发送 block 0、数据块和 EOT。 |
| `ymodem` | 发送 CRC 请求，解析 SOH/STX 帧，校验块号、反码和 CRC，执行 ACK/NAK、重试及结束握手。 |
| 应用适配层 | 提供 UART 字节流回调和 OTA 存储回调，将校验后的数据写入目标 OTA 分区。 |
| 升级入口 | 独占 UART、清理 RX、调用 `ymodem_receive`，并按产品策略决定重启。 |

协议核心不直接依赖 `qm_uart_dev_t`、ESP-IDF 或 `qm_ota_*`；应用通过
`ymodem_config_t` 提供平台回调。UART/OTA 适配示例位于
`examples/ymodem_uart_example.c`。

## 5. 升级流程

1. 接收器发送字符 `C`，请求 CRC-16/XMODEM 模式。
2. 接收并校验 block 0，解析文件名和十进制文件长度。
3. block 0 校验通过后调用 `storage.begin`；成功后回复 ACK 和 `C`。
4. 按序接收 SOH 128 字节或 STX 1024 字节数据块，校验通过后调用 `storage.write`。
5. 仅向 `storage.write` 提供声明文件长度以内的数据，最后一块的协议填充不写入 OTA。
6. 收到 EOT 后执行 EOT/ACK/空 block 0 结束握手。
7. 空 block 0 校验通过后调用 `storage.finish` 提交镜像，成功后发送最终 ACK。

## 6. YMODEM 协议处理

- 支持 SOH 128 字节帧和 STX 1024 字节帧。
- 校验块号、块号反码和大端 CRC-16/XMODEM。
- 数据块必须按期望块号到达；坏 CRC、序号错误和帧超时按帧重试。
- 已确认数据块的重复帧只回复 ACK，不重复调用 `storage.write`。
- 块号为 8 位值，255 后的有效数据块号为 0。
- block 0 的文件名按 basename 处理，必须为可打印字符且不得包含 `/` 或 `\\`；文件名长度和镜像长度受配置限制。
- 兼容部分发送端在首个 EOT 后直接等待 ACK 的行为，以及最终空 block 0 前重复发送 EOT 的恢复流程。

标准结束流程如下：

```text
EOT → NAK → EOT → ACK → C → 空 block 0 → finish → ACK
```

`storage.finish` 为同步操作，应在 PC 发送端的握手等待时间内完成。若 OTA 校验或 Flash 操作耗时较长，应在目标板上验证所用 PC 工具的超时设置。

## 7. 存储与 OTA 生命周期

四个存储回调均为必填项：

| 回调 | 调用时机 | 约束 |
| --- | --- | --- |
| `begin` | block 0 校验通过后、首次 ACK 前 | 准备写入目标，成功后进入写入状态。 |
| `write` | 每个有效数据块校验通过后 | 只接收声明长度以内的有效固件数据，必须同步完成。 |
| `finish` | 空 block 0 校验通过后、最终 ACK 前 | 提交镜像，不得在回调内重启。 |
| `abort` | `begin` 成功且 `finish` 未成功时 | 释放未提交的写入目标，不得切换启动分区；每次会话最多调用一次。 |

`finish` 成功后，即使最终 UART ACK 发送失败，也不会再次调用 `abort`。存储适配层应保证 `abort` 可在写入失败、超时和取消路径安全调用。

`begin` 失败时必须自行释放本次申请的资源，此时组件不调用 `abort`。
`write` 返回 `QM_EOK` 表示传入的 `length` 字节已全部同步写入；失败返回负错误码。

## 8. API 调用顺序

接口定义见 [include/ymodem.h](include/ymodem.h)。调用顺序如下：

1. 填充 `ymodem_config_t`。
2. 调用 `ymodem_create()`。
3. 在拥有升级 UART 的任务中调用 `ymodem_receive()`。
4. 返回 `QM_EOK` 后，`storage.finish` 已完成镜像提交；应用按产品策略重启。
5. 调用 `ymodem_destroy()`。

同一句柄同一时间只允许一个接收会话。调用方必须保证 `ymodem_receive`、回调、
`ymodem_destroy`、UART 和回调上下文的访问串行化。`ymodem_destroy` 只能用于空闲句柄，不负责终止活动会话。

### 8.1 数据结构

| 结构体 | 成员 | 用途与约束 |
| --- | --- | --- |
| `ymodem_file_info_t` | `name`、`total_size` | 文件名与声明字节数，供存储准备和提交校验使用；长度允许为 0，由存储后端决定是否接受。元数据及名称仅在回调期间借用，需要保留时应复制。 |
| `ymodem_storage_t` | `begin`、`write`、`finish`、`abort`、`user` | 四个必填的存储操作及共用上下文。 |
| `ymodem_progress_t` | `transferred`、`total_size` | 已成功写入的有效字节数与声明总字节数，供应用计算百分比或显示字节进度。 |
| `ymodem_config_t` | 传输、存储、时限、长度限制和进度配置 | 创建时复制；运行时默认值在组件内部副本中解析，不修改调用方配置。 |

`ymodem_config_t` 的成员约定如下：

| 成员 | 约定 |
| --- | --- |
| `read`、`write`、`transport_user` | 两个传输回调必填，共用传输上下文。 |
| `storage` | 存储回调组，使用独立的 `storage.user`。 |
| `handshake_timeout_ms` | 首包与末尾帧的等待时限；有效覆盖值为 1..`UINT32_MAX / 2` ms。 |
| `packet_timeout_ms` | 数据帧的等待时限，包含等待起始字节及后续整帧；有效覆盖值同上。 |
| `retry_max` | 首次尝试之后的最大重试次数，非零覆盖值为 1..255。 |
| `max_file_size` | 最大镜像字节数，应按存储目标容量设置。 |
| `progress`、`progress_user` | 可选进度回调及独立的应用上下文；`progress` 为 NULL 时忽略 `progress_user`。 |

两个时限、重试次数和最大镜像长度设为 0 时使用 Kconfig 默认值。
`retry_max = 0` 不表示禁用重试，`max_file_size = 0` 不表示无限制。
超出定时器范围的时限使 `ymodem_create()` 返回 NULL。
三个上下文指针分别服务于传输、存储和应用通知，允许为 NULL；非空时所引用对象必须保持有效至句柄销毁。

### 8.2 进度回调

`ymodem_progress_fn` 接收 `const ymodem_progress_t *progress` 和配置中的
`progress_user`。每次 `storage.write` 成功后、该块 ACK 之前调用。
重复块、失败的写入及零长度文件不触发进度回调。
进度快照仅在回调期间有效；跨任务通知应复制字段值，不得保存快照指针。

百分比由应用在回调内计算，乘法使用 64 位中间值：

```c
uint32_t percent = 0U;

if (progress->total_size != 0U) {
    percent = (uint32_t)(((uint64_t)progress->transferred * 100U) /
                         progress->total_size);
}
```

回调返回 `QM_EOK` 继续接收，其他值请求取消。`transferred == total_size`
只表示数据写入完成；升级完成以 `ymodem_receive()` 返回 `QM_EOK` 为准。

## 9. UART 传输要求

- 升级 UART 必须由本次会话独占，禁止混入业务数据和日志。
- `read`/`write` 回调允许部分读写；返回正数表示已处理的字节数，返回 `0` 或 `-QM_ETIMEOUT` 表示超时，其他负值表示传输错误。
- `packet_timeout_ms` 是一次数据帧读取的总时限，包含等待起始字节的时间。9600 baud、8N1 下接收 1024 字节 STX 帧约需 1.1 秒，配置应为传输和调度留出余量。
- 半帧超时后，组件会在一个 `packet_timeout_ms` 窗口内继续读取并丢弃当前帧剩余字节，然后发送 NAK。UART 适配层应保证旧帧残留在该窗口内可读，或提供独立的 RX flush；否则纯字节流无法区分迟到的旧帧尾和下一帧控制字节。

## 10. 错误、重试与取消

`ymodem_receive()` 返回值约定：

- `QM_EOK`：镜像已由 `finish` 提交。
- `-QM_ETIMEOUT`：协议超时或重试次数耗尽。
- `-QM_EIO`：UART 传输错误或帧格式错误。
- `-QM_EINVAL`：配置、文件名或镜像长度无效。
- `-QM_EBUSY`：同一句柄已有接收会话。
- `-QM_EINTR`：PC 发送 CAN 或进度回调请求取消。
- 存储回调返回的负错误码：原样向上传递。

数据块重试次数按块独立计算。会话失败时，组件尽力发送 8 个 CAN 字节；如果已收到远端 CAN，或 OTA 已由 `finish` 提交，则不再发送取消序列。

组件不提供独立日志通道。升级状态通过返回值和可选进度回调传递；升级 UART 不得输出应用日志。

## 11. 配置、构建与验证

启用 `YMODEM_SUPPORT` 后选择 `QM_TIMER`、`QM_CRC` 和 `QM_STRING`，并生成以下配置：

- `YMODEM_HANDSHAKE_TIMEOUT_MS`：block 0 和结束握手超时。
- `YMODEM_PACKET_TIMEOUT_MS`：整帧接收超时。
- `YMODEM_RETRY_MAX`：单帧最大重传次数。
- `YMODEM_FILE_NAME_MAX_LEN`：block 0 文件名最大长度。
- `YMODEM_MAX_FILE_SIZE`：允许接收的最大镜像长度。

OpenCPU 工程在 `CONFIG_YMODEM_SUPPORT=y` 时加入 `src/ymodem.c` 和
`utility/crc/qm_crc.c`。`examples/ymodem_uart_example.c` 仅用于应用适配演示，
需由应用源码列表显式加入。

主机测试覆盖 SOH/STX、部分读写、半帧超时恢复、坏 CRC、重复块、块号回卷、尾部填充、存储失败、取消、结束握手及 finish/abort/ACK 生命周期，并校验进度字节数、默认配置、配置副本与超时参数范围。目标板还应验证实际 PC 工具握手、目标波特率下的整帧时间、OTA 提交与失败释放，以及升级期间业务流量和日志隔离。

## 12. 参考资料

- [Tera Term source code overview](https://teratermproject.github.io/manual/4/en/reference/sourcecode.html)
- [X/YMODEM protocol reference](https://android.googlesource.com/platform/external/syslinux/+/refs/heads/oreo-m7-release/com32/libupload/ymodem.txt)
