#include "qm_config.h"
#include "qm_alarm_example.h"

#include "qm_alarm.h"

#include "qm_errno.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_time.h"

#define LOG_TAG "qm_alarm_example"

#ifndef CONFIG_QM_ALARM_EXAMPLE_NAME
#define CONFIG_QM_ALARM_EXAMPLE_NAME "qm_alarm example"
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_HOUR
#define CONFIG_QM_ALARM_EXAMPLE_HOUR 7
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_MINUTE
#define CONFIG_QM_ALARM_EXAMPLE_MINUTE 30
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_REPEAT_MASK
#define CONFIG_QM_ALARM_EXAMPLE_REPEAT_MASK QM_ALARM_REPEAT_WORKDAY
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_ENABLE_CLOUD_REPORT
#define CONFIG_QM_ALARM_EXAMPLE_ENABLE_CLOUD_REPORT 1
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_ENABLE_LOCAL_ALARM
#define CONFIG_QM_ALARM_EXAMPLE_ENABLE_LOCAL_ALARM 1
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_USE_FIXED_TIME
#define CONFIG_QM_ALARM_EXAMPLE_USE_FIXED_TIME 1
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_OFFSET_MIN
#define CONFIG_QM_ALARM_EXAMPLE_OFFSET_MIN 2
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_ENABLE_SNOOZE
#define CONFIG_QM_ALARM_EXAMPLE_ENABLE_SNOOZE 1
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_SNOOZE_INTERVAL_MIN
#define CONFIG_QM_ALARM_EXAMPLE_SNOOZE_INTERVAL_MIN 5
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_SNOOZE_MAX_COUNT
#define CONFIG_QM_ALARM_EXAMPLE_SNOOZE_MAX_COUNT 3
#endif

#ifndef CONFIG_QM_ALARM_EXAMPLE_EXTENSIONS
#define CONFIG_QM_ALARM_EXAMPLE_EXTENSIONS "{}"
#endif

static qm_alarm_t s_alarm;

static int check_ret(const char *name, int ret)
{
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "%s failed ret=%d", name, ret);
        return ret;
    }

    QM_LOGI(LOG_TAG, "%s ok", name);
    return QM_EOK;
}

static const char *alarm_notify_str(qm_alarm_notify_t notify)
{
    switch (notify) {
        case QM_ALARM_NOTIFY_RING:
            return "ring";
        case QM_ALARM_NOTIFY_DISMISS:
            return "dismiss";
        case QM_ALARM_NOTIFY_SNOOZE:
            return "snooze";
        case QM_ALARM_NOTIFY_CHANGED:
            return "changed";
        default:
            return "unknown";
    }
}

static void alarm_notify_cb(qm_alarm_t alarm,
                            qm_alarm_notify_t notify,
                            const qm_alarm_item_t *item,
                            void *user_ctx)
{
    QM_UNUSED_ARG(alarm);
    QM_UNUSED_ARG(user_ctx);

    if (item == NULL) {
        return;
    }

    QM_LOGI(LOG_TAG,
            "alarm notify=%s source=%s alarm_id=%u name=%s time=%02u:%02u enabled=%u repeat=0x%02x tone={type=%s local_id=%u online_id=%s}",
            alarm_notify_str(notify),
            item->source,
            (unsigned int)item->alarm_id,
            item->name,
            (unsigned int)item->hour,
            (unsigned int)item->minute,
            (unsigned int)item->enabled,
            (unsigned int)item->repeat_mask,
            (item->tone.type == QM_ALARM_TONE_TYPE_LOCAL) ? "local" : "online",
            (unsigned int)item->tone.local_id,
            item->tone.online_id);
}

static void fill_example_alarm(qm_alarm_item_t *item)
{
    qm_time_t now;
    struct qm_tm local_tm;

    if (item == NULL) {
        return;
    }

    qm_memset(item, 0, sizeof(*item));
    item->alarm_id = QM_ALARM_LOCAL_ID_AUTO;
    qm_snprintf(item->source, sizeof(item->source), "%s", QM_ALARM_SOURCE_DEVICE);
    qm_snprintf(item->name, sizeof(item->name), "%s", CONFIG_QM_ALARM_EXAMPLE_NAME);
    item->enabled = QM_TRUE;
    item->repeat_mask = CONFIG_QM_ALARM_EXAMPLE_REPEAT_MASK;
    item->tone.type = QM_ALARM_TONE_TYPE_LOCAL;
    item->tone.local_id = 1U;
    item->vibrate = QM_TRUE;
    item->snooze.enabled = CONFIG_QM_ALARM_EXAMPLE_ENABLE_SNOOZE ? QM_TRUE : QM_FALSE;
    item->snooze.interval_min = CONFIG_QM_ALARM_EXAMPLE_SNOOZE_INTERVAL_MIN;
    item->snooze.max_count = CONFIG_QM_ALARM_EXAMPLE_SNOOZE_MAX_COUNT;
    item->extensions = CONFIG_QM_ALARM_EXAMPLE_EXTENSIONS;

#if CONFIG_QM_ALARM_EXAMPLE_USE_FIXED_TIME
    item->hour = CONFIG_QM_ALARM_EXAMPLE_HOUR;
    item->minute = CONFIG_QM_ALARM_EXAMPLE_MINUTE;
#else
    now = qm_time(NULL) + (qm_time_t)CONFIG_QM_ALARM_EXAMPLE_OFFSET_MIN * 60ULL;
    qm_localtime_r(&now, &local_tm);
    item->hour = (uint8_t)local_tm.tm_hour;
    item->minute = (uint8_t)local_tm.tm_min;
#endif
}

static void log_next_alarm(void)
{
    qm_alarm_item_t next_alarm;
    qm_time_t trigger_time;
    int ret;

    if (s_alarm == NULL) {
        return;
    }

    qm_memset(&next_alarm, 0, sizeof(next_alarm));
    ret = qm_alarm_get_upcoming(s_alarm, &next_alarm, &trigger_time);
    if (ret != QM_EOK) {
        QM_LOGW(LOG_TAG, "get next alarm failed ret=%d", ret);
        return;
    }

    QM_LOGI(LOG_TAG,
            "next alarm source=%s alarm_id=%u name=%s time=%02u:%02u trigger_time=%llu",
            next_alarm.source,
            (unsigned int)next_alarm.alarm_id,
            next_alarm.name,
            (unsigned int)next_alarm.hour,
            (unsigned int)next_alarm.minute,
            (unsigned long long)trigger_time);
}

void qm_alarm_example_stop(void)
{
    if (s_alarm == NULL) {
        return;
    }

    qm_alarm_destroy(s_alarm);
    s_alarm = NULL;
    QM_LOGI(LOG_TAG, "alarm example stopped");
}

int qm_alarm_example_start(void *iot_handle)
{
    qm_alarm_config_t config;
    qm_alarm_item_t item;

    if (iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "iot_handle is required");
        return -QM_EINVAL;
    }

    qm_alarm_example_stop();

    qm_memset(&config, 0, sizeof(config));
    config.iot_handle = iot_handle;
    config.notify_cb = alarm_notify_cb;

    s_alarm = qm_alarm_create(&config);
    if (s_alarm == NULL) {
        QM_LOGE(LOG_TAG, "qm_alarm_create failed");
        return -QM_ENOMEM;
    }

    if (check_ret("qm_alarm_start", qm_alarm_start(s_alarm)) != QM_EOK) {
        qm_alarm_example_stop();
        return -QM_EIO;
    }

#if CONFIG_QM_ALARM_EXAMPLE_ENABLE_LOCAL_ALARM
    fill_example_alarm(&item);
    if (check_ret("qm_alarm_create_item", qm_alarm_create_item(s_alarm, &item)) != QM_EOK) {
        qm_alarm_example_stop();
        return -QM_EIO;
    }

    log_next_alarm();
#else
    QM_LOGI(LOG_TAG, "skip local example alarm create");
#endif

    return QM_EOK;
}
