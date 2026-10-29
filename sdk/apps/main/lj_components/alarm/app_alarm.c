#include "app_alarm.h"

#include <string.h>

#include "agent_app.h"     
#include "boot_sound.h"     /* 示例提示音：复用已内嵌的开机音效 */
#include "qm.h"
#include "qm_log.h"
#include "qm_work.h"

#define LOG_TAG "app_alarm"

static qm_alarm_t s_alarm;
static app_alarm_behavior_cb_t s_cb;
static void *s_cb_user;
static qm_work_t s_evt_work;
static app_alarm_event_t s_evt;

static void alarm_evt_work(void *arg);

#if CONFIG_QM_CLI_SUPPORT
static int alarm_cli_register(void);
static void alarm_cli_unregister(void);
#endif

/* 组件通知回调：只做拷贝 + 投递，立即返回 */
static void alarm_notify_cb(qm_alarm_t alarm, qm_alarm_notify_t notify,
                            const qm_alarm_item_t *item, void *user_ctx)
{
    (void)alarm;
    (void)user_ctx;

    memset(&s_evt, 0, sizeof(s_evt));
    s_evt.notify = notify;
    if (item != NULL) {
        s_evt.alarm_id = item->alarm_id;
        memcpy(s_evt.source, item->source, sizeof(s_evt.source));
        s_evt.hour = item->hour;
        s_evt.minute = item->minute;
        s_evt.repeat_mask = item->repeat_mask;
        s_evt.enabled = item->enabled;
        s_evt.vibrate = item->vibrate;
        s_evt.tone = item->tone;
    }
    (void)qm_post_delayed_action(&s_evt_work, alarm_evt_work, NULL, 0);
}

static void alarm_evt_work(void *arg)
{
    (void)arg;
    if (s_cb != NULL) {
        s_cb(&s_evt, s_cb_user);
    }
}

/* 示例行为实现：RING 播提示音，其余只打日志（产品化时改这里） */
static void app_alarm_behavior(const app_alarm_event_t *evt, void *user)
{
    (void)user;

    switch (evt->notify)
    {
    case QM_ALARM_NOTIFY_RING: {
        int ret;

        QM_LOGI(LOG_TAG, "RING enter id=%u %02u:%02u repeat=0x%02x agent_active=%d",
                (unsigned)evt->alarm_id, (unsigned)evt->hour, (unsigned)evt->minute,
                (unsigned)evt->repeat_mask, agent_app_is_active());
        ret = boot_sound_start();
        QM_LOGI(LOG_TAG, "RING sound_ret=%d", ret);
        break;
    }
    case QM_ALARM_NOTIFY_DISMISS:
        QM_LOGI(LOG_TAG, "DISMISS id=%u", evt->alarm_id);
        break;
    case QM_ALARM_NOTIFY_SNOOZE:
        QM_LOGI(LOG_TAG, "SNOOZE id=%u", evt->alarm_id);
        break;
    case QM_ALARM_NOTIFY_CHANGED:
        QM_LOGI(LOG_TAG, "CHANGED id=%u enabled=%d", evt->alarm_id, (int)evt->enabled);
        break;
    default:
        break;
    }
}

qm_err_t app_alarm_init(void *iot_handle)
{
    qm_alarm_config_t cfg;

    if (s_alarm != NULL) {
        return QM_EOK;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.iot_handle = iot_handle;
    cfg.notify_cb = alarm_notify_cb;
    cfg.user_ctx = NULL;

    s_alarm = qm_alarm_create(&cfg);
    if (s_alarm == NULL) {
        QM_LOGE(LOG_TAG, "create failed");
        return -QM_ENOMEM;
    }
    if (qm_alarm_start(s_alarm) != QM_EOK) {
        QM_LOGE(LOG_TAG, "start failed");
        qm_alarm_destroy(s_alarm);
        s_alarm = NULL;
        return -QM_ERROR;
    }

    s_cb = app_alarm_behavior;
    s_cb_user = NULL;
    QM_LOGI(LOG_TAG, "ready, cloud=%s", (iot_handle != NULL) ? "on" : "off");
#if CONFIG_QM_CLI_SUPPORT
    if (alarm_cli_register() != 0) {
        QM_LOGE(LOG_TAG, "cli register failed");
    }
#endif
    return QM_EOK;
}

void app_alarm_deinit(void)
{
    if (s_alarm == NULL) {
        return;
    }
    (void)qm_alarm_stop(s_alarm);
    qm_alarm_destroy(s_alarm);
    s_alarm = NULL;
    s_cb = NULL;
    s_cb_user = NULL;
#if CONFIG_QM_CLI_SUPPORT
    alarm_cli_unregister();
#endif
    QM_LOGI(LOG_TAG, "stopped");
}

qm_bool app_alarm_is_ready(void)
{
    return (s_alarm != NULL) ? QM_TRUE : QM_FALSE;
}

void app_alarm_item_defaults(qm_alarm_item_t *item)
{
    if (item == NULL) {
        return;
    }
    memset(item, 0, sizeof(*item));
    memcpy(item->source, QM_ALARM_SOURCE_DEVICE, sizeof(QM_ALARM_SOURCE_DEVICE));
    item->alarm_id = QM_ALARM_LOCAL_ID_AUTO;
    item->enabled = QM_TRUE;
    item->tone.type = QM_ALARM_TONE_TYPE_LOCAL;
    item->snooze.enabled = QM_FALSE;
    item->snooze.interval_min = CONFIG_QM_ALARM_SNOOZE_INTERVAL_MIN;
    item->extensions = NULL;
}

qm_err_t app_alarm_add(const qm_alarm_item_t *item)
{
    qm_alarm_item_t copy;

    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    if (item == NULL) {
        return -QM_EINVAL;
    }
    copy = *item;
    if (copy.source[0] == '\0') {
        memcpy(copy.source, QM_ALARM_SOURCE_DEVICE, sizeof(copy.source));
    }
    return qm_alarm_create_item(s_alarm, &copy);
}

qm_err_t app_alarm_modify(const qm_alarm_item_t *item)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    if (item == NULL) {
        return -QM_EINVAL;
    }
    return qm_alarm_modify_item(s_alarm, item);
}

qm_err_t app_alarm_delete(const char *source, uint8_t alarm_id)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_delete_item(s_alarm, source, alarm_id);
}

qm_err_t app_alarm_enable(const char *source, uint8_t alarm_id, qm_bool enabled)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_set_enabled(s_alarm, source, alarm_id, enabled);
}

qm_err_t app_alarm_close_ringing(void)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_dismiss(s_alarm);
}

qm_err_t app_alarm_snooze(void)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_snooze(s_alarm);
}

qm_err_t app_alarm_cancel_snooze(void)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_cancel_snooze(s_alarm);
}

qm_err_t app_alarm_get_next(qm_alarm_item_t *out, qm_time_t *trigger_time)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_get_upcoming(s_alarm, out, trigger_time);
}

qm_err_t app_alarm_get_list(qm_alarm_item_t *out, uint16_t capacity, uint16_t *count)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_list(s_alarm, out, capacity, count);
}

qm_err_t app_alarm_get_state(qm_alarm_state_t *state, uint8_t *alarm_id)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_get_state(s_alarm, state, alarm_id);
}

qm_err_t app_alarm_time_changed(void)
{
    int ret;

    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    /* 这份 qm_alarm 没有单独的对时通知。已经 start 过再调 start 不会重排，
     * 所以先停再开，让 schedule_next 按新时钟算下一次。 */
    ret = qm_alarm_stop(s_alarm);
    if (ret != QM_EOK) {
        return ret;
    }
    return qm_alarm_start(s_alarm);
}

qm_err_t app_alarm_report(void)
{
    if (s_alarm == NULL) {
        return -QM_EINIT;
    }
    return qm_alarm_report(s_alarm);
}

#if CONFIG_QM_CLI_SUPPORT
/* 调试命令行：alarm list|next|state|add|del|on|off|close|snooze|cancel|clear|test */

#include "qm_cli.h"
#include "qm_time.h"
#include "boot_sound.h"

#include <stdio.h>
#include <stdlib.h>

#define ALARM_CLI_LIST_MAX 16

static void cli_usage(void)
{
    qm_cli_printf("usage: alarm list|next|state|add <+N|HH:MM>|del <id>|on <id>|off <id>|close|snooze|cancel|clear|test\r\n");
}

static void cli_print_item(const qm_alarm_item_t *it, unsigned idx)
{
    qm_cli_printf("[%u] %s id=%u %02u:%02u %s repeat=0x%02x vib=%u\r\n",
                  idx, it->source, (unsigned)it->alarm_id,
                  (unsigned)it->hour, (unsigned)it->minute,
                  (it->enabled == QM_TRUE) ? "on " : "off",
                  (unsigned)it->repeat_mask,
                  (it->vibrate == QM_TRUE) ? 1U : 0U);
}

static void cli_cmd_list(char *wbuf, int wlen, int argc, char **argv)
{
    qm_alarm_item_t items[ALARM_CLI_LIST_MAX];
    uint16_t count = 0U;
    uint16_t i;

    (void)wbuf;
    (void)wlen;
    (void)argc;
    (void)argv;

    if (app_alarm_get_list(items, ALARM_CLI_LIST_MAX, &count) != QM_EOK) {
        qm_cli_printf("alarm: list failed\r\n");
        return;
    }
    qm_cli_printf("alarm count=%u\r\n", (unsigned)count);
    for (i = 0U; i < count; i++) {
        cli_print_item(&items[i], (unsigned)i);
    }
}

static void cli_cmd_next(char *wbuf, int wlen, int argc, char **argv)
{
    qm_alarm_item_t it;
    qm_time_t when = 0U;
    struct qm_tm tm;
    int ret;

    (void)wbuf;
    (void)wlen;
    (void)argc;
    (void)argv;

    ret = app_alarm_get_next(&it, &when);
    if (ret != QM_EOK) {
        qm_cli_printf("alarm: next none (ret=%d)\r\n", ret);
        return;
    }
    qm_localtime_r(&when, &tm);
    qm_cli_printf("alarm: next id=%u %02d:%02d (epoch=%llu)\r\n",
                  (unsigned)it.alarm_id, tm.tm_hour, tm.tm_min,
                  (unsigned long long)when);
}

static void cli_cmd_state(char *wbuf, int wlen, int argc, char **argv)
{
    qm_alarm_state_t st = QM_ALARM_STATE_IDLE;
    uint8_t alarm_id = 0U;
    const char *name = "unknown";
    int ret;

    (void)wbuf;
    (void)wlen;
    (void)argc;
    (void)argv;

    ret = app_alarm_get_state(&st, &alarm_id);
    if (ret != QM_EOK) {
        qm_cli_printf("alarm: state failed (ret=%d)\r\n", ret);
        return;
    }
    switch (st)
    {
    case QM_ALARM_STATE_IDLE:
        name = "idle";
        break;
    case QM_ALARM_STATE_RINGING:
        name = "ringing";
        break;
    case QM_ALARM_STATE_SNOOZING:
        name = "snoozing";
        break;
    default:
        break;
    }
    qm_cli_printf("alarm: state=%s alarm_id=%u\r\n", name, (unsigned)alarm_id);
}

/* add +N（N 分钟后）或 add HH:MM（当天该时刻，一次性） */
static void cli_cmd_add(char *wbuf, int wlen, int argc, char **argv)
{
    qm_alarm_item_t it;
    const char *spec;
    int ret;

    (void)wbuf;
    (void)wlen;

    if (argc < 3) {
        cli_usage();
        return;
    }
    spec = argv[2];
    app_alarm_item_defaults(&it);

    if (spec[0] == '+') {
        int mins = atoi(&spec[1]);
        qm_time_t now = qm_time(NULL);
        struct qm_tm tm;
        int total;

        if (mins <= 0) {
            cli_usage();
            return;
        }
        qm_localtime_r(&now, &tm);
        total = tm.tm_hour * 60 + tm.tm_min + mins;
        it.hour = (uint8_t)((total / 60) % 24);
        it.minute = (uint8_t)(total % 60);
    } else {
        int hour = 0;
        int minute = 0;

        if (sscanf(spec, "%d:%d", &hour, &minute) != 2 ||
            hour < 0 || hour > 23 || minute < 0 || minute > 59) {
            cli_usage();
            return;
        }
        it.hour = (uint8_t)hour;
        it.minute = (uint8_t)minute;
    }

    ret = app_alarm_add(&it);
    qm_cli_printf("alarm: add %02u:%02u ret=%d\r\n",
                  (unsigned)it.hour, (unsigned)it.minute, ret);
}

static void cli_cmd_del(char *wbuf, int wlen, int argc, char **argv)
{
    int id;
    int ret;

    (void)wbuf;
    (void)wlen;

    if (argc < 3) {
        cli_usage();
        return;
    }
    id = atoi(argv[2]);
    ret = app_alarm_delete(QM_ALARM_SOURCE_DEVICE, (uint8_t)id);
    qm_cli_printf("alarm: del id=%d ret=%d\r\n", id, ret);
}

static void cli_set_enabled(int argc, char **argv, qm_bool enabled)
{
    int id;
    int ret;

    if (argc < 3) {
        cli_usage();
        return;
    }
    id = atoi(argv[2]);
    ret = app_alarm_enable(QM_ALARM_SOURCE_DEVICE, (uint8_t)id, enabled);
    qm_cli_printf("alarm: %s id=%d ret=%d\r\n", (enabled == QM_TRUE) ? "on" : "off", id, ret);
}

static void cli_cmd_on(char *wbuf, int wlen, int argc, char **argv)
{
    (void)wbuf;
    (void)wlen;
    cli_set_enabled(argc, argv, QM_TRUE);
}

static void cli_cmd_off(char *wbuf, int wlen, int argc, char **argv)
{
    (void)wbuf;
    (void)wlen;
    cli_set_enabled(argc, argv, QM_FALSE);
}

static void cli_cmd_close(char *wbuf, int wlen, int argc, char **argv)
{
    (void)wbuf;
    (void)wlen;
    (void)argc;
    (void)argv;
    qm_cli_printf("alarm: close ret=%d\r\n", app_alarm_close_ringing());
}

static void cli_cmd_snooze(char *wbuf, int wlen, int argc, char **argv)
{
    (void)wbuf;
    (void)wlen;
    (void)argc;
    (void)argv;
    qm_cli_printf("alarm: snooze ret=%d\r\n", app_alarm_snooze());
}

static void cli_cmd_cancel(char *wbuf, int wlen, int argc, char **argv)
{
    (void)wbuf;
    (void)wlen;
    (void)argc;
    (void)argv;
    qm_cli_printf("alarm: cancel snooze ret=%d\r\n", app_alarm_cancel_snooze());
}

static void cli_cmd_clear(char *wbuf, int wlen, int argc, char **argv)
{
    qm_alarm_item_t items[ALARM_CLI_LIST_MAX];
    uint16_t count = 0U;
    uint16_t i;
    unsigned removed = 0U;

    (void)wbuf;
    (void)wlen;
    (void)argc;
    (void)argv;

    if (app_alarm_get_list(items, ALARM_CLI_LIST_MAX, &count) != QM_EOK) {
        qm_cli_printf("alarm: list failed\r\n");
        return;
    }
    /* 组件没有 clear-all 接口，只能逐条删（source + id 即主键） */
    for (i = 0U; i < count; i++) {
        if (app_alarm_delete(items[i].source, items[i].alarm_id) == QM_EOK) {
            removed++;
        }
    }
    qm_cli_printf("alarm: cleared %u/%u\r\n", removed, (unsigned)count);
}

static void cli_cmd_test(char *wbuf, int wlen, int argc, char **argv)
{
    int ret;

    (void)wbuf;
    (void)wlen;
    (void)argc;
    (void)argv;

    ret = boot_sound_start();
    qm_cli_printf("alarm: test sound ret=%d (see boot_snd logs)\r\n", ret);
}

static void cli_cmd_alarm(char *wbuf, int wlen, int argc, char **argv)
{
    if (argc < 2) {
        cli_usage();
        return;
    }

    if (strcmp(argv[1], "list") == 0) {
        cli_cmd_list(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "next") == 0) {
        cli_cmd_next(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "state") == 0) {
        cli_cmd_state(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "add") == 0) {
        cli_cmd_add(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "del") == 0) {
        cli_cmd_del(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "on") == 0) {
        cli_cmd_on(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "off") == 0) {
        cli_cmd_off(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "close") == 0) {
        cli_cmd_close(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "snooze") == 0) {
        cli_cmd_snooze(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "cancel") == 0) {
        cli_cmd_cancel(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "test") == 0) {
        cli_cmd_test(wbuf, wlen, argc, argv);
    } else if (strcmp(argv[1], "clear") == 0) {
        cli_cmd_clear(wbuf, wlen, argc, argv);
    } else {
        cli_usage();
    }
}

static const struct qm_cli_command s_alarm_cli_cmd =
{
    .name = "alarm",
    .help = "alarm list|next|state|add <+N|HH:MM>|del <id>|on <id>|off <id>|close|snooze|cancel|clear|test",
    .function = cli_cmd_alarm,
};

static int alarm_cli_register(void)
{
    return qm_cli_register_command(&s_alarm_cli_cmd);
}

static void alarm_cli_unregister(void)
{
    (void)qm_cli_unregister_command(&s_alarm_cli_cmd);
}
#endif /* CONFIG_QM_CLI_SUPPORT */
