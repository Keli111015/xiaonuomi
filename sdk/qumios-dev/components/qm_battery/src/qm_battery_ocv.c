#include "qm_battery_ocv.h"

static const qm_battery_ocv_point_t s_default_ocv_table[] = {
    {  0.0f, 3.000f, -0.00120f },
    {  5.0f, 3.300f, -0.00100f },
    { 10.0f, 3.450f, -0.00080f },
    { 20.0f, 3.620f, -0.00055f },
    { 30.0f, 3.700f, -0.00035f },
    { 40.0f, 3.750f, -0.00025f },
    { 50.0f, 3.790f, -0.00020f },
    { 60.0f, 3.830f, -0.00022f },
    { 70.0f, 3.890f, -0.00030f },
    { 80.0f, 3.970f, -0.00045f },
    { 90.0f, 4.080f, -0.00065f },
    {100.0f, 4.200f, -0.00090f },
};

#define OCV_TEMP_REF_C 25.0f
#define OCV_DEFAULT_COUNT ((int)(sizeof(s_default_ocv_table) / sizeof(s_default_ocv_table[0])))

/**
 * @brief Clamp a floating-point value to a range.
 *
 * @param[in] v Input value.
 * @param[in] lo Lower bound.
 * @param[in] hi Upper bound.
 * @return Clamped value.
 */
static qm_float ocv_clampf(qm_float v, qm_float lo, qm_float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * @brief Linearly interpolate between two values.
 *
 * @param[in] a Start value.
 * @param[in] b End value.
 * @param[in] t Interpolation ratio.
 * @return Interpolated value.
 */
static qm_float ocv_lerp(qm_float a, qm_float b, qm_float t)
{
    return a + (b - a) * t;
}

/**
 * @brief Select user OCV table or fallback to the built-in default table.
 *
 * @param[in] table User table.
 * @param[in] table_count User table point count.
 * @param[out] count Selected table point count.
 * @return Selected OCV table.
 */
static const qm_battery_ocv_point_t *ocv_table_select(const qm_battery_ocv_point_t *table,
                                                      int table_count,
                                                      int *count)
{
    if (!table || table_count < 2) {
        if (count) *count = OCV_DEFAULT_COUNT;
        return s_default_ocv_table;
    }
    if (count) *count = table_count;
    return table;
}

/**
 * @brief Return the built-in OCV table.
 *
 * @param[out] count Number of default table points.
 * @return Default OCV table.
 */
const qm_battery_ocv_point_t *qm_battery_ocv_default_table(int *count)
{
    if (count) *count = OCV_DEFAULT_COUNT;
    return s_default_ocv_table;
}

/**
 * @brief Look up battery OCV for a given SOC and optional temperature compensation.
 *
 * @param[in] table OCV table.
 * @param[in] table_count Number of table points.
 * @param[in] soc_percent SOC in percent.
 * @param[in] temperature_c Temperature in degrees Celsius.
 * @param[in] soh SOH ratio.
 * @param[in] temp_comp_enable QM_TRUE to enable temperature compensation.
 * @return Estimated OCV in volts.
 */
qm_float qm_battery_ocv_lookup(const qm_battery_ocv_point_t *table,
                            int table_count,
                            qm_float soc_percent,
                            qm_float temperature_c,
                            qm_float soh,
                            qm_bool temp_comp_enable)
{
    const qm_battery_ocv_point_t *ocv_table;
    qm_float soc = ocv_clampf(soc_percent, 0.0f, 100.0f);
    qm_float soh_limited = ocv_clampf(soh, 0.5f, 1.0f);
    qm_float ocv;
    qm_float coeff;
    qm_float aging_shift;
    int count;
    int i;

    ocv_table = ocv_table_select(table, table_count, &count);

    if (soc <= ocv_table[0].soc_percent) {
        ocv = ocv_table[0].voltage_25c;
        coeff = ocv_table[0].temp_coeff_v_per_c;
    } else if (soc >= ocv_table[count - 1].soc_percent) {
        ocv = ocv_table[count - 1].voltage_25c;
        coeff = ocv_table[count - 1].temp_coeff_v_per_c;
    } else {
        ocv = ocv_table[count - 1].voltage_25c;
        coeff = ocv_table[count - 1].temp_coeff_v_per_c;
        for (i = 0; i < count - 1; i++) {
            const qm_battery_ocv_point_t *p0 = &ocv_table[i];
            const qm_battery_ocv_point_t *p1 = &ocv_table[i + 1];
            if (soc >= p0->soc_percent && soc <= p1->soc_percent) {
                qm_float span = p1->soc_percent - p0->soc_percent;
                qm_float t = span > 0.0f ? (soc - p0->soc_percent) / span : 0.0f;
                ocv = ocv_lerp(p0->voltage_25c, p1->voltage_25c, t);
                coeff = ocv_lerp(p0->temp_coeff_v_per_c, p1->temp_coeff_v_per_c, t);
                break;
            }
        }
    }

    if (temp_comp_enable) {
        ocv += coeff * (temperature_c - OCV_TEMP_REF_C);
    }

    aging_shift = (1.0f - soh_limited) * 0.012f * ((50.0f - soc) / 50.0f);
    return ocv + aging_shift;
}

/**
 * @brief Estimate SOC by inverting the OCV table.
 *
 * @param[in] table OCV table.
 * @param[in] table_count Number of table points.
 * @param[in] voltage Relaxed cell voltage in volts.
 * @param[in] temperature_c Temperature in degrees Celsius.
 * @param[in] soh SOH ratio.
 * @param[in] temp_comp_enable QM_TRUE to enable temperature compensation.
 * @return Estimated SOC in percent.
 */
qm_float qm_battery_ocv_soc_from_voltage(const qm_battery_ocv_point_t *table,
                                      int table_count,
                                      qm_float voltage,
                                      qm_float temperature_c,
                                      qm_float soh,
                                      qm_bool temp_comp_enable)
{
    const qm_battery_ocv_point_t *ocv_table;
    int count;
    int i;

    ocv_table = ocv_table_select(table, table_count, &count);

    if (voltage <= qm_battery_ocv_lookup(ocv_table, count, 0.0f, temperature_c,
                                         soh, temp_comp_enable)) {
        return 0.0f;
    }
    if (voltage >= qm_battery_ocv_lookup(ocv_table, count, 100.0f, temperature_c,
                                         soh, temp_comp_enable)) {
        return 100.0f;
    }

    for (i = 0; i < count - 1; i++) {
        qm_float soc0 = ocv_table[i].soc_percent;
        qm_float soc1 = ocv_table[i + 1].soc_percent;
        qm_float v0 = qm_battery_ocv_lookup(ocv_table, count, soc0, temperature_c,
                                         soh, temp_comp_enable);
        qm_float v1 = qm_battery_ocv_lookup(ocv_table, count, soc1, temperature_c,
                                         soh, temp_comp_enable);

        if (voltage >= v0 && voltage <= v1) {
            qm_float span = v1 - v0;
            qm_float t = span > 0.0001f ? (voltage - v0) / span : 0.0f;
            return ocv_clampf(ocv_lerp(soc0, soc1, t), 0.0f, 100.0f);
        }
    }

    return 50.0f;
}
