# qm_cloud authCode Registration Example

This example shows the minimal cloud onboarding flow using authCode registration.

Fill these macros in `qm_config/qm_config.h`:

- `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_PRODUCT_ID`
- `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_AUTHCODE`
- `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_SN`
- `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_ENV`

If `CONFIG_QM_CLOUD_AUTHCODE_EXAMPLE_HOST` is empty, the dynreg module selects the default authCode host by environment.

The example assumes the network is already connected. After registration it starts `qm_iot` and calls `qm_iot_network_update(..., QM_IOT_NETWORK_UP)`.
