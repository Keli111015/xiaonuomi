#include "qm_battery.h"
#include "qm_battery_filter.h"

#include "qm.h"

#include <string.h>

#ifndef CONFIG_QM_BATTERY_DEFAULT_TEMP_C
#define CONFIG_QM_BATTERY_DEFAULT_TEMP_C 25
#endif

#ifndef CONFIG_QM_BATTERY_TEMP_COMP_ENABLE
#define CONFIG_QM_BATTERY_TEMP_COMP_ENABLE 0
#endif

#ifndef CONFIG_QM_BATTERY_SOH_UPDATE_ENABLE
#define CONFIG_QM_BATTERY_SOH_UPDATE_ENABLE 1
#endif

#ifndef CONFIG_QM_BATTERY_R_NOMINAL_MOHM
#define CONFIG_QM_BATTERY_R_NOMINAL_MOHM 60
#endif

#ifndef CONFIG_QM_BATTERY_FULL_ANCHOR_MV
#define CONFIG_QM_BATTERY_FULL_ANCHOR_MV 4180
#endif

#ifndef CONFIG_QM_BATTERY_EMPTY_ANCHOR_MV
#define CONFIG_QM_BATTERY_EMPTY_ANCHOR_MV 3300
#endif

#ifndef CONFIG_QM_BATTERY_CHARGE_SETTLE_S
#define CONFIG_QM_BATTERY_CHARGE_SETTLE_S 30
#endif

#ifndef CONFIG_QM_BATTERY_CHARGE_SETTLE_RM_SCALE
#define CONFIG_QM_BATTERY_CHARGE_SETTLE_RM_SCALE 6
#endif

#ifndef CONFIG_QM_BATTERY_RELAX_CONFIRM_S
#define CONFIG_QM_BATTERY_RELAX_CONFIRM_S 60
#endif

#ifndef CONFIG_QM_BATTERY_INITIAL_SOC_MAX_AGE_H
#define CONFIG_QM_BATTERY_INITIAL_SOC_MAX_AGE_H 24
#endif

#ifndef CONFIG_QM_BATTERY_STANDBY_DRAIN_PCT_PER_DAY
#define CONFIG_QM_BATTERY_STANDBY_DRAIN_PCT_PER_DAY 2
#endif

#ifndef CONFIG_QM_BATTERY_CHARGE_FULL_CONFIRM_S
#define CONFIG_QM_BATTERY_CHARGE_FULL_CONFIRM_S 8
#endif

#ifndef CONFIG_QM_BATTERY_CHARGE_FULL_MIN_MV
#define CONFIG_QM_BATTERY_CHARGE_FULL_MIN_MV 4050
#endif

#ifndef CONFIG_QM_BATTERY_LEARNED_FULL_MIN_MV
#define CONFIG_QM_BATTERY_LEARNED_FULL_MIN_MV 4050
#endif

#ifndef CONFIG_QM_BATTERY_LEARNED_FULL_MAX_MV
#define CONFIG_QM_BATTERY_LEARNED_FULL_MAX_MV 4200
#endif

#ifndef CONFIG_QM_BATTERY_LEARNED_FULL_BLEND_PERMILLE
#define CONFIG_QM_BATTERY_LEARNED_FULL_BLEND_PERMILLE 80
#endif

#ifndef CONFIG_QM_BATTERY_FULL_GUARD_S
#define CONFIG_QM_BATTERY_FULL_GUARD_S 1800
#endif

#ifndef CONFIG_QM_BATTERY_FULL_GUARD_MIN_PERCENT
#define CONFIG_QM_BATTERY_FULL_GUARD_MIN_PERCENT 98
#endif

#define RELAX_FAST_TAU_S      8.0f
#define RELAX_SLOW_TAU_S      90.0f
#define RELAX_SOC_BLEND       0.035f
#define ANCHOR_BLEND_FAST     0.20f
#define ANCHOR_BLEND_SLOW     0.05f
#define DVDT_LIMIT_VPS        0.030f
#define SECONDS_PER_DAY       86400.0f

/**
 * @brief Return whether the persisted SOC seed is older than the configured trust window.
 *
 * @param[in] handle Estimator handle.
 * @return QM_TRUE when the seed should be verified before being trusted.
 */
static qm_bool initial_soc_seed_expired(const qm_battery_t *handle)
{
    if (!handle->config.initial_soc_valid) {
        return QM_FALSE;
    }
    if (handle->config.initial_soc_max_age_s <= 0.0f) {
        return QM_FALSE;
    }
    return handle->config.initial_soc_age_s > handle->config.initial_soc_max_age_s ?
           QM_TRUE : QM_FALSE;
}

/**
 * @brief Update relaxation detection state with fast and slow voltage filters.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Current measured battery voltage in volts.
 * @param[in] dt Sample interval in seconds.
 */
static void update_relaxation(qm_battery_t *handle, qm_float voltage, qm_float dt)
{
    qm_float dt_limited = qm_battery_clampf(dt, QM_BATTERY_DT_MIN_S, QM_BATTERY_DT_MAX_S);
    qm_float fast_a = dt_limited / (RELAX_FAST_TAU_S + dt_limited);
    qm_float slow_a = dt_limited / (RELAX_SLOW_TAU_S + dt_limited);
    qm_float dvdt;

    handle->fast_voltage = qm_battery_blendf(handle->fast_voltage, voltage, fast_a);
    handle->slow_voltage = qm_battery_blendf(handle->slow_voltage, voltage, slow_a);
    dvdt = (voltage - handle->prev_voltage) / dt_limited;
    handle->filtered_dvdt = qm_battery_blendf(handle->filtered_dvdt, dvdt, fast_a);

    if (qm_battery_absf(handle->fast_voltage - handle->slow_voltage) < handle->config.relax_delta_v &&
        qm_battery_absf(handle->filtered_dvdt) < handle->config.relax_dvdt_vps) {
        handle->relax_time_s += dt_limited;
        if (handle->relax_time_s >= handle->config.relax_confirm_s) {
            handle->relaxed = QM_TRUE;
        }
    } else {
        handle->relax_time_s = 0.0f;
        handle->relaxed = QM_FALSE;
    }
}

/**
 * @brief Update charging state transition settling state.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] charging Current charging state.
 * @param[in] dt Sample interval in seconds.
 */
static void update_charge_settling(qm_battery_t *handle, qm_bool charging, qm_float dt)
{
    if (charging != handle->last_charging) {
        handle->charge_settle_left_s = handle->config.charge_settle_s > 0.0f ?
                                       handle->config.charge_settle_s : 0.0f;
        handle->relax_time_s = 0.0f;
        handle->relaxed = QM_FALSE;
    } else if (handle->charge_settle_left_s > 0.0f) {
        handle->charge_settle_left_s -= dt;
        if (handle->charge_settle_left_s < 0.0f) {
            handle->charge_settle_left_s = 0.0f;
        }
    }
    if (handle->charge_settle_left_s > 0.0f) {
        handle->relax_time_s = 0.0f;
        handle->relaxed = QM_FALSE;
    }
    handle->last_charging = charging;
}

/**
 * @brief Return the active full-charge voltage anchor.
 *
 * @param[in] handle Estimator handle.
 * @return Learned full voltage when available, otherwise configured full anchor.
 */
static qm_float effective_full_anchor_v(const qm_battery_t *handle)
{
    if (handle->learned_full_valid) {
        return handle->learned_full_v;
    }
    if (handle->config.learned_full_valid) {
        return handle->config.learned_full_v;
    }
    return handle->config.full_anchor_v;
}

/**
 * @brief Return the active lower SOC bound while recent-full guard is enabled.
 *
 * @param[in] handle Estimator handle.
 * @return Guard SOC floor in percent.
 */
static qm_float full_guard_floor_percent(const qm_battery_t *handle)
{
    return qm_battery_clampf(handle->config.full_guard_min_percent,
                             handle->config.soc_min_percent,
                             handle->config.soc_max_percent);
}

/**
 * @brief Decrease recent-full protection time.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] charging QM_TRUE when the battery is charging.
 * @param[in] dt Sample interval in seconds.
 */
static void update_full_guard(qm_battery_t *handle, qm_bool charging, qm_float dt)
{
    if (charging) {
        return;
    }
    if (handle->full_guard_left_s <= 0.0f) {
        handle->full_guard_left_s = 0.0f;
        handle->full_guard_active = QM_FALSE;
        return;
    }
    handle->full_guard_left_s -= dt;
    if (handle->full_guard_left_s <= 0.0f) {
        handle->full_guard_left_s = 0.0f;
        handle->full_guard_active = QM_FALSE;
    }
}

/**
 * @brief Protect SOC from immediate downward correction after confirmed full.
 *
 * @param[in,out] handle Estimator handle.
 */
static void apply_full_guard_floor(qm_battery_t *handle)
{
    qm_float floor_soc;

    if (!handle->full_guard_active) {
        return;
    }
    floor_soc = full_guard_floor_percent(handle);
    if (handle->x[IDX_SOC] < floor_soc) {
        handle->x[IDX_SOC] = floor_soc;
    }
    if (handle->output_soc < floor_soc) {
        handle->output_soc = floor_soc;
    }
}

/**
 * @brief Learn a full-charge voltage from a confirmed charger-full event.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Current measured battery voltage in volts.
 */
static void learn_full_voltage(qm_battery_t *handle, qm_float voltage)
{
    qm_float learned = qm_battery_clampf(voltage,
                                         handle->config.learned_full_min_v,
                                         handle->config.learned_full_max_v);
    if (!handle->learned_full_valid) {
        handle->learned_full_v = learned;
        handle->learned_full_valid = QM_TRUE;
    } else {
        handle->learned_full_v = qm_battery_blendf(handle->learned_full_v,
                                                   learned,
                                                   handle->config.learned_full_blend);
    }
}

/**
 * @brief Update charger-full debounce state and apply a high-confidence SOC anchor.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Current measured battery voltage in volts.
 * @param[in] charging QM_TRUE when the battery is charging.
 * @param[in] charge_full QM_TRUE when charger IC reports full.
 * @param[in] dt Sample interval in seconds.
 */
static void apply_charge_full_signal(qm_battery_t *handle,
                                     qm_float voltage,
                                     qm_bool charging,
                                     qm_bool charge_full,
                                     qm_float dt)
{
    qm_bool voltage_ok = voltage >= handle->config.charge_full_min_v ? QM_TRUE : QM_FALSE;
    qm_bool full_valid = charging && charge_full && voltage_ok;
    qm_float confirm_s = handle->config.charge_full_confirm_s > 0.0f ?
                         handle->config.charge_full_confirm_s : 0.0f;

    /* A charger-full GPIO is only trusted after voltage and debounce checks. */
    if (full_valid) {
        handle->charge_full_time_s += dt;
    } else {
        handle->charge_full_time_s = 0.0f;
        handle->charge_full_confirmed = QM_FALSE;
        return;
    }

    if (handle->charge_full_time_s < confirm_s) {
        return;
    }

    if (!handle->charge_full_confirmed) {
        learn_full_voltage(handle, voltage);
    }

    handle->charge_full_confirmed = QM_TRUE;
    /* Keep SOC near full through the immediate unplug voltage drop. */
    handle->full_guard_left_s = handle->config.full_guard_s > 0.0f ?
                                handle->config.full_guard_s : 0.0f;
    handle->full_guard_active = handle->full_guard_left_s > 0.0f ? QM_TRUE : QM_FALSE;
    handle->charge_settle_left_s = 0.0f;
    handle->relax_time_s = 0.0f;
    handle->relaxed = QM_FALSE;

    handle->x[IDX_SOC] = handle->config.soc_max_percent;
    handle->output_soc = handle->config.soc_max_percent;
    handle->p[IDX_SOC][IDX_SOC] *= 0.05f;
    qm_battery_filter_clamp_state(handle, handle->x);
    apply_full_guard_floor(handle);
    qm_battery_filter_stabilize_covariance(handle);
}

/**
 * @brief Apply an OCV upper-bound check to an old persisted SOC seed.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Current measured battery voltage in volts.
 * @param[in] temperature_c Temperature used for OCV lookup.
 * @param[in] has_temperature QM_TRUE if temperature_c is measured.
 * @param[in] charging QM_TRUE when the battery is charging.
 */
static void verify_initial_soc_seed(qm_battery_t *handle,
                                    qm_float voltage,
                                    qm_float temperature_c,
                                    qm_bool has_temperature,
                                    qm_bool charging)
{
    qm_float ocv_soc;

    if (!initial_soc_seed_expired(handle)) {
        return;
    }

    if (charging) {
        return;
    }

    ocv_soc = qm_battery_ocv_soc_from_voltage(handle->config.ocv_table,
                                              handle->config.ocv_table_count,
                                              voltage,
                                              temperature_c,
                                              handle->x[IDX_SOH],
                                              has_temperature && handle->config.temp_comp_enable);
    if (handle->x[IDX_SOC] > ocv_soc) {
        handle->x[IDX_SOC] = ocv_soc;
    }
}

/**
 * @brief Blend SOC toward OCV-derived SOC while the cell is relaxed.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Current measured battery voltage in volts.
 * @param[in] temperature_c Temperature used for OCV lookup.
 * @param[in] has_temperature QM_TRUE if temperature_c is measured.
 * @param[in] charging QM_TRUE when the battery is charging.
 */
static void apply_ocv_relax_anchor(qm_battery_t *handle,
                                   qm_float voltage,
                                   qm_float temperature_c,
                                   qm_bool has_temperature,
                                   qm_bool charging)
{
    qm_float ocv_soc;

    if (!handle->relaxed) {
        return;
    }

    if (charging) {
        return;
    }

    if (handle->charge_settle_left_s > 0.0f) {
        return;
    }

    ocv_soc = qm_battery_ocv_soc_from_voltage(handle->config.ocv_table,
                                              handle->config.ocv_table_count,
                                              voltage,
                                              temperature_c,
                                              handle->x[IDX_SOH],
                                              has_temperature && handle->config.temp_comp_enable);
    handle->x[IDX_SOC] = qm_battery_blendf(handle->x[IDX_SOC], ocv_soc, RELAX_SOC_BLEND);
    apply_full_guard_floor(handle);
    handle->p[IDX_SOC][IDX_SOC] *= 0.92f;
    qm_battery_filter_stabilize_covariance(handle);
}

/**
 * @brief Apply full and empty voltage anchors to suppress long-term drift.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Current measured battery voltage in volts.
 * @param[in] charging QM_TRUE when the battery is charging.
 */
static void apply_voltage_anchors(qm_battery_t *handle, qm_float voltage, qm_bool charging)
{
    qm_float alpha = handle->relaxed ? ANCHOR_BLEND_FAST : ANCHOR_BLEND_SLOW;

    if (handle->charge_settle_left_s > 0.0f) {
        return;
    }

    if (charging && voltage >= effective_full_anchor_v(handle)) {
        handle->x[IDX_SOC] = qm_battery_blendf(handle->x[IDX_SOC],
                                    handle->config.soc_max_percent,
                                    alpha);
        handle->p[IDX_SOC][IDX_SOC] *= 0.75f;
    } else if (!charging && voltage <= handle->config.empty_anchor_v) {
        handle->x[IDX_SOC] = qm_battery_blendf(handle->x[IDX_SOC],
                                    handle->config.soc_min_percent,
                                    alpha);
        handle->p[IDX_SOC][IDX_SOC] *= 0.75f;
    }

    qm_battery_filter_clamp_state(handle, handle->x);
    apply_full_guard_floor(handle);
    qm_battery_filter_stabilize_covariance(handle);
}

/**
 * @brief Slowly blend SOH toward the value inferred from internal resistance.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] dt Sample interval in seconds.
 * @param[in] has_temperature QM_TRUE if a real temperature value is available.
 */
static void update_soh_from_resistance(qm_battery_t *handle,
                                       qm_float dt,
                                       qm_bool has_temperature)
{
    qm_float soh_from_r;
    qm_float alpha;

    if (!handle->config.soh_update_enable) {
        return;
    }
    if (handle->charge_settle_left_s > 0.0f) {
        return;
    }

    soh_from_r = handle->config.r_nominal_ohm / handle->x[IDX_R];
    soh_from_r = qm_battery_clampf(soh_from_r,
                              handle->config.soh_min_ratio,
                              handle->config.soh_max_ratio);
    alpha = qm_battery_clampf(dt / 3600.0f, 0.00002f, 0.0020f);
    if (!has_temperature) {
        alpha *= 0.25f;
    }
    handle->x[IDX_SOH] = qm_battery_blendf(handle->x[IDX_SOH], soh_from_r, alpha);
    qm_battery_filter_clamp_state(handle, handle->x);
}

/**
 * @brief Return whether a calibration float is finite and positive.
 *
 * @param[in] value Value to validate.
 * @return QM_TRUE when value is finite and greater than zero.
 */
static qm_bool calibration_positive(qm_float value)
{
    return qm_battery_isfinitef(value) && value > 0.0f ? QM_TRUE : QM_FALSE;
}

/**
 * @brief Fill estimator configuration with conservative default parameters.
 *
 * @param[out] config Destination configuration.
 */
void qm_battery_default_config(qm_battery_config_t *config)
{
    int count = 0;

    if (!config) {
        return;
    }

    memset(config, 0, sizeof(*config));
    config->k1_soc_per_vps = -4.0f;
    config->k2_current_per_vps = -18.0f;
    config->default_temperature_c = (qm_float)CONFIG_QM_BATTERY_DEFAULT_TEMP_C;
    config->initial_soc_percent = 50.0f;
    config->initial_soc_age_s = 0.0f;
    config->initial_soc_max_age_s = (qm_float)CONFIG_QM_BATTERY_INITIAL_SOC_MAX_AGE_H * 3600.0f;
    config->standby_drain_percent_per_day = (qm_float)CONFIG_QM_BATTERY_STANDBY_DRAIN_PCT_PER_DAY;
    config->temp_comp_enable = CONFIG_QM_BATTERY_TEMP_COMP_ENABLE ? QM_TRUE : QM_FALSE;
    config->soh_update_enable = CONFIG_QM_BATTERY_SOH_UPDATE_ENABLE ? QM_TRUE : QM_FALSE;
    config->initial_soc_valid = QM_FALSE;
    config->learned_full_valid = QM_FALSE;
    config->adc_scale_v_per_count = 0.001f;
    config->adc_offset_v = 0.0f;
    config->adc_divider_ratio = 1.0f;
    config->soc_min_percent = 0.0f;
    config->soc_max_percent = 100.0f;
    config->soh_min_ratio = 0.50f;
    config->soh_max_ratio = 1.00f;
    config->r_min_ohm = 0.010f;
    config->r_max_ohm = 0.500f;
    /* Production should replace lab-dependent defaults through calibration import. */
    config->r_nominal_ohm = (qm_float)CONFIG_QM_BATTERY_R_NOMINAL_MOHM / 1000.0f;
    config->q_soc = 0.0200f;
    config->q_soh = 0.00000002f;
    config->q_r = 0.00000020f;
    config->rm_base_v2 = 0.000025f;
    config->rm_relax_scale = 0.35f;
    config->rm_dynamic_scale = 4.00f;
    config->rm_slope_gain = 0.040f;
    /* Anchors below are safe fallbacks until qm_battery_apply_calibration() is used. */
    config->full_anchor_v = (qm_float)CONFIG_QM_BATTERY_FULL_ANCHOR_MV / 1000.0f;
    config->empty_anchor_v = (qm_float)CONFIG_QM_BATTERY_EMPTY_ANCHOR_MV / 1000.0f;
    config->charge_full_confirm_s = (qm_float)CONFIG_QM_BATTERY_CHARGE_FULL_CONFIRM_S;
    config->charge_full_min_v = (qm_float)CONFIG_QM_BATTERY_CHARGE_FULL_MIN_MV / 1000.0f;
    config->learned_full_v = config->full_anchor_v;
    config->learned_full_min_v = (qm_float)CONFIG_QM_BATTERY_LEARNED_FULL_MIN_MV / 1000.0f;
    config->learned_full_max_v = (qm_float)CONFIG_QM_BATTERY_LEARNED_FULL_MAX_MV / 1000.0f;
    config->learned_full_blend =
        (qm_float)CONFIG_QM_BATTERY_LEARNED_FULL_BLEND_PERMILLE / 1000.0f;
    config->full_guard_s = (qm_float)CONFIG_QM_BATTERY_FULL_GUARD_S;
    config->full_guard_min_percent = (qm_float)CONFIG_QM_BATTERY_FULL_GUARD_MIN_PERCENT;
    config->charge_settle_s = (qm_float)CONFIG_QM_BATTERY_CHARGE_SETTLE_S;
    config->charge_settle_rm_scale = (qm_float)CONFIG_QM_BATTERY_CHARGE_SETTLE_RM_SCALE;
    config->relax_delta_v = 0.006f;
    config->relax_dvdt_vps = 0.00025f;
    config->relax_confirm_s = (qm_float)CONFIG_QM_BATTERY_RELAX_CONFIRM_S;
    config->ocv_table = qm_battery_ocv_default_table(&count);
    config->ocv_table_count = count;
}

/**
 * @brief Apply required laboratory calibration values to a config.
 *
 * @param[in,out] config Destination config initialized by qm_battery_default_config().
 * @param[in] calibration Laboratory calibration values to import.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_apply_calibration(qm_battery_config_t *config,
                                 const qm_battery_calibration_t *calibration)
{
    if (!config || !calibration) {
        return -QM_EINVAL;
    }
    if (!calibration->ocv_table || calibration->ocv_table_count < 2) {
        return -QM_EINVAL;
    }
    if (!calibration_positive(calibration->adc_scale_v_per_count) ||
        !qm_battery_isfinitef(calibration->adc_offset_v) ||
        !calibration_positive(calibration->adc_divider_ratio) ||
        !calibration_positive(calibration->r_nominal_ohm) ||
        !calibration_positive(calibration->full_anchor_v) ||
        !calibration_positive(calibration->empty_anchor_v) ||
        !calibration_positive(calibration->charge_full_min_v)) {
        return -QM_EINVAL;
    }
    if (calibration->empty_anchor_v >= calibration->full_anchor_v) {
        return -QM_EINVAL;
    }

    /* Only import measured cell/board parameters; strategy knobs stay in cfg. */
    config->ocv_table = calibration->ocv_table;
    config->ocv_table_count = calibration->ocv_table_count;
    config->adc_scale_v_per_count = calibration->adc_scale_v_per_count;
    config->adc_offset_v = calibration->adc_offset_v;
    config->adc_divider_ratio = calibration->adc_divider_ratio;
    config->r_nominal_ohm = calibration->r_nominal_ohm;
    config->full_anchor_v = calibration->full_anchor_v;
    config->empty_anchor_v = calibration->empty_anchor_v;
    config->charge_full_min_v = calibration->charge_full_min_v;
    return QM_EOK;
}

/**
 * @brief Reset runtime state and covariance while preserving configuration.
 *
 * @param[in,out] handle Estimator handle.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_reset(qm_battery_t *handle)
{
    qm_battery_config_t config;

    if (!handle) {
        return -QM_EINVAL;
    }

    config = handle->config;
    memset(handle, 0, sizeof(*handle));
    handle->config = config;
    if (handle->config.initial_soc_valid) {
        qm_float offline_drain = 0.0f;
        /* Seed boot SOC from storage, then account for product-off drain. */
        if (handle->config.initial_soc_age_s > 0.0f &&
            handle->config.standby_drain_percent_per_day > 0.0f) {
            offline_drain = handle->config.initial_soc_age_s *
                            handle->config.standby_drain_percent_per_day /
                            SECONDS_PER_DAY;
        }
        handle->x[IDX_SOC] = qm_battery_clampf(handle->config.initial_soc_percent - offline_drain,
                                               handle->config.soc_min_percent,
                                               handle->config.soc_max_percent);
    } else {
        handle->x[IDX_SOC] = 50.0f;
    }
    handle->x[IDX_SOH] = 1.0f;
    handle->x[IDX_R] = handle->config.r_nominal_ohm;
    if (handle->config.learned_full_valid) {
        handle->learned_full_v = qm_battery_clampf(handle->config.learned_full_v,
                                                   handle->config.learned_full_min_v,
                                                   handle->config.learned_full_max_v);
        handle->learned_full_valid = QM_TRUE;
    }
    qm_battery_filter_covariance_init(handle->p);
    handle->output_soc = handle->x[IDX_SOC];
    handle->output_soh = handle->x[IDX_SOH] * 100.0f;
    return QM_EOK;
}

/**
 * @brief Initialize estimator state and copy user configuration.
 *
 * @param[out] handle Estimator handle.
 * @param[in] config Optional user configuration.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_init(qm_battery_t *handle,
                        const qm_battery_config_t *config)
{
    qm_battery_config_t local_config;

    if (!handle) {
        return -QM_EINVAL;
    }

    if (config) {
        local_config = *config;
        if (!local_config.ocv_table || local_config.ocv_table_count < 2) {
            int count = 0;
            local_config.ocv_table = qm_battery_ocv_default_table(&count);
            local_config.ocv_table_count = count;
        }
    } else {
        qm_battery_default_config(&local_config);
    }

    memset(handle, 0, sizeof(*handle));
    handle->config = local_config;
    return qm_battery_reset(handle);
}

/**
 * @brief Run one voltage-only estimator update cycle.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Current measured battery voltage in volts.
 * @param[in] temperature Temperature in degrees Celsius when available.
 * @param[in] has_temperature QM_TRUE if temperature is measured.
 * @param[in] charging QM_TRUE when the battery is charging.
 * @param[in] charge_full QM_TRUE when charger IC reports full.
 * @param[in] dt Sample interval in seconds.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_update_voltage(qm_battery_t *handle,
                              qm_float voltage,
                              qm_float temperature,
                              qm_bool has_temperature,
                              qm_bool charging,
                              qm_bool charge_full,
                              qm_float dt)
{
    qm_float dt_limited;
    qm_float dvdt;
    qm_float temp;

    if (!handle || !qm_battery_isfinitef(voltage) || !qm_battery_isfinitef(dt)) {
        return -QM_EINVAL;
    }

    temp = has_temperature && qm_battery_isfinitef(temperature) ?
           temperature : handle->config.default_temperature_c;
    dt_limited = qm_battery_clampf(dt, QM_BATTERY_DT_MIN_S, QM_BATTERY_DT_MAX_S);

    if (!handle->initialized) {
        if (!handle->config.initial_soc_valid && !charging) {
            /* First non-charging voltage is close enough to OCV to seed SOC. */
            handle->x[IDX_SOC] = qm_battery_ocv_soc_from_voltage(handle->config.ocv_table,
                                                                 handle->config.ocv_table_count,
                                                                 voltage,
                                                                 temp,
                                                                 handle->x[IDX_SOH],
                                                                 has_temperature && handle->config.temp_comp_enable);
        } else if (handle->config.initial_soc_valid) {
            /* Old saved SOC is only checked downward when the cell is not charging. */
            verify_initial_soc_seed(handle, voltage, temp, has_temperature, charging);
        }
        handle->prev_voltage = voltage;
        handle->fast_voltage = voltage;
        handle->slow_voltage = voltage;
        handle->filtered_dvdt = 0.0f;
        if (charging) {
            handle->charge_settle_left_s = handle->config.charge_settle_s > 0.0f ?
                                           handle->config.charge_settle_s : 0.0f;
        }
        handle->output_soc = handle->x[IDX_SOC];
        handle->output_soh = handle->x[IDX_SOH] * 100.0f;
        handle->last_voltage = voltage;
        handle->last_dvdt = 0.0f;
        handle->last_charging = charging;
        handle->initialized = QM_TRUE;
        apply_charge_full_signal(handle, voltage, charging, charge_full, dt_limited);
        return QM_EOK;
    }

    dvdt = (voltage - handle->prev_voltage) / dt_limited;
    dvdt = qm_battery_clampf(dvdt, -DVDT_LIMIT_VPS, DVDT_LIMIT_VPS);

    /* Update order keeps transient voltage changes from immediately overriding anchors. */
    update_full_guard(handle, charging, dt_limited);
    update_relaxation(handle, voltage, dt_limited);
    update_charge_settling(handle, charging, dt_limited);
    qm_battery_filter_predict(handle, dvdt, charging, dt_limited);
    qm_battery_filter_update(handle, voltage, temp, dvdt, has_temperature);
    apply_ocv_relax_anchor(handle, voltage, temp, has_temperature, charging);
    apply_voltage_anchors(handle, voltage, charging);
    apply_charge_full_signal(handle, voltage, charging, charge_full, dt_limited);
    update_soh_from_resistance(handle, dt_limited, has_temperature);

    handle->output_soc = qm_battery_blendf(handle->output_soc, handle->x[IDX_SOC], 0.18f);
    handle->output_soh = qm_battery_blendf(handle->output_soh, handle->x[IDX_SOH] * 100.0f, 0.02f);
    apply_full_guard_floor(handle);
    handle->output_soc = qm_battery_clampf(handle->output_soc, 0.0f, 100.0f);
    handle->output_soh = qm_battery_clampf(handle->output_soh, 50.0f, 100.0f);
    handle->prev_voltage = voltage;
    handle->last_voltage = voltage;
    handle->last_dvdt = dvdt;
    return QM_EOK;
}

/**
 * @brief Read ADC once and convert raw sample to battery voltage.
 *
 * @param[in] handle Estimator handle.
 * @param[in] adc ADC device handle.
 * @param[out] voltage Converted battery voltage in volts.
 * @param[in] timeout_ms ADC read timeout in milliseconds.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_read_adc_voltage(qm_battery_t *handle,
                                    qm_adc_dev_t *adc,
                                    qm_float *voltage,
                                    uint32_t timeout_ms)
{
#if defined(CONFIG_QM_ADC_SUPPORT) && CONFIG_QM_ADC_SUPPORT
    int32_t adc_raw = 0;
    int ret;

    if (!handle || !adc || !voltage) {
        return -QM_EINVAL;
    }

    ret = qm_adc_value_get(adc, &adc_raw, timeout_ms);
    if (ret != QM_EOK) {
        return ret;
    }

    *voltage = ((qm_float)adc_raw * handle->config.adc_scale_v_per_count +
                handle->config.adc_offset_v) *
               handle->config.adc_divider_ratio;
    return QM_EOK;
#else
    (void)handle;
    (void)adc;
    (void)voltage;
    (void)timeout_ms;
    return -QM_ENOSYS;
#endif
}

/**
 * @brief Read ADC and feed the converted voltage into the estimator.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] adc ADC device handle.
 * @param[in] charging QM_TRUE when the battery is charging.
 * @param[in] charge_full QM_TRUE when charger IC reports full.
 * @param[in] dt Sample interval in seconds.
 * @param[in] timeout_ms ADC read timeout in milliseconds.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_update_adc(qm_battery_t *handle,
                          qm_adc_dev_t *adc,
                          qm_bool charging,
                          qm_bool charge_full,
                          qm_float dt,
                          uint32_t timeout_ms)
{
    qm_float voltage = 0.0f;
    int ret;

    ret = qm_battery_read_adc_voltage(handle, adc, &voltage, timeout_ms);
    if (ret != QM_EOK) {
        return ret;
    }

    return qm_battery_update_voltage(handle,
                                     voltage,
                                     handle->config.default_temperature_c,
                                     QM_FALSE,
                                     charging,
                                     charge_full,
                                     dt);
}

/**
 * @brief Return the smoothed SOC output.
 *
 * @param[in] handle Estimator handle.
 * @return SOC in percent.
 */
qm_float qm_battery_get_soc(const qm_battery_t *handle)
{
    return handle ? handle->output_soc : 0.0f;
}

/**
 * @brief Return the smoothed SOH output.
 *
 * @param[in] handle Estimator handle.
 * @return SOH in percent.
 */
qm_float qm_battery_get_soh(const qm_battery_t *handle)
{
    return handle ? handle->output_soh : 0.0f;
}

/**
 * @brief Return the latest estimated internal resistance.
 *
 * @param[in] handle Estimator handle.
 * @return Internal resistance in ohms.
 */
qm_float qm_battery_get_internal_resistance(const qm_battery_t *handle)
{
    return handle ? handle->x[IDX_R] : 0.0f;
}

/**
 * @brief Return the current relaxation detection result.
 *
 * @param[in] handle Estimator handle.
 * @return QM_TRUE if relaxed, otherwise QM_FALSE.
 */
qm_bool qm_battery_is_relaxed(const qm_battery_t *handle)
{
    return handle ? handle->relaxed : QM_FALSE;
}

/**
 * @brief Copy the current estimator outputs to a result structure.
 *
 * @param[in] handle Estimator handle.
 * @param[out] result Destination result structure.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_get_result(const qm_battery_t *handle,
                              qm_battery_result_t *result)
{
    if (!handle || !result) {
        return -QM_EINVAL;
    }

    result->soc_percent = handle->output_soc;
    result->soh_percent = handle->output_soh;
    result->resistance_ohm = handle->x[IDX_R];
    result->voltage_v = handle->last_voltage;
    result->dvdt_vps = handle->last_dvdt;
    result->learned_full_v = handle->learned_full_valid ?
                             handle->learned_full_v : effective_full_anchor_v(handle);
    result->relaxed = handle->relaxed;
    result->initialized = handle->initialized;
    result->charge_full_confirmed = handle->charge_full_confirmed;
    result->learned_full_valid = handle->learned_full_valid;
    result->full_guard_active = handle->full_guard_active;
    return QM_EOK;
}
