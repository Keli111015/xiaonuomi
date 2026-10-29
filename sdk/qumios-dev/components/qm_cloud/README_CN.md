# QM IoT Cloud SDK

面向嵌入式设备的 **C 语言物联网接入组件**，负责与云端建立连接、属性上报/下发、设备 Topic、OTA 等基础能力。动态注册通过模块 API 由应用显式调用，不再耦合到 `qm_iot_api` 内部流程。SDK 通过 `qm.h` / `qm_spec_api.h` 等平台抽象层与底层 OS、TLS 等对接，网络连接和配网流程由应用或独立网络组件负责。

**声明版本**：`CONFIG_QM_IOT_SDK_VESRION`（见 `config/qm_iot_config.h`，当前为 `1.3.1`）。

## 架构

```text
应用层
    | 凭据、联网状态、回调
    v
qm_cloud 公共 API
    | MQTT、属性、设备 Topic、动态注册、OTA
    v
QUMI TCP/TLS 与 OS 抽象
    v
云端服务
```

组件不负责 Wi-Fi、蜂窝网络、以太网配网或产品级凭据策略；应用应先完成联网，再通知云会话网络状态。

## 目录结构

| 目录 | 说明 |
|------|------|
| `api/` | 对外主 API：`qm_iot_init`、`qm_iot_setopt`、`qm_iot_start` / `qm_iot_stop`、属性上报、设备 Topic 转调、事件通知等（`qm_iot_api.h`） |
| `priv_include/` | 内部头文件，不对应用直接暴露 |
| `common/` | Topic 格式、JSON 键名、日志 level/type 等公共常量（`qm_iot_common.h`） |
| `config/` | 功能开关、缓冲区长度、PID/DID/证书等持久化配置的 getter/setter（均需传入 `iot_handle`） |
| `mqtt/` | MQTT 传输相关实现 |
| `pub/` | 属性发布相关逻辑 |
| `dynreg/` | 动态注册 |
| `dev/` | 设备 Topic（`device/up`、`device/down`）、devInfo 上报、结构化日志上报等 |
| `ntp/` | 云端时间同步子功能，应用直接创建和请求 |
| `weather/` | 云端天气子功能，应用直接创建和请求 |
| `shadow/` | 设备影子 |
| `subdev/` | 子设备 |
| `ota/` | OTA（含 Jobs、文件流与加解密抽象；CBOR 使用公共 `components/tinycbor`） |

## 依赖与集成前提

- 工程需能包含 **`qm.h`**、**`qm_spec_api.h`** 等 Spec 与平台头文件，并提供 `qm_free` 等内存约定（属性上报接口说明调用后由 SDK 释放 `property_operation` 相关资源）。
- 应用侧负责 WiFi、4G、以太网、配网等网络流程；联网成功后调用 `qm_iot_network_update(handle, QM_IOT_NETWORK_UP)`，断网时调用 `qm_iot_network_update(handle, QM_IOT_NETWORK_DOWN)`。
- 动态注册由应用直接调用 `qm_iot_dynreg_*` 模块 API；内部使用 productSecret + HMAC-SHA1 签名注册。若应用未通过 `QM_IOT_DYNREGOPT_HOST` 显式配置 host，dynreg 会按当前环境（`qm_iot_dynreg_env_set` / `qm_iot_dynreg_env_get`，默认正式环境）选择内置默认 host。拿到 DID、MQTT host、证书和 OTA public key 后，通过 `QM_IOT_OPT_CLOUD_CREDENTIAL` 配置给 `qm_iot_api`。设备信息由应用通过 `QM_IOT_OPT_DEVINFO` 填充结构体，JSON 组包与 MQTT 上报由 dev 模块内部完成。
- NTP、Weather 等子功能需在 `QM_IOT_EVENT_CLOUD_CONN` 后通过 **`qm_iot_get_mqtt_handle`** 获取 MQTT 句柄，自行创建对应子模块实例并请求。

## 功能开关（节选）

在 `config/qm_iot_config.h` 或 `qm_kconfig` 中通过宏裁剪能力：

- **`CONFIG_QM_IOT_SPEC_SUPPORT`**：QM 标准 Spec（属性 set/get 请求与响应等扩展 `recv` 类型）。
- **`CONFIG_QM_IOT_MQTT_SUPPORT`** / **`CONFIG_QM_IOT_OTA_SUPPORT`** / **`CONFIG_QM_IOT_DYNREG_SUPPORT`** 等：对应子模块是否编译。
- **`CONFIG_QM_IOT_DEVMNT_SUPPORT`** / **`CONFIG_QM_IOT_DEV_INFO_SUPPORT`**：设备 Topic 管理与 devInfo MQTT 上报。
- **`CONFIG_QM_IOT_LOG_PUSH_SUPPORT`**：结构化日志上报（`qm_iot_log_push` / `qm_iot_log_push_raw`，默认开启）。
- **`CONFIG_QM_IOT_NTP_SUPPORT`** / **`CONFIG_QM_IOT_WEATHER_SUPPORT`**：保留云端时间同步、天气子功能编译；应用直接调用 `qm_iot_ntp_*` / `qm_iot_weather_*`，`qm_iot_api` 不自动创建或请求。
- **`CONFIG_QM_IOT_EVENT_LISTENER_NUM`**：`qm_iot_event_register` 可追加注册的事件回调槽位数（默认 4）。

具体默认值与长度限制请直接查阅 `config/qm_iot_config.h` 与 `qm_kconfig`。

## 主流程（概念）

1. **`qm_iot_init()`** 创建会话句柄。
2. **`qm_iot_setopt()`** 配置必选/可选项，例如：
   - `QM_IOT_OPT_VERSION_STR`、`QM_IOT_OPT_PRODUCT_ID`、`QM_IOT_OPT_PRODUCT_SECRET`、`QM_IOT_OPT_SN`
   - `QM_IOT_OPT_CLOUD_CREDENTIAL`
   - `QM_IOT_OPT_RECV_HANDLER`、`QM_IOT_OPT_EVENT_HANDLER`
   - `QM_IOT_OPT_USERDATA`
   - `QM_IOT_OPT_DEVINFO`
3. 可选配置设备信息快照，应用只填充 `qm_iot_devinfo_t` 结构体，协议 JSON 由 dev 模块生成并在连云后自动上报（`CONFIG_QM_IOT_DEV_INFO_SUPPORT`）。
4. 应用完成联网后按需直接调用 **`qm_iot_dynreg_start`** 获取云连接凭据；启动前先配置 **`QM_IOT_DYNREGOPT_PRODUCT_SECRET`** 等选项。如需切换默认服务器环境，先调用 **`qm_iot_dynreg_env_set`**。拿到凭据后通过 **`QM_IOT_OPT_CLOUD_CREDENTIAL`** 配置给 `qm_iot_api`。
5. **`qm_iot_start(handle)`** 启动云会话准备流程。
6. 应用调用 **`qm_iot_network_update(handle, QM_IOT_NETWORK_UP)`** 触发连云；断网时调用 **`qm_iot_network_update(handle, QM_IOT_NETWORK_DOWN)`**。
7. 在回调中处理 `qm_iot_recv_t`（云端下发）与 `qm_iot_event_t`（网络、连接、OTA 等），并使用 **`qm_iot_report` / `qm_iot_property_report`** 等上报属性（视 `CONFIG_QM_IOT_SPEC_SUPPORT` 而定）。
8. 可选：通过 **`qm_iot_up_report`** / **`qm_iot_down_register`** 经 device Topic 与云端交互；通过 **`qm_iot_log_push`** 上报结构化日志；通过 **`qm_iot_event_register`** 追加事件回调（与 `QM_IOT_OPT_EVENT_HANDLER` 单槽并存）。

事件与接收类型定义见 **`api/qm_iot_api.h`** 中的 `qm_iot_event_type_t`、`qm_iot_recv_type_t`。

## 说明

### 日志配置

`qm_cloud` 的本地日志由 `CONFIG_QM_CLOUD_LOG_CONFIG` 总开关控制。总开关关闭时只保留错误和生命周期日志；打开后，可分别配置 `ERROR`、`LIFECYCLE`、`CONTROL`、`UPLINK`、`DOWNLINK`、`PAYLOAD`、`AUTH`、`OTA`、`SUBDEV`、`SERVICE` 和 `STATS` 分类。运行时可通过 `qm_cloud_set_log_flags()` 修改全局 mask，通过 `qm_cloud_get_log_flags()` 读取当前 mask。

日志配置只影响 `qm_cloud` 自身，不会改变 `qm_http_client`、WebSocket 或其他组件的日志开关。payload、凭据、证书、密钥、签名和完整 MQTT topic 不应直接输出；调试日志只记录长度、状态、消息 ID 或脱敏后的摘要。

- **Spec 属性路径**：新协议建议走 `CONFIG_QM_IOT_SPEC_SUPPORT` 条件下的 `qm_iot_property_report` 及 `QM_IOT_RECV_TYPE_PROPERTY_*` 接收类型；`qm_iot_report` 为兼容接口。
- **OTA**：`QM_IOT_EVENT_OTA_READY` 的 `is_busy` 用于在升级前由应用侧声明是否可升级；具体策略以产品为准。
- **示例**：`examples/qm_cloud/` 提供 dynreg、authcode 等最小入网示例。

## 文档与细节

- **各版本改动摘要**：见 [`CHANGLOG.md`](./CHANGLOG.md)。

更完整的参数含义、结构体字段与回调约定以源码内 **Doxygen 风格注释** 为准，尤其是 `api/qm_iot_api.h`、`dev/qm_iot_dev.h` 与 `priv_include/qm_iot_session_internal.h`。
