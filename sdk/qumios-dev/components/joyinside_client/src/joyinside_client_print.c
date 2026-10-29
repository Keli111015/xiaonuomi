/**
 * @file joyinside_client_print.c
 * @brief PRINT protocol validation, request normalization, and uplink encoding.
 */

#include "joyinside_client_print.h"

#include "cJSON.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_time.h"
#include "qm_utils_uuid.h"
#include "joyinside_client_log.h"
#include "joyinside_client_internal.h"

#define LOG_TAG "joyinside_print"

#define JOYINSIDE_PRINT_STRCMP_EQUAL 0
#define JOYINSIDE_PRINT_RESPONSE_CODE_OK 200
#define JOYINSIDE_PRINT_UUID36_LEN 36
#define JOYINSIDE_PRINT_UUID36_BUFFER_LEN (JOYINSIDE_PRINT_UUID36_LEN + 1U)
#define JOYINSIDE_PRINT_CSTR_FIRST_INDEX 0U
#define JOYINSIDE_PRINT_CSTR_TERMINATOR_CHAR '\0'
#define JOYINSIDE_PRINT_MSEC_PER_SEC 1000ULL
#define JOYINSIDE_PRINT_USEC_PER_MSEC 1000ULL
static qm_bool print_text_empty(const char *text)
{
    return text == NULL ||
           text[JOYINSIDE_PRINT_CSTR_FIRST_INDEX] == JOYINSIDE_PRINT_CSTR_TERMINATOR_CHAR;
}

static qm_bool print_text_equals(const char *left, const char *right)
{
    if (left == NULL || right == NULL) {
        return QM_FALSE;
    }
    return qm_strcmp(left, right) == JOYINSIDE_PRINT_STRCMP_EQUAL ? QM_TRUE : QM_FALSE;
}

static const char *print_json_string(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return NULL;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static const cJSON *print_json_child(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return NULL;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return item;
}

static qm_bool print_json_has_string(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsString(item) && !print_text_empty(item->valuestring);
}

static qm_bool print_json_has_item(const cJSON *object, const char *name)
{
    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }
    return cJSON_GetObjectItemCaseSensitive(object, name) != NULL ? QM_TRUE : QM_FALSE;
}

static qm_bool print_json_has_number(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsNumber(item) ? QM_TRUE : QM_FALSE;
}

static qm_bool print_json_has_array(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsArray(item) ? QM_TRUE : QM_FALSE;
}

static qm_bool print_json_has_object(const cJSON *object, const char *name)
{
    const cJSON *item;

    if (object == NULL || name == NULL) {
        return QM_FALSE;
    }
    item = cJSON_GetObjectItemCaseSensitive(object, name);
    return cJSON_IsObject(item) ? QM_TRUE : QM_FALSE;
}

static uint64_t print_timestamp_ms(void)
{
    struct qm_timeval tv;

    if (qm_gettimeofday(&tv, NULL) != QM_EOK) {
        return 0ULL;
    }
    return ((uint64_t)tv.tv_sec * JOYINSIDE_PRINT_MSEC_PER_SEC) +
           ((uint64_t)tv.tv_usec / JOYINSIDE_PRINT_USEC_PER_MSEC);
}

static joyinside_print_handler_context_t *print_handler_find(joyinside_client_handle_t handle)
{
    joyinside_client_extensions_t *ext = joyinside_client_extensions(handle);
    if (!ext) return NULL;
    return ext->print.handle ? &ext->print : NULL;
}

static joyinside_print_handler_context_t *print_handler_alloc(joyinside_client_handle_t handle)
{
    joyinside_client_extensions_t *ext = joyinside_client_extensions(handle);
    if (!ext) return NULL;
    ext->print.handle = handle;
    return &ext->print;
}

static void print_handler_clear(joyinside_print_handler_context_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    qm_memset(ctx, 0, sizeof(*ctx));
}

static int print_parse_event_data(const char *event_data_json, cJSON **event_data)
{
    if (event_data == NULL) {
        return -QM_EINVAL;
    }
    *event_data = NULL;

    if (!print_text_empty(event_data_json)) {
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

static int print_validate_uplink_event_data(const char *event_type, const cJSON *event_data)
{
    if (event_data == NULL) {
        return -QM_EINVAL;
    }

    if (print_text_equals(event_type, JOYINSIDE_CLIENT_PRINT_EVENT_STATUS)) {
        return print_json_has_string(event_data, "battery") &&
               print_json_has_array(event_data, "printerStatus") ? QM_EOK : -QM_EINVAL;
    }
    if (print_text_equals(event_type, JOYINSIDE_CLIENT_PRINT_EVENT_SUCCESS)) {
        return print_json_has_string(event_data, "printTaskId") &&
               print_json_has_number(event_data, "printSuccess") ? QM_EOK : -QM_EINVAL;
    }
    if (print_text_equals(event_type, JOYINSIDE_CLIENT_PRINT_EVENT_IMAGE)) {
        return print_json_has_string(event_data, "printTaskId") &&
               print_json_has_string(event_data, "imageId") ? QM_EOK : -QM_EINVAL;
    }
    return -QM_EINVAL;
}

static int print_validate_downlink_root(const cJSON *root)
{
    if (root == NULL) {
        return -QM_EINVAL;
    }
    return print_json_has_item(root, "code") &&
           print_json_has_item(root, "mid") &&
           print_json_has_item(root, "uid") &&
           print_json_has_number(root, "t") &&
           print_json_has_string(root, "contentType") &&
           print_json_has_object(root, "content") ? QM_EOK : -QM_EINVAL;
}

static int print_validate_downlink_event_data(const char *event_type, const cJSON *event_data)
{
    if (print_text_equals(event_type, JOYINSIDE_CLIENT_PRINT_EVENT_START)) {
        if (event_data == NULL) {
            return -QM_EINVAL;
        }
        return print_json_has_string(event_data, "printTaskId") &&
               print_json_has_string(event_data, "printThickness") &&
               print_json_has_string(event_data, "paperType") &&
               print_json_has_string(event_data, "printDirection") &&
               print_json_has_array(event_data, "imgUrlList") ? QM_EOK : -QM_EINVAL;
    }
    if (print_text_equals(event_type, JOYINSIDE_CLIENT_PRINT_EVENT_IMAGE_DISPLAY)) {
        if (event_data == NULL) {
            return -QM_EINVAL;
        }
        return print_json_has_string(event_data, "printTaskId") &&
               print_json_has_string(event_data, "imgUrl") ? QM_EOK : -QM_EINVAL;
    }
    if (print_text_equals(event_type, JOYINSIDE_CLIENT_PRINT_EVENT_STOP)) {
        if (event_data == NULL) {
            return -QM_EINVAL;
        }
        return print_json_has_string(event_data, "printTaskId") ? QM_EOK : -QM_EINVAL;
    }
    if (print_text_equals(event_type, JOYINSIDE_CLIENT_PRINT_EVENT_CONFIG)) {
        /* Partial update supported: fields absent keep the previous value. */
        return cJSON_IsObject(event_data) ? QM_EOK : -QM_EINVAL;
    }
    if (print_text_equals(event_type, JOYINSIDE_CLIENT_PRINT_EVENT_STATUS_QUERY)) {
        return (event_data == NULL || cJSON_IsNull(event_data)) ? QM_EOK : -QM_EINVAL;
    }
    return -QM_EINVAL;
}

/**
 * @brief Normalize a validated PRINT downlink without transferring JSON ownership.
 *
 * @par 中文说明
 * 在不转移 JSON 所有权的前提下标准化已校验的 PRINT 下行请求。
 *
 * Every string and cJSON field placed in @p request remains borrowed from the
 * current downlink root and is valid only for the command callback.
 */
static void print_fill_request(joyinside_client_print_request_t *request,
                               joyinside_client_handle_t handle,
                               const cJSON *root,
                               const cJSON *content,
                               const cJSON *event_data)
{
    const cJSON *item;

    if (request == NULL || content == NULL) {
        return;
    }
    /* Request fields borrow JSON nodes owned by the current downlink event.
     * / 请求字段借用当前下行事件持有的 JSON 节点。 */
    qm_memset(request, 0, sizeof(*request));
    request->handle = handle;
    request->content = content;
    request->event_data = event_data;
    request->round_id = print_json_string(content, "roundId");
    request->event_type = print_json_string(content, "eventType");
    if (root) {
        request->meta.mid = print_json_string(root, "mid");
        request->meta.uid = print_json_string(root, "uid");
        request->meta.request_id = print_json_string(root, "requestId");
        request->meta.content_type = print_json_string(root, "contentType");
        request->meta.msg = print_json_string(root, "msg");
        item = print_json_child(root, "code");
        if (cJSON_IsNumber(item)) {
            request->meta.code = item->valueint;
        } else if (cJSON_IsString(item)) {
            uint32_t code;
            uint32_t len = qm_strlen(item->valuestring);
            if (len && len <= UINT8_MAX &&
                qm_str2uint(item->valuestring, (uint8_t)len, &code) == QM_EOK && code <= INT32_MAX) {
                request->meta.code = (int)code;
            }
        }
        item = print_json_child(root, "t");
        if (cJSON_IsNumber(item) && item->valuedouble >= 0.0 &&
            item->valuedouble < (double)UINT64_MAX) {
            request->meta.timestamp_ms = (uint64_t)item->valuedouble;
        }
    }
    if (event_data == NULL || !cJSON_IsObject(event_data)) {
        return;
    }
    request->print_task_id = print_json_string(event_data, "printTaskId");
    request->print_thickness = print_json_string(event_data, "printThickness");
    request->paper_type = print_json_string(event_data, "paperType");
    request->print_direction = print_json_string(event_data, "printDirection");
    request->print_mode = print_json_string(event_data, "printMode");
    request->img_url = print_json_string(event_data, "imgUrl");
    request->image_id = print_json_string(event_data, "imageId");
    item = print_json_child(event_data, "imgUrlList");
    if (cJSON_IsArray(item)) {
        request->img_url_list = item;
        cJSON *entry;
        uint32_t count = 0U;
        cJSON_ArrayForEach(entry, (cJSON *)item) {
            count++;
        }
        request->img_count = count;
    }
}

static int print_dispatch_request(const joyinside_client_print_request_t *request,
                                  const joyinside_print_handler_context_t *ctx)
{
    if (request == NULL || ctx == NULL || ctx->config.on_command == NULL) {
        return -QM_EINVAL;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK,
            "print downlink event=%s task=%s uid=%s",
            request->event_type ? request->event_type : "",
            request->print_task_id ? request->print_task_id : "",
            request->meta.uid ? request->meta.uid : "");
    return ctx->config.on_command(request, ctx->config.user_context);
}

int joyinside_client_print_register(joyinside_client_handle_t handle,
                                    const joyinside_client_print_config_t *config)
{
    joyinside_print_handler_context_t *ctx;

    if (handle == NULL || config == NULL || config->on_command == NULL) {
        return -QM_EINVAL;
    }

    if (joyinside_client_running(handle)) return -QM_EBUSY;
    joyinside_client_print_unregister(handle);
    ctx = print_handler_alloc(handle);
    if (ctx == NULL) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "print handler context exhausted");
        return -QM_ENOMEM;
    }

    ctx->config = *config;
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "print handler registered");
    return QM_EOK;
}

int joyinside_client_print_unregister(joyinside_client_handle_t handle)
{
    joyinside_print_handler_context_t *ctx;

    if (!handle) return -QM_EINVAL;
    if (joyinside_client_running(handle)) return -QM_EBUSY;
    ctx = print_handler_find(handle);
    if (ctx == NULL) {
        return QM_EOK;
    }
    print_handler_clear(ctx);
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "print handler unregistered");
    return QM_EOK;
}

int joyinside_client_print_handle_message(joyinside_client_handle_t handle,
                                          const struct cJSON *root)
{
    joyinside_print_handler_context_t *ctx;
    joyinside_client_print_request_t request;
    const cJSON *content;
    const cJSON *event_data;
    const char *content_type;
    const char *event_type;
    int ret;

    ctx = print_handler_find(handle);
    if (ctx == NULL || ctx->config.on_command == NULL) {
        return QM_EOK;
    }
    if (root == NULL) {
        return -QM_EINVAL;
    }

    content_type = print_json_string((const cJSON *)root, "contentType");
    if (!print_text_equals(content_type, JOYINSIDE_CLIENT_PRINT_CONTENT_TYPE)) {
        return QM_EOK;
    }

    content = print_json_child((const cJSON *)root, "content");
    if (content == NULL || !cJSON_IsObject(content)) {
        return -QM_EINVAL;
    }
    event_type = print_json_string(content, "eventType");
    if (print_text_empty(event_type)) {
        return -QM_EINVAL;
    }
    event_data = print_json_child(content, "eventData");

    ret = print_validate_downlink_root((const cJSON *)root);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = print_validate_downlink_event_data(event_type, event_data);
    if (ret != QM_EOK) {
        return ret;
    }

    print_fill_request(&request, handle, (const cJSON *)root, content, event_data);
    return print_dispatch_request(&request, ctx);
}

int joyinside_client_print_send_event(joyinside_client_handle_t handle,
                                      const char *event_type,
                                      const char *event_data_json,
                                      uint32_t timeout_ms)
{
    char mid[JOYINSIDE_PRINT_UUID36_BUFFER_LEN];
    cJSON *root = NULL;
    cJSON *content = NULL;
    cJSON *event_data = NULL;
    char *json = NULL;
    int ret = QM_EOK;

    if (handle == NULL || print_text_empty(event_type)) {
        return -QM_EINVAL;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send print event type=%s timeout=%lums",
            event_type, (unsigned long)timeout_ms);

    ret = print_parse_event_data(event_data_json, &event_data);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = print_validate_uplink_event_data(event_type, event_data);
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
        !cJSON_AddStringToObject(root, "contentType", JOYINSIDE_CLIENT_PRINT_CONTENT_TYPE) ||
        !cJSON_AddNumberToObject(root, "t", (double)print_timestamp_ms()) ||
        !cJSON_AddStringToObject(content, "eventType", event_type) ||
        !cJSON_AddItemToObject(content, "eventData", event_data)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    event_data = NULL;

    if (joyinside_client_get_uid(handle) &&
        joyinside_client_get_uid(handle)[JOYINSIDE_PRINT_CSTR_FIRST_INDEX] != JOYINSIDE_PRINT_CSTR_TERMINATOR_CHAR) {
        /* Handbook 9: PRINT uplink requires uid in the message body. */
        if (!cJSON_AddStringToObject(root, "uid", joyinside_client_get_uid(handle))) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    } else {
        JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
                "print uplink has no uid, handbook requires uid for PRINT");
    }

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

    ret = joyinside_client_send_text(handle, json, timeout_ms);
    if (ret < QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "send print event failed type=%s ret=%d", event_type, ret);
    }

cleanup:
    cJSON_Delete(root);
    cJSON_Delete(content);
    cJSON_Delete(event_data);
    cJSON_free(json);
    return ret;
}
