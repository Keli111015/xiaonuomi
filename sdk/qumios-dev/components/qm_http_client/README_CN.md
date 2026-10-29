# QM HTTP Client 组件说明

## 目标

`qm_http_client` 提供基于 qumi 抽象接口的 HTTP/HTTPS Client，支持：

- `http://` 和 `https://`
- `GET / POST / PUT / DELETE / HEAD`
- Basic 认证
- Digest 认证：`MD5`、`MD5-sess`、`SHA-256`、`SHA-256-sess`
- 自动重定向
- 固定长度、`chunked` 和 `until-close` 响应体读取
- 事件回调和兼容旧接口的 `on_body`

组件设计目标：

- 只依赖 qumi 网络、TLS、内存、字符串、日志和摘要工具接口，不直接向业务暴露底层 TCP/TLS 细节。
- 支持一次性请求和可复用的持久 client handle 两种用法。
- 相比单一 `on_body`，通过 `CONNECTED / HEADER / DATA / FINISH / DISCONNECTED / ERROR / REDIRECT` 事件提供更细粒度的处理能力。

## 文件结构

```text
components/qm_http_client/
├── README.md
├── qm_kconfig
├── include/
│   ├── qm_http_client.h
│   ├── qm_http_client_log.h      # internal category-aware logging macros
│   └── qm_http_client_transport.h  # internal component header
└── src/
    ├── qm_http_client.c
    └── qm_http_client_transport.c
```

## 分层设计

```text
应用请求与回调
        v
 qm_http_client
        | URL、认证、响应解析
        v
 QUMI TCP/TLS transport
        v
      HTTP 服务
```

组件内部按三层划分：

- `client`：位于 `src/qm_http_client.c`，负责 URL 解析、请求拼装、响应解析、认证、重定向、事件派发和对外 API。
- `transport`：位于 `include/qm_http_client_transport.h` 与 `src/qm_http_client_transport.c`，负责把 `qm_tcp` / `qm_tls` 适配成组件内部统一的 connect/read/write/close 抽象。
- `hal`：由 qumi 现有底层组件提供，包括 `qm_tcp`、`qm_tls`、`qm_kernel`、`qm_string`、摘要工具等。

这样的分层使对外 API 不直接暴露 TCP/TLS 细节；当底层连接实现变化时，通常只需要调整 transport 适配层，而不需要改动 HTTP 协议状态机。

## 依赖边界

组件内部依赖：

| 依赖 | 用途 |
| --- | --- |
| `qm_kernel.h` | 内存分配、基础运行时接口 |
| `qm_log.h` | 日志输出 |
| `qm_string.h` | 字符串解析与格式化 |
| `qm_tcp.h` | `http://` TCP transport |
| `qm_tls.h` | `https://` TLS transport |
| `qm_utils_base64.h` | Basic 认证编码 |
| `qm_utils_md5.h` | Digest 认证摘要 |
| `qm_utils_sha256.h` | Digest 认证摘要 |

公共头文件只暴露 `qm_errno.h`、`qm_config.h`、`qm_types.h` 和组件自身类型。`qm_tcp.h`、`qm_tls.h` 与 transport 细节不会泄漏到业务层。

## 功能特性

可选的 `interrupted(user)` 回调提供协作式取消。客户端在每次传输收发前后检查它，
包括响应头和 chunk 解析循环；取消时返回 `-QM_EINTR`。回调必须非阻塞，读取调用方
同步保护的取消状态，并在请求返回前保持该状态。请求取消的任务不得并发关闭或释放
客户端；HTTP 调用任务在当前调用返回后执行清理。`timeout_ms` 限定单次传输收发等待，
DNS、TCP 连接和 TLS 握手仍遵循 HAL 实现的时限，不能靠该回调抢占已进入的建连调用。

- 支持一次性接口 `qm_http_client_execute()`
- 支持持久 handle 接口：`init -> open -> write -> fetch_headers -> read/drain_response_body -> close/cleanup`
- 支持请求体流式写入：手动 `open + write`，或 `body_cb` 按块提供请求体
- 支持 keep-alive 连接复用
- 支持自动 Digest 401 重试
- 支持 301/302/303/307/308 自动重定向
- `303` 以及 `POST + 301/302` 会自动切换为 `GET`
- 支持事件回调和旧版 body callback 同时使用

## URL 解析规则

- 支持标准 `http://` 和 `https://` URL
- 支持只有 query 的 URL，例如 `http://host?x=1`，请求路径会按 `/?x=1` 发送
- 支持 IPv6 host，但必须使用方括号形式，例如 `http://[2001:db8::1]:8080/path`
- 当前不支持 `userinfo`，例如 `http://user:pass@host/path` 会直接返回 `-QM_EINVAL`
- 如果 host 或 path 超出组件内部缓冲区上限，会明确返回 `-QM_EFULL`
- `qm_http_client_set_url()` 会在保存前执行同一套 URL 解析校验；失败时保留旧 URL

## 配置项

`qm_kconfig` 提供以下默认配置：

| 配置项 | 说明 |
| --- | --- |
| `QM_HTTP_CLIENT_TIMEOUT_MS` | 默认 connect/read/write 超时 |
| `QM_HTTP_CLIENT_HEADER_MAX` | 最大响应头长度 |
| `QM_HTTP_CLIENT_BODY_MAX` | 最大响应体长度 |
| `QM_HTTP_CLIENT_RX_BUF_SIZE` | 流式读取缓冲区大小 |
| `QM_HTTP_CLIENT_LINE_MAX` | `chunked` 行解析缓冲区大小 |
| `QM_HTTP_CLIENT_REDIRECT_MAX` | 自动重定向默认次数 |
| `QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986` | 是否启用 RFC 3986 风格的重定向 URL 解析，默认关闭 |
| `QM_HTTP_CLIENT_AUTH_RETRY_MAX` | Digest 401 自动重试默认次数 |

日志配置：`QM_HTTP_CLIENT_LOG_CONFIG` 是日志分类默认值的总开关。关闭时默认只输出错误和生命周期日志；打开后可分别选择 `QM_HTTP_CLIENT_LOG_ERROR`、`QM_HTTP_CLIENT_LOG_LIFECYCLE`、`QM_HTTP_CLIENT_LOG_CONTROL`、`QM_HTTP_CLIENT_LOG_UPLINK`、`QM_HTTP_CLIENT_LOG_DOWNLINK`、`QM_HTTP_CLIENT_LOG_PAYLOAD` 和 `QM_HTTP_CLIENT_LOG_STATS`。

运行时日志掩码是组件全局配置，不会修改 TCP、TLS 或 QUMI 全局日志：

```c
qm_http_client_set_log_flags(QM_HTTP_CLIENT_LOG_ERROR |
                             QM_HTTP_CLIENT_LOG_LIFECYCLE |
                             QM_HTTP_CLIENT_LOG_DOWNLINK);
```

调用 `qm_http_client_set_log_flags(QM_HTTP_CLIENT_LOG_NONE)` 可关闭全部 `qm_http_client` 日志。

在 `qm_http_client_config_t` 和 `qm_http_client_request_t` 中，对应字段传 `0` 时会回退到这些默认值。

如果某些工程没有生成对应的 `CONFIG_QM_HTTP_CLIENT_*` 宏，组件内部也会使用同样的编译期 fallback 默认值，避免因为缺少配置宏而直接编译失败。

## 两种使用方式

### 一次性请求

适合单次 HTTP 请求：

1. 填写 `qm_http_client_request_t`
2. 可选设置 `on_event`
3. 可选设置 `on_body`
4. 调用 `qm_http_client_execute()`
5. 从 `qm_http_client_response_t` 读取状态码、长度、是否 chunked

示例：

```c
static int http_event_cb(const qm_http_client_event_t *event)
{
    switch (event->event_id) {
        case QM_HTTP_CLIENT_EVENT_HEADER:
            QM_LOGI("http", "%s: %s",
                    event->header_name ? event->header_name : "",
                    event->header_value ? event->header_value : "");
            break;
        case QM_HTTP_CLIENT_EVENT_DATA:
            QM_LOGI("http", "data len=%d", event->data_len);
            break;
        default:
            break;
    }
    return QM_EOK;
}

qm_http_client_request_t req = {0};
qm_http_client_response_t resp = {0};

req.method = QM_HTTP_GET;
req.url = "http://example.com/";
req.user_agent = "my-product/1.0";
req.timeout_ms = CONFIG_QM_HTTP_CLIENT_TIMEOUT_MS;
req.max_header_len = CONFIG_QM_HTTP_CLIENT_HEADER_MAX;
req.max_body_len = CONFIG_QM_HTTP_CLIENT_BODY_MAX;
req.rx_buf_size = CONFIG_QM_HTTP_CLIENT_RX_BUF_SIZE;
req.on_event = http_event_cb;

qm_http_client_execute(&req, &resp);
```

流式请求体上传：

```c
typedef struct {
    const char *data;
    int total_len;
    int offset;
} upload_ctx_t;

static int upload_body_cb(char *buffer, int max_len, void *user)
{
    upload_ctx_t *ctx = (upload_ctx_t *)user;
    int left = ctx->total_len - ctx->offset;
    int n = left < max_len ? left : max_len;

    if (n <= 0) return 0;
    qm_memcpy(buffer, ctx->data + ctx->offset, n);
    ctx->offset += n;
    return n;
}

static int upload_body_reset(void *user)
{
    upload_ctx_t *ctx = (upload_ctx_t *)user;
    ctx->offset = 0;
    return QM_EOK;
}

upload_ctx_t ctx = {
    .data = big_json,
    .total_len = big_json_len,
    .offset = 0,
};

qm_http_client_request_t req = {0};
qm_http_client_response_t resp = {0};

req.method = QM_HTTP_POST;
req.url = "https://example.com/upload";
req.content_type = "application/json";
req.body_len = ctx.total_len;
req.body_cb = upload_body_cb;
req.body_reset = upload_body_reset;
req.user = &ctx;

qm_http_client_execute(&req, &resp);
```

说明：

- `body` 和 `body_cb` 二选一，不能同时设置
- `body_len` 始终是总请求体长度
- `body_cb` 适合 OTA、文件上传、大 JSON
- 当前不支持请求端 `chunked` 上传，因此流式 body 仍要求总长度已知
- 如果自动重定向或 Digest 认证重试需要重放请求体，组件会调用 `body_reset`
- 只要请求没有手动设置 `Content-Length`，组件就会自动补这个头；即使 `body_len == 0` 也会发送 `Content-Length: 0`
- 可以通过 `req.user_agent` 自定义 `User-Agent`
- 持久 handle 场景可以直接用 `qm_http_client_set_header(client, "User-Agent", "...")`
- 如果显式添加了 `User-Agent` 请求头，则优先使用显式头值
- `Host`、`Connection`、`Content-Length` 由组件维护，用户 header 不允许覆盖或删除，避免请求头与内部状态不一致

### 持久 client handle

适合多次请求、需要手动控制发送和读取流程，或需要连接复用：

1. 调用 `qm_http_client_init()`
2. 用 `set_url/set_method/set_header/...` 配置请求
3. 调用 `qm_http_client_open()`
4. 如有请求体，调用 `qm_http_client_write()`
5. 调用 `qm_http_client_fetch_headers()`
6. 循环调用 `qm_http_client_read()`
7. 调用 `qm_http_client_close()` 或直接继续下一次请求
8. 结束后调用 `qm_http_client_cleanup()`

如果请求体很大，不需要一次性放进内存，可以直接使用：

```c
qm_http_client_open(client, total_len);

while ((n = file_read(fd, buf, sizeof(buf))) > 0) {
    qm_http_client_write(client, buf, n);
}
```

如果你已经拿到了状态码或响应头，但不需要继续处理 body，可以直接丢弃剩余响应体：

```c
qm_http_client_fetch_headers(client);
qm_http_client_drain_response_body(client);
```

这样在服务端允许 keep-alive 时，连接仍有机会被下一次请求复用。

## 事件模型

相较只拿 body 的 `on_body`，`on_event` 可以感知完整请求生命周期。

事件类型：

| 事件 | 说明 |
| --- | --- |
| `QM_HTTP_CLIENT_EVENT_CONNECTED` | transport 已建立连接 |
| `QM_HTTP_CLIENT_EVENT_HEADER` | 收到一行响应头 |
| `QM_HTTP_CLIENT_EVENT_DATA` | 收到响应体片段 |
| `QM_HTTP_CLIENT_EVENT_FINISH` | 响应体接收完成 |
| `QM_HTTP_CLIENT_EVENT_DISCONNECTED` | transport 已关闭 |
| `QM_HTTP_CLIENT_EVENT_ERROR` | 请求处理失败 |
| `QM_HTTP_CLIENT_EVENT_REDIRECT` | 收到带 `Location` 的重定向响应 |

`qm_http_client_event_t` 关键字段：

| 字段 | 说明 |
| --- | --- |
| `client` | 当前 client handle |
| `user` | 用户上下文 |
| `status_code` | 当前 HTTP 状态码 |
| `data` / `data_len` | `DATA` 事件的 body 片段，或 `HEADER` 事件的原始行 |
| `header_name` / `header_value` | `HEADER` 事件的头字段 |
| `error_code` | `ERROR` 事件的错误码 |
| `redirect_url` | `REDIRECT` 事件的目标 URL |

注意：

- 事件对象以及其中的字符串、数据缓冲区只在回调期间有效。
- 如果同时设置了 `on_event` 和 `on_body`，同一个响应片段会先触发 `DATA` 事件，再进入 `on_body`。
- `FINISH` 在 body 完整接收后只派发一次。

## `on_event` 与 `on_body`

推荐新代码优先使用 `on_event`：

- `on_event` 适合做状态机、调试、统计、重定向、头解析和错误处理
- `on_body` 适合兼容旧代码，只关注 body 流数据

如果你只需要 body，可以继续使用 `on_body`。如果你还需要：

- 连接建立时机
- 响应头
- 重定向 URL
- 完成通知
- 断连通知
- 错误原因

那就应该使用 `on_event`。

## 认证与重定向

认证支持：

- `QM_HTTP_AUTH_TYPE_NONE`
- `QM_HTTP_AUTH_TYPE_BASIC`
- `QM_HTTP_AUTH_TYPE_DIGEST`

Digest 行为：

- 401 响应中解析 `WWW-Authenticate: Digest ...`
- 自动缓存 challenge
- 自动重试带 `Authorization` 的请求，默认次数由 `QM_HTTP_CLIENT_AUTH_RETRY_MAX` 控制

重定向行为：

- 默认启用自动重定向
- `max_redirection_count` 为 0 时使用组件默认值 `QM_HTTP_CLIENT_REDIRECT_MAX`
- 设置 `disable_auto_redirect = QM_TRUE` 可关闭自动重定向
- `qm_http_client_request_t` 和 `qm_http_client_config_t` 都支持这两个重定向策略字段
- 即使启用自动重定向，仍然会先派发 `REDIRECT` 事件
- `Location` 支持绝对 URL、`//host/path`、`/path`、相对路径、`?query` 和 `#fragment`；
  自动重定向会按 RFC 3986 规则归并 `.` / `..` path segment，fragment 不会作为 HTTP 请求路径发送
- `QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986` 默认关闭，关闭时使用较简单的相对路径拼接逻辑；需要更完整的
  RFC 3986 相对引用处理时再开启

补充说明：

- `qm_http_client_get_location()` 返回的 `location` 只在持久 handle 的下一次请求前有效。
- 在 `qm_http_client_execute()` 一次性接口中，内部 client 会在返回前销毁，因此 `resp.location` 不保留；如需 redirect URL，请在 `REDIRECT` 事件中处理。

## TLS

默认 transport 行为：

- `http://` 使用 `qm_tcp_establish()` / `qm_tcp_read()` / `qm_tcp_write()` / `qm_tcp_destroy()`
- `https://` 使用 `qm_tls_establish()` / `qm_tls_read()` / `qm_tls_write()` / `qm_tls_destroy()`

单向 TLS：

```c
qm_http_tls_config_t tls;

/* Use the transport/platform default trust source. */
qm_http_client_tls_config_init(&tls,
                               NULL, 0,
                               NULL, 0,
                               NULL, 0);

/* Or use a caller-provided CA certificate. */
qm_http_client_tls_config_init(&tls,
                               ca_cert, strlen(ca_cert),
                               NULL, 0,
                               NULL, 0);
```

双向 TLS：

```c
qm_http_tls_config_t tls;

/* ca_cert may also be NULL to use the transport/platform default trust source. */
qm_http_client_tls_config_init(&tls,
                               ca_cert, strlen(ca_cert),
                               client_cert, strlen(client_cert),
                               client_key, strlen(client_key));
```

说明：

- `ca_crt == NULL` 是合法配置，表示使用 transport/platform 默认信任源。
- 单向 TLS 只配置服务端证书校验来源，不配置 `client_crt` / `client_key`。
- 双向 TLS 必须同时配置 `client_crt` 和 `client_key`；二者只传一个会返回 `-QM_EINVAL`。
- `qm_http_client_init()` 和 `qm_http_client_execute()` 只复制 `tls` 结构体字段，不复制证书内容。
- 证书 buffer 需要在请求执行期间保持有效。
- 如果使用 `body_cb` 且服务器要求 Digest `auth-int`，当前不支持边上传边计算实体摘要，会返回 `-QM_ENOSYS`。

## 读取行为

响应体支持三种模式：

- 固定 `Content-Length`
- `Transfer-Encoding: chunked`
- 无长度、读到连接关闭为止

`rx_buf_size` 和 `max_body_len` 的关系：

- `rx_buf_size` 控制单次内部读缓冲和单次 `DATA` / `on_body` 回调大小，直接影响临时 RAM 占用。
- `max_body_len` 是响应体长度保护上限，不表示一次性申请这么大的内存。
- `Transfer-Encoding: chunked` 会按当前读取缓冲分段回调给应用层处理，不要求应用层一次持有完整响应体。

接口返回约定：

| 接口 | 成功返回 | 失败返回 |
| --- | --- | --- |
| `qm_http_client_open()` | `QM_EOK` | 负错误码 |
| `qm_http_client_write()` | 正数写入字节数 | 负错误码 |
| `qm_http_client_fetch_headers()` | `QM_EOK` | 负错误码 |
| `qm_http_client_read()` | 正数读取字节数，完成返回 `0` | 负错误码 |
| `qm_http_client_drain_response_body()` | `QM_EOK` | 负错误码 |
| `qm_http_client_perform()` | `QM_EOK` | 负错误码 |
| `qm_http_client_close()` | `QM_EOK` | 负错误码 |
| `qm_http_client_cleanup()` | `QM_EOK` | 负错误码 |
| `qm_http_client_execute()` | `QM_EOK` | 负错误码 |
| `qm_http_client_tls_config_init()` | `QM_EOK` | 负错误码 |

补充说明：

- `Content-Length` 现在使用 `uint32_t` 保存
- 通过 `resp.content_length_known` 或 `qm_http_client_has_content_length()` 判断是否存在该头
- 已知长度从 `resp.content_length` 或 `qm_http_client_get_content_length()` 读取

## 示例

组件通用示例：

```text
examples/qm_http_client/qm_http_client_example.c
```

OpenCPU 工程示例：

```text
apps/opencpu/app/qm_http_client_example.c
```

当前示例已经使用事件回调风格：

- 打印 `CONNECTED`
- 打印每一行 `HEADER`
- 在 `DATA` 中累计 body 长度
- 在 `FINISH` 中输出总长度
- 在 `ERROR` 中输出错误码与状态码
- 在 `REDIRECT` 中输出目标 URL
- 额外提供 `qm_http_client_resume_download_example_start()`，演示普通 HTTP 下载使用 `Range` 请求头断点续传，并校验 `206` / `Content-Range`
- 额外提供 `qm_http_client_wifi_demo_start()`，演示 `qm_wifi` 初始化、联网、获取 IP 后再启动整套 HTTP client demo

## OpenCPU 编译

```sh
cd /Users/wells/Documents/project/esp32_combo
. ./export.sh
cd apps/opencpu
idf.py build
```

## 适用场景

适合以下场景：

- 设备通过 REST API 拉取配置
- 设备上报 JSON 到云端
- 需要 HTTP Basic/Digest 认证的网关/设备接口
- 需要通过事件回调分析响应头、跳转和 body 流数据
