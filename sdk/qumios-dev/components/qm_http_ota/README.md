# QM HTTP OTA

`qm_http_ota` downloads an image through `qm_http_client` and writes it either
to the current SoC through `qm_ota` or to an external MCU through callbacks.
It supports streamed progress events, resume records, and optional MD5 or
HMAC-SHA256 validation.

## Architecture

```text
application OTA policy and event callback
                v
            qm_http_ota
       |                 |
       v                 v
qm_http_client      qm_ota or MCU callbacks
       |                 |
       +---- image service / target storage
```

The component owns download orchestration and image transfer. Product code owns
version policy, trusted digest/signature input, restart policy, and external
MCU transport/storage behavior. Event headers and data buffers are borrowed and
valid only during the callback.

## Responsibility Boundary

The OTA component never decides whether an image is trusted or whether the
device should reboot; those decisions belong to the application.

## Basic Usage

Provide `qm_http_client_request_t` and `qm_http_ota_config_t` with a non-zero
`expected_total_size`, then call `qm_http_ota()` for a one-shot operation. The
staged API is `qm_http_ota_begin()`, repeated `qm_http_ota_perform()`, then
`qm_http_ota_finish()`; call `qm_http_ota_abort()` on failure or cancellation.

Enable `QM_HTTP_OTA_SUPPORT` in `qm_kconfig`. See `README_CN.md` for target,
resume, digest, and MCU callback contracts.
