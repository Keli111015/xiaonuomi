# QM HTTP Server

`qm_http_server` 提供基于 QUMI transport 的 HTTP/HTTPS server。上层 HTTP 代码只依赖 `qm_tcp`、`qm_tls`、`qm_kernel`、`qm_log` 等 QUMI 接口；IDF、mbedTLS、lwIP 等平台实现细节只能出现在平台 HAL 适配层中。

## 文件结构

## 架构

```text
HTTP 客户端
    v
QUMI TCP/TLS 字节流
    v
qm_http_server
    | 路由、请求解析、响应写入
    v
应用路由与错误 handler
```

```text
components/qm_http_server/
├── README.md
├── include/
│   ├── qm_http_server.h
│   └── qm_http_server_transport.h
├── qm_kconfig
└── src/
    ├── qm_http_server.c
    └── qm_http_server_transport.c
```

## 基本接口

典型流程：

1. 定义静态 `qm_http_route_t` 路由表。
2. 填充 `qm_http_server_config_t`。
3. 调用 `qm_http_server_create()` 创建 server。
4. 调用 `qm_http_server_start()` 开始监听。
5. 不再使用时调用 `qm_http_server_destroy()`。

最小配置只需要端口和静态路由表：

```c
static const qm_http_route_t routes[] = {
    {QM_HTTP_GET, "/api/v1/ping", ping_handler},
};

qm_http_server_config_t cfg;

qm_memset(&cfg, 0, sizeof(cfg));
cfg.port = 8080;
cfg.routes = routes;
cfg.route_count = sizeof(routes) / sizeof(routes[0]);
cfg.error_handler = error_handler;
```

`timeout_ms`、`max_header_len`、`max_body_len`、`task_stack`、`task_prio` 可以不填，组件会使用 `CONFIG_QM_HTTP_SERVER_*` 默认值。

示例也提供 `qm_http_server_wifi_demo_start()`：通过 `qm_wifi_init()` / `qm_wifi_set_mode()` / `qm_wifi_set_config()` / `qm_wifi_start()` 初始化 STA，收到 `QM_WIFI_EVENT_STA_GOT_IP` 后再启动 HTTP server；WiFi 断开时会停止 server 并按次数重连。

## 请求与响应

handler 通过 `qm_http_request_t` 获取请求元数据：

- `req->method`
- `req->path`
- `req->query`
- `req->uri`
- `req->content_length`

常用请求读取接口：

- `qm_http_query_get()`
- `qm_http_request_get_header()`
- `qm_http_request_read()`
- `qm_http_request_read_all()`

常用响应接口：

- `qm_http_response_send()`
- `qm_http_response_set_header()`
- `qm_http_response_begin_chunked()`
- `qm_http_response_write_chunk()`
- `qm_http_response_end_chunked()`

## HTTPS 配置

HTTPS 只需要在 HTTP server 配置里声明 scheme、证书和私钥：

```c
cfg.port = 443;
cfg.scheme = QM_HTTP_SERVER_SCHEME_HTTPS;
cfg.https_cert_pem = servercert_start;
cfg.https_cert_len = servercert_end - servercert_start;
cfg.https_key_pem = prvtkey_pem_start;
cfg.https_key_len = prvtkey_pem_end - prvtkey_pem_start;
```

当前服务端 TLS 依赖平台实现 `qm_tls_accept()`。没有实现该接口的平台会在 HTTPS accept 阶段失败，HTTP 模式不受影响。

## 示例

- `GET /api/v1/ping?id=123`
  - 演示 `req->query`、`qm_http_query_get()` 和 `qm_http_response_set_header()`
  - 返回 `{"ok":true}`
- `HEAD /api/v1/ping`
  - 演示 `QM_HTTP_HEAD` 和空 body 响应
- `POST /api/v1/echo`
  - 演示 `qm_http_request_get_header()` 和 `qm_http_request_read_all()`
  - 按请求 `Content-Type` 原样回显 body
- `POST /api/v1/upload`
  - 演示 `qm_http_request_read()` 流式读取请求 body
- `GET /api/v1/stream`
  - 演示 `qm_http_response_begin_chunked()` / `qm_http_response_write_chunk()` / `qm_http_response_end_chunked()`
- `PUT /api/v1/resource/*`
  - 演示 `QM_HTTP_PUT` 和 slash-star 通配路由
- `DELETE /api/v1/resource/*`
  - 演示 `QM_HTTP_DELETE` 复用同一个 handler
- `ANY /api/v1/any`
  - 演示 `QM_HTTP_ANY` 和 `req->method` / `req->uri` / `req->content_length`

未匹配路由、方法不匹配、请求过大等错误会进入 `qm_http_server_config_t.error_handler`。

示例文件：

- `apps/opencpu/app/qm_http_server_example.c`：opencpu 工程内同步版本，已加入当前工程构建。
- `examples/qm_http_server/qm_http_server_example.c`：独立示例源码。

独立示例不会自动参与 `apps/opencpu` 构建。接入工程时可以将示例源码加入工程源码列表，并将 `examples/qm_http_server/qm_config/qm_config.h` 中的 HTTP server 配置合并到工程配置头。
