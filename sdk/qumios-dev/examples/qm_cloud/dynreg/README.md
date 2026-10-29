# qm_cloud Dynamic Registration Example

This example shows cloud onboarding with BLE provisioning and productSecret dynamic registration.

Fill these macros in `qm_config/qm_config.h`:

- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_ID`
- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_PRODUCT_SECRET`
- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_SN`
- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ENV`
- `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_ADV_CID`

If `CONFIG_QM_CLOUD_DYNREG_EXAMPLE_HOST` is empty, the dynreg module selects the default host by environment.

The example starts BLE provisioning first. After it receives WiFi credentials, it connects WiFi, runs dynamic registration after `GOT_IP`, starts `qm_iot`, and calls `qm_iot_network_update(..., QM_IOT_NETWORK_UP)`.
