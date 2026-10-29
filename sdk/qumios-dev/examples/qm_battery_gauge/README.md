# QM Battery Gauge Example

Chinese documentation: [README_CN.md](README_CN.md)

This example shows how to call `qm_battery_gauge` from the `qumi/examples`
tree.

## Files

```text
qumi/examples/qm_battery_gauge/
├── README.md
├── README_CN.md
├── qm_battery_gauge_example.c
├── qm_kconfig
└── qm_config/
    └── qm_config.h
```

## Notes

- The default flow uses a simulated voltage/current trace.
- The trace follows the supplied 18650 5200 mAh data style: about 1 A
  discharge, rest recovery, then charge and charger-full confirmation.
- The example runs full-current, charge-current-only, and voltage-only modes.
- The example includes a sleep-resume demo flow.
- The example includes a power-loss resume demo flow.
- The example includes an unknown-offline-time restore demo flow.
- Current sign matches the component convention: positive is charging,
  negative is discharging.
- The example logs SOC, raw SOC, compensated voltage, remaining mAh, charged
  mAh, discharged mAh, state, and flag changes.

Useful example macros:

- `CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SLEEP_RESUME_ENABLE`
- `CONFIG_QM_BATTERY_GAUGE_EXAMPLE_POWEROFF_RESUME_ENABLE`
- `CONFIG_QM_BATTERY_GAUGE_EXAMPLE_UNKNOWN_OFFLINE_TIME_ENABLE`

These macros only gate the example demo flows. They do not disable restore
support in the component itself.

## Retuning

If the product does not use the supplied test cell, retune the gauge instead
of only changing the simulated trace. At minimum, update:

- `design_capacity_mah`
- `usable_capacity_mah`
- `internal_resistance_ohm`
- `low_voltage_v`, `empty_voltage_v`, and `cutoff_voltage_v`
- `full_voltage_v` and `full_current_ma`
- `ocv_table` and `ocv_table_count`

The example config entry is:

```text
qumi/examples/qm_battery_gauge/qm_battery_gauge_example.c
```

The OpenCPU example entry is:

```text
apps/opencpu/app/qm_battery_gauge_example.c
```

Override fields after `qm_battery_gauge_default_config(&cfg);`:

```c
static const qm_battery_gauge_ocv_point_t s_product_ocv_table[] = {
    {  0.0f, 3.050f, 0.0f },
    { 10.0f, 3.420f, 0.0f },
    { 20.0f, 3.520f, 0.0f },
    { 30.0f, 3.610f, 0.0f },
    { 40.0f, 3.680f, 0.0f },
    { 50.0f, 3.740f, 0.0f },
    { 60.0f, 3.800f, 0.0f },
    { 70.0f, 3.870f, 0.0f },
    { 80.0f, 3.950f, 0.0f },
    { 90.0f, 4.070f, 0.0f },
    {100.0f, 4.200f, 0.0f },
};

cfg.design_capacity_mah = 5000.0f;
cfg.usable_capacity_mah = 4900.0f;
cfg.internal_resistance_ohm = 0.065f;
cfg.ocv_table = s_product_ocv_table;
cfg.ocv_table_count =
    sizeof(s_product_ocv_table) / sizeof(s_product_ocv_table[0]);
```

OpenCPU macro defaults live in:

```text
apps/opencpu/qm_config/qm_config.h
```

The built-in fallback OCV table lives in:

```text
qumi/components/qm_battery_gauge/src/qm_battery_gauge.c
```

## Entry Point

```c
qm_application_start();
```

`qm_application_start()` calls `qm_battery_gauge_example_start()`.
