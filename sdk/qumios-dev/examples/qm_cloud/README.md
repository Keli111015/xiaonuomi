# qm_cloud Examples

This folder contains two minimal cloud onboarding examples:

- `dynreg/`: BLE provisioning plus productSecret dynamic registration.
- `authcode/`: authCode registration.

The `dynreg` example starts BLE provisioning, connects WiFi with the received credentials, then requests cloud auth info and starts `qm_iot`. The `authcode` example keeps the minimal flow and assumes the network is already connected by the application or platform layer.

If the example host macro is empty, the dynreg module selects the default host from `CONFIG_QM_CLOUD_*_EXAMPLE_ENV`.
