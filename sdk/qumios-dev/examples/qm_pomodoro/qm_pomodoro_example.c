#include "qm_config.h"
#include "qm_pomodoro.h"

#include "qm_kernel.h"
#include "qm_log.h"

#define LOG_TAG "qm_pomodoro_example"

#ifndef CONFIG_QM_POMODORO_EXAMPLE_FOCUS_SEC
#define CONFIG_QM_POMODORO_EXAMPLE_FOCUS_SEC 4
#endif

#ifndef CONFIG_QM_POMODORO_EXAMPLE_SHORT_BREAK_SEC
#define CONFIG_QM_POMODORO_EXAMPLE_SHORT_BREAK_SEC 2
#endif

#ifndef CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_SEC
#define CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_SEC 3
#endif

#ifndef CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_INTERVAL
#define CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_INTERVAL 2
#endif

#ifndef CONFIG_QM_POMODORO_EXAMPLE_FOCUS_TARGET
#define CONFIG_QM_POMODORO_EXAMPLE_FOCUS_TARGET 3
#endif

#ifndef CONFIG_QM_POMODORO_EXAMPLE_TICK_MS
#define CONFIG_QM_POMODORO_EXAMPLE_TICK_MS 700
#endif

static qm_pomodoro_t s_pomodoro;

static const char *qm_pomodoro_example_state_name(qm_pomodoro_state_t state)
{
    switch (state) {
    case QM_POMODORO_STATE_IDLE:
        return "idle";
    case QM_POMODORO_STATE_FOCUS:
        return "focus";
    case QM_POMODORO_STATE_SHORT_BREAK:
        return "short_break";
    case QM_POMODORO_STATE_LONG_BREAK:
        return "long_break";
    case QM_POMODORO_STATE_FINISHED:
        return "finished";
    default:
        return "unknown";
    }
}

static const char *qm_pomodoro_example_event_name(qm_pomodoro_event_t event)
{
    switch (event) {
    case QM_POMODORO_EVENT_STATE_CHANGED:
        return "state_changed";
    case QM_POMODORO_EVENT_TICK:
        return "tick";
    case QM_POMODORO_EVENT_PHASE_DONE:
        return "phase_done";
    case QM_POMODORO_EVENT_PAUSED:
        return "paused";
    case QM_POMODORO_EVENT_RESUMED:
        return "resumed";
    case QM_POMODORO_EVENT_STOPPED:
        return "stopped";
    case QM_POMODORO_EVENT_FINISHED:
        return "finished";
    default:
        return "unknown";
    }
}

static void qm_pomodoro_example_log_status(const char *tag,
                                           const qm_pomodoro_status_t *status)
{
    if (!status) {
        return;
    }

    QM_LOGI(LOG_TAG,
            "%s state=%s paused=%d remaining=%u elapsed=%u duration=%u completed=%u target=%u",
            tag,
            qm_pomodoro_example_state_name(status->state),
            status->paused,
            (unsigned int)status->remaining_sec,
            (unsigned int)status->elapsed_sec,
            (unsigned int)status->duration_sec,
            (unsigned int)status->completed_focus,
            (unsigned int)status->focus_target);
}

static void qm_pomodoro_example_event(qm_pomodoro_t pomodoro,
                                      const qm_pomodoro_event_data_t *event)
{
    (void)pomodoro;

    if (!event) {
        return;
    }

    QM_LOGI(LOG_TAG, "event=%s previous=%s",
            qm_pomodoro_example_event_name(event->event),
            qm_pomodoro_example_state_name(event->previous_state));
    qm_pomodoro_example_log_status("event_status", &event->status);
}

static int qm_pomodoro_example_check(const char *name, int ret)
{
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "%s failed ret=%d", name, ret);
        return ret;
    }

    QM_LOGI(LOG_TAG, "%s ok", name);
    return QM_EOK;
}

static int qm_pomodoro_example_get_status(const char *tag)
{
    qm_pomodoro_status_t status;
    int ret = qm_pomodoro_get_status(s_pomodoro, &status);

    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "get_status failed ret=%d", ret);
        return ret;
    }

    qm_pomodoro_example_log_status(tag, &status);
    return QM_EOK;
}

void qm_pomodoro_example_stop(void)
{
    if (!s_pomodoro) {
        return;
    }

    (void)qm_pomodoro_stop(s_pomodoro);
    qm_pomodoro_destroy(s_pomodoro);
    s_pomodoro = NULL;
}

void qm_pomodoro_example_start(void)
{
    qm_pomodoro_config_t config = {
        .focus_sec = CONFIG_QM_POMODORO_EXAMPLE_FOCUS_SEC,
        .short_break_sec = CONFIG_QM_POMODORO_EXAMPLE_SHORT_BREAK_SEC,
        .long_break_sec = CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_SEC,
        .long_break_interval = CONFIG_QM_POMODORO_EXAMPLE_LONG_BREAK_INTERVAL,
        .focus_target = CONFIG_QM_POMODORO_EXAMPLE_FOCUS_TARGET,
        .tick_ms = CONFIG_QM_POMODORO_EXAMPLE_TICK_MS,
        .event_cb = qm_pomodoro_example_event,
        .user_ctx = NULL,
    };

    qm_pomodoro_example_stop();

    s_pomodoro = qm_pomodoro_create(&config);
    if (!s_pomodoro) {
        QM_LOGE(LOG_TAG, "create failed");
        return;
    }

    if (qm_pomodoro_example_check("start", qm_pomodoro_start(s_pomodoro)) != QM_EOK) {
        goto exit;
    }

    qm_msleep(2100);
    if (qm_pomodoro_example_get_status("after_ticks") != QM_EOK) {
        goto exit;
    }

    if (qm_pomodoro_example_check("pause", qm_pomodoro_pause(s_pomodoro)) != QM_EOK) {
        goto exit;
    }
    if (qm_pomodoro_example_get_status("after_pause") != QM_EOK) {
        goto exit;
    }

    qm_msleep(1000);
    if (qm_pomodoro_example_check("resume", qm_pomodoro_resume(s_pomodoro)) != QM_EOK) {
        goto exit;
    }

    qm_msleep(1000);
    if (qm_pomodoro_example_check("skip_focus_1", qm_pomodoro_skip(s_pomodoro)) != QM_EOK) {
        goto exit;
    }
    if (qm_pomodoro_example_get_status("after_skip_focus_1") != QM_EOK) {
        goto exit;
    }

    if (qm_pomodoro_example_check("skip_short_break", qm_pomodoro_skip(s_pomodoro)) != QM_EOK) {
        goto exit;
    }
    if (qm_pomodoro_example_get_status("after_skip_short_break") != QM_EOK) {
        goto exit;
    }

    if (qm_pomodoro_example_check("skip_focus_2", qm_pomodoro_skip(s_pomodoro)) != QM_EOK) {
        goto exit;
    }
    if (qm_pomodoro_example_get_status("after_skip_focus_2") != QM_EOK) {
        goto exit;
    }

    if (qm_pomodoro_example_check("skip_long_break", qm_pomodoro_skip(s_pomodoro)) != QM_EOK) {
        goto exit;
    }
    if (qm_pomodoro_example_get_status("after_skip_long_break") != QM_EOK) {
        goto exit;
    }

    if (qm_pomodoro_example_check("skip_focus_3", qm_pomodoro_skip(s_pomodoro)) != QM_EOK) {
        goto exit;
    }
    (void)qm_pomodoro_example_get_status("after_finish");

    (void)qm_pomodoro_example_check("stop", qm_pomodoro_stop(s_pomodoro));

exit:
    qm_pomodoro_destroy(s_pomodoro);
    s_pomodoro = NULL;
}

void qm_application_start(void)
{
    qm_pomodoro_example_start();
}
