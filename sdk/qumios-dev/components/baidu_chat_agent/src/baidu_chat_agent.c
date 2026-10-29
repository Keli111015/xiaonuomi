#include "baidu_chat_agent.h"
#include "baidu_chat_agent_log.h"

#include "cJSON.h"
#include "qm_kernel.h"
#include "qm_string.h"
#include "qm_websocket_client.h"

#define LOG_TAG "baidu_chat_agent"
#define BAIDU_CHAT_AGENT_DEFAULT_ENDPOINT "wss://rtc-aiotgw.exp.bcelive.com/v1/realtime"
#define BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_20MS 20U
#define BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_40MS 40U
#define BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_60MS 60U

#ifndef CONFIG_BAIDU_CHAT_AGENT_WS_RX_SIZE
#define CONFIG_BAIDU_CHAT_AGENT_WS_RX_SIZE 4096
#endif

#ifndef CONFIG_BAIDU_CHAT_AGENT_TIMEOUT_MS
#define CONFIG_BAIDU_CHAT_AGENT_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_BAIDU_CHAT_AGENT_WS_TASK_STACK_SIZE
#define CONFIG_BAIDU_CHAT_AGENT_WS_TASK_STACK_SIZE 8192
#endif

#ifndef CONFIG_BAIDU_CHAT_AGENT_WS_TASK_PRIORITY
#define CONFIG_BAIDU_CHAT_AGENT_WS_TASK_PRIORITY 5
#endif

#ifndef CONFIG_BAIDU_CHAT_AGENT_SEND_QUEUE_SIZE
#define CONFIG_BAIDU_CHAT_AGENT_SEND_QUEUE_SIZE 8
#endif

#ifndef CONFIG_BAIDU_CHAT_AGENT_RECONNECT_INTERVAL_MS
#define CONFIG_BAIDU_CHAT_AGENT_RECONNECT_INTERVAL_MS 1000
#endif

struct baidu_chat_agent {
    baidu_chat_agent_config_t config;
    baidu_chat_agent_event_cb_t event_cb;
    void *user_ctx;
    void *ws;
    char *uri;
    char *headers;
    char *endpoint;
    char *app_id;
    char *audio_codec;
    char *user_id;
    char *device_id;
    char *license_key;
    union {
        struct {
            char *instance_id;
            char *token;
        } instance_token;
        struct {
            char *access_key;
            char *secret_key;
            char *cfg;
        } api_key_secret;
    } auth;
    baidu_chat_agent_auth_mode_t auth_mode;
    uint32_t packet_time_ms;
    uint32_t packet_max_len;
    qm_bool started;
    qm_bool connected;
};

static volatile uint32_t g_baidu_chat_agent_log_flags = BAIDU_CHAT_AGENT_KCONFIG_LOG_FLAGS;

uint32_t baidu_chat_agent_get_log_flags(void)
{
    return g_baidu_chat_agent_log_flags;
}

int baidu_chat_agent_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)BAIDU_CHAT_AGENT_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_baidu_chat_agent_log_flags = log_flags;
    return QM_EOK;
}

typedef struct {
    const char *instance_id;
    const char *token;
    const char *access_key;
    const char *secret_key;
    const char *cfg;
} baidu_chat_agent_auth_view_t;

static int send_compound_text(baidu_chat_agent_handle_t handle,
                              const char *prefix0,
                              const char *prefix1,
                              const char *text,
                              uint32_t timeout_ms);
static qm_bool starts_with(const char *text, uint32_t text_len, const char *prefix);

static const char *bool_text(qm_bool value)
{
    return value ? "true" : "false";
}

static uint32_t default_u32(uint32_t value, uint32_t fallback)
{
    return value == 0U ? fallback : value;
}

static int default_int(int value, int fallback)
{
    return value == 0 ? fallback : value;
}

static void free_string(char **value)
{
    if (value == NULL || *value == NULL) {
        return;
    }

    qm_free(*value);
    *value = NULL;
}

static int replace_string(char **dst, const char *src)
{
    char *copy = NULL;

    if (dst == NULL) {
        return -QM_EINVAL;
    }

    if (src != NULL) {
        copy = qm_strdup(src);
        if (copy == NULL) {
            return -QM_ENOMEM;
        }
    }

    free_string(dst);
    *dst = copy;
    return QM_EOK;
}

static void fill_start_params_auth_view(const baidu_chat_agent_start_params_t *params,
                                        baidu_chat_agent_auth_view_t *view)
{
    if (view == NULL) {
        return;
    }

    qm_memset(view, 0, sizeof(*view));
    if (params == NULL) {
        return;
    }

    if (params->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_INSTANCE_TOKEN) {
        view->instance_id = params->auth.instance_token.instance_id;
        view->token = params->auth.instance_token.token;
    } else if (params->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_API_KEY_SECRET) {
        view->access_key = params->auth.api_key_secret.access_key;
        view->secret_key = params->auth.api_key_secret.secret_key;
        view->cfg = params->auth.api_key_secret.cfg;
    }
}

static void fill_runtime_auth_view(baidu_chat_agent_handle_t handle,
                                   baidu_chat_agent_auth_view_t *view)
{
    if (view == NULL) {
        return;
    }

    qm_memset(view, 0, sizeof(*view));
    if (handle == NULL) {
        return;
    }

    if (handle->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_INSTANCE_TOKEN) {
        view->instance_id = handle->auth.instance_token.instance_id;
        view->token = handle->auth.instance_token.token;
    } else if (handle->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_API_KEY_SECRET) {
        view->access_key = handle->auth.api_key_secret.access_key;
        view->secret_key = handle->auth.api_key_secret.secret_key;
        view->cfg = handle->auth.api_key_secret.cfg;
    }
}

static void clear_runtime_auth_for_mode(baidu_chat_agent_handle_t handle,
                                        baidu_chat_agent_auth_mode_t auth_mode)
{
    if (handle == NULL) {
        return;
    }

    if (auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_INSTANCE_TOKEN) {
        free_string(&handle->auth.instance_token.instance_id);
        free_string(&handle->auth.instance_token.token);
    } else if (auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_API_KEY_SECRET) {
        free_string(&handle->auth.api_key_secret.access_key);
        free_string(&handle->auth.api_key_secret.secret_key);
        free_string(&handle->auth.api_key_secret.cfg);
    }

    qm_memset(&handle->auth, 0, sizeof(handle->auth));
}

static const char *present_text(const char *value)
{
    return (value != NULL && value[0] != '\0') ? "yes" : "no";
}

static qm_bool string_present(const char *value)
{
    return (value != NULL && value[0] != '\0') ? QM_TRUE : QM_FALSE;
}

static qm_bool string_equals(const char *lhs, const char *rhs)
{
    if (lhs == NULL || rhs == NULL) {
        return QM_FALSE;
    }

    return qm_strcmp(lhs, rhs) == 0 ? QM_TRUE : QM_FALSE;
}

static qm_bool is_supported_audio_codec(const char *audio_codec)
{
    if (!string_present(audio_codec)) {
        return QM_FALSE;
    }

    return string_equals(audio_codec, "raw") ||
           string_equals(audio_codec, "raw16k") ||
           string_equals(audio_codec, "pcma") ||
           string_equals(audio_codec, "pcmu") ||
           string_equals(audio_codec, "g722") ||
           string_equals(audio_codec, "opus");
}

static qm_bool is_supported_cfg_language(const char *value)
{
    if (!string_present(value)) {
        return QM_FALSE;
    }

    return string_equals(value, "zh") ||
           string_equals(value, "en");
}

static qm_bool is_supported_cfg_samplerate(uint32_t value)
{
    return value == 8000U ||
           value == 16000U;
}

static qm_bool is_supported_cfg_channel(uint32_t value)
{
    return value == 1U || value == 2U;
}

static qm_bool is_supported_cfg_aopt(uint32_t value)
{
    return value == BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_20MS ||
           value == BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_40MS ||
           value == BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_60MS;
}

static int parse_cfg_string_item(const cJSON *object,
                                 const char *key,
                                 const char **out_value)
{
    const cJSON *item;

    if (object == NULL || key == NULL || out_value == NULL) {
        return -QM_EINVAL;
    }

    *out_value = NULL;
    item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == NULL) {
        return -QM_ENOENT;
    }

    if (!cJSON_IsString(item) || cJSON_GetStringValue(item) == NULL) {
        return -QM_EINVAL;
    }

    *out_value = cJSON_GetStringValue(item);
    return QM_EOK;
}

static int parse_cfg_u32_item(const cJSON *object,
                              const char *key,
                              uint32_t *out_value)
{
    const cJSON *item;
    double number;
    uint32_t number_value;

    if (object == NULL || key == NULL || out_value == NULL) {
        return -QM_EINVAL;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == NULL) {
        return -QM_ENOENT;
    }

    if (!cJSON_IsNumber(item)) {
        return -QM_EINVAL;
    }

    number = cJSON_GetNumberValue(item);
    if (number < 0.0 || number > 4294967295.0) {
        return -QM_EINVAL;
    }

    number_value = (uint32_t)number;
    if ((double)number_value != number) {
        return -QM_EINVAL;
    }

    *out_value = number_value;
    return QM_EOK;
}

static int parse_cfg_bool_item(const cJSON *object,
                               const char *key,
                               qm_bool *out_value)
{
    const cJSON *item;

    if (object == NULL || key == NULL || out_value == NULL) {
        return -QM_EINVAL;
    }

    item = cJSON_GetObjectItemCaseSensitive(object, key);
    if (item == NULL) {
        return -QM_ENOENT;
    }

    if (!cJSON_IsBool(item)) {
        return -QM_EINVAL;
    }

    *out_value = cJSON_IsTrue(item) ? QM_TRUE : QM_FALSE;
    return QM_EOK;
}

static int validate_cfg_json(const char *cfg, const char *audio_codec)
{
    cJSON *root = NULL;
    const char *cfg_audio_codec = NULL;
    const char *cfg_language = NULL;
    uint32_t sample_rate = 0U;
    uint32_t channels = 0U;
    uint32_t aopt = 0U;
    qm_bool dfda = QM_FALSE;
    int ret;

    if (!string_present(cfg)) {
        return QM_EOK;
    }

    root = cJSON_ParseWithLength(cfg, qm_strlen(cfg));
    if (root == NULL) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg json parse failed");
        return -QM_EINVAL;
    }

    if (!cJSON_IsObject(root)) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg root is not object");
        cJSON_Delete(root);
        return -QM_EINVAL;
    }

    ret = parse_cfg_string_item(root, "audiocodec", &cfg_audio_codec);
    if (ret == QM_EOK && string_equals(cfg_audio_codec, audio_codec) == QM_FALSE) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg audiocodec mismatch");
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg audiocodec invalid type");
        cJSON_Delete(root);
        return ret;
    }

    ret = parse_cfg_string_item(root, "language", &cfg_language);
    if (ret == QM_EOK && is_supported_cfg_language(cfg_language) == QM_FALSE) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg language unsupported");
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg language invalid type");
        cJSON_Delete(root);
        return ret;
    }

    ret = parse_cfg_u32_item(root, "samplerate", &sample_rate);
    if (ret == QM_EOK && is_supported_cfg_samplerate(sample_rate) == QM_FALSE) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg samplerate unsupported");
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg samplerate invalid type");
        cJSON_Delete(root);
        return ret;
    }

    ret = parse_cfg_u32_item(root, "channels", &channels);
    if (ret == QM_EOK && is_supported_cfg_channel(channels) == QM_FALSE) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg channels unsupported");
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg channels invalid type");
        cJSON_Delete(root);
        return ret;
    }

    ret = parse_cfg_bool_item(root, "dfda", &dfda);
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg dfda invalid type");
        cJSON_Delete(root);
        return ret;
    }

    ret = parse_cfg_u32_item(root, "aopt", &aopt);
    if (ret == QM_EOK && is_supported_cfg_aopt(aopt) == QM_FALSE) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg aopt unsupported");
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    if (ret != QM_EOK && ret != -QM_ENOENT) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg aopt invalid type");
        cJSON_Delete(root);
        return ret;
    }

    cJSON_Delete(root);
    return QM_EOK;
}

static uint32_t url_encoded_length(const char *src)
{
    uint32_t len = 0U;
    unsigned char ch;

    if (src == NULL) {
        return 0U;
    }

    while (*src != '\0') {
        ch = (unsigned char)(*src);
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            len += 1U;
        } else {
            len += 3U;
        }
        src++;
    }

    return len;
}

static int url_encode_copy(char *dst, uint32_t dst_len, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t offset = 0U;
    unsigned char ch;

    if (dst == NULL || dst_len == 0U) {
        return -QM_EINVAL;
    }

    if (src == NULL) {
        dst[0] = '\0';
        return QM_EOK;
    }

    while (*src != '\0') {
        ch = (unsigned char)(*src);
        if ((ch >= 'A' && ch <= 'Z') ||
            (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') ||
            ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            if (offset + 1U >= dst_len) {
                return -QM_EFULL;
            }
            dst[offset++] = (char)ch;
        } else {
            if (offset + 3U >= dst_len) {
                return -QM_EFULL;
            }
            dst[offset++] = '%';
            dst[offset++] = hex[(ch >> 4) & 0x0FU];
            dst[offset++] = hex[ch & 0x0FU];
        }
        src++;
    }

    dst[offset] = '\0';
    return QM_EOK;
}

static char query_separator(const char *uri)
{
    if (uri == NULL) {
        return '?';
    }

    while (*uri != '\0') {
        if (*uri == '?') {
            return '&';
        }
        uri++;
    }

    return '?';
}

static int append_query_string(char *dst,
                               uint32_t dst_len,
                               uint32_t *offset,
                               const char *key,
                               const char *value)
{
    int ret;

    if (dst == NULL || offset == NULL || key == NULL || value == NULL) {
        return -QM_EINVAL;
    }

    ret = qm_snprintf(dst + *offset,
                      (int)(dst_len - *offset),
                      "%c%s=%s",
                      query_separator(dst),
                      key,
                      value);
    if (ret < 0 || (uint32_t)ret >= (dst_len - *offset)) {
        return -QM_EFULL;
    }

    *offset += (uint32_t)ret;
    return QM_EOK;
}

static int append_query_u32(char *dst,
                            uint32_t dst_len,
                            uint32_t *offset,
                            const char *key,
                            uint32_t value)
{
    int ret;

    if (dst == NULL || offset == NULL || key == NULL) {
        return -QM_EINVAL;
    }

    ret = qm_snprintf(dst + *offset,
                      (int)(dst_len - *offset),
                      "%c%s=%lu",
                      query_separator(dst),
                      key,
                      (unsigned long)value);
    if (ret < 0 || (uint32_t)ret >= (dst_len - *offset)) {
        return -QM_EFULL;
    }

    *offset += (uint32_t)ret;
    return QM_EOK;
}

static void mask_preview(const char *src, char *dst, uint32_t dst_len)
{
    uint32_t len;

    if (dst == NULL || dst_len == 0U) {
        return;
    }

    dst[0] = '\0';
    if (src == NULL) {
        qm_snprintf(dst, (int)dst_len, "null");
        return;
    }

    len = qm_strlen(src);
    if (len == 0U) {
        qm_snprintf(dst, (int)dst_len, "empty");
        return;
    }

    if (len <= 6U) {
        qm_snprintf(dst, (int)dst_len, "***(%lu)", (unsigned long)len);
        return;
    }

    qm_snprintf(dst,
                (int)dst_len,
                "%c%c***%c%c(len=%lu)",
                src[0],
                src[1],
                src[len - 2U],
                src[len - 1U],
                (unsigned long)len);
}

static void log_runtime_identity(baidu_chat_agent_handle_t handle, const char *phase)
{
    char user_preview[32];
    char device_preview[32];
    char license_preview[32];

    if (handle == NULL || phase == NULL) {
        return;
    }

    mask_preview(handle->user_id, user_preview, sizeof(user_preview));
    mask_preview(handle->device_id, device_preview, sizeof(device_preview));
    mask_preview(handle->license_key, license_preview, sizeof(license_preview));

    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_CONTROL,
            "%s user=%s device=%s license=%s headers=%s endpoint=%s auth_mode=%d",
            phase,
            user_preview,
            device_preview,
            license_preview,
            present_text(handle->headers),
            present_text(handle->endpoint),
            handle->auth_mode);
}

static void log_text_summary(const char *phase,
                             const char *prefix,
                             const char *text,
                             uint32_t text_len)
{
    uint32_t preview_len;
    char preview[48];

    if (phase == NULL) {
        return;
    }

    preview[0] = '\0';
    if (text != NULL && text_len > 0U) {
        preview_len = text_len > 16U ? 16U : text_len;
        qm_memcpy(preview, text, preview_len);
        preview[preview_len] = '\0';
    } else {
        qm_snprintf(preview, sizeof(preview), "empty");
    }

    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_PAYLOAD,
            "%s prefix=%s len=%lu preview=%s%s",
            phase,
            prefix != NULL ? prefix : "none",
            (unsigned long)text_len,
            preview,
            text_len > 16U ? "..." : "");
}

static void log_full_text(const char *phase, const char *text, uint32_t text_len)
{
    char *text_copy;

    if (phase == NULL || text == NULL) {
        return;
    }

    text_copy = (char *)qm_malloc(text_len + 1U);
    if (text_copy == NULL) {
        BAIDU_CHAT_AGENT_LOGW(LOG_TAG, BAIDU_CHAT_AGENT_LOG_PAYLOAD,
                              "%s full text alloc failed len=%lu", phase, (unsigned long)text_len);
        return;
    }

    if (text_len > 0U) {
        qm_memcpy(text_copy, text, text_len);
    }
    text_copy[text_len] = '\0';

    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_PAYLOAD, "%s text=%s", phase, text_copy);
    qm_free(text_copy);
}

static void log_full_prefixed_text(const char *phase,
                                   const char *prefix,
                                   const char *text,
                                   uint32_t text_len)
{
    char *text_copy;

    if (phase == NULL || text == NULL) {
        return;
    }

    text_copy = (char *)qm_malloc(text_len + 1U);
    if (text_copy == NULL) {
        BAIDU_CHAT_AGENT_LOGW(LOG_TAG, BAIDU_CHAT_AGENT_LOG_PAYLOAD,
                "%s full text alloc failed prefix=%s len=%lu",
                phase,
                prefix != NULL ? prefix : "none",
                (unsigned long)text_len);
        return;
    }

    if (text_len > 0U) {
        qm_memcpy(text_copy, text, text_len);
    }
    text_copy[text_len] = '\0';

    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_PAYLOAD,
            "%s prefix=%s len=%lu text=%s",
            phase,
            prefix != NULL ? prefix : "none",
            (unsigned long)text_len,
            text_copy);
    qm_free(text_copy);
}

static void log_license_active_payload(const char *payload)
{
    cJSON *root = NULL;
    const cJSON *dev_id;
    const cJSON *uid;
    const cJSON *lic_key;
    char lic_preview[32];

    if (payload == NULL) {
        return;
    }

    root = cJSON_Parse(payload);
    if (root == NULL || !cJSON_IsObject(root)) {
        if (root != NULL) {
            cJSON_Delete(root);
        }
        BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_PAYLOAD,
                              "send license active frame=[E]:[LIC]:[ACTIVE]:%s", payload);
        return;
    }

    dev_id = cJSON_GetObjectItemCaseSensitive(root, "devId");
    uid = cJSON_GetObjectItemCaseSensitive(root, "uId");
    lic_key = cJSON_GetObjectItemCaseSensitive(root, "licKey");
    mask_preview((cJSON_IsString(lic_key) && cJSON_GetStringValue(lic_key) != NULL) ?
                 cJSON_GetStringValue(lic_key) :
                 NULL,
                 lic_preview,
                 sizeof(lic_preview));

    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_PAYLOAD,
            "send license active frame=[E]:[LIC]:[ACTIVE]:{\"devId\":\"%s\",\"uId\":\"%s\",\"licKey\":\"%s\"}",
            (cJSON_IsString(dev_id) && cJSON_GetStringValue(dev_id) != NULL) ?
            cJSON_GetStringValue(dev_id) :
            "",
            (cJSON_IsString(uid) && cJSON_GetStringValue(uid) != NULL) ?
            cJSON_GetStringValue(uid) :
            "",
            lic_preview);
    cJSON_Delete(root);
}

static void emit_error(baidu_chat_agent_handle_t handle,
                       baidu_chat_agent_error_type_t type,
                       int code,
                       const char *message)
{
    baidu_chat_agent_event_t event;

    if (handle == NULL || handle->event_cb == NULL) {
        return;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = BAIDU_CHAT_AGENT_EVENT_ERROR;
    event.data.error.type = type;
    event.data.error.code = code;
    event.data.error.message = message;
    handle->event_cb(handle, &event, handle->user_ctx);
}

static void emit_text_event(baidu_chat_agent_handle_t handle,
                            baidu_chat_agent_event_id_t event_id,
                            baidu_chat_agent_text_type_t type,
                            baidu_chat_agent_message_semantic_t semantic,
                            const char *prefix,
                            const char *text,
                            uint32_t text_len,
                            qm_bool final)
{
    baidu_chat_agent_event_t event;
    char *text_copy = NULL;

    if (handle == NULL || handle->event_cb == NULL) {
        return;
    }

    /**
     * WebSocket text payload slices are length-delimited and are not guaranteed
     * to be NUL-terminated. Build a temporary C string so upper layers can log
     * or clone the text safely during the callback.
     */
    if (text != NULL) {
        text_copy = (char *)qm_malloc(text_len + 1U);
        if (text_copy == NULL) {
            emit_error(handle,
                       BAIDU_CHAT_AGENT_ERROR_MEMORY,
                       -QM_ENOMEM,
                       "text copy alloc failed");
            return;
        }
        if (text_len > 0U) {
            qm_memcpy(text_copy, text, text_len);
        }
        text_copy[text_len] = '\0';
        text = text_copy;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = event_id;
    event.data.text.type = type;
    event.data.text.semantic = semantic;
    event.data.text.prefix = prefix;
    event.data.text.text = text;
    event.data.text.text_len = text_len;
    event.data.text.final = final;
    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK,
            "emit text event id=%d type=%d final=%s prefix=%s len=%lu",
            event_id,
            type,
            bool_text(final),
            prefix != NULL ? prefix : "none",
            (unsigned long)text_len);
    handle->event_cb(handle, &event, handle->user_ctx);
    qm_free(text_copy);
}

static baidu_chat_agent_message_semantic_t classify_event_semantic(const char *text,
                                                                   uint32_t text_len)
{
    if (starts_with(text, text_len, "[LIC]:")) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_ASR_START_LONGTEXT_REC)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_ASR_START_LONGTEXT_REC;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_ASR_STOP_LONGTEXT_REC)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_ASR_STOP_LONGTEXT_REC;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_ASR_ENABLE_REALTIME)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_ASR_ENABLE_REALTIME;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_ASR_DISABLE_REALTIME)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_ASR_DISABLE_REALTIME;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_PLAY_AUDIO)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_PLAY_AUDIO;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_TTS_BEGIN_SPEAKING)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_TTS_BEGIN_SPEAKING;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_TTS_END_SPEAKING)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_TTS_END_SPEAKING;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_VOICE_COMING)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_VOICE_COMING;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_VOICE_DISAPPEAR)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_VOICE_DISAPPEAR;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_BEGIN)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_BEGIN;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_END)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_END;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_STOP)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_STOP;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_PAUSE)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_PAUSE;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_RESUME)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_RESUME;
    }
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_EVENT_MEDIA_READY)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_MEDIA_READY;
    }
    return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE;
}

static baidu_chat_agent_message_semantic_t classify_set_semantic(const char *text,
                                                                 uint32_t text_len)
{
    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_SET_MEDIA_GENERATE_DISABLE)) {
        return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_SET_MEDIA_GENERATE_DISABLE;
    }
    return BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE;
}

static void emit_audio_event(baidu_chat_agent_handle_t handle,
                             const uint8_t *data,
                             uint32_t data_len,
                             uint32_t total_data_len,
                             uint32_t current_data_offset)
{
    baidu_chat_agent_event_t event;

    if (handle == NULL || handle->event_cb == NULL) {
        return;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = BAIDU_CHAT_AGENT_EVENT_AUDIO;
    event.data.audio.data = data;
    event.data.audio.data_len = data_len;
    event.data.audio.total_data_len = total_data_len;
    event.data.audio.current_data_offset = current_data_offset;
    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK,
            "emit audio event chunk=%lu total=%lu offset=%lu",
            (unsigned long)data_len,
            (unsigned long)total_data_len,
            (unsigned long)current_data_offset);
    handle->event_cb(handle, &event, handle->user_ctx);
}

static void emit_license_event(baidu_chat_agent_handle_t handle,
                               baidu_chat_agent_license_state_t state,
                               const char *text,
                               uint32_t text_len)
{
    baidu_chat_agent_event_t event;
    char *text_copy = NULL;

    if (handle == NULL || handle->event_cb == NULL) {
        return;
    }

    if (text != NULL) {
        text_copy = (char *)qm_malloc(text_len + 1U);
        if (text_copy == NULL) {
            emit_error(handle,
                       BAIDU_CHAT_AGENT_ERROR_MEMORY,
                       -QM_ENOMEM,
                       "license text copy alloc failed");
            return;
        }
        if (text_len > 0U) {
            qm_memcpy(text_copy, text, text_len);
        }
        text_copy[text_len] = '\0';
        text = text_copy;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = BAIDU_CHAT_AGENT_EVENT_LICENSE;
    event.data.license.state = state;
    event.data.license.text = text;
    event.data.license.text_len = text_len;
    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK,
            "emit license event state=%d len=%lu",
            state,
            (unsigned long)text_len);
    handle->event_cb(handle, &event, handle->user_ctx);
    qm_free(text_copy);
}

static int validate_start_params(const baidu_chat_agent_start_params_t *params)
{
    int ret;
    qm_bool has_instance_auth;
    qm_bool has_key_secret_auth;
    baidu_chat_agent_auth_view_t auth_view;

    if (params == NULL || !string_present(params->app_id) || !string_present(params->audio_codec)) {
        return -QM_EINVAL;
    }

    if (is_supported_audio_codec(params->audio_codec) == QM_FALSE) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "invalid audio codec ac=%s", params->audio_codec);
        return -QM_EINVAL;
    }

    if (params->packet_time_ms != 0U &&
        params->packet_time_ms != BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_20MS &&
        params->packet_time_ms != BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_40MS &&
        params->packet_time_ms != BAIDU_CHAT_AGENT_AUDIO_PACKET_TIME_60MS) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "invalid ptime=%lu", (unsigned long)params->packet_time_ms);
        return -QM_EINVAL;
    }

    if (!string_equals(params->audio_codec, "opus")) {
        if (params->packet_time_ms > 0U) {
            BAIDU_CHAT_AGENT_LOGE(LOG_TAG,
                    "ptime only allowed for opus ac=%s ptime=%lu",
                    params->audio_codec,
                    (unsigned long)params->packet_time_ms);
            return -QM_EINVAL;
        }
        if (params->packet_max_len > 0U) {
            BAIDU_CHAT_AGENT_LOGE(LOG_TAG,
                    "plen only allowed for opus ac=%s plen=%lu",
                    params->audio_codec,
                    (unsigned long)params->packet_max_len);
            return -QM_EINVAL;
        }
    }

    fill_start_params_auth_view(params, &auth_view);

    has_instance_auth =
        string_present(auth_view.instance_id) || string_present(auth_view.token);
    has_key_secret_auth =
        string_present(auth_view.access_key) || string_present(auth_view.secret_key);

    if (params->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_INSTANCE_TOKEN) {
        if (!string_present(auth_view.instance_id) || !string_present(auth_view.token)) {
            return -QM_EINVAL;
        }
        if (has_key_secret_auth == QM_TRUE) {
            return -QM_EINVAL;
        }
        if (string_present(auth_view.cfg)) {
            BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg not allowed in instance-token mode");
            return -QM_EINVAL;
        }
    } else if (params->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_API_KEY_SECRET) {
        if (!string_present(auth_view.access_key) || !string_present(auth_view.secret_key)) {
            return -QM_EINVAL;
        }
        if (has_instance_auth == QM_TRUE) {
            return -QM_EINVAL;
        }
    } else {
        return -QM_EINVAL;
    }

    ret = validate_cfg_json(auth_view.cfg, params->audio_codec);
    if (ret != QM_EOK) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "cfg validation failed ac=%s cfg=%s",
                params->audio_codec,
                present_text(auth_view.cfg));
        return ret;
    }

    return QM_EOK;
}

static qm_bool starts_with(const char *text, uint32_t text_len, const char *prefix)
{
    uint32_t prefix_len;

    if (text == NULL || prefix == NULL) {
        return QM_FALSE;
    }

    prefix_len = qm_strlen(prefix);
    if (text_len < prefix_len) {
        return QM_FALSE;
    }

    return (qm_strncmp(text, prefix, prefix_len) == 0) ? QM_TRUE : QM_FALSE;
}

static int build_license_active_payload(baidu_chat_agent_handle_t handle, char **out_payload)
{
    cJSON *root = NULL;
    char *payload = NULL;

    if (handle == NULL || out_payload == NULL) {
        return -QM_EINVAL;
    }

    *out_payload = NULL;
    if (!string_present(handle->device_id) ||
        !string_present(handle->user_id) ||
        !string_present(handle->license_key)) {
        const char *missing_field = !string_present(handle->device_id) ? "device_id" :
                                    (!string_present(handle->user_id) ? "user_id" :
                                     "license_key");
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG,
                "license active missing field=%s device=%s user=%s lic=%s",
                missing_field,
                present_text(handle->device_id),
                present_text(handle->user_id),
                present_text(handle->license_key));
        return -QM_EINVAL;
    }

    root = cJSON_CreateObject();
    if (root == NULL) {
        return -QM_ENOMEM;
    }

    if (!cJSON_AddStringToObject(root, "devId", handle->device_id) ||
        !cJSON_AddStringToObject(root, "uId", handle->user_id) ||
        !cJSON_AddStringToObject(root, "licKey", handle->license_key)) {
        cJSON_Delete(root);
        return -QM_ENOMEM;
    }

    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (payload == NULL) {
        return -QM_ENOMEM;
    }

    *out_payload = payload;
    return QM_EOK;
}

static void handle_license_event(baidu_chat_agent_handle_t handle,
                                 const char *text,
                                 uint32_t text_len)
{
    char *payload = NULL;
    int ret;

    if (handle == NULL || text == NULL) {
        return;
    }

    if (starts_with(text, text_len, "[LIC]:[MUST]:")) {
        BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK, "license event must received");
        emit_license_event(handle,
                           BAIDU_CHAT_AGENT_LICENSE_STATE_MUST,
                           text,
                           text_len);
        ret = build_license_active_payload(handle, &payload);
        if (ret != QM_EOK) {
            BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "license_key missing or identity incomplete, skip [LIC]:[ACTIVE] send");
            emit_error(handle,
                       BAIDU_CHAT_AGENT_ERROR_STATE,
                       ret,
                       "license active payload build failed");
            return;
        }

        BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_UPLINK,
                "license event active send user=%s device=%s lic=%s",
                present_text(handle->user_id),
                present_text(handle->device_id),
                present_text(handle->license_key));
        log_license_active_payload(payload);
        emit_license_event(handle,
                           BAIDU_CHAT_AGENT_LICENSE_STATE_ACTIVE_SENT,
                           payload,
                           (uint32_t)qm_strlen(payload));
        ret = send_compound_text(handle,
                                 BAIDU_CHAT_AGENT_PREFIX_EVENT,
                                 "[LIC]:[ACTIVE]:",
                                 payload,
                                 0U);
        cJSON_free(payload);
        if (ret != QM_EOK) {
            BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "license event active send failed ret=%d", ret);
            emit_error(handle,
                       BAIDU_CHAT_AGENT_ERROR_WS,
                       ret,
                       "license active send failed");
        }
        return;
    }

    if (starts_with(text, text_len, "[LIC]:[RES]:[PASS]:")) {
        BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK, "license event pass");
        emit_license_event(handle,
                           BAIDU_CHAT_AGENT_LICENSE_STATE_PASS,
                           text,
                           text_len);
        return;
    }

    if (starts_with(text, text_len, "[LIC]:[RES]:[FAILED]:")) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "license event failed");
        emit_license_event(handle,
                           BAIDU_CHAT_AGENT_LICENSE_STATE_FAILED,
                           text,
                           text_len);
        emit_error(handle,
                   BAIDU_CHAT_AGENT_ERROR_PROTOCOL,
                   -QM_EIO,
                   "license activation failed");
        return;
    }
}

static void dispatch_text(baidu_chat_agent_handle_t handle, const char *text, uint32_t text_len)
{
    const char *event_text;
    uint32_t event_text_len;

    if (text == NULL || text_len == 0U) {
        BAIDU_CHAT_AGENT_LOGW(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK, "recv empty text frame");
        emit_text_event(handle,
                        BAIDU_CHAT_AGENT_EVENT_TEXT,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_UNKNOWN,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        NULL,
                        text,
                        text_len,
                        QM_FALSE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_ASR_MID_APPEND)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_ASR_MID_APPEND,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_MID_APPEND),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_MID_APPEND));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_ASR,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_ASR_MID_APPEND,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_ASR_MID_APPEND,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_MID_APPEND),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_MID_APPEND),
                        QM_FALSE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL_APPEND)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL_APPEND,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL_APPEND),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL_APPEND));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_ASR,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_ASR_FINAL_APPEND,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL_APPEND,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL_APPEND),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL_APPEND),
                        QM_TRUE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_ASR_MID)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_ASR_MID,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_MID),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_MID));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_ASR,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_ASR_MID,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_ASR_MID,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_MID),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_MID),
                        QM_FALSE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_ASR,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_ASR_FINAL,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL),
                        QM_TRUE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_AGENT_MID)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_AGENT_MID,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_MID),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_MID));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_AGENT_TEXT,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_AGENT_MID,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_AGENT_MID,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_MID),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_MID),
                        QM_FALSE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_AGENT_HANGUP)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_AGENT_HANGUP,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_HANGUP),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_HANGUP));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_AGENT_TEXT,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_AGENT_HANGUP,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_AGENT_HANGUP,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_HANGUP),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_HANGUP),
                        QM_TRUE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_AGENT_FINAL)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_AGENT_FINAL,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_FINAL),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_FINAL));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_AGENT_TEXT,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_AGENT_FINAL,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_AGENT_FINAL,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_FINAL),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_AGENT_FINAL),
                        QM_TRUE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_BREAK)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_BREAK,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_BREAK),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_BREAK));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_BREAK,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_BREAK,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_BREAK,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_BREAK),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_BREAK),
                        QM_TRUE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_FUNCTION_CALL)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_FUNCTION_CALL,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_FUNCTION_CALL),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_FUNCTION_CALL));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_FUNCTION_CALL,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_FUNCTION_CALL,
                        BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                        BAIDU_CHAT_AGENT_PREFIX_FUNCTION_CALL,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_FUNCTION_CALL),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_FUNCTION_CALL),
                        QM_TRUE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_EVENT)) {
        event_text = text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_EVENT);
        event_text_len = text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_EVENT);
        log_full_text("recv event", event_text, event_text_len);
        handle_license_event(handle,
                             event_text,
                             event_text_len);
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_EVENT_TEXT,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_EVENT,
                        classify_event_semantic(event_text, event_text_len),
                        BAIDU_CHAT_AGENT_PREFIX_EVENT,
                        event_text,
                        event_text_len,
                        QM_TRUE);
        return;
    }

    if (starts_with(text, text_len, BAIDU_CHAT_AGENT_PREFIX_SET)) {
        log_text_summary("recv text", BAIDU_CHAT_AGENT_PREFIX_SET,
                         text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_SET),
                         text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_SET));
        emit_text_event(handle, BAIDU_CHAT_AGENT_EVENT_SET_TEXT,
                        BAIDU_CHAT_AGENT_TEXT_TYPE_SET,
                        classify_set_semantic(text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_SET),
                                              text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_SET)),
                        BAIDU_CHAT_AGENT_PREFIX_SET,
                        text + qm_strlen(BAIDU_CHAT_AGENT_PREFIX_SET),
                        text_len - qm_strlen(BAIDU_CHAT_AGENT_PREFIX_SET),
                        QM_TRUE);
        return;
    }

    log_text_summary("recv text", "unknown", text, text_len);
    emit_text_event(handle,
                    BAIDU_CHAT_AGENT_EVENT_TEXT,
                    BAIDU_CHAT_AGENT_TEXT_TYPE_UNKNOWN,
                    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE,
                    NULL,
                    text,
                    text_len,
                    QM_FALSE);
}

static void websocket_event_handler(qm_websocket_event_id_t event_id,
                                    qm_websocket_event_data_t *event_data,
                                    void *user_context)
{
    baidu_chat_agent_handle_t handle = (baidu_chat_agent_handle_t)user_context;

    if (handle == NULL) {
        return;
    }

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            handle->connected = QM_TRUE;
            BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE, "websocket connected");
            log_runtime_identity(handle, "session ready");
            if (handle->event_cb != NULL) {
                baidu_chat_agent_event_t event;

                qm_memset(&event, 0, sizeof(event));
                event.id = BAIDU_CHAT_AGENT_EVENT_CONNECTED;
                handle->event_cb(handle, &event, handle->user_ctx);
            }
            break;
        case WEBSOCKET_EVENT_DATA:
            if (event_data == NULL || event_data->data_ptr == NULL || event_data->data_len <= 0) {
                BAIDU_CHAT_AGENT_LOGW(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK,
                                      "recv invalid websocket data event");
                return;
            }

            if (event_data->opcode == 2) {
                BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK,
                        "recv binary audio chunk=%d total=%d offset=%d fin=%s",
                        event_data->data_len,
                        event_data->payload_len,
                        event_data->payload_offset,
                        bool_text(event_data->fin));
                emit_audio_event(handle,
                                 (const uint8_t *)event_data->data_ptr,
                                 (uint32_t)event_data->data_len,
                                 (uint32_t)event_data->payload_len,
                                 (uint32_t)event_data->payload_offset);
            } else {
                BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_DOWNLINK,
                        "recv text frame len=%d total=%d offset=%d fin=%s",
                        event_data->data_len,
                        event_data->payload_len,
                        event_data->payload_offset,
                        bool_text(event_data->fin));
                dispatch_text(handle,
                              event_data->data_ptr,
                              (uint32_t)event_data->data_len);
            }
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            handle->connected = QM_FALSE;
            BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE, "websocket disconnected");
            if (handle->event_cb != NULL) {
                baidu_chat_agent_event_t event;

                qm_memset(&event, 0, sizeof(event));
                event.id = BAIDU_CHAT_AGENT_EVENT_DISCONNECTED;
                handle->event_cb(handle, &event, handle->user_ctx);
            }
            break;
        case WEBSOCKET_EVENT_ERROR:
            BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "websocket error code=%d type=%d transport=%d status=%d",
                    event_data != NULL ? event_data->error_code : -QM_EIO,
                    event_data != NULL ? event_data->error_info.error_type : 0,
                    event_data != NULL ? event_data->error_info.transport_error : 0,
                    event_data != NULL ? event_data->error_info.handshake_status_code : 0);
            emit_error(handle,
                       BAIDU_CHAT_AGENT_ERROR_WS,
                       event_data != NULL ? event_data->error_code : -QM_EIO,
                       "websocket error");
            break;
        default:
            break;
    }
}

static int configure_runtime_strings(baidu_chat_agent_handle_t handle,
                                     const baidu_chat_agent_start_params_t *params)
{
    int ret;
    baidu_chat_agent_auth_view_t auth_view;

    fill_start_params_auth_view(params, &auth_view);

    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_CONTROL,
            "configure runtime endpoint=%s app_id=%s instance=%s token=%s ak=%s sk=%s ac=%s cfg=%s headers=%s user=%s device=%s lic=%s auth_mode=%d ptime=%lu plen=%lu",
            present_text(BAIDU_CHAT_AGENT_DEFAULT_ENDPOINT),
            present_text(params->app_id),
            present_text(auth_view.instance_id),
            present_text(auth_view.token),
            present_text(auth_view.access_key),
            present_text(auth_view.secret_key),
            present_text(params->audio_codec),
            present_text(auth_view.cfg),
            present_text(params->headers),
            present_text(params->user_id),
            present_text(params->device_id),
            present_text(params->license_key),
            params->auth_mode,
            (unsigned long)params->packet_time_ms,
            (unsigned long)params->packet_max_len);

    ret = replace_string(&handle->endpoint, BAIDU_CHAT_AGENT_DEFAULT_ENDPOINT);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = replace_string(&handle->app_id, params->app_id);
    if (ret != QM_EOK) {
        return ret;
    }

    clear_runtime_auth_for_mode(handle, handle->auth_mode);
    handle->auth_mode = params->auth_mode;

    if (handle->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_INSTANCE_TOKEN) {
        ret = replace_string(&handle->auth.instance_token.instance_id,
                             auth_view.instance_id);
        if (ret != QM_EOK) {
            return ret;
        }

        ret = replace_string(&handle->auth.instance_token.token, auth_view.token);
        if (ret != QM_EOK) {
            return ret;
        }
    } else if (handle->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_API_KEY_SECRET) {
        ret = replace_string(&handle->auth.api_key_secret.access_key,
                             auth_view.access_key);
        if (ret != QM_EOK) {
            return ret;
        }

        ret = replace_string(&handle->auth.api_key_secret.secret_key,
                             auth_view.secret_key);
        if (ret != QM_EOK) {
            return ret;
        }

        ret = replace_string(&handle->auth.api_key_secret.cfg, auth_view.cfg);
        if (ret != QM_EOK) {
            return ret;
        }
    } else {
        return -QM_EINVAL;
    }

    ret = replace_string(&handle->audio_codec, params->audio_codec);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = replace_string(&handle->headers, params->headers);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = replace_string(&handle->user_id, params->user_id);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = replace_string(&handle->device_id, params->device_id);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = replace_string(&handle->license_key, params->license_key);
    if (ret != QM_EOK) {
        return ret;
    }

    handle->packet_time_ms = params->packet_time_ms;
    handle->packet_max_len = params->packet_max_len;
    if (!string_present(handle->license_key)) {
        BAIDU_CHAT_AGENT_LOGW(LOG_TAG, BAIDU_CHAT_AGENT_LOG_CONTROL,
                              "license_key absent at start, [LIC]:[ACTIVE] cannot be sent if server requires licensing");
    }
    return QM_EOK;
}

static int build_runtime_uri(baidu_chat_agent_handle_t handle)
{
    uint32_t endpoint_len;
    uint32_t app_id_encoded_len = 0U;
    uint32_t instance_id_encoded_len = 0U;
    uint32_t token_encoded_len = 0U;
    uint32_t access_key_encoded_len = 0U;
    uint32_t secret_key_encoded_len = 0U;
    uint32_t audio_codec_encoded_len = 0U;
    uint32_t cfg_encoded_len = 0U;
    uint32_t total_len;
    uint32_t offset;
    int ret;
    char *app_id_encoded = NULL;
    char *instance_id_encoded = NULL;
    char *token_encoded = NULL;
    char *access_key_encoded = NULL;
    char *secret_key_encoded = NULL;
    char *audio_codec_encoded = NULL;
    char *cfg_encoded = NULL;
    char *uri = NULL;
    baidu_chat_agent_auth_view_t auth_view;

    if (handle == NULL || !string_present(handle->endpoint) ||
        !string_present(handle->app_id) || !string_present(handle->audio_codec)) {
        return -QM_EINVAL;
    }

    fill_runtime_auth_view(handle, &auth_view);
    endpoint_len = (uint32_t)qm_strlen(handle->endpoint);
    app_id_encoded_len = url_encoded_length(handle->app_id);
    audio_codec_encoded_len = url_encoded_length(handle->audio_codec);
    if (string_present(auth_view.instance_id)) {
        instance_id_encoded_len = url_encoded_length(auth_view.instance_id);
    }
    if (string_present(auth_view.token)) {
        token_encoded_len = url_encoded_length(auth_view.token);
    }
    if (string_present(auth_view.access_key)) {
        access_key_encoded_len = url_encoded_length(auth_view.access_key);
    }
    if (string_present(auth_view.secret_key)) {
        secret_key_encoded_len = url_encoded_length(auth_view.secret_key);
    }
    if (string_present(auth_view.cfg)) {
        cfg_encoded_len = url_encoded_length(auth_view.cfg);
    }

    total_len = endpoint_len + 256U + app_id_encoded_len + audio_codec_encoded_len +
                instance_id_encoded_len + token_encoded_len + access_key_encoded_len +
                secret_key_encoded_len + cfg_encoded_len;

    uri = (char *)qm_malloc(total_len + 1U);
    if (uri == NULL) {
        return -QM_ENOMEM;
    }

    app_id_encoded = (char *)qm_malloc(app_id_encoded_len + 1U);
    audio_codec_encoded = (char *)qm_malloc(audio_codec_encoded_len + 1U);
    if (app_id_encoded == NULL || audio_codec_encoded == NULL) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = url_encode_copy(app_id_encoded, app_id_encoded_len + 1U, handle->app_id);
    if (ret != QM_EOK) {
        goto cleanup;
    }
    ret = url_encode_copy(audio_codec_encoded, audio_codec_encoded_len + 1U, handle->audio_codec);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    if (instance_id_encoded_len > 0U) {
        instance_id_encoded = (char *)qm_malloc(instance_id_encoded_len + 1U);
        if (instance_id_encoded == NULL) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        ret = url_encode_copy(instance_id_encoded,
                              instance_id_encoded_len + 1U,
                              auth_view.instance_id);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    if (token_encoded_len > 0U) {
        token_encoded = (char *)qm_malloc(token_encoded_len + 1U);
        if (token_encoded == NULL) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        ret = url_encode_copy(token_encoded, token_encoded_len + 1U, auth_view.token);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    if (access_key_encoded_len > 0U) {
        access_key_encoded = (char *)qm_malloc(access_key_encoded_len + 1U);
        if (access_key_encoded == NULL) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        ret = url_encode_copy(access_key_encoded,
                              access_key_encoded_len + 1U,
                              auth_view.access_key);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    if (secret_key_encoded_len > 0U) {
        secret_key_encoded = (char *)qm_malloc(secret_key_encoded_len + 1U);
        if (secret_key_encoded == NULL) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        ret = url_encode_copy(secret_key_encoded,
                              secret_key_encoded_len + 1U,
                              auth_view.secret_key);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    if (cfg_encoded_len > 0U) {
        cfg_encoded = (char *)qm_malloc(cfg_encoded_len + 1U);
        if (cfg_encoded == NULL) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }

        ret = url_encode_copy(cfg_encoded, cfg_encoded_len + 1U, auth_view.cfg);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    ret = qm_snprintf(uri, (int)(total_len + 1U), "%s", handle->endpoint);
    if (ret < 0) {
        qm_free(cfg_encoded);
        qm_free(uri);
        return -QM_EIO;
    }
    offset = (uint32_t)ret;

    ret = append_query_string(uri, total_len + 1U, &offset, "a", app_id_encoded);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    if (handle->auth_mode == BAIDU_CHAT_AGENT_AUTH_MODE_INSTANCE_TOKEN) {
        ret = append_query_string(uri, total_len + 1U, &offset, "id", instance_id_encoded);
        if (ret != QM_EOK) {
            goto cleanup;
        }
        ret = append_query_string(uri, total_len + 1U, &offset, "t", token_encoded);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    } else {
        ret = append_query_string(uri, total_len + 1U, &offset, "ak", access_key_encoded);
        if (ret != QM_EOK) {
            goto cleanup;
        }
        ret = append_query_string(uri, total_len + 1U, &offset, "sk", secret_key_encoded);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    ret = append_query_string(uri, total_len + 1U, &offset, "ac", audio_codec_encoded);
    if (ret != QM_EOK) {
        goto cleanup;
    }

    if (handle->packet_time_ms > 0U) {
        ret = append_query_u32(uri, total_len + 1U, &offset, "ptime", handle->packet_time_ms);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    if (handle->packet_max_len > 0U) {
        ret = append_query_u32(uri, total_len + 1U, &offset, "plen", handle->packet_max_len);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    if (cfg_encoded != NULL) {
        ret = append_query_string(uri, total_len + 1U, &offset, "cfg", cfg_encoded);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    free_string(&handle->uri);
    handle->uri = uri;
    uri = NULL;
    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_CONTROL,
            "build uri ok endpoint=%s app_id=%s auth_mode=%d ac=%s ptime=%lu plen=%lu cfg=%s",
            present_text(handle->endpoint),
            present_text(handle->app_id),
            handle->auth_mode,
            present_text(handle->audio_codec),
            (unsigned long)handle->packet_time_ms,
            (unsigned long)handle->packet_max_len,
            present_text(auth_view.cfg));
    ret = QM_EOK;

cleanup:
    qm_free(app_id_encoded);
    qm_free(instance_id_encoded);
    qm_free(token_encoded);
    qm_free(access_key_encoded);
    qm_free(secret_key_encoded);
    qm_free(audio_codec_encoded);
    qm_free(cfg_encoded);
    qm_free(uri);
    return ret;
}

static int ensure_ready_to_send(baidu_chat_agent_handle_t handle)
{
    if (handle == NULL) {
        return -QM_EINVAL;
    }

    if (handle->ws == NULL || handle->connected == QM_FALSE) {
        return -QM_EBUSY;
    }

    return QM_EOK;
}

static int send_prefixed_text(baidu_chat_agent_handle_t handle,
                              const char *prefix,
                              const char *text,
                              uint32_t timeout_ms)
{
    char *buffer;
    uint32_t prefix_len;
    uint32_t text_len;
    uint32_t total_len;
    int ret;

    ret = ensure_ready_to_send(handle);
    if (ret != QM_EOK) {
        return ret;
    }

    if (prefix == NULL || text == NULL) {
        return -QM_EINVAL;
    }

    prefix_len = qm_strlen(prefix);
    text_len = qm_strlen(text);
    total_len = prefix_len + text_len;
    buffer = (char *)qm_malloc(total_len + 1U);
    if (buffer == NULL) {
        return -QM_ENOMEM;
    }

    qm_memcpy(buffer, prefix, prefix_len);
    qm_memcpy(buffer + prefix_len, text, text_len);
    buffer[total_len] = '\0';

    log_full_prefixed_text("send text", prefix, text, text_len);

    ret = qm_websocket_client_send_text(handle->ws,
                                        buffer,
                                        (int)total_len,
                                        default_u32(timeout_ms,
                                                    default_u32(handle->config.timeout_ms,
                                                                BAIDU_CHAT_AGENT_DEFAULT_TIMEOUT_MS)));
    qm_free(buffer);
    if (ret < 0) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "send text failed prefix=%s ret=%d",
                prefix != NULL ? prefix : "none",
                ret);
    }
    return ret < 0 ? ret : QM_EOK;
}

static int send_compound_text(baidu_chat_agent_handle_t handle,
                              const char *prefix0,
                              const char *prefix1,
                              const char *text,
                              uint32_t timeout_ms)
{
    char *buffer;
    uint32_t prefix0_len;
    uint32_t prefix1_len;
    uint32_t text_len;
    uint32_t total_len;
    int ret;

    ret = ensure_ready_to_send(handle);
    if (ret != QM_EOK) {
        return ret;
    }

    if (prefix0 == NULL || prefix1 == NULL || text == NULL) {
        return -QM_EINVAL;
    }

    prefix0_len = (uint32_t)qm_strlen(prefix0);
    prefix1_len = (uint32_t)qm_strlen(prefix1);
    text_len = (uint32_t)qm_strlen(text);
    total_len = prefix0_len + prefix1_len + text_len;
    buffer = (char *)qm_malloc(total_len + 1U);
    if (buffer == NULL) {
        return -QM_ENOMEM;
    }

    qm_memcpy(buffer, prefix0, prefix0_len);
    qm_memcpy(buffer + prefix0_len, prefix1, prefix1_len);
    qm_memcpy(buffer + prefix0_len + prefix1_len, text, text_len);
    buffer[total_len] = '\0';

    if (!(string_equals(prefix0, BAIDU_CHAT_AGENT_PREFIX_EVENT) == QM_TRUE &&
          string_equals(prefix1, "[LIC]:[ACTIVE]:") == QM_TRUE)) {
        log_full_prefixed_text("send text", prefix0, text, text_len);
    }

    ret = qm_websocket_client_send_text(handle->ws,
                                        buffer,
                                        (int)total_len,
                                        default_u32(timeout_ms,
                                                    default_u32(handle->config.timeout_ms,
                                                                BAIDU_CHAT_AGENT_DEFAULT_TIMEOUT_MS)));
    qm_free(buffer);
    if (ret < 0) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "send compound text failed prefix=%s%s ret=%d",
                prefix0,
                prefix1,
                ret);
    }
    return ret < 0 ? ret : QM_EOK;
}

baidu_chat_agent_handle_t baidu_chat_agent_init(const baidu_chat_agent_init_config_t *config)
{
    baidu_chat_agent_handle_t handle;

    if (config == NULL || config->event_cb == NULL) {
        return NULL;
    }

    handle = (baidu_chat_agent_handle_t)qm_calloc(1U, sizeof(*handle));
    if (handle == NULL) {
        return NULL;
    }

    handle->config = config->config;
    handle->event_cb = config->event_cb;
    handle->user_ctx = config->user_ctx;
    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE,
            "init timeout=%lu rx=%lu reconnect=%lu ws_stack=%d ws_prio=%d send_queue=%d auto_reconnect=%s ping_disabled=%s",
            (unsigned long)default_u32(handle->config.timeout_ms, CONFIG_BAIDU_CHAT_AGENT_TIMEOUT_MS),
            (unsigned long)default_u32(handle->config.ws_rx_size, CONFIG_BAIDU_CHAT_AGENT_WS_RX_SIZE),
            (unsigned long)default_u32(handle->config.reconnect_interval_ms, CONFIG_BAIDU_CHAT_AGENT_RECONNECT_INTERVAL_MS),
            default_int(handle->config.ws_stack_size, CONFIG_BAIDU_CHAT_AGENT_WS_TASK_STACK_SIZE),
            default_int(handle->config.ws_priority, CONFIG_BAIDU_CHAT_AGENT_WS_TASK_PRIORITY),
            default_int(handle->config.send_queue_size, CONFIG_BAIDU_CHAT_AGENT_SEND_QUEUE_SIZE),
            bool_text(handle->config.disable_auto_reconnect == QM_FALSE),
            bool_text(handle->config.disable_ping));
    return handle;
}

int baidu_chat_agent_start(baidu_chat_agent_handle_t handle,
                           const baidu_chat_agent_start_params_t *params)
{
    qm_websocket_client_config_t ws_cfg;
    int ret;

    if (handle == NULL || params == NULL) {
        return -QM_EINVAL;
    }

    if (handle->started == QM_TRUE) {
        BAIDU_CHAT_AGENT_LOGW(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE, "start rejected: already started");
        return -QM_EBUSY;
    }

    ret = validate_start_params(params);
    if (ret != QM_EOK) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "invalid start params ret=%d", ret);
        return ret;
    }

    ret = configure_runtime_strings(handle, params);
    if (ret != QM_EOK) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "configure runtime failed ret=%d", ret);
        return ret;
    }

    ret = build_runtime_uri(handle);
    if (ret != QM_EOK) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "build runtime uri failed ret=%d", ret);
        return ret;
    }

    /* Build the WebSocket runtime configuration only from QUMI transport knobs. */
    qm_memset(&ws_cfg, 0, sizeof(ws_cfg));
    ws_cfg.uri = handle->uri;
    ws_cfg.headers = handle->headers;
    ws_cfg.network_timeout_ms =
        (int)default_u32(handle->config.timeout_ms, CONFIG_BAIDU_CHAT_AGENT_TIMEOUT_MS);
    ws_cfg.buffer_size =
        (int)default_u32(handle->config.ws_rx_size, CONFIG_BAIDU_CHAT_AGENT_WS_RX_SIZE);
    ws_cfg.task_stack =
        default_int(handle->config.ws_stack_size, CONFIG_BAIDU_CHAT_AGENT_WS_TASK_STACK_SIZE);
    ws_cfg.task_priority =
        default_int(handle->config.ws_priority, CONFIG_BAIDU_CHAT_AGENT_WS_TASK_PRIORITY);
    ws_cfg.send_queue_size =
        default_int(handle->config.send_queue_size, CONFIG_BAIDU_CHAT_AGENT_SEND_QUEUE_SIZE);
    ws_cfg.reconnect_interval_ms =
        (int)default_u32(handle->config.reconnect_interval_ms,
                         CONFIG_BAIDU_CHAT_AGENT_RECONNECT_INTERVAL_MS);
    ws_cfg.disable_auto_reconnect = handle->config.disable_auto_reconnect;
    ws_cfg.disable_ping = handle->config.disable_ping;
    ws_cfg.user_context = handle;

    handle->ws = qm_websocket_client_init(&ws_cfg);
    if (handle->ws == NULL) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "websocket init failed");
        return -QM_EINIT;
    }

    ret = qm_websocket_register_events(handle->ws,
                                       WEBSOCKET_EVENT_MAX,
                                       websocket_event_handler,
                                       handle);
    if (ret != QM_EOK) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "websocket register events failed ret=%d", ret);
        qm_websocket_client_destroy(handle->ws);
        handle->ws = NULL;
        return ret;
    }

    ret = qm_websocket_client_start(handle->ws);
    if (ret != QM_EOK) {
        BAIDU_CHAT_AGENT_LOGE(LOG_TAG, "websocket start failed ret=%d", ret);
        qm_websocket_client_destroy(handle->ws);
        handle->ws = NULL;
        return ret;
    }

    /* The session becomes externally usable after the transport start succeeds. */
    handle->started = QM_TRUE;
    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE,
            "start ok timeout=%d rx=%d reconnect=%d send_queue=%d",
            ws_cfg.network_timeout_ms,
            ws_cfg.buffer_size,
            ws_cfg.reconnect_interval_ms,
            ws_cfg.send_queue_size);
    log_runtime_identity(handle, "start request");
    return QM_EOK;
}

int baidu_chat_agent_stop(baidu_chat_agent_handle_t handle)
{
    int ret = QM_EOK;
    int destroy_ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE,
            "stop request started=%s connected=%s ws=%s",
            bool_text(handle->started),
            bool_text(handle->connected),
            bool_text(handle->ws != NULL));
    /* Stop and destroy the transport together so the handle returns to a clean state. */
    if (handle->ws != NULL) {
        ret = qm_websocket_client_stop(handle->ws);
        if (ret != QM_EOK && ret != -QM_EIO) {
            return ret;
        }
        destroy_ret = qm_websocket_client_destroy(handle->ws);
        if (destroy_ret != QM_EOK) {
            return destroy_ret;
        }
        handle->ws = NULL;
    }

    handle->started = QM_FALSE;
    handle->connected = QM_FALSE;
    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE,
                         "stop done ret=%d", ret == -QM_EIO ? QM_EOK : ret);
    return ret == -QM_EIO ? QM_EOK : ret;
}

int baidu_chat_agent_destroy(baidu_chat_agent_handle_t handle)
{
    int ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }

    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE, "destroy request");
    /* destroy() is the final owner cleanup path and always stops the session first. */
    ret = baidu_chat_agent_stop(handle);
    if (ret != QM_EOK) {
        BAIDU_CHAT_AGENT_LOGW(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE,
                              "destroy deferred: transport still active ret=%d", ret);
        return ret;
    }
    free_string(&handle->uri);
    free_string(&handle->headers);
    free_string(&handle->endpoint);
    free_string(&handle->app_id);
    free_string(&handle->audio_codec);
    free_string(&handle->user_id);
    free_string(&handle->device_id);
    free_string(&handle->license_key);
    clear_runtime_auth_for_mode(handle, handle->auth_mode);
    qm_free(handle);
    BAIDU_CHAT_AGENT_LOGI(LOG_TAG, BAIDU_CHAT_AGENT_LOG_LIFECYCLE, "destroy done");
    return QM_EOK;
}

int baidu_chat_agent_send_audio(baidu_chat_agent_handle_t handle,
                                const uint8_t *data,
                                uint32_t len,
                                uint32_t timeout_ms)
{
    int ret;

    if (handle == NULL || data == NULL || len == 0U || len > 2147483647U) {
        return -QM_EINVAL;
    }
    if (handle->ws == NULL) return -QM_EBUSY;

    /* WebSocket validates connection admission under its own lock. Avoid an
     * unsynchronized read of the callback-owned connected flag here. */
    ret = qm_websocket_client_send_bin(handle->ws,
                                       (const char *)data,
                                       (int)len,
                                       timeout_ms);
    /* Queue pressure is expected on this capture path; do not log per frame. */
    return ret < 0 ? ret : ((uint32_t)ret == len ? QM_EOK : -QM_EIO);
}

int baidu_chat_agent_send_text(baidu_chat_agent_handle_t handle,
                               const char *text,
                               uint32_t timeout_ms)
{
    return send_prefixed_text(handle,
                              BAIDU_CHAT_AGENT_PREFIX_TEXT_TO_AGENT,
                              text,
                              timeout_ms);
}

int baidu_chat_agent_send_text_to_tts(baidu_chat_agent_handle_t handle,
                                      const char *text,
                                      uint32_t timeout_ms)
{
    return send_prefixed_text(handle,
                              BAIDU_CHAT_AGENT_PREFIX_TEXT_TO_TTS,
                              text,
                              timeout_ms);
}

int baidu_chat_agent_send_event(baidu_chat_agent_handle_t handle,
                                const char *event,
                                uint32_t timeout_ms)
{
    return send_prefixed_text(handle,
                              BAIDU_CHAT_AGENT_PREFIX_EVENT,
                              event,
                              timeout_ms);
}

int baidu_chat_agent_send_asr_start_longtext_rec(baidu_chat_agent_handle_t handle,
                                                 uint32_t timeout_ms)
{
    return baidu_chat_agent_send_event(handle,
                                       BAIDU_CHAT_AGENT_EVENT_ASR_START_LONGTEXT_REC,
                                       timeout_ms);
}

int baidu_chat_agent_send_asr_stop_longtext_rec(baidu_chat_agent_handle_t handle,
                                                uint32_t timeout_ms)
{
    return baidu_chat_agent_send_event(handle,
                                       BAIDU_CHAT_AGENT_EVENT_ASR_STOP_LONGTEXT_REC,
                                       timeout_ms);
}

int baidu_chat_agent_set_asr_realtime_enabled(baidu_chat_agent_handle_t handle,
                                              qm_bool enabled,
                                              uint32_t timeout_ms)
{
    return baidu_chat_agent_send_event(handle,
                                       enabled == QM_TRUE ?
                                       BAIDU_CHAT_AGENT_EVENT_ASR_ENABLE_REALTIME :
                                       BAIDU_CHAT_AGENT_EVENT_ASR_DISABLE_REALTIME,
                                       timeout_ms);
}

int baidu_chat_agent_send_remote_player_control(baidu_chat_agent_handle_t handle,
                                                baidu_chat_agent_remote_player_op_t op,
                                                uint32_t timeout_ms)
{
    switch (op) {
        case BAIDU_CHAT_AGENT_REMOTE_PLAYER_STOP:
            return baidu_chat_agent_send_event(handle,
                                               BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_STOP,
                                               timeout_ms);
        case BAIDU_CHAT_AGENT_REMOTE_PLAYER_PAUSE:
            return baidu_chat_agent_send_event(handle,
                                               BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_PAUSE,
                                               timeout_ms);
        case BAIDU_CHAT_AGENT_REMOTE_PLAYER_RESUME:
            return baidu_chat_agent_send_event(handle,
                                               BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_RESUME,
                                               timeout_ms);
        default:
            return -QM_EINVAL;
    }
}

int baidu_chat_agent_send_break(baidu_chat_agent_handle_t handle,
                                const char *reason,
                                uint32_t timeout_ms)
{
    return send_prefixed_text(handle,
                              BAIDU_CHAT_AGENT_PREFIX_BREAK,
                              reason != NULL ? reason : "",
                              timeout_ms);
}
