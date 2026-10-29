#include "qm_config.h"
#include "qm_battery.h"

#include "qm.h"
#include "qm_log.h"

#define LOG_TAG "qm_battery_example"

#ifndef CONFIG_QM_BATTERY_EXAMPLE_USE_ADC
#define CONFIG_QM_BATTERY_EXAMPLE_USE_ADC 0
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_ADC_PORT
#define CONFIG_QM_BATTERY_EXAMPLE_ADC_PORT 0
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_ADC_PIN
#define CONFIG_QM_BATTERY_EXAMPLE_ADC_PIN 2
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_ADC_SCALE_V_PER_COUNT
#define CONFIG_QM_BATTERY_EXAMPLE_ADC_SCALE_V_PER_COUNT 0.001f
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_ADC_OFFSET_V
#define CONFIG_QM_BATTERY_EXAMPLE_ADC_OFFSET_V 0.0f
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_ADC_DIVIDER_RATIO
#define CONFIG_QM_BATTERY_EXAMPLE_ADC_DIVIDER_RATIO 1.0f
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_R_NOMINAL_MOHM
#define CONFIG_QM_BATTERY_EXAMPLE_R_NOMINAL_MOHM 60
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_FULL_ANCHOR_MV
#define CONFIG_QM_BATTERY_EXAMPLE_FULL_ANCHOR_MV 4180
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_EMPTY_ANCHOR_MV
#define CONFIG_QM_BATTERY_EXAMPLE_EMPTY_ANCHOR_MV 3300
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_CHARGE_FULL_MIN_MV
#define CONFIG_QM_BATTERY_EXAMPLE_CHARGE_FULL_MIN_MV 4050
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_ENABLE
#define CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_ENABLE 1
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_PERCENT
#define CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_PERCENT 72
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_S
#define CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_S 7200
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_KNOWN
#define CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_KNOWN 1
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_ENABLE
#define CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_ENABLE 1
#endif

#ifndef CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_MV
#define CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_MV 4120
#endif

static qm_battery_t s_battery;
static qm_bool s_last_charge_full_confirmed = QM_FALSE;
static qm_bool s_last_full_guard_active = QM_FALSE;
static qm_bool s_last_relaxed = QM_FALSE;

typedef struct {
    const char *event;
    qm_float voltage_v;
    qm_bool charging;
    qm_bool charge_full;
} qm_battery_example_trace_point_t;

static const qm_battery_example_trace_point_t s_example_trace[] = {
    {"battery_only_stable",        4.100f, QM_FALSE, QM_FALSE},
    {"battery_only_stable",        4.096f, QM_FALSE, QM_FALSE},
    {"battery_only_stable",        4.093f, QM_FALSE, QM_FALSE},
    {"battery_only_stable",        4.091f, QM_FALSE, QM_FALSE},
    {"plug_in_voltage_step_up",    4.155f, QM_TRUE,  QM_FALSE},
    {"plug_in_settling",           4.142f, QM_TRUE,  QM_FALSE},
    {"charge_ramp",                4.150f, QM_TRUE,  QM_FALSE},
    {"charge_voltage_spike_up",    4.205f, QM_TRUE,  QM_FALSE},
    {"charge_spike_recovery",      4.152f, QM_TRUE,  QM_FALSE},
    {"charge_ramp",                4.156f, QM_TRUE,  QM_FALSE},
    {"charge_full_signal",         4.158f, QM_TRUE,  QM_TRUE},
    {"charge_full_signal",         4.157f, QM_TRUE,  QM_TRUE},
    {"charge_full_signal",         4.156f, QM_TRUE,  QM_TRUE},
    {"charge_full_signal",         4.155f, QM_TRUE,  QM_TRUE},
    {"charge_full_signal",         4.154f, QM_TRUE,  QM_TRUE},
    {"charge_full_signal",         4.153f, QM_TRUE,  QM_TRUE},
    {"charge_full_signal",         4.152f, QM_TRUE,  QM_TRUE},
    {"charge_full_confirmed",      4.151f, QM_TRUE,  QM_TRUE},
    {"unplug_voltage_step_down",   4.090f, QM_FALSE, QM_FALSE},
    {"load_voltage_sag_down",      3.980f, QM_FALSE, QM_FALSE},
    {"load_sag_recovery",          4.070f, QM_FALSE, QM_FALSE},
    {"battery_only_after_unplug",  4.064f, QM_FALSE, QM_FALSE},
    {"battery_only_after_unplug",  4.058f, QM_FALSE, QM_FALSE},
};

static void qm_battery_example_log_result(const qm_battery_result_t *result,
                                          int step,
                                          const qm_battery_example_trace_point_t *point)
{
    if (!result || !point) {
        return;
    }

    QM_LOGI(LOG_TAG,
            "step=%d event=%s v=%.3f dvdt=%.3f charging=%d full=%d full_ok=%d guard=%d soc=%.2f soh=%.2f r=%.4f learned_full=%.3f learned_ok=%d relaxed=%d",
            step,
            point->event,
            result->voltage_v,
            result->dvdt_vps,
            point->charging,
            point->charge_full,
            result->charge_full_confirmed,
            result->full_guard_active,
            result->soc_percent,
            result->soh_percent,
            result->resistance_ohm,
            result->learned_full_v,
            result->learned_full_valid,
            result->relaxed);

    if (result->charge_full_confirmed != s_last_charge_full_confirmed) {
        QM_LOGI(LOG_TAG,
                "state change: full_confirmed=%d at step=%d event=%s soc=%.2f learned_full=%.3f",
                result->charge_full_confirmed,
                step,
                point->event,
                result->soc_percent,
                result->learned_full_v);
        s_last_charge_full_confirmed = result->charge_full_confirmed;
    }
    if (result->full_guard_active != s_last_full_guard_active) {
        QM_LOGI(LOG_TAG,
                "state change: full_guard=%d at step=%d event=%s soc=%.2f",
                result->full_guard_active,
                step,
                point->event,
                result->soc_percent);
        s_last_full_guard_active = result->full_guard_active;
    }
    if (result->relaxed != s_last_relaxed) {
        QM_LOGI(LOG_TAG,
                "state change: relaxed=%d at step=%d event=%s dvdt=%.3f",
                result->relaxed,
                step,
                point->event,
                result->dvdt_vps);
        s_last_relaxed = result->relaxed;
    }
}

static void qm_battery_example_run_trace(void)
{
    qm_battery_result_t result;
    int i;

    for (i = 0; i < (int)(sizeof(s_example_trace) / sizeof(s_example_trace[0])); i++) {
        qm_battery_update_voltage(&s_battery,
                                  s_example_trace[i].voltage_v,
                                  25.0f,
                                  QM_FALSE,
                                  s_example_trace[i].charging,
                                  s_example_trace[i].charge_full,
                                  1.0f);
        qm_battery_get_result(&s_battery, &result);
        qm_battery_example_log_result(&result, i, &s_example_trace[i]);
        qm_msleep(1000);
    }
}

#if CONFIG_QM_BATTERY_EXAMPLE_USE_ADC
static void qm_battery_example_run_adc(void)
{
#if defined(CONFIG_QM_ADC_SUPPORT) && CONFIG_QM_ADC_SUPPORT
    qm_adc_dev_t adc = {
        .port = CONFIG_QM_BATTERY_EXAMPLE_ADC_PORT,
        .pin.adc_pin = CONFIG_QM_BATTERY_EXAMPLE_ADC_PIN,
        .priv = NULL,
    };
    qm_battery_result_t result;
    qm_battery_example_trace_point_t point = {
        "adc_sample",
        0.0f,
        QM_FALSE,
        QM_FALSE,
    };
    int i;

    if (qm_adc_init(&adc) != QM_EOK) {
        QM_LOGW(LOG_TAG, "adc init failed, fall back to trace");
        qm_battery_example_run_trace();
        return;
    }

    for (i = 0; i < 12; i++) {
        if (qm_battery_update_adc(&s_battery, &adc, QM_FALSE, QM_FALSE, 1.0f, 0) == QM_EOK) {
            qm_battery_get_result(&s_battery, &result);
            point.voltage_v = result.voltage_v;
            qm_battery_example_log_result(&result, i, &point);
        }
        qm_msleep(1000);
    }

    qm_adc_deinit(&adc);
#else
    qm_battery_example_run_trace();
#endif
}
#endif

static void qm_battery_example_apply_calibration(qm_battery_config_t *cfg)
{
    qm_battery_calibration_t calibration;
    int ocv_count = 0;

    if (!cfg) {
        return;
    }

    calibration.ocv_table = qm_battery_ocv_default_table(&ocv_count);
    calibration.ocv_table_count = ocv_count;
    calibration.adc_scale_v_per_count = CONFIG_QM_BATTERY_EXAMPLE_ADC_SCALE_V_PER_COUNT;
    calibration.adc_offset_v = CONFIG_QM_BATTERY_EXAMPLE_ADC_OFFSET_V;
    calibration.adc_divider_ratio = CONFIG_QM_BATTERY_EXAMPLE_ADC_DIVIDER_RATIO;
    calibration.r_nominal_ohm =
        (qm_float)CONFIG_QM_BATTERY_EXAMPLE_R_NOMINAL_MOHM / 1000.0f;
    calibration.full_anchor_v =
        (qm_float)CONFIG_QM_BATTERY_EXAMPLE_FULL_ANCHOR_MV / 1000.0f;
    calibration.empty_anchor_v =
        (qm_float)CONFIG_QM_BATTERY_EXAMPLE_EMPTY_ANCHOR_MV / 1000.0f;
    calibration.charge_full_min_v =
        (qm_float)CONFIG_QM_BATTERY_EXAMPLE_CHARGE_FULL_MIN_MV / 1000.0f;

    if (qm_battery_apply_calibration(cfg, &calibration) != QM_EOK) {
        QM_LOGW(LOG_TAG, "battery calibration import failed, use defaults");
        return;
    }
    QM_LOGI(LOG_TAG,
            "calibration imported: ocv=%d adc_scale=%.6f divider=%.3f r_nominal=%.4f full=%.3f empty=%.3f charge_full_min=%.3f",
            ocv_count,
            calibration.adc_scale_v_per_count,
            calibration.adc_divider_ratio,
            calibration.r_nominal_ohm,
            calibration.full_anchor_v,
            calibration.empty_anchor_v,
            calibration.charge_full_min_v);
}

void qm_battery_example_start(void)
{
    qm_battery_config_t cfg;

    qm_battery_default_config(&cfg);
    qm_battery_example_apply_calibration(&cfg);
#if CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_ENABLE
    cfg.initial_soc_valid = QM_TRUE;
    cfg.initial_soc_percent = (qm_float)CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_PERCENT;
#if CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_KNOWN
    cfg.initial_soc_age_s = (qm_float)CONFIG_QM_BATTERY_EXAMPLE_SAVED_SOC_AGE_S;
#else
    cfg.initial_soc_age_s = cfg.initial_soc_max_age_s + 1.0f;
#endif
    QM_LOGI(LOG_TAG,
            "restore saved soc: soc=%.2f age_s=%.0f max_age_s=%.0f standby_drain=%.2f%%/day",
            cfg.initial_soc_percent,
            cfg.initial_soc_age_s,
            cfg.initial_soc_max_age_s,
            cfg.standby_drain_percent_per_day);
#else
    QM_LOGI(LOG_TAG, "restore saved soc: disabled");
#endif
#if CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_ENABLE
    cfg.learned_full_valid = QM_TRUE;
    cfg.learned_full_v = (qm_float)CONFIG_QM_BATTERY_EXAMPLE_LEARNED_FULL_MV / 1000.0f;
    QM_LOGI(LOG_TAG, "restore learned full: %.3f", cfg.learned_full_v);
#else
    QM_LOGI(LOG_TAG, "restore learned full: disabled");
#endif
    if (qm_battery_init(&s_battery, &cfg) != QM_EOK) {
        QM_LOGE(LOG_TAG, "battery init failed");
        return;
    }
    s_last_charge_full_confirmed = QM_FALSE;
    s_last_full_guard_active = QM_FALSE;
    s_last_relaxed = QM_FALSE;
    QM_LOGI(LOG_TAG,
            "battery init ok: charge_settle=%.0fs full_confirm=%.0fs full_guard=%.0fs guard_min=%.1f%%",
            cfg.charge_settle_s,
            cfg.charge_full_confirm_s,
            cfg.full_guard_s,
            cfg.full_guard_min_percent);

#if CONFIG_QM_BATTERY_EXAMPLE_USE_ADC
    qm_battery_example_run_adc();
#else
    qm_battery_example_run_trace();
#endif
}

void qm_application_start(void)
{
    qm_battery_example_start();
}
