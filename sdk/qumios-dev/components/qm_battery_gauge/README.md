# qm_battery_gauge

`qm_battery_gauge` estimates 1S Li-ion battery SOC from signed current,
terminal voltage, and an optional temperature sample.

The component is designed as a product-facing gauge layer:

- Coulomb counting is the primary SOC source.
- Voltage lookup corrects long-term drift.
- Load compensation uses `V_ocv = V_terminal - I * R`.
- Full-current, charge-current-only, and voltage-only modes are supported.
- No dynamic memory is used; the caller owns the handle.
- The default table is derived from the provided 18650 5200 mAh test data.

## Responsibility Boundary

```text
ADC / current monitor / charger state
                | samples
                v
        qm_battery_gauge
                | SOC and snapshot
                v
         application policy
                | storage, UI, shutdown
```

The component estimates battery state from caller-provided samples. It does not
configure sensors, own periodic sampling, persist snapshots by default, or make
product shutdown and charging decisions.

## Current Sign

`current_ma > 0` means charge current flows into the battery.

`current_ma < 0` means discharge current flows out of the battery.

This matches the test data where the discharge step is about `-1040 mA`.

If hardware only measures charge current, use:

```c
cfg.current_mode = QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY;
```

In that mode, charge samples use `current_ma` for coulomb counting. Discharge
samples should set `current_valid = QM_FALSE`; SOC then follows the OCV table
with filtering instead of discharge coulomb counting.

## Minimal Usage

```c
#include "qm_battery_gauge.h"

static qm_battery_gauge_t g_battery_gauge;

void app_battery_init(void)
{
    qm_battery_gauge_config_t cfg;

    qm_battery_gauge_default_config(&cfg);
    cfg.design_capacity_mah = 5200.0f;
    cfg.usable_capacity_mah = 5300.0f;
    cfg.internal_resistance_ohm = 0.080f;
    cfg.current_mode = QM_BATTERY_GAUGE_CURRENT_FULL;
    qm_battery_gauge_init(&g_battery_gauge, &cfg, NULL, NULL);
}

void app_battery_tick(qm_float voltage_v, qm_float current_ma, qm_bool charging)
{
    qm_battery_gauge_sample_t sample = {
        .voltage_v = voltage_v,
        .current_ma = current_ma,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .current_valid = QM_TRUE,
        .has_temperature = QM_FALSE,
        .charging = charging,
        .charge_full = QM_FALSE,
    };

    qm_battery_gauge_update_sample(&g_battery_gauge, &sample);
}
```

## Hardware Pairing

The gauge does not own the current/voltage sensor driver. Feed it samples from
an ADC plus shunt amplifier, INA226/INA228, or another fuel-gauge front end.

Recommended first hardware path:

```text
Battery -> shunt resistor -> current/voltage monitor -> MCU -> qm_battery_gauge
```

For a 5200 mAh cell:

- 1 A load: 10 mOhm to 20 mOhm shunt is convenient.
- True 3C load: use 2 mOhm to 5 mOhm and check shunt power dissipation.
- Start with `internal_resistance_ohm = 0.080f`, then tune with load-step data.

## Default Data

The built-in table is based on the supplied workbook:

- Step 3 discharge current: about `-1040.9 mA`
- Discharged capacity: about `5295.9 mAh`
- Terminal voltage range: `4.122 V` to `2.749 V`
- Rest voltage after cutoff: about `3.088 V`

Production projects should replace the table with a relaxed OCV-SOC table
measured on the target cell and pack.

## Using Another Cell

The default table is only a bring-up profile for the supplied 1S 18650
5200 mAh data. If the product uses another cell, pack, protection board,
shunt, or load profile, override the config and OCV table in application code.

Override the config and OCV table from application initialization code instead
of editing the component defaults.

Application-side override:

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

qm_battery_gauge_config_t cfg;

qm_battery_gauge_default_config(&cfg);
cfg.design_capacity_mah = 5000.0f;
cfg.usable_capacity_mah = 4900.0f;
cfg.internal_resistance_ohm = 0.065f;
cfg.low_voltage_v = 3.30f;
cfg.empty_voltage_v = 3.00f;
cfg.cutoff_voltage_v = 2.75f;
cfg.full_voltage_v = 4.19f;
cfg.full_current_ma = 250.0f;
cfg.ocv_table = s_product_ocv_table;
cfg.ocv_table_count =
    sizeof(s_product_ocv_table) / sizeof(s_product_ocv_table[0]);

qm_battery_gauge_init(&g_battery_gauge, &cfg, NULL, NULL);
```

OCV table notes:

- Voltage and SOC must increase from the empty point to the full point.
- Use at least `0/5/10/.../100%` points when possible.
- Prefer relaxed open-circuit voltage instead of loaded terminal voltage.
- If only loaded discharge data is available, compensate it with
  `V_ocv = V_terminal - I * R` first.
- Retune `internal_resistance_ohm` on the target pack and board.

Initialization rules:

- `qm_battery_gauge_init(&gauge, NULL, NULL, NULL)` uses the built-in default config.
- An explicit config must provide a valid `ocv_table` and `ocv_table_count`.
- Invalid OCV tables return `-QM_EINVAL` instead of falling back silently.

## Feature Macros

`CONFIG_QM_BATTERY_GAUGE_SUPPORT` enables the component. Snapshot creation,
sleep restore, and power-loss restore are standard component capabilities and
do not have separate component macros.

If snapshot persistence helpers are needed, enable:

```text
CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT=1
```

Then the component can save and load one snapshot directly:

```c
qm_battery_gauge_save_snapshot(&gauge, NULL, seq, now_s);
qm_battery_gauge_load_snapshot(NULL, &snapshot);
```

Passing NULL uses `QM_BATTERY_GAUGE_DEFAULT_SNAPSHOT_KEY`. Dual slots, save
cadence, and product-specific key policy should still stay in the application.

Interface layering:

- `qm_battery_gauge_make_snapshot()` is the base snapshot export API for
  products that manage RTC memory, dual slots, version migration, or multiple
  candidate snapshots in application code
- `qm_battery_gauge_save_snapshot()` and `qm_battery_gauge_load_snapshot()`
  are single-key convenience storage APIs for quick integration, not a
  replacement for application-level persistence policy

Internal tuning constants such as current deadband, relaxation time, and filter
time constants intentionally stay inside the component; product code should tune
capacity, resistance, thresholds, OCV table, and temperature compensation.

## Long Sleep

`dt_s` is the interval since the previous sample, in seconds. It is used for
coulomb counting and is clamped by `max_update_dt_s` to avoid large timestamp
glitches.

For normal periodic sampling:

```c
sample.dt_s = 1.0f;
qm_battery_gauge_update_sample(&gauge, &sample);
```

After long deep sleep, do not pass hours directly as `dt_s`.

If the same `qm_battery_gauge_t` object stays alive and only SOC needs to be
corrected, use this light-sleep restore flow:

1. Save a `qm_battery_gauge_snapshot_t` with
   `qm_battery_gauge_make_snapshot()`.
2. On wake, fill `qm_battery_gauge_restore_config_t` with the snapshot, offline
   time, average offline current, wake voltage/current, and charger state.
3. Call `qm_battery_gauge_estimate_restore_soc()`.
4. Apply the result with `qm_battery_gauge_set_soc()`.
5. Update once with a normal short sample, for example `dt_s = 1.0f`.

For cold boot, power-loss resume, or any case where the gauge object must be
rebuilt, use `qm_battery_gauge_init()` with the restore config.

The component owns basic snapshot field checks and SOC fusion. Storage
integrity can be handled by the storage layer, such as qm kv; the application
owns storage, wear leveling, and selecting the newest valid snapshot.
If `CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT` is enabled, the component also offers simple
single-key snapshot save/load helpers for quick integration.
