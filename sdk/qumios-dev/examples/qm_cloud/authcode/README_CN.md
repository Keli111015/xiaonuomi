# qm_cloud authCode 注册示例

本示例展示 authCode 注册入网的最小流程。

在 `qm_config/qm_config.h` 中填写：

- `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_PRODUCT_ID`
- `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_AUTHCODE`
- `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_SN`
- `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_ENV`

如果 `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_HOST` 为空，dynreg 模块会按环境自动选择 authCode 默认 host。

示例假设网络已经由外部完成连接。authCode 注册成功后，示例会启动 `qm_iot` 并调用 `qm_iot_network_update(..., QM_IOT_NETWORK_UP)`。
