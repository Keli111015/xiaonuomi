#include "qm_config.h"
#include "qm_timer_mgr.h"

#include "qm_kernel.h"
#include "qm_log.h"

#define LOG_TAG "qm_timer_mgr_example"

#ifndef CONFIG_QM_TIMER_MGR_EXAMPLE_COUNTDOWN_SEC
#define CONFIG_QM_TIMER_MGR_EXAMPLE_COUNTDOWN_SEC 5
#endif

#ifndef CONFIG_QM_TIMER_MGR_EXAMPLE_STOPWATCH_SEC
#define CONFIG_QM_TIMER_MGR_EXAMPLE_STOPWATCH_SEC 2
#endif

#ifndef CONFIG_QM_TIMER_MGR_EXAMPLE_TICK_MS
#define CONFIG_QM_TIMER_MGR_EXAMPLE_TICK_MS 1000
#endif

static qm_timer_mgr_t s_timer;

static const char *qm_timer_mgr_example_event_name(qm_timer_mgr_event_t event)
{
    switch (event) {
    case QM_TIMER_MGR_EVENT_STARTED:
        return "started";
    case QM_TIMER_MGR_EVENT_TICK:
        return "tick";
    case QM_TIMER_MGR_EVENT_PAUSED:
        return "paused";
    case QM_TIMER_MGR_EVENT_RESUMED:
        return "resumed";
    case QM_TIMER_MGR_EVENT_RESTARTED:
        return "restarted";
    case QM_TIMER_MGR_EVENT_STOPPED:
        return "stopped";
    case QM_TIMER_MGR_EVENT_FINISHED:
        return "finished";
    default:
        return "unknown";
    }
}

static void qm_timer_mgr_example_log_status(const char *tag,
                                            const qm_timer_mgr_status_t *status)
{
    if (!status) {
        return;
    }

    QM_LOGI(LOG_TAG,
            "%s mode=%s state=%s duration=%u elapsed=%u remaining=%u",
            tag,
            qm_timer_mgr_mode_str(status->mode),
            qm_timer_mgr_state_str(status->state),
            (unsigned int)status->duration_sec,
            (unsigned int)status->elapsed_sec,
            (unsigned int)status->remaining_sec);
}

static void qm_timer_mgr_example_event(qm_timer_mgr_t timer,
                                       const qm_timer_mgr_event_data_t *event)
{
    (void)timer;

    if (!event) {
        return;
    }

    QM_LOGI(LOG_TAG, "event=%s previous=%s",
            qm_timer_mgr_example_event_name(event->event),
            qm_timer_mgr_state_str(event->previous_state));
    qm_timer_mgr_example_log_status("event_status", &event->status);
}

static int qm_timer_mgr_example_check(const char *name, int ret)
{
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "%s failed ret=%d", name, ret);
        return ret;
    }

    QM_LOGI(LOG_TAG, "%s ok", name);
    return QM_EOK;
}

static int qm_timer_mgr_example_get_status(const char *tag)
{
    qm_timer_mgr_status_t status;
    int ret = qm_timer_mgr_get_status(s_timer, &status);

    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "get_status failed ret=%d", ret);
        return ret;
    }

    qm_timer_mgr_example_log_status(tag, &status);
    return QM_EOK;
}

void qm_timer_mgr_example_stop(void)
{
    if (!s_timer) {
        return;
    }

    (void)qm_timer_mgr_stop(s_timer);
    qm_timer_mgr_destroy(s_timer);
    s_timer = NULL;
}

/* Countdown: start -> tick -> pause -> resume -> stop -> restart -> finished. */
static void qm_timer_mgr_example_countdown(void)
{
    uint32_t duration = (uint32_t)CONFIG_QM_TIMER_MGR_EXAMPLE_COUNTDOWN_SEC;

    if (qm_timer_mgr_example_check("cd_start",
            qm_timer_mgr_start(s_timer, QM_TIMER_MGR_MODE_COUNTDOWN, duration)) != QM_EOK) {
        return;
    }

    qm_msleep(CONFIG_QM_TIMER_MGR_EXAMPLE_TICK_MS + 200);
    if (qm_timer_mgr_example_get_status("cd_after_ticks") != QM_EOK) {
        return;
    }

    if (qm_timer_mgr_example_check("cd_pause", qm_timer_mgr_pause(s_timer)) != QM_EOK) {
        return;
    }
    if (qm_timer_mgr_example_get_status("cd_after_pause") != QM_EOK) {
        return;
    }

    qm_msleep(500);
    if (qm_timer_mgr_example_check("cd_resume", qm_timer_mgr_resume(s_timer)) != QM_EOK) {
        return;
    }

    qm_msleep(CONFIG_QM_TIMER_MGR_EXAMPLE_TICK_MS + 200);
    if (qm_timer_mgr_example_check("cd_stop", qm_timer_mgr_stop(s_timer)) != QM_EOK) {
        return;
    }
    if (qm_timer_mgr_example_get_status("cd_after_stop") != QM_EOK) {
        return;
    }

    if (qm_timer_mgr_example_check("cd_restart", qm_timer_mgr_restart(s_timer)) != QM_EOK) {
        return;
    }

    /* Let the countdown run to zero so FINISHED fires. */
    qm_msleep((duration + 1) * 1000);
    if (qm_timer_mgr_example_get_status("cd_after_finish") != QM_EOK) {
        return;
    }
}

/* Stopwatch: start -> tick -> pause -> resume -> stop (never finishes). */
static void qm_timer_mgr_example_stopwatch(void)
{
    if (qm_timer_mgr_example_check("sw_start",
            qm_timer_mgr_start(s_timer, QM_TIMER_MGR_MODE_STOPWATCH, 0)) != QM_EOK) {
        return;
    }

    qm_msleep(CONFIG_QM_TIMER_MGR_EXAMPLE_STOPWATCH_SEC * 1000 + 200);
    if (qm_timer_mgr_example_get_status("sw_after_ticks") != QM_EOK) {
        return;
    }

    if (qm_timer_mgr_example_check("sw_pause", qm_timer_mgr_pause(s_timer)) != QM_EOK) {
        return;
    }
    if (qm_timer_mgr_example_get_status("sw_after_pause") != QM_EOK) {
        return;
    }

    if (qm_timer_mgr_example_check("sw_resume", qm_timer_mgr_resume(s_timer)) != QM_EOK) {
        return;
    }

    qm_msleep(1000);
    if (qm_timer_mgr_example_check("sw_stop", qm_timer_mgr_stop(s_timer)) != QM_EOK) {
        return;
    }
    if (qm_timer_mgr_example_get_status("sw_after_stop") != QM_EOK) {
        return;
    }
}

void qm_timer_mgr_example_start(void)
{
    qm_timer_mgr_config_t config = {
        .mode = QM_TIMER_MGR_MODE_COUNTDOWN,
        .duration_sec = (uint32_t)CONFIG_QM_TIMER_MGR_EXAMPLE_COUNTDOWN_SEC,
        .tick_ms = CONFIG_QM_TIMER_MGR_EXAMPLE_TICK_MS,
        .event_cb = qm_timer_mgr_example_event,
        .user_ctx = NULL,
    };

    qm_timer_mgr_example_stop();

    s_timer = qm_timer_mgr_create(&config);
    if (!s_timer) {
        QM_LOGE(LOG_TAG, "create failed");
        return;
    }

    qm_timer_mgr_example_countdown();
    qm_timer_mgr_example_stopwatch();

    qm_timer_mgr_example_stop();
}

void qm_application_start(void)
{
    qm_timer_mgr_example_start();
}
