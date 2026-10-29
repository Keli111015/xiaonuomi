#ifndef QM_BATTERY_FILTER_H
#define QM_BATTERY_FILTER_H

#include "qm_battery.h"

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IDX_SOC 0
#define IDX_SOH 1
#define IDX_R   2

#define QM_BATTERY_DT_MIN_S 0.050f
#define QM_BATTERY_DT_MAX_S 60.0f

/**
 * @brief Clamp a floating-point value to a range.
 */
static inline qm_float qm_battery_clampf(qm_float v, qm_float lo, qm_float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * @brief Return the absolute value of a floating-point value.
 */
static inline qm_float qm_battery_absf(qm_float v)
{
    return v < 0.0f ? -v : v;
}

/**
 * @brief Blend one value toward another by alpha.
 */
static inline qm_float qm_battery_blendf(qm_float old_v, qm_float target_v, qm_float alpha)
{
    return old_v + (target_v - old_v) * qm_battery_clampf(alpha, 0.0f, 1.0f);
}

/**
 * @brief Test whether a floating-point value is finite.
 */
static inline int qm_battery_isfinitef(qm_float v)
{
    return isfinite(v) ? 1 : 0;
}

/**
 * @brief Clamp the estimator state vector to valid ranges.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in,out] x State vector.
 */
void qm_battery_filter_clamp_state(qm_battery_t *handle,
                                   qm_float x[QM_BATTERY_STATE_NUM]);

/**
 * @brief Stabilize the estimator covariance matrix.
 *
 * @param[in,out] handle Estimator handle.
 */
void qm_battery_filter_stabilize_covariance(qm_battery_t *handle);

/**
 * @brief Initialize the covariance matrix with safe defaults.
 *
 * @param[out] p Covariance matrix.
 */
void qm_battery_filter_covariance_init(qm_float p[QM_BATTERY_STATE_NUM][QM_BATTERY_STATE_NUM]);

/**
 * @brief Run the UKF prediction step.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] dvdt Voltage slope in volts per second.
 * @param[in] charging QM_TRUE when the battery is charging.
 * @param[in] dt Sample interval in seconds.
 */
void qm_battery_filter_predict(qm_battery_t *handle,
                               qm_float dvdt,
                               qm_bool charging,
                               qm_float dt);

/**
 * @brief Run the UKF measurement update step.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Measured battery voltage in volts.
 * @param[in] temperature_c Cell temperature in degrees Celsius.
 * @param[in] dvdt Voltage slope in volts per second.
 * @param[in] has_temperature QM_TRUE if temperature is valid.
 */
void qm_battery_filter_update(qm_battery_t *handle,
                              qm_float voltage,
                              qm_float temperature_c,
                              qm_float dvdt,
                              qm_bool has_temperature);

#ifdef __cplusplus
}
#endif

#endif /* QM_BATTERY_FILTER_H */
