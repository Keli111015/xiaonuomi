#ifndef QM_BATTERY_H
#define QM_BATTERY_H

#include "qm_adc.h"
#include "qm_battery_ocv.h"
#include "qm_errno.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define QM_BATTERY_STATE_NUM 3

/**
 * @brief Runtime configuration for the battery estimator.
 *
 * @note ADC conversion:
 *       battery_voltage = (adc_raw * adc_scale_v_per_count + adc_offset_v) *
 *                         adc_divider_ratio
 * @note If the platform ADC already returns battery millivolts, use
 *       adc_scale_v_per_count = 0.001f and adc_divider_ratio = 1.0f.
 */
typedef struct {
    qm_float k1_soc_per_vps;                 /*!< SOC prediction gain from voltage slope, in percent per V/s */
    qm_float k2_current_per_vps;             /*!< Equivalent current gain from voltage slope, used with internal resistance */
    qm_float default_temperature_c;          /*!< Default cell temperature in degrees Celsius when no measurement is provided */
    qm_float initial_soc_percent;            /*!< Optional persisted SOC seed used before the first voltage update */
    qm_float initial_soc_age_s;              /*!< Age of the persisted SOC seed in seconds */
    qm_float initial_soc_max_age_s;          /*!< Maximum trusted persisted SOC age in seconds; 0 disables age check */
    qm_float standby_drain_percent_per_day;  /*!< SOC drop applied to persisted seed while the product was off or sleeping */
    qm_bool temp_comp_enable;             /*!< Enable OCV temperature compensation when QM_TRUE */
    qm_bool soh_update_enable;            /*!< Enable SOH update from estimated internal resistance when QM_TRUE */
    qm_bool initial_soc_valid;            /*!< Enable initial_soc_percent when QM_TRUE */
    qm_bool learned_full_valid;           /*!< Enable learned_full_v when QM_TRUE */

    qm_float adc_scale_v_per_count;          /*!< ADC pin voltage scale in volts per raw ADC count */
    qm_float adc_offset_v;                   /*!< ADC pin voltage offset in volts */
    qm_float adc_divider_ratio;              /*!< Divider ratio from ADC pin voltage to battery terminal voltage */

    qm_float soc_min_percent;                /*!< Minimum allowed SOC estimate in percent */
    qm_float soc_max_percent;                /*!< Maximum allowed SOC estimate in percent */
    qm_float soh_min_ratio;                  /*!< Minimum allowed SOH estimate as a 0.0 to 1.0 ratio */
    qm_float soh_max_ratio;                  /*!< Maximum allowed SOH estimate as a 0.0 to 1.0 ratio */
    qm_float r_min_ohm;                      /*!< Minimum allowed internal resistance estimate in ohms */
    qm_float r_max_ohm;                      /*!< Maximum allowed internal resistance estimate in ohms */
    qm_float r_nominal_ohm;                  /*!< Nominal new-cell internal resistance in ohms */

    qm_float q_soc;                          /*!< UKF process noise for SOC state */
    qm_float q_soh;                          /*!< UKF process noise for SOH state */
    qm_float q_r;                            /*!< UKF process noise for internal resistance state */
    qm_float rm_base_v2;                     /*!< Base voltage measurement noise variance in V^2 */
    qm_float rm_relax_scale;                 /*!< Measurement-noise scale when the battery is relaxed */
    qm_float rm_dynamic_scale;               /*!< Measurement-noise scale when the battery is under dynamic load */
    qm_float rm_slope_gain;                  /*!< Additional measurement-noise gain per absolute voltage slope */

    qm_float full_anchor_v;                  /*!< Charging voltage anchor that pulls SOC toward 100 percent */
    qm_float empty_anchor_v;                 /*!< Discharging voltage anchor that pulls SOC toward 0 percent */
    qm_float charge_full_confirm_s;          /*!< Required continuous charger-full signal time before accepting full */
    qm_float charge_full_min_v;              /*!< Minimum voltage required to accept the charger-full signal */
    qm_float learned_full_v;                 /*!< Optional persisted learned full-charge voltage */
    qm_float learned_full_min_v;             /*!< Lower bound for learned full-charge voltage */
    qm_float learned_full_max_v;             /*!< Upper bound for learned full-charge voltage */
    qm_float learned_full_blend;             /*!< Blend alpha for each newly confirmed charger-full event */
    qm_float full_guard_s;                   /*!< Time to protect SOC after a confirmed full event, in seconds */
    qm_float full_guard_min_percent;         /*!< Minimum SOC while the recent-full guard is active */
    qm_float charge_settle_s;                /*!< Settling window after charging state changes, in seconds */
    qm_float charge_settle_rm_scale;         /*!< Measurement-noise scale during charging state settling */
    qm_float relax_delta_v;                  /*!< Fast/slow voltage filter delta threshold for relaxation detection */
    qm_float relax_dvdt_vps;                 /*!< Voltage slope threshold for relaxation detection in V/s */
    qm_float relax_confirm_s;                /*!< Required stable time before entering relaxed state, in seconds */

    const qm_battery_ocv_point_t *ocv_table; /*!< OCV-SOC lookup table, referenced directly */
    int ocv_table_count;                  /*!< Number of entries in ocv_table */
} qm_battery_config_t;

/**
 * @brief Required laboratory calibration values for one battery design.
 *
 * @note The OCV table is referenced directly and must remain valid for the
 *       lifetime of any estimator initialized with the destination config.
 * @note Voltage and resistance values use volts and ohms. ADC scale converts
 *       one raw ADC count to ADC-pin volts before divider compensation.
 */
typedef struct {
    const qm_battery_ocv_point_t *ocv_table; /*!< Measured OCV-SOC lookup table */
    int ocv_table_count;                     /*!< Number of entries in ocv_table */
    qm_float adc_scale_v_per_count;          /*!< ADC pin voltage scale in volts per raw ADC count */
    qm_float adc_offset_v;                   /*!< ADC pin voltage offset in volts */
    qm_float adc_divider_ratio;              /*!< Divider ratio from ADC pin voltage to battery terminal voltage */
    qm_float r_nominal_ohm;                  /*!< Measured new-cell internal resistance baseline */
    qm_float full_anchor_v;                  /*!< Calibrated full-charge voltage anchor */
    qm_float empty_anchor_v;                 /*!< Calibrated low/empty voltage anchor */
    qm_float charge_full_min_v;              /*!< Minimum terminal voltage for accepting charger-full signal */
} qm_battery_calibration_t;

/**
 * @brief Output snapshot of the estimator.
 */
typedef struct {
    qm_float soc_percent;                    /*!< Filtered state of charge in percent */
    qm_float soh_percent;                    /*!< Filtered state of health in percent */
    qm_float resistance_ohm;                 /*!< Estimated internal resistance in ohms */
    qm_float voltage_v;                      /*!< Latest measured battery voltage in volts */
    qm_float dvdt_vps;                       /*!< Latest voltage slope in volts per second */
    qm_float learned_full_v;                 /*!< Current learned full-charge voltage in volts */
    qm_bool relaxed;                      /*!< QM_TRUE when the estimator detects a relaxed battery state */
    qm_bool initialized;                  /*!< QM_TRUE after the first valid voltage update */
    qm_bool charge_full_confirmed;        /*!< QM_TRUE when charger-full signal has passed validation */
    qm_bool learned_full_valid;           /*!< QM_TRUE when learned_full_v is valid */
    qm_bool full_guard_active;            /*!< QM_TRUE while recent-full SOC protection is active */
} qm_battery_result_t;

/**
 * @brief User-owned estimator object.
 *
 * @note Do not edit fields directly.
 */
typedef struct {
    qm_battery_config_t config;           /*!< Active estimator configuration */
    qm_float x[QM_BATTERY_STATE_NUM];        /*!< UKF state vector: SOC, SOH ratio, internal resistance */
    qm_float p[QM_BATTERY_STATE_NUM][QM_BATTERY_STATE_NUM]; /*!< UKF covariance matrix */
    qm_float prev_voltage;                   /*!< Previous measured voltage used to compute voltage slope */
    qm_float fast_voltage;                   /*!< Fast low-pass filtered voltage for relaxation detection */
    qm_float slow_voltage;                   /*!< Slow low-pass filtered voltage for relaxation detection */
    qm_float filtered_dvdt;                  /*!< Filtered voltage slope for relaxation detection */
    qm_float relax_time_s;                   /*!< Accumulated time that relaxation conditions have been met */
    qm_float output_soc;                     /*!< Smoothed SOC output in percent */
    qm_float output_soh;                     /*!< Smoothed SOH output in percent */
    qm_float last_voltage;                   /*!< Latest measured voltage in volts */
    qm_float last_dvdt;                      /*!< Latest clamped voltage slope in volts per second */
    qm_float charge_settle_left_s;           /*!< Remaining settling time after charging state changes */
    qm_float charge_full_time_s;             /*!< Continuous valid charger-full signal time in seconds */
    qm_float full_guard_left_s;              /*!< Remaining time for recent-full SOC protection */
    qm_float learned_full_v;                 /*!< Runtime learned full-charge voltage in volts */
    qm_bool initialized;                  /*!< QM_TRUE after the first valid voltage update */
    qm_bool relaxed;                      /*!< QM_TRUE when the battery is currently considered relaxed */
    qm_bool last_charging;                /*!< Charging state from the previous update */
    qm_bool charge_full_confirmed;        /*!< QM_TRUE when charger-full signal has passed validation */
    qm_bool learned_full_valid;           /*!< QM_TRUE when learned_full_v is valid */
    qm_bool full_guard_active;            /*!< QM_TRUE while recent-full SOC protection is active */
} qm_battery_t;

/**
 * @brief Fill a configuration structure with safe defaults.
 *
 * @param[out] config Destination configuration.
 */
void qm_battery_default_config(qm_battery_config_t *config);

/**
 * @brief Apply required laboratory calibration values to a config.
 *
 * @param[in,out] config Destination config initialized by qm_battery_default_config().
 * @param[in] calibration Laboratory calibration values to import.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_apply_calibration(qm_battery_config_t *config,
                                 const qm_battery_calibration_t *calibration);

/**
 * @brief Initialize a user-owned estimator handle.
 *
 * @param[out] handle Estimator handle to initialize.
 * @param[in] config Configuration to copy, or NULL to use defaults.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_init(qm_battery_t *handle,
                        const qm_battery_config_t *config);

/**
 * @brief Reset estimator state while keeping the current configuration.
 *
 * @param[in,out] handle Estimator handle.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_reset(qm_battery_t *handle);

/**
 * @brief Update the estimator with a measured battery voltage.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Measured battery voltage in volts.
 * @param[in] temperature Measured temperature in degrees Celsius.
 * @param[in] has_temperature QM_TRUE if temperature is valid, otherwise QM_FALSE.
 * @param[in] charging QM_TRUE when the battery is charging or the charger is attached.
 * @param[in] charge_full QM_TRUE when the charger IC reports charge complete.
 * @param[in] dt Sample interval in seconds.
 * @return QM_EOK on success, negative error code otherwise.
 *
 * @note If has_temperature is QM_FALSE, config.default_temperature_c is used
 *       and temperature compensation is effectively disabled.
 * @note If the hardware has no charger-full signal, pass QM_FALSE for charge_full.
 */
int qm_battery_update_voltage(qm_battery_t *handle,
                              qm_float voltage,
                              qm_float temperature,
                              qm_bool has_temperature,
                              qm_bool charging,
                              qm_bool charge_full,
                              qm_float dt);

/**
 * @brief Convert one ADC sample into battery voltage using the configured scale.
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
                                    uint32_t timeout_ms);

/**
 * @brief Sample ADC and update the estimator.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] adc ADC device handle.
 * @param[in] charging QM_TRUE when the battery is charging or the charger is attached.
 * @param[in] charge_full QM_TRUE when the charger IC reports charge complete.
 * @param[in] dt Sample interval in seconds.
 * @param[in] timeout_ms ADC read timeout in milliseconds.
 * @return QM_EOK on success, negative error code otherwise.
 *
 * @note Temperature is unknown in this convenience API, so
 *       default_temperature_c is used. Use qm_battery_update_voltage() if a
 *       temperature source exists.
 * @note If the hardware has no charger-full signal, pass QM_FALSE for charge_full.
 */
int qm_battery_update_adc(qm_battery_t *handle,
                          qm_adc_dev_t *adc,
                          qm_bool charging,
                          qm_bool charge_full,
                          qm_float dt,
                          uint32_t timeout_ms);

/**
 * @brief Get the latest SOC output.
 *
 * @param[in] handle Estimator handle.
 * @return SOC in percent, or 0.0f if handle is NULL.
 */
qm_float qm_battery_get_soc(const qm_battery_t *handle);

/**
 * @brief Get the latest SOH output.
 *
 * @param[in] handle Estimator handle.
 * @return SOH in percent, or 0.0f if handle is NULL.
 */
qm_float qm_battery_get_soh(const qm_battery_t *handle);

/**
 * @brief Get the latest estimated internal resistance.
 *
 * @param[in] handle Estimator handle.
 * @return Resistance in ohms, or 0.0f if handle is NULL.
 */
qm_float qm_battery_get_internal_resistance(const qm_battery_t *handle);

/**
 * @brief Check whether the estimator is in the relaxed state.
 *
 * @param[in] handle Estimator handle.
 * @return QM_TRUE if relaxed, otherwise QM_FALSE.
 */
qm_bool qm_battery_is_relaxed(const qm_battery_t *handle);

/**
 * @brief Copy the current estimator output into a result structure.
 *
 * @param[in] handle Estimator handle.
 * @param[out] result Destination result structure.
 * @return QM_EOK on success, negative error code otherwise.
 */
int qm_battery_get_result(const qm_battery_t *handle,
                              qm_battery_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* QM_BATTERY_H */
