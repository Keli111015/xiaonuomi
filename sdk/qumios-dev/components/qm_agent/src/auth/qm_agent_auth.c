/**
 * @file qm_agent_auth.c
 * @brief Agent credential acquisition, parsing, caching, and retry scheduling.
 *
 * Agent 凭据获取、解析、缓存和重试调度。认证收包只复制或解析必要数据，认证状态变更由
 * Agent 控制任务串行处理；重试 generation 用于忽略过期认证周期的定时动作。
 */

#include "qm_agent_internal.h"
#include "backoff_algorithm.h"
#include "cJSON.h"
#include "qm_iot_api.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"

#if defined(CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP) && CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP
#define QM_AGENT_AUTH_USE_HTTP 1
#include "qm_http_client.h"
#include "qm_utils_hmac.h"
#else
#define QM_AGENT_AUTH_USE_HTTP 0
#include "qm_utils_list.h"
#endif

#define QM_AGENT_AUTH_UINT32_MAX_VALUE 4294967295.0
#define QM_AGENT_AUTH_RETRY_BASE_DELAY_MS 500U
#define QM_AGENT_AUTH_RETRY_MAX_DELAY_MS 5000U
#define QM_AGENT_AUTH_RETRY_MAX_ATTEMPTS 5U
#define QM_AGENT_AUTH_REPLY_MAX_BYTES 8192U
#define QM_AGENT_AUTH_REPLY_TIMEOUT_MS 5000U

#if QM_AGENT_AUTH_USE_HTTP
#define QM_AGENT_AUTH_HTTP_TIMEOUT_MS 5000U
#define QM_AGENT_AUTH_HTTP_RESPONSE_MAX_LEN 8192U
#define QM_AGENT_AUTH_HTTP_URL_MAX_LEN 256U
#define QM_AGENT_AUTH_HTTP_SIGN_SOURCE_MAX_LEN 256U
#define QM_AGENT_AUTH_HTTP_SIGNATURE_MAX_LEN (QM_UTILS_HMAC_SHA1_HEX_LEN + 1U)
#define QM_AGENT_AUTH_HTTP_PATH "/agent-device/device/getAgentToken"
#define QM_AGENT_AUTH_HTTP_BASE_URL_DEV "https://api-dev.ljyd.cn"
#define QM_AGENT_AUTH_HTTP_BASE_URL_TEST "https://api-test.ljyd.cn"
#define QM_AGENT_AUTH_HTTP_BASE_URL_PROD "https://api.ljyd.cn"
#define QM_AGENT_AUTH_HTTP_NONCE_MAX ((1U << 31) - 1U)
#endif

#define LOG_TAG "qm_agent_auth"

/**
 * @brief Device-topic authentication helper for qm_agent transports.
 *
 * The helper owns the getAgentToken request/reply flow and caches the most
 * recent parsed reply for the qm_agent control task. Applications configure
 * the Agent and never access this runtime directly.
 */

struct qm_agent_auth {
    /** @brief Agent that owns this helper and schedules its retries. */
    qm_agent_handle_t owner;

    /** @brief Borrowed qm_iot handle. */
    void *iot_handle;

    /** @brief Latest parsed authentication reply. */
    qm_agent_auth_info_t cached_auth;

    /** @brief Whether cached_auth currently holds valid data. */
    qm_bool has_cached_auth;

    /** @brief Exponential backoff state for token-request retries. */
    backoff_algorithm_ctx_t retry_backoff;

    /** @brief Whether a token-request retry cycle is active. */
    qm_bool retry_active;
    /** @brief A failed cycle requires explicit stop/start or reconnection. */
    qm_bool retry_exhausted;
    /** @brief A successful MQTT publish is waiting for its bounded reply. */
    qm_bool awaiting_reply;

    /** @brief Request id reused across retries when non-zero. */
    uint32_t request_msg_id;

    /** @brief Invalidates delayed retry notifications after reset/stop. */
    uint32_t retry_generation;

#if QM_AGENT_AUTH_USE_HTTP
    /** @brief Optional HTTP request configuration. */
    qm_agent_auth_http_config_t http_config;
#else
    /** @brief Registry node and never-reused callback identity. */
    qm_list_node_t registration;
    uint32_t cookie;
    /** @brief Latest MQTT reply, copied by the ingress callback. */
    char *pending_reply;
    uint32_t pending_reply_len;
    /** @brief Connectivity snapshot; guarded by the registration mutex. */
    qm_bool cloud_online;
    qm_bool disconnected;
#endif
};

static int qm_agent_auth_send_request(qm_agent_auth_t *auth);

#if !QM_AGENT_AUTH_USE_HTTP
/* Session lifecycle initialization is serialized by the application task.
 * Keep this mutex for process lifetime: copied cloud callbacks may arrive
 * after the last session has been removed. They carry IDs, never pointers. */
static qm_mutex_t s_auth_lock;
static qm_list_t s_auth_registry;
static uint32_t s_next_cookie;
static uint32_t s_next_request;

/**
 * @brief Resolve one live auth helper by its never-reused callback cookie.
 * 根据永不复用的回调 cookie 查找活动认证辅助对象。
 *
 * @pre s_auth_lock is held by the caller. 调用方必须持有 s_auth_lock。
 */
static qm_agent_auth_t *find_auth_locked(void *userdata)
{
    qm_list_node_t *node;
    uint32_t cookie = (uint32_t)(uintptr_t)userdata;

    for (node = s_auth_registry.head; node != NULL; node = node->next) {
        qm_agent_auth_t *auth = node->val;
        if (auth->cookie == cookie) return auth;
    }
    return NULL;
}

/**
 * @brief Copy cloud connectivity state and schedule serialized reconciliation.
 * 复制云连接状态并安排串行协调。
 *
 * The callback retains no provider data and uses only the cookie lookup while
 * holding the registry lock. 回调不保留供应商数据，只在注册表锁保护下按 cookie 查找对象。
 */
static void auth_cloud_event(void *iot, const qm_iot_event_t *event, void *userdata)
{
    qm_agent_auth_t *auth;

    QM_UNUSED_ARG(iot);
    if (event == NULL || (event->type != QM_IOT_EVENT_CLOUD_CONN &&
                          event->type != QM_IOT_EVENT_CLOUD_DISCONN)) return;
    qm_mutex_lock(&s_auth_lock, QM_WAIT_FOREVER);
    auth = find_auth_locked(userdata);
    if (auth != NULL) {
        auth->cloud_online = event->type == QM_IOT_EVENT_CLOUD_CONN;
        if (!auth->cloud_online) {
            auth->disconnected = QM_TRUE;
            qm_free(auth->pending_reply);
            auth->pending_reply = NULL;
            auth->pending_reply_len = 0U;
        }
        (void)qm_agent_control_schedule(auth->owner, QM_AGENT_CONTROL_RECONCILE,
                                        0U, 0U, 0U);
    }
    qm_mutex_unlock(&s_auth_lock);
}

/**
 * @brief Copy an MQTT authentication reply before the IoT callback returns.
 * 在 IoT 回调返回前复制 MQTT 认证回复。
 *
 * Parsing is deferred to the Agent control task so the MQTT callback does not
 * retain provider-owned memory or perform lifecycle transitions.
 * 解析被延后到 Agent 控制任务，避免 MQTT 回调保留供应商内存或执行生命周期状态转换。
 */
static void auth_reply_recv_handler(void *iot, const qm_iot_mqtt_recv_t *packet,
                                     void *userdata)
{
    qm_agent_auth_t *auth;
    char *copy;

    QM_UNUSED_ARG(iot);
    if (packet == NULL || packet->type != QM_IOT_MQTTRECV_PUB ||
        packet->data.pub.payload == NULL || packet->data.pub.payload_len == 0U ||
        packet->data.pub.payload_len > QM_AGENT_AUTH_REPLY_MAX_BYTES) return;
    qm_mutex_lock(&s_auth_lock, QM_WAIT_FOREVER);
    auth = find_auth_locked(userdata);
    if (auth != NULL && auth->cloud_online) {
        copy = qm_malloc(packet->data.pub.payload_len);
        if (copy != NULL) {
            qm_memcpy(copy, packet->data.pub.payload, packet->data.pub.payload_len);
            qm_free(auth->pending_reply);
            auth->pending_reply = copy;
            auth->pending_reply_len = packet->data.pub.payload_len;
            (void)qm_agent_control_schedule(auth->owner, QM_AGENT_CONTROL_RECONCILE,
                                            0U, 0U, 0U);
        }
    }
    qm_mutex_unlock(&s_auth_lock);
}
#endif

#if QM_AGENT_AUTH_USE_HTTP
typedef struct {
    char *buf;
    uint32_t buf_len;
    uint32_t used;
} qm_agent_auth_http_rsp_t;

/**
 * @brief Release strings owned by an HTTP auth configuration copy.
 * 释放 HTTP 认证配置副本持有的字符串。
 */
static void qm_agent_auth_http_config_clear(qm_agent_auth_http_config_t *config)
{
    if (config == NULL) {
        return;
    }

    qm_free((void *)config->base_url);
    qm_free((void *)config->product_secret);
    qm_free((void *)config->sn);
    qm_memset(config, 0, sizeof(*config));
}
#endif

#if QM_AGENT_AUTH_USE_HTTP
/**
 * @brief Resolve the built-in base URL for one authentication environment.
 * 为认证环境解析内置基础 URL。
 */
static const char *qm_agent_auth_http_default_base_url(qm_agent_auth_env_t env)
{
    switch (env) {
        case QM_AGENT_AUTH_ENV_DEV:
            return QM_AGENT_AUTH_HTTP_BASE_URL_DEV;
        case QM_AGENT_AUTH_ENV_TEST:
            return QM_AGENT_AUTH_HTTP_BASE_URL_TEST;
        case QM_AGENT_AUTH_ENV_PROD:
        default:
            return QM_AGENT_AUTH_HTTP_BASE_URL_PROD;
    }
}

/**
 * @brief Validate mandatory HTTP credential fields before a request is built.
 * 在构造请求前校验必需的 HTTP 凭据字段。
 */
static qm_bool qm_agent_auth_http_config_valid(const qm_agent_auth_http_config_t *config)
{
    return (config != NULL &&
            config->product_id != 0U &&
            config->product_secret != NULL &&
            config->product_secret[0] != '\0' &&
            config->sn != NULL &&
            config->sn[0] != '\0');
}

/**
 * @brief Append one borrowed HTTP response block to bounded owned storage.
 * 将一段借用的 HTTP 响应数据追加到有界自有存储。
 */
static int qm_agent_auth_http_body_cb(const char *data, int len, void *user)
{
    qm_agent_auth_http_rsp_t *rsp = (qm_agent_auth_http_rsp_t *)user;

    if (rsp == NULL || data == NULL || len < 0) {
        return -QM_EINVAL;
    }

    if (len == 0) {
        return QM_EOK;
    }

    if (rsp->used + (uint32_t)len >= rsp->buf_len) {
        return -QM_EFULL;
    }

    qm_memcpy(rsp->buf + rsp->used, data, (uint32_t)len);
    rsp->used += (uint32_t)len;
    rsp->buf[rsp->used] = '\0';
    return QM_EOK;
}

/**
 * @brief Build the endpoint URL into a caller-owned bounded buffer.
 * 将端点 URL 构造到调用方持有的有界缓冲区。
 */
static int qm_agent_auth_http_build_url(const qm_agent_auth_http_config_t *config,
                                        char *url,
                                        uint32_t url_len)
{
    const char *base_url;
    int ret;

    if (config == NULL || url == NULL || url_len == 0U) {
        return -QM_EINVAL;
    }

    base_url = (config->base_url != NULL && config->base_url[0] != '\0') ?
                   config->base_url :
                   qm_agent_auth_http_default_base_url(config->env);
    if (base_url == NULL || base_url[0] == '\0') {
        return -QM_EINVAL;
    }

    ret = qm_snprintf(url, url_len, "%s%s", base_url, QM_AGENT_AUTH_HTTP_PATH);
    if (ret <= 0 || (uint32_t)ret >= url_len) {
        return -QM_EFULL;
    }

    return QM_EOK;
}

/**
 * @brief Create an owned signed JSON payload for one HTTP credential request.
 * 为一次 HTTP 凭据请求创建自有的已签名 JSON 载荷。
 *
 * @post The caller releases *payload_out with cJSON_free(). 调用方必须使用 cJSON_free() 释放 *payload_out。
 */
static int qm_agent_auth_http_build_payload(const qm_agent_auth_http_config_t *config,
                                            char **payload_out)
{
    cJSON *root;
    char *payload;
    char sign_source[QM_AGENT_AUTH_HTTP_SIGN_SOURCE_MAX_LEN] = {0};
    char signature[QM_AGENT_AUTH_HTTP_SIGNATURE_MAX_LEN] = {0};
    char pid_str[16] = {0};
    char timestamp_str[16] = {0};
    uint32_t now_ms;
    uint32_t nonce;
    int ret;

    if (config == NULL || payload_out == NULL) {
        return -QM_EINVAL;
    }

    *payload_out = NULL;
    if (!qm_agent_auth_http_config_valid(config)) {
        return -QM_EINVAL;
    }

    now_ms = qm_now_ms();
    qm_srandom(now_ms);
    nonce = qm_random_get(QM_AGENT_AUTH_HTTP_NONCE_MAX);

    ret = qm_snprintf(sign_source,
                      sizeof(sign_source),
                      "pid=%d,nonce=%d,sn=%s,timestamp=%d",
                      (int)config->product_id,
                      (int)nonce,
                      config->sn,
                      (int)now_ms);
    if (ret <= 0 || (uint32_t)ret >= sizeof(sign_source)) {
        return -QM_EFULL;
    }

    ret = qm_utils_hmac_sha1(sign_source,
                             (int)qm_strlen(sign_source),
                             signature,
                             config->product_secret,
                             (int)qm_strlen(config->product_secret));
    if (ret != QM_EOK) {
        return ret;
    }
    signature[QM_UTILS_HMAC_SHA1_HEX_LEN] = '\0';

    ret = qm_snprintf(pid_str, sizeof(pid_str), "%u", (unsigned int)config->product_id);
    if (ret <= 0 || (uint32_t)ret >= sizeof(pid_str)) {
        return -QM_EFULL;
    }

    ret = qm_snprintf(timestamp_str, sizeof(timestamp_str), "%u", (unsigned int)now_ms);
    if (ret <= 0 || (uint32_t)ret >= sizeof(timestamp_str)) {
        return -QM_EFULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return -QM_ENOMEM;
    }

    cJSON_AddStringToObject(root, "sn", config->sn);
    cJSON_AddStringToObject(root, "pid", pid_str);
    cJSON_AddStringToObject(root, "timestamp", timestamp_str);
    cJSON_AddStringToObject(root, "signature", signature);
    cJSON_AddNumberToObject(root, "nonce", (double)nonce);

    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return -QM_ENOMEM;
    }

    *payload_out = payload;
    return QM_EOK;
}
#endif

/**
 * @brief Start a fresh retry generation with configured bounded backoff.
 * 使用配置的有界退避启动新的重试 generation。
 */
static void qm_agent_auth_reset_retry(qm_agent_auth_t *auth)
{
    uint32_t max_attempts = QM_AGENT_AUTH_RETRY_MAX_ATTEMPTS;

    if (auth == NULL) {
        return;
    }

    auth->retry_generation++;
    if (auth->retry_generation == 0U) auth->retry_generation = 1U;

#if QM_AGENT_AUTH_USE_HTTP
    if (auth->http_config.retry_num != 0U) {
        max_attempts = auth->http_config.retry_num;
    }
#endif

    backoff_algorithm_init(&auth->retry_backoff,
                           QM_AGENT_AUTH_RETRY_BASE_DELAY_MS,
                           QM_AGENT_AUTH_RETRY_MAX_DELAY_MS,
                           max_attempts);
    auth->retry_active = QM_TRUE;
}

/**
 * @brief Invalidate pending retry deadlines and clear reply-wait state.
 * 使待执行重试截止时间失效，并清除等待回复状态。
 */
static void qm_agent_auth_stop_retry(qm_agent_auth_t *auth)
{
    if (auth == NULL) {
        return;
    }

    auth->retry_generation++;
    if (auth->retry_generation == 0U) auth->retry_generation = 1U;

    auth->retry_active = QM_FALSE;
    auth->awaiting_reply = QM_FALSE;
    auth->request_msg_id = 0U;
}

/**
 * @brief Cancel the current authentication cycle without destroying cached credentials.
 * 取消当前认证周期，但不销毁已缓存的凭据。
 */
void qm_agent_auth_cancel(qm_agent_auth_t *auth)
{
    qm_agent_auth_stop_retry(auth);
    if (auth != NULL) auth->retry_exhausted = QM_FALSE;
}

/**
 * @brief Schedule a bounded-backoff retry for the current authentication generation.
 * 为当前认证 generation 安排有界退避重试。
 *
 * The generation is recorded with the deadline, allowing the control task to
 * discard retries left behind by a completed or cancelled authentication cycle.
 * generation 会随截止时间记录，使控制任务可丢弃已完成或已取消认证周期遗留的重试。
 */
static int qm_agent_auth_schedule_retry(qm_agent_auth_t *auth,
                                        const char *reason,
                                        int ret,
                                        int32_t reply_code)
{
    backoff_algorithm_status_t status;
    uint16_t delay_ms;
    int schedule_ret;

    if (auth == NULL) {
        return -QM_EINVAL;
    }

    /* Keep diagnostic-only arguments warning-free when control logging is
     * compiled out by the product configuration. */
    QM_UNUSED_ARG(reason);
    QM_UNUSED_ARG(reply_code);

    if (!auth->retry_active) {
        qm_agent_auth_reset_retry(auth);
    }
    auth->awaiting_reply = QM_FALSE;

    status = backoff_algorithm_get_next_delay(&auth->retry_backoff, &delay_ms);
    if (status != BACKOFF_ALGORITHM_SUCCESS) {
        QM_AGENT_LOGE(LOG_TAG,
                           "getAgentToken retry exhausted reason=%s ret=%d reply_code=%d attempts=%u",
                           reason ? reason : "",
                           ret,
                           (int)reply_code,
                           (unsigned int)auth->retry_backoff.attempts_num);
        qm_agent_auth_stop_retry(auth);
        auth->retry_exhausted = QM_TRUE;
        return (ret != QM_EOK) ? ret : -QM_ERROR;
    }

    schedule_ret = qm_agent_control_schedule(auth->owner,
                                             QM_AGENT_CONTROL_AUTH_RETRY,
                                             delay_ms, 0U,
                                             auth->retry_generation);
    if (schedule_ret != QM_EOK) {
        QM_AGENT_LOGE(LOG_TAG,
                           "getAgentToken retry schedule failed ret=%d delay=%u reason=%s",
                           schedule_ret,
                           (unsigned int)delay_ms,
                           reason ? reason : "");
        qm_agent_auth_stop_retry(auth);
        auth->retry_exhausted = QM_TRUE;
        return schedule_ret;
    }

    QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                       "getAgentToken retry scheduled attempt=%u delay=%u reason=%s ret=%d reply_code=%d",
                       (unsigned int)auth->retry_backoff.attempts_num,
                       (unsigned int)delay_ms,
                       reason ? reason : "",
                       ret,
                       (int)reply_code);
    return QM_EOK;
}

/**
 * @brief Arm the reply deadline for the active request generation.
 * 为活动请求 generation 设置回复截止时间。
 */
static int wait_for_reply(qm_agent_auth_t *auth)
{
    int ret;

    if (!auth->retry_active) return QM_EOK;
    auth->awaiting_reply = QM_TRUE;
    ret = qm_agent_control_schedule(auth->owner, QM_AGENT_CONTROL_AUTH_RETRY,
                                    QM_AGENT_AUTH_REPLY_TIMEOUT_MS, 0U,
                                    auth->retry_generation);
    if (ret != QM_EOK) {
        qm_agent_auth_stop_retry(auth);
        auth->retry_exhausted = QM_TRUE;
    }
    return ret;
}

static int parse_uint_field(const cJSON *json, const char *key, uint32_t *value)
{
    const cJSON *field;

    if (json == NULL || key == NULL || value == NULL) {
        return -QM_EINVAL;
    }

    field = cJSON_GetObjectItem(json, key);
    if (field == NULL) {
        return -QM_ENOENT;
    }

    if (cJSON_IsNumber(field)) {
        double number = cJSON_GetNumberValue(field);
        uint32_t number_value;

        if (number < 0.0 || number > QM_AGENT_AUTH_UINT32_MAX_VALUE) {
            return -QM_EINVAL;
        }

        number_value = (uint32_t)number;
        if ((double)number_value != number) {
            return -QM_EINVAL;
        }

        *value = number_value;
        return QM_EOK;
    }

    if (cJSON_IsString(field)) {
        const char *string_value = cJSON_GetStringValue(field);
        uint32_t string_len;

        if (string_value == NULL) {
            return -QM_ERROR;
        }

        string_len = qm_strlen(string_value);
        if (string_len == 0U || string_len > 255U) {
            return -QM_ERROR;
        }

        return qm_str2uint((char *)string_value, (uint8_t)string_len, value);
    }

    return -QM_ERROR;
}

static int parse_string_field(const cJSON *json, const char *key, char **out)
{
    const cJSON *field;
    const char *string_value;

    if (json == NULL || key == NULL || out == NULL) {
        return -QM_EINVAL;
    }

    *out = NULL;

    field = cJSON_GetObjectItem(json, key);
    if (field == NULL) {
        return -QM_ENOENT;
    }

    if (!cJSON_IsString(field)) {
        return -QM_ERROR;
    }

    string_value = cJSON_GetStringValue(field);
    if (string_value == NULL || string_value[0] == '\0') {
        return -QM_ERROR;
    }

    *out = qm_strdup(string_value);
    return (*out != NULL) ? QM_EOK : -QM_ENOMEM;
}

static int parse_string_or_number_field(const cJSON *json, const char *key, char **out)
{
    const cJSON *field;
    const char *string_value;
    char number_buffer[32];
    int ret;

    if (json == NULL || key == NULL || out == NULL) {
        return -QM_EINVAL;
    }

    *out = NULL;

    field = cJSON_GetObjectItem(json, key);
    if (field == NULL) {
        return -QM_ENOENT;
    }

    if (cJSON_IsString(field)) {
        string_value = cJSON_GetStringValue(field);
        if (string_value == NULL || string_value[0] == '\0') {
            return -QM_ERROR;
        }

        *out = qm_strdup(string_value);
        return (*out != NULL) ? QM_EOK : -QM_ENOMEM;
    }

    if (!cJSON_IsNumber(field)) {
        return -QM_ERROR;
    }

    ret = qm_snprintf(number_buffer,
                      sizeof(number_buffer),
                      "%.0f",
                      cJSON_GetNumberValue(field));
    if (ret <= 0 || (uint32_t)ret >= sizeof(number_buffer)) {
        return -QM_EFULL;
    }

    *out = qm_strdup(number_buffer);
    return (*out != NULL) ? QM_EOK : -QM_ENOMEM;
}

static int parse_optional_string_field(const cJSON *json, const char *key, char **out)
{
    const cJSON *field;
    const char *string_value;

    if (json == NULL || key == NULL || out == NULL) {
        return -QM_EINVAL;
    }

    *out = NULL;

    field = cJSON_GetObjectItem(json, key);
    if (field == NULL) {
        return QM_EOK;
    }

    if (!cJSON_IsString(field)) {
        return -QM_ERROR;
    }

    string_value = cJSON_GetStringValue(field);
    if (string_value == NULL || string_value[0] == '\0') {
        return QM_EOK;
    }

    *out = qm_strdup(string_value);
    return (*out != NULL) ? QM_EOK : -QM_ENOMEM;
}

static int parse_int_field(const cJSON *json, const char *key, int32_t *value)
{
    const cJSON *field;
    uint32_t parsed;
    int ret;

    if (json == NULL || key == NULL || value == NULL) {
        return -QM_EINVAL;
    }

    field = cJSON_GetObjectItem(json, key);
    if (field == NULL) {
        return -QM_ENOENT;
    }

    if (cJSON_IsNumber(field)) {
        *value = (int32_t)cJSON_GetNumberValue(field);
        return QM_EOK;
    }

    if (!cJSON_IsString(field) || cJSON_GetStringValue(field) == NULL) {
        return -QM_ERROR;
    }

    ret = qm_str2uint((char *)cJSON_GetStringValue(field),
                      (uint8_t)qm_strlen(cJSON_GetStringValue(field)),
                      &parsed);
    if (ret != QM_EOK) {
        return ret;
    }

    *value = (int32_t)parsed;
    return QM_EOK;
}

void qm_agent_auth_info_deinit(qm_agent_auth_info_t *info)
{
    if (info == NULL) {
        return;
    }

    qm_free(info->ljyd.token);
    qm_free(info->ljyd.user_id);

    switch (info->type) {
        case QM_AGENT_AUTH_TYPE_JOYINSIDE:
            qm_free(info->data.joyinside.access_token);
            qm_free(info->data.joyinside.bot_id);
            break;
        case QM_AGENT_AUTH_TYPE_AGORA:
            qm_free(info->data.agora.license);
            qm_free(info->data.agora.token);
            qm_free(info->data.agora.app_id);
            qm_free(info->data.agora.channel_name);
            break;
        case QM_AGENT_AUTH_TYPE_BAIDU:
            qm_free(info->data.baidu.license_key);
            qm_free(info->data.baidu.instance_id);
            qm_free(info->data.baidu.app_id);
            qm_free(info->data.baidu.audio_codec);
            qm_free(info->data.baidu.token);
            break;
        case QM_AGENT_AUTH_TYPE_LGEINE:
            /* The lgeine payload is the shared ljyd struct released above. */
            break;
        case QM_AGENT_AUTH_TYPE_NONE:
        default:
            break;
    }

    qm_memset(info, 0, sizeof(*info));
}

static int clone_auth_info(const qm_agent_auth_info_t *src,
                           qm_agent_auth_info_t *dst)
{
    int ret;

    if (src == NULL || dst == NULL) {
        return -QM_EINVAL;
    }

    qm_memset(dst, 0, sizeof(*dst));
    dst->msg_id = src->msg_id;
    dst->code = src->code;
    dst->timestamp = src->timestamp;
    dst->type = src->type;

    dst->ljyd.expire_time = src->ljyd.expire_time;
    if (src->ljyd.token != NULL) {
        dst->ljyd.token = qm_strdup(src->ljyd.token);
        if (dst->ljyd.token == NULL) {
            ret = -QM_ENOMEM;
            goto err;
        }
    }
    if (src->ljyd.user_id != NULL) {
        dst->ljyd.user_id = qm_strdup(src->ljyd.user_id);
        if (dst->ljyd.user_id == NULL) {
            ret = -QM_ENOMEM;
            goto err;
        }
    }

    switch (src->type) {
        case QM_AGENT_AUTH_TYPE_JOYINSIDE:
            dst->data.joyinside.expire_time = src->data.joyinside.expire_time;
            if (src->data.joyinside.access_token != NULL) {
                dst->data.joyinside.access_token =
                    qm_strdup(src->data.joyinside.access_token);
                if (dst->data.joyinside.access_token == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            if (src->data.joyinside.bot_id != NULL) {
                dst->data.joyinside.bot_id =
                    qm_strdup(src->data.joyinside.bot_id);
                if (dst->data.joyinside.bot_id == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            break;

        case QM_AGENT_AUTH_TYPE_AGORA:
            dst->data.agora.uid = src->data.agora.uid;
            dst->data.agora.expire_time = src->data.agora.expire_time;
            if (src->data.agora.license != NULL) {
                dst->data.agora.license = qm_strdup(src->data.agora.license);
                if (dst->data.agora.license == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            if (src->data.agora.token != NULL) {
                dst->data.agora.token = qm_strdup(src->data.agora.token);
                if (dst->data.agora.token == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            if (src->data.agora.app_id != NULL) {
                dst->data.agora.app_id = qm_strdup(src->data.agora.app_id);
                if (dst->data.agora.app_id == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            if (src->data.agora.channel_name != NULL) {
                dst->data.agora.channel_name =
                    qm_strdup(src->data.agora.channel_name);
                if (dst->data.agora.channel_name == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            break;

        case QM_AGENT_AUTH_TYPE_BAIDU:
            dst->data.baidu.expire_time = src->data.baidu.expire_time;
            if (src->data.baidu.license_key != NULL) {
                dst->data.baidu.license_key = qm_strdup(src->data.baidu.license_key);
                if (dst->data.baidu.license_key == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            if (src->data.baidu.instance_id != NULL) {
                dst->data.baidu.instance_id = qm_strdup(src->data.baidu.instance_id);
                if (dst->data.baidu.instance_id == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            if (src->data.baidu.app_id != NULL) {
                dst->data.baidu.app_id = qm_strdup(src->data.baidu.app_id);
                if (dst->data.baidu.app_id == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            if (src->data.baidu.audio_codec != NULL) {
                dst->data.baidu.audio_codec = qm_strdup(src->data.baidu.audio_codec);
                if (dst->data.baidu.audio_codec == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            if (src->data.baidu.token != NULL) {
                dst->data.baidu.token = qm_strdup(src->data.baidu.token);
                if (dst->data.baidu.token == NULL) {
                    ret = -QM_ENOMEM;
                    goto err;
                }
            }
            break;

        case QM_AGENT_AUTH_TYPE_LGEINE:
            /* The lgeine payload is the shared ljyd struct cloned above. */
            break;

        case QM_AGENT_AUTH_TYPE_NONE:
        default:
            break;
    }

    return QM_EOK;

err:
    qm_agent_auth_info_deinit(dst);
    return ret;
}

static int parse_ljyd_auth(const cJSON *params, qm_agent_auth_ljyd_auth_t *auth)
{
    const cJSON *ljyd;
    int ret;

    if (params == NULL || auth == NULL) {
        return -QM_EINVAL;
    }

    ljyd = cJSON_GetObjectItem(params, "ljyd");
    if (ljyd == NULL) {
        return -QM_ENOENT;
    }

    if (!cJSON_IsObject(ljyd)) {
        return -QM_ERROR;
    }

    ret = parse_string_field(ljyd, "token", &auth->token);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_field(ljyd, "userId", &auth->user_id);
    if (ret == -QM_ENOENT) {
        ret = QM_EOK;
    }
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_uint_field(ljyd, "expireTime", &auth->expire_time);
    if (ret == -QM_ENOENT) {
        ret = QM_EOK;
    }
    if (ret != QM_EOK) {
        goto err;
    }

    return QM_EOK;

err:
    qm_free(auth->token);
    qm_free(auth->user_id);
    auth->token = NULL;
    auth->user_id = NULL;
    auth->expire_time = 0;
    return ret;
}

static int parse_joyinside_auth(const cJSON *params,
                                qm_agent_auth_joyinside_auth_t *auth)
{
    const cJSON *joyinside;
    int ret;

    if (params == NULL || auth == NULL) {
        return -QM_EINVAL;
    }

    joyinside = cJSON_GetObjectItem(params, "joyinside");
    if (joyinside == NULL) {
        return -QM_ENOENT;
    }

    if (!cJSON_IsObject(joyinside)) {
        return -QM_ERROR;
    }

    ret = parse_string_field(joyinside,
                             "access_token",
                             &auth->access_token);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_field(joyinside, "botId", &auth->bot_id);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_uint_field(joyinside,
                           "expireTime",
                           &auth->expire_time);
    if (ret == -QM_ENOENT) {
        ret = QM_EOK;
    }
    if (ret != QM_EOK) {
        goto err;
    }

    return QM_EOK;

err:
    qm_free(auth->access_token);
    qm_free(auth->bot_id);
    auth->access_token = NULL;
    auth->bot_id = NULL;
    auth->expire_time = 0;
    return ret;
}

static int parse_agora_auth(const cJSON *params, qm_agent_auth_agora_auth_t *auth)
{
    const cJSON *agora;
    int ret;

    if (params == NULL || auth == NULL) {
        return -QM_EINVAL;
    }

    agora = cJSON_GetObjectItem(params, "agora");
    if (agora == NULL) {
        return -QM_ENOENT;
    }

    if (!cJSON_IsObject(agora)) {
        return -QM_ERROR;
    }

    ret = parse_optional_string_field(agora, "license", &auth->license);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_field(agora, "token", &auth->token);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_field(agora, "appid", &auth->app_id);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_uint_field(agora, "uid", &auth->uid);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_field(agora,
                             "channelName",
                             &auth->channel_name);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_uint_field(agora, "expireTime", &auth->expire_time);
    if (ret == -QM_ENOENT) {
        ret = QM_EOK;
    }
    if (ret != QM_EOK) {
        goto err;
    }

    return QM_EOK;

err:
    qm_free(auth->license);
    qm_free(auth->token);
    qm_free(auth->app_id);
    qm_free(auth->channel_name);
    auth->license = NULL;
    auth->token = NULL;
    auth->app_id = NULL;
    auth->channel_name = NULL;
    auth->uid = 0;
    auth->expire_time = 0;
    return ret;
}

static int parse_baidu_auth(const cJSON *params, qm_agent_auth_baidu_auth_t *auth)
{
    const cJSON *baidu;
    int ret;

    if (params == NULL || auth == NULL) {
        return -QM_EINVAL;
    }

    baidu = cJSON_GetObjectItem(params, "baidu");
    if (baidu == NULL) {
        return -QM_ENOENT;
    }

    if (!cJSON_IsObject(baidu)) {
        return -QM_ERROR;
    }

    ret = parse_string_field(baidu, "license", &auth->license_key);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_or_number_field(baidu, "instanceId", &auth->instance_id);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_field(baidu, "appId", &auth->app_id);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_field(baidu, "audiocodec", &auth->audio_codec);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_string_field(baidu, "token", &auth->token);
    if (ret != QM_EOK) {
        goto err;
    }

    ret = parse_uint_field(baidu, "expireTime", &auth->expire_time);
    if (ret == -QM_ENOENT) {
        ret = QM_EOK;
    }
    if (ret != QM_EOK) {
        goto err;
    }

    return QM_EOK;

err:
    qm_free(auth->license_key);
    qm_free(auth->instance_id);
    qm_free(auth->app_id);
    qm_free(auth->audio_codec);
    qm_free(auth->token);
    auth->license_key = NULL;
    auth->instance_id = NULL;
    auth->app_id = NULL;
    auth->audio_codec = NULL;
    auth->token = NULL;
    auth->expire_time = 0U;
    return ret;
}

/**
 * @brief Parse one provider-independent auth reply into owned credential storage.
 * 将一条供应商无关的认证回复解析到自有凭据存储。
 *
 * All accepted strings are duplicated, so a parsed result is independent from
 * the JSON buffer and can outlive the receive callback.
 * 所有接收的字符串均深拷贝，因此解析结果独立于 JSON 缓冲区，可跨越接收回调生命周期。
 */
static int parse_auth_payload(qm_agent_auth_t *auth,
                              const cJSON *params,
                              qm_agent_auth_info_t *parsed)
{
    int ret;

    if (auth == NULL || params == NULL || parsed == NULL) {
        return -QM_EINVAL;
    }

    ret = parse_ljyd_auth(params, &parsed->ljyd);
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        return ret;
    }

    ret = parse_joyinside_auth(params, &parsed->data.joyinside);
    if (ret == QM_EOK) {
        parsed->type = QM_AGENT_AUTH_TYPE_JOYINSIDE;
        return QM_EOK;
    }

    qm_free(parsed->data.joyinside.access_token);
    qm_free(parsed->data.joyinside.bot_id);
    parsed->data.joyinside.access_token = NULL;
    parsed->data.joyinside.bot_id = NULL;
    parsed->data.joyinside.expire_time = 0;

    ret = parse_agora_auth(params, &parsed->data.agora);
    if (ret == QM_EOK) {
        parsed->type = QM_AGENT_AUTH_TYPE_AGORA;
        return QM_EOK;
    }
    if (ret != -QM_ENOENT) {
        QM_AGENT_LOGE(LOG_TAG,
                           "parse agora auth failed ret=%d", ret);
        return ret;
    }

    ret = parse_baidu_auth(params, &parsed->data.baidu);
    if (ret == QM_EOK) {
        parsed->type = QM_AGENT_AUTH_TYPE_BAIDU;
        return QM_EOK;
    }
    if (ret != -QM_ENOENT) {
        QM_AGENT_LOGE(LOG_TAG,
                           "parse baidu auth failed ret=%d", ret);
        return ret;
    }

    /* A reply carrying only the common ljyd token maps to the lgeine
     * transport, which authenticates through the same ljyd token. */
    if (parsed->ljyd.token != NULL) {
        parsed->type = QM_AGENT_AUTH_TYPE_LGEINE;
        return QM_EOK;
    }

    return -QM_ENOENT;
}

/**
 * @brief Replace cached credentials and notify the owning Agent session.
 * 替换缓存凭据并通知所属 Agent 会话。
 */
static int qm_agent_auth_publish_parsed(qm_agent_auth_t *auth,
                                        qm_agent_auth_info_t *parsed)
{
    if (auth == NULL || parsed == NULL) {
        return -QM_EINVAL;
    }

    qm_agent_auth_stop_retry(auth);
    qm_agent_auth_info_deinit(&auth->cached_auth);
    auth->cached_auth = *parsed;
    auth->has_cached_auth = QM_TRUE;
    qm_memset(parsed, 0, sizeof(*parsed));

    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_CONTROL,
                       "getAgentToken parsed type=%d id=%u",
                       auth->cached_auth.type,
                       auth->cached_auth.msg_id);

    qm_agent_auth_ready(auth->owner);

    return QM_EOK;
}

#if QM_AGENT_AUTH_USE_HTTP
static int qm_agent_auth_parse_http_reply(qm_agent_auth_t *auth,
                                          const char *payload,
                                          uint32_t payload_len)
{
    qm_agent_auth_info_t parsed = {0};
    cJSON *root;
    const cJSON *result;
    char *error_msg = NULL;
    int ret;

    if (auth == NULL || payload == NULL || payload_len == 0U) {
        return -QM_EINVAL;
    }

    root = cJSON_ParseWithLength(payload, payload_len);
    if (root == NULL) {
        return -QM_EINVAL;
    }

    ret = parse_int_field(root, "status", &parsed.code);
    if (ret != QM_EOK) {
        cJSON_Delete(root);
        return ret;
    }

    parsed.msg_id = auth->request_msg_id;
    result = cJSON_GetObjectItem(root, "result");
    if (!cJSON_IsObject(result)) {
        ret = -QM_EINVAL;
        goto out;
    }

    ret = parse_auth_payload(auth, result, &parsed);
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        goto out;
    }

    if (parsed.code != 0 ||
        (parsed.type != QM_AGENT_AUTH_TYPE_JOYINSIDE &&
         parsed.type != QM_AGENT_AUTH_TYPE_AGORA &&
         parsed.type != QM_AGENT_AUTH_TYPE_BAIDU &&
         parsed.type != QM_AGENT_AUTH_TYPE_LGEINE)) {
        (void)parse_optional_string_field(root, "msg", &error_msg);
        QM_AGENT_LOGE(LOG_TAG,
                           "http getAgentToken failed code=%d type=%d msg=%s",
                           (int)parsed.code,
                           parsed.type,
                           error_msg ? error_msg : "");
        qm_free(error_msg);
        ret = -QM_EIO;
        goto out;
    }

    ret = qm_agent_auth_publish_parsed(auth, &parsed);

out:
    qm_agent_auth_info_deinit(&parsed);
    cJSON_Delete(root);
    return ret;
}

static int qm_agent_auth_send_http_request(qm_agent_auth_t *auth)
{
    qm_http_client_request_t http_req;
    qm_http_client_response_t http_resp;
    qm_agent_auth_http_rsp_t http_rsp;
    char request_url[QM_AGENT_AUTH_HTTP_URL_MAX_LEN] = {0};
    char *payload = NULL;
    char *rsp_payload = NULL;
    uint32_t timeout_ms;
    int ret;

    if (auth == NULL) {
        return -QM_EINVAL;
    }

    if (!qm_agent_auth_http_config_valid(&auth->http_config)) {
        return -QM_EINVAL;
    }

    ret = qm_agent_auth_http_build_url(&auth->http_config,
                                       request_url,
                                       sizeof(request_url));
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_agent_auth_http_build_payload(&auth->http_config, &payload);
    if (ret != QM_EOK) {
        return ret;
    }

    rsp_payload = (char *)qm_malloc(QM_AGENT_AUTH_HTTP_RESPONSE_MAX_LEN);
    if (rsp_payload == NULL) {
        cJSON_free(payload);
        return -QM_ENOMEM;
    }
    qm_memset(rsp_payload, 0, QM_AGENT_AUTH_HTTP_RESPONSE_MAX_LEN);

    qm_memset(&http_req, 0, sizeof(http_req));
    qm_memset(&http_resp, 0, sizeof(http_resp));
    qm_memset(&http_rsp, 0, sizeof(http_rsp));

    timeout_ms = (auth->http_config.timeout_ms != 0U) ?
                     auth->http_config.timeout_ms :
                     QM_AGENT_AUTH_HTTP_TIMEOUT_MS;

    http_rsp.buf = rsp_payload;
    http_rsp.buf_len = QM_AGENT_AUTH_HTTP_RESPONSE_MAX_LEN;

    http_req.method = QM_HTTP_POST;
    http_req.url = request_url;
    http_req.body = payload;
    http_req.body_len = (int)qm_strlen(payload);
    http_req.content_type = "application/json";
    http_req.timeout_ms = timeout_ms;
    http_req.max_body_len = QM_AGENT_AUTH_HTTP_RESPONSE_MAX_LEN - 1U;
    http_req.user = &http_rsp;
    http_req.on_body = qm_agent_auth_http_body_cb;

    ret = qm_http_client_execute(&http_req, &http_resp);
    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_CONTROL,
                       "auth http ret=%d status=%d body=%u",
                       ret,
                       http_resp.status_code,
                       (unsigned int)http_rsp.used);
    if (ret != QM_EOK) {
        goto out;
    }

    if (http_resp.status_code != 200 || http_rsp.used == 0U) {
        ret = -QM_EIO;
        goto out;
    }

    ret = qm_agent_auth_parse_http_reply(auth, rsp_payload, http_rsp.used);

out:
    if (payload != NULL) {
        cJSON_free(payload);
    }
    qm_free(rsp_payload);
    return ret;
}
#endif

#if !QM_AGENT_AUTH_USE_HTTP
static void parse_mqtt_reply(qm_agent_auth_t *auth, const char *payload, uint32_t len)
{
    qm_agent_auth_info_t parsed = {0};
    cJSON *root;
    const cJSON *method;
    const cJSON *params;
    const cJSON *result;
    char *error_msg;
    int ret;

    if (!auth->retry_active) return;
    root = cJSON_ParseWithLength(payload, len);
    if (root == NULL) {
        return;
    }

    method = cJSON_GetObjectItem(root, "method");
    if (!cJSON_IsString(method) ||
        qm_strcmp(cJSON_GetStringValue(method),
                  QM_AGENT_AUTH_METHOD_GET_AGENT_TOKEN_REPLY) != 0) {
        goto out;
    }

    result = cJSON_GetObjectItem(root, "result");
    if (cJSON_IsObject(result)) {
        ret = parse_int_field(result, "code", &parsed.code);
        if (ret != QM_EOK) {
            goto out;
        }
    } else {
        ret = parse_int_field(root, "code", &parsed.code);
        if (ret != QM_EOK) {
            goto out;
        }
    }

    ret = parse_uint_field(root, "id", &parsed.msg_id);
    if (ret != QM_EOK) {
        goto out;
    }
    if (auth->request_msg_id != 0U && parsed.msg_id != auth->request_msg_id) {
        goto out;
    }

    ret = parse_uint_field(root, "timestamp", &parsed.timestamp);
    if (ret == -QM_ENOENT) {
        ret = parse_uint_field(root, "timestap", &parsed.timestamp);
        if (ret == -QM_ENOENT) {
            ret = QM_EOK;
        }
    }
    if (ret != QM_EOK) {
        goto out;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (!cJSON_IsObject(params)) {
        params = cJSON_GetObjectItem(root, "result");
        if (!cJSON_IsObject(params)) {
            goto out;
        }
    }

    ret = parse_auth_payload(auth, params, &parsed);
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        goto out;
    }

    if (parsed.code != 0 ||
        (parsed.type != QM_AGENT_AUTH_TYPE_JOYINSIDE &&
         parsed.type != QM_AGENT_AUTH_TYPE_AGORA &&
         parsed.type != QM_AGENT_AUTH_TYPE_BAIDU &&
         parsed.type != QM_AGENT_AUTH_TYPE_LGEINE)) {
        error_msg = NULL;
        if (cJSON_IsObject(result)) {
            (void)parse_optional_string_field(result, "msg", &error_msg);
        }
        QM_AGENT_LOGE(LOG_TAG,
                           "getAgentTokenReply failed code=%d type=%d msg=%s",
                           (int)parsed.code,
                           parsed.type,
                           error_msg ? error_msg : "");
        (void)qm_agent_auth_schedule_retry(auth,
                                           "reply_error",
                                           QM_EOK,
                                           parsed.code);
        qm_free(error_msg);
        goto out;
    }

    (void)qm_agent_auth_publish_parsed(auth, &parsed);

out:
    qm_agent_auth_info_deinit(&parsed);
    cJSON_Delete(root);
}
#endif

/**
 * @brief Parse and publish MQTT input copied by the receive callback.
 * 解析并发布由接收回调复制的 MQTT 输入。
 *
 * This runs on qm_agent_ctl and is the only path that changes auth cache state
 * or schedules the next connection reconciliation.
 * 本函数运行在 qm_agent_ctl 上，是唯一会变更认证缓存或安排下一次连接协调的路径。
 */
void qm_agent_auth_process_pending(qm_agent_auth_t *auth)
{
#if !QM_AGENT_AUTH_USE_HTTP
    char *reply;
    uint32_t len;
    qm_bool disconnected;

    if (auth == NULL) return;
    qm_mutex_lock(&s_auth_lock, QM_WAIT_FOREVER);
    reply = auth->pending_reply;
    len = auth->pending_reply_len;
    disconnected = auth->disconnected;
    auth->pending_reply = NULL;
    auth->pending_reply_len = 0U;
    auth->disconnected = QM_FALSE;
    qm_mutex_unlock(&s_auth_lock);
    if (disconnected) {
        qm_agent_auth_cancel(auth);
        auth->has_cached_auth = QM_FALSE;
        auth->owner->auth.has_auth = QM_FALSE;
    }
    if (reply != NULL) parse_mqtt_reply(auth, reply, len);
    qm_free(reply);
#else
    QM_UNUSED_ARG(auth);
#endif
}

/**
 * @brief Create one auth helper and register its provider callback identity.
 * 创建认证辅助对象并注册其供应商回调标识。
 *
 * MQTT callbacks receive only a never-reused cookie; deinit removes that
 * identity before releasing the helper. MQTT 回调只接收永不复用的 cookie；释放辅助对象前
 * deinit 会先移除该标识。
 */
qm_agent_auth_t *qm_agent_auth_init(qm_agent_handle_t owner)
{
    qm_agent_auth_t *ctx;

#if !QM_AGENT_AUTH_USE_HTTP
    int ret;
    qm_list_node_t *node;

    if (owner == NULL || owner->config.iot_handle == NULL) {
        return NULL;
    }
    if (!qm_mutex_is_valid(&s_auth_lock) && qm_mutex_new(&s_auth_lock) != QM_EOK) {
        return NULL;
    }
#else
    if (owner == NULL) {
        return NULL;
    }
#endif

    ctx = (qm_agent_auth_t *)qm_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return NULL;
    }

    qm_memset(ctx, 0, sizeof(*ctx));
    ctx->owner = owner;
    ctx->iot_handle = owner->config.iot_handle;
    backoff_algorithm_init(&ctx->retry_backoff,
                           QM_AGENT_AUTH_RETRY_BASE_DELAY_MS,
                           QM_AGENT_AUTH_RETRY_MAX_DELAY_MS,
                           QM_AGENT_AUTH_RETRY_MAX_ATTEMPTS);

#if !QM_AGENT_AUTH_USE_HTTP
    qm_mutex_lock(&s_auth_lock, QM_WAIT_FOREVER);
    for (node = s_auth_registry.head; node != NULL; node = node->next) {
        qm_agent_auth_t *other = node->val;
        /* qm_iot has one method handler per IoT session. */
        if (other->iot_handle == ctx->iot_handle) break;
    }
    if (node != NULL || s_next_cookie == UINT32_MAX) {
        qm_mutex_unlock(&s_auth_lock);
        qm_free(ctx);
        return NULL;
    }
    ctx->cookie = ++s_next_cookie;
    ctx->cloud_online = QM_TRUE;
    qm_list_node_init(&ctx->registration, ctx);
    qm_list_rpush(&s_auth_registry, &ctx->registration);
    qm_mutex_unlock(&s_auth_lock);
    ret = qm_iot_down_register(ctx->iot_handle,
                               (char *)QM_AGENT_AUTH_METHOD_GET_AGENT_TOKEN_REPLY,
                               auth_reply_recv_handler,
                               (void *)(uintptr_t)ctx->cookie);
    if (ret == QM_EOK) {
        ret = qm_iot_event_register(ctx->iot_handle, auth_cloud_event,
                                    (void *)(uintptr_t)ctx->cookie);
        if (ret != QM_EOK) {
            (void)qm_iot_down_register(ctx->iot_handle,
                (char *)QM_AGENT_AUTH_METHOD_GET_AGENT_TOKEN_REPLY, NULL, NULL);
        }
    }
    if (ret != QM_EOK) {
        qm_mutex_lock(&s_auth_lock, QM_WAIT_FOREVER);
        qm_list_remove(&s_auth_registry, &ctx->registration);
        qm_mutex_unlock(&s_auth_lock);
        qm_free(ctx->pending_reply);
        qm_free(ctx);
        return NULL;
    }
#endif

    return ctx;
}

#if QM_AGENT_AUTH_USE_HTTP
/**
 * @brief Deep-copy HTTP configuration into auth-owned storage.
 * 将 HTTP 配置深拷贝到 auth 自有存储。
 */
int qm_agent_auth_set_http_config(qm_agent_auth_t *auth,
                                  const qm_agent_auth_http_config_t *config)
{
    qm_agent_auth_http_config_t updated = {0};

    if (auth == NULL || config == NULL) {
        return -QM_EINVAL;
    }

    if (config->env > QM_AGENT_AUTH_ENV_PROD) {
        return -QM_EINVAL;
    }

    updated.env = config->env;
    updated.product_id = config->product_id;
    updated.timeout_ms = config->timeout_ms;
    updated.retry_num = config->retry_num;

    if (config->base_url != NULL && config->base_url[0] != '\0') {
        updated.base_url = qm_strdup(config->base_url);
        if (updated.base_url == NULL) {
            qm_agent_auth_http_config_clear(&updated);
            return -QM_ENOMEM;
        }
    }

    if (config->product_secret != NULL && config->product_secret[0] != '\0') {
        updated.product_secret = qm_strdup(config->product_secret);
        if (updated.product_secret == NULL) {
            qm_agent_auth_http_config_clear(&updated);
            return -QM_ENOMEM;
        }
    }

    if (config->sn != NULL && config->sn[0] != '\0') {
        updated.sn = qm_strdup(config->sn);
        if (updated.sn == NULL) {
            qm_agent_auth_http_config_clear(&updated);
            return -QM_ENOMEM;
        }
    }

    qm_agent_auth_http_config_clear(&auth->http_config);
    auth->http_config = updated;
    return QM_EOK;
}
#endif

/**
 * @brief Unregister provider callbacks and release one auth helper.
 * 注销供应商回调并释放一个认证辅助对象。
 */
int qm_agent_auth_deinit(qm_agent_auth_t *auth)
{
    if (auth == NULL) {
        return -QM_EINVAL;
    }

    qm_agent_auth_stop_retry(auth);

#if !QM_AGENT_AUTH_USE_HTTP
    /* Joining the control task excludes parsing; removing the ID excludes
     * callbacks already copied out by qm_iot before its unregister call. */
    qm_mutex_lock(&s_auth_lock, QM_WAIT_FOREVER);
    qm_list_remove(&s_auth_registry, &auth->registration);
    qm_mutex_unlock(&s_auth_lock);
    (void)qm_iot_event_unregister_one(auth->iot_handle, auth_cloud_event,
                                       (void *)(uintptr_t)auth->cookie);
    (void)qm_iot_down_register(auth->iot_handle,
                               (char *)QM_AGENT_AUTH_METHOD_GET_AGENT_TOKEN_REPLY,
                               NULL,
                               NULL);
    qm_free(auth->pending_reply);
#endif

    qm_agent_auth_info_deinit(&auth->cached_auth);
#if QM_AGENT_AUTH_USE_HTTP
    qm_agent_auth_http_config_clear(&auth->http_config);
#endif
    qm_free(auth);
    return QM_EOK;
}

/**
 * @brief Submit the current credential request over the selected auth channel.
 * 通过选定认证通道提交当前凭据请求。
 */
static int qm_agent_auth_send_request(qm_agent_auth_t *auth)
{
    if (auth == NULL) {
        return -QM_EINVAL;
    }

#if QM_AGENT_AUTH_USE_HTTP
    return qm_agent_auth_send_http_request(auth);
#else
    qm_bool online;
    if (auth->iot_handle == NULL) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&s_auth_lock, QM_WAIT_FOREVER);
    online = auth->cloud_online;
    qm_mutex_unlock(&s_auth_lock);
    if (!online) return -QM_EBUSY;

    return qm_iot_up_report(auth->iot_handle,
                            (char *)QM_AGENT_AUTH_METHOD_GET_AGENT_TOKEN,
                            auth->request_msg_id,
                            NULL,
                            0);
#endif
}

/**
 * @brief Execute a retry only when its generation still identifies this cycle.
 * 仅当 generation 仍标识当前认证周期时执行重试。
 */
int qm_agent_auth_retry_now(qm_agent_auth_t *auth, uint32_t generation)
{
    int ret;

    if (auth == NULL || !auth->retry_active ||
        auth->retry_generation != generation) {
        return -QM_EBUSY;
    }

    /* A reply deadline starts backoff; it must not publish a new request. */
    if (auth->awaiting_reply) {
        return qm_agent_auth_schedule_retry(auth, "reply_timeout", -QM_ETIMEOUT, 0);
    }

    ret = qm_agent_auth_send_request(auth);
    if (ret == QM_EOK) {
        QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_CONTROL,
                      "getAgentToken retried");
        return wait_for_reply(auth);
    }

    QM_AGENT_LOGE(LOG_TAG, "getAgentToken retry request failed ret=%d", ret);
    return qm_agent_auth_schedule_retry(auth, "request_error", ret, 0);
}

/**
 * @brief Start one authentication request on the configured MQTT or HTTP channel.
 * 通过已配置的 MQTT 或 HTTP 通道发起一次认证请求。
 */
int qm_agent_auth_request_token(qm_agent_auth_t *auth, uint32_t msg_id)
{
    int ret;

    if (auth == NULL) {
        return -QM_EINVAL;
    }
    if (auth->retry_active) return QM_EOK;
    if (auth->retry_exhausted) return -QM_ERROR;

#if !QM_AGENT_AUTH_USE_HTTP
    if (auth->iot_handle == NULL) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&s_auth_lock, QM_WAIT_FOREVER);
    if (!auth->cloud_online || (msg_id == 0U && s_next_request == UINT32_MAX)) {
        qm_mutex_unlock(&s_auth_lock);
        return -QM_EBUSY;
    }
    if (msg_id == 0U) msg_id = ++s_next_request;
    qm_mutex_unlock(&s_auth_lock);
#endif

    /* MQTT request IDs remain distinct across Agent destruction/recreation. */
    auth->has_cached_auth = QM_FALSE;
    qm_agent_auth_reset_retry(auth);
    auth->request_msg_id = msg_id != 0U ? msg_id : auth->retry_generation;
    ret = qm_agent_auth_send_request(auth);
    if (ret == QM_EOK) {
        return wait_for_reply(auth);
    }

    QM_AGENT_LOGE(LOG_TAG,
                       "getAgentToken request failed ret=%d", ret);
    return qm_agent_auth_schedule_retry(auth, "request_error", ret, 0);
}

/**
 * @brief Deep-copy the current credential cache for an Agent caller.
 * 为 Agent 调用方深拷贝当前凭据缓存。
 *
 * The caller owns the resulting strings and must release them with
 * qm_agent_auth_info_deinit().
 * 调用方拥有结果字符串，必须通过 qm_agent_auth_info_deinit() 释放。
 */
int qm_agent_auth_get_token(qm_agent_auth_t *auth, qm_agent_auth_info_t *info)
{
    if (auth == NULL || info == NULL) {
        return -QM_EINVAL;
    }

    if (!auth->has_cached_auth) {
        return -QM_ENOENT;
    }

    return clone_auth_info(&auth->cached_auth, info);
}
