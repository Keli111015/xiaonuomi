# QM Provision

For the Chinese version, see [README_CN.md](README_CN.md).

`qm_provision` is the standalone BLE provisioning component. It keeps the
original `qm_cloud` BLE provisioning behavior, but does not depend on
`qm_cloud`.

## Scope

`qm_provision` owns the BLE provisioning session only:

- starts and stops BLE provisioning advertising
- receives the original BLE spec provisioning payload, with an optional
  positional-format fallback for compatible clients
- parses WiFi provisioning data into `qm_provision_recv_t`
- reports the parsed data through `qm_provision_recv_handler_t`

The component does not own network connection, network type selection, cloud
connection, dynamic registration, token handling, or data persistence. The
application decides how to handle the received provisioning data.

## Flow

1. Create a session with `qm_provision_create()`.
2. Start provisioning with `qm_provision_start()`.
3. The BLE app sends WiFi provisioning data through the original spec payload.
4. `qm_provision_recv_handler_t` receives `ssid`, `password`, `dynreg_host`,
   `timezone`, `audio_uri`, and optional authentication data.
5. The application handles the received provisioning data.
6. Stop the session with `qm_provision_stop()` or destroy it with
   `qm_provision_destroy()`.

## Configuration

Runtime product values are passed through `qm_provision_config_t`:

- `product_id`: product ID used by BLE authentication and advertising payload
- `product_secret`: product secret used by BLE authentication
- `adv_cid`: advertising CID prefix, or `NULL` to use the default value
- `recv_handler`: callback invoked when provisioning data is received
- `userdata`: user context passed back to the callback

Fixed defaults and limits are configured by Kconfig:

- `CONFIG_QM_PROVISION_BLE_ADV_CID`: default BLE advertising CID
- `CONFIG_QM_PROVISION_LOG_CONFIG`: enables component log-category selection
- `CONFIG_QM_PROVISION_NTP_TIMEZONE`: default timezone used when the BLE payload
  does not provide one
- `CONFIG_QM_PROVISION_BLE_DEINIT_DELAY_MS`: BLE disconnect-to-deinit delay
- `CONFIG_QM_PROVISION_APINFO_ALT_FORMAT`: enables the positional APINFO
  fallback when the standard spec decoder finds no SSID
- `CONFIG_QM_PROVISION_BLE_LOCAL_NAME_MAX_LEN`: BLE advertising local name
  maximum content length
- `CONFIG_QM_PROVISION_SSID_MAX_LEN`: `ssid` maximum content length
- `CONFIG_QM_PROVISION_PASSWORD_MAX_LEN`: `password` maximum content length
- `CONFIG_QM_PROVISION_DYNREG_HOST_MAX_LEN`: `dynreg_host` maximum content length
- `CONFIG_QM_PROVISION_AUDIO_URI_MAX_LEN`: `audio_uri` maximum content length
- `CONFIG_QM_PROVISION_AUTHCODE_MAX_LEN`: authentication code maximum content
  length

Keep product identity and callback values out of Kconfig because they are
runtime/session inputs.

The public receive buffer sizes reserve one extra byte for the trailing `\0`:
`QM_PROVISION_SSID_SIZE`, `QM_PROVISION_PASSWORD_SIZE`,
`QM_PROVISION_DYNREG_HOST_SIZE`, `QM_PROVISION_AUDIO_URI_SIZE`, and
`QM_PROVISION_AUTHCODE_SIZE`.

## Data Fields

`qm_provision_recv_t` contains the provisioning data parsed from the BLE payload:

- `ssid`: WiFi SSID
- `password`: WiFi password
- `dynreg_host`: dynamic registration host from the BLE payload
- `timezone`: timezone from the BLE payload, or the configured default timezone
- `audio_uri`: audio resource URI from the BLE payload
- `authcode` / `authcode_len`: app key or network key and its valid length
- `token_type`: token type supplied by the version 2 BLE protocol
- `env_type`: environment type from BLE APINFO property `0xEC` (v2) or standard service `siid=1, piid=12` (v3); `0x00` development, `0x01` test, `0x02` production
- `env_type_valid`: whether `env_type` was received; do not treat its default value as production when absent

The receive object and all embedded fields are valid only while the callback is
running. Copy any data that must outlive the callback.

## Architecture

```text
BLE client
    |
    v
qm_ble transport/authentication
    |
    v
qm_provision payload decoding and session lifecycle
    |
    v
application callback -> WiFi/cloud/persistence policy
```

`qm_provision` depends only on QUMI BLE, spec, work queue, memory, and OS
abstractions. Vendor BLE and RTOS details remain in the platform adapters.

## Basic Usage

```c
#include "qm_provision.h"

static void provision_recv_handler(qm_provision_handle_t handle,
                                   const qm_provision_recv_t *recv,
                                   void *userdata)
{
    (void)handle;
    (void)userdata;

    /* Handle provisioning data from recv here. */
}

void app_provision_start(void)
{
    qm_provision_config_t config = {
        .product_id = 123456,
        .product_secret = "product-secret",
        .adv_cid = NULL,
        .recv_handler = provision_recv_handler,
        .userdata = NULL,
    };
    qm_provision_handle_t handle;

    handle = qm_provision_create(&config);
    if (handle == NULL) {
        return;
    }

    (void)qm_provision_start(handle);
}
```

## Lifecycle

- `qm_provision_create()` creates one BLE provisioning session handle.
- `qm_provision_start()` starts BLE advertising. Calling it repeatedly on the
  same started handle returns success. It returns `-QM_EBUSY` while a previous
  stop is still waiting for BLE disconnection.
- Only one active BLE provisioning session is supported at a time.
- `qm_provision_stop()` stops advertising and disconnects an active central,
  while retaining the session handle so it can be started again.
- `qm_provision_destroy()` stops the session, deinitializes BLE, and releases
  the session handle. Destruction completes after disconnect when a central is
  still connected.

## Error Codes

- `QM_EOK`: operation succeeded
- `-QM_EINVAL`: invalid handle or configuration
- `-QM_EBUSY`: another BLE provisioning session is already active

## Related Examples

- `qumi/examples/qm_provision/qm_provision_example.c`
- `apps/opencpu/app/qm_provision_example.c`
