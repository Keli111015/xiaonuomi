#include "pomodoro.h"

#include "morphcube_study_runtime.h"
#include "cJSON.h"
#include "qm.h"
#include "qm_iot_api.h"
#include "qm_iot_mqtt.h"
#include "qm_pomodoro.h"

#include <string.h>

#define LOG_TAG "pomodoro"

static qm_pomodoro_t g_pomodoro;

static void on_pomodoro_event(qm_pomodoro_t t, const qm_pomodoro_event_data_t *e)
{
    (void)t;
    if (e == NULL) {
        return;
    }
    int ret = morphcube_study_runtime_on_pomodoro(e);
    if (ret != QM_EOK) {
        QM_LOGW(LOG_TAG, "study event queue failed ret=%d", ret);
    }

    switch (e->event) {
    case QM_POMODORO_EVENT_TICK:
        /* Remaining time was updated. */
        QM_LOGI(LOG_TAG, "remain=%us", (unsigned)e->status.remaining_sec);
        break;
    case QM_POMODORO_EVENT_STATE_CHANGED:
        /* Phase changed. */
        break;
    case QM_POMODORO_EVENT_PHASE_DONE:
        /* Current focus or break phase completed. */
        break;
    case QM_POMODORO_EVENT_PAUSED:
        /* Timer was paused. */
        break;
    case QM_POMODORO_EVENT_RESUMED:
        /* Timer resumed from pause. */
        break;
    case QM_POMODORO_EVENT_STOPPED:
        /* Timer was stopped and reset to idle. */
        break;
    case QM_POMODORO_EVENT_FINISHED:
        /* Target focus count completed. */
        break;
    default:
        /* Unknown event. */
        break;
    }
}

static int pomodoro_start(void)
{
    return qm_pomodoro_start(g_pomodoro);
}

static int pomodoro_stop(void)
{
    return qm_pomodoro_stop(g_pomodoro);
}

static int pomodoro_pause(void)
{
    return qm_pomodoro_pause(g_pomodoro);
}

static int pomodoro_resume(void)
{
    return qm_pomodoro_resume(g_pomodoro);
}

static inline int pomodoro_skip(void)
{
    return qm_pomodoro_skip(g_pomodoro);
}

static inline int pomodoro_get_status(qm_pomodoro_status_t *status)
{
    return qm_pomodoro_get_status(g_pomodoro, status);
}

void pomodoro_init(void)
{
    qm_pomodoro_config_t cfg = {0};

    cfg.focus_sec           = CONFIG_QM_POMODORO_FOCUS_SEC;
    cfg.short_break_sec     = CONFIG_QM_POMODORO_SHORT_BREAK_SEC;
    cfg.long_break_sec      = CONFIG_QM_POMODORO_LONG_BREAK_SEC;
    cfg.long_break_interval = CONFIG_QM_POMODORO_LONG_BREAK_INTERVAL;
    cfg.focus_target        = CONFIG_QM_POMODORO_FOCUS_TARGET;
    cfg.tick_ms             = CONFIG_QM_POMODORO_TICK_MS;
    cfg.event_cb            = on_pomodoro_event;

    g_pomodoro = qm_pomodoro_create(&cfg);
    if (g_pomodoro == NULL) {
        QM_LOGE(LOG_TAG, "create failed");
        return;
    }
    int ret = morphcube_study_runtime_init(g_pomodoro);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "study runtime init failed ret=%d", ret);
        return;
    }
#if CONFIG_APP_POMODORO_AUTO_START
    (void)qm_pomodoro_start(g_pomodoro);
#endif
}

static void handle_pomodoro_cloud_down(void *handle,
                                       const qm_iot_mqtt_recv_t *packet,
                                       void *userdata)
{
    cJSON *root;
    cJSON *sub;
    const char *subMethod;
    int ret;

    (void)handle;
    (void)userdata;
    if (packet == NULL || packet->type != QM_IOT_MQTTRECV_PUB ||
        packet->data.pub.payload == NULL) {
        return;
    }

    root = cJSON_ParseWithLength((const char *)packet->data.pub.payload,
                                 packet->data.pub.payload_len);
    if (root == NULL) {
        return;
    }

    sub = cJSON_GetObjectItem(root, "subMethod");
    if (!cJSON_IsString(sub) || sub->valuestring == NULL) {
        sub = cJSON_GetObjectItem(root, "submethod");
    }
    subMethod = cJSON_IsString(sub) ? sub->valuestring : "";

    if (g_pomodoro == NULL) {
        QM_LOGW(LOG_TAG, "not created, ignore %s", subMethod);
        cJSON_Delete(root);
        return;
    }

    if (strcmp(subMethod, "start") == 0) {
        ret = pomodoro_start();
    } else if (strcmp(subMethod, "stop") == 0) {
        ret = pomodoro_stop();
    } else if (strcmp(subMethod, "pause") == 0) {
        ret = pomodoro_pause();
    } else if (strcmp(subMethod, "resume") == 0) {
        ret = pomodoro_resume();
    } else if (strcmp(subMethod, "distracted") == 0) {
        ret = morphcube_study_runtime_confirm_distraction();
    } else {
        QM_LOGW(LOG_TAG, "unknown subMethod=%s", subMethod);
        cJSON_Delete(root);
        return;
    }

    if (ret != QM_EOK) {
        QM_LOGW(LOG_TAG, "%s failed ret=%d", subMethod, ret);
    } else {
        QM_LOGI(LOG_TAG, "%s ok", subMethod);
    }
    cJSON_Delete(root);
}

int pomodoro_on_connected(void *iot_handle)
{
    int ret;

    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }
    ret = qm_iot_down_register(iot_handle, "pomodoro",
                               handle_pomodoro_cloud_down, NULL);
    if (ret != QM_EOK) {
        QM_LOGW(LOG_TAG, "down reg failed ret=%d", ret);
        return ret;
    }
    QM_LOGI(LOG_TAG, "down registered: pomodoro");
    return QM_EOK;
}
