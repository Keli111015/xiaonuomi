# QM Alarm Example

Chinese documentation: [README_CN.md](README_CN.md)

This example keeps the reusable `qm_alarm` usage itself in `qumi/examples`.
The runnable ESP OpenCPU entry is implemented independently in
`apps/opencpu/app/qm_alarm_example.c`.

## Files

```text
qumi/examples/qm_alarm/
├── README.md
├── README_CN.md
├── qm_kconfig
├── qm_alarm_example.c
├── qm_alarm_example.h
└── qm_config/
    └── qm_config.h
```

## What It Shows

- Create, start, report, stop, and destroy a `qm_alarm` instance.
- Create one local example alarm after the cloud session is ready.
- Register the `alarm` / `alarmReply` MQTT methods through an existing
  `qm_iot` handle.
- Log ring, dismiss, and snooze actions through the example callback.
- Support the same startup shape as `qm_player`: fixed Wi-Fi or BLE provision
  -> dynreg -> `qm_iot` -> `qm_alarm`.

## Entry Points

```c
if (qm_alarm_example_start(iot_handle) != QM_EOK) {
    /* handle startup error */
}
qm_alarm_example_stop();
```

## Notes

- `qm_alarm` requires an already started `qm_iot` handle because it registers
  the `alarm` and `alarmReply` methods on the device MQTT session.
- The OpenCPU-side runnable entry is
  `apps/opencpu/app/qm_alarm_example.c`. It handles Wi-Fi, dynreg, `qm_iot`,
  and local `qm_alarm` startup in one self-contained file, without depending
  on `qumi/examples/qm_alarm/qm_alarm_example.c`.
- If `CONFIG_QM_ALARM_EXAMPLE_WIFI_SSID` is empty, the OpenCPU wrapper falls
  back to BLE provisioning and can take the dynreg host from the provision
  packet.
- The example can either use a fixed alarm time or derive a near-future alarm
  from the current local time through `CONFIG_QM_ALARM_EXAMPLE_USE_FIXED_TIME`.
