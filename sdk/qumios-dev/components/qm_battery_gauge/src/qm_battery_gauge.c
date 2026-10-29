#include "qm_battery_gauge.h"

#if !CONFIG_QM_WITHOUT_MATH_H_SUPPORT
#include <math.h>
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT
#define CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT 0
#endif

#if CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT
#include "qm_kv.h"
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_DESIGN_CAPACITY_MAH
#define CONFIG_QM_BATTERY_GAUGE_DESIGN_CAPACITY_MAH 5200
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_USABLE_CAPACITY_MAH
#define CONFIG_QM_BATTERY_GAUGE_USABLE_CAPACITY_MAH 5300
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_R_INTERNAL_MOHM
#define CONFIG_QM_BATTERY_GAUGE_R_INTERNAL_MOHM 80
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_LOW_SOC_PERCENT
#define CONFIG_QM_BATTERY_GAUGE_LOW_SOC_PERCENT 10
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_LOW_VOLTAGE_MV
#define CONFIG_QM_BATTERY_GAUGE_LOW_VOLTAGE_MV 3300
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EMPTY_VOLTAGE_MV
#define CONFIG_QM_BATTERY_GAUGE_EMPTY_VOLTAGE_MV 3000
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_CUTOFF_VOLTAGE_MV
#define CONFIG_QM_BATTERY_GAUGE_CUTOFF_VOLTAGE_MV 2750
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_FULL_VOLTAGE_MV
#define CONFIG_QM_BATTERY_GAUGE_FULL_VOLTAGE_MV 4190
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_FULL_CURRENT_MA
#define CONFIG_QM_BATTERY_GAUGE_FULL_CURRENT_MA 250
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_DEFAULT_TEMP_C
#define CONFIG_QM_BATTERY_GAUGE_DEFAULT_TEMP_C 25
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_TEMP_COMP_ENABLE
#define CONFIG_QM_BATTERY_GAUGE_TEMP_COMP_ENABLE 0
#endif

#define GAUGE_DT_MIN_S 0.001f
#define GAUGE_DEFAULT_MAX_DT_S 60.0f
#define GAUGE_MIN_VOLTAGE_V 0.5f
#define GAUGE_MAX_VOLTAGE_V 6.0f
#define GAUGE_TEMP_REF_C 25.0f
#define GAUGE_CHARGE_EFFICIENCY_RATIO 0.995f
#define GAUGE_CURRENT_DEADBAND_MA 5.0f
#define GAUGE_RELAX_CURRENT_MA 30.0f
#define GAUGE_RELAX_CONFIRM_S 180.0f
#define GAUGE_VOLTAGE_RELAX_TAU_S 120.0f
#define GAUGE_VOLTAGE_RUNNING_TAU_S 3600.0f
#define GAUGE_OUTPUT_TAU_S 8.0f

/*
 * Bring-up OCV table derived from the supplied 1S 18650 5200 mAh discharge
 * data. Product code should pass its own relaxed OCV table in config.
 */
static const qm_battery_gauge_ocv_point_t s_default_ocv_table[] = {
    {  0.0f, 3.088f, 0.0f },
    {  5.0f, 3.345f, 0.0f },
    { 10.0f, 3.430f, 0.0f },
    { 15.0f, 3.491f, 0.0f },
    { 20.0f, 3.535f, 0.0f },
    { 25.0f, 3.566f, 0.0f },
    { 30.0f, 3.591f, 0.0f },
    { 35.0f, 3.614f, 0.0f },
    { 40.0f, 3.635f, 0.0f },
    { 45.0f, 3.658f, 0.0f },
    { 50.0f, 3.685f, 0.0f },
    { 55.0f, 3.720f, 0.0f },
    { 60.0f, 3.767f, 0.0f },
    { 65.0f, 3.816f, 0.0f },
    { 70.0f, 3.865f, 0.0f },
    { 75.0f, 3.912f, 0.0f },
    { 80.0f, 3.960f, 0.0f },
    { 85.0f, 4.014f, 0.0f },
    { 90.0f, 4.066f, 0.0f },
    { 95.0f, 4.105f, 0.0f },
    {100.0f, 4.200f, 0.0f },
};

#define GAUGE_DEFAULT_TABLE_COUNT \
    ((qm_size_t)(sizeof(s_default_ocv_table) / sizeof(s_default_ocv_table[0])))

static const qm_battery_gauge_ocv_point_t *gauge_default_ocv_table(
    qm_size_t *count)
{
    if (count) {
        *count = GAUGE_DEFAULT_TABLE_COUNT;
    }
    return s_default_ocv_table;
}

static qm_float gauge_clampf(qm_float value, qm_float lo, qm_float hi)
{
    if (value < lo) return lo;
    if (value > hi) return hi;
    return value;
}

static qm_float gauge_absf(qm_float value)
{
    return value < 0.0f ? -value : value;
}

static qm_float gauge_blendf(qm_float old_value, qm_float target, qm_float alpha)
{
    qm_float a = gauge_clampf(alpha, 0.0f, 1.0f);
    return old_value + (target - old_value) * a;
}

static qm_bool gauge_isfinitef(qm_float value)
{
#if !CONFIG_QM_WITHOUT_MATH_H_SUPPORT
    return isfinite(value) ? QM_TRUE : QM_FALSE;
#else
    return value == value && value < 3.4e38f && value > -3.4e38f ?
           QM_TRUE : QM_FALSE;
#endif
}

static qm_float gauge_alpha_from_tau(qm_float dt_s, qm_float tau_s)
{
    if (tau_s <= 0.0f) {
        return 1.0f;
    }
    return gauge_clampf(dt_s / (tau_s + dt_s), 0.0f, 1.0f);
}

static qm_float gauge_restore_voltage_weight(qm_bool snapshot_used,
                                             qm_bool offline_time_valid,
                                             qm_float offline_time_s)
{
    if (!snapshot_used) return 1.0f;
    if (!offline_time_valid) return 0.5f;
    if (offline_time_s < 600.0f) return 0.0f;
    if (offline_time_s < 3600.0f) return 0.2f;
    if (offline_time_s < 24.0f * 3600.0f) return 0.4f;
    if (offline_time_s < 7.0f * 24.0f * 3600.0f) return 0.6f;
    return 0.8f;
}

static qm_bool gauge_ocv_table_valid(
    const qm_battery_gauge_ocv_point_t *table,
    qm_size_t count)
{
    qm_size_t i;

    if (!table || count < 2) {
        return QM_FALSE;
    }
    if (table[0].soc_percent > 5.0f ||
        table[count - 1].soc_percent < 95.0f) {
        return QM_FALSE;
    }

    for (i = 0; i < count; i++) {
        if (!gauge_isfinitef(table[i].soc_percent) ||
            !gauge_isfinitef(table[i].voltage_25c) ||
            !gauge_isfinitef(table[i].temp_coeff_v_per_c) ||
            table[i].soc_percent < 0.0f ||
            table[i].soc_percent > 100.0f ||
            table[i].voltage_25c < GAUGE_MIN_VOLTAGE_V ||
            table[i].voltage_25c > GAUGE_MAX_VOLTAGE_V) {
            return QM_FALSE;
        }
        if (i > 0 &&
            (table[i].soc_percent <= table[i - 1].soc_percent ||
             table[i].voltage_25c <= table[i - 1].voltage_25c)) {
            return QM_FALSE;
        }
    }

    return QM_TRUE;
}

static qm_float gauge_point_voltage(const qm_battery_gauge_ocv_point_t *point,
                                    qm_float temperature_c,
                                    qm_bool temp_comp_enable)
{
    qm_float voltage = point->voltage_25c;
    if (temp_comp_enable) {
        voltage += point->temp_coeff_v_per_c * (temperature_c - GAUGE_TEMP_REF_C);
    }
    return voltage;
}

static qm_float gauge_soc_from_voltage(const qm_battery_gauge_config_t *config,
                                       qm_float voltage_v,
                                       qm_float temperature_c)
{
    const qm_battery_gauge_ocv_point_t *table;
    qm_size_t count;
    qm_size_t i;

    table = config->ocv_table;
    count = config->ocv_table_count;

    if (voltage_v <= gauge_point_voltage(&table[0], temperature_c,
                                         config->temp_comp_enable)) {
        return table[0].soc_percent;
    }
    if (voltage_v >= gauge_point_voltage(&table[count - 1], temperature_c,
                                         config->temp_comp_enable)) {
        return table[count - 1].soc_percent;
    }

    /* Linear interpolation keeps the OCV table small and predictable. */
    for (i = 0; i < count - 1; i++) {
        qm_float v0 = gauge_point_voltage(&table[i], temperature_c,
                                          config->temp_comp_enable);
        qm_float v1 = gauge_point_voltage(&table[i + 1], temperature_c,
                                          config->temp_comp_enable);
        if (voltage_v >= v0 && voltage_v <= v1) {
            qm_float span = v1 - v0;
            qm_float t = span > 0.0001f ? (voltage_v - v0) / span : 0.0f;
            return gauge_clampf(table[i].soc_percent +
                                (table[i + 1].soc_percent - table[i].soc_percent) * t,
                                0.0f, 100.0f);
        }
    }

    return 50.0f;
}

static void gauge_sync_remaining(qm_battery_gauge_t *gauge)
{
    gauge->soc_percent = gauge_clampf(gauge->soc_percent, 0.0f, 100.0f);
    gauge->remaining_mah = gauge->config.usable_capacity_mah *
                           gauge->soc_percent / 100.0f;
}

static void gauge_sync_soc(qm_battery_gauge_t *gauge)
{
    qm_float capacity = gauge->config.usable_capacity_mah;
    if (capacity <= 0.0f) {
        gauge->soc_percent = 0.0f;
        gauge->remaining_mah = 0.0f;
        return;
    }
    gauge->remaining_mah = gauge_clampf(gauge->remaining_mah, 0.0f, capacity);
    gauge->soc_percent = gauge->remaining_mah * 100.0f / capacity;
}

static void gauge_force_soc(qm_battery_gauge_t *gauge, qm_float soc_percent)
{
    gauge->soc_percent = gauge_clampf(soc_percent, 0.0f, 100.0f);
    gauge->output_soc_percent = gauge->soc_percent;
    gauge_sync_remaining(gauge);
}

static qm_err_t gauge_reset(qm_battery_gauge_t *gauge)
{
    qm_battery_gauge_t zero_gauge = { 0 };
    qm_battery_gauge_config_t config;
    qm_float boot_soc;

    if (!gauge) {
        return -QM_EINVAL;
    }

    config = gauge->config;
    *gauge = zero_gauge;
    gauge->config = config;
    gauge->temperature_c = config.default_temperature_c;
    gauge->state = QM_BATTERY_GAUGE_STATE_UNKNOWN;

    boot_soc = config.initial_soc_valid ?
               config.initial_soc_percent : 50.0f;
    gauge->soc_percent = gauge_clampf(boot_soc, 0.0f, 100.0f);
    gauge->output_soc_percent = gauge->soc_percent;
    gauge->soc_seeded = config.initial_soc_valid;
    gauge_sync_remaining(gauge);
    return QM_EOK;
}

static qm_bool gauge_config_valid(const qm_battery_gauge_config_t *config)
{
    if (!config) {
        return QM_FALSE;
    }
    if (!gauge_isfinitef(config->design_capacity_mah) ||
        !gauge_isfinitef(config->usable_capacity_mah) ||
        config->design_capacity_mah <= 0.0f ||
        config->usable_capacity_mah <= 0.0f) {
        return QM_FALSE;
    }
    if (!gauge_isfinitef(config->internal_resistance_ohm) ||
        config->internal_resistance_ohm < 0.0f) {
        return QM_FALSE;
    }
    if (!gauge_isfinitef(config->max_update_dt_s) ||
        config->max_update_dt_s < 0.0f) {
        return QM_FALSE;
    }
    if (config->current_mode < QM_BATTERY_GAUGE_CURRENT_FULL ||
        config->current_mode > QM_BATTERY_GAUGE_CURRENT_VOLTAGE_ONLY) {
        return QM_FALSE;
    }
    return gauge_ocv_table_valid(config->ocv_table, config->ocv_table_count);
}

static qm_bool gauge_sample_current_valid(
    const qm_battery_gauge_config_t *config,
    const qm_battery_gauge_sample_t *sample)
{
    if (config->current_mode == QM_BATTERY_GAUGE_CURRENT_FULL) {
        return QM_TRUE;
    }
    if (config->current_mode == QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY) {
        return sample->charging && sample->current_valid ? QM_TRUE : QM_FALSE;
    }
    return QM_FALSE;
}

static qm_bool gauge_sample_direction_valid(qm_bool charging,
                                            qm_float current_ma)
{
    if (current_ma == 0.0f) {
        return QM_TRUE;
    }
    if (charging && current_ma < 0.0f) {
        return QM_FALSE;
    }
    return QM_TRUE;
}

static void gauge_apply_coulomb_count(qm_battery_gauge_t *gauge,
                                      qm_float current_ma,
                                      qm_float dt_s)
{
    qm_float delta_mah = current_ma * dt_s / 3600.0f;

    /*
     * Current sign convention: positive current charges the battery, negative
     * current discharges it. Charging is derated by coulomb efficiency.
     */
    if (delta_mah > 0.0f) {
        qm_float stored_mah = delta_mah * GAUGE_CHARGE_EFFICIENCY_RATIO;
        gauge->charged_mah += delta_mah;
        gauge->remaining_mah += stored_mah;
    } else if (delta_mah < 0.0f) {
        gauge->discharged_mah += -delta_mah;
        gauge->remaining_mah += delta_mah;
    }

    gauge_sync_soc(gauge);
}

static void gauge_update_relaxation(qm_battery_gauge_t *gauge,
                                    qm_bool current_valid,
                                    qm_float current_ma,
                                    qm_float dt_s)
{
    if (!current_valid) {
        gauge->relax_time_s = 0.0f;
        gauge->relaxed = QM_FALSE;
        return;
    }

    /* OCV correction is trusted faster only after the current stays near zero. */
    if (gauge_absf(current_ma) <= GAUGE_RELAX_CURRENT_MA) {
        gauge->relax_time_s += dt_s;
        if (gauge->relax_time_s >= GAUGE_RELAX_CONFIRM_S) {
            gauge->relaxed = QM_TRUE;
        }
    } else {
        gauge->relax_time_s = 0.0f;
        gauge->relaxed = QM_FALSE;
    }
}

static void gauge_apply_voltage_correction(qm_battery_gauge_t *gauge,
                                           qm_float voltage_soc,
                                           qm_bool charging,
                                           qm_float dt_s)
{
    qm_float tau_s = gauge->relaxed ?
                     GAUGE_VOLTAGE_RELAX_TAU_S :
                     GAUGE_VOLTAGE_RUNNING_TAU_S;
    qm_float alpha;

    if (charging && !gauge->relaxed) {
        /* Charging terminal voltage is less reliable before the cell settles. */
        tau_s *= 4.0f;
    }

    alpha = gauge_alpha_from_tau(dt_s, tau_s);
    gauge->soc_percent = gauge_blendf(gauge->soc_percent, voltage_soc, alpha);
    gauge_sync_remaining(gauge);
}

static void gauge_apply_voltage_only_correction(qm_battery_gauge_t *gauge,
                                                qm_float voltage_soc,
                                                qm_bool charging,
                                                qm_float dt_s)
{
    qm_float alpha;

    if (charging) {
        gauge_apply_voltage_correction(gauge, voltage_soc, charging, dt_s);
        return;
    }

    if (voltage_soc < gauge->soc_percent) {
        alpha = gauge_alpha_from_tau(dt_s, GAUGE_VOLTAGE_RUNNING_TAU_S);
        gauge->soc_percent = gauge_blendf(gauge->soc_percent, voltage_soc, alpha);
        gauge_sync_remaining(gauge);
        return;
    }

    if (gauge->relaxed) {
        alpha = gauge_alpha_from_tau(dt_s, GAUGE_VOLTAGE_RELAX_TAU_S * 4.0f);
        gauge->soc_percent = gauge_blendf(gauge->soc_percent, voltage_soc, alpha);
        gauge_sync_remaining(gauge);
    }
}

static void gauge_apply_soc_correction(qm_battery_gauge_t *gauge,
                                       qm_float voltage_soc,
                                       qm_bool charging,
                                       qm_float dt_s)
{
    if (gauge->config.current_mode == QM_BATTERY_GAUGE_CURRENT_FULL) {
        gauge_apply_voltage_correction(gauge, voltage_soc, charging, dt_s);
        return;
    }

    gauge_apply_voltage_only_correction(gauge, voltage_soc, charging, dt_s);
}

static void gauge_apply_voltage_anchors(qm_battery_gauge_t *gauge,
                                        qm_bool charging,
                                        qm_float dt_s)
{
    qm_float alpha;

    if (!charging && gauge->voltage_v <= gauge->config.cutoff_voltage_v) {
        /* Use raw terminal voltage for cutoff so protection is never delayed. */
        gauge_force_soc(gauge, 0.0f);
        gauge->empty = QM_TRUE;
        return;
    }

    if (!charging && (gauge->voltage_v <= gauge->config.empty_voltage_v ||
                      gauge->compensated_voltage_v <= gauge->config.empty_voltage_v)) {
        /* Near empty, voltage anchors should win over slow drift correction. */
        alpha = gauge_alpha_from_tau(dt_s, 15.0f);
        gauge->soc_percent = gauge_blendf(gauge->soc_percent, 0.0f, alpha);
        gauge_sync_remaining(gauge);
    }
}

static void gauge_apply_full_anchor(qm_battery_gauge_t *gauge,
                                    qm_bool charging,
                                    qm_bool charge_full,
                                    qm_bool current_valid,
                                    qm_float current_ma,
                                    qm_float dt_s)
{
    qm_bool current_ok = current_valid &&
                         current_ma >= 0.0f &&
                         current_ma <= gauge->config.full_current_ma ?
                         QM_TRUE : QM_FALSE;
    qm_bool voltage_ok = gauge->voltage_v >= gauge->config.full_voltage_v ?
                         QM_TRUE : QM_FALSE;
    qm_bool full_condition = charging && voltage_ok && (charge_full || current_ok);

    /* Require a stable charger-full condition before clamping SOC to 100%. */
    if (full_condition) {
        gauge->full_time_s += dt_s;
    } else {
        gauge->full_time_s = 0.0f;
        if (!charging) {
            gauge->full = QM_FALSE;
        }
        return;
    }

    if (gauge->full_time_s >= gauge->config.full_confirm_s) {
        gauge_force_soc(gauge, 100.0f);
        gauge->full = QM_TRUE;
        gauge->empty = QM_FALSE;
    }
}

static void gauge_update_flags(qm_battery_gauge_t *gauge, qm_bool charging)
{
    gauge->low = (gauge->output_soc_percent <= gauge->config.low_soc_percent ||
                  gauge->voltage_v <= gauge->config.low_voltage_v ||
                  gauge->compensated_voltage_v <= gauge->config.low_voltage_v) ?
                 QM_TRUE : QM_FALSE;

    if (gauge->output_soc_percent <= 0.5f ||
        gauge->voltage_v <= gauge->config.cutoff_voltage_v) {
        gauge->empty = QM_TRUE;
    } else if (charging || gauge->output_soc_percent > 2.0f) {
        gauge->empty = QM_FALSE;
    }

    if (gauge->full) {
        gauge->state = QM_BATTERY_GAUGE_STATE_FULL;
    } else if (gauge->empty) {
        gauge->state = QM_BATTERY_GAUGE_STATE_EMPTY;
    } else if (gauge->low) {
        gauge->state = QM_BATTERY_GAUGE_STATE_LOW;
    } else if (charging && gauge->current_ma >= 0.0f) {
        gauge->state = QM_BATTERY_GAUGE_STATE_CHARGING;
    } else if (!charging &&
               gauge->config.current_mode != QM_BATTERY_GAUGE_CURRENT_FULL) {
        gauge->state = QM_BATTERY_GAUGE_STATE_DISCHARGING;
    } else if (gauge->current_ma < 0.0f) {
        gauge->state = QM_BATTERY_GAUGE_STATE_DISCHARGING;
    } else {
        gauge->state = QM_BATTERY_GAUGE_STATE_IDLE;
    }
}

void qm_battery_gauge_default_config(qm_battery_gauge_config_t *config)
{
    qm_battery_gauge_config_t zero_config = { 0 };
    qm_size_t count = 0;

    if (!config) {
        return;
    }

    *config = zero_config;
    config->design_capacity_mah =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_DESIGN_CAPACITY_MAH;
    config->usable_capacity_mah =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_USABLE_CAPACITY_MAH;
    config->internal_resistance_ohm =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_R_INTERNAL_MOHM / 1000.0f;
    config->max_update_dt_s = GAUGE_DEFAULT_MAX_DT_S;
    config->current_mode = QM_BATTERY_GAUGE_CURRENT_FULL;
    config->full_voltage_v =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_FULL_VOLTAGE_MV / 1000.0f;
    config->full_current_ma =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_FULL_CURRENT_MA;
    config->full_confirm_s = 8.0f;
    config->empty_voltage_v =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_EMPTY_VOLTAGE_MV / 1000.0f;
    config->cutoff_voltage_v =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_CUTOFF_VOLTAGE_MV / 1000.0f;
    config->low_voltage_v =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_LOW_VOLTAGE_MV / 1000.0f;
    config->low_soc_percent =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_LOW_SOC_PERCENT;
    config->default_temperature_c =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_DEFAULT_TEMP_C;
    config->load_compensation_enable = QM_TRUE;
    config->temp_comp_enable =
        CONFIG_QM_BATTERY_GAUGE_TEMP_COMP_ENABLE ? QM_TRUE : QM_FALSE;
    config->initial_soc_valid = QM_FALSE;
    config->initial_soc_percent = 50.0f;
    config->ocv_table = gauge_default_ocv_table(&count);
    config->ocv_table_count = count;
}

qm_err_t qm_battery_gauge_init(
    qm_battery_gauge_t *gauge,
    const qm_battery_gauge_config_t *config,
    const qm_battery_gauge_restore_config_t *restore,
    qm_battery_gauge_restore_result_t *result)
{
    qm_battery_gauge_config_t local_config;
    qm_battery_gauge_restore_result_t local_result;
    qm_err_t err;

    if (!gauge) {
        return -QM_EINVAL;
    }

    if (config) {
        local_config = *config;
        if (local_config.usable_capacity_mah <= 0.0f &&
            local_config.design_capacity_mah > 0.0f) {
            local_config.usable_capacity_mah = local_config.design_capacity_mah;
        }
    } else {
        qm_battery_gauge_default_config(&local_config);
    }

    if (restore) {
        /*
         * A restore boot uses the same gauge init path, but seeds SOC from the
         * persisted snapshot, expected offline drain, and wake voltage.
         */
        err = qm_battery_gauge_estimate_restore_soc(&local_config,
                                                    restore,
                                                    &local_result);
        if (err != QM_EOK) {
            return err;
        }
        local_config.initial_soc_valid = QM_TRUE;
        local_config.initial_soc_percent = local_result.final_soc_percent;
        if (result) {
            *result = local_result;
        }
    } else if (result) {
        qm_battery_gauge_restore_result_t zero_result = { 0 };
        *result = zero_result;
    }

    if (!gauge_config_valid(&local_config)) {
        return -QM_EINVAL;
    }

    gauge->config = local_config;
    return gauge_reset(gauge);
}

qm_err_t qm_battery_gauge_set_soc(qm_battery_gauge_t *gauge,
                                  qm_float soc_percent)
{
    if (!gauge || !gauge_isfinitef(soc_percent)) {
        return -QM_EINVAL;
    }

    gauge_force_soc(gauge, soc_percent);
    gauge->soc_seeded = QM_TRUE;
    return QM_EOK;
}

qm_err_t qm_battery_gauge_make_snapshot(
    const qm_battery_gauge_t *gauge,
    qm_battery_gauge_snapshot_t *snapshot,
    qm_u32 seq,
    qm_u32 timestamp_s)
{
    qm_battery_gauge_snapshot_t zero_snapshot = { 0 };

    if (!gauge || !snapshot || !gauge->soc_seeded) {
        return -QM_EINVAL;
    }

    *snapshot = zero_snapshot;
    snapshot->magic = QM_BATTERY_GAUGE_SNAPSHOT_MAGIC;
    snapshot->version = QM_BATTERY_GAUGE_SNAPSHOT_VERSION;
    snapshot->seq = seq;
    snapshot->timestamp_s = timestamp_s;
    snapshot->soc_percent = gauge->output_soc_percent;
    snapshot->remaining_mah = gauge->remaining_mah;
    snapshot->usable_capacity_mah = gauge->config.usable_capacity_mah;
    snapshot->voltage_v = gauge->voltage_v;
    snapshot->temperature_c = gauge->temperature_c;
    snapshot->charging = gauge->state == QM_BATTERY_GAUGE_STATE_CHARGING ?
                         QM_TRUE : QM_FALSE;
    snapshot->full = gauge->full;
    snapshot->valid = QM_TRUE;
    return QM_EOK;
}

static qm_bool gauge_snapshot_is_valid(
    const qm_battery_gauge_snapshot_t *snapshot)
{
    if (!snapshot ||
        snapshot->magic != QM_BATTERY_GAUGE_SNAPSHOT_MAGIC ||
        snapshot->version != QM_BATTERY_GAUGE_SNAPSHOT_VERSION ||
        !snapshot->valid ||
        !gauge_isfinitef(snapshot->soc_percent) ||
        !gauge_isfinitef(snapshot->remaining_mah) ||
        !gauge_isfinitef(snapshot->usable_capacity_mah) ||
        !gauge_isfinitef(snapshot->voltage_v) ||
        !gauge_isfinitef(snapshot->temperature_c) ||
        snapshot->soc_percent < 0.0f ||
        snapshot->soc_percent > 100.0f ||
        snapshot->usable_capacity_mah <= 0.0f ||
        snapshot->voltage_v < GAUGE_MIN_VOLTAGE_V ||
        snapshot->voltage_v > GAUGE_MAX_VOLTAGE_V) {
        return QM_FALSE;
    }

    return QM_TRUE;
}

qm_err_t qm_battery_gauge_save_snapshot(
    const qm_battery_gauge_t *gauge,
    const char *key,
    qm_u32 seq,
    qm_u32 timestamp_s)
{
#if CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT
    qm_battery_gauge_snapshot_t snapshot;
    const char *store_key = key ? key : QM_BATTERY_GAUGE_DEFAULT_SNAPSHOT_KEY;
    qm_err_t err;

    err = qm_battery_gauge_make_snapshot(gauge, &snapshot, seq, timestamp_s);
    if (err != QM_EOK) {
        return err;
    }

    return qm_kv_set(store_key, &snapshot, (int)sizeof(snapshot), 1);
#else
    (void)gauge;
    (void)key;
    (void)seq;
    (void)timestamp_s;
    return -QM_ENOSYS;
#endif
}

qm_err_t qm_battery_gauge_load_snapshot(
    const char *key,
    qm_battery_gauge_snapshot_t *snapshot)
{
#if CONFIG_QM_BATTERY_GAUGE_STORAGE_SUPPORT
    qm_battery_gauge_snapshot_t loaded_snapshot;
    const char *store_key = key ? key : QM_BATTERY_GAUGE_DEFAULT_SNAPSHOT_KEY;
    int len = (int)sizeof(loaded_snapshot);
    qm_err_t err;

    if (!snapshot) {
        return -QM_EINVAL;
    }

    err = qm_kv_get(store_key, &loaded_snapshot, &len);
    if (err != QM_EOK) {
        return err;
    }
    if (len != (int)sizeof(loaded_snapshot) ||
        !gauge_snapshot_is_valid(&loaded_snapshot)) {
        return -QM_EINVAL;
    }

    *snapshot = loaded_snapshot;
    return QM_EOK;
#else
    (void)key;
    (void)snapshot;
    return -QM_ENOSYS;
#endif
}

qm_err_t qm_battery_gauge_estimate_restore_soc(
    const qm_battery_gauge_config_t *config,
    const qm_battery_gauge_restore_config_t *restore,
    qm_battery_gauge_restore_result_t *result)
{
    qm_battery_gauge_restore_result_t zero_result = { 0 };
    qm_bool snapshot_used;
    qm_float wake_current_ma;
    qm_float wake_temperature_c;
    qm_float compensated_voltage_v;
    qm_float voltage_soc;
    qm_float saved_soc;
    qm_float saved_after_drain;
    qm_float offline_drain_mah = 0.0f;
    qm_float voltage_weight;

    if (!config || !restore || !result || !gauge_config_valid(config) ||
        !gauge_isfinitef(restore->wake_voltage_v) ||
        restore->wake_voltage_v < GAUGE_MIN_VOLTAGE_V ||
        restore->wake_voltage_v > GAUGE_MAX_VOLTAGE_V ||
        !gauge_isfinitef(restore->wake_current_ma) ||
        !gauge_isfinitef(restore->offline_current_ma) ||
        restore->offline_current_ma < 0.0f ||
        (restore->has_temperature &&
         !gauge_isfinitef(restore->wake_temperature_c)) ||
        (restore->offline_time_valid &&
         (!gauge_isfinitef(restore->offline_time_s) ||
          restore->offline_time_s < 0.0f))) {
        return -QM_EINVAL;
    }

    wake_current_ma =
        gauge_absf(restore->wake_current_ma) <= GAUGE_CURRENT_DEADBAND_MA ?
        0.0f : restore->wake_current_ma;
    if (!gauge_sample_direction_valid(restore->charging, wake_current_ma)) {
        return -QM_EINVAL;
    }

    *result = zero_result;
    wake_temperature_c = restore->has_temperature ?
                         restore->wake_temperature_c :
                         config->default_temperature_c;
    compensated_voltage_v = config->load_compensation_enable ?
                            restore->wake_voltage_v -
                            (wake_current_ma / 1000.0f) *
                            config->internal_resistance_ohm :
                            restore->wake_voltage_v;
    /* Restore uses the same load-compensated voltage path as live updates. */
    voltage_soc = gauge_soc_from_voltage(config,
                                         compensated_voltage_v,
                                         wake_temperature_c);
    snapshot_used = gauge_snapshot_is_valid(restore->snapshot);
    saved_soc = snapshot_used ? restore->snapshot->soc_percent : voltage_soc;
    saved_after_drain = saved_soc;

    if (snapshot_used && restore->offline_time_valid) {
        qm_float capacity = restore->snapshot->usable_capacity_mah > 0.0f ?
                            restore->snapshot->usable_capacity_mah :
                            config->usable_capacity_mah;
        offline_drain_mah = restore->offline_current_ma *
                            restore->offline_time_s / 3600.0f;
        saved_after_drain -= offline_drain_mah * 100.0f / capacity;
        saved_after_drain = gauge_clampf(saved_after_drain, 0.0f, 100.0f);
    }

    voltage_weight = gauge_restore_voltage_weight(snapshot_used,
                                                  restore->offline_time_valid,
                                                  restore->offline_time_s);

    result->final_soc_percent =
        saved_after_drain * (1.0f - voltage_weight) +
        voltage_soc * voltage_weight;
    result->final_soc_percent = gauge_clampf(result->final_soc_percent,
                                             0.0f,
                                             100.0f);

    if (restore->charge_full &&
        restore->charging &&
        restore->wake_voltage_v >= config->full_voltage_v) {
        result->final_soc_percent = 100.0f;
        result->charge_full_used = QM_TRUE;
    }

    result->saved_soc_percent = saved_soc;
    result->saved_after_drain_percent = saved_after_drain;
    result->voltage_soc_percent = voltage_soc;
    result->offline_drain_mah = offline_drain_mah;
    result->voltage_weight = voltage_weight;
    result->snapshot_used = snapshot_used;
    result->voltage_used = voltage_weight > 0.0f ? QM_TRUE : QM_FALSE;
    return QM_EOK;
}

qm_err_t qm_battery_gauge_update_sample(
    qm_battery_gauge_t *gauge,
    const qm_battery_gauge_sample_t *sample)
{
    qm_float dt_s;
    qm_float current_ma;
    qm_bool current_valid;
    qm_float temperature_c;
    qm_float voltage_soc;
    qm_float output_alpha;

    if (!gauge || !sample ||
        !gauge_isfinitef(sample->voltage_v) ||
        !gauge_isfinitef(sample->dt_s)) {
        return -QM_EINVAL;
    }
    if ((gauge->config.current_mode == QM_BATTERY_GAUGE_CURRENT_FULL ||
         sample->current_valid) &&
        !gauge_isfinitef(sample->current_ma)) {
        return -QM_EINVAL;
    }
    if (sample->voltage_v < GAUGE_MIN_VOLTAGE_V ||
        sample->voltage_v > GAUGE_MAX_VOLTAGE_V ||
        sample->dt_s <= 0.0f) {
        return -QM_EINVAL;
    }

    dt_s = gauge_clampf(sample->dt_s, GAUGE_DT_MIN_S,
                        gauge->config.max_update_dt_s > 0.0f ?
                        gauge->config.max_update_dt_s : GAUGE_DEFAULT_MAX_DT_S);
    current_valid = gauge_sample_current_valid(&gauge->config, sample);
    current_ma = current_valid && gauge_absf(sample->current_ma) > GAUGE_CURRENT_DEADBAND_MA ?
                 sample->current_ma : 0.0f;
    if (current_valid && !gauge_sample_direction_valid(sample->charging, current_ma)) {
        return -QM_EINVAL;
    }

    temperature_c = sample->has_temperature &&
                    gauge_isfinitef(sample->temperature_c) ?
                    sample->temperature_c :
                    gauge->config.default_temperature_c;

    gauge->voltage_v = sample->voltage_v;
    gauge->current_ma = current_ma;
    gauge->current_valid = current_valid;
    gauge->temperature_c = temperature_c;
    gauge->runtime_s += dt_s;

    if (gauge->config.load_compensation_enable && current_valid) {
        /* Estimate OCV from terminal voltage and I*R drop before table lookup. */
        gauge->compensated_voltage_v =
            sample->voltage_v -
            (current_ma / 1000.0f) * gauge->config.internal_resistance_ohm;
    } else {
        gauge->compensated_voltage_v = sample->voltage_v;
    }

    voltage_soc = gauge_soc_from_voltage(&gauge->config,
                                         gauge->compensated_voltage_v,
                                         temperature_c);

    if (!gauge->soc_seeded) {
        gauge_force_soc(gauge, voltage_soc);
        gauge->soc_seeded = QM_TRUE;
    }

    if (current_valid) {
        gauge_apply_coulomb_count(gauge, current_ma, dt_s);
    }
    gauge_update_relaxation(gauge, current_valid, current_ma, dt_s);
    gauge_apply_soc_correction(gauge, voltage_soc, sample->charging, dt_s);
    gauge_apply_voltage_anchors(gauge, sample->charging, dt_s);
    gauge_apply_full_anchor(gauge, sample->charging, sample->charge_full,
                            current_valid, current_ma, dt_s);

    output_alpha = gauge_alpha_from_tau(dt_s, GAUGE_OUTPUT_TAU_S);
    gauge->output_soc_percent = gauge_blendf(gauge->output_soc_percent,
                                             gauge->soc_percent,
                                             output_alpha);
    gauge->output_soc_percent = gauge_clampf(gauge->output_soc_percent,
                                             0.0f, 100.0f);
    gauge_update_flags(gauge, sample->charging);
    gauge->initialized = QM_TRUE;
    return QM_EOK;
}

qm_err_t qm_battery_gauge_get_result(const qm_battery_gauge_t *gauge,
                                     qm_battery_gauge_result_t *result)
{
    qm_battery_gauge_result_t zero_result = { 0 };

    if (!gauge || !result) {
        return -QM_EINVAL;
    }

    *result = zero_result;
    result->soc_percent = gauge->output_soc_percent;
    result->raw_soc_percent = gauge->soc_percent;
    result->soh_percent = gauge->config.design_capacity_mah > 0.0f ?
                          gauge->config.usable_capacity_mah * 100.0f /
                          gauge->config.design_capacity_mah : 0.0f;
    result->remaining_mah = gauge->remaining_mah;
    result->usable_capacity_mah = gauge->config.usable_capacity_mah;
    result->voltage_v = gauge->voltage_v;
    result->current_ma = gauge->current_ma;
    result->current_valid = gauge->current_valid;
    result->compensated_voltage_v = gauge->compensated_voltage_v;
    result->temperature_c = gauge->temperature_c;
    result->charged_mah = gauge->charged_mah;
    result->discharged_mah = gauge->discharged_mah;
    result->runtime_s = gauge->runtime_s;
    result->state = gauge->state;
    result->initialized = gauge->initialized;
    result->relaxed = gauge->relaxed;
    result->low = gauge->low;
    result->empty = gauge->empty;
    result->full = gauge->full;
    return QM_EOK;
}
