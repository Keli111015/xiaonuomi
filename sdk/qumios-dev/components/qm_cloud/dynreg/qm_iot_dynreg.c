#include "qm_cloud_log.h"
#include "qm.h"
#include "qm_iot_dynreg.h"
#include "qm_http_client.h"

#include "cJSON.h"
#include "json_parser.h"
#include "backoff_algorithm.h"

#include "qm_utils_string.h"
#include "qm_utils_hmac.h"

#include "qm_iot_config.h"

#define LOG_TAG "DYNREG"

#define BACKOFF_BASE_TIME_MS    (1 * 1000)

#define DATA_MAX_STR_LEN        (128)
#define RSP_PAYLOAD_MAX_STR_LEN    (8 * 1024)
#define DYNREG_ERROR_BODY_LOG_MAX_LEN (256)
#define QM_IOT_DYNREG_URL_MAX_LEN  512


#define SHADOW_SN_KEY           "sn"
#define SHADOW_STATUS_KEY       "status"
#define SHADOW_RESULT_KEY       "result"
#define SHADOW_DID_KEY          "did"
#define SHADOW_HOST_KEY         "awsBrokerUrl"
#define AI_VOICE_HOST_KEY       "voiceUrl"
#define AGORA_HOST_KEY          "agoraGenTokenUrl"
#define SHADOW_CA_KEY           "awsRootPem"
#define SHADOW_CLIENT_CA_KEY    "certificatePem"
#define SHADOW_CLIENT_KEY       "privateKey"

#define SHADOW_PUBLIC_KEY       "otaPublicKey"

/** 动态注册 nonce 上限，与示例 wifi_combo.c 保持一致。 */
#define QM_IOT_DYNREG_NONCE_MAX         ((1U << 31) - 1)

/** 动态注册 HTTP POST JSON 请求体字段：设备序列号。 */
#define QM_IOT_DYNREG_SN_KEY            "sn"
/** 动态注册 HTTP POST JSON 请求体字段：产品 ID（字符串形式，与云端协议一致）。 */
#define QM_IOT_DYNREG_PID_KEY           "pid"
/** 动态注册 HTTP POST JSON 请求体字段：毫秒时间戳（字符串形式）。 */
#define QM_IOT_DYNREG_TIMESTAMP_KEY     "timestamp"
/** 动态注册 HTTP POST JSON 请求体字段：HMAC-SHA1 签名（十六进制字符串）。 */
#define QM_IOT_DYNREG_SIGNATURE_KEY     "signature"
/** 动态注册 HTTP POST JSON 请求体字段：随机数。 */
#define QM_IOT_DYNREG_NONCE_KEY         "nonce"

#define QM_IOT_DYNREG_SIGNATURE_PATH \
    "/device/api/iot/device/register"

#define QM_IOT_DYNREG_DEV_SIGNATURE_HOST \
    "https://cn-north-1-dev-testcloudserver.xjiangiot.com" QM_IOT_DYNREG_SIGNATURE_PATH
#define QM_IOT_DYNREG_TEST_SIGNATURE_HOST \
    "https://cn-north-1-test-testcloudserver.xjiangiot.com" QM_IOT_DYNREG_SIGNATURE_PATH
#define QM_IOT_DYNREG_PROD_SIGNATURE_HOST \
    "https://cn-north-1-prod-testcloudserver.xjiangiot.com" QM_IOT_DYNREG_SIGNATURE_PATH

typedef struct
{
    char *host;
    uint16_t port;
    uint32_t product_id;
    char *product_secret;
    char *sn;
}qm_iot_dynreg_req_t;

typedef struct
{
    void *userdata;
    uint32_t retry_num;
    uint32_t recv_timeout;
    qm_iot_dynreg_req_t req_data;
    qm_iot_dynreg_recv_handler_t recv_handler;
}qm_iot_dynreg_handle_t;

typedef struct
{
    char *buf;
    uint32_t buf_len;
    uint32_t used;
}qm_iot_dynreg_http_rsp_t;

static qm_iot_dynreg_env_t s_dynreg_env = QM_IOT_DYNREG_ENV_PROD;

static const char *dynreg_default_host_get(void)
{
    switch (s_dynreg_env) {
        case QM_IOT_DYNREG_ENV_DEV:
            return QM_IOT_DYNREG_DEV_SIGNATURE_HOST;

        case QM_IOT_DYNREG_ENV_TEST:
            return QM_IOT_DYNREG_TEST_SIGNATURE_HOST;

        case QM_IOT_DYNREG_ENV_PROD:
        default:
            return QM_IOT_DYNREG_PROD_SIGNATURE_HOST;
    }
}

static int dynreg_http_body_cb(const char *data, int len, void *user)
{
    qm_iot_dynreg_http_rsp_t *rsp = (qm_iot_dynreg_http_rsp_t *)user;

    if (rsp == NULL || data == NULL || len < 0) {
        return -QM_EINVAL;
    }

    if (len == 0) {
        return QM_EOK;
    }

    if (rsp->used + (uint32_t)len >= rsp->buf_len) {
        return -QM_EFULL;
    }

    memcpy(rsp->buf + rsp->used, data, (uint32_t)len);
    rsp->used += (uint32_t)len;
    rsp->buf[rsp->used] = '\0';
    return QM_EOK;
}

static void dynreg_status_notify(qm_iot_dynreg_handle_t *dynreg_handle,
                                 uint32_t code)
{
    qm_iot_dynreg_recv_t dynreg_recv;

    if (dynreg_handle == NULL || dynreg_handle->recv_handler == NULL) {
        return;
    }

    memset(&dynreg_recv, 0, sizeof(dynreg_recv));
    dynreg_recv.type = QM_IOT_DYNREGRECV_STATUS_CODE;
    dynreg_recv.data.status_code.code = code;
    dynreg_handle->recv_handler(dynreg_handle, &dynreg_recv, dynreg_handle->userdata);
}

static qm_bool dynreg_url_has_scheme(const char *host)
{
    return (host != NULL &&
            (strncmp(host, "http://", 7) == 0 ||
             strncmp(host, "https://", 8) == 0));
}

static const char *dynreg_url_suffix_find(const char *str)
{
    if (str == NULL) {
        return NULL;
    }

    while (*str != '\0') {
        if (*str == '/' || *str == '?' || *str == '#') {
            return str;
        }
        str++;
    }

    return NULL;
}

static const char *dynreg_url_path_tail_find(const char *path)
{
    if (path == NULL) {
        return NULL;
    }

    while (*path != '\0') {
        if (*path == '?' || *path == '#') {
            return path;
        }
        path++;
    }

    return path;
}

static qm_bool dynreg_path_is_default_prefix(const char *path,
                                             uint32_t path_len,
                                             const char *default_path)
{
    uint32_t cmp_len = path_len;

    if (path == NULL || default_path == NULL || cmp_len == 0U) {
        return QM_FALSE;
    }

    while (cmp_len > 1U && path[cmp_len - 1U] == '/') {
        cmp_len--;
    }

    if (strncmp(default_path, path, cmp_len) != 0) {
        return QM_FALSE;
    }

    return default_path[cmp_len] != '\0' ? QM_TRUE : QM_FALSE;
}

static qm_bool dynreg_authority_has_port(const char *authority, uint32_t authority_len)
{
    const char *end;
    const char *close;

    if (authority == NULL || authority_len == 0U) {
        return QM_FALSE;
    }

    end = authority + authority_len;
    if (authority[0] == '[') {
        close = memchr(authority, ']', authority_len);
        return (close != NULL && close + 1 < end && close[1] == ':') ? QM_TRUE : QM_FALSE;
    }

    return memchr(authority, ':', authority_len) != NULL ? QM_TRUE : QM_FALSE;
}

static int dynreg_build_url(const char *host,
                            uint16_t port,
                            char *url,
                            uint32_t url_len)
{
    const char *default_path;
    const char *suffix;
    int ret;

    if (host == NULL || host[0] == '\0' || url == NULL || url_len == 0U) {
        return -QM_EINVAL;
    }

    default_path = QM_IOT_DYNREG_SIGNATURE_PATH;

    if (dynreg_url_has_scheme(host)) {
        const char *authority = strstr(host, "://") + 3;

        suffix = dynreg_url_suffix_find(authority);
        if (suffix == NULL) {
            ret = qm_snprintf(url, url_len, "%s%s", host, default_path);
        } else if (*suffix == '/') {
            const char *tail = dynreg_url_path_tail_find(suffix);
            uint32_t path_len = (uint32_t)(tail - suffix);

            if (dynreg_path_is_default_prefix(suffix, path_len, default_path)) {
                ret = qm_snprintf(url,
                                  url_len,
                                  "%.*s%s%s",
                                  (int)(suffix - host),
                                  host,
                                  default_path,
                                  tail);
            } else {
                ret = qm_snprintf(url, url_len, "%s", host);
            }
        } else {
            ret = qm_snprintf(url,
                              url_len,
                              "%.*s%s%s",
                              (int)(suffix - host),
                              host,
                              default_path,
                              suffix);
        }
    } else {
        uint32_t authority_len;
        qm_bool has_port;
        const char *path;
        const char *tail;

        suffix = dynreg_url_suffix_find(host);
        authority_len = suffix ? (uint32_t)(suffix - host) : (uint32_t)strlen(host);
        if (authority_len == 0U) {
            return -QM_EINVAL;
        }

        has_port = dynreg_authority_has_port(host, authority_len);
        if (suffix != NULL && *suffix == '/') {
            const char *path_tail = dynreg_url_path_tail_find(suffix);
            uint32_t path_len = (uint32_t)(path_tail - suffix);

            if (dynreg_path_is_default_prefix(suffix, path_len, default_path)) {
                path = default_path;
                tail = path_tail;
            } else {
                path = suffix;
                tail = "";
            }
        } else {
            path = default_path;
            tail = (suffix != NULL) ? suffix : "";
        }

        if (has_port) {
            ret = qm_snprintf(url,
                              url_len,
                              "https://%.*s%s%s",
                              (int)authority_len,
                              host,
                              path,
                              tail);
        } else {
            ret = qm_snprintf(url,
                              url_len,
                              "https://%.*s:%u%s%s",
                              (int)authority_len,
                              host,
                              port,
                              path,
                              tail);
        }
    }

    if (ret < 0 || (uint32_t)ret >= url_len) {
        return -QM_EFULL;
    }

    return QM_EOK;
}

static int qm_iot_dynreg_recv_handler(void *handle, char *buff, int len)
{
    int type;
    int code = 0;
    char *reuslt = NULL;
    int result_len = 0;
    int sn_len = 0;
    char *value = NULL;
    int value_len = 0;
    int log_len = 0;

    char *did = NULL;
    int did_len = 0;
    qm_iot_dynreg_recv_t dynreg_recv = {0};
    qm_iot_dynreg_handle_t *dynreg_handle = (qm_iot_dynreg_handle_t *)handle;
    if(handle == NULL || buff == NULL || len == 0){
        return -QM_EINVAL;
    }

    //解析数据
    value = qm_json_get_value_by_name(buff, len, SHADOW_STATUS_KEY, &value_len, &type);
    if(value == NULL){
        return -QM_EINVAL;
    }
    code = int_str_to_num(value, value_len);
    if(code != QM_EOK){
        log_len = len;
        if (log_len > DYNREG_ERROR_BODY_LOG_MAX_LEN) {
            log_len = DYNREG_ERROR_BODY_LOG_MAX_LEN;
        }
        QM_CLOUD_LOGE(LOG_TAG,
                      "dynreg response status=%d body_len=%d body=%.*s%s",
                      code,
                      len,
                      log_len,
                      buff,
                      len > log_len ? "..." : "");
        dynreg_recv.data.status_code.code = code;
        dynreg_recv.type = QM_IOT_DYNREGRECV_STATUS_CODE;
        goto __exit;
    }

    dynreg_recv.type = QM_IOT_DYNREGRECV_DEVICE_INFO;

    reuslt = qm_json_get_value_by_name(buff, len, SHADOW_RESULT_KEY, &result_len, &type);
    if(reuslt == NULL){
        QM_CLOUD_LOGE(LOG_TAG, "[reuslt] NULL!!");
        return -QM_EINVAL;
    }
    did = qm_json_get_value_by_name(reuslt, result_len, SHADOW_DID_KEY, (int *)&did_len, &type);
    if(did == NULL || !did_len){
        QM_CLOUD_LOGE(LOG_TAG, "[did] NULL!!");
        return -QM_EINVAL;
    }
    dynreg_recv.data.device_info.did = int_str_to_num(did, did_len);

    (void)qm_json_get_value_by_name(reuslt, result_len, SHADOW_SN_KEY, (int *)&sn_len, &type);

#if CONFIG_QM_IOT_AUDIO_URI_SUPPORT
    (void)qm_json_get_value_by_name(reuslt, result_len, AI_VOICE_HOST_KEY, NULL, &type);
    (void)qm_json_get_value_by_name(reuslt, result_len, AGORA_HOST_KEY, NULL, &type);
#endif

#if CONFIG_QM_IOT_MQTT_SUPPORT

    dynreg_recv.data.device_info.mqtt_host = qm_json_get_value_by_name(reuslt, result_len, SHADOW_HOST_KEY, (int *)&dynreg_recv.data.device_info.mqtt_host_len, &type);
    if(dynreg_recv.data.device_info.mqtt_host == NULL ||
        !dynreg_recv.data.device_info.mqtt_host_len){
        return -QM_EINVAL;
    }

    dynreg_recv.data.device_info.server_cert = qm_json_get_value_by_name(reuslt, result_len, SHADOW_CA_KEY,
                                                                        (int *)&dynreg_recv.data.device_info.server_cert_len, &type);
    if(dynreg_recv.data.device_info.server_cert == NULL ||
        !dynreg_recv.data.device_info.server_cert_len){
        return -QM_EINVAL;
    }

    dynreg_recv.data.device_info.client_crt = qm_json_get_value_by_name(reuslt, result_len, SHADOW_CLIENT_CA_KEY,
                                                                        (int *)&dynreg_recv.data.device_info.client_cert_len, &type);
    if(dynreg_recv.data.device_info.client_crt == NULL ||
        !dynreg_recv.data.device_info.client_cert_len){
        return -QM_EINVAL;
    }

    dynreg_recv.data.device_info.client_private_key = qm_json_get_value_by_name(reuslt, result_len, SHADOW_CLIENT_KEY,
                                                                                (int *)&dynreg_recv.data.device_info.client_privkey_len, &type);
    if(dynreg_recv.data.device_info.client_private_key == NULL ||
        !dynreg_recv.data.device_info.client_privkey_len){
        return -QM_EINVAL;
    }

    dynreg_recv.data.device_info.public_key = qm_json_get_value_by_name(reuslt, result_len, SHADOW_PUBLIC_KEY,
                                                                                (int *)&dynreg_recv.data.device_info.public_key_len, &type);
    if(dynreg_recv.data.device_info.public_key == NULL ||
        !dynreg_recv.data.device_info.public_key_len){
        return -QM_EINVAL;
    }
    //

#endif

__exit:
    if(dynreg_handle->recv_handler){
        dynreg_handle->recv_handler(dynreg_handle, &dynreg_recv, dynreg_handle->userdata);
    }
    return QM_EOK;
}

static int32_t dynreg_signature_payload_append(cJSON *root,
                                               const qm_iot_dynreg_req_t *req_handle)
{
    int nonce;
    char hmac_source[128] = {0};
    char signature[64 + 1] = {0};
    char pid_str[16] = {0};
    char timestamp_str[16] = {0};
    uint32_t now_ms = qm_now_ms();

    if (root == NULL ||
        req_handle == NULL ||
        req_handle->product_secret == NULL ||
        req_handle->product_secret[0] == '\0') {
        return -QM_EINVAL;
    }

    qm_srandom(qm_now_ms());
    nonce = (int)qm_random_get(QM_IOT_DYNREG_NONCE_MAX);

    qm_snprintf(hmac_source, sizeof(hmac_source) - 1,
                "pid=%d,nonce=%d,sn=%s,timestamp=%d",
                (int)req_handle->product_id,
                nonce,
                req_handle->sn,
                (int)now_ms);

    qm_utils_hmac_sha1(hmac_source,
                       strlen(hmac_source),
                       signature,
                       req_handle->product_secret,
                       strlen(req_handle->product_secret));

    qm_snprintf(pid_str, sizeof(pid_str), "%u", (unsigned int)req_handle->product_id);
    qm_snprintf(timestamp_str, sizeof(timestamp_str), "%u", (unsigned int)now_ms);

    /* Signature register keeps pid/timestamp as strings to match the existing cloud protocol. */
    cJSON_AddStringToObject(root, QM_IOT_DYNREG_SN_KEY, req_handle->sn);
    cJSON_AddStringToObject(root, QM_IOT_DYNREG_PID_KEY, pid_str);
    cJSON_AddStringToObject(root, QM_IOT_DYNREG_TIMESTAMP_KEY, timestamp_str);
    cJSON_AddStringToObject(root, QM_IOT_DYNREG_SIGNATURE_KEY, signature);
    cJSON_AddNumberToObject(root, QM_IOT_DYNREG_NONCE_KEY, (double)nonce);
    QM_CLOUD_LOGE(LOG_TAG,
                  "dynreg request payload={\"sn\":\"%s\",\"pid\":\"%s\",\"timestamp\":\"%s\",\"signature\":\"<redacted>\",\"nonce\":%d} signature_len=%lu",
                  req_handle->sn,
                  pid_str,
                  timestamp_str,
                  nonce,
                  (unsigned long)strlen(signature));
    return QM_EOK;
}


void *qm_iot_dynreg_init(void)
{
    qm_iot_dynreg_handle_t *dynreg_handle = (qm_iot_dynreg_handle_t *)qm_malloc(sizeof(qm_iot_dynreg_handle_t));
    if(dynreg_handle == NULL){
        return NULL;
    }
    memset(dynreg_handle, 0, sizeof(qm_iot_dynreg_handle_t));
    dynreg_handle->req_data.port = CONFIG_QM_IOT_DYNREG_PORT;
    dynreg_handle->recv_timeout = CONFIG_QM_IOT_DYNREG_RECV_TIMEOUT_MS;
    dynreg_handle->retry_num = CONFIG_QM_IOT_DYNREG_RETRY_COUNT;

    return (void *)dynreg_handle;
}

int32_t qm_iot_dynreg_env_set(qm_iot_dynreg_env_t env)
{
    if (env > QM_IOT_DYNREG_ENV_PROD) {
        return -QM_EINVAL;
    }

    s_dynreg_env = env;
    return QM_EOK;
}

qm_iot_dynreg_env_t qm_iot_dynreg_env_get(void)
{
    return s_dynreg_env;
}

int32_t qm_iot_dynreg_setopt(void *handle, qm_iot_dynreg_option_t option, void *data)
{
    qm_iot_dynreg_req_t *req_handle = NULL;
    qm_iot_dynreg_handle_t *dynreg_handle = (qm_iot_dynreg_handle_t *)handle;
    if(dynreg_handle == NULL){
        return -QM_EINVAL;
    }

    if(data == NULL && option != QM_IOT_DYNREGOPT_USERDATA){
        return -QM_EINVAL;
    }

    req_handle = &dynreg_handle->req_data;

    switch (option)
    {

        case QM_IOT_DYNREGOPT_HOST:
            req_handle->host = (char *)data;
        break;

        case QM_IOT_DYNREGOPT_PRODUCT_ID:
            memcpy(&req_handle->product_id, (uint32_t *)data, sizeof(uint32_t));
        break;

        case QM_IOT_DYNREGOPT_PRODUCT_SECRET:
            req_handle->product_secret = (char *)data;
        break;

        case QM_IOT_DYNREGOPT_SN:
            req_handle->sn = (char *)data;
        break;

        case QM_IOT_DYNREGOPT_PORT:
            memcpy(&req_handle->port, (uint16_t *)data, sizeof(uint16_t));
        break;

        case QM_IOT_DYNREGOPT_RECV_HANDLER:
            dynreg_handle->recv_handler = (qm_iot_dynreg_recv_handler_t)data;
        break;

        case QM_IOT_DYNREGOPT_USERDATA:
            dynreg_handle->userdata = (void *)data;
        break;

        case QM_IOT_DYNREGOPT_RECV_TIMEOUT_MS:
            memcpy(&dynreg_handle->recv_timeout, (uint32_t *)data, sizeof(uint32_t));
        break;

        case QM_IOT_DYNREGOPT_MAX_RETRY_NUM:
            memcpy(&dynreg_handle->retry_num, (uint32_t *)data, sizeof(uint32_t));
        break;

        default:

        break;
    }

    return QM_EOK;
}

/**
 * @brief 生成动态注册 HTTP POST JSON 请求体（内部使用）。
 *
 * @param[in] handle dynreg 会话句柄。
 * @param[in] type   当前网络类型。
 * @return 成功返回需由调用方 cJSON_free 的请求体；失败返回 NULL。
 */
static char *qm_iot_dynreg_payload_create(void *handle, qm_iot_network_type_t type)
{
    cJSON *root = NULL;
    char *payload = NULL;
    int32_t ret = -QM_EINVAL;
    qm_iot_dynreg_handle_t *dynreg_handle = (qm_iot_dynreg_handle_t *)handle;
    qm_iot_dynreg_req_t *req_handle = NULL;

    if (dynreg_handle == NULL || type <= QM_IOT_NETWORK_TYPE_NONE) {
        return NULL;
    }

    req_handle = &dynreg_handle->req_data;
    if (req_handle->sn == NULL ||
        req_handle->sn[0] == '\0' ||
        req_handle->product_id == 0) {
        return NULL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    ret = dynreg_signature_payload_append(root, req_handle);

    if (ret != QM_EOK) {
        cJSON_Delete(root);
        return NULL;
    }

    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (payload != NULL) {
        QM_CLOUD_LOGD(LOG_TAG, QM_CLOUD_LOG_AUTH, "dynreg request payload_len=%lu",
                      (unsigned long)strlen(payload));
    }
    return payload;
}

int32_t qm_iot_dynreg_start(void *handle, qm_iot_network_type_t type)
{
    int ret = QM_EOK;
    uint16_t next_backoff = 0;
    backoff_algorithm_ctx_t backoff_ctx = {0};
    backoff_algorithm_status_t status = BACKOFF_ALGORITHM_SUCCESS;

    char *payload = NULL;
    char *rsp_payload = NULL;
    char request_url[QM_IOT_DYNREG_URL_MAX_LEN] = {0};
    const char *host = NULL;

    qm_http_client_request_t http_req;
    qm_http_client_response_t http_resp;
    qm_iot_dynreg_http_rsp_t http_rsp;
    qm_iot_dynreg_req_t *req_handle = NULL;
    qm_iot_dynreg_handle_t *dynreg_handle = (qm_iot_dynreg_handle_t *)handle;
    if(dynreg_handle == NULL || type <= QM_IOT_NETWORK_TYPE_NONE){
        return -QM_EINVAL;
    }

    req_handle = &dynreg_handle->req_data;
    host = req_handle->host;
    if (host == NULL || host[0] == '\0') {
        host = dynreg_default_host_get();
    }

    ret = dynreg_build_url(host,
                           req_handle->port,
                           request_url,
                           sizeof(request_url));
    if (ret != QM_EOK) {
        return ret;
    }
    QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_AUTH, "dynreg url_len=%lu",
                  (unsigned long)strlen(request_url));

    payload = qm_iot_dynreg_payload_create(handle, type);
    if (payload == NULL || payload[0] == '\0') {
        cJSON_free(payload);
        return -QM_EINVAL;
    }

    rsp_payload = (char *)qm_malloc(RSP_PAYLOAD_MAX_STR_LEN);
    if(rsp_payload == NULL){
        cJSON_free(payload);
        return -QM_ENOMEM;
    }
    memset(rsp_payload, 0, RSP_PAYLOAD_MAX_STR_LEN);


    //链接connect
    backoff_algorithm_init(&backoff_ctx, BACKOFF_BASE_TIME_MS, dynreg_handle->recv_timeout, dynreg_handle->retry_num);
    while (1)
    {

        memset(&http_req, 0, sizeof(http_req));
        memset(&http_resp, 0, sizeof(http_resp));
        memset(&http_rsp, 0, sizeof(http_rsp));
        memset(rsp_payload, 0, RSP_PAYLOAD_MAX_STR_LEN);

        http_rsp.buf = rsp_payload;
        http_rsp.buf_len = RSP_PAYLOAD_MAX_STR_LEN;
        http_req.method = QM_HTTP_POST;
        http_req.url = request_url;
        http_req.body = payload;
        http_req.body_len = strlen(payload);
        http_req.content_type = "application/json";
        http_req.timeout_ms = dynreg_handle->recv_timeout;
        http_req.max_body_len = RSP_PAYLOAD_MAX_STR_LEN - 1U;
        http_req.user = &http_rsp;
        http_req.on_body = dynreg_http_body_cb;

        ret = qm_http_client_execute(&http_req, &http_resp);
        QM_CLOUD_LOGI(LOG_TAG, QM_CLOUD_LOG_AUTH, "dynreg http ret=%d status=%d body=%u",
                ret,
                http_resp.status_code,
                (unsigned int)http_rsp.used);
        if (ret == QM_EOK && http_resp.status_code != 200 && http_rsp.used != 0U) {
            QM_CLOUD_LOGW(LOG_TAG, QM_CLOUD_LOG_AUTH, "dynreg http error body_len=%u",
                          (unsigned int)http_rsp.used);
        }
        if(ret == QM_EOK && http_rsp.used != 0U && http_resp.status_code == 200){
            break;
        }
        if (ret == QM_EOK &&
            http_resp.status_code >= 400 &&
            http_resp.status_code < 500) {
            dynreg_status_notify(dynreg_handle, (uint32_t)http_resp.status_code);
            ret = -QM_EIO;
            break;
        }

        status = backoff_algorithm_get_next_delay(&backoff_ctx, &next_backoff);
        if(status != BACKOFF_ALGORITHM_SUCCESS){
            ret = -QM_ETIMEOUT;
            break;
        }

        qm_msleep(next_backoff);
    }

    if(ret != QM_EOK){
        goto __exit;
    }

    qm_iot_dynreg_recv_handler(dynreg_handle, rsp_payload, (int)http_rsp.used);

__exit:

    if(payload){
        cJSON_free(payload);
        payload = NULL;
    }

    if(rsp_payload){
        qm_free(rsp_payload);
        rsp_payload = NULL;
    }

    return ret;
}

int32_t qm_iot_dynreg_deinit(void **handle)
{
    qm_iot_dynreg_handle_t **dynreg_handle = (qm_iot_dynreg_handle_t **)handle;
    if(handle == NULL || *dynreg_handle == NULL){
        return -QM_EINVAL;
    }

    qm_free(*dynreg_handle);

    *dynreg_handle = NULL;
    return QM_EOK;
}
