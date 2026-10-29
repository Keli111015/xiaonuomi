/**
 * @file qm_audio_cloud_protocol.c
 * @brief Default player JSON protocol codec. / 默认播放器 JSON 协议编解码器。
 */
#include "cloud/qm_audio_cloud_protocol.h"
#include "qm.h"
#include "qm_audio_priv.h"

#if CONFIG_QM_AUDIO_CLOUD_SUPPORT

#include "cJSON.h"
#include "qm_string.h"

static const cJSON *get_any(const cJSON *object, const char *first, const char *second, const char *third)
{
    const cJSON *item = NULL;

    /** @brief Accept only documented spelling aliases. / 仅接受已文档化的字段别名，保持单一类型化模型。 */
    if (object == NULL) {
        return NULL;
    }
    if (first != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, first);
    }
    if (item == NULL && second != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, second);
    }
    if (item == NULL && third != NULL) {
        item = cJSON_GetObjectItemCaseSensitive((cJSON *)object, third);
    }
    return item;
}

static qm_bool get_u32_value(const cJSON *item, uint32_t *value)
{
    if (value == NULL || item == NULL) {
        return QM_FALSE;
    }
    if (cJSON_IsNumber(item)) {
        /** JSON numbers are doubles; reject fractions and out-of-range values. / JSON 数字为双精度值；拒绝小数和超出 uint32_t 范围的值。 */
        if (item->valuedouble < 0.0 ||
            item->valuedouble > (double)UINT32_MAX) {
            return QM_FALSE;
        }
        *value = (uint32_t)item->valuedouble;
        if ((double)*value != item->valuedouble) {
            return QM_FALSE;
        }
        return QM_TRUE;
    }
    if (!cJSON_IsString(item) || item->valuestring == NULL ||
        item->valuestring[0] == '\0') {
        return QM_FALSE;
    }
    return qm_str2uint(item->valuestring,
                       (uint8_t)qm_strlen(item->valuestring), value) == QM_EOK ?
           QM_TRUE : QM_FALSE;
}

static const char *get_string(const cJSON *object, const char *first, const char *second, const char *third)
{
    const cJSON *item = get_any(object, first, second, third);

    return cJSON_IsString(item) && item->valuestring != NULL ?
           item->valuestring : NULL;
}

static qm_audio_cloud_command_type_t command_type(const char *submethod)
{
    if (submethod == NULL) {
        return QM_AUDIO_CLOUD_COMMAND_INVALID;
    }
    if (qm_strcmp(submethod, "play") == 0) return QM_AUDIO_CLOUD_COMMAND_PLAY;
    if (qm_strcmp(submethod, "stop") == 0) return QM_AUDIO_CLOUD_COMMAND_STOP;
    if (qm_strcmp(submethod, "limit") == 0) return QM_AUDIO_CLOUD_COMMAND_LIMIT;
    if (qm_strcmp(submethod, "pause") == 0) return QM_AUDIO_CLOUD_COMMAND_PAUSE;
    if (qm_strcmp(submethod, "resume") == 0) return QM_AUDIO_CLOUD_COMMAND_RESUME;
    if (qm_strcmp(submethod, "get") == 0) return QM_AUDIO_CLOUD_COMMAND_GET;
    return QM_AUDIO_CLOUD_COMMAND_INVALID;
}

static uint32_t get_u32_field(const cJSON *object, const char *first, const char *second, const char *third)
{
    uint32_t value;

    if (!get_u32_value(get_any(object, first, second, third), &value)) {
        return 0U;
    }
    return value;
}

static const char *state_name(qm_audio_control_state_t state)
{
    /* The player protocol has no separate buffering-drain state. */
    switch (state) {
        case QM_AUDIO_CONTROL_WAITING:
            return "buffering";
        case QM_AUDIO_CONTROL_PLAYING:
        case QM_AUDIO_CONTROL_DRAINING:
            return "playing";
        case QM_AUDIO_CONTROL_PAUSED:
            return "paused";
        case QM_AUDIO_CONTROL_ERROR:
            return "error";
        case QM_AUDIO_CONTROL_NEW:
        case QM_AUDIO_CONTROL_FINISHED:
        case QM_AUDIO_CONTROL_CANCELLED:
        case QM_AUDIO_CONTROL_PREEMPTED:
        default:
            return "stopped";
    }
}

static uint32_t location_type(qm_audio_source_type_t source_type)
{
    /* URI and file stay distinct; all in-process inputs share the local code. */
    switch (source_type) {
        case QM_AUDIO_SOURCE_URI:
            return 0U;
        case QM_AUDIO_SOURCE_FILE:
            return 1U;
        case QM_AUDIO_SOURCE_MEMORY:
        case QM_AUDIO_SOURCE_CUSTOM:
        default:
            return 2U;
    }
}

/**
 * @brief Decode JSON synchronously while keeping parsed strings callback-borrowed. / 同步解码 JSON，并使解析出的字符串仅在回调期间借用。
 */
int qm_audio_cloud_protocol_decode(const char *payload, uint32_t payload_len, qm_audio_cloud_protocol_command_cb_t callback, void *user_ctx)
{
    cJSON *root;
    const cJSON *params;
    const cJSON *resource_id;
    const char *submethod;
    qm_audio_cloud_command_t command;
    int ret;

    if (payload == NULL || payload_len == 0U || callback == NULL) {
        return -QM_EINVAL;
    }
    /* ParseWithLength keeps MQTT payloads valid without adding a temporary NUL. */
    root = cJSON_ParseWithLength(payload, payload_len);
    if (root == NULL || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    submethod = get_string(root, "subMethod", "submethod", "cmd");
    params = get_any(root, "params", "data", NULL);
    qm_memset(&command, 0, sizeof(command));
    command.type = command_type(submethod);
    command.submethod = submethod;
    command.msg_id = get_u32_field(root, "id", "msgId", "msg_id");
    if (command.type == QM_AUDIO_CLOUD_COMMAND_INVALID || submethod == NULL ||
        (command.type == QM_AUDIO_CLOUD_COMMAND_PLAY &&
         (params == NULL || !cJSON_IsObject(params)))) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    if (params != NULL && !cJSON_IsObject(params)) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    resource_id = get_any(params, "resourceId", "resourceID", "resource_id");
    command.has_resource_id = get_u32_value(resource_id, &command.resource_id);
    if (!command.has_resource_id) {
        command.has_resource_id = get_u32_value(get_any(params, "id", NULL, NULL),
                                                &command.resource_id);
    }
    command.uri = get_string(params, "url", "uri", NULL);
    command.title = get_string(params, "title", NULL, NULL);
    command.description = get_string(params, "description", NULL, NULL);
    command.codec = get_string(params, "audioType", "type", "codec");
    command.resource_bundle = get_string(params, "resourceBundleName",
                                         "resource_bundle_name", NULL);
    command.guide_words = get_string(params, "guideWords", "guide_words", NULL);
    command.file_size = get_u32_field(params, "fileSize", "file_size", NULL);
    command.duration_s = get_u32_field(params, "totalDuration", "duration", NULL);
    if (command.type == QM_AUDIO_CLOUD_COMMAND_PLAY && command.uri == NULL &&
        !command.has_resource_id) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    /* Command strings point into root, so visit synchronously before deletion. */
    ret = callback(&command, user_ctx);
    cJSON_Delete(root);
    return ret;
}

int qm_audio_cloud_protocol_result_code(int result)
{
    /* Keep the product protocol's reply code independent from qm_errno values. */
    if (result == QM_EOK) {
        return 0;
    }
    if (result == -QM_EINVAL || result == -QM_ENOMEM) {
        return 1;
    }
    if (result == -QM_ENOSYS) {
        return 2;
    }
    if (result == -QM_EBUSY || result == -QM_EINTR || result == -QM_EFULL) {
        return 4;
    }
    return 7;
}

char *qm_audio_cloud_protocol_encode_status(const qm_audio_cloud_protocol_status_t *status)
{
    cJSON *root;
    char *payload;

    if (status == NULL) {
        return NULL;
    }
    /* All fields are represented in one payload so reports stay schema-stable. */
    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }
    cJSON_AddStringToObject(root, "playStatus", state_name(status->state));
    cJSON_AddStringToObject(root, "title", status->title != NULL ? status->title : "");
    cJSON_AddNumberToObject(root, "totalDuration", (double)status->duration_s);
    /* The protocol uses whole seconds even though Control tracks milliseconds. */
    cJSON_AddNumberToObject(root, "currentPos", (double)(status->position_ms / 1000U));
    cJSON_AddNumberToObject(root, "fileSize", (double)status->file_size);
    cJSON_AddNumberToObject(root, "locationType", (double)location_type(status->source_type));
    cJSON_AddStringToObject(root, "type", status->codec != NULL ? status->codec : "");
    cJSON_AddNumberToObject(root, "result", (double)status->result);
    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return payload;
}

void qm_audio_cloud_protocol_free_payload(char *payload)
{
    if (payload != NULL) {
        cJSON_free(payload);
    }
}

#else

/** @brief Keep the public codec linkable when cloud support is compiled out. / 云端支持关闭时仍保持公共编解码接口可链接。 */

int qm_audio_cloud_protocol_decode(const char *payload, uint32_t payload_len, qm_audio_cloud_protocol_command_cb_t callback, void *user_ctx)
{
    QM_UNUSED_ARG(payload);
    QM_UNUSED_ARG(payload_len);
    QM_UNUSED_ARG(callback);
    QM_UNUSED_ARG(user_ctx);
    return -QM_ENOSYS;
}

int qm_audio_cloud_protocol_result_code(int result)
{
    QM_UNUSED_ARG(result);
    return 7;
}

char *qm_audio_cloud_protocol_encode_status(const qm_audio_cloud_protocol_status_t *status)
{
    QM_UNUSED_ARG(status);
    return NULL;
}

void qm_audio_cloud_protocol_free_payload(char *payload)
{
    QM_UNUSED_ARG(payload);
}

#endif
