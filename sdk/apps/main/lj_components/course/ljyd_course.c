/**
 ******************************************************************************
 * @file    ljyd_course.c
 * @brief   课程表（课表）独立实现：数据模型 + Flash 持久化 + 云端同步 + 播报。
 *
 * @details
 * 参考 WSL eye_jj 分支的课程表功能，下沉为本仓库 app 层独立模块：
 *  - 开机从 qm_kv 恢复课表（key = "course_cfg"）；
 *  - 连云后注册 `course` 下行，收到 `course/sync` 全量覆盖并回复 `course/syncAck`；
 *  - 端侧为被动接收端，绝不主动向云端请求课表；
 *  - 恢复出厂时由 ljyd_provision_clear_and_reboot() 调用 clear_configs()。
 ******************************************************************************
 */
#include "ljyd_course.h"

#include "qm.h"
#include "qm_iot_api.h"
#include "qm_kernel.h"
#include "qm_kv.h"
#include "qm_log.h"
#include "qm_spec_api.h"
#include "qm_time.h"

#include "cJSON.h"

#include <stdio.h>
#include <string.h>

#define LOG_TAG "course"

/**
 * @brief Flash 持久化结构。
 *
 * @note 字段顺序 / 类型与 qm_course 中间件及旧版 app course_config_t 完全一致，
 *       保证 OTA 升级后仍能读取旧固件写入的 course_cfg 数据。
 */
typedef struct {
    uint8_t             synced;      /**< 1 = 已同步。 */
    uint16_t            entry_count; /**< 有效条目数。 */
    ljyd_course_entry_t entries[LJYD_COURSE_MAX_ENTRIES];
} ljyd_course_store_t;

static ljyd_course_store_t s_store;
static void               *s_iot_handle;
static uint8_t             s_loaded;
static uint8_t             s_event_registered;
static uint8_t             s_down_registered;
static volatile int        s_updated;

/* qm_iot_down_register() 的 method 参数非 const，这里保留可写副本。 */
static char s_method[]   = LJYD_COURSE_METHOD;
static char s_sub_sync[] = LJYD_COURSE_SUB_SYNC;
static char s_sub_ack[]  = LJYD_COURSE_SUB_ACK;

/* -------------------------------------------------------------------------- */
/* 数据模型 / Flash 持久化                                                      */
/* -------------------------------------------------------------------------- */

static void ljyd_course_store_reset(void)
{
    memset(&s_store, 0, sizeof(s_store));
}

static void ljyd_course_load(void)
{
    int len = (int)sizeof(s_store);

    if (qm_kv_get(LJYD_COURSE_KV_KEY, &s_store, &len) != QM_EOK ||
        len != (int)sizeof(s_store)) {
        ljyd_course_store_reset();
        QM_LOGI(LOG_TAG, "no valid flash data, key=%s", LJYD_COURSE_KV_KEY);
        return;
    }

    if (s_store.entry_count > LJYD_COURSE_MAX_ENTRIES) {
        s_store.entry_count = LJYD_COURSE_MAX_ENTRIES;
    }
    QM_LOGI(LOG_TAG, "loaded: key=%s synced=%u entries=%u", LJYD_COURSE_KV_KEY,
            (unsigned)s_store.synced, (unsigned)s_store.entry_count);
}

static int ljyd_course_save(void)
{
    int ret = qm_kv_set(LJYD_COURSE_KV_KEY, &s_store, (int)sizeof(s_store), 1);

    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "save failed: key=%s ret=%d", LJYD_COURSE_KV_KEY, ret);
    } else {
        QM_LOGI(LOG_TAG, "saved: key=%s synced=%u entries=%u", LJYD_COURSE_KV_KEY,
                (unsigned)s_store.synced, (unsigned)s_store.entry_count);
    }
    return ret;
}

/**
 * @brief 全量覆盖课表并持久化（同时置 synced = 1）。
 */
static int ljyd_course_apply_sync(const ljyd_course_entry_t *entries,
                                  uint16_t count)
{
    uint16_t accepted;

    if (count > 0 && entries == NULL) {
        return -QM_EINVAL;
    }

    ljyd_course_store_reset();

    accepted = (count > LJYD_COURSE_MAX_ENTRIES) ? LJYD_COURSE_MAX_ENTRIES
                                                : count;
    for (uint16_t i = 0; i < accepted; i++) {
        s_store.entries[i] = entries[i];
        /* 保证字符串以 '\0' 结尾，防止上位机漏填终止符 */
        s_store.entries[i].subject[LJYD_COURSE_SUBJECT_MAX_LEN - 1]   = '\0';
        s_store.entries[i].textbook[LJYD_COURSE_TEXTBOOK_MAX_LEN - 1] = '\0';
    }

    s_store.synced      = 1;
    s_store.entry_count = accepted;

    QM_LOGI(LOG_TAG, "apply_sync: input=%u accepted=%u", (unsigned)count,
            (unsigned)accepted);
    return ljyd_course_save();
}

/* -------------------------------------------------------------------------- */
/* 云端同步（course/sync -> course/syncAck）                                     */
/* -------------------------------------------------------------------------- */

static void ljyd_course_send_ack(uint32_t msg_id)
{
    qm_iot_down_req_t reply_req = {
        .method    = s_method,
        .submethod = s_sub_ack,
        .msg_id    = msg_id,
    };

    if (s_iot_handle == NULL) {
        return;
    }

    int ret = (int)qm_iot_down_reply(s_iot_handle, &reply_req, 0, NULL, 0);
    QM_LOGI(LOG_TAG, "ack sent: method=%sReply subMethod=%s id=%u code=0 ret=%d",
            s_method, s_sub_ack, (unsigned)msg_id, ret);
}

/**
 * @brief 解析 params[0].extensions.courses[] 并全量覆盖课表。
 *
 * @return int 写入的条目数（>= 0）；负值为错误码。
 */
static int ljyd_course_parse_courses(cJSON *params)
{
    int count = 0;
    cJSON *p;
    cJSON *ext;
    cJSON *courses;
    ljyd_course_entry_t *entries;
    int n;
    int ret;

    if (!cJSON_IsArray(params) || cJSON_GetArraySize(params) <= 0) {
        return 0;
    }

    p       = cJSON_GetArrayItem(params, 0);
    ext     = p ? cJSON_GetObjectItem(p, "extensions") : NULL;
    courses = ext ? cJSON_GetObjectItem(ext, "courses") : NULL;
    if (!cJSON_IsArray(courses)) {
        return 0;
    }

    /* 下行回调运行在云任务栈上，避免在栈上放 3KB 的条目数组 */
    entries = (ljyd_course_entry_t *)qm_malloc(
        (unsigned int)(sizeof(ljyd_course_entry_t) * LJYD_COURSE_MAX_ENTRIES));
    if (entries == NULL) {
        QM_LOGE(LOG_TAG, "parse: alloc entries failed");
        return -QM_ENOMEM;
    }
    memset(entries, 0, sizeof(ljyd_course_entry_t) * LJYD_COURSE_MAX_ENTRIES);

    n = cJSON_GetArraySize(courses);
    for (int i = 0; i < n && count < LJYD_COURSE_MAX_ENTRIES; i++) {
        cJSON *c = cJSON_GetArrayItem(courses, i);
        if (c == NULL) {
            continue;
        }

        cJSON *wd = cJSON_GetObjectItem(c, "weekday");
        cJSON *pd = cJSON_GetObjectItem(c, "period");
        cJSON *sj = cJSON_GetObjectItem(c, "subject");
        cJSON *tb = cJSON_GetObjectItem(c, "textbook");
        if (!cJSON_IsNumber(wd) || !cJSON_IsNumber(pd) || !cJSON_IsString(sj)) {
            continue;
        }

        ljyd_course_entry_t *e = &entries[count];
        e->weekday = (uint8_t)wd->valueint;
        e->period  = (uint8_t)pd->valueint;
        snprintf(e->subject, sizeof(e->subject), "%s", cJSON_GetStringValue(sj));
        if (cJSON_IsString(tb)) {
            snprintf(e->textbook, sizeof(e->textbook), "%s",
                     cJSON_GetStringValue(tb));
        }
        QM_LOGI(LOG_TAG, "entry[%d]: weekday=%u period=%u subject=%s textbook=%s",
                count, e->weekday, e->period, e->subject,
                e->textbook[0] ? e->textbook : "(none)");
        count++;
    }

    ret = ljyd_course_apply_sync(entries, (uint16_t)count);
    qm_free(entries);

    return (ret == QM_EOK) ? count : ret;
}

static void ljyd_course_handle_down(void *handle,
                                    const qm_iot_mqtt_recv_t *packet,
                                    void *userdata)
{
    (void)handle;
    (void)userdata;

    QM_LOGI(LOG_TAG, "******course_handle_down*******");
    if (packet == NULL || packet->type != QM_IOT_MQTTRECV_PUB ||
        packet->data.pub.payload == NULL) {
        QM_LOGE(LOG_TAG, "down: invalid packet type=%d payload=%p",
                packet ? (int)packet->type : -1,
                (packet && packet->data.pub.payload) ? packet->data.pub.payload
                                                     : NULL);
        return;
    }

    QM_LOGI(LOG_TAG, "down recv: payload_len=%u payload=%.*s",
            (unsigned)packet->data.pub.payload_len,
            (int)packet->data.pub.payload_len,
            (const char *)packet->data.pub.payload);

    cJSON *root = cJSON_ParseWithLength((const char *)packet->data.pub.payload,
                                        packet->data.pub.payload_len);
    if (root == NULL) {
        QM_LOGE(LOG_TAG, "down: json parse failed");
        return;
    }

    do {
        cJSON *method = cJSON_GetObjectItem(root, "method");
        cJSON *sub    = cJSON_GetObjectItem(root, "subMethod");
        if (!cJSON_IsString(method) || !cJSON_IsString(sub) ||
            strcmp(cJSON_GetStringValue(method), s_method) != 0 ||
            strcmp(cJSON_GetStringValue(sub), s_sub_sync) != 0) {
            break;   /* 非本模块报文，直接忽略 */
        }

        uint32_t msg_id = 0;
        cJSON *id_item = cJSON_GetObjectItem(root, "id");
        if (cJSON_IsNumber(id_item)) {
            msg_id = (uint32_t)id_item->valuedouble;
        }

        int synced = ljyd_course_parse_courses(
            cJSON_GetObjectItem(root, "params"));
        if (synced < 0) {
            QM_LOGE(LOG_TAG, "apply failed (%d), skip ack", synced);
            break;
        }

        QM_LOGI(LOG_TAG, "course synced: %d entries, total=%u", synced,
                (unsigned)s_store.entry_count);

        ljyd_course_send_ack(msg_id);

        s_updated = 1;
        QM_LOGI(LOG_TAG, "sync applied: synced=%u entries=%u",
                (unsigned)s_store.synced, (unsigned)s_store.entry_count);
    } while (0);

    cJSON_Delete(root);
}

static int ljyd_course_register_down(void)
{
    int ret;

    if (s_iot_handle == NULL) {
        QM_LOGW(LOG_TAG, "register_down: cloud not ready");
        return -QM_EINVAL;
    }

    ret = (int)qm_iot_down_register(s_iot_handle, s_method,
                                    ljyd_course_handle_down, NULL);
    s_down_registered = (ret == QM_EOK) ? 1 : 0;
    QM_LOGI(LOG_TAG, "down_register ret=%d (method=%s)", ret, s_method);
    /* 端侧被动接收：绝不主动发送同步请求 */
    return ret;
}

/**
 * @brief 云事件监听：连云就绪后注册 `course` 下行，重连后重新挂订阅。
 */
static void ljyd_course_iot_event(void *handle,
                                  const qm_iot_event_t *event,
                                  void *userdata)
{
    (void)userdata;

    if (event == NULL) {
        return;
    }
    if (event->type == QM_IOT_EVENT_CLOUD_CONN) {
        s_iot_handle = handle;
        (void)ljyd_course_register_down();
    }
}

int ljyd_course_init(void *iot_handle)
{
    if (!s_loaded) {
        ljyd_course_store_reset();
        ljyd_course_load();
        s_loaded = 1;
    }

    if (iot_handle == NULL) {
        QM_LOGE(LOG_TAG, "init: iot handle is NULL");
        return -QM_EINVAL;
    }
    s_iot_handle = iot_handle;

    /* ljyd_cloud 的事件回调是单槽且已被 agent_app 占用，这里用 qm_cloud 的
     * 多监听器接口追加，互不影响。 */
    if (!s_event_registered) {
        int32_t ret = qm_iot_event_register(iot_handle, ljyd_course_iot_event, NULL);
        if (ret != QM_EOK) {
            QM_LOGE(LOG_TAG, "iot event register failed ret=%d", (int)ret);
            return (int)ret;
        }
        s_event_registered = 1;
        QM_LOGI(LOG_TAG, "iot event listener registered");
    }

    /* 若调用时已连云，CLOUD_CONN 事件可能已错过，主动补一次注册。 */
    if (!s_down_registered) {
        (void)ljyd_course_register_down();
    }

    QM_LOGI(LOG_TAG, "init ok: synced=%u entries=%u", (unsigned)s_store.synced,
            (unsigned)s_store.entry_count);
    return QM_EOK;
}

/* -------------------------------------------------------------------------- */
/* 查询 / 清理 / 播报                                                           */
/* -------------------------------------------------------------------------- */

int ljyd_course_is_synced(void)
{
    return s_store.synced ? 1 : 0;
}

uint16_t ljyd_course_entry_count(void)
{
    return s_store.entry_count;
}

int ljyd_course_get_day_entries(int weekday, ljyd_course_entry_t *out, int max)
{
    int count = 0;
    int total = (int)s_store.entry_count;

    if (out == NULL || max <= 0) {
        return -QM_EINVAL;
    }

    if (total > LJYD_COURSE_MAX_ENTRIES) {
        total = LJYD_COURSE_MAX_ENTRIES;
    }

    for (int i = 0; i < total && count < max; i++) {
        if (s_store.entries[i].weekday == (uint8_t)weekday) {
            out[count++] = s_store.entries[i];
        }
    }

    /* 按节次升序排序（插入排序） */
    for (int i = 1; i < count; i++) {
        ljyd_course_entry_t key = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].period > key.period) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = key;
    }
    return count;
}

int ljyd_course_get_today_weekday(int *out_weekday)
{
    if (out_weekday == NULL) {
        return -QM_EINVAL;
    }

    qm_time_t t = qm_time(NULL);
    struct qm_tm *tm = qm_localtime(&t);
    if (tm == NULL) {
        QM_LOGW(LOG_TAG, "localtime unavailable, rtc not ready");
        return -QM_EINIT;
    }

    /* qm_tm.tm_wday: 0 = 周日；本模块约定 1 = 周一 ~ 7 = 周日 */
    int weekday = (tm->tm_wday == 0) ? 7 : tm->tm_wday;
    if (weekday < 1 || weekday > 7) {
        return -QM_EINIT;
    }

    *out_weekday = weekday;
    return QM_EOK;
}

/**
 * @brief 本地时间是否已可用。
 *
 * @note 未对时（1970-01-01）时 tm_wday 虽然可算，但周几不可信；
 *       本产品未开 SNTP，播报前必须校验年份。
 */
static int ljyd_course_local_time_valid(void)
{
    qm_time_t now = qm_time(NULL);
    struct qm_tm *tm = qm_localtime(&now);

    return (tm != NULL && (tm->tm_year + 1900) >= LJYD_COURSE_MIN_VALID_YEAR) ? 1 : 0;
}

int ljyd_course_take_update_flag(void)
{
    int updated = s_updated;
    s_updated = 0;
    return updated;
}

void ljyd_course_clear_configs(void)
{
    int ret;

    ljyd_course_store_reset();
    s_updated = 0;
    ret = qm_kv_del(LJYD_COURSE_KV_KEY);
    QM_LOGI(LOG_TAG, "cleared: key=%s kv_del ret=%d", LJYD_COURSE_KV_KEY, ret);
}

/**
 * @brief 上报今日周几（siid=10 / piid=1），触发云端 TTS 播报当天课程。
 */
static int ljyd_course_report_weekday(uint8_t weekday)
{
    qm_spec_property_operation_t *op;
    qm_spec_property_t *prop;
    int ret;

    if (s_iot_handle == NULL) {
        return -QM_EINVAL;
    }

    op = qm_spec_property_operation_creat();
    if (op == NULL) {
        QM_LOGE(LOG_TAG, "tts req fail: property_operation creat NULL");
        return -QM_ENOMEM;
    }

    prop = qm_spec_property_creat();
    if (prop == NULL) {
        QM_LOGE(LOG_TAG, "tts req fail: property creat NULL");
        (void)qm_spec_property_operation_delete(op);
        return -QM_ENOMEM;
    }

    qm_spec_property_pack_xiid(prop, LJYD_COURSE_TTS_SIID, LJYD_COURSE_TTS_PIID);
    qm_spec_property_pack_number(prop, (int)weekday);
    prop->did = qm_iot_did_get(s_iot_handle);
    qm_spec_property_add(op, prop);

    QM_LOGI(LOG_TAG, "tts req: siid=%d piid=%d weekday=%u did=%u",
            LJYD_COURSE_TTS_SIID, LJYD_COURSE_TTS_PIID, (unsigned)weekday,
            (unsigned)prop->did);

    ret = (int)qm_iot_property_report(s_iot_handle, op);
    if (ret != QM_EOK) {
        (void)qm_spec_property_operation_delete(op);
        QM_LOGE(LOG_TAG, "tts req failed ret=%d", ret);
        return ret;
    }
    return QM_EOK;
}

void ljyd_course_broadcast_today(void)
{
    int weekday = -1;

    QM_LOGI(LOG_TAG, "broadcast_today enter: synced=%d entries=%u",
            ljyd_course_is_synced(), (unsigned)s_store.entry_count);

    if (!ljyd_course_is_synced()) {
        QM_LOGW(LOG_TAG, "NOT synced, skip broadcast");
        return;
    }
    if (s_iot_handle == NULL) {
        QM_LOGW(LOG_TAG, "cloud not ready, skip broadcast");
        return;
    }
    if (!ljyd_course_local_time_valid()) {
        QM_LOGW(LOG_TAG, "local time not synced, skip broadcast");
        return;
    }

    if (ljyd_course_get_today_weekday(&weekday) != QM_EOK) {
        QM_LOGW(LOG_TAG, "invalid weekday, skip broadcast");
        return;
    }

    QM_LOGI(LOG_TAG, "weekday=%d", weekday);
    (void)ljyd_course_report_weekday((uint8_t)weekday);
}
