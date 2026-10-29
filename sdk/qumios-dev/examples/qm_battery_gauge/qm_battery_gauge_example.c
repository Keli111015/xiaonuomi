#include "qm_config.h"
#include "qm_battery_gauge.h"

#include "qm.h"
#include "qm_log.h"

#define LOG_TAG "qm_battery_gauge_example"

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_DESIGN_CAPACITY_MAH
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_DESIGN_CAPACITY_MAH 5200
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_USABLE_CAPACITY_MAH
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_USABLE_CAPACITY_MAH 5300
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_R_INTERNAL_MOHM
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_R_INTERNAL_MOHM 80
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SAVED_SOC_ENABLE
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SAVED_SOC_ENABLE 1
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SAVED_SOC_PERCENT
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SAVED_SOC_PERCENT 100
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SLEEP_RESUME_ENABLE
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SLEEP_RESUME_ENABLE 1
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_POWEROFF_RESUME_ENABLE
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_POWEROFF_RESUME_ENABLE 1
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_UNKNOWN_OFFLINE_TIME_ENABLE
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_UNKNOWN_OFFLINE_TIME_ENABLE 1
#endif

#ifndef CONFIG_QM_BATTERY_GAUGE_EXAMPLE_STEP_DELAY_MS
#define CONFIG_QM_BATTERY_GAUGE_EXAMPLE_STEP_DELAY_MS 250
#endif

typedef struct {
    const char *event;
    qm_float voltage_v;
    qm_float current_ma;
    qm_float dt_s;
    qm_bool charging;
    qm_bool charge_full;
} qm_battery_gauge_example_point_t;

static qm_battery_gauge_t s_gauge;
static qm_battery_gauge_state_t s_last_state = QM_BATTERY_GAUGE_STATE_UNKNOWN;
static qm_bool s_last_low = QM_FALSE;
static qm_bool s_last_empty = QM_FALSE;
static qm_bool s_last_full = QM_FALSE;
static qm_bool s_last_relaxed = QM_FALSE;

static void qm_battery_gauge_example_reset_log_state(void)
{
    s_last_state = QM_BATTERY_GAUGE_STATE_UNKNOWN;
    s_last_low = QM_FALSE;
    s_last_empty = QM_FALSE;
    s_last_full = QM_FALSE;
    s_last_relaxed = QM_FALSE;
}

/*
 * 示例曲线使用测试表里的 1A 左右放电数据做演示：
 * - current_ma < 0 表示放电
 * - current_ma > 0 表示充电
 * - dt_s 表示该工况持续时间，组件会用它做库仑计积分
 *
 * 真实产品接入时，这里不需要保留，直接把采样芯片读到的电压/电流
 * 填到 qm_battery_gauge_sample_t 后调用 qm_battery_gauge_update_sample()。
 */
static const qm_battery_gauge_example_point_t s_trace[] = {
    {"boot_full",           4.120f,     0.0f, 10.0f, QM_FALSE, QM_FALSE},
    {"discharge_95",        4.022f, -1040.9f, 915.0f, QM_FALSE, QM_FALSE},
    {"discharge_90",        3.983f, -1040.9f, 915.0f, QM_FALSE, QM_FALSE},
    {"discharge_80",        3.877f, -1040.9f, 1830.0f, QM_FALSE, QM_FALSE},
    {"discharge_70",        3.782f, -1040.9f, 1830.0f, QM_FALSE, QM_FALSE},
    {"discharge_60",        3.683f, -1040.9f, 1830.0f, QM_FALSE, QM_FALSE},
    {"discharge_50",        3.602f, -1040.9f, 1830.0f, QM_FALSE, QM_FALSE},
    {"discharge_40",        3.552f, -1040.9f, 1830.0f, QM_FALSE, QM_FALSE},
    {"discharge_30",        3.508f, -1040.9f, 1830.0f, QM_FALSE, QM_FALSE},
    {"discharge_20",        3.451f, -1040.9f, 1830.0f, QM_FALSE, QM_FALSE},
    {"discharge_10",        3.347f, -1040.9f, 1830.0f, QM_FALSE, QM_FALSE},
    {"discharge_5",         3.262f, -1040.9f,  915.0f, QM_FALSE, QM_FALSE},
    {"cutoff",              2.749f, -1040.9f,  916.0f, QM_FALSE, QM_FALSE},
    {"rest_recovery",       3.088f,     0.0f, 300.0f, QM_FALSE, QM_FALSE},
    {"charge_start",        3.176f,  1040.9f, 300.0f, QM_TRUE,  QM_FALSE},
    {"charge_mid",          3.650f,  1040.9f, 3600.0f, QM_TRUE,  QM_FALSE},
    {"charge_top",          4.190f,   260.0f, 1800.0f, QM_TRUE,  QM_FALSE},
    {"charge_full_signal",  4.201f,   120.0f,   4.0f, QM_TRUE,  QM_TRUE},
    {"charge_full_confirm", 4.201f,   110.0f,   4.0f, QM_TRUE,  QM_TRUE},
    {"unplug_idle",         4.120f,     0.0f, 300.0f, QM_FALSE, QM_FALSE},
};

static void qm_battery_gauge_example_run_sleep_resume(
    const qm_battery_gauge_config_t *cfg)
{
    qm_battery_gauge_result_t before_sleep;
    qm_battery_gauge_result_t after_wakeup;
    qm_battery_gauge_snapshot_t snapshot;
    qm_battery_gauge_restore_result_t restore_result;
    qm_battery_gauge_restore_config_t restore = { 0 };
    qm_battery_gauge_sample_t wake_sample = {
        .voltage_v = 4.010f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };

    if (qm_battery_gauge_get_result(&s_gauge, &before_sleep) != QM_EOK) {
        return;
    }
    if (qm_battery_gauge_make_snapshot(&s_gauge, &snapshot, 1, 1000) != QM_EOK) {
        return;
    }

    /*
     * restore 是这一次“从睡眠恢复”要喂给组件的上下文：
     * snapshot 来自睡前快照，其余字段来自唤醒时采样和应用层掌握的休眠信息。
     */
    restore.snapshot = &snapshot;
    restore.offline_time_valid = QM_TRUE;
    restore.offline_time_s = 10.0f * 3600.0f;
    restore.offline_current_ma = 0.080f;
    restore.wake_voltage_v = wake_sample.voltage_v;
    restore.wake_current_ma = wake_sample.current_ma;
    restore.wake_temperature_c = wake_sample.temperature_c;
    restore.has_temperature = wake_sample.has_temperature;
    restore.charging = wake_sample.charging;
    restore.charge_full = wake_sample.charge_full;

    /*
     * 深睡期间没有连续电流采样，不要把 10 小时直接塞进 dt_s。
     * 组件用平均睡眠电流扣减 SOC，再用唤醒静置电压做轻量融合。
     */
    if (qm_battery_gauge_estimate_restore_soc(cfg,
                                              &restore,
                                              &restore_result) != QM_EOK) {
        return;
    }

    qm_battery_gauge_set_soc(&s_gauge, restore_result.final_soc_percent);
    qm_battery_gauge_update_sample(&s_gauge, &wake_sample);

    if (qm_battery_gauge_get_result(&s_gauge, &after_wakeup) == QM_EOK) {
        QM_LOGI(LOG_TAG,
                "sleep resume: saved=%.2f final=%.2f wake_v=%.3f soc=%.2f",
                before_sleep.soc_percent,
                restore_result.final_soc_percent,
                wake_sample.voltage_v,
                after_wakeup.soc_percent);
    }
}

static void qm_battery_gauge_example_run_poweroff_resume(
    const qm_battery_gauge_config_t *cfg)
{
    qm_battery_gauge_result_t result;
    qm_battery_gauge_t saved_gauge;
    qm_battery_gauge_config_t saved_cfg;
    qm_battery_gauge_snapshot_t snapshot;
    qm_battery_gauge_restore_config_t restore = { 0 };
    qm_battery_gauge_restore_result_t restore_result;
    qm_battery_gauge_sample_t saved_sample = {
        .voltage_v = 3.950f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };
    qm_battery_gauge_sample_t boot_sample = {
        .voltage_v = 3.900f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };

    /*
     * 断电关机恢复示例：
     * 假设最近一次快照保存了 72%，设备断电 2 天后开机。
     * 快照结构和恢复 SOC 的融合策略由组件提供，存储完整性交给 qm kv。
     */
    saved_cfg = *cfg;
    saved_cfg.initial_soc_valid = QM_TRUE;
    saved_cfg.initial_soc_percent = 72.0f;
    if (qm_battery_gauge_init(&saved_gauge, &saved_cfg, NULL, NULL) != QM_EOK ||
        qm_battery_gauge_update_sample(&saved_gauge, &saved_sample) != QM_EOK ||
        qm_battery_gauge_make_snapshot(&saved_gauge, &snapshot, 10, 1000) != QM_EOK) {
        return;
    }

    /* 断电重启时，restore 由“快照 + 开机采样 + 离线时间估计”共同组成。 */
    restore.snapshot = &snapshot;
    restore.offline_time_valid = QM_TRUE;
    restore.offline_time_s = 2.0f * 24.0f * 3600.0f;
    restore.offline_current_ma = 0.020f;
    restore.wake_voltage_v = boot_sample.voltage_v;
    restore.wake_current_ma = boot_sample.current_ma;
    restore.wake_temperature_c = boot_sample.temperature_c;
    restore.has_temperature = boot_sample.has_temperature;
    restore.charging = boot_sample.charging;
    restore.charge_full = boot_sample.charge_full;

    if (qm_battery_gauge_init(&s_gauge,
                              cfg,
                              &restore,
                              &restore_result) != QM_EOK) {
        return;
    }

    qm_battery_gauge_update_sample(&s_gauge, &boot_sample);

    if (qm_battery_gauge_get_result(&s_gauge, &result) == QM_EOK) {
        QM_LOGI(LOG_TAG,
                "poweroff resume: saved=%.2f final=%.2f wake_v=%.3f soc=%.2f",
                snapshot.soc_percent,
                restore_result.final_soc_percent,
                boot_sample.voltage_v,
                result.soc_percent);
    }
}

static void qm_battery_gauge_example_run_unknown_offline_time_restore(
    const qm_battery_gauge_config_t *cfg)
{
    qm_battery_gauge_t saved_gauge;
    qm_battery_gauge_config_t saved_cfg;
    qm_battery_gauge_snapshot_t snapshot;
    qm_battery_gauge_restore_config_t restore = { 0 };
    qm_battery_gauge_restore_result_t restore_result;
    qm_battery_gauge_result_t result;
    qm_battery_gauge_sample_t saved_sample = {
        .voltage_v = 3.930f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };
    qm_battery_gauge_sample_t wake_sample = {
        .voltage_v = 3.860f,
        .current_ma = 0.0f,
        .temperature_c = 25.0f,
        .dt_s = 1.0f,
        .has_temperature = QM_FALSE,
        .charging = QM_FALSE,
        .charge_full = QM_FALSE,
    };

    /*
     * 未知离线时长恢复示例：
     * 手上只有上次快照和当前开机电压，不知道设备关机/休眠了多久。
     * 组件会走保守融合：保存 SOC 和唤醒电压 SOC 各占一半。
     */
    saved_cfg = *cfg;
    saved_cfg.initial_soc_valid = QM_TRUE;
    saved_cfg.initial_soc_percent = 68.0f;
    if (qm_battery_gauge_init(&saved_gauge, &saved_cfg, NULL, NULL) != QM_EOK ||
        qm_battery_gauge_update_sample(&saved_gauge, &saved_sample) != QM_EOK ||
        qm_battery_gauge_make_snapshot(&saved_gauge, &snapshot, 20, 2000) != QM_EOK) {
        return;
    }

    /*
     * offline_time_valid = QM_FALSE 表示应用层不知道离线多久。
     * 这种情况下组件不会按离线电流扣减容量，而会做保守融合。
     */
    restore.snapshot = &snapshot;
    restore.offline_time_valid = QM_FALSE;
    restore.offline_current_ma = 0.0f;
    restore.wake_voltage_v = wake_sample.voltage_v;
    restore.wake_current_ma = wake_sample.current_ma;
    restore.wake_temperature_c = wake_sample.temperature_c;
    restore.has_temperature = wake_sample.has_temperature;
    restore.charging = wake_sample.charging;
    restore.charge_full = wake_sample.charge_full;

    if (qm_battery_gauge_init(&s_gauge,
                              cfg,
                              &restore,
                              &restore_result) != QM_EOK) {
        return;
    }

    qm_battery_gauge_update_sample(&s_gauge, &wake_sample);

    if (qm_battery_gauge_get_result(&s_gauge, &result) == QM_EOK) {
        QM_LOGI(LOG_TAG,
                "unknown offline: saved=%.2f voltage_soc=%.2f final=%.2f soc=%.2f",
                snapshot.soc_percent,
                restore_result.voltage_soc_percent,
                restore_result.final_soc_percent,
                result.soc_percent);
    }
}

static const char *qm_battery_gauge_example_state_name(qm_battery_gauge_state_t state)
{
    switch (state) {
    case QM_BATTERY_GAUGE_STATE_IDLE:
        return "idle";
    case QM_BATTERY_GAUGE_STATE_CHARGING:
        return "charging";
    case QM_BATTERY_GAUGE_STATE_DISCHARGING:
        return "discharging";
    case QM_BATTERY_GAUGE_STATE_FULL:
        return "full";
    case QM_BATTERY_GAUGE_STATE_LOW:
        return "low";
    case QM_BATTERY_GAUGE_STATE_EMPTY:
        return "empty";
    default:
        return "unknown";
    }
}

static const char *qm_battery_gauge_example_mode_name(
    qm_battery_gauge_current_mode_t mode)
{
    switch (mode) {
    case QM_BATTERY_GAUGE_CURRENT_FULL:
        return "full-current";
    case QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY:
        return "charge-only";
    case QM_BATTERY_GAUGE_CURRENT_VOLTAGE_ONLY:
        return "voltage-only";
    default:
        return "unknown-mode";
    }
}

static qm_bool qm_battery_gauge_example_current_valid(
    qm_battery_gauge_current_mode_t mode,
    const qm_battery_gauge_example_point_t *point)
{
    if (mode == QM_BATTERY_GAUGE_CURRENT_FULL) {
        return QM_TRUE;
    }
    if (mode == QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY) {
        return point->charging ? QM_TRUE : QM_FALSE;
    }
    return QM_FALSE;
}

static void qm_battery_gauge_example_log_result(
    int step,
    const qm_battery_gauge_example_point_t *point,
    const qm_battery_gauge_result_t *result)
{
    if (!point || !result) {
        return;
    }

    /* result 里包含业务常用的电量、剩余容量、补偿电压和状态标志。 */
    QM_LOGI(LOG_TAG,
            "step=%d event=%s v=%.3f i=%.1f comp_v=%.3f dt=%.0f charging=%d charge_full=%d soc=%.2f raw=%.2f remain=%.1f discharge=%.1f charge=%.1f state=%s relaxed=%d low=%d empty=%d full=%d",
            step,
            point->event,
            result->voltage_v,
            result->current_ma,
            result->compensated_voltage_v,
            point->dt_s,
            point->charging,
            point->charge_full,
            result->soc_percent,
            result->raw_soc_percent,
            result->remaining_mah,
            result->discharged_mah,
            result->charged_mah,
            qm_battery_gauge_example_state_name(result->state),
            result->relaxed,
            result->low,
            result->empty,
            result->full);

    if (result->state != s_last_state) {
        QM_LOGI(LOG_TAG,
                "state change: %s -> %s at step=%d event=%s soc=%.2f",
                qm_battery_gauge_example_state_name(s_last_state),
                qm_battery_gauge_example_state_name(result->state),
                step,
                point->event,
                result->soc_percent);
        s_last_state = result->state;
    }
    if (result->low != s_last_low) {
        QM_LOGI(LOG_TAG, "flag change: low=%d at step=%d soc=%.2f",
                result->low, step, result->soc_percent);
        s_last_low = result->low;
    }
    if (result->empty != s_last_empty) {
        QM_LOGI(LOG_TAG, "flag change: empty=%d at step=%d soc=%.2f",
                result->empty, step, result->soc_percent);
        s_last_empty = result->empty;
    }
    if (result->full != s_last_full) {
        QM_LOGI(LOG_TAG, "flag change: full=%d at step=%d soc=%.2f",
                result->full, step, result->soc_percent);
        s_last_full = result->full;
    }
    if (result->relaxed != s_last_relaxed) {
        QM_LOGI(LOG_TAG, "flag change: relaxed=%d at step=%d comp_v=%.3f",
                result->relaxed, step, result->compensated_voltage_v);
        s_last_relaxed = result->relaxed;
    }
}

static void qm_battery_gauge_example_run_mode_trace(
    const qm_battery_gauge_config_t *base_cfg,
    qm_battery_gauge_current_mode_t mode)
{
    qm_battery_gauge_config_t cfg = *base_cfg;
    qm_battery_gauge_result_t result;
    int i;

    cfg.current_mode = mode;
    if (qm_battery_gauge_init(&s_gauge, &cfg, NULL, NULL) != QM_EOK) {
        QM_LOGE(LOG_TAG, "mode init failed: %s",
                qm_battery_gauge_example_mode_name(mode));
        return;
    }

    qm_battery_gauge_example_reset_log_state();
    QM_LOGI(LOG_TAG, "run current mode: %s",
            qm_battery_gauge_example_mode_name(mode));

    for (i = 0; i < (int)(sizeof(s_trace) / sizeof(s_trace[0])); i++) {
        qm_bool current_valid =
            qm_battery_gauge_example_current_valid(mode, &s_trace[i]);
        /*
         * sample 是每次 update 的输入帧：
         * voltage_v/current_ma 是本次测量值，dt_s 是距离上次 update 的秒数。
         * current_valid 用来区分“真实 0mA”和“这次根本没有测到电流”。
         */
        qm_battery_gauge_sample_t sample = {
            .voltage_v = s_trace[i].voltage_v,
            .current_ma = s_trace[i].current_ma,
            .temperature_c = 25.0f,
            .dt_s = s_trace[i].dt_s,
            .current_valid = current_valid,
            .has_temperature = QM_FALSE,
            .charging = s_trace[i].charging,
            .charge_full = s_trace[i].charge_full,
        };

        if (qm_battery_gauge_update_sample(&s_gauge, &sample) != QM_EOK) {
            QM_LOGE(LOG_TAG, "update failed mode=%s step=%d event=%s",
                    qm_battery_gauge_example_mode_name(mode),
                    i,
                    s_trace[i].event);
            return;
        }
        if (qm_battery_gauge_get_result(&s_gauge, &result) != QM_EOK) {
            QM_LOGE(LOG_TAG, "get result failed at step=%d", i);
            return;
        }
        qm_battery_gauge_example_log_result(i, &s_trace[i], &result);
        qm_msleep(CONFIG_QM_BATTERY_GAUGE_EXAMPLE_STEP_DELAY_MS);
    }
}

static void qm_battery_gauge_example_run_trace(const qm_battery_gauge_config_t *cfg)
{
    qm_battery_gauge_example_run_mode_trace(cfg, QM_BATTERY_GAUGE_CURRENT_FULL);
    qm_battery_gauge_example_run_mode_trace(cfg, QM_BATTERY_GAUGE_CURRENT_CHARGE_ONLY);
    qm_battery_gauge_example_run_mode_trace(cfg, QM_BATTERY_GAUGE_CURRENT_VOLTAGE_ONLY);
}

void qm_battery_gauge_example_start(void)
{
    qm_battery_gauge_config_t cfg;

    qm_battery_gauge_default_config(&cfg);
    /*
     * 这些是产品需要根据目标电池重新标定的参数。
     * 换电芯时至少要更新容量、等效内阻、满电/低电/截止阈值和 OCV 表。
     * usable_capacity_mah 用于库仑计积分，internal_resistance_ohm 用于负载补偿。
     */
    cfg.design_capacity_mah =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_EXAMPLE_DESIGN_CAPACITY_MAH;
    cfg.usable_capacity_mah =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_EXAMPLE_USABLE_CAPACITY_MAH;
    cfg.internal_resistance_ohm =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_EXAMPLE_R_INTERNAL_MOHM / 1000.0f;
    cfg.max_update_dt_s = 7200.0f;

#if CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SAVED_SOC_ENABLE
    /*
     * 断电关机恢复入口：
     * 最简单场景可直接填 initial_soc_percent；完整快照恢复见
     * qm_battery_gauge_init() 的 restore 入参示例。
     * 这里适合“应用层已经有一个可信 SOC”的简单恢复方式。
     */
    cfg.initial_soc_valid = QM_TRUE;
    cfg.initial_soc_percent =
        (qm_float)CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SAVED_SOC_PERCENT;
#endif

    QM_LOGI(LOG_TAG,
            "battery gauge init ok: design=%.0fmAh usable=%.0fmAh r=%.0fmOhm saved_soc=%d initial=%.1f%%",
            cfg.design_capacity_mah,
            cfg.usable_capacity_mah,
            cfg.internal_resistance_ohm * 1000.0f,
            cfg.initial_soc_valid,
            cfg.initial_soc_percent);

    qm_battery_gauge_example_run_trace(&cfg);
#if CONFIG_QM_BATTERY_GAUGE_EXAMPLE_SLEEP_RESUME_ENABLE
    cfg.current_mode = QM_BATTERY_GAUGE_CURRENT_FULL;
    if (qm_battery_gauge_init(&s_gauge, &cfg, NULL, NULL) != QM_EOK) {
        QM_LOGE(LOG_TAG, "battery gauge init failed before sleep resume");
        return;
    }
    qm_battery_gauge_example_run_sleep_resume(&cfg);
#endif
#if CONFIG_QM_BATTERY_GAUGE_EXAMPLE_POWEROFF_RESUME_ENABLE
    qm_battery_gauge_example_run_poweroff_resume(&cfg);
#endif
#if CONFIG_QM_BATTERY_GAUGE_EXAMPLE_UNKNOWN_OFFLINE_TIME_ENABLE
    qm_battery_gauge_example_run_unknown_offline_time_restore(&cfg);
#endif
}

void qm_application_start(void)
{
    qm_battery_gauge_example_start();
}
