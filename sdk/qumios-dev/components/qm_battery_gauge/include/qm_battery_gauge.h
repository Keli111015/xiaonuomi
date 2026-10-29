#ifndef QM_BATTERY_GAUGE_H
#define QM_BATTERY_GAUGE_H

#include "qm_errno.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    QM_BATTERY_GAUGE_STATE_UNKNOWN = 0,
    QM_BATTERY_GAUGE_STATE_IDLE,
    QM_BATTERY_GAUGE_STATE_CHARGING,
    QM_BATTERY_GAUGE_STATE_DISCHARGING,
    QM_BATTERY_GAUGE_STATE_FULL,
    QM_BATTERY_GAUGE_STATE_LOW,
    QM_BATTERY_GAUGE_STATE_EMPTY,
} qm_battery_gauge_state_t;

typedef enum {
    QM_BATTERY_GAUGE_CURRENT_FULL = 0,        /*!< Signed charge/discharge current is available */
    QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY,    /*!< Only charge current is available; discharge uses voltage */
    QM_BATTERY_GAUGE_CURRENT_VOLTAGE_ONLY,   /*!< No current is available; SOC follows voltage */
} qm_battery_gauge_current_mode_t;

#define QM_BATTERY_GAUGE_SNAPSHOT_MAGIC   0x51424753u
#define QM_BATTERY_GAUGE_SNAPSHOT_VERSION 1u
#define QM_BATTERY_GAUGE_DEFAULT_SNAPSHOT_KEY "qm_battery_gauge_snapshot"

/**
 * @brief One point in the voltage-SOC lookup table.
 *
 * @note voltage_25c is the compensated cell voltage at 25 C.
 */
typedef struct {
    qm_float soc_percent;        /*!< SOC point in percent */
    qm_float voltage_25c;        /*!< Compensated cell voltage at 25 C */
    qm_float temp_coeff_v_per_c; /*!< Optional local voltage temperature coefficient */
} qm_battery_gauge_ocv_point_t;

/**
 * @brief Runtime configuration for the battery gauge.
 *
 * @note Current sign convention:
 *       current_ma > 0 means charge current flows into the battery.
 *       current_ma < 0 means discharge current flows out of the battery.
 *       When current_mode is QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY, discharge
 *       samples may set current_valid to QM_FALSE and SOC will follow voltage.
 */
typedef struct {
    qm_float design_capacity_mah;       /*!< Nominal new-cell capacity */
    qm_float usable_capacity_mah;       /*!< Capacity used by coulomb counting */
    qm_float internal_resistance_ohm;   /*!< Cell plus board resistance for load compensation */
    qm_float max_update_dt_s;           /*!< Upper clamp for one update interval */
    qm_battery_gauge_current_mode_t current_mode; /*!< Current measurement capability */

    qm_float full_voltage_v;            /*!< Minimum voltage for voltage-only full detection */
    qm_float full_current_ma;           /*!< Charge current below this value can confirm full */
    qm_float full_confirm_s;            /*!< Continuous full condition time before forcing 100 percent */
    qm_float empty_voltage_v;           /*!< Voltage anchor that pulls SOC toward empty */
    qm_float cutoff_voltage_v;          /*!< Voltage at or below which SOC is forced to 0 */
    qm_float low_voltage_v;             /*!< Low-battery voltage warning threshold */
    qm_float low_soc_percent;           /*!< Low-battery SOC warning threshold */

    qm_float default_temperature_c;     /*!< Temperature used when no sensor exists */
    qm_bool load_compensation_enable;   /*!< Enable R * I voltage compensation */
    qm_bool temp_comp_enable;           /*!< Enable OCV table temperature compensation */
    qm_bool initial_soc_valid;          /*!< Use initial_soc_percent as boot seed */
    qm_float initial_soc_percent;       /*!< Persisted SOC seed */

    const qm_battery_gauge_ocv_point_t *ocv_table; /*!< Voltage-SOC lookup table */
    qm_size_t ocv_table_count;                     /*!< Number of entries in ocv_table */
} qm_battery_gauge_config_t;

/**
 * @brief One measured battery sample.
 */
typedef struct {
    qm_float voltage_v;       /*!< Battery terminal voltage */
    qm_float current_ma;      /*!< Signed current when valid; positive charging, negative discharging */
    qm_float temperature_c;   /*!< Battery temperature */
    qm_float dt_s;            /*!< Sample interval in seconds */
    qm_bool current_valid;    /*!< QM_TRUE when current_ma is measured; ignored in full-current mode */
    qm_bool has_temperature;  /*!< QM_TRUE when temperature_c is valid */
    qm_bool charging;         /*!< QM_TRUE when charger path is active */
    qm_bool charge_full;      /*!< QM_TRUE when charger IC reports full */
} qm_battery_gauge_sample_t;

/**
 * @brief Output snapshot of the battery gauge.
 */
typedef struct {
    qm_float soc_percent;          /*!< Smoothed SOC output */
    qm_float raw_soc_percent;      /*!< Internal unsmoothed SOC */
    qm_float soh_percent;          /*!< Usable capacity divided by design capacity */
    qm_float remaining_mah;        /*!< Estimated remaining capacity */
    qm_float usable_capacity_mah;  /*!< Capacity used by coulomb counting */
    qm_float voltage_v;            /*!< Latest terminal voltage */
    qm_float current_ma;           /*!< Latest signed current */
    qm_bool current_valid;         /*!< QM_TRUE when latest current_ma is measured */
    qm_float compensated_voltage_v; /*!< Latest compensated voltage */
    qm_float temperature_c;        /*!< Latest temperature used by the gauge */
    qm_float charged_mah;          /*!< Accumulated charged capacity since reset */
    qm_float discharged_mah;       /*!< Accumulated discharged capacity since reset */
    qm_float runtime_s;            /*!< Accumulated update time since reset */
    qm_battery_gauge_state_t state; /*!< Current gauge state */
    qm_bool initialized;           /*!< QM_TRUE after at least one sample update */
    qm_bool relaxed;               /*!< QM_TRUE when current is low long enough for OCV correction */
    qm_bool low;                   /*!< QM_TRUE when low-battery threshold is reached */
    qm_bool empty;                 /*!< QM_TRUE when empty/cutoff threshold is reached */
    qm_bool full;                  /*!< QM_TRUE after full condition is confirmed */
} qm_battery_gauge_result_t;

/**
 * @brief Persisted snapshot used to restore SOC after sleep or power loss.
 *
 * @note The component only fills and checks basic snapshot fields. The
 *       application owns storage, integrity checking, wear leveling, and
 *       selecting the newest valid slot.
 */
typedef struct {
    qm_u32 magic;                 /*!< Must be QM_BATTERY_GAUGE_SNAPSHOT_MAGIC */
    qm_u16 version;               /*!< Must be QM_BATTERY_GAUGE_SNAPSHOT_VERSION */
    qm_u32 seq;                   /*!< Monotonic sequence managed by application */
    qm_u32 timestamp_s;           /*!< Application timestamp when saved */
    qm_float soc_percent;         /*!< Saved output SOC */
    qm_float remaining_mah;       /*!< Saved remaining capacity */
    qm_float usable_capacity_mah; /*!< Saved usable capacity */
    qm_float voltage_v;           /*!< Saved terminal voltage */
    qm_float temperature_c;       /*!< Saved battery temperature */
    qm_bool charging;             /*!< Charger state when saved */
    qm_bool full;                 /*!< Full flag when saved */
    qm_bool valid;                /*!< QM_TRUE when this snapshot should be used */
} qm_battery_gauge_snapshot_t;

/**
 * @brief Inputs used to restore SOC after an offline period.
 *
 * @note This is one-shot restore context, not product config. Typical sources:
 *       snapshot from RTC/NVS, offline time from RTC/timestamp delta, offline
 *       current from measured sleep current, wake voltage/current from ADC or
 *       gauge front-end, and charging/full flags from charger status.
 */
typedef struct {
    const qm_battery_gauge_snapshot_t *snapshot; /*!< Optional snapshot loaded from RTC memory or persistent storage */
    qm_bool offline_time_valid;                  /*!< QM_TRUE when offline_time_s is known from RTC or saved timestamps */
    qm_float offline_time_s;                     /*!< Seconds since the snapshot was saved */
    qm_float offline_current_ma;                 /*!< Estimated average offline drain current, positive mA */
    qm_float wake_voltage_v;                     /*!< Battery voltage measured at boot or wakeup */
    qm_float wake_current_ma;                    /*!< Signed battery current measured at boot or wakeup */
    qm_float wake_temperature_c;                 /*!< Battery temperature measured at boot or wakeup */
    qm_bool has_temperature;                     /*!< QM_TRUE when wake_temperature_c comes from a real measurement */
    qm_bool charging;                            /*!< Charger online/path active state at boot or wakeup */
    qm_bool charge_full;                         /*!< Charger IC full indication at boot or wakeup */
} qm_battery_gauge_restore_config_t;

/**
 * @brief Output details from SOC restoration.
 */
typedef struct {
    qm_float final_soc_percent;        /*!< SOC selected for initialization */
    qm_float saved_soc_percent;        /*!< Snapshot SOC before offline drain */
    qm_float saved_after_drain_percent; /*!< Snapshot SOC after offline drain */
    qm_float voltage_soc_percent;      /*!< SOC estimated from wake voltage */
    qm_float offline_drain_mah;        /*!< Estimated offline drain */
    qm_float voltage_weight;           /*!< Weight applied to voltage SOC */
    qm_bool snapshot_used;             /*!< QM_TRUE when a valid snapshot was used */
    qm_bool voltage_used;              /*!< QM_TRUE when wake voltage was used */
    qm_bool charge_full_used;          /*!< QM_TRUE when full signal forced 100% */
} qm_battery_gauge_restore_result_t;

/**
 * @brief User-owned gauge object.
 *
 * @note Do not edit fields directly.
 */
typedef struct {
    qm_battery_gauge_config_t config;
    qm_float soc_percent;
    qm_float output_soc_percent;
    qm_float remaining_mah;
    qm_float charged_mah;
    qm_float discharged_mah;
    qm_float voltage_v;
    qm_float current_ma;
    qm_bool current_valid;
    qm_float compensated_voltage_v;
    qm_float temperature_c;
    qm_float relax_time_s;
    qm_float full_time_s;
    qm_float runtime_s;
    qm_battery_gauge_state_t state;
    qm_bool initialized;
    qm_bool soc_seeded;
    qm_bool relaxed;
    qm_bool low;
    qm_bool empty;
    qm_bool full;
} qm_battery_gauge_t;

/**
 * @brief Fill a config structure with safe defaults.
 *
 * @param[out] config Destination config.
 */
void qm_battery_gauge_default_config(qm_battery_gauge_config_t *config);

/**
 * @brief Initialize a user-owned battery gauge handle.
 *
 * @param[out] gauge Gauge handle to initialize.
 * @param[in] config Config to copy, or NULL to use defaults.
 * @param[in] restore Optional restore inputs, or NULL for normal boot.
 * @param[out] result Optional restore details when restore is not NULL.
 * @return QM_EOK on success, negative error code otherwise.
 */
qm_err_t qm_battery_gauge_init(
    qm_battery_gauge_t *gauge,
    const qm_battery_gauge_config_t *config,
    const qm_battery_gauge_restore_config_t *restore,
    qm_battery_gauge_restore_result_t *result);

/**
 * @brief Seed or override SOC manually.
 *
 * @note Use this when the gauge object stays alive and only SOC needs to be
 *       corrected, for example after light sleep restore or factory
 *       calibration.
 *
 * @param[in,out] gauge Gauge handle.
 * @param[in] soc_percent SOC in percent.
 * @return QM_EOK on success, negative error code otherwise.
 */
qm_err_t qm_battery_gauge_set_soc(qm_battery_gauge_t *gauge,
                                  qm_float soc_percent);

/**
 * @brief Fill a persistent snapshot from current gauge state.
 *
 * @param[in] gauge Gauge handle.
 * @param[out] snapshot Destination snapshot.
 * @param[in] seq Application-managed sequence.
 * @param[in] timestamp_s Application timestamp.
 * @return QM_EOK on success, negative error code otherwise.
 */
qm_err_t qm_battery_gauge_make_snapshot(
    const qm_battery_gauge_t *gauge,
    qm_battery_gauge_snapshot_t *snapshot,
    qm_u32 seq,
    qm_u32 timestamp_s);

/**
 * @brief Save a generated snapshot to the configured persistent storage.
 *
 * @note Pass NULL key to use QM_BATTERY_GAUGE_DEFAULT_SNAPSHOT_KEY.
 */
qm_err_t qm_battery_gauge_save_snapshot(
    const qm_battery_gauge_t *gauge,
    const char *key,
    qm_u32 seq,
    qm_u32 timestamp_s);

/**
 * @brief Load a snapshot from the configured persistent storage.
 *
 * @note Pass NULL key to use QM_BATTERY_GAUGE_DEFAULT_SNAPSHOT_KEY.
 */
qm_err_t qm_battery_gauge_load_snapshot(
    const char *key,
    qm_battery_gauge_snapshot_t *snapshot);

/**
 * @brief Estimate SOC after sleep or power loss without initializing a gauge.
 *
 * @note Use this with qm_battery_gauge_set_soc() when the existing gauge
 *       object should keep its runtime counters. For cold boot or deep sleep,
 *       prefer qm_battery_gauge_init(..., restore, ...).
 *
 * @param[in] config Battery gauge config used for capacity, OCV, and thresholds.
 * @param[in] restore Restore inputs.
 * @param[out] result Restore result.
 * @return QM_EOK on success, negative error code otherwise.
 */
qm_err_t qm_battery_gauge_estimate_restore_soc(
    const qm_battery_gauge_config_t *config,
    const qm_battery_gauge_restore_config_t *restore,
    qm_battery_gauge_restore_result_t *result);

/**
 * @brief Update the gauge with one voltage/current sample.
 *
 * @param[in,out] gauge Gauge handle.
 * @param[in] sample Measured sample.
 * @return QM_EOK on success, negative error code otherwise.
 */
qm_err_t qm_battery_gauge_update_sample(
    qm_battery_gauge_t *gauge,
    const qm_battery_gauge_sample_t *sample);

/**
 * @brief Copy the current gauge output into a result structure.
 *
 * @param[in] gauge Gauge handle.
 * @param[out] result Destination result.
 * @return QM_EOK on success, negative error code otherwise.
 */
qm_err_t qm_battery_gauge_get_result(const qm_battery_gauge_t *gauge,
                                     qm_battery_gauge_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* QM_BATTERY_GAUGE_H */
