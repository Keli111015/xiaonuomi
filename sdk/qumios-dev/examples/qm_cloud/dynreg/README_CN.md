# qm_cloud 动态注册示例

本示例展示 BLE 配网 + productSecret 动态注册入网流程。

在 `qm_config/qm_config.h` 中填写：

- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_ID`
- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_SECRET`
- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_SN`
- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ENV`
- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ADV_CID`

如果 `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_HOST` 为空，dynreg 模块会按环境自动选择默认 host。

示例会先启动 BLE 配网。收到 WiFi 凭证后连接 WiFi，在 `GOT_IP` 后执行动态注册，随后启动 `qm_iot` 并调用 `qm_iot_network_update(..., QM_IOT_NETWORK_UP)`。
