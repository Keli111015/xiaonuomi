# qm_battery

`qm_battery` estimates 1S Li-ion battery SOC and SOH from voltage, optional
temperature, charger state, and elapsed time. It uses a UKF-based model and
does not allocate memory or create tasks; the application owns the handle and
sampling cadence.

## Architecture

```text
ADC or voltage source / charger state
                | samples
                v
           qm_battery
                | SOC, SOH, resistance
                v
          application policy
                | UI, persistence, shutdown
```

The component does not configure the ADC, own a fuel-gauge IC, persist state,
or decide charging and shutdown policy. With no current measurement, SOC/SOH
accuracy depends on the product OCV table, ADC calibration, and load profile.

## Responsibility Boundary

Sensor setup, sampling cadence, persistence, UI, charging policy, and shutdown
remain application responsibilities.

## Basic Usage

```c
qm_battery_t battery;
qm_battery_config_t config;

qm_battery_default_config(&config);
config.temp_comp_enable = QM_FALSE;
qm_battery_init(&battery, &config);
qm_battery_update_voltage(&battery, voltage_v, 25.0f, QM_FALSE,
                          charging, charge_full, elapsed_sec);
```

Use `qm_battery_update_adc()` only on targets that enable `QM_ADC_SUPPORT`.
Replace the default OCV table and calibration parameters in application code
for every new cell, pack, divider, and load profile. See `README_CN.md` for
the production calibration workflow and `tools/qm_battery_calibrate.py`.

## Configuration

Enable `QM_BATTERY_SUPPORT` in `qm_kconfig`. Product-independent defaults live
in Kconfig; runtime calibration, OCV tables, and persistence policy belong to
`qm_battery_config_t` and application code.
