# QM Battery Example

Chinese documentation: [README_CN.md](README_CN.md)

This example shows how to call the `qm_battery` estimator from the `qumi/examples` tree.

## Files

```text
qumi/examples/qm_battery/
├── README.md
├── README_CN.md
├── qm_battery_example.c
├── qm_kconfig
└── qm_config/
    └── qm_config.h
```

## Notes

- Default flow uses a simulated voltage trace.
- Optional ADC path can be enabled with `CONFIG_QM_BATTERY_EXAMPLE_USE_ADC`;
  it uses `qm_battery_update_adc()` and can be wired to a real charger-full
  GPIO in product code.
- Temperature is not required; the example uses the default temperature path.
- The simulated trace includes non-charging, plug-in, unplug, voltage spike,
  and voltage sag points so `charge_settle_s` and dynamic-voltage handling can
  be observed.
- The simulated trace also includes charger-full points and uses
  `qm_battery_update_voltage()` so full-signal debounce and the 100% full anchor
  can be observed.
- The example restores a persisted SOC seed by default. It sets
  `initial_soc_valid`, `initial_soc_percent`, and `initial_soc_age_s` before
  calling `qm_battery_init()`. Standby drain is controlled by the component
  strategy default in `qm_config`.
- The example also restores a persisted `learned_full_v` by default and logs
  `learned_full`/`learned_ok` so charger-full relearning can be observed.
- It also logs `guard` so the recent-full SOC guard after unplugging can be
  observed.
- If the product can boot while already charging, restore a saved SOC plus its
  age instead of using the first charging terminal voltage as OCV.

## Example Configuration

```text
## Lab calibration inputs
CONFIG_QM_BATTERY_EXAMPLE_USE_ADC=0
CONFIG_QM_BATTERY_EXAMPLE_ADC_SCALE_V_PER_COUNT=0.001f
CONFIG_QM_BATTERY_EXAMPLE_ADC_OFFSET_V=0.0f
CONFIG_QM_BATTERY_EXAMPLE_ADC_DIVIDER_RATIO=1.0f
CONFIG_QM_BATTERY_EXAMPLE_R_NOMINAL_MOHM=60
CONFIG_QM_BATTERY_EXAMPLE_FULL_ANCHOR_MV=4180
CONFIG_QM_BATTERY_EXAMPLE_EMPTY_ANCHOR_MV=3300
CONFIG_QM_BATTERY_EXAMPLE_CHARGE_FULL_MIN_MV=4050

## Demo persistence seeds
CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_ENABLE=1
CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_PERCENT=72
CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_S=7200
CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_KNOWN=1
CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_ENABLE=1
CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_MV=4120

## Strategy defaults from qm_config
CONFIG_QM_BATTERY_CHARGE_FULL_CONFIRM_S=8
CONFIG_QM_BATTERY_LEARNED_FULL_BLEND_PERMILLE=80
CONFIG_QM_BATTERY_FULL_GUARD_S=1800
CONFIG_QM_BATTERY_FULL_GUARD_MIN_PERCENT=98
```

The `CONFIG_QM_BATTERY_EXAMPLE_ADC_*`,
`CONFIG_QM_BATTERY_EXAMPLE_R_NOMINAL_MOHM`,
`CONFIG_QM_BATTERY_EXAMPLE_FULL_ANCHOR_MV`,
`CONFIG_QM_BATTERY_EXAMPLE_EMPTY_ANCHOR_MV`, and
`CONFIG_QM_BATTERY_EXAMPLE_CHARGE_FULL_MIN_MV` values simulate laboratory
calibration results. The example imports them through
`qm_battery_apply_calibration()` after `qm_battery_default_config()`.

`CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_*` values are demo persistence seeds used
to show how saved SOC ages and offline drain interact at boot.

`CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_*` values are also demo persistence
seeds used to show how a persisted full-voltage value is restored and updated.

`CONFIG_QM_BATTERY_CHARGE_FULL_CONFIRM_S`,
`CONFIG_QM_BATTERY_LEARNED_FULL_BLEND_PERMILLE`,
`CONFIG_QM_BATTERY_FULL_GUARD_S`, and
`CONFIG_QM_BATTERY_FULL_GUARD_MIN_PERCENT` are strategy knobs in `qm_config`.

`CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_S` is the age of the restored SOC
record. The component subtracts standby drain before the first update and uses
the first non-charging OCV sample as a downward check when the saved value is
older than `CONFIG_QM_BATTERY_INITIAL_SOC_MAX_AGE_H`.

If the saved SOC age is not available, set
`CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_KNOWN=0`. The example then marks the
saved SOC as stale by setting `initial_soc_age_s` to
`initial_soc_max_age_s + 1`, so the first non-charging OCV sample can check it
downward.

`CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_MV` simulates a full-voltage value
persisted by the product. A newly confirmed charger-full event updates it slowly
according to `CONFIG_QM_BATTERY_LEARNED_FULL_BLEND_PERMILLE`; the product should
persist `result.learned_full_v` only when `result.learned_full_valid` is true.

## Simulated Trace

The default trace is equivalent to:

```csv
time_s,event,voltage,charging,charge_full
0,battery_only_stable,4.100,0,0
1,battery_only_stable,4.096,0,0
2,battery_only_stable,4.093,0,0
3,battery_only_stable,4.091,0,0
4,plug_in_voltage_step_up,4.155,1,0
5,plug_in_settling,4.142,1,0
6,charge_ramp,4.150,1,0
7,charge_voltage_spike_up,4.205,1,0
8,charge_spike_recovery,4.152,1,0
9,charge_ramp,4.156,1,0
10,charge_full_signal,4.158,1,1
11,charge_full_signal,4.157,1,1
12,charge_full_signal,4.156,1,1
13,charge_full_signal,4.155,1,1
14,charge_full_signal,4.154,1,1
15,charge_full_signal,4.153,1,1
16,charge_full_signal,4.152,1,1
17,charge_full_confirmed,4.151,1,1
18,unplug_voltage_step_down,4.090,0,0
19,load_voltage_sag_down,3.980,0,0
20,load_sag_recovery,4.070,0,0
21,battery_only_after_unplug,4.064,0,0
22,battery_only_after_unplug,4.058,0,0
```

The example also prints a few one-shot key logs before and during the trace:
calibration import result, restored SOC/learned-full seeds, init strategy values,
and state changes for `full_confirmed`, `full_guard`, and `relaxed`. Per-step
logs still carry the full numeric snapshot for SOC, SOH, resistance, `dvdt`,
learned full voltage, and guard state.

## Entry Point

```c
qm_application_start();
```

`qm_application_start()` calls `qm_battery_example_start()`.
