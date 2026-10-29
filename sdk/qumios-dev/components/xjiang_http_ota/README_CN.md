# Xjiang HTTP OTA

English documentation: [README.md](README.md)

`xjiang_http_ota` 是一个对接宁波小匠 HTTP OTA 版本查询接口的轻量适配层。
业务层传入产品 `pid`、当前模组版本和 MCU 版本后，组件会发起版本查询：

```text
GET /agent-device/iot/ota/version?pid=<pid>&moduleVersion=<version>&mcuVersion=<version>
```

组件会把服务端返回的模组、MCU 固件信息转换成 `qm_http_ota` 可直接使用的结果。
如果同时存在模组和 MCU 升级，由业务层自行决定先升级哪个目标。

## 主要接口

- `xjiang_http_ota_check()`：根据 `pid`、模组版本、MCU 版本和可选 Authorization token 查询可升级信息。版本号按 `1.0.0` 这样的三段数字比较，只要服务端返回版本任意一段大于当前对应字段，就认为该目标可升级。返回结果中的字符串由组件动态分配，使用完成后调用 `xjiang_http_ota_info_free()` 释放。
- `xjiang_http_ota_perform()`：使用 `xjiang_http_ota_check()` 返回的结果执行升级。业务层通过 `target` 指定本次升级模组还是 MCU。可选输出结果为 `qm_http_ota_result_t`。
- `xjiang_http_ota_set_env()`：设置版本查询接口环境。未显式配置时，默认使用 `XJIANG_HTTP_OTA_ENV_PROD`。
- `xjiang_http_ota_set_mcu_ota()`：注册 MCU OTA 的业务回调。执行 `XJIANG_HTTP_OTA_TARGET_MCU` 升级时，组件负责下载固件，下载到的数据会通过 `write(user, offset, data, len)` 回调交给业务层。

默认不配置环境时走 `XJIANG_HTTP_OTA_ENV_PROD`。`check` 只负责查询和比较版本，不会立即升级；业务层可以在后续合适的时机继续调用 `xjiang_http_ota_perform()`，并传入 `XJIANG_HTTP_OTA_TARGET_MODULE` 或 `XJIANG_HTTP_OTA_TARGET_MCU` 决定本次升级目标。

执行 MCU 升级前，必须先通过 `xjiang_http_ota_set_mcu_ota()` 注册 `qm_http_ota_mcu_config_t`。MCU 固件如何写入下位机、串口透传、分包缓存、落盘等，都由业务回调负责。传入 `NULL` 可清除已注册的 MCU OTA 回调。

固件完整性校验由 `qm_http_ota` 负责，这个适配层仅保留小匠接口返回的 `sign` 字段，供业务层按需查看或继续透传。

## 示例

`qm` 目录下的独立示例位于 `qumi/examples/xjiang_http_ota/`：

```text
qumi/examples/xjiang_http_ota/
├── README.md
├── xjiang_http_ota_example.c
└── qm_config/
    └── qm_config.h
```

运行真实产品前，需要先配置这些编译期宏：

- `XJIANG_HTTP_OTA_EXAMPLE_PID`
- `XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MODULE_VERSION`
- `XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MCU_VERSION`
- `XJIANG_HTTP_OTA_EXAMPLE_AUTHORIZATION_TOKEN`

在执行版本查询或升级前，也可以先设置运行环境：

```c
xjiang_http_ota_set_env(XJIANG_HTTP_OTA_ENV_DEV);
xjiang_http_ota_check(pid, module_version, mcu_version, "Bearer your_token", &info);
```

如果只想查看版本查询结果，可调用 `xjiang_http_ota_example_check_start()`。
如果要演示完整升级流程，可调用 `xjiang_http_ota_example_start()`。

示例中的 MCU 路径已经注册了一组 `qm_http_ota_mcu_config_t` 示例回调，实际接入时重点替换 `write()` 中的业务写入逻辑，例如串口发包、I2C/SPI 透传或本地缓存。
