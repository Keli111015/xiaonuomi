#include "xjiang_http_ota.h"

#include <limits.h>

#include "cJSON.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_utils_base64.h"

#ifndef CONFIG_XJIANG_HTTP_OTA_TIMEOUT_MS
#define CONFIG_XJIANG_HTTP_OTA_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_XJIANG_HTTP_OTA_HEADER_MAX
#define CONFIG_XJIANG_HTTP_OTA_HEADER_MAX 2048
#endif

#ifndef CONFIG_XJIANG_HTTP_OTA_RESPONSE_BODY_MAX
#define CONFIG_XJIANG_HTTP_OTA_RESPONSE_BODY_MAX 2048
#endif

#ifndef CONFIG_XJIANG_HTTP_OTA_RX_BUFFER_SIZE
#define CONFIG_XJIANG_HTTP_OTA_RX_BUFFER_SIZE 512
#endif

#define LOG_TAG "xjiang_http_ota"
#define XJIANG_HTTP_OTA_USER_AGENT "xjiang-http-ota/1.0"
#define XJIANG_HTTP_OTA_ENDPOINT_DEV \
    "https://api-dev.ljyd.cn/agent-device/iot/ota/version"
#define XJIANG_HTTP_OTA_ENDPOINT_TEST \
    "https://api-test.ljyd.cn/agent-device/iot/ota/version"
#define XJIANG_HTTP_OTA_ENDPOINT_PROD \
    "https://api.ljyd.cn/agent-device/iot/ota/version"
#define XJIANG_HTTP_OTA_HMAC_SHA256_DIGEST_LEN 32
#define XJIANG_HTTP_OTA_HMAC_SHA256_HEX_LEN    64

#if defined(__GNUC__)
#define XJIANG_HTTP_OTA_MAYBE_UNUSED __attribute__((unused))
#else
#define XJIANG_HTTP_OTA_MAYBE_UNUSED
#endif

typedef struct {
    char *body;         /*!< Accumulated response body buffer */
    uint32_t max_len;   /*!< Maximum body length accepted */
    uint32_t len;       /*!< Current body length */
} xjiang_http_ota_body_ctx_t;

typedef struct {
    const char *endpoint;                       /*!< Version API endpoint override */
    const char *pid;                            /*!< Product PID */
    const char *current_module_version;         /*!< Current module version */
    const char *current_mcu_version;            /*!< Current MCU version */
    const char *user_agent;                     /*!< Version API HTTP user agent */
    const qm_http_header_t *headers;            /*!< Version API extra headers */
    int header_count;                           /*!< Version API extra header count */
    uint32_t timeout_ms;                        /*!< Version API request timeout */
    uint32_t max_header_len;                    /*!< Version API max response header length */
    uint32_t max_body_len;                      /*!< Version API max response body length */
    uint32_t rx_buf_size;                       /*!< Version API receive buffer size */
    const qm_http_tls_config_t *tls;            /*!< Version API TLS configuration */
    const char *authorization_token;            /*!< Version API Authorization header value */
    qm_http_ota_event_cb_t on_event;            /*!< Firmware OTA event callback */
    void *user;                                 /*!< User context for OTA event callback */
    qm_bool auto_restart;                       /*!< Restart after module OTA finish */
    qm_bool resume_enable;                      /*!< Enable firmware OTA resume */
    uint32_t resume_offset;                     /*!< Explicit firmware OTA resume offset */
    const char *resume_if_range;                /*!< Optional firmware OTA If-Range value */
    uint8_t progress_step;                      /*!< Firmware OTA progress event step */
    const qm_http_ota_mcu_config_t *mcu_ota;    /*!< MCU OTA business callbacks */
    const char *firmware_user_agent;            /*!< Firmware download HTTP user agent */
    const qm_http_header_t *firmware_headers;   /*!< Firmware download extra headers */
    int firmware_header_count;                  /*!< Firmware download extra header count */
    uint32_t firmware_timeout_ms;               /*!< Firmware download request timeout */
    uint32_t firmware_header_max;               /*!< Firmware download max response header length */
    uint32_t firmware_rx_buf_size;              /*!< Firmware download receive buffer size */
    const qm_http_tls_config_t *firmware_tls;   /*!< Firmware download TLS configuration */
    const char *product_secret;                 /*!< Product secret used for HMAC-SHA256 verification */
} xjiang_http_ota_config_t;

typedef struct {
    const xjiang_http_ota_config_t *config;     /*!< Active operation configuration */
} xjiang_http_ota_runtime_t;

static xjiang_http_ota_env_t s_xjiang_http_ota_env = XJIANG_HTTP_OTA_ENV_PROD;
static const qm_http_tls_config_t s_xjiang_http_ota_tls_no_ca = {
    .ca_crt = NULL,
    .ca_crt_len = 0,
    .client_crt = NULL,
    .client_crt_len = 0,
    .client_key = NULL,
    .client_key_len = 0,
};
static qm_http_ota_mcu_ops_t s_xjiang_http_ota_mcu_ops;
static qm_http_ota_mcu_config_t s_xjiang_http_ota_mcu_ota;
static qm_bool s_xjiang_http_ota_mcu_ota_valid;
static char *s_xjiang_http_ota_product_secret;

static XJIANG_HTTP_OTA_MAYBE_UNUSED const char *xjiang_http_ota_safe_str(const char *text)
{
    return text ? text : "";
}

static void xjiang_http_ota_mask_url(const char *url,
                                     char *out,
                                     uint32_t out_len)
{
    uint32_t url_len;
    uint32_t prefix_len = 16;
    uint32_t suffix_len = 8;

    if (!out || out_len == 0) {
        return;
    }

    out[0] = '\0';
    if (!url || url[0] == '\0') {
        return;
    }

    url_len = qm_strlen(url);
    if (out_len < prefix_len + suffix_len + 4) {
        return;
    }
    if (url_len <= prefix_len + suffix_len) {
        prefix_len = url_len > 4 ? 4 : url_len;
        suffix_len = 0;
    }

    qm_memcpy(out, url, prefix_len);
    qm_memcpy(out + prefix_len, "...", 3);
    if (suffix_len > 0) {
        qm_memcpy(out + prefix_len + 3, url + url_len - suffix_len, suffix_len);
    }
    out[prefix_len + 3 + suffix_len] = '\0';
}

static int xjiang_http_ota_strdup_len(const char *src, uint32_t len, char **dst)
{
    char *copy;

    if (!dst) {
        return -QM_EINVAL;
    }

    *dst = NULL;
    if (!src || len == 0) {
        return QM_EOK;
    }

    copy = (char *)qm_malloc(len + 1);
    if (!copy) {
        return -QM_ENOMEM;
    }

    qm_memcpy(copy, src, len);
    copy[len] = '\0';
    *dst = copy;
    return QM_EOK;
}

static int xjiang_http_ota_sign_base64_to_hmac_hex(const char *sign,
                                                   char *hex,
                                                   uint32_t hex_len)
{
    uint8_t digest[XJIANG_HTTP_OTA_HMAC_SHA256_DIGEST_LEN];
    uint32_t digest_len = 0;
    int ret;

    if (!sign || sign[0] == '\0' || !hex ||
        hex_len < XJIANG_HTTP_OTA_HMAC_SHA256_HEX_LEN + 1u) {
        return -QM_EINVAL;
    }

    ret = qm_utils_base64decode((const uint8_t *)sign,
                                (uint32_t)qm_strlen(sign),
                                sizeof(digest),
                                digest,
                                &digest_len);
    if (ret != QM_EOK ||
        digest_len != XJIANG_HTTP_OTA_HMAC_SHA256_DIGEST_LEN) {
        QM_LOGE(LOG_TAG, "invalid firmware sign ret=%d digest_len=%u",
                ret,
                (unsigned int)digest_len);
        return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
    }

    ret = qm_hex2str(digest, digest_len, hex, 1);
    if (ret != QM_EOK) {
        return ret;
    }
    hex[digest_len * 2u] = '\0';
    return QM_EOK;
}

static void xjiang_http_ota_firmware_info_deinit(xjiang_http_ota_firmware_info_t *info)
{
    if (!info) {
        return;
    }

    if (info->version) {
        qm_free(info->version);
    }
    if (info->url) {
        qm_free(info->url);
    }
    if (info->sign) {
        qm_free(info->sign);
    }
    if (info->file_name) {
        qm_free(info->file_name);
    }
    qm_memset(info, 0, sizeof(*info));
}

void xjiang_http_ota_info_free(xjiang_http_ota_info_t *info)
{
    if (!info) {
        return;
    }

    xjiang_http_ota_firmware_info_deinit(&info->module);
    xjiang_http_ota_firmware_info_deinit(&info->mcu);
    info->status_code = 0;
}

static XJIANG_HTTP_OTA_MAYBE_UNUSED const char *xjiang_http_ota_env_name(xjiang_http_ota_env_t env)
{
    switch (env) {
        case XJIANG_HTTP_OTA_ENV_DEV:
            return "dev";
        case XJIANG_HTTP_OTA_ENV_TEST:
            return "test";
        case XJIANG_HTTP_OTA_ENV_PROD:
        default:
            return "prod";
    }
}

static XJIANG_HTTP_OTA_MAYBE_UNUSED const char *xjiang_http_ota_target_name(xjiang_http_ota_target_t target)
{
    return target == XJIANG_HTTP_OTA_TARGET_MCU ? "mcu" : "module";
}

static const char *xjiang_http_ota_endpoint(xjiang_http_ota_env_t env)
{
    switch (env) {
        case XJIANG_HTTP_OTA_ENV_DEV:
            return XJIANG_HTTP_OTA_ENDPOINT_DEV;
        case XJIANG_HTTP_OTA_ENV_TEST:
            return XJIANG_HTTP_OTA_ENDPOINT_TEST;
        case XJIANG_HTTP_OTA_ENV_PROD:
        default:
            return XJIANG_HTTP_OTA_ENDPOINT_PROD;
    }
}

int xjiang_http_ota_set_env(xjiang_http_ota_env_t env)
{
    if (env < XJIANG_HTTP_OTA_ENV_PROD || env > XJIANG_HTTP_OTA_ENV_TEST) {
        return -QM_EINVAL;
    }

    s_xjiang_http_ota_env = env;
    QM_LOGI(LOG_TAG, "set env=%s", xjiang_http_ota_env_name(env));
    return QM_EOK;
}

int xjiang_http_ota_set_product_secret(const char *product_secret)
{
    char *secret_copy = NULL;
    int ret;

    if (product_secret && product_secret[0] != '\0') {
        ret = xjiang_http_ota_strdup_len(product_secret,
                                         (uint32_t)qm_strlen(product_secret),
                                         &secret_copy);
        if (ret != QM_EOK) {
            return ret;
        }
    }

    if (s_xjiang_http_ota_product_secret) {
        qm_free(s_xjiang_http_ota_product_secret);
    }
    s_xjiang_http_ota_product_secret = secret_copy;
    QM_LOGI(LOG_TAG, "set product secret configured=%d",
            s_xjiang_http_ota_product_secret ? 1 : 0);
    return QM_EOK;
}

int xjiang_http_ota_set_mcu_ota(const qm_http_ota_mcu_config_t *mcu_ota)
{
    const qm_http_ota_mcu_ops_t *ops;

    if (!mcu_ota) {
        qm_memset(&s_xjiang_http_ota_mcu_ops, 0, sizeof(s_xjiang_http_ota_mcu_ops));
        qm_memset(&s_xjiang_http_ota_mcu_ota, 0, sizeof(s_xjiang_http_ota_mcu_ota));
        s_xjiang_http_ota_mcu_ota_valid = QM_FALSE;
        QM_LOGI(LOG_TAG, "clear mcu ota callbacks");
        return QM_EOK;
    }

    ops = mcu_ota->ops;
    if (!ops || !ops->begin || !ops->write || !ops->finish || !ops->abort) {
        QM_LOGE(LOG_TAG, "invalid mcu ota callbacks");
        return -QM_EINVAL;
    }

    s_xjiang_http_ota_mcu_ops = *ops;
    s_xjiang_http_ota_mcu_ota.ops = &s_xjiang_http_ota_mcu_ops;
    s_xjiang_http_ota_mcu_ota.user = mcu_ota->user;
    s_xjiang_http_ota_mcu_ota_valid = QM_TRUE;
    QM_LOGI(LOG_TAG, "register mcu ota callbacks user=%p", mcu_ota->user);
    return QM_EOK;
}

static void xjiang_http_ota_fill_config(xjiang_http_ota_config_t *config,
                                        const char *pid,
                                        const char *module_version,
                                        const char *mcu_version,
                                        const char *authorization_token)
{
    if (!config) {
        return;
    }

    qm_memset(config, 0, sizeof(*config));
    config->pid = pid;
    config->current_module_version = module_version;
    config->current_mcu_version = mcu_version;
    config->user_agent = XJIANG_HTTP_OTA_USER_AGENT;
    config->firmware_user_agent = XJIANG_HTTP_OTA_USER_AGENT;
    config->tls = &s_xjiang_http_ota_tls_no_ca;
    config->firmware_tls = &s_xjiang_http_ota_tls_no_ca;
    config->authorization_token = authorization_token;
    config->product_secret = s_xjiang_http_ota_product_secret;
}

static void xjiang_http_ota_fill_perform_config(xjiang_http_ota_config_t *config)
{
    if (!config) {
        return;
    }

    qm_memset(config, 0, sizeof(*config));
    config->mcu_ota = s_xjiang_http_ota_mcu_ota_valid ?
                      &s_xjiang_http_ota_mcu_ota :
                      NULL;
    config->firmware_user_agent = XJIANG_HTTP_OTA_USER_AGENT;
    config->tls = &s_xjiang_http_ota_tls_no_ca;
    config->firmware_tls = &s_xjiang_http_ota_tls_no_ca;
    config->product_secret = s_xjiang_http_ota_product_secret;
}

static int xjiang_http_ota_build_check_url(const xjiang_http_ota_config_t *config,
                                           char **url_out)
{
    const char *endpoint;
    const char *separator;
    char *url;
    int url_len;
    int ret;

    if (!config || !url_out || !config->pid || config->pid[0] == '\0' ||
        !config->current_module_version || !config->current_mcu_version ||
        config->header_count < 0) {
        return -QM_EINVAL;
    }
    *url_out = NULL;

    endpoint = config->endpoint ? config->endpoint :
               xjiang_http_ota_endpoint(s_xjiang_http_ota_env);
    if (!endpoint || endpoint[0] == '\0') {
        return -QM_EINVAL;
    }

    separator = qm_strchr(endpoint, '?') ? "&" : "?";
    url_len = (int)qm_strlen(endpoint) +
              (int)qm_strlen(config->pid) +
              (int)qm_strlen(config->current_module_version) +
              (int)qm_strlen(config->current_mcu_version) +
              64;

    url = (char *)qm_malloc((uint32_t)url_len);
    if (!url) {
        return -QM_ENOMEM;
    }

    ret = qm_snprintf(url, (int)url_len,
                      "%s%spid=%s&moduleVersion=%s&mcuVersion=%s",
                      endpoint,
                      separator,
                      config->pid,
                      config->current_module_version,
                      config->current_mcu_version);
    if (ret < 0 || ret >= url_len) {
        qm_free(url);
        return -QM_EFULL;
    }

    *url_out = url;
    return QM_EOK;
}

static qm_bool xjiang_http_ota_headers_have_authorization(const qm_http_header_t *headers,
                                                          int header_count)
{
    int i;

    if (!headers || header_count <= 0) {
        return QM_FALSE;
    }

    for (i = 0; i < header_count; i++) {
        if (headers[i].name &&
            qm_strcasecmp(headers[i].name, "Authorization") == 0) {
            return QM_TRUE;
        }
    }
    return QM_FALSE;
}

static int xjiang_http_ota_build_check_headers(const xjiang_http_ota_config_t *config,
                                               qm_http_header_t *auth_header,
                                               qm_http_header_t **headers_out,
                                               int *header_count_out)
{
    qm_http_header_t *headers;
    int header_count;

    if (!config || !auth_header || !headers_out || !header_count_out ||
        config->header_count < 0) {
        return -QM_EINVAL;
    }

    *headers_out = NULL;
    *header_count_out = config->header_count;
    if (!config->authorization_token || config->authorization_token[0] == '\0' ||
        xjiang_http_ota_headers_have_authorization(config->headers, config->header_count)) {
        *headers_out = (qm_http_header_t *)config->headers;
        return QM_EOK;
    }

    auth_header->name = "Authorization";
    auth_header->value = config->authorization_token;
    if (!config->headers || config->header_count == 0) {
        *headers_out = auth_header;
        *header_count_out = 1;
        return QM_EOK;
    }

    header_count = config->header_count + 1;
    headers = (qm_http_header_t *)qm_malloc((uint32_t)header_count * sizeof(*headers));
    if (!headers) {
        return -QM_ENOMEM;
    }
    qm_memcpy(headers, config->headers, (uint32_t)config->header_count * sizeof(*headers));
    headers[config->header_count] = *auth_header;

    *headers_out = headers;
    *header_count_out = header_count;
    return QM_EOK;
}

static int xjiang_http_ota_body_cb(const char *data, int len, void *user)
{
    xjiang_http_ota_body_ctx_t *ctx = (xjiang_http_ota_body_ctx_t *)user;

    if (!ctx || (!data && len > 0) || len < 0) {
        return -QM_EINVAL;
    }
    if (len == 0) {
        return QM_EOK;
    }
    if (ctx->len > ctx->max_len || (uint32_t)len > ctx->max_len - ctx->len) {
        return -QM_EFULL;
    }
    qm_memcpy(ctx->body + ctx->len, data, (uint32_t)len);
    ctx->len += (uint32_t)len;
    ctx->body[ctx->len] = '\0';
    return QM_EOK;
}

static int xjiang_http_ota_json_get_string(const cJSON *json,
                                           const char *name,
                                           char **out)
{
    const cJSON *value;

    if (!json || !name || !out) {
        return -QM_EINVAL;
    }

    *out = NULL;
    value = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!value) {
        return QM_EOK;
    }
    if (cJSON_IsNull(value)) {
        return QM_EOK;
    }
    if (cJSON_IsString(value)) {
        if (!value->valuestring) {
            return QM_EOK;
        }
        *out = qm_strdup(value->valuestring);
        return *out ? QM_EOK : -QM_ENOMEM;
    }
    if (cJSON_IsNumber(value)) {
        char number[16];
        int ret;

        ret = qm_snprintf(number, sizeof(number), "%d", value->valueint);
        if (ret < 0 || ret >= (int)sizeof(number)) {
            return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
        }
        return xjiang_http_ota_strdup_len(number, (uint32_t)ret, out);
    }
    return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
}

static int xjiang_http_ota_json_get_string_fallback(const cJSON *json,
                                                    const char *primary,
                                                    const char *fallback,
                                                    char **out)
{
    int ret;

    ret = xjiang_http_ota_json_get_string(json, primary, out);
    if (ret != QM_EOK || (out && *out) || !fallback) {
        return ret;
    }
    return xjiang_http_ota_json_get_string(json, fallback, out);
}

static int xjiang_http_ota_parse_uint32(const char *value, uint32_t value_len, uint32_t *out)
{
    uint64_t parsed = 0;
    int ret;

    if (!out) {
        return -QM_EINVAL;
    }
    *out = 0;
    if (!value || value_len == 0) {
        return QM_EOK;
    }
    if (value_len > 20u) {
        return -QM_EFULL;
    }

    ret = qm_str2uint64((char *)value, (uint8_t)value_len, &parsed);
    if (ret != QM_EOK) {
        return ret == -QM_EINVAL ? -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE : ret;
    }
    if (parsed > UINT32_MAX) {
        return -QM_EFULL;
    }

    *out = (uint32_t)parsed;
    return QM_EOK;
}

static int xjiang_http_ota_json_get_uint32(const cJSON *json,
                                           const char *name,
                                           uint32_t *out)
{
    const cJSON *value;
    uint32_t len;

    if (!json || !name || !out) {
        return -QM_EINVAL;
    }

    *out = 0;
    value = cJSON_GetObjectItemCaseSensitive(json, name);
    if (!value) {
        return QM_EOK;
    }
    if (cJSON_IsNull(value)) {
        return QM_EOK;
    }
    if (cJSON_IsNumber(value)) {
        if (value->valuedouble < 0 || value->valuedouble > (double)UINT32_MAX ||
            value->valuedouble != (double)((uint32_t)value->valuedouble)) {
            return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
        }
        *out = (uint32_t)value->valuedouble;
        return QM_EOK;
    }
    if (cJSON_IsString(value)) {
        len = value->valuestring ? (uint32_t)qm_strlen(value->valuestring) : 0;
        return xjiang_http_ota_parse_uint32(value->valuestring, len, out);
    }
    return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
}

static int xjiang_http_ota_json_get_uint32_fallback(const cJSON *json,
                                                    const char *primary,
                                                    const char *fallback,
                                                    uint32_t *out)
{
    int ret;

    ret = xjiang_http_ota_json_get_uint32(json, primary, out);
    if (ret != QM_EOK || !fallback || (out && *out != 0)) {
        return ret;
    }
    return xjiang_http_ota_json_get_uint32(json, fallback, out);
}

static int xjiang_http_ota_parse_semver3(const char *version, uint32_t parts[3])
{
    const char *segment_start;
    uint32_t segment_len;
    int index = 0;
    int ret;

    if (!version || !parts) {
        return -QM_EINVAL;
    }

    parts[0] = 0;
    parts[1] = 0;
    parts[2] = 0;

    segment_start = version;
    while (*version) {
        if (index >= 3 || *version == '.') {
            return -QM_EINVAL;
        }
        while (*version >= '0' && *version <= '9') {
            version++;
        }
        segment_len = (uint32_t)(version - segment_start);
        if (segment_len == 0) {
            return -QM_EINVAL;
        }

        ret = xjiang_http_ota_parse_uint32(segment_start, segment_len, &parts[index++]);
        if (ret != QM_EOK) {
            return ret;
        }

        if (index < 3) {
            if (*version != '.') {
                return -QM_EINVAL;
            }
            version++;
            segment_start = version;
        } else if (*version != '\0') {
            return -QM_EINVAL;
        }
    }

    return index == 3 ? QM_EOK : -QM_EINVAL;
}

static int xjiang_http_ota_compare_version3_any_field(const char *new_version,
                                                      const char *current_version,
                                                      int *compare)
{
    uint32_t new_parts[3];
    uint32_t current_parts[3];
    qm_bool has_lower_field = QM_FALSE;
    int ret;
    int i;

    if (!compare) {
        return -QM_EINVAL;
    }
    *compare = 0;

    ret = xjiang_http_ota_parse_semver3(new_version, new_parts);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_parse_semver3(current_version, current_parts);
    if (ret != QM_EOK) {
        return ret;
    }

    for (i = 0; i < 3; i++) {
        if (new_parts[i] > current_parts[i]) {
            *compare = 1;
            return QM_EOK;
        }
        if (new_parts[i] < current_parts[i]) {
            has_lower_field = QM_TRUE;
        }
    }
    *compare = has_lower_field ? -1 : 0;
    return QM_EOK;
}

static int xjiang_http_ota_mark_available_by_version(xjiang_http_ota_firmware_info_t *info,
                                                     const char *current_version)
{
    int compare;
    int ret;

    if (!info || !current_version) {
        return -QM_EINVAL;
    }
    info->available = QM_FALSE;
    if (!info->version || info->version[0] == '\0') {
        return QM_EOK;
    }

    ret = xjiang_http_ota_compare_version3_any_field(info->version,
                                                     current_version,
                                                     &compare);
    if (ret != QM_EOK) {
        return ret;
    }
    info->available = compare > 0 ? QM_TRUE : QM_FALSE;
    return QM_EOK;
}

static void xjiang_http_ota_clear_incomplete_firmware(xjiang_http_ota_firmware_info_t *info)
{
    if (!info || !info->available) {
        return;
    }
    if (!info->url || info->url[0] == '\0' || info->file_size == 0) {
        info->available = QM_FALSE;
    }
}

static void xjiang_http_ota_log_firmware_info(const char *name,
                                              const xjiang_http_ota_firmware_info_t *info)
{
    char masked_url[32];

    if (!name || !info) {
        return;
    }

    xjiang_http_ota_mask_url(info->url, masked_url, sizeof(masked_url));
    QM_LOGI(LOG_TAG,
            "%s available=%d version=%s file=%s size=%u sign=%s url=%s",
            name,
            info->available,
            xjiang_http_ota_safe_str(info->version),
            xjiang_http_ota_safe_str(info->file_name),
            (unsigned int)info->file_size,
            xjiang_http_ota_safe_str(info->sign),
            masked_url);
}

static int xjiang_http_ota_parse_firmware_info(const cJSON *json,
                                               const xjiang_http_ota_config_t *config,
                                               xjiang_http_ota_info_t *info)
{
    int ret;

    if (!config || !info) {
        return -QM_EINVAL;
    }

    ret = xjiang_http_ota_json_get_string(json,
                                          "upgradableModuleVersion",
                                          &info->module.version);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_json_get_string(json,
                                          "upgradableModuleUrl",
                                          &info->module.url);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_json_get_string_fallback(json,
                                                   "upgradableMoudleSign",
                                                   "upgradableModuleSign",
                                                   &info->module.sign);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_json_get_uint32_fallback(json,
                                                   "upgradableMoudleFileSize",
                                                   "upgradableModuleFileSize",
                                                   &info->module.file_size);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_json_get_string_fallback(json,
                                                   "upgradableMoudleFileName",
                                                   "upgradableModuleFileName",
                                                   &info->module.file_name);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_mark_available_by_version(&info->module,
                                                    config->current_module_version);
    if (ret != QM_EOK) {
        return ret;
    }
    xjiang_http_ota_clear_incomplete_firmware(&info->module);

    ret = xjiang_http_ota_json_get_string(json,
                                          "upgradableMcuVersion",
                                          &info->mcu.version);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_json_get_string(json,
                                          "upgradableMcuUrl",
                                          &info->mcu.url);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_json_get_string(json,
                                          "upgradableMcuSign",
                                          &info->mcu.sign);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_json_get_uint32(json,
                                          "upgradableMcuFileSize",
                                          &info->mcu.file_size);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_json_get_string(json,
                                          "upgradableMcuFileName",
                                          &info->mcu.file_name);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = xjiang_http_ota_mark_available_by_version(&info->mcu,
                                                    config->current_mcu_version);
    if (ret != QM_EOK) {
        return ret;
    }
    xjiang_http_ota_clear_incomplete_firmware(&info->mcu);

    xjiang_http_ota_log_firmware_info("module", &info->module);
    xjiang_http_ota_log_firmware_info("mcu", &info->mcu);
    return QM_EOK;
}

static int xjiang_http_ota_parse_version_response(char *body,
                                                  uint32_t body_len,
                                                  const xjiang_http_ota_config_t *config,
                                                  xjiang_http_ota_info_t *info)
{
    cJSON *root;
    const cJSON *object;
    const cJSON *value;
    int ret;

    if (!body || body_len == 0 || !config || !info) {
        return -QM_EINVAL;
    }

    root = cJSON_ParseWithLength(body, body_len);
    if (!root) {
        return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
    }
    if (!cJSON_IsObject(root)) {
        cJSON_Delete(root);
        return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
    }

    object = root;
    value = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (value) {
        if (cJSON_IsString(value) && (!value->valuestring || value->valuestring[0] == '\0')) {
            cJSON_Delete(root);
            return QM_EOK;
        }
        if (cJSON_IsObject(value)) {
            object = value;
        } else {
            cJSON_Delete(root);
            return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
        }
    } else {
        value = cJSON_GetObjectItemCaseSensitive(root, "result");
        if (value && cJSON_IsObject(value)) {
            object = value;
        } else if (value) {
            cJSON_Delete(root);
            return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
        } else {
            value = cJSON_GetObjectItemCaseSensitive(root, "data");
            if (value && cJSON_IsObject(value)) {
                object = value;
            } else if (value) {
                cJSON_Delete(root);
                return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
            }
        }
    }

    ret = xjiang_http_ota_parse_firmware_info(object, config, info);
    cJSON_Delete(root);
    return ret;
}

static int xjiang_http_ota_check_with_config(const xjiang_http_ota_config_t *config,
                                             xjiang_http_ota_info_t *info)
{
    qm_http_client_request_t req;
    qm_http_client_response_t resp;
    xjiang_http_ota_body_ctx_t body_ctx;
    char *url = NULL;
    qm_http_header_t auth_header;
    qm_http_header_t *check_headers = NULL;
    uint32_t body_max;
    int check_header_count = 0;
    int ret;

    if (!config || !info) {
        return -QM_EINVAL;
    }

    qm_memset(info, 0, sizeof(*info));
    ret = xjiang_http_ota_build_check_url(config, &url);
    if (ret != QM_EOK) {
        return ret;
    }
    QM_LOGI(LOG_TAG, "check start env=%s pid=%s module=%s mcu=%s",
            xjiang_http_ota_env_name(s_xjiang_http_ota_env),
            config->pid,
            config->current_module_version,
            config->current_mcu_version);
    QM_LOGD(LOG_TAG, "check url=%s", url);

    qm_memset(&auth_header, 0, sizeof(auth_header));
    ret = xjiang_http_ota_build_check_headers(config,
                                              &auth_header,
                                              &check_headers,
                                              &check_header_count);
    if (ret != QM_EOK) {
        qm_free(url);
        return ret;
    }
    QM_LOGI(LOG_TAG, "check authorization configured=%d headers=%d",
            (config->authorization_token && config->authorization_token[0] != '\0') ? 1 : 0,
            check_header_count);

    body_max = config->max_body_len ? config->max_body_len :
               CONFIG_XJIANG_HTTP_OTA_RESPONSE_BODY_MAX;
    if (body_max == 0 || body_max > INT_MAX) {
        if (check_headers && check_headers != config->headers && check_headers != &auth_header) {
            qm_free(check_headers);
        }
        qm_free(url);
        return -QM_EINVAL;
    }

    body_ctx.body = (char *)qm_malloc(body_max + 1);
    if (!body_ctx.body) {
        if (check_headers && check_headers != config->headers && check_headers != &auth_header) {
            qm_free(check_headers);
        }
        qm_free(url);
        return -QM_ENOMEM;
    }
    body_ctx.max_len = body_max;
    body_ctx.len = 0;
    body_ctx.body[0] = '\0';

    qm_memset(&req, 0, sizeof(req));
    req.method = QM_HTTP_GET;
    req.url = url;
    req.user_agent = config->user_agent ? config->user_agent : XJIANG_HTTP_OTA_USER_AGENT;
    req.headers = check_headers;
    req.header_count = check_header_count;
    req.timeout_ms = config->timeout_ms ? config->timeout_ms : CONFIG_XJIANG_HTTP_OTA_TIMEOUT_MS;
    req.max_header_len = config->max_header_len ? config->max_header_len :
                         CONFIG_XJIANG_HTTP_OTA_HEADER_MAX;
    req.max_body_len = body_max;
    req.rx_buf_size = config->rx_buf_size ? config->rx_buf_size :
                      CONFIG_XJIANG_HTTP_OTA_RX_BUFFER_SIZE;
    req.tls = config->tls;
    req.user = &body_ctx;
    req.on_body = xjiang_http_ota_body_cb;

    qm_memset(&resp, 0, sizeof(resp));
    ret = qm_http_client_execute(&req, &resp);
    info->status_code = resp.status_code;
    QM_LOGI(LOG_TAG, "check http ret=%d status=%d body=%u",
            ret,
            resp.status_code,
            (unsigned int)body_ctx.len);
    if (body_ctx.len > 0) {
        QM_LOGI(LOG_TAG, "check response body=%s", body_ctx.body);
    }
    if (ret != QM_EOK && resp.status_code >= 200 && resp.status_code < 300 &&
        body_ctx.len > 0) {
        QM_LOGW(LOG_TAG,
                "check http returned ret=%d after receiving body, continue parse",
                ret);
        ret = QM_EOK;
    }
    if (ret == QM_EOK && (resp.status_code < 200 || resp.status_code >= 300)) {
        QM_LOGE(LOG_TAG, "check http bad status=%d", resp.status_code);
        ret = -QM_EIO;
    }
    if (ret == QM_EOK) {
        ret = xjiang_http_ota_parse_version_response(body_ctx.body,
                                                     body_ctx.len,
                                                     config,
                                                     info);
        if (ret != QM_EOK) {
            QM_LOGE(LOG_TAG, "parse version response failed ret=%d", ret);
            xjiang_http_ota_info_free(info);
        }
    }

    if (check_headers && check_headers != config->headers && check_headers != &auth_header) {
        qm_free(check_headers);
    }
    qm_free(body_ctx.body);
    qm_free(url);
    return ret;
}

static const xjiang_http_ota_firmware_info_t *
xjiang_http_ota_select_firmware(const xjiang_http_ota_info_t *info,
                                xjiang_http_ota_target_t target)
{
    if (!info) {
        return NULL;
    }
    if (target == XJIANG_HTTP_OTA_TARGET_MCU) {
        return &info->mcu;
    }
    return &info->module;
}

static int xjiang_http_ota_bridge_event(const qm_http_ota_event_t *event)
{
    xjiang_http_ota_runtime_t *runtime;
    qm_http_ota_event_t forwarded;

    if (!event || !event->user) {
        return -QM_EINVAL;
    }

    runtime = (xjiang_http_ota_runtime_t *)event->user;
    if (!runtime->config->on_event) {
        return QM_EOK;
    }

    forwarded = *event;
    forwarded.user = runtime->config->user;
    return runtime->config->on_event(&forwarded);
}

static int xjiang_http_ota_upgrade_target(const xjiang_http_ota_config_t *config,
                                          const xjiang_http_ota_firmware_info_t *firmware,
                                          xjiang_http_ota_target_t target,
                                          qm_bool auto_restart,
                                          qm_http_ota_result_t *ota_result)
{
    qm_http_client_request_t firmware_req;
    qm_http_ota_config_t ota_config;
    qm_http_ota_handle_t handle = NULL;
    qm_http_ota_result_t result_snapshot;
    xjiang_http_ota_runtime_t runtime;
    char expected_hmac_sha256_hex[XJIANG_HTTP_OTA_HMAC_SHA256_HEX_LEN + 1];
    int ret;

    if (!config || !firmware || !firmware->available) {
        return -QM_EINVAL;
    }
    if (!firmware->url || firmware->url[0] == '\0' || firmware->file_size == 0) {
        QM_LOGE(LOG_TAG, "invalid firmware target=%s url_empty=%d size=%u",
                xjiang_http_ota_target_name(target),
                (!firmware->url || firmware->url[0] == '\0'),
                (unsigned int)firmware->file_size);
        return -QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE;
    }

    QM_LOGI(LOG_TAG, "upgrade start target=%s version=%s file=%s size=%u",
            xjiang_http_ota_target_name(target),
            xjiang_http_ota_safe_str(firmware->version),
            xjiang_http_ota_safe_str(firmware->file_name),
            (unsigned int)firmware->file_size);

    qm_memset(expected_hmac_sha256_hex, 0, sizeof(expected_hmac_sha256_hex));
    if (config->product_secret && config->product_secret[0] != '\0') {
        ret = xjiang_http_ota_sign_base64_to_hmac_hex(firmware->sign,
                                                      expected_hmac_sha256_hex,
                                                      sizeof(expected_hmac_sha256_hex));
        if (ret != QM_EOK) {
            QM_LOGE(LOG_TAG, "firmware sign decode failed target=%s ret=%d",
                    xjiang_http_ota_target_name(target),
                    ret);
            return ret;
        }
        QM_LOGI(LOG_TAG, "hmac verify enabled target=%s sign=%s expected_hex=%s",
                xjiang_http_ota_target_name(target),
                xjiang_http_ota_safe_str(firmware->sign),
                expected_hmac_sha256_hex);
    }

    qm_memset(&result_snapshot, 0, sizeof(result_snapshot));
    qm_memset(&runtime, 0, sizeof(runtime));
    runtime.config = config;

    qm_memset(&firmware_req, 0, sizeof(firmware_req));
    firmware_req.method = QM_HTTP_GET;
    firmware_req.url = firmware->url;
    firmware_req.user_agent = config->firmware_user_agent ?
                              config->firmware_user_agent :
                              XJIANG_HTTP_OTA_USER_AGENT;
    firmware_req.headers = config->firmware_headers;
    firmware_req.header_count = config->firmware_header_count;
    firmware_req.timeout_ms = config->firmware_timeout_ms;
    firmware_req.max_header_len = config->firmware_header_max;
    firmware_req.rx_buf_size = config->firmware_rx_buf_size;
    firmware_req.tls = config->firmware_tls ? config->firmware_tls : config->tls;

    qm_memset(&ota_config, 0, sizeof(ota_config));
    ota_config.http_config = &firmware_req;
    ota_config.on_event = xjiang_http_ota_bridge_event;
    ota_config.user = &runtime;
    ota_config.auto_restart = auto_restart;
    ota_config.resume_enable = config->resume_enable;
    ota_config.resume_offset = config->resume_offset;
    ota_config.resume_if_range = config->resume_if_range;
    ota_config.product_secret = config->product_secret;
    ota_config.expected_hmac_sha256_hex = expected_hmac_sha256_hex[0] ?
                                          expected_hmac_sha256_hex :
                                          NULL;
    ota_config.digest_type = QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256;
    ota_config.digest_verify_mode = QM_HTTP_OTA_DIGEST_VERIFY_STREAM;
    ota_config.target = target == XJIANG_HTTP_OTA_TARGET_MCU ?
                        QM_HTTP_OTA_TARGET_MCU :
                        QM_HTTP_OTA_TARGET_SOC;
    ota_config.mcu_ota = config->mcu_ota;
    ota_config.expected_total_size = firmware->file_size;
    ota_config.progress_step = config->progress_step;

    ret = qm_http_ota_begin(&ota_config, &handle);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "upgrade begin failed target=%s ret=%d",
                xjiang_http_ota_target_name(target),
                ret);
        return ret;
    }

    do {
        ret = qm_http_ota_perform(handle);
    } while (ret == -QM_ERR_HTTP_OTA_IN_PROGRESS);

    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "upgrade perform failed target=%s ret=%d",
                xjiang_http_ota_target_name(target),
                ret);
        (void)qm_http_ota_abort(handle);
        return ret;
    }

    (void)qm_http_ota_get_result(handle, &result_snapshot);
    if (ota_result) {
        *ota_result = result_snapshot;
    }
    ret = qm_http_ota_finish(handle);
    if (ret == QM_EOK) {
        if (expected_hmac_sha256_hex[0] != '\0') {
            QM_LOGI(LOG_TAG,
                    "firmware verify success target=%s digest=hmac-sha256 mode=stream sign=%s downloaded=%u",
                    xjiang_http_ota_target_name(target),
                    xjiang_http_ota_safe_str(firmware->sign),
                    (unsigned int)result_snapshot.downloaded_size);
        }
        QM_LOGI(LOG_TAG, "upgrade finish target=%s downloaded=%u total=%u",
                xjiang_http_ota_target_name(target),
                (unsigned int)result_snapshot.downloaded_size,
                (unsigned int)result_snapshot.total_size);
    } else {
        QM_LOGE(LOG_TAG, "upgrade finish failed target=%s ret=%d",
                xjiang_http_ota_target_name(target),
                ret);
    }
    return ret;
}

static int xjiang_http_ota_perform_with_config(const xjiang_http_ota_config_t *config,
                                               const xjiang_http_ota_info_t *info,
                                               xjiang_http_ota_target_t target,
                                               qm_http_ota_result_t *result)
{
    const xjiang_http_ota_firmware_info_t *firmware;
    qm_bool any_available;
    int ret;

    if (!config || !info || config->firmware_header_count < 0 ||
        (target != XJIANG_HTTP_OTA_TARGET_MODULE && target != XJIANG_HTTP_OTA_TARGET_MCU)) {
        return -QM_EINVAL;
    }
    QM_LOGI(LOG_TAG, "perform start target=%s",
            xjiang_http_ota_target_name(target));

    if (result) {
        qm_memset(result, 0, sizeof(*result));
    }

    any_available = (info->module.available || info->mcu.available) ? QM_TRUE : QM_FALSE;
    firmware = xjiang_http_ota_select_firmware(info, target);
    QM_LOGI(LOG_TAG, "perform available module=%d mcu=%d selected=%d",
            info->module.available,
            info->mcu.available,
            firmware ? firmware->available : 0);

    if (!any_available) {
        QM_LOGI(LOG_TAG, "perform no upgrade target=%s",
                xjiang_http_ota_target_name(target));
        return -QM_ERR_XJIANG_HTTP_OTA_NO_UPGRADE;
    }
    if (!firmware || !firmware->available) {
        QM_LOGW(LOG_TAG, "perform target not upgradable target=%s module=%d mcu=%d",
                xjiang_http_ota_target_name(target),
                info->module.available,
                info->mcu.available);
        return -QM_ERR_XJIANG_HTTP_OTA_UPGRADE_NOT_ALLOWED;
    }
    if (target == XJIANG_HTTP_OTA_TARGET_MCU && !config->mcu_ota) {
        QM_LOGE(LOG_TAG, "mcu ota callbacks not registered");
        return -QM_EINVAL;
    }

    ret = xjiang_http_ota_upgrade_target(config, firmware,
                                         target,
                                         config->auto_restart,
                                         result);
    if (ret != QM_EOK) {
        return ret;
    }

    QM_LOGI(LOG_TAG, "perform finish target=%s", xjiang_http_ota_target_name(target));
    return QM_EOK;
}

int xjiang_http_ota_check(const char *pid,
                          const char *module_version,
                          const char *mcu_version,
                          const char *authorization_token,
                          xjiang_http_ota_info_t *info)
{
    xjiang_http_ota_config_t config;

    xjiang_http_ota_fill_config(&config,
                                pid,
                                module_version,
                                mcu_version,
                                authorization_token);
    return xjiang_http_ota_check_with_config(&config, info);
}

int xjiang_http_ota_perform(const xjiang_http_ota_info_t *info,
                            xjiang_http_ota_target_t target,
                            qm_http_ota_result_t *result)
{
    xjiang_http_ota_config_t config;

    xjiang_http_ota_fill_perform_config(&config);
    return xjiang_http_ota_perform_with_config(&config, info, target, result);
}
