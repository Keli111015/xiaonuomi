# QM HTTP OTA 示例

英文文档：[README.md](README.md)

这个示例演示如何使用 `qm_http_ota` 通过 Wi-Fi 执行 HTTP/HTTPS OTA。

## 文件结构

```text
qumi/examples/qm_http_ota/
├── README.md
├── README_CN.md
├── qm_http_ota_example.c
└── qm_config/
    └── qm_config.h
```

## 示例能力

- Wi-Fi STA 初始化与自动重连
- 一键 OTA：`qm_http_ota()`
- 分阶段 OTA：`qm_http_ota_begin()` / `qm_http_ota_perform()` / `qm_http_ota_finish()`
- 断点信息查询：`qm_http_ota_resume_info_get()`
- 断点信息清理：`qm_http_ota_resume_info_clear()`
- 可选 HMAC-SHA256 文件校验
- MCU OTA 回调路径，业务层负责真实 MCU 存储或传输

## 需要修改的宏

在编译前请至少修改以下宏：

- `EXAMPLE_WIFI_SSID`
- `EXAMPLE_WIFI_PASSWORD`
- `QM_HTTP_OTA_EXAMPLE_URL`
- `QM_HTTP_OTA_EXAMPLE_EXPECTED_TOTAL_SIZE`：固件完整长度，必须与实际文件大小一致

可选宏：

- `QM_HTTP_OTA_EXAMPLE_IF_RANGE`
- `QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP`
- `QM_HTTP_OTA_EXAMPLE_PRODUCT_SECRET`
- `QM_HTTP_OTA_EXAMPLE_EXPECTED_HMAC_SHA256`
- `QM_HTTP_OTA_EXAMPLE_MCU_URL`
- `QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE`

## 运行流程

1. 调用 `qm_http_ota_wifi_demo_start()`
2. Wi-Fi STA 启动后自动连接 AP
3. 获取 IP 后自动开始 OTA 示例
4. 下载过程中输出进度日志
5. 下载完成后调用 `qm_http_ota_finish()`
6. 默认不自动重启，由业务自行决定是否调用 `qm_reboot()`

## 接入方式

示例位于 `qumi/examples/qm_http_ota/`。接入时将
`qumi/examples/qm_http_ota/qm_http_ota_example.c` 加入工程源码列表，并将
`qumi/examples/qm_http_ota/qm_config/qm_config.h` 中相关配置合并到项目配置头。
