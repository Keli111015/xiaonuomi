#include "qm_battery_gauge.h"

#include <stdio.h>

static const qm_battery_gauge_ocv_point_t s_valid_ocv[] = {
    {  0.0f, 3.000f, 0.0f },
    { 20.0f, 3.500f, 0.0f },
    { 40.0f, 3.650f, 0.0f },
    { 60.0f, 3.800f, 0.0f },
    { 80.0f, 3.950f, 0.0f },
    {100.0f, 4.200f, 0.0f },
};

static const qm_battery_gauge_ocv_point_t s_bad_voltage_ocv[] = {
    {  0.0f, 3.000f, 0.0f },
    { 50.0f, 3.800f, 0.0f },
    {100.0f, 3.700f, 0.0f },
};

static const qm_battery_gauge_ocv_point_t s_bad_soc_ocv[] = {
    {  0.0f, 3.000f, 0.0f },
    { 50.0f, 3.800f, 0.0f },
    { 40.0f, 4.000f, 0.0f },
};

#define TEST_ASSERT(expr) do { \
    if (!(expr)) { \
        printf("assert failed: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        return 1; \
    } \
} while (0)

static int test_default_config_init(void)
{
    qm_battery_gauge_t gauge;

    TEST_ASSERT(qm_battery_gauge_init(&gauge, NULL, NULL, NULL) == QM_EOK);
    return 0;
}

static int test_invalid_ocv_table(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_config_t cfg;

    qm_battery_gauge_default_config(&cfg);
    cfg.ocv_table = 0;
    cfg.ocv_table_count = 0;
    TEST_ASSERT(qm_battery_gauge_init(&gauge, &cfg, NULL, NULL) == -QM_EINVAL);

    qm_battery_gauge_default_config(&cfg);
    cfg.ocv_table = s_bad_voltage_ocv;
    cfg.ocv_table_count = sizeof(s_bad_voltage_ocv) / sizeof(s_bad_voltage_ocv[0]);
    TEST_ASSERT(qm_battery_gauge_init(&gauge, &cfg, NULL, NULL) == -QM_EINVAL);

    qm_battery_gauge_default_config(&cfg);
    cfg.ocv_table = s_bad_soc_ocv;
    cfg.ocv_table_count = sizeof(s_bad_soc_ocv) / sizeof(s_bad_soc_ocv[0]);
    TEST_ASSERT(qm_battery_gauge_init(&gauge, &cfg, NULL, NULL) == -QM_EINVAL);

    return 0;
}

static int test_discharge_to_cutoff(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_result_t result;
    qm_battery_gauge_sample_t sample = {
        .voltage_v = 2.740f,
        .current_ma = -1000.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };

    qm_battery_gauge_default_config(&cfg);
    cfg.initial_soc_valid = QM_TRUE;
    cfg.initial_soc_percent = 30.0f;
    TEST_ASSERT(qm_battery_gauge_init(&gauge, &cfg, NULL, NULL) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &sample) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_get_result(&gauge, &result) == QM_EOK);
    TEST_ASSERT(result.empty == QM_TRUE);
    TEST_ASSERT(result.soc_percent <= 0.01f);

    return 0;
}

static int test_charge_full_anchor(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_result_t result;
    qm_battery_gauge_sample_t sample = {
        .voltage_v = 4.201f,
        .current_ma = 120.0f,
        .temperature_c = 25.0f,
        .dt_s = 4.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_TRUE,
        .charge_full = QM_TRUE,
    };

    qm_battery_gauge_default_config(&cfg);
    cfg.initial_soc_valid = QM_TRUE;
    cfg.initial_soc_percent = 80.0f;
    TEST_ASSERT(qm_battery_gauge_init(&gauge, &cfg, NULL, NULL) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &sample) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &sample) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_get_result(&gauge, &result) == QM_EOK);
    TEST_ASSERT(result.full == QM_TRUE);
    TEST_ASSERT(result.soc_percent >= 99.9f);

    return 0;
}

static int test_current_sign_error(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_sample_t sample = {
        .voltage_v = 3.900f,
        .current_ma = -100.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_TRUE,
        .charge_full = QM_FALSE,
    };

    TEST_ASSERT(qm_battery_gauge_init(&gauge, NULL, NULL, NULL) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &sample) == -QM_EINVAL);
    return 0;
}

static int test_custom_ocv_table(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_sample_t sample = {
        .voltage_v = 3.800f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };

    qm_battery_gauge_default_config(&cfg);
    cfg.ocv_table = s_valid_ocv;
    cfg.ocv_table_count = sizeof(s_valid_ocv) / sizeof(s_valid_ocv[0]);
    TEST_ASSERT(qm_battery_gauge_init(&gauge, &cfg, NULL, NULL) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &sample) == QM_EOK);

    return 0;
}

static int test_charge_only_current_mode(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_result_t result;
    qm_battery_gauge_sample_t discharge_sample = {
        .voltage_v = 3.500f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 3600.0f,
        .current_valid = QM_FALSE,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };
    qm_battery_gauge_sample_t charge_sample = {
        .voltage_v = 3.700f,
        .current_ma = 1000.0f,
        .temperature_c = 25.0f,
        .dt_s = 3600.0f,
        .current_valid = QM_TRUE,
        .has_temperature = QM_FALSE,
        .charging = QM_TRUE,
        .charge_full = QM_FALSE,
    };

    qm_battery_gauge_default_config(&cfg);
    cfg.ocv_table = s_valid_ocv;
    cfg.ocv_table_count = sizeof(s_valid_ocv) / sizeof(s_valid_ocv[0]);
    cfg.current_mode = QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY;
    cfg.max_update_dt_s = 3600.0f;
    cfg.initial_soc_valid = QM_TRUE;
    cfg.initial_soc_percent = 80.0f;
    TEST_ASSERT(qm_battery_gauge_init(&gauge, &cfg, NULL, NULL) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &discharge_sample) ==
                QM_EOK);
    TEST_ASSERT(qm_battery_gauge_get_result(&gauge, &result) == QM_EOK);
    TEST_ASSERT(result.current_valid == QM_FALSE);
    TEST_ASSERT(result.discharged_mah <= 0.01f);
    TEST_ASSERT(result.raw_soc_percent < 80.0f);
    TEST_ASSERT(result.state == QM_BATTERY_GAUGE_STATE_DISCHARGING ||
                result.state == QM_BATTERY_GAUGE_STATE_LOW);

    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &charge_sample) ==
                QM_EOK);
    TEST_ASSERT(qm_battery_gauge_get_result(&gauge, &result) == QM_EOK);
    TEST_ASSERT(result.current_valid == QM_TRUE);
    TEST_ASSERT(result.charged_mah > 900.0f);
    return 0;
}

static int test_snapshot_validation(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_snapshot_t snapshot;
    qm_battery_gauge_restore_config_t restore = { 0 };
    qm_battery_gauge_restore_result_t restore_result;
    qm_battery_gauge_sample_t sample = {
        .voltage_v = 3.900f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };

    TEST_ASSERT(qm_battery_gauge_init(&gauge, NULL, NULL, NULL) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_make_snapshot(&gauge, &snapshot, 1, 100) ==
                -QM_EINVAL);
    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &sample) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_make_snapshot(&gauge, &snapshot, 1, 100) ==
                QM_EOK);
    qm_battery_gauge_default_config(&cfg);

    restore.snapshot = &snapshot;
    restore.offline_time_valid = QM_FALSE;
    restore.offline_current_ma = 0.0f;
    restore.wake_voltage_v = 3.900f;
    restore.wake_current_ma = 0.0f;
    restore.wake_temperature_c = 25.0f;
    restore.has_temperature = QM_FALSE;
    restore.charging = QM_FALSE;
    restore.charge_full = QM_FALSE;
    TEST_ASSERT(qm_battery_gauge_estimate_restore_soc(&cfg,
                                                      &restore,
                                                      &restore_result) ==
                QM_EOK);
    TEST_ASSERT(restore_result.snapshot_used == QM_TRUE);

    snapshot.soc_percent = 101.0f;
    TEST_ASSERT(qm_battery_gauge_estimate_restore_soc(&cfg,
                                                      &restore,
                                                      &restore_result) ==
                QM_EOK);
    TEST_ASSERT(restore_result.snapshot_used == QM_FALSE);
    return 0;
}

static int test_restore_with_snapshot(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_snapshot_t snapshot;
    qm_battery_gauge_restore_config_t restore = { 0 };
    qm_battery_gauge_restore_result_t result;
    qm_battery_gauge_sample_t sample = {
        .voltage_v = 3.950f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };

    qm_battery_gauge_default_config(&cfg);
    cfg.initial_soc_valid = QM_TRUE;
    cfg.initial_soc_percent = 80.0f;
    TEST_ASSERT(qm_battery_gauge_init(&gauge, &cfg, NULL, NULL) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_update_sample(&gauge, &sample) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_make_snapshot(&gauge, &snapshot, 7, 1000) ==
                QM_EOK);

    restore.snapshot = &snapshot;
    restore.offline_time_valid = QM_TRUE;
    restore.offline_time_s = 2.0f * 24.0f * 3600.0f;
    restore.offline_current_ma = 0.020f;
    restore.wake_voltage_v = 3.900f;
    restore.wake_current_ma = 0.0f;
    restore.wake_temperature_c = 25.0f;
    restore.has_temperature = QM_FALSE;
    restore.charging = QM_FALSE;
    restore.charge_full = QM_FALSE;

    TEST_ASSERT(qm_battery_gauge_estimate_restore_soc(&cfg, &restore, &result) ==
                QM_EOK);
    TEST_ASSERT(result.snapshot_used == QM_TRUE);
    TEST_ASSERT(result.offline_drain_mah > 0.9f);
    TEST_ASSERT(result.voltage_weight > 0.5f);
    TEST_ASSERT(result.final_soc_percent > 60.0f);
    TEST_ASSERT(result.final_soc_percent < 90.0f);
    return 0;
}

static int test_restore_without_snapshot(void)
{
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_restore_config_t restore = { 0 };
    qm_battery_gauge_restore_result_t result;

    qm_battery_gauge_default_config(&cfg);
    restore.snapshot = 0;
    restore.offline_time_valid = QM_FALSE;
    restore.offline_time_s = 0.0f;
    restore.offline_current_ma = 0.0f;
    restore.wake_voltage_v = 3.900f;
    restore.wake_current_ma = 0.0f;
    restore.wake_temperature_c = 25.0f;
    restore.has_temperature = QM_FALSE;
    restore.charging = QM_FALSE;
    restore.charge_full = QM_FALSE;

    TEST_ASSERT(qm_battery_gauge_estimate_restore_soc(&cfg, &restore, &result) ==
                QM_EOK);
    TEST_ASSERT(result.snapshot_used == QM_FALSE);
    TEST_ASSERT(result.voltage_used == QM_TRUE);
    TEST_ASSERT(result.final_soc_percent == result.voltage_soc_percent);
    return 0;
}

static int test_init_restore(void)
{
    qm_battery_gauge_t gauge;
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_restore_config_t restore = { 0 };
    qm_battery_gauge_restore_result_t result;
    qm_battery_gauge_result_t gauge_result;

    qm_battery_gauge_default_config(&cfg);
    restore.snapshot = 0;
    restore.offline_time_valid = QM_FALSE;
    restore.offline_current_ma = 0.0f;
    restore.wake_voltage_v = 3.900f;
    restore.wake_current_ma = 0.0f;
    restore.wake_temperature_c = 25.0f;
    restore.has_temperature = QM_FALSE;
    restore.charging = QM_FALSE;
    restore.charge_full = QM_FALSE;

    TEST_ASSERT(qm_battery_gauge_init(&gauge,
                                      &cfg,
                                      &restore,
                                      &result) == QM_EOK);
    TEST_ASSERT(qm_battery_gauge_get_result(&gauge, &gauge_result) == QM_EOK);
    TEST_ASSERT(gauge_result.soc_percent == result.final_soc_percent);
    return 0;
}

static int test_restore_charge_full_anchor(void)
{
    qm_battery_gauge_config_t cfg;
    qm_battery_gauge_restore_config_t restore = { 0 };
    qm_battery_gauge_restore_result_t result;

    qm_battery_gauge_default_config(&cfg);
    restore.snapshot = 0;
    restore.offline_time_valid = QM_FALSE;
    restore.offline_current_ma = 0.0f;
    restore.wake_voltage_v = cfg.full_voltage_v;
    restore.wake_current_ma = 120.0f;
    restore.wake_temperature_c = 25.0f;
    restore.has_temperature = QM_FALSE;
    restore.charging = QM_TRUE;
    restore.charge_full = QM_TRUE;

    TEST_ASSERT(qm_battery_gauge_estimate_restore_soc(&cfg, &restore, &result) ==
                QM_EOK);
    TEST_ASSERT(result.charge_full_used == QM_TRUE);
    TEST_ASSERT(result.final_soc_percent >= 99.9f);
    return 0;
}

int main(void)
{
    TEST_ASSERT(test_default_config_init() == 0);
    TEST_ASSERT(test_invalid_ocv_table() == 0);
    TEST_ASSERT(test_discharge_to_cutoff() == 0);
    TEST_ASSERT(test_charge_full_anchor() == 0);
    TEST_ASSERT(test_current_sign_error() == 0);
    TEST_ASSERT(test_custom_ocv_table() == 0);
    TEST_ASSERT(test_charge_only_current_mode() == 0);
    TEST_ASSERT(test_snapshot_validation() == 0);
    TEST_ASSERT(test_restore_with_snapshot() == 0);
    TEST_ASSERT(test_restore_without_snapshot() == 0);
    TEST_ASSERT(test_init_restore() == 0);
    TEST_ASSERT(test_restore_charge_full_anchor() == 0);
    return 0;
}
