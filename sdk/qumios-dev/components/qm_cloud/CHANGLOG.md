# 更新改动点

本文件记录本组件各版本的功能与结构性改动摘要。细节以源码与提交记录为准。

---

## 1.3.1

- **版本号**：SDK 宏版本升为 `1.3.1`。
- **公共常量**：新增 `common/qm_iot_common.h`，集中 Topic 格式（`device/up`、`device/down`）、JSON 键名、日志 level/type 等常量，供 dev / pub / dynreg 等模块复用。
- **dev 预订阅时序**：`dev` 模块在 MQTT 句柄与 IOT 会话（pid/did）均就绪后再注册 `device/down` 等预订阅（`dev_mqtt_sub_if_ready`），避免凭据未配置时订阅失败。
- **内部头文件**：新增 `priv_include/qm_iot_dev_internal.h`（dev 层 device/up 回复等内部接口）；1.3.0 中提及的 `qm_iot_config_internal.h` 已合并进 `qm_iot_session_internal.h`。
- **日志上报默认开启**：`CONFIG_QM_IOT_LOG_PUSH_SUPPORT` 默认改为 `1`；可通过宏关闭或裁剪 `qm_iot_log_push` / `qm_iot_log_push_raw` 相关代码。
- **config 会话化（完善）**：`qm_iot_pid_get/set`、`qm_iot_did_get/set` 及证书、版本等 getter/setter 统一要求传入 `iot_handle`（`qm_iot_init` 返回值），与 1.3.0 会话模型一致。

---

## 1.3.0

- **版本号**：SDK 宏版本升为 `1.3.0`。
- **架构精简**：移除内置配网模块及 wifi / 4G / wifi_combo 网络适配文件；连云编排合并至 `api/qm_iot_api.c`；内部头文件迁至 `priv_include/`（`qm_iot_session_internal.h`）。
- **网络职责外移（不兼容）**：SDK 不再内置 WiFi / 4G / 配网流程；应用负责联网后调用 `qm_iot_network_update`；移除 `qm_iot_change_net_type` 及 SDK 内网络类型编译分支。
- **初始化 / setopt（不兼容）**：新增 `QM_IOT_OPT_CLOUD_CREDENTIAL`、`QM_IOT_OPT_SN`、`QM_IOT_OPT_DEVINFO`；移除内部动态注册入口；`qm_iot_devinfo_type_t` 重命名为 `qm_iot_network_type_t`；`qm_iot_wifi_devinfo_t` 新增可选 `ip` 字段。
- **动态注册（不兼容）**：应用直接调用 `qm_iot_dynreg_*` 模块 API 并传入 product_id / product_secret / sn / host / port；内部固定使用 productSecret + HMAC-SHA1 签名注册方式；JSON 组包与 HMAC 签名保留在 dynreg 模块，`qm_iot_api` 不再创建或启动 dynreg。
- **动态注册默认 host**：新增 `qm_iot_dynreg_env_set` / `qm_iot_dynreg_env_get` 切换开发、测试、正式环境；应用未配置 `QM_IOT_DYNREGOPT_HOST` 时，dynreg 按环境使用内置默认 host。
- **子功能拆分（不兼容）**：NTP、Weather 等子功能保留为独立模块，应用直接调用对应模块 API；`qm_iot_api` 不再自动创建或请求这些子功能。
- **设备信息**：devInfo JSON 组包与 MQTT 上报下沉至 `dev` 模块 `qm_iot_dev_send_devinfo`；新增 `CONFIG_QM_IOT_DEV_INFO_SUPPORT` 编译开关。
- **日志**：新增 `qm_iot_log_item_t` 及 `qm_iot_log_push` / `qm_iot_log_push_raw` 结构化日志上报接口（`CONFIG_QM_IOT_LOG_PUSH_SUPPORT`）。
- **配置**：任务参数、dynreg 端口 / 超时 / 重试、MQTT keepalive 等默认可在 `qm_iot_config.h` 覆盖；事件多槽注册 `qm_iot_event_register` / `unregister` 保留并与 `QM_IOT_OPT_EVENT_HANDLER` 单槽并存。

---

## 1.2.0

- **版本号**：SDK 宏版本升为 `1.2.0`。
- **初始化**：`qm_iot_init` 内提前创建 OTA/dev，失败路径补齐回收；事件注册检查返回值。
- **版本字符串（不兼容）**：`QM_IOT_OPT_VERSION_STR` + `qm_iot_version_str_*`，替代原先的数值版本选项。
- **MCU 多版本（可选宏）**：支持多路 MCU 版本存储与 OTA 上报扩展（`CONFIG_QM_IOT_OTA_MCU_SUPPORT`）。
- **OTA**：区分模组 / MCU 流程；支持云端协商分块大小；事件字段更完整；Jobs 解析 `fileType` 及可选 `version`/`extra`。
- **设备 Topic**：`device/up`、`device/down` 通用上报/回复/下行注册；内置云端重启通知与应答。
- **其它**：`qm_iot_change_net_type` 按入参切换网络类型；API 与公共头少量注释、JSON 键常量补充。

---

## 1.1.0

- **网络拆分**：网络相关逻辑按 wifi, wifi_combo, 4G 硬件匹配的单元拆分
- **service 服务**：支持set下发服务模块注册。

---

## 历史版本

- **first commit（`22852de`）**：初始代码基线。
