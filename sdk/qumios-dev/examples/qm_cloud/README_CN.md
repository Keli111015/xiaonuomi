# qm_cloud 示例

该目录包含两个最小入网示例：

- `dynreg/`：BLE 配网 + productSecret 动态注册。
- `authcode/`：authCode 注册。

`dynreg` 示例会启动 BLE 配网，收到 WiFi 凭证后连接 WiFi，再获取云端认证信息并启动 `qm_iot`。`authcode` 示例保持最小流程，假设网络已经由应用或平台层完成连接。

如果示例 host 宏为空，dynreg 模块会根据 `CONFIG_QM_CLOUD_*_EXAMPLE_ENV` 自动选择默认 host。
