# QM Resource Manager Example

Chinese documentation: [README_CN.md](README_CN.md)

This example shows how to use `qm_res_mgr` from the `qumi/examples` tree.
The metadata channel is selected at compile time with exactly one option:

- HTTP list/detail + HTTP file download
- MQTT metadata query over `qm_iot` after BLE provisioning + dynreg onboarding

This one-of-two choice only applies to resource metadata query. File download
still uses the HTTP `url` returned by the service.

## Files

```text
qumi/examples/qm_res_mgr/
├── README.md
├── README_CN.md
├── qm_kconfig
├── qm_res_mgr_example.c
└── qm_config/
    └── qm_config.h
```

## Notes

- The example connects to Wi-Fi first, then calls the resource APIs.
- Default flow pulls a resource list and downloads matching items directly from
  the list item `url`.
- When HTTP metadata is selected, generated config keeps
  `CONFIG_QM_RES_MGR_HTTP_SUPPORT` defined and leaves
  `CONFIG_QM_RES_MGR_MQTT_SUPPORT` undefined; this keeps the original HTTP
  metadata flow.
- When MQTT metadata is selected, generated config keeps
  `CONFIG_QM_RES_MGR_MQTT_SUPPORT` defined and leaves
  `CONFIG_QM_RES_MGR_HTTP_SUPPORT` undefined; this switches metadata query to
  MQTT, and the networking path is aligned with
  `qm_cloud_dynreg_example`: BLE provisioning -> Wi-Fi -> dynreg -> `qm_iot`
  -> `QM_IOT_EVENT_CLOUD_CONN` -> `qm_res_mgr_get_list(... MQTT ...)`.
- When `QM_RES_MGR_EXAMPLE_RESOURCE_ID` is `0`, the example downloads every
  resource returned by the list API.
- When `QM_RES_MGR_EXAMPLE_RESOURCE_ID` is non-zero, the example only downloads
  the matching resource.
- `on_data` only counts bytes and prints progress. Product code should add file
  save, file-hit, and cleanup logic around the stream.
- If `QM_RES_MGR_EXAMPLE_DIRECT_URL` is configured, the example skips list and
  detail APIs and downloads that URL directly.

## Example Configuration

```text
CONFIG_QM_RES_MGR_SUPPORT=1
CONFIG_QM_HTTP_CLIENT_SUPPORT=1
CONFIG_QM_WIFI_SUPPORT=1
## Choose HTTP metadata
CONFIG_QM_RES_MGR_HTTP_SUPPORT=y
# CONFIG_QM_RES_MGR_MQTT_SUPPORT is not set

## Or choose MQTT metadata
# CONFIG_QM_RES_MGR_HTTP_SUPPORT is not set
CONFIG_QM_RES_MGR_MQTT_SUPPORT=y

## Optional runtime defaults from qm_config
CONFIG_QM_RES_MGR_SEGMENT_SIZE=1048576
CONFIG_QM_RES_MGR_RETRY_MAX=3
CONFIG_QM_RES_MGR_PAGE_SIZE=10
## Additional HTTP-only config
CONFIG_QM_RES_MGR_TIMEOUT_MS=15000
CONFIG_QM_RES_MGR_HTTP_RX_BUF_SIZE=1024
CONFIG_QM_RES_MGR_HTTP_HEADER_MAX=2048
CONFIG_QM_RES_MGR_API_BODY_MAX=16384

## Required when MQTT metadata is selected
CONFIG_QM_CLOUD_SUPPORT=1
CONFIG_QM_IOT_MQTT_SUPPORT=1
CONFIG_QM_IOT_DYNREG_SUPPORT=1
CONFIG_QM_IOT_DYNREG_SIGNATURE_SUPPORT=1
CONFIG_QM_PROVISION_SUPPORT=1
CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT=5000
```

## Example Macros

The source file defines these overridable macros:

```c
#define EXAMPLE_WIFI_SSID                "your-ssid"
#define EXAMPLE_WIFI_PASSWORD            "your-password"
#define QM_RES_MGR_EXAMPLE_TOKEN         "device-token"
#define QM_RES_MGR_EXAMPLE_DID           "device-did"
#define QM_RES_MGR_EXAMPLE_BUNDLE        "resource-bundle-name"
#define QM_RES_MGR_EXAMPLE_RESOURCE_ID   0
#define QM_RES_MGR_EXAMPLE_DIRECT_URL    ""
#define QM_RES_MGR_EXAMPLE_DIRECT_FILE_SIZE 0
#define QM_RES_MGR_EXAMPLE_ENV           QM_RES_MGR_ENV_DEV

#define CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_ID       0U
#define CONFIG_QM_RES_MGR_EXAMPLE_PRODUCT_SECRET   ""
#define CONFIG_QM_RES_MGR_EXAMPLE_VERSION          "1.0.0"
#define CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_HOST      ""
#define CONFIG_QM_RES_MGR_EXAMPLE_ADV_CID          ""
#define CONFIG_QM_RES_MGR_EXAMPLE_DYNREG_ENV       QM_IOT_DYNREG_ENV_PROD
#define CONFIG_QM_RES_MGR_EXAMPLE_WIFI_RETRY_MAX   10U
#define CONFIG_QM_RES_MGR_EXAMPLE_MQTT_TIMEOUT_MS  15000U
```

`QM_RES_MGR_EXAMPLE_DIRECT_URL` is useful when testing direct file download
without list/detail APIs.

When MQTT metadata mode is selected:

- `QM_RES_MGR_EXAMPLE_TOKEN` is no longer required for metadata query.
- `QM_RES_MGR_EXAMPLE_DID` and `QM_RES_MGR_EXAMPLE_BUNDLE` are still required.
- Resource file download still uses the returned `url`, so HTTP download support
  remains required.
- The HTTP timeout/read-buffer/header Kconfig items are hidden in this mode; the
  download path uses the component defaults instead.

## Entry Point

```c
app_main();
```

After Wi-Fi gets an IP address, the example calls `qm_res_mgr_example_start()`.

In MQTT metadata mode, the example starts BLE provisioning first. After Wi-Fi is ready
and dynreg succeeds, it starts `qm_iot`, reports network-up, waits for
`QM_IOT_EVENT_CLOUD_CONN`, and then triggers `qm_res_mgr_example_start()`.
