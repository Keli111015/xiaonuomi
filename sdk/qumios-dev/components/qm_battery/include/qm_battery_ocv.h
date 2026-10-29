#ifndef QM_BATTERY_OCV_H
#define QM_BATTERY_OCV_H

#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief One point in a battery OCV-SOC table.
 *
 * @note voltage_25c is the relaxed cell voltage at 25 C. temp_coeff_v_per_c
 *       is a local linear temperature coefficient used when temperature
 *       compensation is enabled.
 */
typedef struct {
    qm_float soc_percent;        /*!< State of charge point in percent */
    qm_float voltage_25c;        /*!< Relaxed open-circuit voltage at 25 C */
    qm_float temp_coeff_v_per_c; /*!< Local temperature coefficient in volts per degree Celsius */
} qm_battery_ocv_point_t;

/**
 * @brief Get the built-in 1S Li-ion default OCV table.
 *
 * @param[out] count Number of table points returned.
 * @return Pointer to the built-in table.
 *
 * @note Production projects should replace it with cell-specific measured
 *       data.
 */
const qm_battery_ocv_point_t *qm_battery_ocv_default_table(int *count);

/**
 * @brief Look up OCV by SOC using linear interpolation.
 *
 * @param[in] table OCV table.
 * @param[in] table_count Number of points in the table.
 * @param[in] soc_percent State of charge in percent.
 * @param[in] temperature_c Cell temperature in degrees Celsius.
 * @param[in] soh State of health ratio.
 * @param[in] temp_comp_enable QM_TRUE to apply temperature compensation.
 * @return Estimated open-circuit voltage in volts.
 */
qm_float qm_battery_ocv_lookup(const qm_battery_ocv_point_t *table,
                            int table_count,
                            qm_float soc_percent,
                            qm_float temperature_c,
                            qm_float soh,
                            qm_bool temp_comp_enable);

/**
 * @brief Estimate SOC from a relaxed voltage and OCV table.
 *
 * @param[in] table OCV table.
 * @param[in] table_count Number of points in the table.
 * @param[in] voltage Measured relaxed voltage in volts.
 * @param[in] temperature_c Cell temperature in degrees Celsius.
 * @param[in] soh State of health ratio.
 * @param[in] temp_comp_enable QM_TRUE to apply temperature compensation.
 * @return Estimated state of charge in percent.
 */
qm_float qm_battery_ocv_soc_from_voltage(const qm_battery_ocv_point_t *table,
                                      int table_count,
                                      qm_float voltage,
                                      qm_float temperature_c,
                                      qm_float soh,
                                      qm_bool temp_comp_enable);

#ifdef __cplusplus
}
#endif

#endif /* QM_BATTERY_OCV_H */
