# QM HTTP OTA Example

Chinese documentation: [README_CN.md](README_CN.md)

This example shows how to use `qm_http_ota` to perform HTTP/HTTPS OTA over Wi-Fi.

## Files

```text
qumi/examples/qm_http_ota/
├── README.md
├── README_CN.md
├── qm_http_ota_example.c
└── qm_config/
    └── qm_config.h
```

## Features

- Wi-Fi STA initialization and reconnect
- One-shot OTA with `qm_http_ota()`
- Manual OTA flow with `qm_http_ota_begin()`, `qm_http_ota_perform()`, and `qm_http_ota_finish()`
- Resume information query with `qm_http_ota_resume_info_get()`
- Resume information cleanup with `qm_http_ota_resume_info_clear()`
- Optional HMAC-SHA256 firmware verification
- MCU OTA callback path for business-owned MCU storage or transport

## Required Macros

Set these before running against a real firmware URL:

- `EXAMPLE_WIFI_SSID`
- `EXAMPLE_WIFI_PASSWORD`
- `QM_HTTP_OTA_EXAMPLE_URL`
- `QM_HTTP_OTA_EXAMPLE_EXPECTED_TOTAL_SIZE`

Optional macros:

- `QM_HTTP_OTA_EXAMPLE_IF_RANGE`
- `QM_HTTP_OTA_EXAMPLE_PROGRESS_STEP`
- `QM_HTTP_OTA_EXAMPLE_PRODUCT_SECRET`
- `QM_HTTP_OTA_EXAMPLE_EXPECTED_HMAC_SHA256`
- `QM_HTTP_OTA_EXAMPLE_MCU_URL`
- `QM_HTTP_OTA_EXAMPLE_MCU_MOCK_STORAGE_SIZE`

## Flow

1. Call `qm_http_ota_wifi_demo_start()`
2. The example connects to Wi-Fi
3. After IP acquisition, OTA starts automatically
4. Progress and OTA events are printed in logs
5. `qm_http_ota_finish()` is called after download completes
6. Auto restart is disabled by default; product code decides when to reboot

## Integration

This example lives under `qumi/examples/qm_http_ota/`. Add
`qumi/examples/qm_http_ota/qm_http_ota_example.c` to your project sources and
merge `qumi/examples/qm_http_ota/qm_config/qm_config.h` into your project
configuration.
