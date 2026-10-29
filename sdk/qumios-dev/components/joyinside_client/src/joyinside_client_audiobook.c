/**
 * @file joyinside_client_audiobook.c
 * @brief AUDIO_BOOK validation, dispatch, and protocol heartbeat support.
 */

#include "joyinside_client_audiobook.h"

#include "cJSON.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_time.h"
#include "qm_utils_uuid.h"
#include "joyinside_client_log.h"
#include "joyinside_client_internal.h"

#define LOG_TAG "joyinside_audiobook"

#define JOYINSIDE_AUDIOBOOK_DEFAULT_CONTENT_TYPE "AUDIO_BOOK"
#define JOYINSIDE_AUDIOBOOK_STRCMP_EQUAL 0
#define JOYINSIDE_AUDIOBOOK_INVALID_INT (-1)
#define JOYINSIDE_AUDIOBOOK_RESPONSE_CODE_OK 200
#define JOYINSIDE_AUDIOBOOK_CODE_ABSENT JOYINSIDE_AUDIOBOOK_RESPONSE_CODE_OK
#define JOYINSIDE_AUDIOBOOK_UUID36_LEN 36
#define JOYINSIDE_AUDIOBOOK_UUID36_BUFFER_LEN (JOYINSIDE_AUDIOBOOK_UUID36_LEN + 1U)
#define JOYINSIDE_AUDIOBOOK_CSTR_FIRST_INDEX 0U
#define JOYINSIDE_AUDIOBOOK_CSTR_TERMINATOR_CHAR '\0'
#define JOYINSIDE_AUDIOBOOK_MSEC_PER_SEC 1000ULL
#define JOYINSIDE_AUDIOBOOK_USEC_PER_MSEC 1000ULL

static qm_bool text_empty(const char *text)
{
    return text == NULL ||
           text[JOYINSIDE_AUDIOBOOK_CSTR_FIRST_INDEX] == JOYINSIDE_AUDIOBOOK_CSTR_TERMINATOR_CHAR;
}

static qm_bool text_equals(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return QM_FALSE;
    }

    return qm_strcmp(left, right) == JOYINSIDE_AUDIOBOOK_STRCMP_EQUAL ? QM_TRUE : QM_FALSE;
}

static const char *json_string_value(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return NULL;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static const cJSON *json_child_object(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return NULL;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsObject(item) ? item : NULL;
}

static const cJSON *json_child_value(const cJSON *object, const char *name)
{
    if (object == NULL || name == NULL) {
        return NULL;
    }

    return cJSON_GetObjectItemCaseSensitive(object, name);
}

static qm_bool json_uint64_value(const cJSON *object, const char *name, uint64_t *value)
{
    const cJSON *item;

    if (object == NULL || name == NULL || value == NULL) {
        return QM_FALSE;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(item) && item->valuedouble >= 0.0 &&
        item->valuedouble < (double)UINT64_MAX) {
        *value = (uint64_t)item->valuedouble;
        return (double)*value == item->valuedouble;
    }
    return QM_FALSE;
}

static qm_bool json_has_string(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && !text_empty(item->valuestring);
}

static qm_bool json_has_item(const cJSON *object, const char *name)
{
    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }

    return cJSON_GetObjectItemCaseSensitive(object, name) != NULL ? QM_TRUE : QM_FALSE;
}

static qm_bool json_has_number(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? QM_TRUE : QM_FALSE;
}

static qm_bool json_has_code(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ||
           (cJSON_IsString(item) && !text_empty(item->valuestring)) ? QM_TRUE : QM_FALSE;
}

static qm_bool json_has_bool(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsBool(item) ? QM_TRUE : QM_FALSE;
}

static qm_bool json_bool_value(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsBool(item) && cJSON_IsTrue(item) ? QM_TRUE : QM_FALSE;
}

static qm_bool json_has_object(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsObject(item) ? QM_TRUE : QM_FALSE;
}

static int json_code_value(const cJSON *object, const char *name)
{
    const cJSON *item;
    uint32_t parsed;
    uint32_t len;

    if (object == NULL || name == NULL) {
        return JOYINSIDE_AUDIOBOOK_CODE_ABSENT;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(item)) {
        return item->valueint;
    }
    if (cJSON_IsString(item) && !text_empty(item->valuestring)) {
        len = qm_strlen(item->valuestring);
        if (len > 0U && len <= UINT8_MAX &&
            qm_str2uint(item->valuestring, (uint8_t)len, &parsed) == QM_EOK) {
            return (int)parsed;
        }
    }
    return JOYINSIDE_AUDIOBOOK_CODE_ABSENT;
}

static uint64_t audiobook_timestamp_ms(void)
{
    struct qm_timeval tv;

    if (qm_gettimeofday(&tv, NULL) != QM_EOK) {
        return 0ULL;
    }
    return ((uint64_t)tv.tv_sec * JOYINSIDE_AUDIOBOOK_MSEC_PER_SEC) +
           ((uint64_t)tv.tv_usec / JOYINSIDE_AUDIOBOOK_USEC_PER_MSEC);
}

static qm_bool allowed_uplink_event_type(const char *event_type)
{
    return text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PLAY) ||
           text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_STOP) ||
           text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PING);
}

static qm_bool downlink_pong_event_type(const char *event_type)
{
    return text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PONG);
}

static joyinside_audiobook_handler_context_t *handler_context_find(joyinside_client_handle_t handle)
{
    joyinside_client_extensions_t *ext = joyinside_client_extensions(handle);
    if (!ext) return NULL;
    return ext->audiobook.handle ? &ext->audiobook : NULL;
}

static joyinside_audiobook_handler_context_t *handler_context_alloc(joyinside_client_handle_t handle)
{
    joyinside_client_extensions_t *ext = joyinside_client_extensions(handle);
    if (!ext) return NULL;
    ext->audiobook.handle = handle;
    return &ext->audiobook;
}

static void handler_context_clear(joyinside_audiobook_handler_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }

    /* content_type is the only handler configuration member deep-copied here. */
    qm_free((void *)ctx->config.content_type);
    qm_memset(ctx, 0, sizeof(*ctx));
}

static int parse_event_data_or_empty(const char *event_data_json, cJSON **event_data)
{
    if (event_data == NULL) {
        return -QM_EINVAL;
    }
    *event_data = NULL;

    if (!text_empty(event_data_json)) {
        *event_data = cJSON_Parse(event_data_json);
        if (*event_data == NULL || !cJSON_IsObject(*event_data)) {
            cJSON_Delete(*event_data);
            *event_data = NULL;
            return -QM_EINVAL;
        }
        return QM_EOK;
    }

    *event_data = cJSON_CreateObject();
    return *event_data ? QM_EOK : -QM_ENOMEM;
}

static int validate_uplink_event_data(const char *event_type, const cJSON *event_data)
{
    if (event_data == NULL) {
        return -QM_EINVAL;
    }

    if (text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PLAY)) {
        return json_has_string(event_data, "bookId") &&
               json_has_string(event_data, "chapterId") ? QM_EOK : -QM_EINVAL;
    }
    if (text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_STOP)) {
        return json_has_string(event_data, "bookId") &&
               json_has_string(event_data, "chapterId") &&
               json_has_number(event_data, "progress") &&
               json_has_bool(event_data, "finish") ? QM_EOK : -QM_EINVAL;
    }
    if (text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PING)) {
        return QM_EOK;
    }
    return -QM_EINVAL;
}

static int validate_downlink_common_root(const cJSON *root)
{
    if (root == NULL) {
        return -QM_EINVAL;
    }

    return json_has_item(root, "mid") &&
           json_has_number(root, "t") &&
           json_has_string(root, "contentType") &&
           json_has_object(root, "content") ? QM_EOK : -QM_EINVAL;
}

static int validate_downlink_response_root(const cJSON *root)
{
    if (validate_downlink_common_root(root) != QM_EOK) {
        return -QM_EINVAL;
    }

    return json_has_code(root, "code") &&
           json_has_string(root, "msg") &&
           json_has_string(root, "requestId") ? QM_EOK : -QM_EINVAL;
}

static int validate_downlink_pong_event_data(const cJSON *event_data)
{
    if (event_data == NULL) {
        return -QM_EINVAL;
    }
    if (cJSON_IsObject(event_data) && event_data->child == NULL) {
        return QM_EOK;
    }
    if (cJSON_IsString(event_data) && text_equals(event_data->valuestring, "{}")) {
        return QM_EOK;
    }
    return -QM_EINVAL;
}

static int validate_downlink_event_data(const char *event_type, const cJSON *event_data)
{
    if (event_data == NULL) {
        return -QM_EINVAL;
    }

    if (downlink_pong_event_type(event_type)) {
        return validate_downlink_pong_event_data(event_data);
    }
    if (text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PLAY)) {
        uint64_t total, progress;
        if (!json_uint64_value(event_data, "totalLength", &total) ||
            (json_has_item(event_data, "progress") &&
             (!json_uint64_value(event_data, "progress", &progress) || progress > total)) ||
            (json_has_item(event_data, "includeTTS") && !json_has_bool(event_data, "includeTTS"))) return -QM_EINVAL;
        return json_has_string(event_data, "bookId") &&
               json_has_string(event_data, "bookName") &&
               json_has_string(event_data, "chapter") &&
               json_has_string(event_data, "chapterId") &&
               json_has_string(event_data, "chapterName") &&
               json_has_number(event_data, "totalLength") &&
               json_has_string(event_data, "audioURL") ? QM_EOK : -QM_EINVAL;
    }
    if (text_equals(event_type, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_STOP)) {
        return json_has_string(event_data, "bookId") &&
               json_has_string(event_data, "chapterId") ? QM_EOK : -QM_EINVAL;
    }
    return -QM_EINVAL;
}

static qm_bool should_handle_content(const joyinside_client_audiobook_config_t *config,
                                     const char *content_type)
{
    const char *expected_content_type;

    if (config == NULL || config->on_command == NULL) {
        return QM_FALSE;
    }

    expected_content_type = text_empty(config->content_type) ?
                            JOYINSIDE_AUDIOBOOK_DEFAULT_CONTENT_TYPE :
                            config->content_type;
    return text_equals(content_type, expected_content_type);
}

static void fill_request_from_event_data(joyinside_client_audiobook_request_t *request,
                                         const cJSON *event_data)
{
    if (request == NULL || event_data == NULL || !cJSON_IsObject(event_data)) {
        return;
    }

    request->event_data_info.book_id = json_string_value(event_data, "bookId");
    request->event_data_info.book_name = json_string_value(event_data, "bookName");
    request->event_data_info.chapter = json_string_value(event_data, "chapter");
    request->event_data_info.chapter_id = json_string_value(event_data, "chapterId");
    request->event_data_info.chapter_name = json_string_value(event_data, "chapterName");
    request->event_data_info.audio_url = json_string_value(event_data, "audioURL");
    request->event_data_info.image_url = json_string_value(event_data, "imageURL");
    request->event_data_info.progress_present = json_uint64_value(event_data, "progress", &request->event_data_info.progress);
    (void)json_uint64_value(event_data, "totalLength", &request->event_data_info.total_length);
    request->event_data_info.include_tts = json_bool_value(event_data, "includeTTS");
}

static int fill_request_from_message(joyinside_client_audiobook_request_t *request,
                                     joyinside_client_handle_t handle,
                                     const cJSON *root,
                                     const cJSON *content,
                                     const cJSON *event_data)
{
    if (request == NULL || root == NULL || content == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(request, 0, sizeof(*request));
    request->handle = handle;
    request->meta.code = json_code_value(root, "code");
    request->meta.msg = json_string_value(root, "msg");
    request->meta.content_type = json_string_value(root, "contentType");
    request->meta.mid = json_string_value(root, "mid");
    request->meta.uid = json_string_value(root, "uid");
    request->meta.request_id = json_string_value(root, "requestId");
    (void)json_uint64_value(root, "t", &request->meta.timestamp_ms);
    request->content_info.round_id = json_string_value(content, "roundId");
    request->content_info.event_type = json_string_value(content, "eventType");
    request->content = content;
    request->event_data = event_data;
    fill_request_from_event_data(request, event_data);
    return QM_EOK;
}

/**
 * @brief Deliver a validated request while its parsed JSON remains alive.
 *
 * @par 中文说明
 * 在解析 JSON 仍有效时派发已校验的请求。
 *
 * The callback receives borrowed fields and must copy any data it queues or
 * stores beyond this call. / 回调收到借用字段；若需入队或长期保存，必须自行复制。
 */
static int dispatch_request(const joyinside_client_audiobook_request_t *request,
                            const joyinside_client_audiobook_config_t *config)
{
    if (request == NULL || config == NULL || config->on_command == NULL) {
        return -QM_EINVAL;
    }

    /* The request references the parsed downlink JSON and is valid only until
     * the event path returns to its owner. */
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK,
            "audiobook downlink code=%d event_type=%s round=%s book=%s chapter=%s progress=%llu total=%llu",
            request->meta.code,
            request->content_info.event_type ? request->content_info.event_type : "",
            request->content_info.round_id ? request->content_info.round_id : "",
            request->event_data_info.book_id ? request->event_data_info.book_id : "",
            request->event_data_info.chapter_id ? request->event_data_info.chapter_id : "",
            (unsigned long long)request->event_data_info.progress,
            (unsigned long long)request->event_data_info.total_length);
    return config->on_command(request, config->user_context);
}

int joyinside_client_audiobook_register(joyinside_client_handle_t handle,
                                        const joyinside_client_audiobook_config_t *config)
{
    joyinside_audiobook_handler_context_t *ctx;

    if (handle == NULL || config == NULL || config->on_command == NULL) {
        return -QM_EINVAL;
    }

    if (joyinside_client_running(handle) || joyinside_client_extensions(handle)->media_context) return -QM_EBUSY;
    joyinside_client_audiobook_unregister(handle);
    ctx = handler_context_alloc(handle);
    if (ctx == NULL) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "audiobook handler context exhausted");
        return -QM_ENOMEM;
    }

    ctx->config = *config;
    ctx->config.content_type = NULL;
    if (config->content_type != NULL) {
        ctx->config.content_type = qm_strdup(config->content_type);
        if (ctx->config.content_type == NULL) {
            handler_context_clear(ctx);
            return -QM_ENOMEM;
        }
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "audiobook handler registered content_type=%s",
            ctx->config.content_type ? ctx->config.content_type : JOYINSIDE_AUDIOBOOK_DEFAULT_CONTENT_TYPE);
    return QM_EOK;
}

int joyinside_client_audiobook_unregister(joyinside_client_handle_t handle)
{
    joyinside_audiobook_handler_context_t *ctx;

    if (!handle) return -QM_EINVAL;
    if (joyinside_client_running(handle) || joyinside_client_extensions(handle)->media_context) return -QM_EBUSY;
    joyinside_client_audiobook_stop_ping(handle);
    ctx = handler_context_find(handle);
    if (ctx == NULL) {
        return QM_EOK;
    }

    handler_context_clear(ctx);
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "audiobook handler unregistered");
    return QM_EOK;
}

int joyinside_client_audiobook_handle_message(joyinside_client_handle_t handle,
                                              const struct cJSON *root)
{
    joyinside_audiobook_handler_context_t *ctx;
    joyinside_client_audiobook_request_t request;
    const cJSON *content;
    const cJSON *event_data;
    const char *content_type;
    const char *event_type;
    int ret;

    ctx = handler_context_find(handle);
    if (ctx == NULL || ctx->config.on_command == NULL) {
        return QM_EOK;
    }
    if (root == NULL) {
        return -QM_EINVAL;
    }

    content_type = json_string_value((const cJSON *)root, "contentType");
    if (!should_handle_content(&ctx->config, content_type)) {
        return QM_EOK;
    }

    content = json_child_object((const cJSON *)root, "content");
    if (content == NULL) {
        return -QM_EINVAL;
    }
    event_type = json_string_value(content, "eventType");
    event_data = json_child_value(content, "eventData");
    if (text_empty(event_type)) {
        return -QM_EINVAL;
    }

    ret = downlink_pong_event_type(event_type) ?
          validate_downlink_common_root((const cJSON *)root) :
          validate_downlink_response_root((const cJSON *)root);
    if (ret != QM_EOK) {
        return ret;
    }
    if (!downlink_pong_event_type(event_type) &&
        (!json_has_string(content, "roundId") || !cJSON_IsObject(event_data))) {
        return -QM_EINVAL;
    }
    if (validate_downlink_event_data(event_type, event_data) != QM_EOK) {
        return -QM_EINVAL;
    }

    ret = fill_request_from_message(&request, handle, (const cJSON *)root, content, event_data);
    return ret == QM_EOK ? dispatch_request(&request, &ctx->config) : ret;
}

int joyinside_client_audiobook_send_event(joyinside_client_handle_t handle,
                                          const char *event_type,
                                          const char *event_data_json,
                                          uint32_t timeout_ms)
{
    char mid[JOYINSIDE_AUDIOBOOK_UUID36_BUFFER_LEN];
    cJSON *root = NULL;
    cJSON *content = NULL;
    cJSON *event_data = NULL;
    char *json = NULL;
    int ret = QM_EOK;

    if (handle == NULL || text_empty(event_type) || !allowed_uplink_event_type(event_type)) {
        return -QM_EINVAL;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send audiobook event type=%s timeout=%lums",
            event_type, (unsigned long)timeout_ms);

    ret = parse_event_data_or_empty(event_data_json, &event_data);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = validate_uplink_event_data(event_type, event_data);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    root = cJSON_CreateObject();
    content = cJSON_CreateObject();
    if (root == NULL || content == NULL) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    qm_utils_random_uuid(mid);
    if (!cJSON_AddStringToObject(root, "mid", mid) ||
        !cJSON_AddStringToObject(root, "contentType", JOYINSIDE_AUDIOBOOK_DEFAULT_CONTENT_TYPE) ||
        !cJSON_AddNumberToObject(root, "t", (double)audiobook_timestamp_ms()) ||
        !cJSON_AddStringToObject(content, "eventType", event_type) ||
        !cJSON_AddItemToObject(content, "eventData", event_data)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    event_data = NULL;

    if (!cJSON_AddItemToObject(root, "content", content)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    content = NULL;

    json = cJSON_PrintUnformatted(root);
    if (json == NULL) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    /* Transport submission has no heartbeat side effect. The schedule owner
     * explicitly calls audiobook_start_ping() or audiobook_stop_ping(). */
    ret = joyinside_client_send_text(handle, json, timeout_ms);
    if (ret < QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "send audiobook event failed type=%s ret=%d", event_type, ret);
    }

cleanup:
    cJSON_Delete(root);
    cJSON_Delete(content);
    cJSON_Delete(event_data);
    cJSON_free(json);
    return ret;
}

static int audiobook_periodic_ping(joyinside_client_handle_t handle)
{
    return joyinside_client_audiobook_send_event(handle, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PING, NULL, QM_NO_WAIT);
}

int joyinside_client_audiobook_start_ping(joyinside_client_handle_t handle,
                                          uint32_t interval_ms, uint32_t timeout_ms)
{
    if (!handle) return -QM_EINVAL;
    uint32_t period = interval_ms && interval_ms < JOYINSIDE_CLIENT_AUDIOBOOK_PING_INTERVAL_MAX_MS ?
        interval_ms : JOYINSIDE_CLIENT_AUDIOBOOK_PING_INTERVAL_MAX_MS;
    int ret = joyinside_client_audiobook_send_event(handle, JOYINSIDE_CLIENT_AUDIOBOOK_EVENT_PING, NULL, timeout_ms);
    if (ret != QM_EOK) return ret;
    return joyinside_client_schedule_audiobook_ping(handle, period, audiobook_periodic_ping);
}

void joyinside_client_audiobook_stop_ping(joyinside_client_handle_t handle)
{
    (void)joyinside_client_schedule_audiobook_ping(handle, 0U, NULL);
}
