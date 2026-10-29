/**
 * @file joyinside_client_auth.c
 * @brief Synchronous JoyInside HTTP authentication and registration helpers.
 */

#include "joyinside_client_auth.h"

#include "cJSON.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_time.h"
#include "qm_utils_hmac.h"
#include "qm_utils_uuid.h"
#include "joyinside_client_log.h"
#include "backoff_algorithm.h"

#ifndef CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE
#define CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE 2048
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS
#define CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_MAX_RETRIES
#define CONFIG_JOYINSIDE_CLIENT_MAX_RETRIES 2
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_BASE_DELAY_MS
#define CONFIG_JOYINSIDE_CLIENT_BASE_DELAY_MS 500
#endif

#define JOYINSIDE_CLIENT_TOKEN_URL "https://api.joyinside.com/auth/getToken"
#define JOYINSIDE_CLIENT_REFRESH_TOKEN_URL "https://api.joyinside.com/auth/refreshToken"
#define JOYINSIDE_CLIENT_REGISTER_URL "https://api.joyinside.com/device/register"
#define JOYINSIDE_CLIENT_BIND_USER_URL "https://api.joyinside.com/device/bindUser"
#define JOYINSIDE_CLIENT_QUERY_BOT_BIND_URL "https://api.joyinside.com/device/queryBotBind"
#define JOYINSIDE_CLIENT_UNBIND_USER_URL "https://api.joyinside.com/device/unbindUser"
#define JOYINSIDE_CLIENT_ACCESS_VERSION "V2"
#define JOYINSIDE_CLIENT_UUID32_LEN 32
#define JOYINSIDE_CLIENT_UUID32_BUFFER_LEN (JOYINSIDE_CLIENT_UUID32_LEN + 1U)
#define JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN 1U
#define JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR '\0'
#define JOYINSIDE_CLIENT_CSTR_FIRST_INDEX 0U
#define JOYINSIDE_CLIENT_SNPRINTF_COUNT_ONLY 0U
#define JOYINSIDE_CLIENT_SNPRINTF_ERROR_MAX 0
#define JOYINSIDE_CLIENT_MSEC_PER_SEC 1000ULL
#define JOYINSIDE_CLIENT_HTTP_STATUS_OK 200
#define JOYINSIDE_CLIENT_HTTP_STATUS_RETRY_MIN 500
#define JOYINSIDE_CLIENT_HTTP_RX_BUFFER_MAX 512U
#define JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO 0
#define JOYINSIDE_CLIENT_RETRY_NONE 0
#define JOYINSIDE_CLIENT_RETRY_FIRST_ATTEMPT 0
#define JOYINSIDE_CLIENT_HTTP_BODY_CALLBACK_ERROR_MIN 0
#define JOYINSIDE_CLIENT_MEMSET_ZERO 0
#define JOYINSIDE_CLIENT_RETRY_DELAY_NONE 0U
#define JOYINSIDE_CLIENT_BODY_EMPTY_LEN 0U
#define JOYINSIDE_CLIENT_TIMESTAMP_TEXT_LEN 24U
#define JOYINSIDE_CLIENT_AUTH_HEADER_NONE 0
#define JOYINSIDE_CLIENT_AUTH_HEADER_COUNT 1
#define JOYINSIDE_CLIENT_AUTH_HEADER_INDEX 0U
#define JOYINSIDE_CLIENT_RETRY_DELAY_MAX_MS 30000U

#define LOG_TAG "joyinside_client_auth"

typedef struct {
    char *data;
    uint32_t len;
    uint32_t cap;
} auth_http_body_t;

typedef int (*auth_payload_builder_t)(const void *context, char **payload);
typedef enum { AUTH_BIND, AUTH_QUERY_BIND, AUTH_UNBIND } auth_bind_operation_t;

static __attribute__((unused)) const char *auth_bool_text(qm_bool value)
{
    return value ? "true" : "false";
}

static __attribute__((unused)) const char *auth_present_text(const char *value)
{
    return value && value[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ?
           "yes" : "no";
}

static int auth_parse_u32_text(const char *text, uint32_t *value)
{
    uint32_t len;

    if (!text || !value) {
        return -QM_EINVAL;
    }

    len = qm_strlen(text);
    if (len == JOYINSIDE_CLIENT_BODY_EMPTY_LEN || len > UINT8_MAX) {
        return -QM_EINVAL;
    }
    return qm_str2uint((char *)text, (uint8_t)len, value);
}

static uint64_t auth_timestamp_ms(void)
{
    qm_timeval_t tv;

    if (qm_gettimeofday(&tv, NULL) == QM_EOK) {
        return tv.tv_sec * JOYINSIDE_CLIENT_MSEC_PER_SEC + tv.tv_usec / JOYINSIDE_CLIENT_MSEC_PER_SEC;
    }
    return qm_time(NULL) * JOYINSIDE_CLIENT_MSEC_PER_SEC;
}

static int auth_copy_out(const char *src, uint32_t src_len, char *out, uint32_t *out_len)
{
    uint32_t cap;

    if (!out_len) {
        return -QM_EINVAL;
    }

    /* Publish the required length even when the caller is querying or must grow
     * its buffer; callers never receive a truncated credential. */
    cap = *out_len;
    *out_len = src_len;
    if (!out) {
        return QM_EOK;
    }
    if (cap <= src_len) {
        return -QM_EFULL;
    }
    qm_memcpy(out, src, src_len);
    out[src_len] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    return QM_EOK;
}

static int auth_body_cb(const char *data, int len, void *user)
{
    auth_http_body_t *body = (auth_http_body_t *)user;

    if (!body || !data || len < JOYINSIDE_CLIENT_HTTP_BODY_CALLBACK_ERROR_MIN) {
        return -QM_EINVAL;
    }
    if (body->len >= body->cap || (uint32_t)len >= body->cap - body->len) {
        return -QM_EFULL;
    }
    qm_memcpy(body->data + body->len, data, (uint32_t)len);
    body->len += (uint32_t)len;
    body->data[body->len] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    return QM_EOK;
}

/**
 * @brief Execute a bounded JSON POST with retry-aware payload ownership.
 *
 * @par 中文说明
 * 执行带重试且明确载荷所有权的有界 JSON POST。
 *
 * When build_payload is supplied, it creates a fresh payload for each attempt;
 * this is required for signed requests whose nonce and timestamp must not be
 * reused. The returned payload is freed before the next retry.
 */
static int auth_http_post_json(const char *url,
                               const qm_http_tls_config_t *tls,
                               const qm_http_header_t *headers,
                               int header_count,
                               const char *payload,
                               uint32_t timeout_ms,
                               int max_retries,
                               uint32_t base_delay_ms,
                               uint32_t response_size,
                               auth_http_body_t *body,
                               qm_http_client_response_t *resp,
                               auth_payload_builder_t build_payload,
                               const void *payload_context)
{
    qm_http_client_request_t req;
    int attempt;
    int ret = -QM_EINVAL;
    int retries = max_retries < JOYINSIDE_CLIENT_RETRY_NONE ? JOYINSIDE_CLIENT_RETRY_NONE : max_retries;
    backoff_algorithm_ctx_t backoff;
    uint16_t delay;

    if (!url || (!payload && !build_payload) || !body || !body->data || !resp) {
        return -QM_EINVAL;
    }
    qm_memset(resp, 0, sizeof(*resp));
    if (base_delay_ms > JOYINSIDE_CLIENT_RETRY_DELAY_MAX_MS) {
        base_delay_ms = JOYINSIDE_CLIENT_RETRY_DELAY_MAX_MS;
    }
    backoff_algorithm_init(&backoff, (uint16_t)base_delay_ms,
                            JOYINSIDE_CLIENT_RETRY_DELAY_MAX_MS, (uint32_t)retries);

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
            "http post start payload_len=%lu timeout=%lums retries=%d response_size=%lu tls=%s headers=%d",
            (unsigned long)(payload ? qm_strlen(payload) : 0U),
            (unsigned long)(timeout_ms ? timeout_ms : CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS),
            retries, (unsigned long)response_size, auth_bool_text(tls ? QM_TRUE : QM_FALSE),
            header_count);
    /* A signed payload is built per attempt so nonce and timestamp remain fresh
     * across retries. The factory result belongs to this loop iteration. */
    for (attempt = JOYINSIDE_CLIENT_RETRY_FIRST_ATTEMPT; attempt <= retries; ++attempt) {
        char *attempt_payload = NULL;
        if (build_payload) {
            ret = build_payload(payload_context, &attempt_payload);
            if (ret != QM_EOK) return ret;
        }
        qm_memset(&req, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(req));
        qm_memset(resp, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(*resp));
        body->len = JOYINSIDE_CLIENT_BODY_EMPTY_LEN;
        body->data[JOYINSIDE_CLIENT_BODY_EMPTY_LEN] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;

        req.method = QM_HTTP_POST;
        req.url = url;
        req.body = attempt_payload ? attempt_payload : payload;
        req.body_len = (int)qm_strlen(req.body);
        req.content_type = "application/json";
        req.headers = headers;
        req.header_count = header_count;
        req.timeout_ms = timeout_ms ? timeout_ms : CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS;
        req.max_body_len = response_size;
        req.rx_buf_size = response_size < JOYINSIDE_CLIENT_HTTP_RX_BUFFER_MAX ?
                          response_size : JOYINSIDE_CLIENT_HTTP_RX_BUFFER_MAX;
        req.tls = tls;
        req.user = body;
        req.on_body = auth_body_cb;

        JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "http post attempt=%d/%d", attempt + 1, retries + 1);
        ret = qm_http_client_execute(&req, resp);
        cJSON_free(attempt_payload);
        JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "http post result attempt=%d ret=%d status=%d body_len=%lu",
                attempt + 1, ret, resp->status_code, (unsigned long)body->len);
        if (ret == QM_EOK && resp->status_code < JOYINSIDE_CLIENT_HTTP_STATUS_RETRY_MIN) {
            return QM_EOK;
        }
        if (ret == -QM_EFULL || ret == -QM_EINVAL || ret == -QM_ENOMEM) return ret;
        if (backoff_algorithm_get_next_delay(&backoff, &delay) != BACKOFF_ALGORITHM_SUCCESS) break;
        if (base_delay_ms && delay) qm_msleep(delay);
    }

    return ret == QM_EOK ? -QM_EIO : ret;
}

static int auth_json_copy_string(cJSON *root, const char *key, char *out, uint32_t *out_len)
{
    cJSON *value;
    const char *str;

    if (!root || !key) {
        return -QM_EINVAL;
    }

    value = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsString(value) || !value->valuestring) {
        return -QM_ENOENT;
    }
    str = value->valuestring;
    return auth_copy_out(str, qm_strlen(str), out, out_len);
}

static int auth_json_get_code(cJSON *root, int *code)
{
    cJSON *value;

    if (!root || !code) {
        return -QM_EINVAL;
    }
    value = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (value && (!cJSON_IsString(value) || qm_strcmp(value->valuestring, "SUCCESS") != 0)) {
        return -QM_EIO;
    }

    value = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!value) {
        *code = JOYINSIDE_CLIENT_HTTP_STATUS_OK;
        return QM_EOK;
    }
    if (cJSON_IsNumber(value)) {
        *code = value->valueint;
        return QM_EOK;
    }
    if (cJSON_IsString(value) && value->valuestring) {
        uint32_t parsed;
        int ret = auth_parse_u32_text(value->valuestring, &parsed);
        if (ret == QM_EOK) {
            *code = (int)parsed;
        }
        return ret;
    }
    return -QM_EINVAL;
}

/* Server TTL fields are numbers in examples but may be strings elsewhere. */
static uint32_t auth_json_get_uint(cJSON *root, const char *key)
{
    cJSON *value;
    uint32_t parsed;

    if (!root || !key) {
        return 0U;
    }

    value = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(value) && value->valuedouble >= 0.0 &&
        value->valuedouble <= UINT32_MAX) {
        return (uint32_t)value->valuedouble;
    }
    if (cJSON_IsString(value) && value->valuestring &&
        auth_parse_u32_text(value->valuestring, &parsed) == QM_EOK) {
        return parsed;
    }
    return 0U;
}

static int auth_parse_token_extra(cJSON *root, joyinside_client_token_extra_t *extra)
{
    cJSON *value;

    if (!root || !extra) {
        return -QM_EINVAL;
    }

    extra->expire_in_sec = auth_json_get_uint(root, "expireIn");
    extra->refresh_expire_in_sec = auth_json_get_uint(root, "refreshExpireIn");
    value = cJSON_GetObjectItemCaseSensitive(root, "refreshToken");
    if (cJSON_IsString(value) && value->valuestring && extra->refresh_token_len) {
        return auth_copy_out(value->valuestring, qm_strlen(value->valuestring),
                             extra->refresh_token, extra->refresh_token_len);
    }
    if (extra->refresh_token_len) {
        *extra->refresh_token_len = 0U;
    }
    return QM_EOK;
}

/* Build the /auth/getToken JSON payload with cJSON to keep escaping correct. */
static int auth_make_token_payload(const joyinside_client_token_request_t *request,
                                   const char *timestamp,
                                   const char *nonce,
                                   const char *signature,
                                   char **payload)
{
    cJSON *root = NULL;
    char *printed = NULL;
    int ret = QM_EOK;

    if (!request || !timestamp || !nonce || !signature || !payload) {
        return -QM_EINVAL;
    }
    *payload = NULL;

    root = cJSON_CreateObject();
    if (!root) {
        return -QM_ENOMEM;
    }

    if (!cJSON_AddStringToObject(root, "accessKeyId", request->credentials.access_key_id) ||
        !cJSON_AddStringToObject(root, "accessTimestamp", timestamp) ||
        !cJSON_AddStringToObject(root, "accessNonce", nonce) ||
        !cJSON_AddStringToObject(root, "accessVersion", JOYINSIDE_CLIENT_ACCESS_VERSION) ||
        !cJSON_AddStringToObject(root, "accessSign", signature)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (request->bot_id &&
        request->bot_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        if (!cJSON_AddStringToObject(root, "botId", request->bot_id)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    } else {
        if (!cJSON_AddStringToObject(root, "vendorId", request->credentials.vendor_id)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    }

    printed = cJSON_PrintUnformatted(root);
    if (!printed) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    *payload = printed;
    printed = NULL;

cleanup:
    cJSON_Delete(root);
    cJSON_free(printed);
    if (ret != QM_EOK) {
        *payload = NULL;
    }
    return ret;
}

/* Build the /device/register JSON payload. Optional fields are omitted. */
static int auth_make_register_payload(const joyinside_client_register_request_t *request, char **payload)
{
    const joyinside_client_device_config_t *dev;
    cJSON *root = NULL;
    char *printed = NULL;
    int ret = QM_EOK;

    if (!request || !payload) {
        return -QM_EINVAL;
    }
    *payload = NULL;
    dev = &request->device;

    root = cJSON_CreateObject();
    if (!root) {
        return -QM_ENOMEM;
    }

    if (!cJSON_AddStringToObject(root, "vendorId", request->credentials.vendor_id) ||
        !cJSON_AddStringToObject(root, "productId", request->credentials.product_id) ||
        !cJSON_AddStringToObject(root, "type", dev->type) ||
        !cJSON_AddStringToObject(root, "name", dev->name) ||
        !cJSON_AddStringToObject(root, "deviceId", dev->device_id)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (dev->device_model) {
        if (!cJSON_AddStringToObject(root, "deviceModel", dev->device_model)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    }
    if (dev->timbre_id) {
        if (!cJSON_AddStringToObject(root, "timbreId", dev->timbre_id)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    }
    if (dev->desc) {
        if (!cJSON_AddStringToObject(root, "desc", dev->desc)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    }

    if (dev->resource_authed_count) {
        /* The caller borrows this list for the synchronous payload build. Add
         * each string as an owned cJSON item so one cleanup path owns all JSON.
         * 调用方仅在同步构建期间借用列表；每个字符串转为 cJSON 自有项，由统一
         * cleanup 路径释放。 */
        if (!dev->resource_authed_list) { ret = -QM_EINVAL; goto cleanup; }
        cJSON *resources = cJSON_AddArrayToObject(root, "resourceAuthedList");
        if (!resources) { ret = -QM_ENOMEM; goto cleanup; }
        for (uint32_t i = 0; i < dev->resource_authed_count; ++i) {
            const char *name = dev->resource_authed_list[i];
            if (!name || !name[0]) { ret = -QM_EINVAL; goto cleanup; }
            cJSON *value = cJSON_CreateString(name);
            if (!value || !cJSON_AddItemToArray(resources, value)) {
                cJSON_Delete(value);
                ret = -QM_ENOMEM;
                goto cleanup;
            }
        }
    }

    printed = cJSON_PrintUnformatted(root);
    if (!printed) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    *payload = printed;
    printed = NULL;

cleanup:
    cJSON_Delete(root);
    cJSON_free(printed);
    if (ret != QM_EOK) {
        *payload = NULL;
    }
    return ret;
}

int joyinside_client_generate_signature(const char *access_key_id,
                                    const char *access_timestamp,
                                    const char *access_nonce,
                                    const char *access_secret,
                                    char out_hex[JOYINSIDE_CLIENT_SIGNATURE_BUFFER_LEN])
{
    char *canonical;
    int len;
    int ret;

    if (!access_key_id || !access_timestamp || !access_nonce || !access_secret || !out_hex) {
        return -QM_EINVAL;
    }

    len = qm_snprintf(NULL, JOYINSIDE_CLIENT_SNPRINTF_COUNT_ONLY,
                      "accesskeyid=%s&accessnonce=%s&accesstimestamp=%s&accessversion=%s",
                      access_key_id, access_nonce, access_timestamp, JOYINSIDE_CLIENT_ACCESS_VERSION);
    if (len <= JOYINSIDE_CLIENT_SNPRINTF_ERROR_MAX) {
        return -QM_EIO;
    }

    canonical = (char *)qm_malloc((uint32_t)len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
    if (!canonical) {
        return -QM_ENOMEM;
    }
    qm_snprintf(canonical, (int)((uint32_t)len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN),
                "accesskeyid=%s&accessnonce=%s&accesstimestamp=%s&accessversion=%s",
                access_key_id, access_nonce, access_timestamp, JOYINSIDE_CLIENT_ACCESS_VERSION);
    ret = qm_utils_hmac_md5(canonical, len, out_hex, access_secret, (int)qm_strlen(access_secret));
    qm_free(canonical);
    if (ret != QM_EOK) {
        return ret;
    }
    out_hex[QM_UTILS_HMAC_MD5_HEX_LEN] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    return QM_EOK;
}

/**
 * @brief Build one independently signed getToken payload.
 *
 * @par 中文说明
 * 构造一份独立签名的 getToken 请求载荷。
 *
 * This factory runs once per HTTP attempt so retry requests receive a new nonce
 * and timestamp rather than replaying a previously signed body.
 */
static int auth_signed_payload(const void *context, char **payload)
{
    const joyinside_client_token_request_t *request = context;
    char timestamp[JOYINSIDE_CLIENT_TIMESTAMP_TEXT_LEN];
    char nonce[JOYINSIDE_CLIENT_UUID32_BUFFER_LEN];
    char signature[JOYINSIDE_CLIENT_SIGNATURE_BUFFER_LEN];
    int ret;
    qm_snprintf(timestamp, sizeof(timestamp), "%llu", (unsigned long long)auth_timestamp_ms());
    qm_utils_unformatted_uuid(nonce);
    ret = joyinside_client_generate_signature(request->credentials.access_key_id, timestamp, nonce,
                                               request->credentials.access_key_secret, signature);
    return ret == QM_EOK ? auth_make_token_payload(request, timestamp, nonce, signature, payload) : ret;
}

int joyinside_client_request_token_ex(const joyinside_client_token_request_t *request,
                                  char *token,
                                  uint32_t *token_len,
                                  joyinside_client_token_extra_t *extra)
{
    auth_http_body_t body;
    qm_http_client_response_t resp;
    cJSON *root = NULL;
    uint32_t body_size;
    int code = JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO;
    int ret;

    if (!request || request->response_size >= INT32_MAX ||
        !request->credentials.access_key_id || !request->credentials.access_key_id[0] ||
        !request->credentials.access_key_secret || !request->credentials.access_key_secret[0] ||
        ((!request->bot_id ||
          request->bot_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) &&
         (!request->credentials.vendor_id ||
          request->credentials.vendor_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] ==
              JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR)) ||
        !token_len) {
        return -QM_EINVAL;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
            "token request start mode=%s access_key=%s vendor=%s token_buf=%s timeout=%lums retries=%d response_size=%lu",
            request->bot_id &&
            request->bot_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ?
            "bot" : "vendor",
            auth_present_text(request->credentials.access_key_id),
            auth_present_text(request->credentials.vendor_id),
            token ? "provided" : "query",
            (unsigned long)(request->timeout_ms ? request->timeout_ms : CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS),
            request->max_retries,
            (unsigned long)(request->response_size ? request->response_size :
                            CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE));
    body_size = request->response_size ? request->response_size : CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE;
    body.data = (char *)qm_malloc(body_size + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
    body.cap = body_size + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN;
    body.len = JOYINSIDE_CLIENT_BODY_EMPTY_LEN;
    if (!body.data) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "token response buffer alloc failed size=%lu", (unsigned long)body_size);
        return -QM_ENOMEM;
    }

    ret = auth_http_post_json(request->endpoint.token_url ? request->endpoint.token_url : JOYINSIDE_CLIENT_TOKEN_URL,
                            request->endpoint.http_tls, NULL, JOYINSIDE_CLIENT_AUTH_HEADER_NONE, NULL,
                            request->timeout_ms, request->max_retries,
                            request->base_delay_ms, body_size, &body, &resp,
                            auth_signed_payload, request);
    if (ret == QM_EOK && resp.status_code == JOYINSIDE_CLIENT_HTTP_STATUS_OK) {
        root = cJSON_ParseWithLength(body.data, body.len);
        if (!root) {
            ret = -QM_EIO;
        }
    } else if (ret == QM_EOK) {
        ret = -QM_EIO;
    }
    if (ret == QM_EOK) {
        ret = auth_json_get_code(root, &code);
        if (ret == QM_EOK && code != JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO &&
            code != JOYINSIDE_CLIENT_HTTP_STATUS_OK) {
            ret = -QM_EIO;
        }
        if (ret == QM_EOK) {
            ret = auth_json_copy_string(root, "accessToken", token, token_len);
        }
        if (ret == QM_EOK && extra) {
            ret = auth_parse_token_extra(root, extra);
        }
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "token request done ret=%d status=%d service_code=%d body_len=%lu token_len=%lu",
            ret, resp.status_code, code, (unsigned long)body.len,
            token_len ? (unsigned long)*token_len : 0UL);
    cJSON_Delete(root);
    qm_free(body.data);
    return ret;
}

int joyinside_client_request_token(const joyinside_client_token_request_t *request,
                               char *token,
                               uint32_t *token_len)
{
    return joyinside_client_request_token_ex(request, token, token_len, NULL);
}

/* Build the /auth/refreshToken JSON payload with cJSON to keep escaping correct. */
static int auth_make_refresh_payload(const joyinside_client_refresh_request_t *request,
                                     char **payload)
{
    cJSON *root = NULL;
    char *printed = NULL;
    int ret = QM_EOK;

    if (!request || !payload) {
        return -QM_EINVAL;
    }
    *payload = NULL;

    root = cJSON_CreateObject();
    if (!root) {
        return -QM_ENOMEM;
    }

    if (!cJSON_AddStringToObject(root, "accessKeyId", request->credentials.access_key_id) ||
        !cJSON_AddStringToObject(root, "refreshToken", request->refresh_token)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (request->bot_id &&
        request->bot_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        if (!cJSON_AddStringToObject(root, "botId", request->bot_id)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    } else {
        if (!cJSON_AddStringToObject(root, "vendorId", request->credentials.vendor_id)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    }

    printed = cJSON_PrintUnformatted(root);
    if (!printed) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    *payload = printed;
    printed = NULL;

cleanup:
    cJSON_Delete(root);
    cJSON_free(printed);
    if (ret != QM_EOK) {
        *payload = NULL;
    }
    return ret;
}

int joyinside_client_refresh_token(const joyinside_client_refresh_request_t *request,
                               char *token,
                               uint32_t *token_len,
                               joyinside_client_token_extra_t *extra)
{
    char *payload = NULL;
    char *auth = NULL;
    qm_http_header_t headers[JOYINSIDE_CLIENT_AUTH_HEADER_COUNT];
    int header_count = JOYINSIDE_CLIENT_AUTH_HEADER_NONE;
    auth_http_body_t body;
    qm_http_client_response_t resp;
    cJSON *root = NULL;
    uint32_t body_size;
    int code = JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO;
    int ret;

    qm_memset(&body, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(body));

    if (!request || request->response_size >= INT32_MAX ||
        !request->credentials.access_key_id || !request->credentials.access_key_id[0] || !request->refresh_token ||
        (request->use_bearer && (!request->bearer_token || !request->bearer_token[0])) ||
        request->refresh_token[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        ((!request->bot_id ||
          request->bot_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) &&
         (!request->credentials.vendor_id ||
          request->credentials.vendor_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] ==
              JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR)) ||
        !token_len) {
        return -QM_EINVAL;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
            "token refresh start access_key=%s bot=%s bearer=%s token_buf=%s timeout=%lums retries=%d response_size=%lu",
            auth_present_text(request->credentials.access_key_id),
            auth_present_text(request->bot_id),
            auth_bool_text(request->use_bearer),
            token ? "provided" : "query",
            (unsigned long)(request->timeout_ms ? request->timeout_ms : CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS),
            request->max_retries,
            (unsigned long)(request->response_size ? request->response_size :
                            CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE));

    ret = auth_make_refresh_payload(request, &payload);
    if (ret != QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "refresh payload build failed ret=%d", ret);
        return ret;
    }

    if (request->use_bearer && request->bearer_token &&
        request->bearer_token[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        int auth_len = qm_snprintf(NULL, JOYINSIDE_CLIENT_SNPRINTF_COUNT_ONLY, "Bearer %s",
                                   request->bearer_token);
        if (auth_len <= JOYINSIDE_CLIENT_SNPRINTF_ERROR_MAX) {
            cJSON_free(payload);
            JOYINSIDE_CLIENT_LOGE(LOG_TAG, "refresh authorization header build failed");
            return -QM_EIO;
        }
        auth = (char *)qm_malloc((uint32_t)auth_len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
        if (!auth) {
            cJSON_free(payload);
            JOYINSIDE_CLIENT_LOGE(LOG_TAG, "refresh authorization header alloc failed");
            return -QM_ENOMEM;
        }
        qm_snprintf(auth, (int)((uint32_t)auth_len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN),
                    "Bearer %s", request->bearer_token);
        headers[JOYINSIDE_CLIENT_AUTH_HEADER_INDEX].name = "Authorization";
        headers[JOYINSIDE_CLIENT_AUTH_HEADER_INDEX].value = auth;
        header_count = JOYINSIDE_CLIENT_AUTH_HEADER_COUNT;
    }

    body_size = request->response_size ? request->response_size : CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE;
    body.data = (char *)qm_malloc(body_size + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
    body.cap = body_size + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN;
    body.len = JOYINSIDE_CLIENT_BODY_EMPTY_LEN;
    if (!body.data) {
        cJSON_free(payload);
        qm_free(auth);
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "refresh response buffer alloc failed size=%lu", (unsigned long)body_size);
        return -QM_ENOMEM;
    }

    ret = auth_http_post_json(request->endpoint.refresh_url ? request->endpoint.refresh_url :
                                                           JOYINSIDE_CLIENT_REFRESH_TOKEN_URL,
                            request->endpoint.http_tls, headers, header_count, payload,
                            request->timeout_ms, request->max_retries,
                            request->base_delay_ms, body_size, &body, &resp, NULL, NULL);
    cJSON_free(payload);
    qm_free(auth);
    if (ret == QM_EOK && resp.status_code == JOYINSIDE_CLIENT_HTTP_STATUS_OK) {
        root = cJSON_ParseWithLength(body.data, body.len);
        if (!root) {
            ret = -QM_EIO;
        }
    } else if (ret == QM_EOK) {
        ret = -QM_EIO;
    }
    if (ret == QM_EOK) {
        ret = auth_json_get_code(root, &code);
        if (ret == QM_EOK && code != JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO &&
            code != JOYINSIDE_CLIENT_HTTP_STATUS_OK) {
            ret = -QM_EIO;
        }
        if (ret == QM_EOK) {
            ret = auth_json_copy_string(root, "accessToken", token, token_len);
        }
        if (ret == QM_EOK && extra) {
            ret = auth_parse_token_extra(root, extra);
        }
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "token refresh done ret=%d status=%d service_code=%d body_len=%lu token_len=%lu",
            ret, resp.status_code, code, (unsigned long)body.len,
            token_len ? (unsigned long)*token_len : 0UL);
    cJSON_Delete(root);
    qm_free(body.data);
    return ret;
}

/**
 * @brief Execute the shared Bearer POST for device bind operations.
 *
 * @par 中文说明
 * 执行设备绑定操作共用的 Bearer POST。
 *
 * The response contract differs by operation: query copies the bound UID,
 * unbind requires data=true, and bind requires the returned UID to match.
 */
static int auth_user_bind_post(const joyinside_client_user_bind_request_t *request,
                               const char *url,
                               const char *payload_json,
                               auth_bind_operation_t operation,
                               char *uid, uint32_t *uid_len)
{
    char *auth = NULL;
    qm_http_header_t headers[JOYINSIDE_CLIENT_AUTH_HEADER_COUNT];
    auth_http_body_t body;
    qm_http_client_response_t resp;
    cJSON *root = NULL;
    uint32_t body_size;
    int code = JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO;
    int ret;
    int auth_len;

    qm_memset(&body, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(body));

    auth_len = qm_snprintf(NULL, JOYINSIDE_CLIENT_SNPRINTF_COUNT_ONLY, "Bearer %s",
                           request->access_token);
    if (auth_len <= JOYINSIDE_CLIENT_SNPRINTF_ERROR_MAX) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "user bind authorization header build failed");
        return -QM_EIO;
    }
    auth = (char *)qm_malloc((uint32_t)auth_len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
    if (!auth) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "user bind authorization header alloc failed");
        return -QM_ENOMEM;
    }
    qm_snprintf(auth, (int)((uint32_t)auth_len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN),
                "Bearer %s", request->access_token);
    headers[JOYINSIDE_CLIENT_AUTH_HEADER_INDEX].name = "Authorization";
    headers[JOYINSIDE_CLIENT_AUTH_HEADER_INDEX].value = auth;

    body_size = request->response_size ? request->response_size : CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE;
    body.data = (char *)qm_malloc(body_size + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
    body.cap = body_size + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN;
    body.len = JOYINSIDE_CLIENT_BODY_EMPTY_LEN;
    if (!body.data) {
        qm_free(auth);
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "user bind response buffer alloc failed size=%lu", (unsigned long)body_size);
        return -QM_ENOMEM;
    }

    ret = auth_http_post_json(url, request->endpoint.http_tls, headers,
                            JOYINSIDE_CLIENT_AUTH_HEADER_COUNT, payload_json,
                            request->timeout_ms, request->max_retries,
                            request->base_delay_ms, body_size, &body, &resp, NULL, NULL);
    qm_free(auth);
    if (ret == QM_EOK && resp.status_code == JOYINSIDE_CLIENT_HTTP_STATUS_OK) {
        root = cJSON_ParseWithLength(body.data, body.len);
        if (!root) {
            ret = -QM_EIO;
        }
    } else if (ret == QM_EOK) {
        ret = -QM_EIO;
    }
    if (ret == QM_EOK) {
        ret = auth_json_get_code(root, &code);
        if (ret == QM_EOK && code != JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO &&
            code != JOYINSIDE_CLIENT_HTTP_STATUS_OK) {
            ret = -QM_EIO;
        }
        if (ret == QM_EOK) {
            cJSON *data = cJSON_GetObjectItemCaseSensitive(root, "data");
            if (operation == AUTH_QUERY_BIND) {
                ret = auth_json_copy_string(root, "data", uid, uid_len);
            } else if (operation == AUTH_UNBIND) {
                ret = cJSON_IsTrue(data) ? QM_EOK : -QM_EIO;
            } else {
                ret = cJSON_IsString(data) && qm_strcmp(data->valuestring, request->uid) == 0 ?
                    QM_EOK : -QM_EIO;
            }
        }
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "user bind done ret=%d status=%d service_code=%d body_len=%lu",
            ret, resp.status_code, code, (unsigned long)body.len);
    cJSON_Delete(root);
    qm_free(body.data);
    return ret;
}

static int auth_user_bind_payload(cJSON *root, const joyinside_client_user_bind_request_t *request,
                                   auth_bind_operation_t operation)
{
    if (!root || !request ||
        !cJSON_AddStringToObject(root, "botId", request->bot_id) ||
        !cJSON_AddStringToObject(root, "requestId", request->request_id)) {
        return -QM_EINVAL;
    }
    if (operation != AUTH_QUERY_BIND) {
        if (!cJSON_AddStringToObject(root, "uid", request->uid)) {
            return -QM_ENOMEM;
        }
    }
    if (operation == AUTH_UNBIND &&
        !cJSON_AddBoolToObject(root, "deleteMemory", request->delete_memory)) return -QM_ENOMEM;
    return QM_EOK;
}

static int auth_user_bind_common(const joyinside_client_user_bind_request_t *request,
                                 const char *default_url,
                                 const char *op_name,
                                 auth_bind_operation_t operation,
                                 char *uid, uint32_t *uid_len)
{
    cJSON *root = NULL;
    char *payload = NULL;
    int ret;

    (void)op_name; /* Only referenced by logs that may compile out. */

    if (!request || request->response_size >= INT32_MAX || !request->access_token ||
        request->access_token[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        !request->bot_id ||
        request->bot_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        !request->request_id ||
        request->request_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        (operation != AUTH_QUERY_BIND && (!request->uid || !request->uid[0])) ||
        (operation == AUTH_QUERY_BIND && !uid_len)) {
        return -QM_EINVAL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        return -QM_ENOMEM;
    }
    ret = auth_user_bind_payload(root, request, operation);
    if (ret != QM_EOK) {
        cJSON_Delete(root);
        return ret;
    }
    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return -QM_ENOMEM;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
            "%s start bot=%s uid=%s timeout=%lums retries=%d",
            op_name, auth_present_text(request->bot_id), auth_present_text(request->uid),
            (unsigned long)(request->timeout_ms ? request->timeout_ms : CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS),
            request->max_retries);
    ret = auth_user_bind_post(request, default_url, payload, operation, uid, uid_len);
    cJSON_free(payload);
    return ret;
}

int joyinside_client_bind_user(const joyinside_client_user_bind_request_t *request)
{
    return auth_user_bind_common(request,
                                 request && request->bind_url ?
                                 request->bind_url : JOYINSIDE_CLIENT_BIND_USER_URL,
                                 "bindUser", AUTH_BIND, NULL, NULL);
}

int joyinside_client_query_bot_bind(const joyinside_client_user_bind_request_t *request,
                                    char *uid, uint32_t *uid_len)
{
    return auth_user_bind_common(request,
                                 request && request->query_url ?
                                 request->query_url : JOYINSIDE_CLIENT_QUERY_BOT_BIND_URL,
                                 "queryBotBind", AUTH_QUERY_BIND, uid, uid_len);
}

int joyinside_client_unbind_user(const joyinside_client_user_bind_request_t *request)
{
    return auth_user_bind_common(request,
                                 request && request->unbind_url ?
                                 request->unbind_url : JOYINSIDE_CLIENT_UNBIND_USER_URL,
                                 "unbindUser", AUTH_UNBIND, NULL, NULL);
}

int joyinside_client_register_device(const joyinside_client_register_request_t *request,
                                 char *bot_id,
                                 uint32_t *bot_id_len)
{
    joyinside_client_token_request_t token_req;
    char *token = NULL;
    uint32_t token_len;
    char *auth = NULL;
    qm_http_header_t headers[JOYINSIDE_CLIENT_AUTH_HEADER_COUNT];
    char *payload = NULL;
    auth_http_body_t body;
    qm_http_client_response_t resp;
    cJSON *root = NULL;
    uint32_t body_size;
    int code = JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO;
    int ret;

    qm_memset(&body, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(body));

    if (!request || request->response_size >= INT32_MAX ||
        !request->credentials.access_key_id || !request->credentials.access_key_id[0] ||
        !request->credentials.access_key_secret || !request->credentials.access_key_secret[0] ||
        !request->credentials.vendor_id || !request->credentials.vendor_id[0] ||
        !request->credentials.product_id || !request->credentials.product_id[0] ||
        !request->device.type || !request->device.type[0] ||
        !request->device.name || !request->device.name[0] ||
        !request->device.device_id || !request->device.device_id[0] || !bot_id_len) {
        return -QM_EINVAL;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
            "register start access_key=%s vendor=%s app=%s device_type=%s device_name=%s device_id=%s timeout=%lums retries=%d response_size=%lu",
            auth_present_text(request->credentials.access_key_id),
            auth_present_text(request->credentials.vendor_id),
            auth_present_text(request->credentials.product_id),
            auth_present_text(request->device.type),
            auth_present_text(request->device.name),
            auth_present_text(request->device.device_id),
            (unsigned long)(request->timeout_ms ? request->timeout_ms : CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS),
            request->max_retries,
            (unsigned long)(request->response_size ? request->response_size :
                            CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE));
    qm_memset(&token_req, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(token_req));
    token_req.endpoint = request->endpoint;
    token_req.credentials = request->credentials;
    token_req.timeout_ms = request->timeout_ms;
    token_req.max_retries = request->max_retries;
    token_req.base_delay_ms = request->base_delay_ms;
    token_req.response_size = request->response_size;

    token_len = request->response_size ? request->response_size : CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE;
    token = (char *)qm_malloc(token_len);
    if (!token) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "register token buffer alloc failed size=%lu", (unsigned long)token_len);
        return -QM_ENOMEM;
    }

    ret = joyinside_client_request_token(&token_req, token, &token_len);
    if (ret != QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "register token request failed ret=%d", ret);
        goto cleanup;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "register token ready len=%lu", (unsigned long)token_len);

    ret = auth_make_register_payload(request, &payload);
    if (ret != QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "register payload build failed ret=%d", ret);
        goto cleanup;
    }

    {
        int auth_len = qm_snprintf(NULL, JOYINSIDE_CLIENT_SNPRINTF_COUNT_ONLY, "Bearer %s", token);
        if (auth_len <= JOYINSIDE_CLIENT_SNPRINTF_ERROR_MAX) {
            JOYINSIDE_CLIENT_LOGE(LOG_TAG, "register authorization header build failed");
            ret = -QM_EIO;
            goto cleanup;
        }
        auth = (char *)qm_malloc((uint32_t)auth_len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
        if (!auth) {
            JOYINSIDE_CLIENT_LOGE(LOG_TAG, "register authorization header alloc failed");
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        qm_snprintf(auth, (int)((uint32_t)auth_len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN),
                    "Bearer %s", token);
    }
    headers[JOYINSIDE_CLIENT_AUTH_HEADER_INDEX].name = "Authorization";
    headers[JOYINSIDE_CLIENT_AUTH_HEADER_INDEX].value = auth;

    body_size = request->response_size ? request->response_size : CONFIG_JOYINSIDE_CLIENT_HTTP_BODY_SIZE;
    body.data = (char *)qm_malloc(body_size + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
    body.cap = body_size + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN;
    body.len = JOYINSIDE_CLIENT_BODY_EMPTY_LEN;
    if (!body.data) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "register response buffer alloc failed size=%lu", (unsigned long)body_size);
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = auth_http_post_json(request->endpoint.register_url ? request->endpoint.register_url : JOYINSIDE_CLIENT_REGISTER_URL,
                            request->endpoint.http_tls, headers, JOYINSIDE_CLIENT_AUTH_HEADER_COUNT, payload,
                            request->timeout_ms, request->max_retries,
                            request->base_delay_ms, body_size, &body, &resp, NULL, NULL);
    if (ret == QM_EOK && resp.status_code == JOYINSIDE_CLIENT_HTTP_STATUS_OK) {
        root = cJSON_ParseWithLength(body.data, body.len);
        if (!root) {
            ret = -QM_EIO;
        }
    } else if (ret == QM_EOK) {
        ret = -QM_EIO;
    }
    if (ret == QM_EOK) {
        ret = auth_json_get_code(root, &code);
        if (ret == QM_EOK && code != JOYINSIDE_CLIENT_SUCCESS_CODE_ZERO &&
            code != JOYINSIDE_CLIENT_HTTP_STATUS_OK) {
            ret = -QM_EIO;
        }
        if (ret == QM_EOK) {
            ret = auth_json_copy_string(root, "data", bot_id, bot_id_len);
        }
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "register done ret=%d status=%d service_code=%d body_len=%lu bot_id_len=%lu",
            ret, resp.status_code, code, (unsigned long)body.len,
            bot_id_len ? (unsigned long)*bot_id_len : 0UL);

cleanup:
    cJSON_Delete(root);
    cJSON_free(payload);
    qm_free(auth);
    qm_free(token);
    qm_free(body.data);
    return ret;
}
