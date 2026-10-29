# Xjiang HTTP OTA Example

这个示例演示如何使用 `xjiang_http_ota` 通过 Wi-Fi 查询宁波小匠 OTA 版本，
并继续执行模组或 MCU 升级。

## 文件结构

```text
qumi/examples/xjiang_http_ota/
├── README.md
├── xjiang_http_ota_example.c
└── qm_config/
    └── qm_config.h
```

## 示例能力

- Wi-Fi STA 初始化与自动重连
- 版本查询：`xjiang_http_ota_check()`
- 模组升级：`xjiang_http_ota_perform(..., XJIANG_HTTP_OTA_TARGET_MODULE, ...)`
- MCU 升级：`xjiang_http_ota_set_mcu_ota()` + `xjiang_http_ota_perform(..., XJIANG_HTTP_OTA_TARGET_MCU, ...)`
- 动态字符串释放：`xjiang_http_ota_info_free()`

## 需要修改的宏

在编译前请至少修改以下宏：

- `EXAMPLE_WIFI_SSID`
- `EXAMPLE_WIFI_PASSWORD`
- `XJIANG_HTTP_OTA_EXAMPLE_PID`
- `XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MODULE_VERSION`
- `XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MCU_VERSION`
- `XJIANG_HTTP_OTA_EXAMPLE_AUTHORIZATION_TOKEN`

## 运行流程

1. 如有需要，先调用 `xjiang_http_ota_set_env()` 设置环境
2. 如接口需要鉴权，配置 `XJIANG_HTTP_OTA_EXAMPLE_AUTHORIZATION_TOKEN`
3. 调用 `xjiang_http_ota_wifi_demo_start()`
4. Wi-Fi STA 启动后自动连接 AP
5. 获取 IP 后自动开始版本查询和升级演示
6. 如果同时存在模组和 MCU 升级，示例默认优先升级模组
7. 如果只存在 MCU 升级，示例会注册 MCU OTA 回调并继续下载

## 接入方式

这个目录下的示例文件不会自动参与 `apps/opencpu` 的构建。
如果你想在工程中使用它，可以：

1. 将 `qumi/examples/xjiang_http_ota/xjiang_http_ota_example.c` 加入你的工程源码列表
2. 将 `qumi/examples/xjiang_http_ota/qm_config/qm_config.h` 中相关配置合并到你的工程配置头
3. 在业务入口中调用 `xjiang_http_ota_wifi_demo_start()`

## 日志观察点

建议重点关注这些日志：

- Wi-Fi 连接/重连状态
- `xjiang_http_ota_check()` 返回值和 `status_code`
- module / mcu 的可升级信息
- `xjiang_http_ota_perform()` 返回值
- MCU OTA 示例回调中的 `begin` / `write` / `finish` / `abort`

## MCU 接入说明

示例里的 MCU OTA 回调只是一个最小演示：

- `begin()` 记录总长度和断点位置
- `write()` 记录收到的数据偏移和长度
- `finish()` 标记完成
- `abort()` 记录失败

实际产品接入时，需要把 `write()` 里的逻辑替换成真实的 MCU 固件写入流程，
例如串口透传、I2C/SPI 下发、分包缓存或落盘。
