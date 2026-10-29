# QM HTTP Client 示例

英文文档：[README.md](README.md)

这个示例演示如何在 `qumi/examples` 目录下使用 `qm_http_client`。

## 文件结构

```text
qumi/examples/qm_http_client/
├── README.md
├── README_CN.md
├── qm_http_client_example.c
└── qm_config/
    └── qm_config.h
```

## 示例能力

- Wi-Fi STA 初始化与自动重连
- 普通 GET 请求
- POST、PUT、DELETE、HEAD 请求
- 流式请求体上传
- handle 方式的手动执行流程
- 手动读取响应 body
- Range 下载和类似断点续传的下载流程
- 重定向处理
- HTTPS 单向认证和可选自定义 CA
- HTTPS 双向认证
- Basic 和 Digest 认证示例

## 常用宏

```c
#define EXAMPLE_WIFI_SSID     "your_ssid"
#define EXAMPLE_WIFI_PASSWORD "your_password"
```

测试 URL 如 `QM_HTTP_CLIENT_GET_URL`、`QM_HTTP_CLIENT_POST_URL`、
`QM_HTTP_CLIENT_HTTPS_URL`、`QM_HTTP_CLIENT_DIGEST_URL` 等定义在
`qm_http_client_example.c` 中。

## 运行入口

调用 `qm_http_client_wifi_demo_start()` 会先连接 Wi-Fi，再运行完整示例。
也可以单独调用某个流程：

```c
qm_http_client_example_start();
qm_http_client_post_example_start();
qm_http_client_https_example_start();
qm_http_client_digest_example_start();
```

## 接入方式

示例位于 `qumi/examples/qm_http_client/`。接入时将
`qumi/examples/qm_http_client/qm_http_client_example.c` 加入工程源码列表，并将
`qumi/examples/qm_http_client/qm_config/qm_config.h` 中相关配置合并到项目配置头。
