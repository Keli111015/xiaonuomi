#include "qm_battery_filter.h"

#include <math.h>

#define UKF_N QM_BATTERY_STATE_NUM
#define UKF_SIGMA_COUNT (2 * UKF_N + 1)

#define UKF_ALPHA 1.0f
#define UKF_BETA  2.0f
#define UKF_KAPPA 0.0f

#define SOC_SLEW_PERCENT_STEP 1.5f
#define RESIDUAL_LIMIT_V      0.250f
#define P_DIAG_MIN            0.00000001f
#define P_SOC_INIT            36.0f
#define P_SOH_INIT            0.0025f
#define P_R_INIT              0.0004f
#define P_SOC_MAX             400.0f
#define P_SOH_MAX             0.0300f
#define P_R_MAX               0.0200f
#define RM_MIN_V2             0.000004f
#define RM_MAX_V2             0.010000f
#define GAIN_SOC_MAX          80.0f
#define GAIN_SOH_MAX          0.30f
#define GAIN_R_MAX            1.50f

/**
 * @brief Clear a UKF-sized matrix.
 *
 * @param[out] a Matrix to clear.
 */
static void mat_zero(qm_float a[UKF_N][UKF_N])
{
    int r;
    int c;

    for (r = 0; r < UKF_N; r++) {
        for (c = 0; c < UKF_N; c++) {
            a[r][c] = 0.0f;
        }
    }
}

/**
 * @brief Clamp SOC, SOH, and resistance state values to configured limits.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in,out] x State vector.
 */
void qm_battery_filter_clamp_state(qm_battery_t *handle, qm_float x[UKF_N])
{
    if (!handle || !x) {
        return;
    }

    x[IDX_SOC] = qm_battery_clampf(x[IDX_SOC],
                                   handle->config.soc_min_percent,
                                   handle->config.soc_max_percent);
    x[IDX_SOH] = qm_battery_clampf(x[IDX_SOH],
                                   handle->config.soh_min_ratio,
                                   handle->config.soh_max_ratio);
    x[IDX_R] = qm_battery_clampf(x[IDX_R],
                                 handle->config.r_min_ohm,
                                 handle->config.r_max_ohm);
}

/**
 * @brief Symmetrize and bound the covariance matrix.
 *
 * @param[in,out] p Covariance matrix.
 */
static void stabilize_covariance(qm_float p[UKF_N][UKF_N])
{
    const qm_float max_diag[UKF_N] = {P_SOC_MAX, P_SOH_MAX, P_R_MAX};
    int r;
    int c;

    for (r = 0; r < UKF_N; r++) {
        for (c = r + 1; c < UKF_N; c++) {
            qm_float sym = 0.5f * (p[r][c] + p[c][r]);
            p[r][c] = sym;
            p[c][r] = sym;
        }
    }

    for (r = 0; r < UKF_N; r++) {
        if (!qm_battery_isfinitef(p[r][r]) || p[r][r] < P_DIAG_MIN) {
            p[r][r] = P_DIAG_MIN;
        }
        if (p[r][r] > max_diag[r]) {
            p[r][r] = max_diag[r];
        }
    }

    for (r = 0; r < UKF_N; r++) {
        for (c = r + 1; c < UKF_N; c++) {
            qm_float limit = 0.95f * sqrtf(p[r][r] * p[c][c]);
            p[r][c] = qm_battery_clampf(p[r][c], -limit, limit);
            p[c][r] = p[r][c];
        }
    }
}

/**
 * @brief Stabilize the covariance matrix stored in the estimator handle.
 *
 * @param[in,out] handle Estimator handle.
 */
void qm_battery_filter_stabilize_covariance(qm_battery_t *handle)
{
    if (!handle) {
        return;
    }

    stabilize_covariance(handle->p);
}

/**
 * @brief Initialize covariance with broad SOC uncertainty and bounded SOH/R uncertainty.
 *
 * @param[out] p Covariance matrix.
 */
void qm_battery_filter_covariance_init(qm_float p[UKF_N][UKF_N])
{
    mat_zero(p);
    p[IDX_SOC][IDX_SOC] = P_SOC_INIT;
    p[IDX_SOH][IDX_SOH] = P_SOH_INIT;
    p[IDX_R][IDX_R] = P_R_INIT;
}

/**
 * @brief Compute Cholesky lower-triangular factor for the 3-state covariance.
 *
 * @param[in] a Positive semi-definite covariance matrix.
 * @param[out] l Lower-triangular factor.
 */
static void cholesky3(const qm_float a[UKF_N][UKF_N], qm_float l[UKF_N][UKF_N])
{
    int i;
    int j;
    int k;

    mat_zero(l);
    for (i = 0; i < UKF_N; i++) {
        for (j = 0; j <= i; j++) {
            qm_float sum = a[i][j];
            for (k = 0; k < j; k++) {
                sum -= l[i][k] * l[j][k];
            }
            if (i == j) {
                if (!qm_battery_isfinitef(sum) || sum < P_DIAG_MIN) {
                    sum = P_DIAG_MIN;
                }
                l[i][j] = sqrtf(sum);
            } else {
                l[i][j] = qm_battery_absf(l[j][j]) < 0.0000001f ? 0.0f : sum / l[j][j];
            }
        }
    }
}

/**
 * @brief Compute unscented transform weights and sigma spread.
 *
 * @param[out] wm Mean weights.
 * @param[out] wc Covariance weights.
 * @param[out] gamma Sigma point spread factor.
 */
static void ukf_weights(qm_float wm[UKF_SIGMA_COUNT],
                        qm_float wc[UKF_SIGMA_COUNT],
                        qm_float *gamma)
{
    const qm_float n = (qm_float)UKF_N;
    const qm_float alpha2 = UKF_ALPHA * UKF_ALPHA;
    const qm_float lambda = alpha2 * (n + UKF_KAPPA) - n;
    const qm_float c = n + lambda;
    int i;

    wm[0] = lambda / c;
    wc[0] = wm[0] + (1.0f - alpha2 + UKF_BETA);
    for (i = 1; i < UKF_SIGMA_COUNT; i++) {
        wm[i] = 0.5f / c;
        wc[i] = wm[i];
    }
    *gamma = sqrtf(c);
}

/**
 * @brief Generate sigma points around the current state.
 *
 * @param[in,out] handle Estimator handle for clamp limits.
 * @param[in] x State vector.
 * @param[in] p Covariance matrix.
 * @param[out] sigma Generated sigma point matrix.
 */
static void sigma_points(qm_battery_t *handle,
                         const qm_float x[UKF_N],
                         const qm_float p[UKF_N][UKF_N],
                         qm_float sigma[UKF_SIGMA_COUNT][UKF_N])
{
    qm_float l[UKF_N][UKF_N];
    qm_float wm[UKF_SIGMA_COUNT];
    qm_float wc[UKF_SIGMA_COUNT];
    qm_float gamma;
    int i;
    int j;

    ukf_weights(wm, wc, &gamma);
    cholesky3(p, l);

    for (j = 0; j < UKF_N; j++) {
        sigma[0][j] = x[j];
    }

    for (i = 0; i < UKF_N; i++) {
        for (j = 0; j < UKF_N; j++) {
            qm_float delta = gamma * l[j][i];
            sigma[1 + i][j] = x[j] + delta;
            sigma[1 + UKF_N + i][j] = x[j] - delta;
        }
        qm_battery_filter_clamp_state(handle, sigma[1 + i]);
        qm_battery_filter_clamp_state(handle, sigma[1 + UKF_N + i]);
    }
}

/**
 * @brief Convert voltage slope into a bounded SOC delta.
 *
 * @param[in] handle Estimator handle.
 * @param[in] dvdt Voltage slope in volts per second.
 * @param[in] charging QM_TRUE when the battery is charging.
 * @return SOC delta in percent.
 */
static qm_float state_transition_soc_delta(const qm_battery_t *handle,
                                        qm_float dvdt,
                                        qm_bool charging)
{
    qm_float delta = -handle->config.k1_soc_per_vps * dvdt;

    if (handle->charge_settle_left_s > 0.0f) {
        return 0.0f;
    }

    if (charging) {
        if (delta < 0.0f) delta *= 0.25f;
    } else {
        if (delta > 0.0f) delta *= 0.25f;
    }

    return qm_battery_clampf(delta, -SOC_SLEW_PERCENT_STEP, SOC_SLEW_PERCENT_STEP);
}

/**
 * @brief Propagate one sigma point through the process model.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] in Input sigma point.
 * @param[out] out Propagated sigma point.
 * @param[in] dvdt Voltage slope in volts per second.
 * @param[in] charging QM_TRUE when the battery is charging.
 */
static void state_transition(qm_battery_t *handle,
                             const qm_float in[UKF_N],
                             qm_float out[UKF_N],
                             qm_float dvdt,
                             qm_bool charging)
{
    out[IDX_SOC] = in[IDX_SOC] + state_transition_soc_delta(handle, dvdt, charging);
    out[IDX_SOH] = in[IDX_SOH];
    out[IDX_R] = in[IDX_R];
    qm_battery_filter_clamp_state(handle, out);
}

/**
 * @brief Convert a state sigma point into predicted terminal voltage.
 *
 * @param[in] handle Estimator handle.
 * @param[in] x State sigma point.
 * @param[in] temperature_c Temperature used for OCV lookup.
 * @param[in] dvdt Voltage slope in volts per second.
 * @param[in] has_temperature QM_TRUE if temperature is measured.
 * @return Predicted voltage in volts.
 */
static qm_float measurement_function(const qm_battery_t *handle,
                                  const qm_float x[UKF_N],
                                  qm_float temperature_c,
                                  qm_float dvdt,
                                  qm_bool has_temperature)
{
    qm_bool temp_comp = has_temperature && handle->config.temp_comp_enable;
    qm_float ocv = qm_battery_ocv_lookup(handle->config.ocv_table,
                                      handle->config.ocv_table_count,
                                      x[IDX_SOC],
                                      temperature_c,
                                      x[IDX_SOH],
                                      temp_comp);
    return ocv - handle->config.k2_current_per_vps * dvdt * x[IDX_R];
}

/**
 * @brief Calculate measurement noise based on voltage dynamics and relaxation.
 *
 * @param[in] handle Estimator handle.
 * @param[in] dvdt Voltage slope in volts per second.
 * @param[in] relaxed QM_TRUE when relaxation is detected.
 * @param[in] has_temperature QM_TRUE if temperature is measured.
 * @return Measurement noise variance.
 */
static qm_float adaptive_measurement_noise(const qm_battery_t *handle,
                                        qm_float dvdt,
                                        qm_bool relaxed,
                                        qm_bool has_temperature)
{
    qm_float rm = handle->config.rm_base_v2 + handle->config.rm_slope_gain *
               qm_battery_absf(dvdt);

    rm *= relaxed ? handle->config.rm_relax_scale : handle->config.rm_dynamic_scale;
    if (!has_temperature) {
        rm *= 1.6f;
    }
    if (handle->charge_settle_left_s > 0.0f) {
        rm *= qm_battery_clampf(handle->config.charge_settle_rm_scale, 1.0f, 100.0f);
    }

    return qm_battery_clampf(rm, RM_MIN_V2, RM_MAX_V2);
}

/**
 * @brief Execute the UKF prediction step.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] dvdt Voltage slope in volts per second.
 * @param[in] charging QM_TRUE when the battery is charging.
 * @param[in] dt Sample interval in seconds.
 */
void qm_battery_filter_predict(qm_battery_t *handle,
                               qm_float dvdt,
                               qm_bool charging,
                               qm_float dt)
{
    qm_float sigma[UKF_SIGMA_COUNT][UKF_N];
    qm_float propagated[UKF_SIGMA_COUNT][UKF_N];
    qm_float wm[UKF_SIGMA_COUNT];
    qm_float wc[UKF_SIGMA_COUNT];
    qm_float gamma;
    qm_float q[UKF_N];
    qm_float dt_scale = qm_battery_clampf(dt, QM_BATTERY_DT_MIN_S, QM_BATTERY_DT_MAX_S);
    int i;
    int r;
    int c;

    if (!handle) {
        return;
    }

    ukf_weights(wm, wc, &gamma);
    (void)gamma;
    sigma_points(handle, handle->x, handle->p, sigma);

    for (i = 0; i < UKF_SIGMA_COUNT; i++) {
        state_transition(handle, sigma[i], propagated[i], dvdt, charging);
    }

    for (r = 0; r < UKF_N; r++) {
        handle->x[r] = 0.0f;
        for (i = 0; i < UKF_SIGMA_COUNT; i++) {
            handle->x[r] += wm[i] * propagated[i][r];
        }
    }
    qm_battery_filter_clamp_state(handle, handle->x);

    mat_zero(handle->p);
    for (i = 0; i < UKF_SIGMA_COUNT; i++) {
        qm_float dx[UKF_N];
        for (r = 0; r < UKF_N; r++) {
            dx[r] = propagated[i][r] - handle->x[r];
        }
        for (r = 0; r < UKF_N; r++) {
            for (c = 0; c < UKF_N; c++) {
                handle->p[r][c] += wc[i] * dx[r] * dx[c];
            }
        }
    }

    q[IDX_SOC] = handle->config.q_soc * dt_scale;
    q[IDX_SOH] = handle->config.q_soh * dt_scale;
    q[IDX_R] = handle->config.q_r * dt_scale;
    for (r = 0; r < UKF_N; r++) {
        handle->p[r][r] += q[r];
    }

    stabilize_covariance(handle->p);
}

/**
 * @brief Execute the UKF measurement update step with a voltage observation.
 *
 * @param[in,out] handle Estimator handle.
 * @param[in] voltage Measured battery voltage in volts.
 * @param[in] temperature_c Temperature used for OCV lookup.
 * @param[in] dvdt Voltage slope in volts per second.
 * @param[in] has_temperature QM_TRUE if temperature is measured.
 */
void qm_battery_filter_update(qm_battery_t *handle,
                              qm_float voltage,
                              qm_float temperature_c,
                              qm_float dvdt,
                              qm_bool has_temperature)
{
    qm_float sigma[UKF_SIGMA_COUNT][UKF_N];
    qm_float z_sigma[UKF_SIGMA_COUNT];
    qm_float wm[UKF_SIGMA_COUNT];
    qm_float wc[UKF_SIGMA_COUNT];
    qm_float gamma;
    qm_float z_pred = 0.0f;
    qm_float s;
    qm_float pxz[UKF_N] = {0.0f, 0.0f, 0.0f};
    qm_float k[UKF_N];
    qm_float residual;
    int i;
    int r;
    int c;

    if (!handle) {
        return;
    }

    ukf_weights(wm, wc, &gamma);
    (void)gamma;
    sigma_points(handle, handle->x, handle->p, sigma);

    for (i = 0; i < UKF_SIGMA_COUNT; i++) {
        z_sigma[i] = measurement_function(handle, sigma[i], temperature_c, dvdt, has_temperature);
        z_pred += wm[i] * z_sigma[i];
    }

    s = adaptive_measurement_noise(handle, dvdt, handle->relaxed, has_temperature);
    for (i = 0; i < UKF_SIGMA_COUNT; i++) {
        qm_float dz = z_sigma[i] - z_pred;
        s += wc[i] * dz * dz;
        for (r = 0; r < UKF_N; r++) {
            pxz[r] += wc[i] * (sigma[i][r] - handle->x[r]) * dz;
        }
    }

    if (!qm_battery_isfinitef(s) || s < RM_MIN_V2) {
        s = RM_MIN_V2;
    }

    for (r = 0; r < UKF_N; r++) {
        k[r] = pxz[r] / s;
    }
    k[IDX_SOC] = qm_battery_clampf(k[IDX_SOC], -GAIN_SOC_MAX, GAIN_SOC_MAX);
    k[IDX_SOH] = qm_battery_clampf(k[IDX_SOH], -GAIN_SOH_MAX, GAIN_SOH_MAX);
    k[IDX_R] = qm_battery_clampf(k[IDX_R], -GAIN_R_MAX, GAIN_R_MAX);

    residual = qm_battery_clampf(voltage - z_pred, -RESIDUAL_LIMIT_V, RESIDUAL_LIMIT_V);

    for (r = 0; r < UKF_N; r++) {
        handle->x[r] += k[r] * residual;
    }
    qm_battery_filter_clamp_state(handle, handle->x);

    for (r = 0; r < UKF_N; r++) {
        for (c = 0; c < UKF_N; c++) {
            handle->p[r][c] -= k[r] * s * k[c];
        }
    }
    stabilize_covariance(handle->p);
}
