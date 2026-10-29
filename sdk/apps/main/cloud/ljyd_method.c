#include "ljyd_method.h"
#include "agent_app.h"
#include "qm_config.h"

#if defined(CONFIG_LJYD_PLAYBACK_SUPPORT) && CONFIG_LJYD_PLAYBACK_SUPPORT
#include "playback.h"
#endif

#include "cJSON.h"
#include "qm.h"
#include "qm_iot_api.h"
#include "qm_iot_mqtt.h"

#include <stdio.h>
#include <string.h>

#define LOG_TAG "method"

#define LJYD_METHOD_NEED_REPLY (1u << 0)

typedef struct {
    cJSON *root;
    cJSON *params;
    const char *method;
    const char *submethod;
    uint32_t msg_id;
    int code;
    int has_code;
} ljyd_method_ctx_t;

typedef int (*ljyd_method_fn)(const ljyd_method_ctx_t *ctx);

typedef struct {
    const char *method;
    const char *submethod; /* NULL：这个 method 下任意 subMethod */
    ljyd_method_fn action;
    unsigned flags;
} ljyd_method_entry_t;

static void *s_iot;

static int action_chat_quit(const ljyd_method_ctx_t *ctx);
#if defined(CONFIG_LJYD_PLAYBACK_SUPPORT) && CONFIG_LJYD_PLAYBACK_SUPPORT
static int action_player_play(const ljyd_method_ctx_t *ctx);
#endif

static const ljyd_method_entry_t s_table[] = {
    {"chat", "quit", action_chat_quit, LJYD_METHOD_NEED_REPLY},
#if defined(CONFIG_LJYD_PLAYBACK_SUPPORT) && CONFIG_LJYD_PLAYBACK_SUPPORT
    {"player", "play", action_player_play, LJYD_METHOD_NEED_REPLY},
#endif
};

static int params_source_is_local(const cJSON *params)
{
    const cJSON *src;

    if (params == NULL) {
        return 0;
    }
    src = cJSON_GetObjectItemCaseSensitive(params, "source");
    return (cJSON_IsString(src) && src->valuestring != NULL &&
            strcmp(src->valuestring, "local") == 0) ? 1 : 0;
}

static int json_read_int(const cJSON *obj, const char *key, int *out)
{
    const cJSON *item;

    if (obj == NULL || key == NULL || out == NULL) {
        return 0;
    }
    item = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsNumber(item)) {
        *out = (int)item->valuedouble;
        return 1;
    }
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        item->valuestring[0] != '\0') {
        return sscanf(item->valuestring, "%d", out) == 1;
    }
    return 0;
}

#if defined(CONFIG_LJYD_PLAYBACK_SUPPORT) && CONFIG_LJYD_PLAYBACK_SUPPORT
static int json_read_u32(const cJSON *obj, const char *key, uint32_t *out)
{
    int value;

    if (out == NULL || !json_read_int(obj, key, &value) || value < 0) {
        return 0;
    }
    *out = (uint32_t)value;
    return 1;
}
#endif

static const ljyd_method_entry_t *table_find(const char *method, const char *submethod)
{
    size_t i;
    const ljyd_method_entry_t *wild = NULL;

    if (method == NULL || method[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < QM_ARRAY_SIZE(s_table); i++) {
        const ljyd_method_entry_t *e = &s_table[i];

        if (strcmp(e->method, method) != 0) {
            continue;
        }
        if (e->submethod == NULL) {
            if (wild == NULL) {
                wild = e;
            }
            continue;
        }
        if (submethod != NULL && submethod[0] != '\0' &&
            strcmp(e->submethod, submethod) == 0) {
            return e;
        }
    }
    return wild;
}

static int method_needs_reply(const char *method)
{
    size_t i;

    for (i = 0; i < QM_ARRAY_SIZE(s_table); i++) {
        if (strcmp(s_table[i].method, method) == 0 &&
            (s_table[i].flags & LJYD_METHOD_NEED_REPLY)) {
            return 1;
        }
    }
    return 0;
}

static int action_chat_quit(const ljyd_method_ctx_t *ctx)
{
    const cJSON *it;
    const char *speak = NULL;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (params_source_is_local(ctx->params)) {
        QM_LOGI(LOG_TAG, "chat quit source=local ignore");
        return 0;
    }
    if (ctx->params != NULL) {
        it = cJSON_GetObjectItemCaseSensitive(ctx->params, "speakText");
        if (cJSON_IsString(it) && it->valuestring != NULL &&
            it->valuestring[0] != '\0') {
            speak = it->valuestring;
        }
    }
    QM_LOGI(LOG_TAG, "chat quit speak=%s", speak != NULL ? speak : "");
    return agent_app_leave();
}

#if defined(CONFIG_LJYD_PLAYBACK_SUPPORT) && CONFIG_LJYD_PLAYBACK_SUPPORT
static int action_player_play(const ljyd_method_ctx_t *ctx)
{
    playback_cloud_play_request_t request;
    const cJSON *item;

    if (ctx == NULL || !cJSON_IsObject(ctx->params)) {
        return -QM_EINVAL;
    }
    memset(&request, 0, sizeof(request));
    item = cJSON_GetObjectItemCaseSensitive(ctx->params, "url");
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        item->valuestring[0] == '\0') {
        return -QM_EINVAL;
    }
    request.url = item->valuestring;

    item = cJSON_GetObjectItemCaseSensitive(ctx->params, "guideWords");
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        item->valuestring[0] != '\0') {
        request.guide_words = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(ctx->params, "audioType");
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        item->valuestring[0] != '\0') {
        request.audio_type = item->valuestring;
    } else {
        request.audio_type = "pcm";
    }
    item = cJSON_GetObjectItemCaseSensitive(ctx->params, "title");
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        item->valuestring[0] != '\0') {
        request.title = item->valuestring;
    }
    (void)json_read_u32(ctx->params, "fileSize", &request.file_size);
    (void)json_read_u32(ctx->params, "totalDuration", &request.duration_s);
    QM_LOGI(LOG_TAG, "player play url=%s type=%s size=%u duration=%u guide=%u",
            request.url,
            request.audio_type,
            (unsigned)request.file_size,
            (unsigned)request.duration_s,
            request.guide_words != NULL ? 1U : 0U);
    return playback_submit_cloud_play(&request);
}
#endif

static void method_down_dispatch(void *handle, const qm_iot_mqtt_recv_t *packet, void *userdata)
{
    const char *method = (const char *)userdata;
    cJSON *root = NULL;
    cJSON *item;
    ljyd_method_ctx_t ctx;
    const ljyd_method_entry_t *entry;
    char submethod[24];
    int code;
    int ret;

    (void)handle;
    memset(&ctx, 0, sizeof(ctx));
    ctx.code = -1;
    ctx.method = method;
    ctx.submethod = "";
    submethod[0] = '\0';

    if (s_iot == NULL || method == NULL || packet == NULL ||
        packet->type != QM_IOT_MQTTRECV_PUB ||
        packet->data.pub.payload == NULL ||
        packet->data.pub.payload_len == 0) {
        QM_LOGW(LOG_TAG, "down ignore invalid packet method=%s",
                method != NULL ? method : "");
        return;
    }

    /* MQTT payloads are length-delimited and are not guaranteed to be NUL-terminated. */
    QM_LOGI(LOG_TAG,
            "MQTT downlink method=%s topic=%.*s payload_len=%u payload=%.*s",
            method,
            (int)packet->data.pub.topic_len,
            packet->data.pub.topic != NULL ? packet->data.pub.topic : "",
            (unsigned)packet->data.pub.payload_len,
            (int)packet->data.pub.payload_len,
            (const char *)packet->data.pub.payload);
    QM_HEX_LOGI(LOG_TAG, "MQTT downlink payload hex",
                packet->data.pub.payload, packet->data.pub.payload_len);

    root = cJSON_ParseWithLength((const char *)packet->data.pub.payload,
                                 packet->data.pub.payload_len);
    if (root == NULL) {
        QM_LOGW(LOG_TAG, "down parse failed method=%s", method);
        return;
    }

    ctx.root = root;
    item = cJSON_GetObjectItem(root, "id");
    if (cJSON_IsNumber(item)) {
        ctx.msg_id = (uint32_t)item->valuedouble;
    }

    item = cJSON_GetObjectItem(root, "subMethod");
    if (!cJSON_IsString(item) || item->valuestring == NULL) {
        item = cJSON_GetObjectItem(root, "submethod");
    }
    if (cJSON_IsString(item) && item->valuestring != NULL &&
        item->valuestring[0] != '\0') {
        strncpy(submethod, item->valuestring, sizeof(submethod) - 1);
        submethod[sizeof(submethod) - 1] = '\0';
        ctx.submethod = submethod;
    }

    ctx.params = cJSON_GetObjectItem(root, "params");
    if (json_read_int(root, "code", &ctx.code) ||
        json_read_int(ctx.params, "code", &ctx.code)) {
        ctx.has_code = 1;
    }

    entry = table_find(method, ctx.submethod);
    if (entry == NULL || entry->action == NULL) {
        QM_LOGW(LOG_TAG, "down no match method=%s subMethod=%s msg_id=%u",
                method, ctx.submethod[0] != '\0' ? ctx.submethod : "",
                (unsigned)ctx.msg_id);
        if (method_needs_reply(method)) {
            (void)qm_iot_down_reply_ex(s_iot, (char *)method,
                                       submethod[0] != '\0' ? submethod : NULL,
                                       ctx.msg_id, -QM_EINVAL, NULL, 0);
        }
        cJSON_Delete(root);
        return;
    }

    code = entry->action(&ctx);
    if (entry->flags & LJYD_METHOD_NEED_REPLY) {
        ret = qm_iot_down_reply_ex(s_iot, (char *)method,
                                   submethod[0] != '\0' ? submethod : NULL,
                                   ctx.msg_id, code, NULL, 0);
        QM_LOGI(LOG_TAG, "down method=%s subMethod=%s msg_id=%u code=%d reply=%d",
                method, ctx.submethod[0] != '\0' ? ctx.submethod : "",
                (unsigned)ctx.msg_id, code, ret);
    }

    cJSON_Delete(root);
}

int ljyd_method_on_connected(void *iot_handle)
{
    size_t i;
    size_t j;
    int ret;

    if (iot_handle == NULL) {
        return -QM_EINVAL;
    }
    s_iot = iot_handle;

    for (i = 0; i < QM_ARRAY_SIZE(s_table); i++) {
        const char *method = s_table[i].method;
        int dup = 0;

        for (j = 0; j < i; j++) {
            if (strcmp(s_table[j].method, method) == 0) {
                dup = 1;
                break;
            }
        }
        if (dup) {
            continue;
        }
        ret = qm_iot_down_register(s_iot, (char *)method,
                                   method_down_dispatch, (void *)method);
        if (ret != QM_EOK) {
            QM_LOGW(LOG_TAG, "down reg %s failed ret=%d", method, ret);
            return ret;
        }
        QM_LOGI(LOG_TAG, "down registered: %s", method);
    }
    return QM_EOK;
}

int ljyd_method_up(const char *method, const char *submethod, const char *params_json)
{
    static const char local_params[] = "{\"source\":\"local\"}";
    const char *payload = params_json;
    int ret;

    if (method == NULL || method[0] == '\0' ||
        submethod == NULL || submethod[0] == '\0') {
        return -QM_EINVAL;
    }
    if (s_iot == NULL) {
        QM_LOGW(LOG_TAG, "iot handle not ready, skip %s/%s", method, submethod);
        return -QM_EINVAL;
    }
    if (payload == NULL || payload[0] == '\0') {
        payload = local_params;
    }
    ret = qm_iot_up_report_ex(s_iot, (char *)method, (char *)submethod, 0,
                              (char *)payload, (int)strlen(payload));
    if (ret != QM_EOK) {
        QM_LOGW(LOG_TAG, "%s %s up failed ret=%d", method, submethod, ret);
        return ret;
    }
    QM_LOGI(LOG_TAG, "%s %s up ok", method, submethod);
    return QM_EOK;
}
