# QM HTTP Server 示例

英文文档：[README.md](README.md)

这个示例演示如何在 `qumi/examples` 目录下使用 `qm_http_server`。

## 文件结构

```text
qumi/examples/qm_http_server/
├── README.md
├── README_CN.md
├── qm_http_server_example.c
└── qm_config/
    └── qm_config.h
```

## 示例能力

- Wi-Fi STA 初始化与自动重连
- HTTP Server 启动和停止
- 注册 GET、POST、PUT、DELETE、HEAD 和通配路径
- 解析 query 参数
- 读取请求 header
- 一次性读取 body 和流式读取 body
- 普通响应、分块响应和自定义响应头
- 可通过 `QM_HTTP_SERVER_EXAMPLE_USE_HTTPS` 打开 HTTPS 路径

## 常用宏

```c
#define EXAMPLE_WIFI_SSID     "your_ssid"
#define EXAMPLE_WIFI_PASSWORD "your_password"
```

如果要启用 HTTPS，需要将 `QM_HTTP_SERVER_EXAMPLE_USE_HTTPS` 设为 `1`，并由平台提供示例中使用的证书和私钥符号。

## 运行入口

```c
qm_http_server_wifi_demo_start();
```

设备获取 IP 后会启动 HTTP Server，并在日志中打印路由处理情况。

## 接入方式

示例位于 `qumi/examples/qm_http_server/`。接入时将
`qumi/examples/qm_http_server/qm_http_server_example.c` 加入工程源码列表，并将
`qumi/examples/qm_http_server/qm_config/qm_config.h` 中相关配置合并到项目配置头。
