# Xjiang HTTP OTA

For Chinese documentation, see [README_CN.md](README_CN.md).

`xjiang_http_ota` is a small adapter for Ningbo Xiaojiang's HTTP OTA version
API. Callers pass the product pid string plus current module and MCU versions,
and the component queries:

```text
GET /agent-device/iot/ota/version?pid=<pid>&moduleVersion=<version>&mcuVersion=<version>
```

It maps returned module/MCU firmware metadata into `qm_http_ota`. When both
module and MCU upgrades are returned, application code chooses which target to
upgrade first.

## Main APIs

- `xjiang_http_ota_check()` queries the version API using
  `pid`, `module_version`, `mcu_version`, and optional Authorization token,
  then returns module/MCU upgrade
  metadata. Versions are compared as three numeric fields such as `1.0.0`; if
  any returned field is greater than the current version's corresponding field,
  the firmware is marked available. Dynamic strings in the returned metadata
  are owned by the component and released with
  `xjiang_http_ota_info_free()`.
- `xjiang_http_ota_perform()` consumes the `xjiang_http_ota_info_t`
  returned by `xjiang_http_ota_check()` and upgrades the target selected by the
  application. Its optional output parameter is `qm_http_ota_result_t`.
- `xjiang_http_ota_set_env()` changes the component environment used for the
  default version API endpoint.
- `xjiang_http_ota_set_mcu_ota()` registers business callbacks for MCU OTA.
  When `xjiang_http_ota_perform()` is called with `XJIANG_HTTP_OTA_TARGET_MCU`,
  downloaded firmware chunks are forwarded through `write(user, offset, data,
  len)`.

If no environment is configured, the component uses `XJIANG_HTTP_OTA_ENV_PROD`.
Checking only reports available upgrades; applications can call
`xjiang_http_ota_perform()` later with the same check result when they decide to
upgrade, passing `XJIANG_HTTP_OTA_TARGET_MODULE` or `XJIANG_HTTP_OTA_TARGET_MCU`
to choose the target.

Before performing an MCU upgrade, the application must call
`xjiang_http_ota_set_mcu_ota()` with `qm_http_ota_mcu_config_t`. The MCU
callbacks own the actual MCU transport or storage path. Passing `NULL` clears
the registered MCU OTA callbacks.

File integrity verification is owned by `qm_http_ota`; this adapter only keeps
the Xiaojiang `sign` field in the query result for callers that need to inspect
or forward it.

## Example

The standalone example lives in `qumi/examples/xjiang_http_ota/`:

```text
qumi/examples/xjiang_http_ota/
├── README.md
├── xjiang_http_ota_example.c
└── qm_config/
    └── qm_config.h
```

Set these build-time macros before running against a real product:

- `XJIANG_HTTP_OTA_EXAMPLE_PID`
- `XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MODULE_VERSION`
- `XJIANG_HTTP_OTA_EXAMPLE_CURRENT_MCU_VERSION`
- `XJIANG_HTTP_OTA_EXAMPLE_AUTHORIZATION_TOKEN`

Set the component environment at runtime before checking or performing OTA:

```c
xjiang_http_ota_set_env(XJIANG_HTTP_OTA_ENV_DEV);
xjiang_http_ota_check(pid, module_version, mcu_version, "Bearer your_token", &info);
```

The example also exposes `xjiang_http_ota_example_check_start()` when only the
version query result needs to be inspected. Its MCU path registers sample
`qm_http_ota_mcu_config_t` callbacks before calling `xjiang_http_ota_perform()`;
replace the sample `write()` body with the product's MCU transport or storage
logic.
