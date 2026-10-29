# QM WebSocket Client 组件方案

## 目标

## 架构

```text
应用事件与发送 API
        v
qm_websocket_client
        | 握手、帧、PING、重连
        v
QUMI TCP/TLS transport
        v
    WebSocket 服务
```

`qm_websocket_client` 提供基于 qumi 抽象接口的 WebSocket Client，支持 `ws://` 和 `wss://`，用于设备与云端或局域网 WebSocket 服务进行长连接通信。

组件设计目标：

- 只依赖 qumi 网络、系统、内存、日志和工具接口，不直接调用 ESP-IDF socket、FreeRTOS、lwIP 或 mbedTLS API。
- TCP/TLS 传输层可替换，业务层只感知 WebSocket 连接、事件和收发 API。
- 支持自动重连、PING/PONG 保活、分片发送、分块接收、clean close 和错误分类。
- 大 payload 接收不按完整帧分配内存，而是按固定 `buffer_size` 分块派发事件。

## 文件结构

```text
components/qm_websocket_client/
├── CMakeLists.txt
├── Kconfig
├── README.md
├── TRANSPORT_DESIGN.md
├── include/
│   ├── qm_websocket_client.h
│   └── qm_websocket_transport.h  # internal component header
└── src/
    ├── qm_websocket_client.c
    └── qm_websocket_transport.c
```

## 依赖边界

组件内部依赖：

| 依赖 | 用途 |
| --- | --- |
| `qm_kernel.h` | task、queue、mutex、sleep、内存分配 |
| `qm_log.h` | 日志输出 |
| `qm_tcp.h` | `ws://` TCP transport |
| `qm_tls.h` | `wss://` TLS transport |
| `qm_utils_base64.h` | WebSocket handshake key 编码 |
| `qm_utils_sha1.h` | WebSocket accept 校验 |
| `qm_utils_timer.h` | 超时、保活、stop 等时间判断 |
| `backoff_algorithm.h` | 自动重连指数退避和 jitter |

公共头文件只暴露 `qm_types.h`、`qm_errno.h` 和组件自身类型。`qm_tcp.h`、`qm_tls.h` 不向上层业务泄漏。

## 基本流程

1. 准备网络，如 WiFi STA 获取 IP。
2. 填写 `qm_websocket_client_config_t`。
3. 调用 `qm_websocket_client_init()` 创建 client。
4. 调用 `qm_websocket_register_events()` 注册事件回调。
5. 可选调用 `qm_websocket_client_append_header()` 或 `qm_websocket_client_set_headers()` 设置握手 header。
6. 调用 `qm_websocket_client_start()` 启动连接任务。
7. 在事件回调中处理连接、数据、错误、断开事件。
8. 可选调用 `qm_websocket_unregister_events()` 取消事件回调。
9. 结束时调用 `qm_websocket_client_close()` 或 `qm_websocket_client_stop()`，再 `qm_websocket_client_destroy()`。

## 配置项

`qm_websocket_client_config_t` 用于每个 client 实例的连接参数和按实例覆盖项。数值字段填 `0`
时使用 `qm_config` / Kconfig 默认值，除非字段注释明确说明了其他语义。

接口入参建议放这些“每个连接可能不同”的信息：

| 字段 | 说明 |
| --- | --- |
| `uri` | WebSocket URI，支持 `ws://`、`wss://` |
| `host` / `port` / `path` / `transport_over_ssl` | 不使用 `uri` 时的连接目标 |
| `subprotocol` | `Sec-WebSocket-Protocol`，`init` 时复制 |
| `user_agent` | 握手阶段 User-Agent，`init` 时复制 |
| `headers` | 额外 HTTP header，格式为 `key: value\r\n`，`init` 时复制 |
| `auth` | Authorization header 内容，`init` 时复制 |
| `disable_auto_reconnect` | 禁用自动重连 |
| `disable_ping` | 创建连接后不启动 PING/PONG 保活 |
| `tls_config` | `wss://` TLS 证书配置 |
| `transport` | 自定义 transport，非空时使用外部 transport |
| `user_context` | client 级用户上下文 |

这些字段是实例级覆盖项，通常不需要在业务里重复填写默认值：

| 字段 | 默认来源 |
| --- | --- |
| `task_priority` | `CONFIG_QM_WEBSOCKET_CLIENT_TASK_PRIORITY` |
| `task_stack` | `CONFIG_QM_WEBSOCKET_CLIENT_TASK_STACK_SIZE` |
| `keepalive_ping_interval_ms` | `CONFIG_QM_WEBSOCKET_CLIENT_KEEPALIVE_INTERVAL` |
| `reconnect_interval_ms` | `CONFIG_QM_WEBSOCKET_CLIENT_RECONNECT_INTERVAL` |
| `max_reconnect_timeout_ms` | `CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_TIMEOUT` |
| `max_reconnect_count` | `CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_COUNT` |
| `network_timeout_ms` | `CONFIG_QM_WEBSOCKET_CLIENT_NETWORK_TIMEOUT` |
| `buffer_size` | `CONFIG_QM_WEBSOCKET_CLIENT_BUFFER_SIZE` |
| `send_queue_size` | `CONFIG_QM_WEBSOCKET_CLIENT_SEND_QUEUE_SIZE` |

Kconfig 提供默认值配置：

- `QM_WEBSOCKET_CLIENT_TASK_STACK_SIZE`
- `QM_WEBSOCKET_CLIENT_TASK_PRIORITY`
- `QM_WEBSOCKET_CLIENT_BUFFER_SIZE`
- `QM_WEBSOCKET_CLIENT_KEEPALIVE_INTERVAL`
- `QM_WEBSOCKET_CLIENT_RECONNECT_INTERVAL`
- `QM_WEBSOCKET_CLIENT_NETWORK_TIMEOUT`
- `QM_WEBSOCKET_CLIENT_MAX_RECONNECT_COUNT`
- `QM_WEBSOCKET_CLIENT_SEND_QUEUE_SIZE`
- `QM_WEBSOCKET_CLIENT_MAX_RECONNECT_TIMEOUT`
- `QM_WEBSOCKET_CLIENT_STOP_WAIT_MS`
- `QM_WEBSOCKET_CLIENT_PING_TIMEOUT`
- `QM_WEBSOCKET_CLIENT_PING_MAX_RETRY`
- `QM_WEBSOCKET_CLIENT_LOG_CONFIG`
- `QM_WEBSOCKET_CLIENT_LOG_ERROR`
- `QM_WEBSOCKET_CLIENT_LOG_LIFECYCLE`
- `QM_WEBSOCKET_CLIENT_LOG_CONTROL`
- `QM_WEBSOCKET_CLIENT_LOG_TX`
- `QM_WEBSOCKET_CLIENT_LOG_RX`

## 事件模型

事件类型：

| 事件 | 说明 |
| --- | --- |
| `WEBSOCKET_EVENT_CONNECTED` | 握手成功，连接可用 |
| `WEBSOCKET_EVENT_DATA` | 收到数据或控制帧，取决于配置 |
| `WEBSOCKET_EVENT_DISCONNECTED` | 连接断开 |
| `WEBSOCKET_EVENT_ERROR` | 发生错误 |
| `WEBSOCKET_EVENT_MAX` | 注册时表示注册所有事件 |

`qm_websocket_event_data_t` 关键字段：

| 字段 | 说明 |
| --- | --- |
| `data_ptr` / `data_len` | 当前事件的数据片段，指针只在当前回调期间有效 |
| `opcode` | WebSocket opcode |
| `fin` | 当前消息或分片是否结束 |
| `payload_len` | 当前 WebSocket frame 的完整 payload 长度 |
| `payload_offset` | 当前事件片段在 payload 中的偏移 |
| `client` | client handle |
| `user_context` | client 配置中的用户上下文 |
| `error_code` | qumi 错误码 |
| `error_info` | 错误分类、transport 返回码、handshake 状态 |

接收大 payload 时，组件会按 `buffer_size` 分块派发多个 `WEBSOCKET_EVENT_DATA`。业务需要使用 `payload_offset`、`payload_len`、`fin` 判断是否收到完整消息。

事件回调可通过 `qm_websocket_unregister_events()` 取消注册：

```c
qm_websocket_unregister_events(client, WEBSOCKET_EVENT_DATA, websocket_event_handler);
qm_websocket_unregister_events(client, WEBSOCKET_EVENT_MAX, NULL);
```

`event_id` 为 `WEBSOCKET_EVENT_MAX` 时表示处理全部事件；`event_handler` 为 `NULL` 时表示清除目标事件上的任意 handler。

## 收发 API

普通发送：

```c
qm_websocket_client_send_text(client, "hello", 5, 5000);
qm_websocket_client_send_bin(client, data, len, 5000);
```

发送 API 会把数据复制到内部队列并异步发送；返回值表示已入队的字节数，不代表底层 socket 已完成写入。实际发送失败会通过 `WEBSOCKET_EVENT_ERROR` 上报。

发送超时为零时不等待准入锁或队列容量：锁竞争返回 `-QM_EBUSY`，队列满返回 `-QM_EFULL`。有限超时共用一个准入预算。断线时清理尚未发送的旧连接数据并复位分片状态，旧连接等待中的发送调用不能向新连接入队。发送成功后调用者可以复用输入缓存。销毁前必须停止外部发送调用，并串行执行生命周期 API；本次准入改造不替代原有连接任务的退出契约。

分片发送：

```c
qm_websocket_client_send_text_partial(client, part1, part1_len, 5000);
qm_websocket_client_send_continuation_frame(client, part2, part2_len, 5000);
qm_websocket_client_send_fin(client, 5000);
```

clean close：

```c
qm_websocket_client_close(client, 5000);
qm_websocket_client_close_with_code(client, 1000, "normal", 6, 5000);
```

`qm_websocket_client_stop()` 会直接停止连接任务并关闭 transport；`qm_websocket_client_close()` 会先发送 CLOSE frame。

## 运行时参数

PING 间隔和自动重连间隔支持运行时查询和修改：

```c
int ping_ms = qm_websocket_client_get_ping_interval(client);
qm_websocket_client_set_ping_interval(client, 30000);
qm_websocket_client_set_ping_interval(client, 0); /* disable ping at runtime */

int reconnect_ms = qm_websocket_client_get_reconnect_timeout(client);
qm_websocket_client_set_reconnect_timeout(client, 5000);
```

配置阶段可设置 `disable_ping = QM_TRUE` 禁用 PING；`keepalive_ping_interval_ms = 0` 表示使用默认间隔。`qm_websocket_client_set_ping_interval()` 允许传入 `0` 在运行时禁用 PING；连接中修改为非 0 值会重置下一次 PING 计时。`qm_websocket_client_get_reconnect_timeout()` 在自动重连禁用时返回 `-1`。

自动重连延迟使用 qumi `backoff_algorithm` 组件计算，基于 `reconnect_interval_ms` 做指数退避并加入 jitter，最大延迟由 `CONFIG_QM_WEBSOCKET_CLIENT_MAX_RECONNECT_TIMEOUT` 或实例配置限制。连接成功或主动调用 `qm_websocket_client_reconnect()` 后会重置 backoff 状态。

### 日志开关

日志按组件全局分类控制，不修改全局 QUMI 日志等级。Kconfig 决定初始掩码，
运行时修改会同时作用于所有 client 实例及内置 TCP/TLS transport：

```c
qm_websocket_client_config_t config = {
    .uri = "wss://example.com/ws",
};

/* 运行中临时打开完整链路日志。 */
qm_websocket_client_set_log_flags(QM_WEBSOCKET_LOG_ALL);

/* 统一关闭所有 client 和内置 TCP/TLS transport 日志。 */
qm_websocket_client_set_log_flags(QM_WEBSOCKET_LOG_NONE);
```

类别含义：

| 类别 | 内容 |
| --- | --- |
| `QM_WEBSOCKET_LOG_ERROR` | 连接、握手、协议、收发失败 |
| `QM_WEBSOCKET_LOG_LIFECYCLE` | 初始化、连接、重连、断开、停止 |
| `QM_WEBSOCKET_LOG_CONTROL` | PING、PONG、CLOSE 控制帧 |
| `QM_WEBSOCKET_LOG_TX` | 上行队列、WebSocket 帧和 transport 写入明细 |
| `QM_WEBSOCKET_LOG_RX` | 下行 WebSocket 帧、数据分发和 transport 读取明细 |

`CONFIG_QM_WEBSOCKET_CLIENT_LOG_CONFIG` 是分类配置总开关。关闭时分类选项隐藏，组件默认只启用错误和生命周期日志；打开后可分别设置错误、生命周期、控制、TX 和 RX 默认日志。`TX`、`RX` 属于高频 DEBUG 日志，只有对应 Kconfig 分类和运行时掩码都启用时才会产生；若全局 QUMI 日志等级过滤了 DEBUG，即使打开 `TX`/`RX` 也不会输出这些明细。

## 分层设计

组件内部也按三层划分：

- `client`：位于 `src/qm_websocket_client.c`，负责 URI 解析、握手、收发帧、分片拼装、事件派发、keepalive 和自动重连状态机。
- `transport`：位于 `include/qm_websocket_transport.h` 与 `src/qm_websocket_transport.c`，负责把 `qm_tcp` / `qm_tls` 适配成 WebSocket 内部统一的 connect/read/write/close 抽象。
- `hal`：由 qumi 现有底层组件提供，包括 `qm_tcp`、`qm_tls`、`qm_kernel`、`qm_string`、定时器和随机数等能力。

这种结构让业务只依赖 `qm_websocket_client.h`，而 transport 与底层网络能力保持在组件内部；如果后续替换底层连接实现，优先改 transport 适配层即可。

## TLS 与 Transport

默认 transport：

- `ws://` 使用 `qm_tcp_establish()`、`qm_tcp_read()`、`qm_tcp_write()`、`qm_tcp_destroy()`。
- `wss://` 使用 `qm_tls_establish()`、`qm_tls_read()`、`qm_tls_write()`、`qm_tls_destroy()`。

TLS 配置：

```c
qm_websocket_tls_config_t tls_config;

qm_websocket_tls_config_init(&tls_config,
                             ca_cert, strlen(ca_cert),
                             NULL, 0,
                             NULL, 0);

qm_websocket_client_config_t cfg = {
    .uri = "wss://example.com/ws",
    .tls_config = &tls_config,
};
```

双向认证：

```c
qm_websocket_tls_config_t tls_config;

qm_websocket_tls_config_init(&tls_config,
                             ca_cert, strlen(ca_cert),
                             client_cert, strlen(client_cert),
                             client_key, strlen(client_key));

qm_websocket_client_config_t cfg = {
    .uri = "wss://example.com/ws",
    .tls_config = &tls_config,
};
```

`qm_websocket_client_init()` 会复制 `tls_config` 结构体本身，但不会复制证书内容；证书 buffer 需要在 client 生命周期内保持有效。

只有需要完全替换底层读写适配时才显式传入自定义 `transport`。TLS 单向/双向认证统一使用 `qm_websocket_tls_config_init()`，是否传入 `client_crt` 和 `client_key` 决定认证模式。

组件内部根据 URI scheme 选择内置 TCP/TLS transport。外部统一包含 `qm_websocket_client.h`，设置 `uri` 和可选 `tls_config`；只有完全替换底层读写适配时才通过 `transport` 字段传入自定义 transport。

## opencpu 示例

工程内示例位于：

```text
apps/opencpu/app/websocket_client_example.c
```

已在 `apps/opencpu/main/CMakeLists.txt` 中加入编译，并在 `apps/opencpu/app/qm_app.c` 的 `qm_application_start()` 中启动。

运行前需要修改：

```c
#define EXAMPLE_WIFI_SSID       "your_ssid"
#define EXAMPLE_WIFI_PASSWORD   "your_password"
#define EXAMPLE_WEBSOCKET_URI   "ws://echo.websocket.events"
```

构建：

```sh
cd apps/opencpu
. ../../export.sh
idf.py build
```

烧录：

```sh
idf.py -p PORT flash monitor
```

## 通用示例

通用示例位于：

```text
examples/websocket_client/
└── websocket_client_example.c
```

`websocket_client_example.c` 提供完整 WiFi + WebSocket 示例入口，同时集中演示组件主要 API：事件注册/反注册、header 设置、PING/重连参数 get/set、文本/二进制/分片发送、clean close、TCP、TLS 单向认证和 TLS 双向认证。

可复用启动函数：

- `websocket_client_tcp_example_start()`
- `websocket_client_host_example_start()`
- `websocket_client_tls_server_auth_example_start()`
- `websocket_client_tls_mutual_auth_example_start()`
- `websocket_client_example_start()`
- `websocket_client_reconnect_example()`
- `websocket_client_graceful_close_example()`
- `websocket_client_example_stop()`

运行前可按需定义：

```c
#define EXAMPLE_WIFI_SSID       "your_ssid"
#define EXAMPLE_WIFI_PASSWORD   "your_password"
#define EXAMPLE_WEBSOCKET_URI   "ws://echo.websocket.events"
```

使用 `wss://` 时可额外定义 `EXAMPLE_TLS_CA_CERT` / `EXAMPLE_TLS_CA_CERT_LEN`。需要双向认证时再定义 `EXAMPLE_TLS_CLIENT_CERT`、`EXAMPLE_TLS_CLIENT_CERT_LEN`、`EXAMPLE_TLS_CLIENT_KEY`、`EXAMPLE_TLS_CLIENT_KEY_LEN`。

## 当前限制

- URI 解析支持 `ws://host[:port][/path][?query]` 和 `wss://host[:port][/path][?query]`，暂不支持 IPv6 literal。
- handshake 响应头最大读取 1024 字节。
- 详细 TLS/socket 底层错误目前只能记录 qumi transport 返回码；需要 `qm_tcp/qm_tls` 后续暴露 errno、TLS verify flags 后才能补齐。
- clean close 当前发送 CLOSE frame 后会关闭 transport，不等待服务端回显 CLOSE frame。
- 控制帧 payload 最大 125 字节，符合 RFC 6455 控制帧限制。
- 分片接收会按帧派发，不跨帧聚合完整消息；业务层可通过 `opcode`、`payload_offset`、`payload_len`、`fin` 自行聚合。

## 后续优化建议

- 为 `qm_tcp/qm_tls` 扩展 last error 查询接口，补齐 `error_info`。
- 完善 RFC 6455 协议校验，如控制帧不得分片、保留位检查、close code 合法性检查。
- 进一步补齐 close code 合法性校验和服务端 CLOSE 回显等待。
