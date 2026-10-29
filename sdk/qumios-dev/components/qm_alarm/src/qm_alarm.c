#include "qm_config.h"

#if defined(CONFIG_QM_ALARM_SUPPORT) && CONFIG_QM_ALARM_SUPPORT

#include "qm_alarm.h"
#include "qm_alarm_log.h"
#include "cJSON.h"
#include "qm_iot_api.h"
#include "qm_kernel.h"
#include "qm_kv.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_time.h"
#include "qm_work.h"

#ifndef CONFIG_QM_ALARM_MAX_ALARMS
#define CONFIG_QM_ALARM_MAX_ALARMS 8
#endif

#ifndef CONFIG_QM_ALARM_SNOOZE_INTERVAL_MIN
#define CONFIG_QM_ALARM_SNOOZE_INTERVAL_MIN 5
#endif

#ifndef CONFIG_QM_ALARM_TASK_STACK_SIZE
#define CONFIG_QM_ALARM_TASK_STACK_SIZE 4096
#endif

#ifndef CONFIG_QM_ALARM_TASK_PRIO
#define CONFIG_QM_ALARM_TASK_PRIO 20
#endif

#define QM_ALARM_COMMAND_QUEUE_LEN 24U
#define QM_ALARM_TIMER_RETRY_SHIFT_MAX 3U
#define QM_ALARM_TIMER_RETRY_BASE_MS 20U

struct qm_alarm;
typedef struct {
    struct qm_alarm *ctx;
    qm_mutex_t lock;
    qm_bool lock_ready;
} alarm_timer_token_t;

#define QM_ALARM_TAG "qm_alarm"
#define LOG_TAG QM_ALARM_TAG
#define DISPATCH_METHOD QM_ALARM_METHOD
#define REPLY_METHOD QM_ALARM_REPLY_METHOD
#define SUBMETHOD_CREATE "create"
#define SUBMETHOD_MODIFY "modify"
#define SUBMETHOD_DELETE "delete"
#define SUBMETHOD_ENABLE "enable"
#define SUBMETHOD_UPDATE "update"
#define SUBMETHOD_CLOSE "close"
#define SUBMETHOD_SNOOZE "snooze"
#define SUBMETHOD_CREATE_ACK "createAck"
#define SUBMETHOD_MODIFY_ACK "modifyAck"
#define SUBMETHOD_DELETE_ACK "deleteAck"
#define SUBMETHOD_ENABLE_ACK "enableAck"
#define SUBMETHOD_CLOSE_ACK "closeAck"
#define SUBMETHOD_SNOOZE_ACK "snoozeAck"
#define DEFAULT_ONLINE_TONE_ID "default_01"
#define DEFAULT_LOCAL_TONE_ID 1U
#define MAX_LOCAL_TONE_ID 99U
#define MAX_ALARM_ID 255U
#define TONE_TYPE_LOCAL_STR "local"
#define TONE_TYPE_ONLINE_STR "online"
#define DEFAULT_SNOOZE_INTERVAL_MIN ((uint8_t)CONFIG_QM_ALARM_SNOOZE_INTERVAL_MIN)
/* Current store layout: one compact meta record plus per-slot alarm items. */
#define STORE_META_KV_KEY "qmalarm.meta"
#define STORE_ITEM_KEY_PREFIX "qmalarm.i."
#define STORE_KEY_BUF_LEN 20U
#define STORE_META_MAGIC 0x51414C4DU
#define STORE_META_VERSION 5U
#define STORE_ITEM_MAGIC 0x51414C49U
#define STORE_ITEM_VERSION 3U
#define STORE_CLEANUP_SLOT_COUNT 256U
#define MAX_ALARM_SLOT_COUNT CONFIG_QM_ALARM_MAX_ALARMS
#define MAX_ALARM_BITMAP_BYTES ((MAX_ALARM_SLOT_COUNT + 7U) / 8U)
#define MAX_STORE_EXTENSIONS_LEN 65535U
#define MAX_STORE_ITEM_BLOB_LEN ((uint32_t)sizeof(alarm_store_item_t) + \
                                 MAX_STORE_EXTENSIONS_LEN)
#define INVALID_SLOT_ID 0xFFFFU
#define ACK_EMPTY_ARRAY "[]"

static volatile uint32_t g_qm_alarm_log_flags = QM_ALARM_KCONFIG_LOG_FLAGS;

uint32_t qm_alarm_get_log_flags(void)
{
    return g_qm_alarm_log_flags;
}

int qm_alarm_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)QM_ALARM_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_alarm_log_flags = log_flags;
    return QM_EOK;
}

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t reserved;
} alarm_store_meta_t;

typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t extensions_len;
    uint8_t alarm_id;
    char source[QM_ALARM_SOURCE_MAX_LEN];
    char name[QM_ALARM_NAME_MAX_LEN];
    uint8_t hour;
    uint8_t minute;
    qm_bool enabled;
    uint8_t repeat_mask;
    uint8_t tone_type;
    uint8_t tone_local_id;
    char tone_online_id[QM_ALARM_TONE_ONLINE_ID_MAX_LEN];
    qm_bool vibrate;
    qm_alarm_snooze_t snooze;
} alarm_store_item_t;

struct qm_alarm {
    qm_alarm_config_t config;
    qm_alarm_notify_cb_t notify_cb;
    void *user_ctx;
    qm_alarm_item_t *alarms;
    uint16_t *alarm_slots;
    uint8_t *used_bitmap;
    qm_bool *slot_dirty;
    uint16_t bitmap_bytes;
    uint16_t count;
    uint16_t capacity;
    uint16_t next_local_alarm_id;
    uint32_t next_msg_id;
    qm_work_t *schedule_work;
    alarm_timer_token_t *timer_token;
    qm_bool cloud_ready;
    qm_bool dirty;
    qm_bool storage_ready;
    qm_bool bitmap_dirty;
    qm_bool started;
    qm_bool down_registered;
    qm_bool reply_registered;
    qm_bool iot_event_registered;
    int32_t next_alarm_index;
    qm_time_t next_fire_time;
    int32_t active_alarm_index;
    int32_t snooze_alarm_index;
    qm_time_t snooze_fire_time;
    uint8_t active_snooze_count;
    qm_bool snooze_pending;
    qm_alarm_state_t state;
    uint8_t state_alarm_id;
    qm_bool state_dirty;
    qm_queue_t owner_queue;
    qm_sem_t owner_exit;
    qm_task_t owner_task;
    qm_mutex_t owner_submit_lock;
    qm_bool owner_ready;
    qm_bool owner_submit_lock_ready;
    volatile uint32_t closing;
    volatile uint32_t destroy_claimed;
    volatile uint32_t owner_call_count;
    qm_sem_t owner_calls_zero;
    volatile uint32_t owner_callback_active;
    uint8_t timer_retry_count;
};

typedef struct qm_alarm alarm_ctx_t;

typedef struct {
    uint32_t msg_id;
    char submethod[16];
    char *params;
    uint32_t params_len;
} alarm_cloud_req_t;

typedef enum {
    ALARM_OWNER_START = 0,
    ALARM_OWNER_STOP,
    ALARM_OWNER_DESTROY,
    ALARM_OWNER_TIMER,
    ALARM_OWNER_CREATE,
    ALARM_OWNER_MODIFY,
    ALARM_OWNER_DELETE,
    ALARM_OWNER_ENABLE,
    ALARM_OWNER_SNOOZE,
    ALARM_OWNER_CANCEL_SNOOZE,
    ALARM_OWNER_DISMISS,
    ALARM_OWNER_REPORT,
    ALARM_OWNER_LIST,
    ALARM_OWNER_UPCOMING,
    ALARM_OWNER_STATE,
    ALARM_OWNER_CLOUD_DOWN,
    ALARM_OWNER_IOT_EVENT,
} alarm_owner_cmd_type_t;

typedef struct {
    alarm_owner_cmd_type_t type;
    qm_sem_t *done;
    int *result_out;
    qm_alarm_item_t item;
    char source[QM_ALARM_SOURCE_MAX_LEN];
    uint8_t alarm_id;
    qm_bool enabled;
    qm_alarm_item_t *list_out;
    uint16_t list_capacity;
    uint16_t *list_count;
    qm_time_t *trigger_time;
    qm_alarm_state_t *state_out;
    uint8_t *state_alarm_id_out;
    alarm_cloud_req_t cloud_req;
    qm_iot_event_type_t iot_event_type;
} alarm_owner_cmd_t;

typedef enum {
    ALARM_APPLY_CREATE = 0,
    ALARM_APPLY_MODIFY,
} alarm_apply_mode_t;

typedef enum {
    ALARM_CLOUD_ACTION_CREATE = 0,
    ALARM_CLOUD_ACTION_MODIFY,
    ALARM_CLOUD_ACTION_DELETE,
    ALARM_CLOUD_ACTION_ENABLE,
} alarm_cloud_action_t;

static int schedule_alarm_after_ms(alarm_ctx_t *ctx, uint32_t delay_ms);
static void cancel_scheduled_alarm(alarm_ctx_t *ctx);
static void alarm_timer_fire_internal(alarm_ctx_t *ctx);
static void alarm_timer_cb(void *arg);
static void alarm_deinit(qm_alarm_t alarm);
static int alarm_owner_submit(alarm_ctx_t *ctx,
                              alarm_owner_cmd_t *cmd,
                              qm_bool wait);
static void alarm_owner_task(void *arg);
static int alarm_start_internal(alarm_ctx_t *ctx);
static int alarm_stop_internal(alarm_ctx_t *ctx);
static int handle_update(alarm_ctx_t *ctx);
static int report_or_mark_dirty(alarm_ctx_t *ctx);
static int report_state(alarm_ctx_t *ctx);
static int next_msg_id(alarm_ctx_t *ctx);
static void set_state(alarm_ctx_t *ctx,
                      qm_alarm_state_t state,
                      const qm_alarm_item_t *alarm,
                      qm_bool report_now);

static void alarm_owner_call_release(alarm_ctx_t *ctx)
{
    if (__sync_sub_and_fetch(&ctx->owner_call_count, 1U) == 0U && ctx->closing) {
        (void)qm_sem_signal(&ctx->owner_calls_zero);
    }
}
static void clear_pending_snooze(alarm_ctx_t *ctx, qm_bool reset_count);
static int save_alarms(alarm_ctx_t *ctx);
static int load_alarms(alarm_ctx_t *ctx);
static void clear_loaded_alarms(alarm_ctx_t *ctx);
static void wipe_alarm_store(void);
static int copy_alarm_owned(qm_alarm_item_t *dst, const qm_alarm_item_t *src);
static int32_t find_alarm(alarm_ctx_t *ctx, const char *source, uint8_t alarm_id);
static int validate_alarm(const qm_alarm_item_t *alarm);
static void normalize_alarm(qm_alarm_item_t *alarm);
static int finalize_alarm_change(alarm_ctx_t *ctx, const char *stage);
static int copy_alarm_list(const alarm_ctx_t *ctx,
                           qm_alarm_item_t *out,
                           uint16_t capacity,
                           uint16_t *count);
static int copy_next_alarm(const alarm_ctx_t *ctx,
                           qm_alarm_item_t *out,
                           qm_time_t *trigger_time);
static void sort_alarm_entries(alarm_ctx_t *ctx);
static int select_next_effective_alarm(const alarm_ctx_t *ctx,
                                       qm_time_t now,
                                       int32_t *next_index,
                                       qm_time_t *next_fire_time);
static int assign_local_create_identity(alarm_ctx_t *ctx,
                                        qm_alarm_item_t *alarm);
static uint32_t alarm_extensions_len(const qm_alarm_item_t *alarm);
static void log_alarm_summary(const char *stage,
                              const qm_alarm_item_t *alarm,
                              int32_t index);
static void log_alarm_list_summary(const char *stage, const alarm_ctx_t *ctx);
static void log_json_text(const char *stage, const cJSON *json);
static const char *alarm_time_str(qm_time_t ts, char *buf, uint32_t buf_len);
static qm_time_t local_alarm_time_to_timestamp(const struct qm_tm *local_tm);
static void mark_alarm_slot_dirty(alarm_ctx_t *ctx, uint16_t slot);
static void mark_alarm_index_dirty(alarm_ctx_t *ctx, int32_t alarm_index);
static void notify_alarm(alarm_ctx_t *ctx,
                         qm_alarm_notify_t notify,
                         const qm_alarm_item_t *item);
static uint16_t alarm_bitmap_bytes(uint16_t slot_count);
static qm_bool alarm_bitmap_slot_used(const uint8_t *bitmap,
                                      uint16_t slot_count,
                                      uint16_t slot);
static void alarm_bitmap_set_slot(uint8_t *bitmap,
                                  uint16_t slot_count,
                                  uint16_t slot,
                                  qm_bool used);

/**
 * @brief Return whether one source string is valid.
 */
static qm_bool is_valid_source(const char *source)
{
    if (source == NULL) {
        return QM_FALSE;
    }

    return (qm_strcmp(source, QM_ALARM_SOURCE_DEVICE) == 0 ||
            qm_strcmp(source, QM_ALARM_SOURCE_APP) == 0) ? QM_TRUE : QM_FALSE;
}

static const char *alarm_state_str(qm_alarm_state_t state)
{
    switch (state) {
        case QM_ALARM_STATE_RINGING:
            return "ringing";
        case QM_ALARM_STATE_SNOOZING:
            return "snoozing";
        case QM_ALARM_STATE_IDLE:
        default:
            return "idle";
    }
}

static int report_state(alarm_ctx_t *ctx)
{
    cJSON *array;
    cJSON *item;
    char *payload;
    int payload_len;
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (ctx->config.iot_handle == NULL || !ctx->cloud_ready ||
        ctx->state_alarm_id == QM_ALARM_LOCAL_ID_AUTO) {
        ctx->state_dirty = QM_TRUE;
        return QM_EOK;
    }

    array = cJSON_CreateArray();
    item = cJSON_CreateObject();
    if (array == NULL || item == NULL) {
        cJSON_Delete(array);
        cJSON_Delete(item);
        return -QM_ENOMEM;
    }
    cJSON_AddNumberToObject(item, "alarmId", ctx->state_alarm_id);
    cJSON_AddStringToObject(item, "state", alarm_state_str(ctx->state));
    cJSON_AddItemToArray(array, item);
    payload = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    if (payload == NULL) {
        return -QM_ENOMEM;
    }
    payload_len = (int)qm_strlen(payload);
    ret = qm_iot_up_report_ex(ctx->config.iot_handle,
                              (char *)DISPATCH_METHOD,
                              (char *)QM_ALARM_SUBMETHOD_STATE_CHANGE,
                              (uint32_t)next_msg_id(ctx),
                              payload,
                              payload_len);
    cJSON_free(payload);
    if (ret == QM_EOK) {
        ctx->state_dirty = QM_FALSE;
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
                      "report alarm state id=%u state=%s",
                      (unsigned int)ctx->state_alarm_id,
                      alarm_state_str(ctx->state));
    } else {
        ctx->state_dirty = QM_TRUE;
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_CLOUD,
                      "report alarm state failed id=%u state=%s ret=%d",
                      (unsigned int)ctx->state_alarm_id,
                      alarm_state_str(ctx->state),
                      ret);
    }
    return ret;
}

static void set_state(alarm_ctx_t *ctx,
                      qm_alarm_state_t state,
                      const qm_alarm_item_t *alarm,
                      qm_bool report_now)
{
    if (ctx == NULL) {
        return;
    }
    if (alarm != NULL && alarm->alarm_id != QM_ALARM_LOCAL_ID_AUTO) {
        ctx->state_alarm_id = alarm->alarm_id;
    }
    if (ctx->state == state && !ctx->state_dirty) {
        return;
    }
    ctx->state = state;
    ctx->state_dirty = QM_TRUE;
    if (report_now) {
        (void)report_state(ctx);
    }
}

/**
 * @brief Return whether one source string is the device-side source.
 */
static qm_bool is_device_source(const char *source)
{
    return (source != NULL && qm_strcmp(source, QM_ALARM_SOURCE_DEVICE) == 0) ?
           QM_TRUE :
           QM_FALSE;
}

/**
 * @brief Convert one tone type to protocol text.
 */
static const char *tone_type_str(qm_alarm_tone_type_t type)
{
    switch (type) {
        case QM_ALARM_TONE_TYPE_LOCAL:
            return TONE_TYPE_LOCAL_STR;
        case QM_ALARM_TONE_TYPE_ONLINE:
            return TONE_TYPE_ONLINE_STR;
        default:
            return "unknown";
    }
}

/**
 * @brief Append one repeat label into a comma-separated buffer.
 */
static void repeat_mask_append(char *buf,
                               uint32_t buf_len,
                               uint32_t *used,
                               const char *text)
{
    int written;

    if (buf == NULL || used == NULL || text == NULL || buf_len == 0U || *used >= buf_len) {
        return;
    }

    written = qm_snprintf(buf + *used,
                          buf_len - *used,
                          "%s%s",
                          (*used > 0U) ? "," : "",
                          text);
    if (written <= 0) {
        return;
    }

    if ((uint32_t)written >= buf_len - *used) {
        *used = buf_len - 1U;
        return;
    }
    *used += (uint32_t)written;
}

/**
 * @brief Convert one repeat mask into readable weekday text.
 */
static const char *repeat_mask_str(uint8_t repeat_mask, char *buf, uint32_t buf_len)
{
    uint32_t used = 0U;

    if (buf == NULL || buf_len == 0U) {
        return "";
    }

    buf[0] = '\0';
    if (repeat_mask == 0U) {
        qm_snprintf(buf, buf_len, "once");
        return buf;
    }

    if ((repeat_mask & QM_ALARM_REPEAT_MON) != 0U) {
        repeat_mask_append(buf, buf_len, &used, "mon");
    }
    if ((repeat_mask & QM_ALARM_REPEAT_TUE) != 0U) {
        repeat_mask_append(buf, buf_len, &used, "tue");
    }
    if ((repeat_mask & QM_ALARM_REPEAT_WED) != 0U) {
        repeat_mask_append(buf, buf_len, &used, "wed");
    }
    if ((repeat_mask & QM_ALARM_REPEAT_THU) != 0U) {
        repeat_mask_append(buf, buf_len, &used, "thu");
    }
    if ((repeat_mask & QM_ALARM_REPEAT_FRI) != 0U) {
        repeat_mask_append(buf, buf_len, &used, "fri");
    }
    if ((repeat_mask & QM_ALARM_REPEAT_SAT) != 0U) {
        repeat_mask_append(buf, buf_len, &used, "sat");
    }
    if ((repeat_mask & QM_ALARM_REPEAT_SUN) != 0U) {
        repeat_mask_append(buf, buf_len, &used, "sun");
    }

    if (buf[0] == '\0') {
        qm_snprintf(buf, buf_len, "none");
    }
    return buf;
}

/**
 * @brief Parse one decimal string into an unsigned 8-bit value.
 */
static qm_bool parse_u8_text(const char *text, uint8_t max_value, uint8_t *value)
{
    uint32_t parsed = 0U;

    if (text == NULL || text[0] == '\0' || value == NULL) {
        return QM_FALSE;
    }

    while (*text != '\0') {
        if (*text < '0' || *text > '9') {
            return QM_FALSE;
        }
        parsed = parsed * 10U + (uint32_t)(*text - '0');
        if (parsed > max_value) {
            return QM_FALSE;
        }
        text++;
    }

    *value = (uint8_t)parsed;
    return QM_TRUE;
}

/**
 * @brief Refresh the next local alarm id from one loaded or applied alarm.
 */
static void refresh_next_local_alarm_id(alarm_ctx_t *ctx,
                                        const qm_alarm_item_t *alarm)
{
    if (ctx == NULL || alarm == NULL) {
        return;
    }

    if (!is_device_source(alarm->source) || alarm->alarm_id == QM_ALARM_LOCAL_ID_AUTO) {
        return;
    }

    if (alarm->alarm_id >= ctx->next_local_alarm_id && alarm->alarm_id < MAX_ALARM_ID) {
        ctx->next_local_alarm_id = (uint16_t)alarm->alarm_id + 1U;
    }
}

/**
 * @brief Get the extensions JSON length for logging and storage.
 */
static uint32_t alarm_extensions_len(const qm_alarm_item_t *alarm)
{
    if (alarm == NULL || alarm->extensions == NULL) {
        return 0U;
    }

    return qm_strlen(alarm->extensions);
}

/**
 * @brief Log one alarm summary, including extensions JSON content.
 */
static void log_alarm_summary(const char *stage,
                              const qm_alarm_item_t *alarm,
                              int32_t index)
{
    char repeat_text[32];

    if (stage == NULL || alarm == NULL) {
        return;
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_STATS,
            "%s index=%ld alarm_id=%u source=%s name=%s time=%02u:%02u "
            "enabled=%u repeat=0x%02x(%s) tone={type=%s local_id=%u online_id=%s} vibrate=%u "
            "snooze={enabled=%u interval=%u max=%u} extensions_len=%u extensions=%s",
            stage,
            (long)index,
            (unsigned int)alarm->alarm_id,
            alarm->source,
            alarm->name,
            (unsigned int)alarm->hour,
            (unsigned int)alarm->minute,
            (unsigned int)alarm->enabled,
            (unsigned int)alarm->repeat_mask,
            repeat_mask_str(alarm->repeat_mask, repeat_text, sizeof(repeat_text)),
            tone_type_str(alarm->tone.type),
            (unsigned int)alarm->tone.local_id,
            alarm->tone.online_id,
            (unsigned int)alarm->vibrate,
            (unsigned int)alarm->snooze.enabled,
            (unsigned int)alarm->snooze.interval_min,
            (unsigned int)alarm->snooze.max_count,
            (unsigned int)alarm_extensions_len(alarm),
            (alarm->extensions != NULL) ? alarm->extensions : "");
}

/**
 * @brief Log the current alarm list summary, including extensions JSON content.
 */
static void log_alarm_list_summary(const char *stage, const alarm_ctx_t *ctx)
{
    uint16_t i;

    if (stage == NULL || ctx == NULL) {
        return;
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_STATS,
            "%s count=%u capacity=%u cloud_ready=%u dirty=%u "
            "next_alarm_index=%ld active_alarm_index=%ld snooze_pending=%u",
            stage,
            (unsigned int)ctx->count,
            (unsigned int)ctx->capacity,
            (unsigned int)ctx->cloud_ready,
            (unsigned int)ctx->dirty,
            (long)ctx->next_alarm_index,
            (long)ctx->active_alarm_index,
            (unsigned int)ctx->snooze_pending);
    for (i = 0; i < ctx->count; i++) {
        log_alarm_summary(stage, &ctx->alarms[i], (int32_t)i);
    }
}

/**
 * @brief Log one compact JSON object or array for parser tracing.
 */
static void log_json_text(const char *stage, const cJSON *json)
{
    char *text;

    if (stage == NULL || json == NULL) {
        return;
    }

    text = cJSON_PrintUnformatted((cJSON *)json);
    if (text == NULL) {
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_CLOUD, "%s json print failed", stage);
        return;
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD, "%s %s", stage, text);
    cJSON_free(text);
}

/**
 * @brief Convert one Unix timestamp into a local readable string for logs.
 */
static const char *alarm_time_str(qm_time_t ts, char *buf, uint32_t buf_len)
{
    struct qm_tm tm;

    if (buf == NULL || buf_len == 0U) {
        return "";
    }

    if (ts == 0U || qm_localtime_r(&ts, &tm) == NULL) {
        qm_snprintf(buf, buf_len, "n/a");
        return buf;
    }

    qm_snprintf(buf,
                buf_len,
                "%04d-%02d-%02d %02d:%02d:%02d",
                tm.tm_year + 1900,
                tm.tm_mon + 1,
                tm.tm_mday,
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec);
    return buf;
}

/**
 * @brief Convert one local alarm calendar time into a Unix timestamp.
 */
static qm_time_t local_alarm_time_to_timestamp(const struct qm_tm *local_tm)
{
    qm_timeval_t now_tv = {0};
    qm_timezone_t timezone = {0};
    qm_time_t ts;
    int64_t adjusted_ts;

    if (local_tm == NULL) {
        return 0U;
    }

    ts = qm_mktime((struct qm_tm *)local_tm);
    if (qm_gettimeofday(&now_tv, &timezone) != QM_EOK) {
        return ts;
    }

    adjusted_ts = (int64_t)ts - (int64_t)timezone.minuteswest * 60LL;
    return (adjusted_ts > 0LL) ? (qm_time_t)adjusted_ts : 0U;
}

/**
 * @brief Return the number of bytes needed for one slot-usage bitmap.
 */
static uint16_t alarm_bitmap_bytes(uint16_t slot_count)
{
    return (uint16_t)((slot_count + 7U) / 8U);
}

/**
 * @brief Return whether one slot bit is set inside a bitmap snapshot.
 */
static qm_bool alarm_bitmap_slot_used(const uint8_t *bitmap,
                                      uint16_t slot_count,
                                      uint16_t slot)
{
    if (bitmap == NULL || slot >= slot_count) {
        return QM_FALSE;
    }

    return ((bitmap[slot / 8U] & (uint8_t)(1U << (slot % 8U))) != 0U) ? QM_TRUE : QM_FALSE;
}

/**
 * @brief Set or clear one slot bit inside a bitmap snapshot.
 */
static void alarm_bitmap_set_slot(uint8_t *bitmap,
                                  uint16_t slot_count,
                                  uint16_t slot,
                                  qm_bool used)
{
    uint8_t mask;

    if (bitmap == NULL || slot >= slot_count) {
        return;
    }

    mask = (uint8_t)(1U << (slot % 8U));
    if (used) {
        bitmap[slot / 8U] |= mask;
    } else {
        bitmap[slot / 8U] &= (uint8_t)~mask;
    }
}

/**
 * @brief Count how many slots are marked used in one bitmap snapshot.
 */
/**
 * @brief Reset runtime slot usage tracking.
 */
static void reset_slot_tracking(alarm_ctx_t *ctx)
{
    uint16_t i;

    if (ctx == NULL) {
        return;
    }

    if (ctx->alarm_slots != NULL) {
        for (i = 0; i < ctx->capacity; i++) {
            ctx->alarm_slots[i] = INVALID_SLOT_ID;
        }
    }
    if (ctx->used_bitmap != NULL && ctx->bitmap_bytes > 0U) {
        qm_memset(ctx->used_bitmap, 0, ctx->bitmap_bytes);
    }
    if (ctx->slot_dirty != NULL) {
        qm_memset(ctx->slot_dirty, 0, sizeof(ctx->slot_dirty[0]) * ctx->capacity);
    }
    ctx->bitmap_dirty = QM_FALSE;
}

/**
 * @brief Release an owned extensions JSON string in an alarm item.
 */
static void clear_alarm_extensions(qm_alarm_item_t *alarm)
{
    if (alarm != NULL && alarm->extensions != NULL) {
        qm_free((void *)alarm->extensions);
        alarm->extensions = NULL;
    }
}

/**
 * @brief Populate one alarm item from already-parsed field values.
 */
static void populate_alarm_item_fields(qm_alarm_item_t *alarm,
                                       uint8_t alarm_id,
                                       const char *source,
                                       const char *name,
                                       uint8_t hour,
                                       uint8_t minute,
                                       qm_bool enabled,
                                       uint8_t repeat_mask,
                                       const qm_alarm_tone_t *tone,
                                       qm_bool vibrate,
                                       const qm_alarm_snooze_t *snooze)
{
    if (alarm == NULL) {
        return;
    }

    alarm->alarm_id = alarm_id;
    qm_snprintf(alarm->source,
                sizeof(alarm->source),
                "%s",
                (source != NULL) ? source : "");
    qm_snprintf(alarm->name,
                sizeof(alarm->name),
                "%s",
                (name != NULL) ? name : "");
    alarm->hour = hour;
    alarm->minute = minute;
    alarm->enabled = enabled;
    alarm->repeat_mask = repeat_mask;
    if (tone != NULL) {
        alarm->tone = *tone;
    }
    alarm->vibrate = vibrate;
    if (snooze != NULL) {
        alarm->snooze = *snooze;
    }
}

/**
 * @brief Populate one persisted alarm slot item from an in-memory alarm item.
 */
static void populate_store_item_fields(alarm_store_item_t *item,
                                       const qm_alarm_item_t *alarm,
                                       uint16_t extensions_len)
{
    if (item == NULL || alarm == NULL) {
        return;
    }

    qm_memset(item, 0, sizeof(*item));
    item->magic = STORE_ITEM_MAGIC;
    item->version = STORE_ITEM_VERSION;
    item->extensions_len = extensions_len;
    item->alarm_id = alarm->alarm_id;
    qm_snprintf(item->source, sizeof(item->source), "%s", alarm->source);
    qm_snprintf(item->name, sizeof(item->name), "%s", alarm->name);
    item->hour = alarm->hour;
    item->minute = alarm->minute;
    item->enabled = alarm->enabled;
    item->repeat_mask = alarm->repeat_mask;
    item->tone_type = (uint8_t)alarm->tone.type;
    item->tone_local_id = alarm->tone.local_id;
    qm_snprintf(item->tone_online_id,
                sizeof(item->tone_online_id),
                "%s",
                alarm->tone.online_id);
    item->vibrate = alarm->vibrate;
    item->snooze = alarm->snooze;
}

/**
 * @brief Validate one extensions JSON string.
 */
static int validate_alarm_extensions_text(const char *extensions_text)
{
    cJSON *extensions_json = NULL;

    if (extensions_text == NULL || extensions_text[0] == '\0') {
        return QM_EOK;
    }

    extensions_json = cJSON_Parse(extensions_text);
    if (!cJSON_IsObject(extensions_json)) {
        cJSON_Delete(extensions_json);
        return -QM_EINVAL;
    }
    cJSON_Delete(extensions_json);
    return QM_EOK;
}

/**
 * @brief Validate one owned extensions JSON buffer and attach it to the alarm.
 */
static int set_alarm_extensions_owned(qm_alarm_item_t *alarm, char *extensions_text)
{
    int ret;

    if (alarm == NULL) {
        qm_free(extensions_text);
        return -QM_EINVAL;
    }
    if (extensions_text == NULL || extensions_text[0] == '\0') {
        qm_free(extensions_text);
        alarm->extensions = NULL;
        return QM_EOK;
    }

    ret = validate_alarm_extensions_text(extensions_text);
    if (ret != QM_EOK) {
        qm_free(extensions_text);
        return ret;
    }

    alarm->extensions = extensions_text;
    return QM_EOK;
}

/**
 * @brief Validate and copy a borrowed extensions object JSON string.
 */
static int clone_alarm_extensions(const char *extensions, const char **out)
{
    char *copy;
    uint32_t len;
    int ret;

    if (out == NULL) {
        return -QM_EINVAL;
    }

    *out = NULL;
    if (extensions == NULL || extensions[0] == '\0') {
        return QM_EOK;
    }

    ret = validate_alarm_extensions_text(extensions);
    if (ret != QM_EOK) {
        return ret;
    }

    len = qm_strlen(extensions);
    copy = (char *)qm_malloc(len + 1U);
    if (copy == NULL) {
        return -QM_ENOMEM;
    }

    qm_snprintf(copy, len + 1U, "%s", extensions);
    *out = copy;
    return QM_EOK;
}

/**
 * @brief Reorder in-memory alarms by time while preserving tie order.
 */
static void sort_alarm_entries(alarm_ctx_t *ctx)
{
    char active_source[QM_ALARM_SOURCE_MAX_LEN];
    char next_source[QM_ALARM_SOURCE_MAX_LEN];
    char snooze_source[QM_ALARM_SOURCE_MAX_LEN];
    uint8_t active_alarm_id = 0U;
    uint8_t next_alarm_id = 0U;
    uint8_t snooze_alarm_id = 0U;
    qm_bool has_active = QM_FALSE;
    qm_bool has_next = QM_FALSE;
    qm_bool has_snooze = QM_FALSE;
    uint16_t i;

    if (ctx == NULL || ctx->count < 2U) {
        return;
    }

    qm_memset(active_source, 0, sizeof(active_source));
    qm_memset(next_source, 0, sizeof(next_source));
    qm_memset(snooze_source, 0, sizeof(snooze_source));

    if (ctx->active_alarm_index >= 0 &&
        (uint16_t)ctx->active_alarm_index < ctx->count) {
        qm_snprintf(active_source,
                    sizeof(active_source),
                    "%s",
                    ctx->alarms[ctx->active_alarm_index].source);
        active_alarm_id = ctx->alarms[ctx->active_alarm_index].alarm_id;
        has_active = QM_TRUE;
    }
    if (ctx->next_alarm_index >= 0 &&
        (uint16_t)ctx->next_alarm_index < ctx->count) {
        qm_snprintf(next_source,
                    sizeof(next_source),
                    "%s",
                    ctx->alarms[ctx->next_alarm_index].source);
        next_alarm_id = ctx->alarms[ctx->next_alarm_index].alarm_id;
        has_next = QM_TRUE;
    }
    if (ctx->snooze_pending &&
        ctx->snooze_alarm_index >= 0 &&
        (uint16_t)ctx->snooze_alarm_index < ctx->count) {
        qm_snprintf(snooze_source,
                    sizeof(snooze_source),
                    "%s",
                    ctx->alarms[ctx->snooze_alarm_index].source);
        snooze_alarm_id = ctx->alarms[ctx->snooze_alarm_index].alarm_id;
        has_snooze = QM_TRUE;
    }

    for (i = 1U; i < ctx->count; i++) {
        qm_alarm_item_t current_alarm = ctx->alarms[i];
        uint16_t current_slot = ctx->alarm_slots[i];
        uint16_t j = i;

        while (j > 0U &&
               (current_alarm.hour < ctx->alarms[j - 1U].hour ||
                (current_alarm.hour == ctx->alarms[j - 1U].hour &&
                 current_alarm.minute < ctx->alarms[j - 1U].minute))) {
            ctx->alarms[j] = ctx->alarms[j - 1U];
            ctx->alarm_slots[j] = ctx->alarm_slots[j - 1U];
            j--;
        }

        ctx->alarms[j] = current_alarm;
        ctx->alarm_slots[j] = current_slot;
    }

    ctx->active_alarm_index = has_active ?
                              find_alarm(ctx, active_source, active_alarm_id) :
                              -1;
    ctx->next_alarm_index = has_next ?
                            find_alarm(ctx, next_source, next_alarm_id) :
                            -1;
    ctx->snooze_alarm_index = has_snooze ?
                              find_alarm(ctx, snooze_source, snooze_alarm_id) :
                              -1;
}

/**
 * @brief Return whether one pending snooze target is still trackable.
 */
static qm_bool has_pending_snooze(const alarm_ctx_t *ctx)
{
    if (ctx == NULL || !ctx->snooze_pending) {
        return QM_FALSE;
    }

    if (ctx->snooze_alarm_index < 0 ||
        (uint16_t)ctx->snooze_alarm_index >= ctx->count ||
        ctx->snooze_fire_time == 0U) {
        return QM_FALSE;
    }

    return QM_TRUE;
}

/**
 * @brief Clear one pending snooze target and optionally reset its retry count.
 */
static void clear_pending_snooze(alarm_ctx_t *ctx, qm_bool reset_count)
{
    if (ctx == NULL) {
        return;
    }

    ctx->snooze_pending = QM_FALSE;
    ctx->snooze_alarm_index = -1;
    ctx->snooze_fire_time = 0U;
    if (reset_count) {
        ctx->active_snooze_count = 0U;
    }
}

/**
 * @brief Notify one alarm event to the application.
 */
static void notify_alarm(alarm_ctx_t *ctx,
                         qm_alarm_notify_t notify,
                         const qm_alarm_item_t *item)
{
    if (ctx == NULL || ctx->notify_cb == NULL || item == NULL) {
        return;
    }

    ctx->owner_callback_active = 1U;
    ctx->notify_cb(ctx, notify, item, ctx->user_ctx);
    ctx->owner_callback_active = 0U;
}

/**
 * @brief Deep-copy an alarm item while taking ownership of copied extensions.
 */
static int copy_alarm_owned(qm_alarm_item_t *dst,
                            const qm_alarm_item_t *src)
{
    const char *extensions = NULL;
    int ret;

    if (dst == NULL || src == NULL) {
        return -QM_EINVAL;
    }

    ret = clone_alarm_extensions(src->extensions, &extensions);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_memcpy(dst, src, sizeof(*dst));
    dst->extensions = extensions;
    return QM_EOK;
}

/**
 * @brief Move one owned alarm item into the destination.
 */
static void move_alarm_owned(qm_alarm_item_t *dst,
                             qm_alarm_item_t *src,
                             qm_bool clear_dst)
{
    if (dst == NULL || src == NULL) {
        return;
    }

    if (clear_dst) {
        clear_alarm_extensions(dst);
    }
    qm_memcpy(dst, src, sizeof(*dst));
    src->extensions = NULL;
}

/**
 * @brief Clear all loaded alarm items and reset count.
 */
static void clear_loaded_alarms(alarm_ctx_t *ctx)
{
    uint16_t i;

    if (ctx == NULL || ctx->alarms == NULL) {
        return;
    }

    for (i = 0; i < ctx->count; i++) {
        clear_alarm_extensions(&ctx->alarms[i]);
    }
    qm_memset(ctx->alarms, 0, sizeof(ctx->alarms[0]) * ctx->capacity);
    reset_slot_tracking(ctx);
    ctx->count = 0;
}

/**
 * @brief Build one compact KV key for a slot or order entry.
 */
static void build_store_key(char *key,
                            uint32_t key_len,
                            const char *prefix,
                            uint16_t value)
{
    if (key == NULL || key_len == 0U || prefix == NULL) {
        return;
    }

    qm_snprintf(key, key_len, "%s%u", prefix, (unsigned int)value);
}

/**
 * @brief Return the tracked storage slot for one alarm index.
 */
static uint16_t alarm_slot(const alarm_ctx_t *ctx, uint16_t alarm_index)
{
    if (ctx == NULL || ctx->alarm_slots == NULL || alarm_index >= ctx->count) {
        return INVALID_SLOT_ID;
    }

    return ctx->alarm_slots[alarm_index];
}

/**
 * @brief Find one in-memory alarm index by its persisted storage slot.
 */
static int32_t find_alarm_index_by_slot(const alarm_ctx_t *ctx, uint16_t slot)
{
    uint16_t i;

    if (ctx == NULL || ctx->alarm_slots == NULL) {
        return -1;
    }

    for (i = 0; i < ctx->count; i++) {
        if (ctx->alarm_slots[i] == slot) {
            return (int32_t)i;
        }
    }

    return -1;
}

/**
 * @brief Mark one storage slot dirty so the next save only flushes changed alarms.
 */
static void mark_alarm_slot_dirty(alarm_ctx_t *ctx, uint16_t slot)
{
    if (ctx == NULL || ctx->slot_dirty == NULL ||
        slot == INVALID_SLOT_ID || slot >= ctx->capacity) {
        return;
    }

    ctx->slot_dirty[slot] = QM_TRUE;
}

/**
 * @brief Mark one alarm entry dirty through its current in-memory index.
 */
static void mark_alarm_index_dirty(alarm_ctx_t *ctx, int32_t alarm_index)
{
    if (ctx == NULL || alarm_index < 0 || (uint16_t)alarm_index >= ctx->count) {
        return;
    }

    mark_alarm_slot_dirty(ctx, alarm_slot(ctx, (uint16_t)alarm_index));
}

/**
 * @brief Find the first free storage slot.
 */
static int allocate_alarm_slot(alarm_ctx_t *ctx, uint16_t *slot_out)
{
    uint16_t slot;

    if (ctx == NULL || ctx->used_bitmap == NULL || slot_out == NULL) {
        return -QM_EINVAL;
    }

    for (slot = 0; slot < ctx->capacity; slot++) {
        if (!alarm_bitmap_slot_used(ctx->used_bitmap, ctx->capacity, slot)) {
            *slot_out = slot;
            return QM_EOK;
        }
    }

    return -QM_EFULL;
}

/**
 * @brief Remove one persisted alarm slot from the current store layout.
 */
static void delete_alarm_slot_store(uint16_t slot)
{
    char key[STORE_KEY_BUF_LEN];

    build_store_key(key, sizeof(key), STORE_ITEM_KEY_PREFIX, slot);
    (void)qm_kv_del(key);
}

/**
 * @brief Remove persisted alarm data after version mismatch or reset.
 */
static void wipe_alarm_store(void)
{
    uint16_t i;
    char key[STORE_KEY_BUF_LEN];

    for (i = 0; i < STORE_CLEANUP_SLOT_COUNT; i++) {
        delete_alarm_slot_store(i);
        build_store_key(key, sizeof(key), "qmalarm.e.", i);
        (void)qm_kv_del(key);
        build_store_key(key, sizeof(key), "qmalarm.o.", i);
        (void)qm_kv_del(key);
    }
    (void)qm_kv_del(STORE_META_KV_KEY);
    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_STORAGE,
            "cleanup mismatched alarm store slots=%u",
            (unsigned int)STORE_CLEANUP_SLOT_COUNT);
}

/**
 * @brief Save the compact bitmap meta record.
 */
static int save_alarm_meta(alarm_ctx_t *ctx)
{
    alarm_store_meta_t meta;
    uint8_t meta_buf[sizeof(alarm_store_meta_t) + MAX_ALARM_BITMAP_BYTES];
    int meta_len;
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    if (ctx->count == 0U) {
        ret = qm_kv_del(STORE_META_KV_KEY);
        if (ret != QM_EOK && ret != -QM_ENOENT) {
            QM_ALARM_LOGE(LOG_TAG, "delete empty alarm meta failed ret=%d", ret);
            return ret;
        }
        ctx->bitmap_dirty = QM_FALSE;
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_STORAGE, "delete empty alarm meta");
        return QM_EOK;
    }

    qm_memset(&meta, 0, sizeof(meta));
    meta.magic = STORE_META_MAGIC;
    meta.version = STORE_META_VERSION;

    meta_len = (int)sizeof(meta) + (int)ctx->bitmap_bytes;
    qm_memset(meta_buf, 0, sizeof(meta_buf));
    qm_memcpy(meta_buf, &meta, sizeof(meta));
    qm_memcpy(meta_buf + sizeof(meta), ctx->used_bitmap, ctx->bitmap_bytes);

    ret = qm_kv_set(STORE_META_KV_KEY, meta_buf, meta_len, 1);
    if (ret != QM_EOK) {
        QM_ALARM_LOGE(LOG_TAG,
                "save alarm meta failed count=%u bitmap_bytes=%u ret=%d",
                (unsigned int)ctx->count,
                (unsigned int)ctx->bitmap_bytes,
                ret);
        return ret;
    }

    ctx->bitmap_dirty = QM_FALSE;
    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_STORAGE,
            "save alarm meta count=%u bitmap_bytes=%u",
            (unsigned int)ctx->count,
            (unsigned int)ctx->bitmap_bytes);
    return QM_EOK;
}

/**
 * @brief Load the compact bitmap meta record.
 */
static int load_alarm_meta(alarm_store_meta_t *meta,
                           uint8_t *bitmap,
                           uint16_t *bitmap_bytes)
{
    uint8_t meta_buf[sizeof(alarm_store_meta_t) + MAX_ALARM_BITMAP_BYTES];
    int len = (int)sizeof(meta_buf);

    if (meta == NULL || bitmap == NULL || bitmap_bytes == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(meta, 0, sizeof(*meta));
    qm_memset(bitmap, 0, MAX_ALARM_BITMAP_BYTES);
    *bitmap_bytes = 0U;

    if (qm_kv_get(STORE_META_KV_KEY, meta_buf, &len) != QM_EOK) {
        return -QM_ENOENT;
    }
    if (len < (int)sizeof(*meta) ||
        len > (int)(sizeof(*meta) + MAX_ALARM_BITMAP_BYTES)) {
        return -QM_EIO;
    }

    qm_memcpy(meta, meta_buf, sizeof(*meta));
    *bitmap_bytes = (uint16_t)(len - (int)sizeof(*meta));
    if (*bitmap_bytes > 0U) {
        qm_memcpy(bitmap, meta_buf + sizeof(*meta), *bitmap_bytes);
    }

    return QM_EOK;
}

/**
 * @brief Save one bitmap-indexed alarm slot as a single KV blob.
 */
static int save_alarm_slot(alarm_ctx_t *ctx, uint16_t alarm_index)
{
    const qm_alarm_item_t *alarm;
    const void *item_value = NULL;
    alarm_store_item_t item;
    uint8_t *item_blob = NULL;
    uint32_t item_blob_len_u32;
    uint16_t slot;
    uint32_t extensions_len_u32;
    char key[STORE_KEY_BUF_LEN];
    int ret;

    if (ctx == NULL || alarm_index >= ctx->count) {
        return -QM_EINVAL;
    }

    slot = alarm_slot(ctx, alarm_index);
    if (slot == INVALID_SLOT_ID || slot >= ctx->capacity) {
        return -QM_EINVAL;
    }

    alarm = &ctx->alarms[alarm_index];
    extensions_len_u32 = alarm_extensions_len(alarm);
    if (extensions_len_u32 > MAX_STORE_EXTENSIONS_LEN) {
        QM_ALARM_LOGE(LOG_TAG,
                "save alarm slot extensions too large index=%u slot=%u len=%u",
                (unsigned int)alarm_index,
                (unsigned int)slot,
                (unsigned int)extensions_len_u32);
        return -QM_EINVAL;
    }

    populate_store_item_fields(&item,
                               alarm,
                               (uint16_t)extensions_len_u32);
    item_blob_len_u32 = (uint32_t)sizeof(item) + extensions_len_u32;

    if (extensions_len_u32 > 0U) {
        item_blob = (uint8_t *)qm_malloc(item_blob_len_u32);
        if (item_blob == NULL) {
            return -QM_ENOMEM;
        }

        qm_memcpy(item_blob, &item, sizeof(item));
        qm_memcpy(item_blob + sizeof(item), alarm->extensions, extensions_len_u32);
    }

    item_value = (item_blob != NULL) ? (const void *)item_blob : (const void *)&item;
    build_store_key(key, sizeof(key), STORE_ITEM_KEY_PREFIX, slot);
    ret = qm_kv_set(key,
                    item_value,
                    (int)item_blob_len_u32,
                    1);
    if (item_blob != NULL) {
        qm_free(item_blob);
    }
    if (ret != QM_EOK) {
        QM_ALARM_LOGE(LOG_TAG,
                "save alarm slot failed index=%u slot=%u item_len=%u ret=%d",
                (unsigned int)alarm_index,
                (unsigned int)slot,
                (unsigned int)item_blob_len_u32,
                ret);
        return ret;
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_STORAGE,
            "save slot index=%u slot=%u item_len=%u extensions_len=%u",
            (unsigned int)alarm_index,
            (unsigned int)slot,
            (unsigned int)item_blob_len_u32,
            (unsigned int)item.extensions_len);
    return QM_EOK;
}

/**
 * @brief Save one dirty alarm slot by resolving its current in-memory index.
 */
static int save_alarm_slot_by_slot(alarm_ctx_t *ctx, uint16_t slot)
{
    int32_t alarm_index;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    alarm_index = find_alarm_index_by_slot(ctx, slot);
    if (alarm_index < 0) {
        return -QM_ENOENT;
    }

    return save_alarm_slot(ctx, (uint16_t)alarm_index);
}

/**
 * @brief Load one bitmap-indexed alarm slot and its inline extensions blob.
 */
static int load_alarm_slot(uint16_t slot, qm_alarm_item_t *alarm)
{
    alarm_store_item_t item;
    qm_alarm_tone_t tone;
    uint8_t *item_blob = NULL;
    char *extensions_copy = NULL;
    uint32_t expected_len_u32;
    int len;
    int ret;
    char key[STORE_KEY_BUF_LEN];

    if (alarm == NULL) {
        return -QM_EINVAL;
    }

    item_blob = (uint8_t *)qm_malloc(MAX_STORE_ITEM_BLOB_LEN);
    if (item_blob == NULL) {
        return -QM_ENOMEM;
    }

    qm_memset(alarm, 0, sizeof(*alarm));
    build_store_key(key, sizeof(key), STORE_ITEM_KEY_PREFIX, slot);
    len = (int)MAX_STORE_ITEM_BLOB_LEN;
    ret = qm_kv_get(key, item_blob, &len);
    if (ret != QM_EOK) {
        qm_free(item_blob);
        return -QM_ENOENT;
    }
    if (len < (int)sizeof(item)) {
        qm_free(item_blob);
        return -QM_EIO;
    }

    qm_memcpy(&item, item_blob, sizeof(item));
    if (item.magic != STORE_ITEM_MAGIC ||
        item.version != STORE_ITEM_VERSION) {
        qm_free(item_blob);
        return -QM_EINVAL;
    }

    expected_len_u32 = (uint32_t)sizeof(item) + (uint32_t)item.extensions_len;
    if (expected_len_u32 > MAX_STORE_ITEM_BLOB_LEN ||
        len != (int)expected_len_u32) {
        qm_free(item_blob);
        return -QM_EIO;
    }

    qm_memset(&tone, 0, sizeof(tone));
    tone.type = (qm_alarm_tone_type_t)item.tone_type;
    tone.local_id = item.tone_local_id;
    qm_snprintf(tone.online_id, sizeof(tone.online_id), "%s", item.tone_online_id);
    populate_alarm_item_fields(alarm,
                               item.alarm_id,
                               item.source,
                               item.name,
                               item.hour,
                               item.minute,
                               item.enabled,
                               item.repeat_mask,
                               &tone,
                               item.vibrate,
                               &item.snooze);

    if (item.extensions_len > 0U) {
        extensions_copy = (char *)qm_malloc((uint32_t)item.extensions_len + 1U);
        if (extensions_copy == NULL) {
            qm_free(item_blob);
            return -QM_ENOMEM;
        }

        qm_memcpy(extensions_copy,
                  item_blob + sizeof(item),
                  (uint32_t)item.extensions_len);
        extensions_copy[item.extensions_len] = '\0';
        ret = set_alarm_extensions_owned(alarm, extensions_copy);
        if (ret != QM_EOK) {
            qm_free(item_blob);
            return ret;
        }
        extensions_copy = NULL;
    }

    normalize_alarm(alarm);
    if (validate_alarm(alarm) != QM_EOK) {
        clear_alarm_extensions(alarm);
        qm_free(item_blob);
        return -QM_EINVAL;
    }

    qm_free(item_blob);
    return QM_EOK;
}

/**
 * @brief Save the full alarm state into bitmap-backed keys.
 */
static int save_alarms(alarm_ctx_t *ctx)
{
    uint16_t i;
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->storage_ready) {
        return -QM_EINIT;
    }

    if (ctx->bitmap_dirty) {
        ret = save_alarm_meta(ctx);
        if (ret != QM_EOK) {
            return ret;
        }
    }

    for (i = 0; i < ctx->capacity; i++) {
        if (ctx->slot_dirty == NULL || !ctx->slot_dirty[i]) {
            continue;
        }
        if (!alarm_bitmap_slot_used(ctx->used_bitmap, ctx->capacity, i)) {
            delete_alarm_slot_store(i);
            ctx->slot_dirty[i] = QM_FALSE;
            continue;
        }

        ret = save_alarm_slot_by_slot(ctx, i);
        if (ret != QM_EOK) {
            return ret;
        }
        ctx->slot_dirty[i] = QM_FALSE;
    }

    return QM_EOK;
}

/**
 * @brief Load alarms from bitmap meta and per-slot payloads.
 */
static int load_alarms(alarm_ctx_t *ctx)
{
    alarm_store_meta_t meta;
    uint8_t stored_bitmap[MAX_ALARM_BITMAP_BYTES];
    uint16_t stored_bitmap_bytes = 0U;
    uint16_t stored_slot_count;
    uint16_t slot;
    qm_bool needs_repair = QM_FALSE;
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->storage_ready) {
        return -QM_EINIT;
    }

    clear_loaded_alarms(ctx);
    ctx->next_local_alarm_id = 1U;

    ret = load_alarm_meta(&meta, stored_bitmap, &stored_bitmap_bytes);
    if (ret == -QM_ENOENT) {
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_STORAGE, "alarm store not found");
        return QM_EOK;
    }
    if (ret != QM_EOK ||
        meta.magic != STORE_META_MAGIC ||
        meta.version != STORE_META_VERSION ||
        stored_bitmap_bytes == 0U) {
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_STORAGE,
                "alarm meta invalid ret=%d magic=0x%08x version=%u bitmap_bytes=%u",
                ret,
                (unsigned int)meta.magic,
                (unsigned int)meta.version,
                (unsigned int)stored_bitmap_bytes);
        wipe_alarm_store();
        return QM_EOK;
    }

    stored_slot_count = (uint16_t)(stored_bitmap_bytes * 8U);
    if (stored_bitmap_bytes != ctx->bitmap_bytes) {
        needs_repair = QM_TRUE;
    }

    for (slot = 0U; slot < stored_slot_count; slot++) {
        qm_alarm_item_t loaded_alarm;

        if (!alarm_bitmap_slot_used(stored_bitmap, stored_slot_count, slot)) {
            continue;
        }

        ret = load_alarm_slot(slot, &loaded_alarm);
        if (ret == -QM_ENOMEM) {
            clear_loaded_alarms(ctx);
            return ret;
        }
        if (ret != QM_EOK) {
            QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_STORAGE,
                    "load alarm slot invalid slot=%u ret=%d, delete stale item",
                    (unsigned int)slot,
                    ret);
            delete_alarm_slot_store(slot);
            needs_repair = QM_TRUE;
            continue;
        }
        if (slot >= ctx->capacity || ctx->count >= ctx->capacity) {
            QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_STORAGE,
                    "load alarm slot overflow slot=%u runtime_capacity=%u, delete stale item",
                    (unsigned int)slot,
                    (unsigned int)ctx->capacity);
            clear_alarm_extensions(&loaded_alarm);
            delete_alarm_slot_store(slot);
            needs_repair = QM_TRUE;
            continue;
        }

        qm_memcpy(&ctx->alarms[ctx->count], &loaded_alarm, sizeof(loaded_alarm));
        ctx->alarm_slots[ctx->count] = slot;
        alarm_bitmap_set_slot(ctx->used_bitmap, ctx->capacity, slot, QM_TRUE);
        refresh_next_local_alarm_id(ctx, &ctx->alarms[ctx->count]);
        log_alarm_summary("load alarm", &ctx->alarms[ctx->count], (int32_t)ctx->count);
        ctx->count++;
    }

    sort_alarm_entries(ctx);
    if (ctx->count == 0U) {
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_STORAGE, "alarm store not found");
    }
    if (needs_repair) {
        ctx->bitmap_dirty = QM_TRUE;
        ret = save_alarm_meta(ctx);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    log_alarm_list_summary("load alarm list", ctx);
    return QM_EOK;
}

/**
 * @brief Copy a bounded JSON string into a fixed-size destination buffer.
 */
static void copy_json_string(char *dst,
                             uint32_t dst_len,
                             const cJSON *object,
                             const char *key,
                             const char *fallback)
{
    const cJSON *item;
    const char *value = fallback;

    if (dst == NULL || dst_len == 0U) {
        return;
    }

    item = cJSON_GetObjectItem(object, key);
    if (cJSON_IsString(item) && cJSON_GetStringValue(item) != NULL) {
        value = cJSON_GetStringValue(item);
    }

    dst[0] = '\0';
    if (value != NULL) {
        qm_snprintf(dst, dst_len, "%s", value);
    }
}

/**
 * @brief Parse a JSON boolean, keeping fallback when the field is absent.
 */
static qm_bool json_bool_value(const cJSON *object,
                               const char *key,
                               qm_bool fallback)
{
    const cJSON *item = cJSON_GetObjectItem(object, key);

    if (cJSON_IsBool(item)) {
        return cJSON_IsTrue(item) ? QM_TRUE : QM_FALSE;
    }

    return fallback;
}

/**
 * @brief Parse a JSON integer with range clamping.
 */
static uint8_t json_u8_value(const cJSON *object,
                             const char *key,
                             uint8_t fallback,
                             uint8_t max_value)
{
    const cJSON *item = cJSON_GetObjectItem(object, key);
    int value;

    if (!cJSON_IsNumber(item)) {
        return fallback;
    }

    value = item->valueint;
    if (value < 0) {
        return 0;
    }
    if (value > max_value) {
        return max_value;
    }
    return (uint8_t)value;
}

/**
 * @brief Parse one JSON number field into uint8_t.
 */
static int parse_json_u8_field(const cJSON *object,
                               const char *key,
                               uint8_t min_value,
                               uint8_t max_value,
                               uint8_t *value)
{
    const cJSON *item;
    int parsed;

    if (object == NULL || key == NULL || value == NULL) {
        return -QM_EINVAL;
    }

    item = cJSON_GetObjectItem(object, key);
    if (!cJSON_IsNumber(item)) {
        return -QM_EINVAL;
    }

    parsed = item->valueint;
    if (parsed < (int)min_value || parsed > (int)max_value) {
        return -QM_EINVAL;
    }

    *value = (uint8_t)parsed;
    return QM_EOK;
}

/**
 * @brief Find an alarm by `(source, alarm_id)`.
 */
static int32_t find_alarm(alarm_ctx_t *ctx, const char *source, uint8_t alarm_id)
{
    uint16_t i;

    if (ctx == NULL || source == NULL || alarm_id == QM_ALARM_LOCAL_ID_AUTO) {
        return -1;
    }

    for (i = 0; i < ctx->count; i++) {
        if (ctx->alarms[i].alarm_id == alarm_id &&
            qm_strcmp(ctx->alarms[i].source, source) == 0) {
            return (int32_t)i;
        }
    }

    return -1;
}

/**
 * @brief Validate required alarm fields and value ranges.
 */
static int validate_alarm(const qm_alarm_item_t *alarm)
{
    cJSON *extensions = NULL;

    if (alarm == NULL || alarm->alarm_id == QM_ALARM_LOCAL_ID_AUTO ||
        !is_valid_source(alarm->source) ||
        alarm->hour > 23U || alarm->minute > 59U ||
        (alarm->repeat_mask & ~QM_ALARM_REPEAT_EVERYDAY) != 0U) {
        return -QM_EINVAL;
    }

    if (alarm->tone.type == QM_ALARM_TONE_TYPE_LOCAL) {
        if (alarm->tone.local_id == 0U || alarm->tone.local_id > MAX_LOCAL_TONE_ID) {
            return -QM_EINVAL;
        }
    } else if (alarm->tone.type == QM_ALARM_TONE_TYPE_ONLINE) {
        if (alarm->tone.online_id[0] == '\0') {
            return -QM_EINVAL;
        }
    } else {
        return -QM_EINVAL;
    }

    if (alarm->extensions != NULL && alarm->extensions[0] != '\0') {
        extensions = cJSON_Parse(alarm->extensions);
        if (!cJSON_IsObject(extensions)) {
            cJSON_Delete(extensions);
            return -QM_EINVAL;
        }
        cJSON_Delete(extensions);
    }

    return QM_EOK;
}

/**
 * @brief Normalize optional fields after parsing or local API input.
 */
static void normalize_alarm(qm_alarm_item_t *alarm)
{
    if (alarm == NULL) {
        return;
    }

    if (alarm->source[0] == '\0') {
        qm_snprintf(alarm->source, sizeof(alarm->source), "%s", QM_ALARM_SOURCE_DEVICE);
    }
    if (alarm->tone.type == QM_ALARM_TONE_TYPE_LOCAL) {
        if (alarm->tone.local_id == 0U) {
            alarm->tone.local_id = DEFAULT_LOCAL_TONE_ID;
        }
        alarm->tone.online_id[0] = '\0';
    } else if (alarm->tone.type == QM_ALARM_TONE_TYPE_ONLINE) {
        if (alarm->tone.online_id[0] == '\0') {
            qm_snprintf(alarm->tone.online_id,
                        sizeof(alarm->tone.online_id),
                        "%s",
                        DEFAULT_ONLINE_TONE_ID);
        }
        alarm->tone.local_id = 0U;
    }
    if (alarm->snooze.interval_min == 0U) {
        alarm->snooze.interval_min = DEFAULT_SNOOZE_INTERVAL_MIN;
    }
}

/**
 * @brief Parse one alarm object from protocol JSON.
 */
static int parse_alarm_item(const cJSON *json, qm_alarm_item_t *alarm)
{
    const cJSON *snooze;
    const cJSON *extensions;
    const cJSON *tone;
    const cJSON *tone_type;
    const cJSON *tone_id;
    const char *tone_type_value;
    const char *tone_id_value;

    if (alarm == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(alarm, 0, sizeof(*alarm));
    if (!cJSON_IsObject(json)) {
        return -QM_EINVAL;
    }

    if (parse_json_u8_field(json, "alarmId", 1U, MAX_ALARM_ID, &alarm->alarm_id) != QM_EOK) {
        return -QM_EINVAL;
    }
    copy_json_string(alarm->source,
                     sizeof(alarm->source),
                     json,
                     "source",
                     NULL);
    copy_json_string(alarm->name, sizeof(alarm->name), json, "name", "");
    alarm->hour = json_u8_value(json, "hour", 0, 23);
    alarm->minute = json_u8_value(json, "minute", 0, 59);
    alarm->enabled = json_bool_value(json, "enabled", QM_TRUE);
    alarm->repeat_mask = json_u8_value(json,
                                       "repeatMask",
                                       0,
                                       QM_ALARM_REPEAT_EVERYDAY);
    alarm->vibrate = json_bool_value(json, "vibrate", QM_FALSE);

    tone = cJSON_GetObjectItem(json, "tone");
    if (!cJSON_IsObject(tone)) {
        return -QM_EINVAL;
    }
    tone_type = cJSON_GetObjectItem(tone, "type");
    tone_id = cJSON_GetObjectItem(tone, "id");
    if (!cJSON_IsString(tone_type) || !cJSON_IsString(tone_id)) {
        return -QM_EINVAL;
    }
    tone_type_value = cJSON_GetStringValue(tone_type);
    tone_id_value = cJSON_GetStringValue(tone_id);
    if (tone_type_value == NULL || tone_id_value == NULL) {
        return -QM_EINVAL;
    }

    if (qm_strcmp(tone_type_value, TONE_TYPE_LOCAL_STR) == 0) {
        alarm->tone.type = QM_ALARM_TONE_TYPE_LOCAL;
        if (!parse_u8_text(tone_id_value, MAX_LOCAL_TONE_ID, &alarm->tone.local_id) ||
            alarm->tone.local_id == 0U) {
            return -QM_EINVAL;
        }
    } else if (qm_strcmp(tone_type_value, TONE_TYPE_ONLINE_STR) == 0) {
        alarm->tone.type = QM_ALARM_TONE_TYPE_ONLINE;
        qm_snprintf(alarm->tone.online_id,
                    sizeof(alarm->tone.online_id),
                    "%s",
                    tone_id_value);
    } else {
        return -QM_EINVAL;
    }

    snooze = cJSON_GetObjectItem(json, "snooze");
    if (cJSON_IsObject(snooze)) {
        alarm->snooze.enabled = json_bool_value(snooze, "enabled", QM_FALSE);
        alarm->snooze.interval_min = json_u8_value(snooze,
                                                   "intervalMin",
                                                   DEFAULT_SNOOZE_INTERVAL_MIN,
                                                   60);
        alarm->snooze.max_count = json_u8_value(snooze, "maxCount", 0, 10);
    }

    extensions = cJSON_GetObjectItem(json, "extensions");
    if (extensions != NULL) {
        char *extensions_text;
        char *extensions_copy;
        uint32_t extensions_len;

        if (!cJSON_IsObject(extensions)) {
            return -QM_EINVAL;
        }
        extensions_text = cJSON_PrintUnformatted((cJSON *)extensions);
        if (extensions_text == NULL) {
            return -QM_ENOMEM;
        }
        extensions_len = qm_strlen(extensions_text);
        extensions_copy = (char *)qm_malloc(extensions_len + 1U);
        if (extensions_copy == NULL) {
            cJSON_free(extensions_text);
            return -QM_ENOMEM;
        }
        qm_snprintf(extensions_copy,
                    extensions_len + 1U,
                    "%s",
                    extensions_text);
        alarm->extensions = extensions_copy;
        cJSON_free(extensions_text);
    }

    normalize_alarm(alarm);
    {
        int ret = validate_alarm(alarm);

        if (ret != QM_EOK) {
            clear_alarm_extensions(alarm);
        }
        return ret;
    }
}

/**
 * @brief Parse only alarmId from a command item.
 */
static int parse_alarm_key(const cJSON *json,
                           char *source,
                           uint32_t source_len,
                           uint8_t *alarm_id)
{
    if (!cJSON_IsObject(json) || source == NULL || source_len == 0U || alarm_id == NULL) {
        return -QM_EINVAL;
    }

    copy_json_string(source, source_len, json, "source", NULL);
    if (!is_valid_source(source)) {
        return -QM_EINVAL;
    }
    return parse_json_u8_field(json, "alarmId", 1U, MAX_ALARM_ID, alarm_id);
}

/**
 * @brief Parse alarmId and enabled fields from an enable command item.
 */
static int parse_alarm_enable(const cJSON *json,
                              char *source,
                              uint32_t source_len,
                              uint8_t *alarm_id,
                              qm_bool *enabled)
{
    const cJSON *enabled_item;
    int ret;

    if (enabled == NULL) {
        return -QM_EINVAL;
    }

    ret = parse_alarm_key(json, source, source_len, alarm_id);
    if (ret != QM_EOK) {
        return ret;
    }

    enabled_item = cJSON_GetObjectItem(json, "enabled");
    if (!cJSON_IsBool(enabled_item)) {
        return -QM_EINVAL;
    }

    *enabled = cJSON_IsTrue(enabled_item) ? QM_TRUE : QM_FALSE;
    return QM_EOK;
}

/**
 * @brief Convert one alarm item to cJSON.
 */
static cJSON *alarm_item_to_json(const qm_alarm_item_t *alarm)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *snooze;
    cJSON *tone;
    char local_tone_id[4];

    if (root == NULL || alarm == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    cJSON_AddNumberToObject(root, "alarmId", alarm->alarm_id);
    cJSON_AddStringToObject(root, "source", alarm->source);
    cJSON_AddStringToObject(root, "name", alarm->name);
    cJSON_AddNumberToObject(root, "hour", alarm->hour);
    cJSON_AddNumberToObject(root, "minute", alarm->minute);
    cJSON_AddBoolToObject(root, "enabled", alarm->enabled);
    cJSON_AddNumberToObject(root, "repeatMask", alarm->repeat_mask);
    tone = cJSON_CreateObject();
    if (tone == NULL) {
        cJSON_Delete(root);
        return NULL;
    }
    cJSON_AddStringToObject(tone, "type", tone_type_str(alarm->tone.type));
    if (alarm->tone.type == QM_ALARM_TONE_TYPE_LOCAL) {
        qm_snprintf(local_tone_id,
                    sizeof(local_tone_id),
                    "%u",
                    (unsigned int)alarm->tone.local_id);
        cJSON_AddStringToObject(tone, "id", local_tone_id);
    } else {
        cJSON_AddStringToObject(tone, "id", alarm->tone.online_id);
    }
    cJSON_AddItemToObject(root, "tone", tone);
    cJSON_AddBoolToObject(root, "vibrate", alarm->vibrate);

    snooze = cJSON_CreateObject();
    if (snooze == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(snooze);
        return NULL;
    }

    cJSON_AddBoolToObject(snooze, "enabled", alarm->snooze.enabled);
    cJSON_AddNumberToObject(snooze, "intervalMin", alarm->snooze.interval_min);
    cJSON_AddNumberToObject(snooze, "maxCount", alarm->snooze.max_count);
    cJSON_AddItemToObject(root, "snooze", snooze);

    if (alarm->extensions != NULL &&
        alarm->extensions[0] != '\0' &&
        cJSON_AddRawToObject(root,
                             "extensions",
                             alarm->extensions) == NULL) {
        cJSON_Delete(root);
        return NULL;
    }

    return root;
}

/**
 * @brief Serialize the complete alarm list as the current in-memory order.
 */
static char *build_alarm_list_json(alarm_ctx_t *ctx)
{
    cJSON *array;
    char *json;
    uint16_t i;

    if (ctx == NULL) {
        return NULL;
    }

    array = cJSON_CreateArray();
    if (array == NULL) {
        return NULL;
    }

    for (i = 0; i < ctx->count; i++) {
        cJSON *item = alarm_item_to_json(&ctx->alarms[i]);

        if (item == NULL) {
            cJSON_Delete(array);
            return NULL;
        }
        cJSON_AddItemToArray(array, item);
    }

    json = cJSON_PrintUnformatted(array);
    cJSON_Delete(array);
    return json;
}

/**
 * @brief Build one borrowed-free payload string for the current full alarm list.
 */
static int build_alarm_list_payload(alarm_ctx_t *ctx,
                                    char **payload,
                                    uint32_t *payload_len)
{
    if (ctx == NULL || payload == NULL || payload_len == NULL) {
        return -QM_EINVAL;
    }

    *payload = build_alarm_list_json(ctx);
    if (*payload == NULL) {
        *payload_len = 0U;
        return -QM_ENOMEM;
    }

    *payload_len = (uint32_t)qm_strlen(*payload);
    return QM_EOK;
}

/**
 * @brief Assign one local-create identity for device-side alarms.
 *
 * Reuse the smallest available device alarm id first, then append after the
 * highest seen id.
 */
static int assign_local_create_identity(alarm_ctx_t *ctx,
                                        qm_alarm_item_t *alarm)
{
    uint16_t candidate;
    uint16_t upper_bound;

    if (ctx == NULL || alarm == NULL) {
        return -QM_EINVAL;
    }

    if (alarm->source[0] == '\0') {
        qm_snprintf(alarm->source, sizeof(alarm->source), "%s", QM_ALARM_SOURCE_DEVICE);
    }
    if (!is_device_source(alarm->source)) {
        return QM_EOK;
    }

    if (alarm->alarm_id != QM_ALARM_LOCAL_ID_AUTO) {
        refresh_next_local_alarm_id(ctx, alarm);
        return QM_EOK;
    }

    upper_bound = (ctx->next_local_alarm_id == 0U) ? 1U : ctx->next_local_alarm_id;

    for (candidate = 1U; candidate < upper_bound; candidate++) {
        if (find_alarm(ctx, QM_ALARM_SOURCE_DEVICE, (uint8_t)candidate) < 0) {
            alarm->alarm_id = (uint8_t)candidate;
            if (candidate >= ctx->next_local_alarm_id) {
                ctx->next_local_alarm_id = (uint16_t)(candidate + 1U);
            }
            QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CONTROL,
                    "assign local alarm identity alarm_id=%u source=%s next_local_alarm_id=%u",
                    (unsigned int)alarm->alarm_id,
                    alarm->source,
                    (unsigned int)ctx->next_local_alarm_id);
            return QM_EOK;
        }
    }

    if (upper_bound <= MAX_ALARM_ID) {
        if (find_alarm(ctx, QM_ALARM_SOURCE_DEVICE, (uint8_t)upper_bound) < 0) {
            alarm->alarm_id = (uint8_t)upper_bound;
            ctx->next_local_alarm_id = (uint16_t)(upper_bound + 1U);
            QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CONTROL,
                    "assign local alarm identity alarm_id=%u source=%s next_local_alarm_id=%u",
                    (unsigned int)alarm->alarm_id,
                    alarm->source,
                    (unsigned int)ctx->next_local_alarm_id);
            return QM_EOK;
        }
    }

    QM_ALARM_LOGE(LOG_TAG, "assign local alarm identity exhausted");
    return -QM_EFULL;
}

/**
 * @brief Convert QUMI weekday to alarm repeat-mask bit.
 */
static uint8_t weekday_to_mask(int wday)
{
    if (wday == 0) {
        return QM_ALARM_REPEAT_SUN;
    }
    return (uint8_t)(1U << (uint8_t)(wday - 1));
}

/**
 * @brief Compute the next fire timestamp for an alarm.
 */
static qm_time_t compute_next_fire(const qm_alarm_item_t *alarm,
                                   qm_time_t now)
{
    struct qm_tm tm;
    uint8_t day;

    if (alarm == NULL || !alarm->enabled || qm_localtime_r(&now, &tm) == NULL) {
        return 0;
    }

    for (day = 0; day < 8U; day++) {
        struct qm_tm candidate = tm;
        qm_time_t ts;

        candidate.tm_sec = 0;
        candidate.tm_hour = alarm->hour;
        candidate.tm_min = alarm->minute;
        candidate.tm_mday += day;
        ts = local_alarm_time_to_timestamp(&candidate);
        if (ts <= now) {
            continue;
        }

        if (alarm->repeat_mask == 0U) {
            /* A one-shot alarm has no date field. If today's time has
             * already passed, its single firing is scheduled for tomorrow. */
            return ts;
        }

        if ((alarm->repeat_mask & weekday_to_mask((tm.tm_wday + day) % 7)) != 0U) {
            return ts;
        }
    }

    return 0;
}

/**
 * @brief Disable later one-shot alarms that share the same effective fire time.
 */
static qm_bool disable_same_time_one_shot_peers(alarm_ctx_t *ctx,
                                                int32_t active_index,
                                                qm_time_t fire_time)
{
    const qm_alarm_item_t *active_alarm;
    qm_time_t reference_time;
    qm_bool changed = QM_FALSE;
    uint16_t i;

    if (ctx == NULL || active_index < 0 ||
        (uint16_t)active_index >= ctx->count || fire_time == 0U) {
        return QM_FALSE;
    }

    active_alarm = &ctx->alarms[active_index];
    reference_time = fire_time - 1U;
    for (i = (uint16_t)active_index + 1U; i < ctx->count; i++) {
        qm_alarm_item_t *candidate = &ctx->alarms[i];

        if (candidate->hour != active_alarm->hour ||
            candidate->minute != active_alarm->minute) {
            break;
        }

        if (candidate->repeat_mask == 0U &&
            candidate->enabled &&
            compute_next_fire(candidate, reference_time) == fire_time) {
            candidate->enabled = QM_FALSE;
            mark_alarm_index_dirty(ctx, (int32_t)i);
            changed = QM_TRUE;
            log_alarm_summary("skip same-time one-shot alarm", candidate, (int32_t)i);
        }
    }

    return changed;
}

/**
 * @brief Collect later one-shot alarms that share the same effective fire time.
 */
static uint16_t collect_same_time_one_shot_peer_indices(const alarm_ctx_t *ctx,
                                                        int32_t active_index,
                                                        qm_time_t fire_time,
                                                        int32_t *indices,
                                                        uint16_t capacity)
{
    const qm_alarm_item_t *active_alarm;
    qm_time_t reference_time;
    uint16_t count = 0U;
    uint16_t i;

    if (ctx == NULL || active_index < 0 ||
        (uint16_t)active_index >= ctx->count || fire_time == 0U) {
        return 0U;
    }

    active_alarm = &ctx->alarms[active_index];
    reference_time = fire_time - 1U;
    for (i = (uint16_t)active_index + 1U; i < ctx->count; i++) {
        qm_alarm_item_t *candidate = &ctx->alarms[i];

        if (candidate->hour != active_alarm->hour ||
            candidate->minute != active_alarm->minute) {
            break;
        }

        if (candidate->repeat_mask == 0U &&
            candidate->enabled &&
            compute_next_fire(candidate, reference_time) == fire_time) {
            if (indices != NULL && count < capacity) {
                indices[count] = (int32_t)i;
                count++;
            }
        }
    }

    return count;
}

/**
 * @brief Find the nearest enabled alarm without changing runtime state.
 */
static int compute_best_alarm(const alarm_ctx_t *ctx,
                              qm_time_t now,
                              int32_t *best_index,
                              qm_time_t *best_fire_time)
{
    qm_time_t best = 0;
    int32_t index = -1;
    uint16_t i;

    if (ctx == NULL || best_index == NULL || best_fire_time == NULL) {
        return -QM_EINVAL;
    }

    for (i = 0; i < ctx->count; i++) {
        qm_time_t fire = compute_next_fire(&ctx->alarms[i], now);

        if (fire != 0 && (best == 0 || fire < best)) {
            best = fire;
            index = (int32_t)i;
        }
    }

    *best_index = index;
    *best_fire_time = best;
    return (index >= 0 && best != 0U) ? QM_EOK : -QM_ENOENT;
}

/**
 * @brief Query the next effective alarm without changing timer state.
 */
static int query_next_alarm(const alarm_ctx_t *ctx,
                            int32_t *next_index,
                            qm_time_t *next_fire_time)
{
    if (ctx == NULL || next_index == NULL || next_fire_time == NULL) {
        return -QM_EINVAL;
    }

    return select_next_effective_alarm(ctx, qm_time(NULL), next_index, next_fire_time);
}

/**
 * @brief Select the earliest effective target between normal alarms and a pending snooze.
 */
static int select_next_effective_alarm(const alarm_ctx_t *ctx,
                                       qm_time_t now,
                                       int32_t *next_index,
                                       qm_time_t *next_fire_time)
{
    int32_t regular_index = -1;
    qm_time_t regular_fire_time = 0U;
    int ret;

    if (ctx == NULL || next_index == NULL || next_fire_time == NULL) {
        return -QM_EINVAL;
    }

    ret = compute_best_alarm(ctx, now, &regular_index, &regular_fire_time);
    if (has_pending_snooze(ctx) &&
        ctx->snooze_fire_time > now &&
        (ret != QM_EOK ||
         regular_fire_time == 0U ||
         ctx->snooze_fire_time < regular_fire_time)) {
        *next_index = ctx->snooze_alarm_index;
        *next_fire_time = ctx->snooze_fire_time;
        return QM_EOK;
    }

    if (ret != QM_EOK) {
        return ret;
    }

    *next_index = regular_index;
    *next_fire_time = regular_fire_time;
    return QM_EOK;
}

/**
 * @brief Fire a pending alarm whose timer callback was delayed or dropped.
 *
 * The regular next-fire calculation intentionally ignores past one-shot
 * timestamps. Keep the already selected target alive long enough to recover
 * it before recalculating the schedule.
 */
static qm_bool fire_overdue_scheduled_alarm(alarm_ctx_t *ctx, qm_time_t now)
{
    if (ctx == NULL || ctx->next_alarm_index < 0 ||
        (uint16_t)ctx->next_alarm_index >= ctx->count ||
        ctx->next_fire_time == 0U || ctx->next_fire_time > now ||
        !ctx->alarms[ctx->next_alarm_index].enabled) {
        return QM_FALSE;
    }

    QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
            "recover overdue alarm index=%ld scheduled=%llu now=%llu",
            (long)ctx->next_alarm_index,
            (unsigned long long)ctx->next_fire_time,
            (unsigned long long)now);
    alarm_timer_fire_internal(ctx);
    return QM_TRUE;
}

/**
 * @brief Report the full list when cloud is available, otherwise mark dirty.
 */
static int report_or_mark_dirty(alarm_ctx_t *ctx)
{
    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    if (!ctx->cloud_ready) {
        ctx->dirty = QM_TRUE;
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD, "cloud not ready, mark alarm list dirty");
        return QM_EOK;
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD, "cloud ready, report alarm list");
    return handle_update(ctx);
}

/**
 * @brief Reschedule the nearest enabled alarm.
 */
static int schedule_next(alarm_ctx_t *ctx)
{
    qm_time_t now;
    qm_time_t best = 0;
    char best_time_text[24];
    char now_time_text[24];
    int32_t best_index = -1;
    qm_bool best_is_snooze = QM_FALSE;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    if (ctx->snooze_pending && !has_pending_snooze(ctx)) {
        clear_pending_snooze(ctx, QM_TRUE);
    }

    now = qm_time(NULL);
    if (fire_overdue_scheduled_alarm(ctx, now)) {
        return QM_EOK;
    }
    if (select_next_effective_alarm(ctx, now, &best_index, &best) != QM_EOK) {
        best = 0;
        best_index = -1;
    }
    best_is_snooze = has_pending_snooze(ctx) &&
                     ctx->snooze_alarm_index == best_index &&
                     ctx->snooze_fire_time == best;

    cancel_scheduled_alarm(ctx);

    ctx->next_alarm_index = best_index;
    ctx->next_fire_time = best;
    if (best_index >= 0 && (uint16_t)best_index < ctx->count) {
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                "schedule next best_index=%ld alarm_time=%02u:%02u next_fire_time=%s now=%s mode=%s",
                (long)best_index,
                (unsigned int)ctx->alarms[best_index].hour,
                (unsigned int)ctx->alarms[best_index].minute,
                alarm_time_str(best, best_time_text, sizeof(best_time_text)),
                alarm_time_str(now, now_time_text, sizeof(now_time_text)),
                best_is_snooze ? "snooze" : "regular");
    } else {
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                "schedule next best_index=%ld next_fire_time=%s now=%s",
                (long)best_index,
                alarm_time_str(best, best_time_text, sizeof(best_time_text)),
                alarm_time_str(now, now_time_text, sizeof(now_time_text)));
    }

    if (best_index < 0 || best <= now) {
        return QM_EOK;
    }

    return schedule_alarm_after_ms(ctx, (uint32_t)((best - now) * 1000ULL));
}

static int handle_update(alarm_ctx_t *ctx);

/**
 * @brief Return a short log label for the local/cloud create-modify path.
 */
static const char *alarm_apply_mode_name(alarm_apply_mode_t mode)
{
    switch (mode) {
        case ALARM_APPLY_CREATE:
            return SUBMETHOD_CREATE;
        case ALARM_APPLY_MODIFY:
            return SUBMETHOD_MODIFY;
        default:
            return "unknown";
    }
}

/**
 * @brief Return a short log label for the cloud batch command path.
 */
static const char *alarm_cloud_action_name(alarm_cloud_action_t action)
{
    switch (action) {
        case ALARM_CLOUD_ACTION_CREATE:
            return SUBMETHOD_CREATE;
        case ALARM_CLOUD_ACTION_MODIFY:
            return SUBMETHOD_MODIFY;
        case ALARM_CLOUD_ACTION_DELETE:
            return SUBMETHOD_DELETE;
        case ALARM_CLOUD_ACTION_ENABLE:
            return SUBMETHOD_ENABLE;
        default:
            return "unknown";
    }
}

/**
 * @brief Schedule one alarm dispatch through the QUMI work queue.
 */
static int schedule_alarm_after_ms(alarm_ctx_t *ctx, uint32_t delay_ms)
{
    int ret;

    if (ctx == NULL || ctx->schedule_work == NULL) {
        return -QM_EINVAL;
    }

    if (ctx->timer_token == NULL) {
        return -QM_EINVAL;
    }

    ret = qm_post_delayed_action(ctx->schedule_work,
                                 alarm_timer_cb,
                                 ctx->timer_token,
                                 delay_ms);
    if (ret == QM_EOK) {
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                      "alarm work scheduled delay_ms=%u next_index=%ld next_fire=%llu",
                      (unsigned int)delay_ms,
                      (long)ctx->next_alarm_index,
                      (unsigned long long)ctx->next_fire_time);
    } else {
        QM_ALARM_LOGE(LOG_TAG,
                      "schedule alarm work failed delay_ms=%u ret=%d",
                      (unsigned int)delay_ms,
                      ret);
    }
    return ret;
}

/**
 * @brief Cancel one pending delayed alarm dispatch.
 */
static void cancel_scheduled_alarm(alarm_ctx_t *ctx)
{
    if (ctx == NULL || ctx->schedule_work == NULL) {
        return;
    }

    (void)qm_cancel_delayed_action(ctx->schedule_work);
}

/**
 * @brief Apply one create or modify command to the in-memory alarm list.
 */
static int apply_alarm(alarm_ctx_t *ctx,
                       const qm_alarm_item_t *alarm,
                       alarm_apply_mode_t mode)
{
    int32_t index;
    int ret;
    uint16_t slot = INVALID_SLOT_ID;
    qm_alarm_item_t normalized;

    if (ctx == NULL || alarm == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(&normalized, 0, sizeof(normalized));
    ret = copy_alarm_owned(&normalized, alarm);
    if (ret != QM_EOK) {
        QM_ALARM_LOGE(LOG_TAG,
                "copy alarm for %s failed ret=%d",
                alarm_apply_mode_name(mode),
                ret);
        return ret;
    }
    normalize_alarm(&normalized);
    ret = validate_alarm(&normalized);
    if (ret != QM_EOK) {
        clear_alarm_extensions(&normalized);
        QM_ALARM_LOGE(LOG_TAG,
                "validate alarm for %s failed ret=%d",
                alarm_apply_mode_name(mode),
                ret);
        return ret;
    }

    index = find_alarm(ctx, normalized.source, normalized.alarm_id);
    if (mode == ALARM_APPLY_MODIFY && index < 0) {
        clear_alarm_extensions(&normalized);
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_CONTROL,
                "modify alarm not found source=%s alarm_id=%u",
                alarm->source,
                (unsigned int)alarm->alarm_id);
        return -QM_ENOENT;
    }

    if (index >= 0) {
        slot = alarm_slot(ctx, (uint16_t)index);
        move_alarm_owned(&ctx->alarms[index], &normalized, QM_TRUE);
        sort_alarm_entries(ctx);
        mark_alarm_slot_dirty(ctx, slot);
        index = find_alarm(ctx, normalized.source, normalized.alarm_id);
        if (index >= 0) {
            refresh_next_local_alarm_id(ctx, &ctx->alarms[index]);
            log_alarm_summary((mode == ALARM_APPLY_CREATE) ?
                              "apply alarm create" :
                              "apply alarm modify",
                              &ctx->alarms[index],
                              index);
        }
        return QM_EOK;
    }

    if (ctx->count >= ctx->capacity) {
        clear_alarm_extensions(&normalized);
        QM_ALARM_LOGE(LOG_TAG,
                "alarm capacity full count=%u capacity=%u",
                (unsigned int)ctx->count,
                (unsigned int)ctx->capacity);
        return -QM_EFULL;
    }

    ret = allocate_alarm_slot(ctx, &slot);
    if (ret != QM_EOK) {
        clear_alarm_extensions(&normalized);
        QM_ALARM_LOGE(LOG_TAG, "allocate alarm slot failed ret=%d", ret);
        return ret;
    }

    move_alarm_owned(&ctx->alarms[ctx->count], &normalized, QM_FALSE);
    ctx->alarm_slots[ctx->count] = slot;
    alarm_bitmap_set_slot(ctx->used_bitmap, ctx->capacity, slot, QM_TRUE);
    ctx->bitmap_dirty = QM_TRUE;
    ctx->count++;
    sort_alarm_entries(ctx);
    mark_alarm_slot_dirty(ctx, slot);
    index = find_alarm(ctx, alarm->source, alarm->alarm_id);
    if (index < 0) {
        index = find_alarm(ctx, normalized.source, normalized.alarm_id);
    }
    if (index >= 0) {
        refresh_next_local_alarm_id(ctx, &ctx->alarms[index]);
        log_alarm_summary("apply alarm create", &ctx->alarms[index], index);
    }
    return QM_EOK;
}

/**
 * @brief Persist the in-memory alarm list and refresh runtime scheduling.
 */
static int finalize_alarm_change(alarm_ctx_t *ctx, const char *stage)
{
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    ret = save_alarms(ctx);
    if (ret != QM_EOK) {
        return ret;
    }

    (void)schedule_next(ctx);
    if (stage != NULL) {
        log_alarm_list_summary(stage, ctx);
    }
    return QM_EOK;
}

/**
 * @brief Delete one alarm by id.
 */
static int remove_alarm(alarm_ctx_t *ctx, const char *source, uint8_t alarm_id)
{
    int32_t index;
    uint16_t removed_slot;

    if (ctx == NULL || source == NULL || alarm_id == QM_ALARM_LOCAL_ID_AUTO) {
        return -QM_EINVAL;
    }

    index = find_alarm(ctx, source, alarm_id);
    if (index < 0) {
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_CONTROL,
                "remove alarm not found source=%s alarm_id=%u",
                source,
                (unsigned int)alarm_id);
        return -QM_ENOENT;
    }

    removed_slot = alarm_slot(ctx, (uint16_t)index);
    log_alarm_summary("remove alarm", &ctx->alarms[index], index);
    clear_alarm_extensions(&ctx->alarms[index]);
    if ((uint16_t)index + 1U < ctx->count) {
        qm_memmove(&ctx->alarms[index],
                   &ctx->alarms[index + 1],
                   (ctx->count - (uint16_t)index - 1U) * sizeof(ctx->alarms[0]));
        qm_memmove(&ctx->alarm_slots[index],
                   &ctx->alarm_slots[index + 1],
                   (ctx->count - (uint16_t)index - 1U) * sizeof(ctx->alarm_slots[0]));
    }
    ctx->count--;
    qm_memset(&ctx->alarms[ctx->count], 0, sizeof(ctx->alarms[ctx->count]));
    ctx->alarm_slots[ctx->count] = INVALID_SLOT_ID;
    if (removed_slot != INVALID_SLOT_ID && removed_slot < ctx->capacity) {
        alarm_bitmap_set_slot(ctx->used_bitmap, ctx->capacity, removed_slot, QM_FALSE);
        mark_alarm_slot_dirty(ctx, removed_slot);
    }
    ctx->bitmap_dirty = QM_TRUE;
    return QM_EOK;
}

/**
 * @brief Send cloud ACK for an alarm command.
 */
static int next_msg_id(alarm_ctx_t *ctx)
{
    if (ctx == NULL) {
        return 0;
    }

    ctx->next_msg_id++;
    if (ctx->next_msg_id == 0U) {
        ctx->next_msg_id = 1U;
    }

    return (int)ctx->next_msg_id;
}

static uint32_t json_uint_value(const cJSON *item)
{
    if (cJSON_IsNumber(item) && item->valuedouble >= 0) {
        return (uint32_t)item->valuedouble;
    }

    if (cJSON_IsString(item) && item->valuestring != NULL) {
        uint32_t value = 0;
        const char *p = item->valuestring;

        while (*p >= '0' && *p <= '9') {
            value = value * 10U + (uint32_t)(*p - '0');
            p++;
        }
        return value;
    }

    return 0U;
}

static int reply_ack(alarm_ctx_t *ctx,
                     const alarm_cloud_req_t *req,
                     const char *ack_submethod,
                     int code)
{
    char *payload;
    uint32_t payload_len;
    int ret;

    if (ctx == NULL || req == NULL) {
        return -QM_EINVAL;
    }

    ret = build_alarm_list_payload(ctx, &payload, &payload_len);
    if (ret != QM_EOK) {
        QM_ALARM_LOGE(LOG_TAG,
                "build ack payload failed submethod=%s req_id=%u code=%d ret=%d",
                ack_submethod != NULL ? ack_submethod : "",
                (unsigned int)req->msg_id,
                code,
                ret);
        return ret;
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
            "reply ack submethod=%s req_id=%u code=%d alarm_count=%u payload_len=%u",
            ack_submethod != NULL ? ack_submethod : "",
            (unsigned int)req->msg_id,
            code,
            (unsigned int)ctx->count,
            (unsigned int)payload_len);
    {
        qm_iot_down_req_t reply_req = {
            .method = REPLY_METHOD,
            .submethod = ack_submethod,
            .msg_id = req->msg_id,
        };

        ret = qm_iot_down_reply(ctx->config.iot_handle,
                                &reply_req,
                                code,
                                payload,
                                (int)payload_len);
    }
    if (ret == QM_EOK) {
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
                "reply ack publish ok submethod=%s req_id=%u code=%d",
                ack_submethod != NULL ? ack_submethod : "",
                (unsigned int)req->msg_id,
                code);
    } else {
        QM_ALARM_LOGE(LOG_TAG,
                "reply ack publish failed submethod=%s req_id=%u ret=%d",
                ack_submethod != NULL ? ack_submethod : "",
                (unsigned int)req->msg_id,
                ret);
    }
    cJSON_free(payload);
    return ret;
}

/**
 * @brief Send cloud ACK without the full-list payload.
 */
static int reply_simple_ack(alarm_ctx_t *ctx,
                            const alarm_cloud_req_t *req,
                            const char *ack_submethod,
                            int code,
                            const char *params)
{
    int params_len = 0;

    if (ctx == NULL || req == NULL || ack_submethod == NULL) {
        return -QM_EINVAL;
    }

    if (params != NULL) {
        params_len = (int)qm_strlen(params);
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
            "reply simple ack submethod=%s req_id=%u code=%d params_len=%d",
            ack_submethod,
            (unsigned int)req->msg_id,
            code,
            params_len);
    {
        qm_iot_down_req_t reply_req = {
            .method = REPLY_METHOD,
            .submethod = ack_submethod,
            .msg_id = req->msg_id,
        };

        params_len = qm_iot_down_reply(ctx->config.iot_handle,
                                       &reply_req,
                                       code,
                                       (char *)params,
                                       params_len);
    }
    if (params_len == QM_EOK) {
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
                "reply simple ack publish ok submethod=%s req_id=%u code=%d",
                ack_submethod,
                (unsigned int)req->msg_id,
                code);
    } else {
        QM_ALARM_LOGE(LOG_TAG,
                "reply simple ack publish failed submethod=%s req_id=%u ret=%d",
                ack_submethod,
                (unsigned int)req->msg_id,
                params_len);
    }
    return params_len;
}

/**
 * @brief Handle one cloud alarm batch command.
 */
static int handle_cloud_command(alarm_ctx_t *ctx,
                                const alarm_cloud_req_t *req,
                                const char *ack_submethod,
                                alarm_cloud_action_t action)
{
    cJSON *root;
    cJSON *item;
    qm_alarm_item_t *notify_items = NULL;
    uint16_t notify_capacity = 0U;
    uint16_t notify_count = 0U;
    int ret = QM_EOK;

    if (ctx == NULL || req == NULL || req->params == NULL) {
        return reply_ack(ctx, req, ack_submethod, -QM_EINVAL);
    }
    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
            "handle cloud command action=%s params_len=%u",
            alarm_cloud_action_name(action),
            (unsigned int)req->params_len);

    root = cJSON_ParseWithLength(req->params, req->params_len);
    if (!cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return reply_ack(ctx, req, ack_submethod, -QM_EINVAL);
    }
    notify_capacity = (uint16_t)cJSON_GetArraySize(root);
    if (notify_capacity > 0U) {
        notify_items = (qm_alarm_item_t *)qm_malloc(sizeof(qm_alarm_item_t) * notify_capacity);
        if (notify_items == NULL) {
            cJSON_Delete(root);
            return reply_ack(ctx, req, ack_submethod, -QM_ENOMEM);
        }
        qm_memset(notify_items, 0, sizeof(qm_alarm_item_t) * notify_capacity);
    }

    cJSON_ArrayForEach(item, root) {
        qm_alarm_item_t alarm;

        qm_memset(&alarm, 0, sizeof(alarm));
        log_json_text("cloud command item", item);
        switch (action) {
            case ALARM_CLOUD_ACTION_CREATE:
                ret = parse_alarm_item(item, &alarm);
                if (ret == QM_EOK) {
                    ret = apply_alarm(ctx, &alarm, ALARM_APPLY_CREATE);
                    if (ret == QM_EOK) {
                        int32_t changed_index = find_alarm(ctx, alarm.source, alarm.alarm_id);

                        if (changed_index >= 0 && notify_count < notify_capacity) {
                            ret = copy_alarm_owned(&notify_items[notify_count],
                                                   &ctx->alarms[changed_index]);
                            if (ret == QM_EOK) {
                                notify_count++;
                            }
                        }
                    }
                }
                clear_alarm_extensions(&alarm);
                break;
            case ALARM_CLOUD_ACTION_MODIFY:
                ret = parse_alarm_item(item, &alarm);
                if (ret == QM_EOK) {
                    ret = apply_alarm(ctx, &alarm, ALARM_APPLY_MODIFY);
                    if (ret == QM_EOK) {
                        int32_t changed_index = find_alarm(ctx, alarm.source, alarm.alarm_id);

                        if (changed_index >= 0 && notify_count < notify_capacity) {
                            ret = copy_alarm_owned(&notify_items[notify_count],
                                                   &ctx->alarms[changed_index]);
                            if (ret == QM_EOK) {
                                notify_count++;
                            }
                        }
                    }
                }
                clear_alarm_extensions(&alarm);
                break;
            case ALARM_CLOUD_ACTION_DELETE: {
                char source[QM_ALARM_SOURCE_MAX_LEN];
                uint8_t alarm_id = 0U;
                qm_bool delete_snapshot_ready = QM_FALSE;

                ret = parse_alarm_key(item, source, sizeof(source), &alarm_id);
                if (ret == QM_EOK) {
                    int32_t changed_index = find_alarm(ctx, source, alarm_id);

                    if (changed_index >= 0 && notify_count < notify_capacity) {
                        ret = copy_alarm_owned(&notify_items[notify_count],
                                               &ctx->alarms[changed_index]);
                        if (ret == QM_EOK) {
                            delete_snapshot_ready = QM_TRUE;
                        }
                    }
                }
                if (ret == QM_EOK) {
                    ret = remove_alarm(ctx, source, alarm_id);
                    if (ret == QM_EOK && delete_snapshot_ready) {
                        notify_count++;
                    } else if (delete_snapshot_ready) {
                        clear_alarm_extensions(&notify_items[notify_count]);
                    }
                }
                break;
            }
            case ALARM_CLOUD_ACTION_ENABLE: {
                char source[QM_ALARM_SOURCE_MAX_LEN];
                uint8_t alarm_id = 0U;
                qm_bool enabled = QM_FALSE;
                int32_t index;

                ret = parse_alarm_enable(item,
                                         source,
                                         sizeof(source),
                                         &alarm_id,
                                         &enabled);
                if (ret == QM_EOK) {
                    index = find_alarm(ctx, source, alarm_id);
                    ret = (index >= 0) ? QM_EOK : -QM_ENOENT;
                    if (ret == QM_EOK) {
                        ctx->alarms[index].enabled = enabled;
                        mark_alarm_index_dirty(ctx, index);
                        if (notify_count < notify_capacity) {
                            ret = copy_alarm_owned(&notify_items[notify_count],
                                                   &ctx->alarms[index]);
                            if (ret == QM_EOK) {
                                notify_count++;
                            }
                        }
                    }
                }
                break;
            }
            default:
                ret = -QM_EINVAL;
                break;
        }

        if (ret != QM_EOK) {
            QM_ALARM_LOGE(LOG_TAG, "handle cloud command item failed ret=%d", ret);
            break;
        }
    }

    cJSON_Delete(root);
    if (ret == QM_EOK) {
        ret = finalize_alarm_change(ctx, "handle cloud command result");
        if (ret == QM_EOK) {
            uint16_t i;

            for (i = 0U; i < notify_count; i++) {
                notify_alarm(ctx, QM_ALARM_NOTIFY_CHANGED, &notify_items[i]);
            }
        }
    }
    if (notify_items != NULL) {
        uint16_t i;

        for (i = 0U; i < notify_capacity; i++) {
            clear_alarm_extensions(&notify_items[i]);
        }
        qm_free(notify_items);
    }
    return reply_ack(ctx, req, ack_submethod, ret == QM_EOK ? 0 : ret);
}

/**
 * @brief Apply one local create or modify request and report the new list.
 */
static int local_apply_alarm(alarm_ctx_t *ctx,
                             const qm_alarm_item_t *alarm,
                             alarm_apply_mode_t mode)
{
    qm_alarm_item_t prepared;
    int32_t existing_index = -1;
    int32_t result_index;
    int ret;

    if (ctx == NULL || alarm == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&prepared, 0, sizeof(prepared));
    qm_memcpy(&prepared, alarm, sizeof(prepared));
    if (mode == ALARM_APPLY_CREATE) {
        ret = assign_local_create_identity(ctx, &prepared);
        if (ret != QM_EOK) {
            return ret;
        }
    } else if (prepared.alarm_id != QM_ALARM_LOCAL_ID_AUTO) {
        existing_index = find_alarm(ctx, prepared.source, prepared.alarm_id);
        if (existing_index >= 0 && prepared.source[0] == '\0') {
            qm_snprintf(prepared.source,
                        sizeof(prepared.source),
                        "%s",
                        ctx->alarms[existing_index].source);
        }
    }
    log_alarm_summary((mode == ALARM_APPLY_CREATE) ?
                      "local create request" :
                      "local modify request",
                      &prepared,
                      -1);

    ret = apply_alarm(ctx, &prepared, mode);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = finalize_alarm_change(ctx,
                                (mode == ALARM_APPLY_CREATE) ?
                                "local create result" :
                                "local modify result");
    if (ret != QM_EOK) {
        return ret;
    }

    result_index = find_alarm(ctx, prepared.source, prepared.alarm_id);
    if (result_index >= 0) {
        notify_alarm(ctx, QM_ALARM_NOTIFY_CHANGED, &ctx->alarms[result_index]);
    }

    return report_or_mark_dirty(ctx);
}

/**
 * @brief Apply one local delete request and report the new list.
 */
static int local_delete_alarm(alarm_ctx_t *ctx, const char *source, uint8_t alarm_id)
{
    qm_alarm_item_t removed_alarm;
    qm_bool removed_alarm_copied = QM_FALSE;
    int ret;

    if (ctx == NULL || source == NULL) {
        return -QM_EINVAL;
    }
    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CONTROL,
            "local delete request source=%s alarm_id=%u",
            source,
            (unsigned int)alarm_id);
    qm_memset(&removed_alarm, 0, sizeof(removed_alarm));
    {
        int32_t index = find_alarm(ctx, source, alarm_id);

        if (index >= 0) {
            ret = copy_alarm_owned(&removed_alarm, &ctx->alarms[index]);
            if (ret != QM_EOK) {
                return ret;
            }
            removed_alarm_copied = QM_TRUE;
        }
    }

    ret = remove_alarm(ctx, source, alarm_id);
    if (ret != QM_EOK) {
        if (removed_alarm_copied) {
            clear_alarm_extensions(&removed_alarm);
        }
        return ret;
    }

    ret = finalize_alarm_change(ctx, "local delete result");
    if (ret != QM_EOK) {
        if (removed_alarm_copied) {
            clear_alarm_extensions(&removed_alarm);
        }
        return ret;
    }

    if (removed_alarm_copied) {
        notify_alarm(ctx, QM_ALARM_NOTIFY_CHANGED, &removed_alarm);
    }
    if (removed_alarm_copied) {
        clear_alarm_extensions(&removed_alarm);
    }

    return report_or_mark_dirty(ctx);
}

/**
 * @brief Apply one local enable request and report the new list.
 */
static int local_set_alarm_enabled(alarm_ctx_t *ctx,
                                   const char *source,
                                   uint8_t alarm_id,
                                   qm_bool enabled)
{
    int32_t index;
    int ret;

    if (ctx == NULL || source == NULL) {
        return -QM_EINVAL;
    }

    index = find_alarm(ctx, source, alarm_id);
    if (index < 0) {
        return -QM_ENOENT;
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CONTROL,
            "local enable request source=%s alarm_id=%u enabled=%u",
            source,
            (unsigned int)alarm_id,
            (unsigned int)enabled);
    ctx->alarms[index].enabled = enabled;
    mark_alarm_index_dirty(ctx, index);

    ret = finalize_alarm_change(ctx, "local enable result");
    if (ret != QM_EOK) {
        return ret;
    }

    log_alarm_summary("local enable result", &ctx->alarms[index], index);
    notify_alarm(ctx, QM_ALARM_NOTIFY_CHANGED, &ctx->alarms[index]);
    return report_or_mark_dirty(ctx);
}

/**
 * @brief Copy the in-memory alarm list into one caller-provided array.
 */
static int copy_alarm_list(const alarm_ctx_t *ctx,
                           qm_alarm_item_t *out,
                           uint16_t capacity,
                           uint16_t *count)
{
    uint16_t copy_count;

    if (ctx == NULL || count == NULL) {
        return -QM_EINVAL;
    }

    copy_count = (ctx->count < capacity) ? ctx->count : capacity;
    if (copy_count > 0U && out == NULL) {
        return -QM_EINVAL;
    }

    if (copy_count > 0U) {
        qm_memcpy(out, ctx->alarms, sizeof(qm_alarm_item_t) * copy_count);
    }
    *count = copy_count;
    return (copy_count == ctx->count) ? QM_EOK : -QM_EFULL;
}

/**
 * @brief Copy the next effective alarm and its trigger time.
 */
static int copy_next_alarm(const alarm_ctx_t *ctx,
                           qm_alarm_item_t *out,
                           qm_time_t *trigger_time)
{
    char trigger_time_text[24];
    int32_t next_index;
    int ret;

    if (ctx == NULL || out == NULL || trigger_time == NULL) {
        return -QM_EINVAL;
    }

    ret = query_next_alarm(ctx, &next_index, trigger_time);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_memcpy(out, &ctx->alarms[next_index], sizeof(*out));
    log_alarm_summary("get next alarm", out, next_index);
    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
            "get next alarm ok index=%ld trigger_time=%s",
            (long)next_index,
            alarm_time_str(*trigger_time, trigger_time_text, sizeof(trigger_time_text)));
    return QM_EOK;
}

/**
 * @brief Copy the most recently changed alarm snapshot.
 */
/**
 * @brief Apply one local create request.
 */
static int handle_create_local(alarm_ctx_t *ctx, const qm_alarm_item_t *alarm)
{
    return local_apply_alarm(ctx, alarm, ALARM_APPLY_CREATE);
}

/**
 * @brief Apply one local modify request.
 */
static int handle_modify_local(alarm_ctx_t *ctx, const qm_alarm_item_t *alarm)
{
    return local_apply_alarm(ctx, alarm, ALARM_APPLY_MODIFY);
}

/**
 * @brief Apply one local delete request.
 */
static int handle_delete_local(alarm_ctx_t *ctx, const char *source, uint8_t alarm_id)
{
    return local_delete_alarm(ctx, source, alarm_id);
}

/**
 * @brief Apply one local enable-state update.
 */
static int handle_enable_local(alarm_ctx_t *ctx,
                               const char *source,
                               uint8_t alarm_id,
                               qm_bool enabled)
{
    return local_set_alarm_enabled(ctx, source, alarm_id, enabled);
}

/**
 * @brief Full-list update report.
 */
static int handle_update(alarm_ctx_t *ctx)
{
    char *payload;
    uint32_t payload_len;
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    /* Keep pending sync on serialization or transport failure. */
    ctx->dirty = QM_TRUE;
    ret = build_alarm_list_payload(ctx, &payload, &payload_len);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_iot_up_report_ex(ctx->config.iot_handle,
                              (char *)DISPATCH_METHOD,
                              (char *)SUBMETHOD_UPDATE,
                              (uint32_t)next_msg_id(ctx),
                              payload,
                              (int)payload_len);
    if (ret == QM_EOK) {
        ctx->dirty = QM_FALSE;
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
                "report alarm list ok alarm_count=%u payload_len=%u",
                (unsigned int)ctx->count,
                (unsigned int)payload_len);
    } else {
        QM_ALARM_LOGE(LOG_TAG, "report alarm list failed ret=%d", ret);
    }

    cJSON_free(payload);
    return ret;
}

/**
 * @brief Get the currently active alarm item.
 */
static int get_active_alarm(alarm_ctx_t *ctx, qm_alarm_item_t **alarm_out)
{
    if (ctx == NULL || alarm_out == NULL || ctx->active_alarm_index < 0 ||
        (uint16_t)ctx->active_alarm_index >= ctx->count) {
        return -QM_EBUSY;
    }

    *alarm_out = &ctx->alarms[ctx->active_alarm_index];
    return QM_EOK;
}

/**
 * @brief Snooze the currently active alarm.
 */
static int handle_snooze(alarm_ctx_t *ctx, qm_bool report_now)
{
    qm_alarm_item_t *alarm;
    int32_t snooze_index;
    int ret;

    ret = get_active_alarm(ctx, &alarm);
    if (ret != QM_EOK) {
        return ret;
    }
    if (!alarm->snooze.enabled ||
        ctx->active_snooze_count >= alarm->snooze.max_count) {
        return -QM_EBUSY;
    }

    /* Snooze reuses the active alarm entry and reschedules only that instance. */
    snooze_index = ctx->active_alarm_index;
    ctx->active_snooze_count++;
    ctx->snooze_pending = QM_TRUE;
    ctx->snooze_alarm_index = snooze_index;
    ctx->snooze_fire_time = qm_time(NULL) + alarm->snooze.interval_min * 60ULL;
    ctx->active_alarm_index = -1;
    log_alarm_summary("alarm snooze", alarm, snooze_index);

    ret = schedule_next(ctx);
    if (ret != QM_EOK) {
        ctx->active_alarm_index = snooze_index;
        if (ctx->active_snooze_count > 0U) {
            ctx->active_snooze_count--;
        }
        clear_pending_snooze(ctx, QM_FALSE);
        return ret;
    }

    if (ret == QM_EOK) {
        notify_alarm(ctx, QM_ALARM_NOTIFY_SNOOZE, alarm);
        set_state(ctx, QM_ALARM_STATE_SNOOZING, alarm, report_now);
    }

    return ret;
}

/**
 * @brief Dismiss the currently active alarm.
 */
static int handle_dismiss(alarm_ctx_t *ctx, qm_bool report_now)
{
    qm_alarm_item_t *alarm;
    int32_t dismiss_index;
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    ret = get_active_alarm(ctx, &alarm);
    if (ret != QM_EOK) {
        if (!has_pending_snooze(ctx)) {
            return ret;
        }
        dismiss_index = ctx->snooze_alarm_index;
        alarm = &ctx->alarms[dismiss_index];
    } else {
        dismiss_index = ctx->active_alarm_index;
    }
    if (ctx->snooze_pending) {
        cancel_scheduled_alarm(ctx);
    }

    log_alarm_summary("alarm dismiss", alarm, dismiss_index);
    ctx->active_alarm_index = -1;
    clear_pending_snooze(ctx, QM_TRUE);
    notify_alarm(ctx, QM_ALARM_NOTIFY_DISMISS, alarm);
    set_state(ctx, QM_ALARM_STATE_IDLE, alarm, report_now);

    return schedule_next(ctx);
}

static int handle_cancel_snooze(alarm_ctx_t *ctx)
{
    if (ctx == NULL || !has_pending_snooze(ctx)) {
        return -QM_EBUSY;
    }
    return handle_dismiss(ctx, QM_TRUE);
}

/**
 * @brief Handle cloud close or snooze for the currently active alarm.
 */
static int handle_cloud_active_alarm(alarm_ctx_t *ctx,
                                     const alarm_cloud_req_t *req,
                                     const char *ack_submethod,
                                     qm_bool snooze)
{
    if (ctx == NULL || req == NULL) {
        return -QM_EINVAL;
    }

    {
        int action_ret = snooze ? handle_snooze(ctx, QM_FALSE) : handle_dismiss(ctx, QM_FALSE);
        int ack_ret = reply_simple_ack(ctx,
                                       req,
                                       ack_submethod,
                                       action_ret == QM_EOK ? 0 : action_ret,
                                       snooze ? ACK_EMPTY_ARRAY : NULL);
        if (action_ret == QM_EOK && ack_ret == QM_EOK) {
            (void)report_state(ctx);
        }
        return ack_ret;
    }
}

/**
 * @brief Owner-task transition that promotes the next scheduled alarm to active state.
 */
static void alarm_timer_fire_internal(alarm_ctx_t *ctx)
{
    int32_t *changed_peer_indices = NULL;
    int32_t ring_index;
    int32_t previous_index;
    qm_time_t ring_fire_time;
    qm_time_t now;
    uint16_t changed_peer_count = 0U;
    uint16_t notify_index;
    qm_bool resume_snooze = QM_FALSE;
    qm_bool changed = QM_FALSE;

    if (ctx == NULL || ctx->next_alarm_index < 0 ||
        (uint16_t)ctx->next_alarm_index >= ctx->count) {
        return;
    }

    ring_index = ctx->next_alarm_index;
    ring_fire_time = ctx->next_fire_time;
    now = qm_time(NULL);
    if (ring_fire_time == 0U) {
        return;
    }
    if (ring_fire_time > now) {
        uint32_t retry_delay_ms = (uint32_t)((ring_fire_time - now) * 1000ULL);

        if (retry_delay_ms == 0U) {
            retry_delay_ms = 1U;
        }
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                "alarm timer arrived early index=%ld scheduled=%llu now=%llu retry_ms=%u",
                (long)ring_index,
                (unsigned long long)ring_fire_time,
                (unsigned long long)now,
                (unsigned int)retry_delay_ms);
        (void)schedule_alarm_after_ms(ctx, retry_delay_ms);
        return;
    }
    /* Consume the selected target before notifying or rescheduling. This
     * prevents schedule_next() from recovering the same repeating alarm
     * recursively after it has just fired. */
    ctx->next_alarm_index = -1;
    ctx->next_fire_time = 0U;
    if (ctx->count > 0U) {
        changed_peer_indices = (int32_t *)qm_malloc(sizeof(int32_t) * ctx->count);
    }
    resume_snooze = has_pending_snooze(ctx) &&
                    ctx->snooze_alarm_index == ring_index &&
                    ctx->snooze_fire_time == ring_fire_time;
    if (!resume_snooze) {
        if (ctx->snooze_pending) {
            QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                    "new alarm index=%ld clears previous snooze index=%ld",
                    (long)ring_index,
                    (long)ctx->snooze_alarm_index);
        }
        clear_pending_snooze(ctx, QM_TRUE);
    } else {
        clear_pending_snooze(ctx, QM_FALSE);
    }

    previous_index = ctx->active_alarm_index;
    if (previous_index >= 0 && previous_index != ring_index &&
        (uint16_t)previous_index < ctx->count) {
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                "preempt active alarm index=%ld for next index=%ld",
                (long)previous_index,
                (long)ring_index);
        ctx->active_alarm_index = -1;
        notify_alarm(ctx,
                     QM_ALARM_NOTIFY_DISMISS,
                     &ctx->alarms[previous_index]);
    }
    ctx->active_alarm_index = ring_index;

    log_alarm_summary("alarm ring", &ctx->alarms[ctx->active_alarm_index], ctx->active_alarm_index);
    notify_alarm(ctx,
                 QM_ALARM_NOTIFY_RING,
                 &ctx->alarms[ctx->active_alarm_index]);
    set_state(ctx,
              QM_ALARM_STATE_RINGING,
              &ctx->alarms[ctx->active_alarm_index],
              QM_TRUE);

    /* One-shot alarms are disabled after they ring once. */
    if (ctx->alarms[ctx->active_alarm_index].repeat_mask == 0U &&
        ctx->alarms[ctx->active_alarm_index].enabled) {
        ctx->alarms[ctx->active_alarm_index].enabled = QM_FALSE;
        mark_alarm_index_dirty(ctx, ctx->active_alarm_index);
        changed = QM_TRUE;
    }
    changed_peer_count = collect_same_time_one_shot_peer_indices(ctx,
                                                                 ctx->active_alarm_index,
                                                                 ring_fire_time,
                                                                 changed_peer_indices,
                                                                 (changed_peer_indices != NULL) ? ctx->count : 0U);
    if (disable_same_time_one_shot_peers(ctx,
                                         ctx->active_alarm_index,
                                         ring_fire_time)) {
        changed = QM_TRUE;
    }
    if (changed) {
        (void)save_alarms(ctx);
        notify_alarm(ctx,
                     QM_ALARM_NOTIFY_CHANGED,
                     &ctx->alarms[ctx->active_alarm_index]);
        for (notify_index = 0U; notify_index < changed_peer_count; notify_index++) {
            int32_t peer_index = changed_peer_indices[notify_index];

            if (peer_index >= 0 && (uint16_t)peer_index < ctx->count) {
                notify_alarm(ctx,
                             QM_ALARM_NOTIFY_CHANGED,
                             &ctx->alarms[peer_index]);
            }
        }
    }

    qm_free(changed_peer_indices);

    (void)schedule_next(ctx);
    if (changed) {
        /* Sync all automatic disables together, including same-time peers.
         * Arm the next alarm before attempting the cloud report. */
        (void)report_or_mark_dirty(ctx);
    }
}

/* Timer callbacks only submit work; alarm state remains owner-task confined. */
static void alarm_timer_cb(void *arg)
{
    alarm_timer_token_t *token = (alarm_timer_token_t *)arg;
    alarm_ctx_t *ctx;
    alarm_owner_cmd_t cmd;

    if (token == NULL || !token->lock_ready ||
        qm_mutex_lock(&token->lock, QM_WAIT_FOREVER) != QM_EOK) {
        return;
    }
    ctx = token->ctx;
    if (ctx == NULL) {
        (void)qm_mutex_unlock(&token->lock);
        return;
    }
    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                  "alarm timer callback next_index=%ld next_fire=%llu",
                  (long)ctx->next_alarm_index,
                  (unsigned long long)ctx->next_fire_time);
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_TIMER;
    if (alarm_owner_submit(ctx, &cmd, QM_FALSE) != QM_EOK) {
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                      "timer command submit failed");
        if (!ctx->closing) {
            uint8_t retry_shift = ctx->timer_retry_count;
            uint32_t retry_delay;

            if (retry_shift > QM_ALARM_TIMER_RETRY_SHIFT_MAX) {
                retry_shift = QM_ALARM_TIMER_RETRY_SHIFT_MAX;
            }
            retry_delay = QM_ALARM_TIMER_RETRY_BASE_MS << retry_shift;
            if (ctx->timer_retry_count < 255U) {
                ctx->timer_retry_count++;
            }
            QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_SCHEDULER,
                          "retry alarm timer command delay_ms=%u attempt=%u",
                          (unsigned int)retry_delay,
                          (unsigned int)ctx->timer_retry_count);
            (void)schedule_alarm_after_ms(ctx, retry_delay);
        }
    }
    (void)qm_mutex_unlock(&token->lock);
}

static void handle_cloud_down(void *handle,
                              const qm_iot_mqtt_recv_t *packet,
                              void *userdata);
static void handle_report_reply(void *handle,
                                const qm_iot_mqtt_recv_t *packet,
                                void *userdata);
static void handle_iot_event(void *iot_handle,
                             const qm_iot_event_t *event,
                             void *userdata);
static int parse_cloud_req(const qm_iot_mqtt_recv_t *packet,
                           alarm_cloud_req_t *req);
static void free_cloud_req(alarm_cloud_req_t *req);

static const char *cloud_ack_submethod(const char *submethod)
{
    if (submethod == NULL) {
        return NULL;
    }
    if (qm_strcmp(submethod, SUBMETHOD_CREATE) == 0) {
        return SUBMETHOD_CREATE_ACK;
    }
    if (qm_strcmp(submethod, SUBMETHOD_MODIFY) == 0) {
        return SUBMETHOD_MODIFY_ACK;
    }
    if (qm_strcmp(submethod, SUBMETHOD_DELETE) == 0) {
        return SUBMETHOD_DELETE_ACK;
    }
    if (qm_strcmp(submethod, SUBMETHOD_ENABLE) == 0) {
        return SUBMETHOD_ENABLE_ACK;
    }
    if (qm_strcmp(submethod, SUBMETHOD_CLOSE) == 0) {
        return SUBMETHOD_CLOSE_ACK;
    }
    if (qm_strcmp(submethod, SUBMETHOD_SNOOZE) == 0) {
        return SUBMETHOD_SNOOZE_ACK;
    }
    return NULL;
}

static int alarm_owner_dispatch(alarm_ctx_t *ctx, alarm_owner_cmd_t *cmd)
{
    if (ctx == NULL || cmd == NULL) {
        return -QM_EINVAL;
    }

    switch (cmd->type) {
        case ALARM_OWNER_START:
            return alarm_start_internal(ctx);
        case ALARM_OWNER_STOP:
            return alarm_stop_internal(ctx);
        case ALARM_OWNER_DESTROY:
            return alarm_stop_internal(ctx);
        case ALARM_OWNER_TIMER:
            if (!ctx->started) {
                return QM_EOK;
            }
            ctx->timer_retry_count = 0U;
            alarm_timer_fire_internal(ctx);
            return QM_EOK;
        case ALARM_OWNER_CREATE:
            return handle_create_local(ctx, &cmd->item);
        case ALARM_OWNER_MODIFY:
            return handle_modify_local(ctx, &cmd->item);
        case ALARM_OWNER_DELETE:
            return handle_delete_local(ctx, cmd->source, cmd->alarm_id);
        case ALARM_OWNER_ENABLE:
            return handle_enable_local(ctx, cmd->source, cmd->alarm_id, cmd->enabled);
        case ALARM_OWNER_SNOOZE:
            return handle_snooze(ctx, QM_TRUE);
        case ALARM_OWNER_CANCEL_SNOOZE:
            return handle_cancel_snooze(ctx);
        case ALARM_OWNER_DISMISS:
            return handle_dismiss(ctx, QM_TRUE);
        case ALARM_OWNER_REPORT:
            return handle_update(ctx);
        case ALARM_OWNER_LIST:
            return copy_alarm_list(ctx, cmd->list_out, cmd->list_capacity, cmd->list_count);
        case ALARM_OWNER_UPCOMING:
            return copy_next_alarm(ctx, cmd->list_out, cmd->trigger_time);
        case ALARM_OWNER_STATE:
            if (cmd->state_out == NULL || cmd->state_alarm_id_out == NULL) {
                return -QM_EINVAL;
            }
            *cmd->state_out = ctx->state;
            *cmd->state_alarm_id_out = ctx->state_alarm_id;
            return QM_EOK;
        case ALARM_OWNER_CLOUD_DOWN:
            if (qm_strcmp(cmd->cloud_req.submethod, SUBMETHOD_CREATE) == 0) {
                return handle_cloud_command(ctx, &cmd->cloud_req,
                                             SUBMETHOD_CREATE_ACK,
                                             ALARM_CLOUD_ACTION_CREATE);
            }
            if (qm_strcmp(cmd->cloud_req.submethod, SUBMETHOD_MODIFY) == 0) {
                return handle_cloud_command(ctx, &cmd->cloud_req,
                                             SUBMETHOD_MODIFY_ACK,
                                             ALARM_CLOUD_ACTION_MODIFY);
            }
            if (qm_strcmp(cmd->cloud_req.submethod, SUBMETHOD_DELETE) == 0) {
                return handle_cloud_command(ctx, &cmd->cloud_req,
                                             SUBMETHOD_DELETE_ACK,
                                             ALARM_CLOUD_ACTION_DELETE);
            }
            if (qm_strcmp(cmd->cloud_req.submethod, SUBMETHOD_ENABLE) == 0) {
                return handle_cloud_command(ctx, &cmd->cloud_req,
                                             SUBMETHOD_ENABLE_ACK,
                                             ALARM_CLOUD_ACTION_ENABLE);
            }
            if (qm_strcmp(cmd->cloud_req.submethod, SUBMETHOD_CLOSE) == 0) {
                return handle_cloud_active_alarm(ctx, &cmd->cloud_req,
                                                 SUBMETHOD_CLOSE_ACK, QM_FALSE);
            }
            if (qm_strcmp(cmd->cloud_req.submethod, SUBMETHOD_SNOOZE) == 0) {
                return handle_cloud_active_alarm(ctx, &cmd->cloud_req,
                                                 SUBMETHOD_SNOOZE_ACK, QM_TRUE);
            }
            QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_CLOUD,
                          "unsupported alarm submethod=%s",
                          cmd->cloud_req.submethod);
            return -QM_EINVAL;
        case ALARM_OWNER_IOT_EVENT:
            if (cmd->iot_event_type == QM_IOT_EVENT_CLOUD_CONN) {
                ctx->cloud_ready = QM_TRUE;
                QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
                              "alarm cloud connected dirty=%u",
                              (unsigned int)ctx->dirty);
                if (ctx->dirty) {
                    (void)handle_update(ctx);
                }
                (void)report_state(ctx);
                return QM_EOK;
            }
            if (cmd->iot_event_type == QM_IOT_EVENT_CLOUD_DISCONN) {
                ctx->cloud_ready = QM_FALSE;
                QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
                              "alarm cloud disconnected");
            }
            if (cmd->iot_event_type == QM_IOT_EVENT_NETWORK_UP &&
                ctx->cloud_ready) {
                /* A retained MQTT session may survive a short Wi-Fi outage
                 * without another CLOUD_CONN event. Do not infer Cloud UP. */
                if (ctx->dirty) {
                    (void)handle_update(ctx);
                }
                if (ctx->state_dirty) {
                    (void)report_state(ctx);
                }
            }
            return QM_EOK;
        default:
            return -QM_EINVAL;
    }
}

static void alarm_owner_task(void *arg)
{
    alarm_ctx_t *ctx = (alarm_ctx_t *)arg;
    alarm_owner_cmd_t cmd;
    unsigned int size;

    if (ctx == NULL) {
        return;
    }

    for (;;) {
        size = sizeof(cmd);
        if (qm_queue_recv(&ctx->owner_queue, &cmd, &size, QM_WAIT_FOREVER) != QM_EOK) {
            continue;
        }
        if (size != sizeof(cmd)) {
            continue;
        }

        if (cmd.result_out != NULL) {
            *cmd.result_out = alarm_owner_dispatch(ctx, &cmd);
        } else {
            (void)alarm_owner_dispatch(ctx, &cmd);
        }
        if (cmd.done != NULL) {
            (void)qm_sem_signal(cmd.done);
        }
        if (cmd.type == ALARM_OWNER_DESTROY) {
            break;
        }
        if (cmd.type == ALARM_OWNER_CLOUD_DOWN) {
            free_cloud_req(&cmd.cloud_req);
        }
    }

    (void)qm_sem_signal(&ctx->owner_exit);
    (void)qm_task_exit(NULL);
}

static int alarm_owner_submit(alarm_ctx_t *ctx,
                              alarm_owner_cmd_t *cmd,
                              qm_bool wait)
{
    qm_sem_t done;
    int result = QM_EOK;
    int ret;

    if (ctx == NULL || cmd == NULL) {
        return -QM_EINVAL;
    }
    (void)__sync_add_and_fetch(&ctx->owner_call_count, 1U);
    if (!ctx->owner_ready || !ctx->owner_submit_lock_ready) {
        (void)__sync_sub_and_fetch(&ctx->owner_call_count, 1U);
        return -QM_EINVAL;
    }
    if (wait && ctx->owner_callback_active) {
        alarm_owner_call_release(ctx);
        return -QM_EBUSY;
    }
    if (ctx->closing && cmd->type != ALARM_OWNER_DESTROY) {
        alarm_owner_call_release(ctx);
        return -QM_EBUSY;
    }
    if (qm_mutex_lock(&ctx->owner_submit_lock, QM_WAIT_FOREVER) != QM_EOK) {
        alarm_owner_call_release(ctx);
        return -QM_EBUSY;
    }
    if (ctx->closing && cmd->type != ALARM_OWNER_DESTROY) {
        (void)qm_mutex_unlock(&ctx->owner_submit_lock);
        alarm_owner_call_release(ctx);
        return -QM_EBUSY;
    }

    qm_memset(&done, 0, sizeof(done));
    cmd->done = NULL;
    cmd->result_out = NULL;
    if (wait) {
        ret = qm_sem_new(&done, 0);
        if (ret != QM_EOK) {
            (void)qm_mutex_unlock(&ctx->owner_submit_lock);
            alarm_owner_call_release(ctx);
            return ret;
        }
        cmd->done = &done;
        cmd->result_out = &result;
    }

    ret = qm_queue_send(&ctx->owner_queue, cmd, sizeof(*cmd));
    if (ret != QM_EOK) {
        if (wait) {
            (void)qm_sem_free(&done);
        }
        (void)qm_mutex_unlock(&ctx->owner_submit_lock);
        alarm_owner_call_release(ctx);
        return ret;
    }
    (void)qm_mutex_unlock(&ctx->owner_submit_lock);
    if (wait) {
        ret = qm_sem_wait(&done, QM_WAIT_FOREVER);
        (void)qm_sem_free(&done);
        if (ret != QM_EOK) {
            alarm_owner_call_release(ctx);
            return ret;
        }
        alarm_owner_call_release(ctx);
        return result;
    }
    alarm_owner_call_release(ctx);
    return QM_EOK;
}

/**
 * @brief Initialize alarm runtime.
 */
qm_alarm_t qm_alarm_create(const qm_alarm_config_t *config)
{
    const qm_alarm_config_t *alarm_config = config;
    alarm_ctx_t *ctx;
    uint16_t capacity = (uint16_t)CONFIG_QM_ALARM_MAX_ALARMS;

    if (alarm_config != NULL && alarm_config->max_alarms > 0U) {
        capacity = alarm_config->max_alarms;
    }
    if (capacity == 0U || capacity > MAX_ALARM_SLOT_COUNT) {
        QM_ALARM_LOGE(LOG_TAG,
                "invalid alarm capacity=%u max_supported=%u",
                (unsigned int)capacity,
                (unsigned int)MAX_ALARM_SLOT_COUNT);
        return NULL;
    }

    ctx = (alarm_ctx_t *)qm_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }
    qm_memset(ctx, 0, sizeof(*ctx));
    if (alarm_config != NULL) {
        ctx->config = *alarm_config;
        ctx->notify_cb = alarm_config->notify_cb;
        ctx->user_ctx = alarm_config->user_ctx;
    }

    ctx->alarms = (qm_alarm_item_t *)
        qm_malloc(sizeof(qm_alarm_item_t) * capacity);
    if (ctx->alarms == NULL) {
        qm_free(ctx);
        return NULL;
    }
    qm_memset(ctx->alarms, 0, sizeof(qm_alarm_item_t) * capacity);
    ctx->alarm_slots = (uint16_t *)qm_malloc(sizeof(uint16_t) * capacity);
    if (ctx->alarm_slots == NULL) {
        qm_free(ctx->alarms);
        qm_free(ctx);
        return NULL;
    }
    ctx->bitmap_bytes = alarm_bitmap_bytes(capacity);
    ctx->used_bitmap = (uint8_t *)qm_malloc(ctx->bitmap_bytes);
    if (ctx->used_bitmap == NULL) {
        qm_free(ctx->alarm_slots);
        qm_free(ctx->alarms);
        qm_free(ctx);
        return NULL;
    }
    ctx->slot_dirty = (qm_bool *)qm_malloc(sizeof(qm_bool) * capacity);
    if (ctx->slot_dirty == NULL) {
        qm_free(ctx->used_bitmap);
        qm_free(ctx->alarm_slots);
        qm_free(ctx->alarms);
        qm_free(ctx);
        return NULL;
    }
    ctx->schedule_work = (qm_work_t *)qm_malloc(sizeof(*ctx->schedule_work));
    if (ctx->schedule_work == NULL) {
        qm_free(ctx->slot_dirty);
        qm_free(ctx->used_bitmap);
        qm_free(ctx->alarm_slots);
        qm_free(ctx->alarms);
        qm_free(ctx);
        return NULL;
    }
    qm_memset(ctx->schedule_work, 0, sizeof(*ctx->schedule_work));
    ctx->timer_token = (alarm_timer_token_t *)qm_malloc(sizeof(*ctx->timer_token));
    if (ctx->timer_token == NULL) {
        qm_free(ctx->schedule_work);
        qm_free(ctx->slot_dirty);
        qm_free(ctx->used_bitmap);
        qm_free(ctx->alarm_slots);
        qm_free(ctx->alarms);
        qm_free(ctx);
        return NULL;
    }
    qm_memset(ctx->used_bitmap, 0, ctx->bitmap_bytes);
    qm_memset(ctx->slot_dirty, 0, sizeof(qm_bool) * capacity);
    ctx->timer_token->ctx = ctx;
    if (qm_mutex_new(&ctx->timer_token->lock) != QM_EOK) {
        qm_free(ctx->timer_token);
        qm_free(ctx->schedule_work);
        qm_free(ctx->slot_dirty);
        qm_free(ctx->used_bitmap);
        qm_free(ctx->alarm_slots);
        qm_free(ctx->alarms);
        qm_free(ctx);
        return NULL;
    }
    ctx->timer_token->lock_ready = QM_TRUE;

    ctx->capacity = capacity;
    ctx->next_local_alarm_id = 1U;
    ctx->next_alarm_index = -1;
    ctx->active_alarm_index = -1;
    ctx->snooze_alarm_index = -1;
    reset_slot_tracking(ctx);
    {
        int ret;

        /* Load persisted alarms before the runtime starts accepting requests. */
        ctx->storage_ready = QM_TRUE;
        ret = load_alarms(ctx);
        if (ret != QM_EOK) {
            alarm_deinit(ctx);
            return NULL;
        }
    }

    if (qm_queue_new(&ctx->owner_queue,
                     QM_ALARM_COMMAND_QUEUE_LEN,
                     sizeof(alarm_owner_cmd_t)) != QM_EOK) {
        alarm_deinit(ctx);
        return NULL;
    }
    if (qm_sem_new(&ctx->owner_exit, 0) != QM_EOK) {
        (void)qm_queue_free(&ctx->owner_queue);
        alarm_deinit(ctx);
        return NULL;
    }
    if (qm_sem_new(&ctx->owner_calls_zero, 0) != QM_EOK) {
        (void)qm_sem_free(&ctx->owner_exit);
        (void)qm_queue_free(&ctx->owner_queue);
        alarm_deinit(ctx);
        return NULL;
    }
    if (qm_mutex_new(&ctx->owner_submit_lock) != QM_EOK) {
        (void)qm_sem_free(&ctx->owner_calls_zero);
        (void)qm_sem_free(&ctx->owner_exit);
        (void)qm_queue_free(&ctx->owner_queue);
        alarm_deinit(ctx);
        return NULL;
    }
    ctx->owner_submit_lock_ready = QM_TRUE;
    if (qm_task_new(&ctx->owner_task,
                    "qm_alarm_owner",
                    alarm_owner_task,
                    ctx,
                    CONFIG_QM_ALARM_TASK_STACK_SIZE,
                    CONFIG_QM_ALARM_TASK_PRIO) != QM_EOK) {
        ctx->owner_submit_lock_ready = QM_FALSE;
        (void)qm_mutex_free(&ctx->owner_submit_lock);
        (void)qm_sem_free(&ctx->owner_calls_zero);
        (void)qm_sem_free(&ctx->owner_exit);
        (void)qm_queue_free(&ctx->owner_queue);
        alarm_deinit(ctx);
        return NULL;
    }
    ctx->owner_ready = QM_TRUE;

    return ctx;
}

/**
 * @brief Start alarm scheduling.
 */
static int alarm_start_internal(alarm_ctx_t *ctx)
{
    int ret;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (ctx->started) {
        return QM_EOK;
    }

    if (ctx->config.iot_handle != NULL) {
        /* Bind alarm/down and alarmReply handlers directly to the iot session. */
        ret = qm_iot_down_register(ctx->config.iot_handle,
                                   (char *)DISPATCH_METHOD,
                                   handle_cloud_down,
                                   ctx);
        if (ret != QM_EOK) {
            return ret;
        }
        ctx->down_registered = QM_TRUE;

        ret = qm_iot_down_register(ctx->config.iot_handle,
                                   (char *)REPLY_METHOD,
                                   handle_report_reply,
                                   ctx);
        if (ret != QM_EOK) {
            (void)qm_iot_down_register(ctx->config.iot_handle,
                                       (char *)DISPATCH_METHOD,
                                       NULL,
                                       NULL);
            ctx->down_registered = QM_FALSE;
            return ret;
        }
        ctx->reply_registered = QM_TRUE;

        ret = qm_iot_event_register(ctx->config.iot_handle, handle_iot_event, ctx);
        if (ret != QM_EOK) {
            (void)qm_iot_down_register(ctx->config.iot_handle,
                                       (char *)REPLY_METHOD,
                                       NULL,
                                       NULL);
            (void)qm_iot_down_register(ctx->config.iot_handle,
                                       (char *)DISPATCH_METHOD,
                                       NULL,
                                       NULL);
            ctx->down_registered = QM_FALSE;
            ctx->reply_registered = QM_FALSE;
            return ret;
        }
        ctx->iot_event_registered = QM_TRUE;
        ctx->cloud_ready = QM_FALSE;
    }

    ret = schedule_next(ctx);
    if (ret != QM_EOK) {
        (void)alarm_stop_internal(ctx);
        return ret;
    }

    ctx->started = QM_TRUE;
    if (ctx->config.iot_handle != NULL) {
        /* The IOT handle is initialized before the MQTT session is connected.
         * Defer the initial list report until QM_IOT_EVENT_CLOUD_CONN. */
        ctx->dirty = QM_TRUE;
        QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
                      "cloud not connected, defer initial alarm list report");
    }

    return QM_EOK;
}

/**
 * @brief Stop alarm scheduling.
 */
static int alarm_stop_internal(alarm_ctx_t *ctx)
{
    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    cancel_scheduled_alarm(ctx);
    ctx->next_alarm_index = -1;
    ctx->next_fire_time = 0U;
    ctx->active_alarm_index = -1;
    clear_pending_snooze(ctx, QM_TRUE);
    if (ctx->config.iot_handle != NULL) {
        if (ctx->iot_event_registered) {
            (void)qm_iot_event_unregister_one(ctx->config.iot_handle,
                                              handle_iot_event,
                                              ctx);
            ctx->iot_event_registered = QM_FALSE;
        }
        if (ctx->reply_registered) {
            (void)qm_iot_down_register(ctx->config.iot_handle,
                                       (char *)REPLY_METHOD,
                                       NULL,
                                       NULL);
            ctx->reply_registered = QM_FALSE;
        }
        if (ctx->down_registered) {
            (void)qm_iot_down_register(ctx->config.iot_handle,
                                       (char *)DISPATCH_METHOD,
                                       NULL,
                                       NULL);
            ctx->down_registered = QM_FALSE;
        }
    }
    ctx->started = QM_FALSE;
    ctx->cloud_ready = QM_FALSE;
    return QM_EOK;
}

/**
 * @brief Deinitialize one alarm runtime.
 */
static void alarm_deinit(qm_alarm_t alarm)
{
    alarm_ctx_t *ctx = alarm;
    uint16_t i;

    if (ctx == NULL) {
        return;
    }

    cancel_scheduled_alarm(ctx);
    if (ctx->timer_token != NULL) {
        if (ctx->timer_token->lock_ready) {
            (void)qm_mutex_lock(&ctx->timer_token->lock, QM_WAIT_FOREVER);
        }
        ctx->timer_token->ctx = NULL;
        if (ctx->timer_token->lock_ready) {
            (void)qm_mutex_unlock(&ctx->timer_token->lock);
        }
    }
    if (ctx->owner_ready) {
        (void)qm_queue_free(&ctx->owner_queue);
        (void)qm_sem_free(&ctx->owner_exit);
        (void)qm_sem_free(&ctx->owner_calls_zero);
        if (ctx->owner_submit_lock_ready) {
            (void)qm_mutex_free(&ctx->owner_submit_lock);
        }
        ctx->owner_ready = QM_FALSE;
        ctx->owner_submit_lock_ready = QM_FALSE;
    }
    /* extensions are heap-owned snapshots and must be released item by item. */
    for (i = 0; i < ctx->count; i++) {
        clear_alarm_extensions(&ctx->alarms[i]);
    }
    if (ctx->timer_token != NULL) {
        if (ctx->timer_token->lock_ready) {
            (void)qm_mutex_free(&ctx->timer_token->lock);
            ctx->timer_token->lock_ready = QM_FALSE;
        }
        qm_free(ctx->timer_token);
    }
    /* A queued qm_work event still contains the work pointer after cancel.
     * Keep initialized work storage alive so that stale events can be rejected
     * safely by qm_work's sequence check. */
    if (ctx->schedule_work != NULL && ctx->schedule_work->magic == 0U) {
        qm_free(ctx->schedule_work);
    }
    qm_free(ctx->slot_dirty);
    qm_free(ctx->used_bitmap);
    qm_free(ctx->alarm_slots);
    qm_free(ctx->alarms);
    qm_free(ctx);
}

/**
 * @brief Track cloud connection changes for deferred full-list sync.
 */
static void handle_iot_event(void *iot_handle,
                             const qm_iot_event_t *event,
                             void *userdata)
{
    alarm_ctx_t *ctx = (alarm_ctx_t *)userdata;

    QM_UNUSED_ARG(iot_handle);

    if (ctx == NULL || event == NULL) {
        return;
    }
    {
        alarm_owner_cmd_t cmd;

        qm_memset(&cmd, 0, sizeof(cmd));
        cmd.type = ALARM_OWNER_IOT_EVENT;
        cmd.iot_event_type = event->type;
        (void)alarm_owner_submit(ctx, &cmd, QM_FALSE);
    }
}

/**
 * @brief Release heap-owned request payload created during cloud parsing.
 */
static void free_cloud_req(alarm_cloud_req_t *req)
{
    if (req == NULL) {
        return;
    }

    if (req->params != NULL) {
        cJSON_free(req->params);
        req->params = NULL;
    }
}

/**
 * @brief Parse one `alarm` downlink packet into a lightweight request view.
 */
static int parse_cloud_req(const qm_iot_mqtt_recv_t *packet,
                           alarm_cloud_req_t *req)
{
    cJSON *root = NULL;
    const cJSON *id = NULL;
    const cJSON *submethod = NULL;
    const cJSON *params = NULL;
    const char *submethod_value = NULL;

    if (packet == NULL || req == NULL ||
        packet->type != QM_IOT_MQTTRECV_PUB ||
        packet->data.pub.payload == NULL ||
        packet->data.pub.payload_len == 0U) {
        return -QM_EINVAL;
    }

    qm_memset(req, 0, sizeof(*req));
    root = cJSON_ParseWithLength((const char *)packet->data.pub.payload,
                                 packet->data.pub.payload_len);
    if (root == NULL) {
        return -QM_EINVAL;
    }

    submethod = cJSON_GetObjectItem(root, "subMethod");
    if (!cJSON_IsString(submethod)) {
        submethod = cJSON_GetObjectItem(root, "submethod");
    }
    if (!cJSON_IsString(submethod)) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }

    submethod_value = cJSON_GetStringValue(submethod);
    if (submethod_value == NULL || submethod_value[0] == '\0') {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (params != NULL) {
        /* Keep params as compact JSON text so the existing batch handlers stay unchanged. */
        req->params = cJSON_PrintUnformatted((cJSON *)params);
        if (req->params == NULL) {
            cJSON_Delete(root);
            return -QM_ENOMEM;
        }
        req->params_len = (uint32_t)qm_strlen(req->params);
    }

    id = cJSON_GetObjectItem(root, "id");
    req->msg_id = json_uint_value(id);
    qm_snprintf(req->submethod, sizeof(req->submethod), "%s", submethod_value);
    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
            "parsed cloud req submethod=%s id=%u params_len=%u",
            req->submethod,
            (unsigned int)req->msg_id,
            (unsigned int)req->params_len);
    cJSON_Delete(root);
    return QM_EOK;
}

/**
 * @brief Dispatch one cloud `alarm` command to the existing batch handlers.
 */
static void handle_cloud_down(void *handle,
                              const qm_iot_mqtt_recv_t *packet,
                              void *userdata)
{
    alarm_ctx_t *ctx = (alarm_ctx_t *)userdata;
    alarm_cloud_req_t req;
    alarm_owner_cmd_t cmd;
    int ret;

    QM_UNUSED_ARG(handle);

    if (ctx == NULL) {
        return;
    }

    ret = parse_cloud_req(packet, &req);
    if (ret != QM_EOK) {
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_CLOUD, "parse cloud down failed ret=%d", ret);
        return;
    }

    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_CLOUD_DOWN;
    cmd.cloud_req = req;
    ret = alarm_owner_submit(ctx, &cmd, QM_FALSE);
    if (ret != QM_EOK) {
        const char *ack_submethod = cloud_ack_submethod(req.submethod);

        /* Build the failure ACK while req.params is still owned by req. */
        if (ack_submethod != NULL) {
            (void)reply_simple_ack(ctx,
                                   &req,
                                   ack_submethod,
                                   -QM_EBUSY,
                                   qm_strcmp(req.submethod, SUBMETHOD_SNOOZE) == 0 ?
                                   ACK_EMPTY_ARRAY :
                                   (qm_strcmp(req.submethod, SUBMETHOD_CLOSE) == 0 ?
                                    NULL : ACK_EMPTY_ARRAY));
        }
        free_cloud_req(&req);
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_CLOUD,
                      "drop cloud command ret=%d", ret);
    }
}

/**
 * @brief Receive `alarmReply` packets for logging and future extension.
 */
static void handle_report_reply(void *handle,
                                const qm_iot_mqtt_recv_t *packet,
                                void *userdata)
{
    QM_UNUSED_ARG(handle);
    QM_UNUSED_ARG(userdata);

    if (packet == NULL || packet->type != QM_IOT_MQTTRECV_PUB) {
        return;
    }

    QM_ALARM_LOGI(LOG_TAG, QM_ALARM_LOG_CLOUD,
            "recv alarm reply payload_len=%u",
            (unsigned int)packet->data.pub.payload_len);
}

void qm_alarm_destroy(qm_alarm_t alarm)
{
    alarm_ctx_t *ctx = alarm;
    alarm_owner_cmd_t cmd;
    int ret;

    if (alarm == NULL) {
        return;
    }

    if (__sync_lock_test_and_set(&ctx->destroy_claimed, 1U) != 0U) {
        return;
    }
    ctx->closing = 1U;

    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_DESTROY;
    ret = alarm_owner_submit(ctx, &cmd, QM_TRUE);
    if (ret != QM_EOK) {
        QM_ALARM_LOGW(LOG_TAG, QM_ALARM_LOG_LIFECYCLE,
                      "alarm owner destroy command failed ret=%d", ret);
        ctx->closing = 0U;
        ctx->destroy_claimed = 0U;
        return;
    }
    (void)qm_sem_wait(&ctx->owner_exit, QM_WAIT_FOREVER);
    if (__sync_fetch_and_add(&ctx->owner_call_count, 0U) != 0U) {
        (void)qm_sem_wait(&ctx->owner_calls_zero, QM_WAIT_FOREVER);
    }
    alarm_deinit(alarm);
}

int qm_alarm_start(qm_alarm_t alarm)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_START;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_stop(qm_alarm_t alarm)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_STOP;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_create_item(qm_alarm_t alarm, const qm_alarm_item_t *item)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL || item == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_CREATE;
    cmd.item = *item;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_modify_item(qm_alarm_t alarm, const qm_alarm_item_t *item)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL || item == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_MODIFY;
    cmd.item = *item;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_delete_item(qm_alarm_t alarm,
                         const char *source,
                         uint8_t alarm_id)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL || source == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_DELETE;
    qm_snprintf(cmd.source, sizeof(cmd.source), "%s", source);
    cmd.alarm_id = alarm_id;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_set_enabled(qm_alarm_t alarm,
                         const char *source,
                         uint8_t alarm_id,
                         qm_bool enabled)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL || source == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_ENABLE;
    qm_snprintf(cmd.source, sizeof(cmd.source), "%s", source);
    cmd.alarm_id = alarm_id;
    cmd.enabled = enabled;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_snooze(qm_alarm_t alarm)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_SNOOZE;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_cancel_snooze(qm_alarm_t alarm)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_CANCEL_SNOOZE;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_dismiss(qm_alarm_t alarm)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_DISMISS;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_get_state(qm_alarm_t alarm,
                       qm_alarm_state_t *state,
                       uint8_t *alarm_id)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL || state == NULL || alarm_id == NULL) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_STATE;
    cmd.state_out = state;
    cmd.state_alarm_id_out = alarm_id;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_report(qm_alarm_t alarm)
{
    if (alarm == NULL) {
        return -QM_EINVAL;
    }
    if (alarm->config.iot_handle == NULL) {
        return -QM_EINVAL;
    }

    {
        alarm_owner_cmd_t cmd;

        qm_memset(&cmd, 0, sizeof(cmd));
        cmd.type = ALARM_OWNER_REPORT;
        return alarm_owner_submit(alarm, &cmd, QM_TRUE);
    }
}

int qm_alarm_list(qm_alarm_t alarm,
                  qm_alarm_item_t *out,
                  uint16_t capacity,
                  uint16_t *count)
{
    alarm_owner_cmd_t cmd;

    if (alarm == NULL || count == NULL || (capacity > 0U && out == NULL)) {
        return -QM_EINVAL;
    }
    qm_memset(&cmd, 0, sizeof(cmd));
    cmd.type = ALARM_OWNER_LIST;
    cmd.list_out = out;
    cmd.list_capacity = capacity;
    cmd.list_count = count;
    return alarm_owner_submit(alarm, &cmd, QM_TRUE);
}

int qm_alarm_get_upcoming(qm_alarm_t alarm,
                          qm_alarm_item_t *out,
                          qm_time_t *trigger_time)
{
    if (out == NULL || trigger_time == NULL) {
        return -QM_EINVAL;
    }

    {
        alarm_owner_cmd_t cmd;

        qm_memset(&cmd, 0, sizeof(cmd));
        cmd.type = ALARM_OWNER_UPCOMING;
        cmd.list_out = out;
        cmd.trigger_time = trigger_time;
        return alarm_owner_submit(alarm, &cmd, QM_TRUE);
    }
}

#endif /* CONFIG_QM_ALARM_SUPPORT */
