# QM Resource Manager 示例

英文文档：[README.md](README.md)

这个示例演示如何在 `qumi/examples` 目录下使用 `qm_res_mgr` 查询资源列表并下载资源文件。
组件元数据通道通过编译宏二选一：

- HTTP 列表/详情 + HTTP 文件下载
- BLE 配网 + dynreg 入云后的 MQTT 元数据查询

这里的二选一只针对资源元数据查询；资源文件下载仍然使用服务端返回的 HTTP `url`。

## 文件结构

```text
qumi/examples/qm_res_mgr/
├── README.md
├── README_CN.md
├── qm_kconfig
├── qm_res_mgr_example.c
└── qm_config/
    └── qm_config.h
```

## 示例能力

- 先连接 Wi-Fi，再调用资源管理接口
- 从资源列表接口拉取资源，并根据列表项中的 `url` 下载文件
- 选择 HTTP 元数据通道时，生成配置只定义
  `CONFIG_QM_RES_MGR_HTTP_SUPPORT`，不会定义
  `CONFIG_QM_RES_MGR_MQTT_SUPPORT`，此时保持原来的 HTTP 元数据流程
- 选择 MQTT 元数据通道时，生成配置只定义
  `CONFIG_QM_RES_MGR_MQTT_SUPPORT`，不会定义
  `CONFIG_QM_RES_MGR_HTTP_SUPPORT`，此时走 MQTT 元数据流程，联网时序与 `qm_cloud_dynreg_example`
  保持一致：BLE 配网 -> Wi-Fi -> dynreg -> `qm_iot` -> 云连接成功后拉资源列表
- `QM_RES_MGR_EXAMPLE_RESOURCE_ID` 为 `0` 时下载列表里的全部资源
- `QM_RES_MGR_EXAMPLE_RESOURCE_ID` 非 `0` 时只下载匹配资源
- `QM_RES_MGR_EXAMPLE_DIRECT_URL` 非空时跳过列表和详情接口，直接下载该 URL
- `on_data` 示例只统计下载字节数，产品中需要替换为保存文件、命中缓存和清理逻辑

## 主要配置

```text
CONFIG_QM_RES_MGR_SUPPORT=1
CONFIG_QM_HTTP_CLIENT_SUPPORT=1
CONFIG_QM_WIFI_SUPPORT=1
## 二选一：HTTP 元数据
CONFIG_QM_RES_MGR_HTTP_SUPPORT=y
# CONFIG_QM_RES_MGR_MQTT_SUPPORT is not set

## 或者二选一：MQTT 元数据
# CONFIG_QM_RES_MGR_HTTP_SUPPORT is not set
CONFIG_QM_RES_MGR_MQTT_SUPPORT=y
CONFIG_QM_RES_MGR_SEGMENT_SIZE=1048576
CONFIG_QM_RES_MGR_RETRY_MAX=3
CONFIG_QM_RES_MGR_PAGE_SIZE=10

## 仅 HTTP 元数据通道额外配置
CONFIG_QM_RES_MGR_TIMEOUT_MS=15000
CONFIG_QM_RES_MGR_HTTP_RX_BUF_SIZE=1024
CONFIG_QM_RES_MGR_HTTP_HEADER_MAX=2048
CONFIG_QM_RES_MGR_API_BODY_MAX=16384

## 当选择 MQTT 元数据通道时还需要：
CONFIG_QM_CLOUD_SUPPORT=1
CONFIG_QM_IOT_MQTT_SUPPORT=1
CONFIG_QM_IOT_DYNREG_SUPPORT=1
CONFIG_QM_IOT_DYNREG_SIGNATURE_SUPPORT=1
CONFIG_QM_PROVISION_SUPPORT=1
CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT=5000
```

## 示例宏

```c
#define EXAMPLE_WIFI_SSID                   "your-ssid"
#define EXAMPLE_WIFI_PASSWORD               "your-password"
#define QM_RES_MGR_EXAMPLE_TOKEN            "device-token"
#define QM_RES_MGR_EXAMPLE_DID              "device-did"
#define QM_RES_MGR_EXAMPLE_BUNDLE           "resource-bundle-name"
#define QM_RES_MGR_EXAMPLE_RESOURCE_ID      0
#define QM_RES_MGR_EXAMPLE_DIRECT_URL       ""
#define QM_RES_MGR_EXAMPLE_DIRECT_FILE_SIZE 0
#define QM_RES_MGR_EXAMPLE_ENV              QM_RES_MGR_ENV_DEV

#define CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID       0U
#define CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET   ""
#define CONFIG_QM_RES_MGR_EXAMPLE_VERSION          "1.0.0"
#define CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_HOST      ""
#define CONFIG_QM_RES_MGR_EXAMPLE_ADV_CID          ""
#define CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_ENV       QM_IOT_DYNREG_ENV_PROD
#define CONFIG_QM_RES_MGR_EXAMPLE_WIFI_RETRY_MAX   10U
#define CONFIG_QM_RES_MGR_EXAMPLE_MQTT_TIMEOUT_MS  15000U
```

选择 MQTT 元数据通道后：

- 元数据查询不再依赖 `QM_RES_MGR_EXAMPLE_TOKEN`
- 但 `QM_RES_MGR_EXAMPLE_DID` 和 `QM_RES_MGR_EXAMPLE_BUNDLE` 仍然必填
- 资源文件下载仍然使用服务端返回的 `url`，因此 HTTP 下载能力仍需保留
- 这个模式下 HTTP 超时 / 读缓冲 / header 相关 Kconfig 项会被隐藏，下载路径使用组件默认值

## 运行入口

示例入口为：

```c
app_main();
```

Wi-Fi 获取 IP 后，示例会自动调用 `qm_res_mgr_example_start()`。

MQTT 元数据模式下，示例会先启动 BLE 配网。收到 Wi-Fi 凭证并联网成功后，执行
dynreg、启动 `qm_iot`、上报 network up，等到 `QM_IOT_EVENT_CLOUD_CONN`
再触发 `qm_res_mgr_example_start()`。

## 接入方式

将 `qumi/examples/qm_res_mgr/qm_res_mgr_example.c` 加入工程源码列表，并将
`qumi/examples/qm_res_mgr/qm_config/qm_config.h` 中相关配置合并到项目配置头。
