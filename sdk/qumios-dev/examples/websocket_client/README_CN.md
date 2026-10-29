# QM WebSocket Client 示例

英文文档：[README.md](README.md)

这个示例演示如何在现有 `qumi/examples/websocket_client/` 示例目录下使用
`qm_websocket_client`。

## 文件结构

```text
qumi/examples/websocket_client/
├── README.md
├── README_CN.md
└── websocket_client_example.c
```

## 示例能力

- Wi-Fi STA 初始化与自动重连
- 通过 URI 创建 WebSocket client
- 通过 host、port、path、SSL 标志创建 WebSocket client
- 发送文本帧、二进制帧和分片帧
- 重连和优雅关闭示例
- TLS 服务端认证和双向认证入口

## 常用宏

```c
#define EXAMPLE_WIFI_SSID       "your_ssid"
#define EXAMPLE_WIFI_PASSWORD   "your_password"
#define EXAMPLE_WEBSOCKET_URI   "ws://echo.websocket.events"
#define EXAMPLE_WEBSOCKET_HOST  "echo.websocket.events"
#define EXAMPLE_WEBSOCKET_PORT  80
#define EXAMPLE_WEBSOCKET_PATH  "/"
#define EXAMPLE_WEBSOCKET_SSL   QM_FALSE
```

TLS 示例会使用 `EXAMPLE_TLS_CA_CERT`、`EXAMPLE_TLS_CLIENT_CERT` 和
`EXAMPLE_TLS_CLIENT_KEY`。

## 运行入口

```c
qm_application_start();
websocket_client_example_start(EXAMPLE_WEBSOCKET_URI);
websocket_client_example_stop(handle);
```

`qm_application_start()` 会先连接 Wi-Fi，获取 IP 后再启动 WebSocket client。

## 接入方式

示例位于 `qumi/examples/websocket_client/`。接入时将
`qumi/examples/websocket_client/websocket_client_example.c` 加入工程源码列表，并在项目配置中打开
`qm_websocket_client`、Wi-Fi 和事件队列相关依赖。
