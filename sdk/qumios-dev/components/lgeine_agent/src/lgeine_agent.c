#include "lgeine_agent.h"

#include "cJSON.h"
#include "qm_kernel.h"
#include "qm_sprintf.h"
#include "qm_string.h"
#include "qm_websocket_client.h"

/*
 * lgeine_agent core implementation.
 *
 * This file implements the WebSocket transport task, the lgeine protocol state
 * machine (hello handshake -> ready -> session messaging), and event dispatch
 * to the user callback. See lgeine_agent.h for the public API and event model.
 */
#define LOG_TAG "lgeine_agent"
#define LGEINE_WS_OPCODE_TEXT 1
#define LGEINE_WS_OPCODE_BINARY 2
#define LGEINE_WS_CLOSE_NORMAL 1000
#define LGEINE_WS_CLOSE_POLICY 1008
#define LGEINE_TEXT_INITIAL_CAPACITY 256U

#define LGEINE_AGENT_DEV_ENDPOINT \
    "wss://voice-dev.ljyd.cn/agent-voicechat/ljyd/v1"
#define LGEINE_AGENT_TEST_ENDPOINT \
    "wss://voice-test.ljyd.cn/agent-voicechat/ljyd/v1"
#define LGEINE_AGENT_PROD_ENDPOINT \
    "wss://voice.ljyd.cn/agent-voicechat/ljyd/v1"

#ifndef CONFIG_LGEINE_AGENT_WS_RX_SIZE
#define CONFIG_LGEINE_AGENT_WS_RX_SIZE 4096
#endif
#ifndef CONFIG_LGEINE_AGENT_MAX_TEXT_MESSAGE_SIZE
#define CONFIG_LGEINE_AGENT_MAX_TEXT_MESSAGE_SIZE 16384
#endif
#ifndef CONFIG_LGEINE_AGENT_NETWORK_TIMEOUT_MS
#define CONFIG_LGEINE_AGENT_NETWORK_TIMEOUT_MS 10000
#endif
#ifndef CONFIG_LGEINE_AGENT_HELLO_TIMEOUT_MS
#define CONFIG_LGEINE_AGENT_HELLO_TIMEOUT_MS LGEINE_AGENT_HELLO_TIMEOUT_MS
#endif
#ifndef CONFIG_LGEINE_AGENT_RECONNECT_INTERVAL_MS
#define CONFIG_LGEINE_AGENT_RECONNECT_INTERVAL_MS 1000
#endif
#ifndef CONFIG_LGEINE_AGENT_WS_TASK_STACK_SIZE
#define CONFIG_LGEINE_AGENT_WS_TASK_STACK_SIZE 8192
#endif
#ifndef CONFIG_LGEINE_AGENT_WS_TASK_PRIORITY
#define CONFIG_LGEINE_AGENT_WS_TASK_PRIORITY 5
#endif
#ifndef CONFIG_LGEINE_AGENT_SEND_QUEUE_SIZE
#define CONFIG_LGEINE_AGENT_SEND_QUEUE_SIZE 8
#endif

struct lgeine_agent {
    lgeine_agent_config_t config;
    lgeine_agent_event_cb_t event_cb;
    void *user_ctx;
    void *ws;
    qm_mutex_t lock;
    qm_timer_t hello_timer;
    qm_timer_t close_timer;
    qm_bool lock_valid;
    qm_bool timer_valid;
    qm_bool close_timer_valid;
    qm_bool connected;
    qm_bool ready;
    qm_bool hello_failed;
    qm_bool starting;
    qm_bool destroying;
    qm_bool tts_active;
    qm_bool tts_sentence_active;
    qm_bool listen_active;    /* listen interaction in flight; gates downlink stt */
    qm_bool listen_recording; /* recording active after listen.start; gates uplink audio */
    int deferred_close_code;
    lgeine_agent_audio_info_t requested_audio;
    lgeine_agent_audio_info_t negotiated_audio;
    char *endpoint_uri;
    char *session_id;
    uint8_t *binary_buffer;
    uint32_t binary_len;
    uint32_t binary_capacity;
    uint32_t binary_payload_len;
    qm_bool binary_discarding;
    char *text_buffer;
    uint32_t text_len;
    uint32_t text_capacity;
    qm_bool text_discarding;
};

static void request_close_on_session_end(lgeine_agent_handle_t handle);
static void request_reconnect_or_close(lgeine_agent_handle_t handle, int close_code);
static void reset_binary_message(lgeine_agent_handle_t handle);
static qm_bool valid_pcm_frame_len(const lgeine_agent_audio_info_t *audio,
                                   uint32_t len);

static volatile uint32_t g_lgeine_agent_log_flags = LGEINE_AGENT_KCONFIG_LOG_FLAGS;

uint32_t lgeine_agent_get_log_flags(void)
{
    return g_lgeine_agent_log_flags;
}

int lgeine_agent_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)LGEINE_AGENT_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_lgeine_agent_log_flags = log_flags;
    return QM_EOK;
}

/*
 * Per-handle config resolvers: each returns the configured value when non-zero
 * and the Kconfig default otherwise, giving the state machine a single source
 * of truth for runtime timing and buffer parameters.
 */
static uint32_t network_timeout_ms(lgeine_agent_handle_t handle)
{
    return handle->config.network_timeout_ms ? handle->config.network_timeout_ms :
                                               CONFIG_LGEINE_AGENT_NETWORK_TIMEOUT_MS;
}

static uint32_t hello_timeout_ms(lgeine_agent_handle_t handle)
{
    return handle->config.hello_timeout_ms ? handle->config.hello_timeout_ms :
                                             CONFIG_LGEINE_AGENT_HELLO_TIMEOUT_MS;
}

static uint32_t reconnect_interval_ms(lgeine_agent_handle_t handle)
{
    return handle->config.reconnect_interval_ms ? handle->config.reconnect_interval_ms :
                                                  CONFIG_LGEINE_AGENT_RECONNECT_INTERVAL_MS;
}

static uint32_t ws_rx_size(lgeine_agent_handle_t handle)
{
    return handle->config.ws_rx_size ? handle->config.ws_rx_size :
                                       CONFIG_LGEINE_AGENT_WS_RX_SIZE;
}

static uint32_t max_text_size(lgeine_agent_handle_t handle)
{
    return handle->config.max_text_message_size ? handle->config.max_text_message_size :
                                                  CONFIG_LGEINE_AGENT_MAX_TEXT_MESSAGE_SIZE;
}

static int ws_stack_size(lgeine_agent_handle_t handle)
{
    return handle->config.ws_stack_size ? handle->config.ws_stack_size :
                                          CONFIG_LGEINE_AGENT_WS_TASK_STACK_SIZE;
}

static int ws_priority(lgeine_agent_handle_t handle)
{
    return handle->config.ws_priority ? handle->config.ws_priority :
                                        CONFIG_LGEINE_AGENT_WS_TASK_PRIORITY;
}

static int send_queue_size(lgeine_agent_handle_t handle)
{
    return handle->config.send_queue_size ? handle->config.send_queue_size :
                                            CONFIG_LGEINE_AGENT_SEND_QUEUE_SIZE;
}

/*
 * Forward one event to the user callback, guarding against a NULL handle,
 * callback, or event.
 */
static void emit_event(lgeine_agent_handle_t handle, const lgeine_agent_event_t *event)
{
    if (handle && handle->event_cb && event) {
        handle->event_cb(handle, event, handle->user_ctx);
    }
}

/*
 * Build and emit an LGEINE_AGENT_EVENT_ERROR event carrying the error type,
 * numeric code, human-readable message, and optional raw JSON payload.
 */
static void emit_error(lgeine_agent_handle_t handle,
                       lgeine_agent_error_type_t type,
                       int code,
                       const char *message,
                       const char *raw_json)
{
    lgeine_agent_event_t event;

    if (!handle) {
        return;
    }
    qm_memset(&event, 0, sizeof(event));
    event.id = LGEINE_AGENT_EVENT_ERROR;
    event.raw_json = raw_json;
    event.data.error.type = type;
    event.data.error.code = code;
    event.data.error.message = message;
    LGEINE_AGENT_LOGE(LOG_TAG, "%s code=%d", message ? message : "error", code);
    emit_event(handle, &event);
}

/*
 * Validate an audio_info: the format must be Opus or PCM and rate/bits/
 * frameDuration must be non-zero with a supported channel count.
 */
static qm_bool valid_audio_info(const lgeine_agent_audio_info_t *audio)
{
    return audio &&
           (audio->format == LGEINE_AGENT_AUDIO_FORMAT_OPUS ||
            audio->format == LGEINE_AGENT_AUDIO_FORMAT_PCM) &&
           audio->rate != 0U && audio->bits != 0U &&
           (audio->channel == 1U || audio->channel == 2U) &&
           audio->frame_duration_ms != 0U;
}

static qm_bool valid_header_identity(const char *value)
{
    size_t i;

    if (!value || value[0] == '\0' || qm_strlen(value) > 64U) {
        return QM_FALSE;
    }
    for (i = 0U; value[i] != '\0'; ++i) {
        if ((unsigned char)value[i] > 0x7fU || value[i] == '\r' || value[i] == '\n') {
            return QM_FALSE;
        }
    }
    return QM_TRUE;
}

static qm_bool valid_uuid4(const char *id)
{
    size_t i;
    if (!id || qm_strlen(id) != 36U || id[8] != '-' || id[13] != '-' ||
        id[18] != '-' || id[23] != '-' || id[14] != '4' ||
        (id[19] != '8' && id[19] != '9' && id[19] != 'a' && id[19] != 'b' &&
         id[19] != 'A' && id[19] != 'B')) {
        return QM_FALSE;
    }
    for (i = 0U; i < 36U; ++i) {
        if (i == 8U || i == 13U || i == 18U || i == 23U) continue;
        if (!((id[i] >= '0' && id[i] <= '9') ||
              (id[i] >= 'a' && id[i] <= 'f') ||
              (id[i] >= 'A' && id[i] <= 'F'))) {
            return QM_FALSE;
        }
    }
    return QM_TRUE;
}

static qm_bool valid_wss_uri(const char *uri)
{
    return uri && qm_strlen(uri) > 6U &&
           qm_strncmp(uri, "wss://", 6U) == 0;
}

/*
 * Map an environment to its built-in default endpoint. Returns NULL for an
 * unknown environment value. The returned pointer is a static literal and is
 * not owned by the caller.
 */
static const char *env_endpoint(lgeine_agent_env_t env)
{
    switch (env) {
        case LGEINE_AGENT_ENV_DEV: return LGEINE_AGENT_DEV_ENDPOINT;
        case LGEINE_AGENT_ENV_TEST: return LGEINE_AGENT_TEST_ENDPOINT;
        case LGEINE_AGENT_ENV_PROD:
        case LGEINE_AGENT_ENV_DEFAULT: return LGEINE_AGENT_PROD_ENDPOINT;
        default: return NULL;
    }
}

/*
 * Resolve the effective endpoint: use start_params.uri when provided, otherwise
 * fall back to the built-in endpoint of start_params.env. The returned string
 * is heap-allocated and owned by the caller.
 */
static int resolve_endpoint(const lgeine_agent_start_params_t *params,
                            char **endpoint)
{
    const char *default_uri;
    char *uri;

    if (params->uri) {
        if (!valid_wss_uri(params->uri)) {
            return -QM_EINVAL;
        }
        uri = qm_strdup(params->uri);
        if (!uri) {
            return -QM_ENOMEM;
        }
        *endpoint = uri;
        return QM_EOK;
    }
    default_uri = env_endpoint(params->env);
    if (!default_uri) {
        return -QM_EINVAL;
    }
    uri = qm_strdup(default_uri);
    if (!uri) {
        return -QM_ENOMEM;
    }
    *endpoint = uri;
    return QM_EOK;
}

static qm_bool ready_locked(lgeine_agent_handle_t handle)
{
    return handle->ready && handle->ws && !handle->destroying;
}

static const char *audio_format_name(lgeine_agent_audio_format_t format)
{
    switch (format) {
        case LGEINE_AGENT_AUDIO_FORMAT_OPUS: return "opus";
        case LGEINE_AGENT_AUDIO_FORMAT_PCM: return "pcm";
        default: return NULL;
    }
}

static const char *listen_state_name(lgeine_agent_listen_state_t state)
{
    switch (state) {
        case LGEINE_AGENT_LISTEN_STATE_START: return "start";
        case LGEINE_AGENT_LISTEN_STATE_STOP: return "stop";
        case LGEINE_AGENT_LISTEN_STATE_DETECT: return "detect";
        default: return NULL;
    }
}

static const char *listen_mode_name(lgeine_agent_listen_mode_t mode)
{
    switch (mode) {
        case LGEINE_AGENT_LISTEN_MODE_AUTO: return "auto";
        case LGEINE_AGENT_LISTEN_MODE_MANUAL: return "manual";
        case LGEINE_AGENT_LISTEN_MODE_REALTIME: return "realtime";
        default: return NULL;
    }
}

static const char *abort_reason_name(lgeine_agent_abort_reason_t reason)
{
    switch (reason) {
        case LGEINE_AGENT_ABORT_REASON_USER_STOP: return "user_stop";
        case LGEINE_AGENT_ABORT_REASON_WAKEUP: return "wakeup";
        default: return NULL;
    }
}

/*
 * Serialize and send an owned cJSON object as a WebSocket text frame. The root
 * is consumed (deleted) regardless of outcome. Returns QM_EOK when the complete
 * frame was written, or a negative error code otherwise.
 */
static int send_owned_root(lgeine_agent_handle_t handle,
                           cJSON *root,
                           uint32_t timeout_ms)
{
    char *json;
    uint32_t len;
    int ret;

    if (!handle || !root || !handle->ws) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) {
        return -QM_ENOMEM;
    }
    len = qm_strlen(json);
    LGEINE_AGENT_LOGI(LOG_TAG, LGEINE_AGENT_LOG_UPLINK,
                      "send text len=%lu", (unsigned long)len);
    ret = qm_websocket_client_send_text(handle->ws, json, (int)len,
                                         timeout_ms ? timeout_ms : network_timeout_ms(handle));
    cJSON_free(json);
    if (ret < QM_EOK) {
        return ret;
    }
    return (uint32_t)ret == len ? QM_EOK : -QM_EIO;
}

/*
 * Enforce the sessionId rule for non-hello uplink messages: an existing
 * sessionId must match the ready session, otherwise the current sessionId is
 * injected. Caller must hold handle->lock.
 */
static int add_session_id_locked(lgeine_agent_handle_t handle, cJSON *root)
{
    cJSON *id;

    if (!ready_locked(handle) || !handle->session_id) {
        return -QM_EBUSY;
    }
    id = cJSON_GetObjectItemCaseSensitive(root, "sessionId");
    if (id) {
        if (!cJSON_IsString(id) || !id->valuestring || id->valuestring[0] == '\0') {
            return -QM_EINVAL;
        }
        return qm_strcmp(id->valuestring, handle->session_id) == 0 ?
                   QM_EOK : -QM_EINVAL;
    }
    return cJSON_AddStringToObject(root, "sessionId", handle->session_id) ?
               QM_EOK : -QM_ENOMEM;
}

/*
 * Mutex-protected wrapper that validates/injects the sessionId before sending
 * an owned root. Used by all post-handshake uplink senders.
 */
static int send_session_root(lgeine_agent_handle_t handle,
                             cJSON *root,
                             uint32_t timeout_ms)
{
    int ret;

    if (!handle || !root) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    ret = add_session_id_locked(handle, root);
    if (ret == QM_EOK) {
        ret = send_owned_root(handle, root, timeout_ms);
        root = NULL;
    }
    qm_mutex_unlock(&handle->lock);
    cJSON_Delete(root);
    return ret;
}

/*
 * Allocate a JSON object pre-populated with only the "method" field.
 */
static cJSON *make_method(const char *method)
{
    cJSON *root = cJSON_CreateObject();

    if (!root) {
        return NULL;
    }
    if (!cJSON_AddStringToObject(root, "method", method)) {
        cJSON_Delete(root);
        return NULL;
    }
    return root;
}

/*
 * Build and send the `hello` message carrying the agent's requested uplink
 * audio_info (format, rate, bits, channel, frameDuration).
 */
static int send_hello(lgeine_agent_handle_t handle)
{
    const lgeine_agent_audio_info_t *audio = &handle->requested_audio;
    const char *format_name = audio_format_name(audio->format);
    cJSON *root = make_method("hello");
    cJSON *audio_info = cJSON_CreateObject();

    if (!root || !audio_info || !format_name) {
        cJSON_Delete(root);
        cJSON_Delete(audio_info);
        return -QM_ENOMEM;
    }
    cJSON_AddItemToObject(root, "audioInfo", audio_info);
    if (!cJSON_AddStringToObject(audio_info, "format", format_name) ||
        !cJSON_AddNumberToObject(audio_info, "rate", audio->rate) ||
        !cJSON_AddNumberToObject(audio_info, "bits", audio->bits) ||
        !cJSON_AddNumberToObject(audio_info, "channel", audio->channel) ||
        !cJSON_AddNumberToObject(audio_info, "frameDuration", audio->frame_duration_ms)) {
        cJSON_Delete(root);
        return -QM_ENOMEM;
    }
    return send_owned_root(handle, root, network_timeout_ms(handle));
}

/*
 * Small cJSON accessor helpers used by the downlink parsers. They centralize
 * type checking so parse sites stay concise and consistent.
 */
static cJSON *json_item(cJSON *object, const char *name)
{
    return object ? cJSON_GetObjectItemCaseSensitive(object, name) : NULL;
}

static const char *json_string(cJSON *object, const char *name)
{
    cJSON *item = json_item(object, name);

    return cJSON_IsString(item) && item->valuestring ? item->valuestring : NULL;
}

static qm_bool json_bool(cJSON *object, const char *name, qm_bool *value)
{
    cJSON *item = json_item(object, name);

    if (!cJSON_IsBool(item) || !value) {
        return QM_FALSE;
    }
    *value = item->valueint ? QM_TRUE : QM_FALSE;
    return QM_TRUE;
}

static qm_bool json_int(cJSON *object, const char *name,
                        int min_value, int max_value, int *value)
{
    cJSON *item = json_item(object, name);
    double number;
    int parsed;

    if (!cJSON_IsNumber(item) || !value) {
        return QM_FALSE;
    }
    number = item->valuedouble;
    if (number < (double)min_value || number > (double)max_value) {
        return QM_FALSE;
    }
    parsed = (int)number;
    if ((double)parsed != number) {
        return QM_FALSE;
    }
    *value = parsed;
    return QM_TRUE;
}

static qm_bool json_u32(cJSON *object, const char *name, uint32_t *value)
{
    cJSON *item = json_item(object, name);
    double number;
    uint32_t parsed;

    if (!cJSON_IsNumber(item) || !value) {
        return QM_FALSE;
    }
    number = item->valuedouble;
    if (number < 0.0 || number > 4294967295.0) {
        return QM_FALSE;
    }
    parsed = (uint32_t)number;
    if ((double)parsed != number) {
        return QM_FALSE;
    }
    *value = parsed;
    return QM_TRUE;
}

static qm_bool parse_audio_format(const char *name, lgeine_agent_audio_format_t *format)
{
    if (!name || !format) {
        return QM_FALSE;
    }
    if (qm_strcmp(name, "opus") == 0) {
        *format = LGEINE_AGENT_AUDIO_FORMAT_OPUS;
        return QM_TRUE;
    }
    if (qm_strcmp(name, "pcm") == 0) {
        *format = LGEINE_AGENT_AUDIO_FORMAT_PCM;
        return QM_TRUE;
    }
    return QM_FALSE;
}

/*
 * Parse a server-provided audio_info object into the given struct, validating
 * the format name, non-zero rate/bits, channel, and frameDuration.
 */
static qm_bool parse_server_audio(cJSON *object, lgeine_agent_audio_info_t *audio)
{
    lgeine_agent_audio_format_t format;

    if (!cJSON_IsObject(object) || !audio ||
        !parse_audio_format(json_string(object, "format"), &format) ||
        !json_u32(object, "rate", &audio->rate) || audio->rate == 0U ||
        !json_u32(object, "bits", &audio->bits) || audio->bits == 0U ||
        !json_u32(object, "channel", &audio->channel) ||
        (audio->channel != 1U && audio->channel != 2U) ||
        !json_u32(object, "frameDuration", &audio->frame_duration_ms) ||
        audio->frame_duration_ms == 0U) {
        return QM_FALSE;
    }
    audio->format = format;
    return QM_TRUE;
}

/*
 * Return the ready flag under lock. `ready` becomes true only after a successful
 * hello handshake and is reset on disconnect/stop.
 */
static qm_bool protocol_ready(lgeine_agent_handle_t handle)
{
    qm_bool ready;

    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    ready = handle->ready;
    qm_mutex_unlock(&handle->lock);
    return ready;
}

/*
 * Handle a non-zero helloReply code: mark the handshake failed, then reconnect
 * or close according to the configured transport policy.
 */
static void reject_hello_reply(lgeine_agent_handle_t handle,
                               int code,
                               const char *message,
                               const char *raw_json)
{
    /* The handshake is over; disarm the hello timer so a stale timeout cannot
     * fire while the transport reconnects (and before DISCONNECTED stops it). */
    (void)qm_timer_stop(&handle->hello_timer);
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (!handle->destroying && handle->connected) {
        handle->hello_failed = QM_TRUE;
    }
    qm_mutex_unlock(&handle->lock);

    emit_error(handle, LGEINE_AGENT_ERROR_REJECTED, code,
               message ? message : "helloReply rejected", raw_json);
    request_reconnect_or_close(handle, LGEINE_WS_CLOSE_POLICY);
}

/*
 * Store the sessionId and server audio_info from a successful helloReply,
 * transition the session to ready, stop the hello timer, and emit READY.
 */
static int accept_hello_reply(lgeine_agent_handle_t handle,
                              cJSON *root,
                              const char *raw_json)
{
    lgeine_agent_event_t event;
    cJSON *audio_object;
    const char *session_id = json_string(root, "sessionId");
    char *id_copy;

    if (!valid_uuid4(session_id)) {
        emit_error(handle, LGEINE_AGENT_ERROR_PROTOCOL, -QM_EINVAL,
                   "helloReply missing sessionId", raw_json);
        return -QM_EINVAL;
    }
    audio_object = json_item(root, "audioInfo");
    if (!audio_object) {
        emit_error(handle, LGEINE_AGENT_ERROR_PROTOCOL, -QM_EINVAL,
                   "helloReply missing audioInfo", raw_json);
        return -QM_EINVAL;
    }
    id_copy = qm_strdup(session_id);
    if (!id_copy) {
        emit_error(handle, LGEINE_AGENT_ERROR_MEMORY, -QM_ENOMEM,
                   "helloReply sessionId allocation failed", raw_json);
        return -QM_ENOMEM;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = LGEINE_AGENT_EVENT_READY;
    event.raw_json = raw_json;
    if (!parse_server_audio(audio_object, &event.data.ready.audio_info)) {
        qm_free(id_copy);
        emit_error(handle, LGEINE_AGENT_ERROR_PROTOCOL, -QM_EINVAL,
                   "helloReply audioInfo is invalid", raw_json);
        return -QM_EINVAL;
    }

    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (handle->destroying || !handle->connected || handle->hello_failed) {
        qm_mutex_unlock(&handle->lock);
        qm_free(id_copy);
        return -QM_EBUSY;
    }
    qm_free(handle->session_id);
    handle->session_id = id_copy;
    handle->negotiated_audio = event.data.ready.audio_info;
    handle->ready = QM_TRUE;
    event.session_id = handle->session_id;
    qm_mutex_unlock(&handle->lock);
    (void)qm_timer_stop(&handle->hello_timer);

    LGEINE_AGENT_LOGI(LOG_TAG, LGEINE_AGENT_LOG_LIFECYCLE,
                      "hello handshake ready");
    emit_event(handle, &event);
    return QM_EOK;
}

/*
 * Dispatch a helloReply: validate `code`, then route to reject (code != 0) or
 * accept (code == 0).
 */
static int handle_hello_reply(lgeine_agent_handle_t handle,
                              cJSON *root,
                              const char *raw_json)
{
    int code;

    if (!json_int(root, "code", 0, 2147483647, &code)) {
        emit_error(handle, LGEINE_AGENT_ERROR_PROTOCOL, -QM_EINVAL,
                   "helloReply missing code", raw_json);
        return -QM_EINVAL;
    }
    if (code != 0) {
        reject_hello_reply(handle, code, json_string(root, "message"), raw_json);
        return -QM_EINVAL;
    }
    return accept_hello_reply(handle, root, raw_json);
}

static lgeine_agent_tts_type_t classify_tts_type(const char *type)
{
    if (!type) return LGEINE_AGENT_TTS_TYPE_UNKNOWN;
    if (qm_strcmp(type, "chat") == 0) return LGEINE_AGENT_TTS_TYPE_CHAT;
    if (qm_strcmp(type, "welcome") == 0) return LGEINE_AGENT_TTS_TYPE_WELCOME;
    if (qm_strcmp(type, "goodbye") == 0) return LGEINE_AGENT_TTS_TYPE_GOODBYE;
    if (qm_strcmp(type, "music") == 0) return LGEINE_AGENT_TTS_TYPE_MUSIC;
    return LGEINE_AGENT_TTS_TYPE_UNKNOWN;
}

static qm_bool valid_tts_type(const char *type)
{
    return classify_tts_type(type) != LGEINE_AGENT_TTS_TYPE_UNKNOWN;
}

static qm_bool parse_tts_state(const char *name, lgeine_agent_tts_state_t *state)
{
    if (!name || !state) return QM_FALSE;
    if (qm_strcmp(name, "start") == 0) { *state = LGEINE_AGENT_TTS_STATE_START; return QM_TRUE; }
    if (qm_strcmp(name, "sentence_start") == 0) { *state = LGEINE_AGENT_TTS_STATE_SENTENCE_START; return QM_TRUE; }
    if (qm_strcmp(name, "sentence_end") == 0) { *state = LGEINE_AGENT_TTS_STATE_SENTENCE_END; return QM_TRUE; }
    if (qm_strcmp(name, "stop") == 0) { *state = LGEINE_AGENT_TTS_STATE_STOP; return QM_TRUE; }
    return QM_FALSE;
}

static qm_bool parse_server_abort_reason(const char *name,
                                         lgeine_agent_server_abort_reason_t *reason)
{
    if (!name || !reason) return QM_FALSE;
    if (qm_strcmp(name, "server_policy") == 0) {
        *reason = LGEINE_AGENT_SERVER_ABORT_SERVER_POLICY;
        return QM_TRUE;
    }
    if (qm_strcmp(name, "session_end") == 0) {
        *reason = LGEINE_AGENT_SERVER_ABORT_SESSION_END;
        return QM_TRUE;
    }
    return QM_FALSE;
}

static void request_reconnect_or_close(lgeine_agent_handle_t handle, int close_code)
{
    void *ws = NULL;
    qm_bool reconnect = QM_FALSE;

    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (!handle->destroying && handle->connected && handle->ws) {
        ws = handle->ws;
        reconnect = !handle->config.disable_auto_reconnect;
    }
    qm_mutex_unlock(&handle->lock);

    if (!ws) {
        return;
    }
    if (reconnect) {
        (void)qm_websocket_client_reconnect(ws);
    } else {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->deferred_close_code = close_code;
        qm_mutex_unlock(&handle->lock);
        request_close_on_session_end(handle);
    }
}

/*
 * Dispatch one parsed server message. After a successful handshake, invalid
 * business messages are ignored without emitting an error and without closing
 * the WebSocket, in line with the protocol's post-handshake rules.
 */
static int dispatch_server_message(lgeine_agent_handle_t handle,
                                   char *json,
                                   uint32_t len)
{
    lgeine_agent_event_t event;
    lgeine_agent_tts_state_t tts_state;
    lgeine_agent_server_abort_reason_t abort_reason;
    cJSON *root;
    const char *method;
    const char *session_id;
    const char *text;
    const char *type_name;
    qm_bool is_final;
    qm_bool session_matches;
    qm_bool session_end;

    root = cJSON_ParseWithLength(json, len);
    if (!root || !cJSON_IsObject(root)) {
        cJSON_Delete(root);
        if (!protocol_ready(handle)) {
            emit_error(handle, LGEINE_AGENT_ERROR_PROTOCOL, -QM_EINVAL,
                       "invalid JSON object", json);
        }
        return -QM_EINVAL;
    }
    method = json_string(root, "method");
    if (!method || method[0] == '\0') {
        if (!protocol_ready(handle)) {
            emit_error(handle, LGEINE_AGENT_ERROR_PROTOCOL, -QM_EINVAL,
                       "message missing method", json);
        }
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    LGEINE_AGENT_LOGI(LOG_TAG, LGEINE_AGENT_LOG_DOWNLINK,
                      "recv method=%s len=%lu", method, (unsigned long)len);

    if (qm_strcmp(method, "helloReply") == 0) {
        if (protocol_ready(handle)) {
            LGEINE_AGENT_LOGD(LOG_TAG, LGEINE_AGENT_LOG_DOWNLINK,
                              "ignore duplicate helloReply");
            cJSON_Delete(root);
            return QM_EOK;
        }
        int ret = handle_hello_reply(handle, root, json);
        cJSON_Delete(root);
        return ret;
    }
    if (!protocol_ready(handle)) {
        emit_error(handle, LGEINE_AGENT_ERROR_STATE, -QM_EBUSY,
                   "message received before helloReply", json);
        cJSON_Delete(root);
        return -QM_EBUSY;
    }

    session_id = json_string(root, "sessionId");
    if (!session_id || session_id[0] == '\0') {
        LGEINE_AGENT_LOGD(LOG_TAG, LGEINE_AGENT_LOG_DOWNLINK,
                          "ignore message without sessionId");
        cJSON_Delete(root);
        return QM_EOK;
    }
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    session_matches = handle->session_id &&
                      qm_strcmp(session_id, handle->session_id) == 0;
    qm_mutex_unlock(&handle->lock);
    if (!session_matches) {
        /* Stale frame from a previous interaction or session; drop it. */
        LGEINE_AGENT_LOGD(LOG_TAG, LGEINE_AGENT_LOG_DOWNLINK,
                          "ignore message with stale sessionId");
        cJSON_Delete(root);
        return QM_EOK;
    }

    qm_memset(&event, 0, sizeof(event));
    event.raw_json = json;
    event.session_id = session_id;
    session_end = QM_FALSE;

    if (qm_strcmp(method, "stt") == 0) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        session_matches = session_matches && handle->listen_active;
        qm_mutex_unlock(&handle->lock);
        if (!session_matches) goto ignore;
        text = json_string(root, "text");
        if (!text || !json_bool(root, "isFinal", &is_final)) goto ignore;
        event.id = LGEINE_AGENT_EVENT_STT;
        event.data.stt.text = text;
        event.data.stt.is_final = is_final;
        if (is_final) {
            qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
            handle->listen_active = QM_FALSE;
            qm_mutex_unlock(&handle->lock);
        }
    } else if (qm_strcmp(method, "llm") == 0) {
        text = json_string(root, "emotion");
        if (!text) goto ignore;
        event.id = LGEINE_AGENT_EVENT_LLM;
        event.data.llm.emotion = text;
    } else if (qm_strcmp(method, "tts") == 0) {
        const char *state_name = json_string(root, "state");
        type_name = json_string(root, "type");
        if (!parse_tts_state(state_name, &tts_state) || !type_name ||
            !valid_tts_type(type_name)) goto ignore;
        event.id = LGEINE_AGENT_EVENT_TTS;
        event.data.tts.state = tts_state;
        event.data.tts.type_name = type_name;
        event.data.tts.type = classify_tts_type(type_name);
        event.data.tts.text = json_string(root, "text");
        if (tts_state == LGEINE_AGENT_TTS_STATE_SENTENCE_START &&
            !event.data.tts.text) goto ignore;
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        if (tts_state == LGEINE_AGENT_TTS_STATE_START) {
            if (handle->tts_active || handle->tts_sentence_active) {
                qm_mutex_unlock(&handle->lock);
                goto ignore;
            }
            handle->tts_active = QM_TRUE;
        } else if (tts_state == LGEINE_AGENT_TTS_STATE_SENTENCE_START) {
            if (!handle->tts_active || handle->tts_sentence_active) {
                qm_mutex_unlock(&handle->lock);
                goto ignore;
            }
            handle->tts_sentence_active = QM_TRUE;
        } else if (tts_state == LGEINE_AGENT_TTS_STATE_SENTENCE_END) {
            if (!handle->tts_active || !handle->tts_sentence_active) {
                qm_mutex_unlock(&handle->lock);
                goto ignore;
            }
            handle->tts_sentence_active = QM_FALSE;
        } else if (tts_state == LGEINE_AGENT_TTS_STATE_STOP) {
            if (!handle->tts_active || handle->tts_sentence_active) {
                qm_mutex_unlock(&handle->lock);
                goto ignore;
            }
            handle->tts_active = QM_FALSE;
        }
        qm_mutex_unlock(&handle->lock);
    } else if (qm_strcmp(method, "abort") == 0) {
        if (!parse_server_abort_reason(json_string(root, "reason"), &abort_reason)) {
            goto ignore;
        }
        event.id = LGEINE_AGENT_EVENT_ABORT;
        event.data.abort.reason = abort_reason;
        session_end = (abort_reason == LGEINE_AGENT_SERVER_ABORT_SESSION_END);
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->tts_active = QM_FALSE;
        handle->tts_sentence_active = QM_FALSE;
        handle->listen_active = QM_FALSE;
        handle->listen_recording = QM_FALSE;
        qm_mutex_unlock(&handle->lock);
    } else {
        goto ignore;
    }

    emit_event(handle, &event);
    if (session_end) {
        request_close_on_session_end(handle);
    }
    cJSON_Delete(root);
    return QM_EOK;

ignore:
    LGEINE_AGENT_LOGD(LOG_TAG, LGEINE_AGENT_LOG_DOWNLINK,
                      "ignore invalid %s message", method);
    cJSON_Delete(root);
    return QM_EOK;
}

static void reset_text_message(lgeine_agent_handle_t handle)
{
    handle->text_len = 0U;
    handle->text_discarding = QM_FALSE;
    if (handle->text_buffer) {
        handle->text_buffer[0] = '\0';
    }
}

static void reset_binary_message(lgeine_agent_handle_t handle)
{
    handle->binary_len = 0U;
    handle->binary_payload_len = 0U;
    handle->binary_discarding = QM_FALSE;
}

static int grow_binary_buffer(lgeine_agent_handle_t handle, uint32_t required)
{
    uint8_t *buffer;
    uint32_t capacity;

    if (required > ws_rx_size(handle)) {
        return -QM_EFULL;
    }
    if (required <= handle->binary_capacity) {
        return QM_EOK;
    }
    capacity = handle->binary_capacity ? handle->binary_capacity : ws_rx_size(handle);
    if (capacity > ws_rx_size(handle)) {
        capacity = ws_rx_size(handle);
    }
    buffer = (uint8_t *)qm_malloc(capacity);
    if (!buffer) {
        return -QM_ENOMEM;
    }
    if (handle->binary_buffer && handle->binary_len > 0U) {
        qm_memcpy(buffer, handle->binary_buffer, handle->binary_len);
    }
    qm_free(handle->binary_buffer);
    handle->binary_buffer = buffer;
    handle->binary_capacity = capacity;
    return QM_EOK;
}

static int valid_downlink_audio_len(lgeine_agent_handle_t handle, uint32_t len)
{
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (handle->negotiated_audio.format == LGEINE_AGENT_AUDIO_FORMAT_PCM &&
        !valid_pcm_frame_len(&handle->negotiated_audio, len)) {
        qm_mutex_unlock(&handle->lock);
        return -QM_EINVAL;
    }
    qm_mutex_unlock(&handle->lock);
    return QM_EOK;
}

static void receive_binary_chunk(lgeine_agent_handle_t handle,
                                 const qm_websocket_event_data_t *data)
{
    uint32_t chunk_len;
    uint32_t payload_len;
    uint32_t offset;
    int ret;
    lgeine_agent_event_t event;

    if (!data || data->data_len < 0 || data->payload_len < 0 ||
        data->payload_offset < 0 || data->payload_offset > data->payload_len ||
        data->data_len > data->payload_len - data->payload_offset ||
        (data->data_len > 0 && !data->data_ptr)) {
        reset_binary_message(handle);
        return;
    }
    chunk_len = (uint32_t)data->data_len;
    payload_len = (uint32_t)data->payload_len;
    offset = (uint32_t)data->payload_offset;
    if (payload_len == 0U || payload_len > ws_rx_size(handle) ||
        (offset == 0U && handle->binary_len != 0U) ||
        (offset != handle->binary_len)) {
        handle->binary_discarding = QM_TRUE;
    }
    if (handle->binary_discarding) {
        if (data->fin) reset_binary_message(handle);
        return;
    }
    if (offset == 0U) {
        reset_binary_message(handle);
        handle->binary_payload_len = payload_len;
    }
    ret = grow_binary_buffer(handle, handle->binary_len + chunk_len);
    if (ret != QM_EOK) {
        handle->binary_discarding = QM_TRUE;
        if (data->fin) reset_binary_message(handle);
        return;
    }
    if (chunk_len > 0U && data->data_ptr) {
        qm_memcpy(handle->binary_buffer + handle->binary_len, data->data_ptr, chunk_len);
    }
    handle->binary_len += chunk_len;
    if (!data->fin) return;
    if (handle->binary_len == 0U || handle->binary_len != handle->binary_payload_len ||
        valid_downlink_audio_len(handle, handle->binary_len) != QM_EOK) {
        reset_binary_message(handle);
        return;
    }
    qm_memset(&event, 0, sizeof(event));
    event.id = LGEINE_AGENT_EVENT_AUDIO;
    event.data.audio.data = handle->binary_buffer;
    event.data.audio.data_len = handle->binary_len;
    event.data.audio.final = QM_TRUE;
    emit_event(handle, &event);
    reset_binary_message(handle);
}

static int grow_text_buffer(lgeine_agent_handle_t handle, uint32_t required)
{
    char *buffer;
    uint32_t capacity;
    uint32_t limit = max_text_size(handle);

    if (required > limit + 1U) {
        return -QM_EFULL;
    }
    if (required <= handle->text_capacity) {
        return QM_EOK;
    }
    capacity = handle->text_capacity ? handle->text_capacity : LGEINE_TEXT_INITIAL_CAPACITY;
    if (capacity > limit + 1U) {
        capacity = limit + 1U;
    }
    while (capacity < required) {
        uint32_t next = capacity <= (limit + 1U) / 2U ? capacity * 2U : limit + 1U;
        if (next == capacity) {
            return -QM_EFULL;
        }
        capacity = next;
    }
    buffer = (char *)qm_malloc(capacity);
    if (!buffer) {
        return -QM_ENOMEM;
    }
    if (handle->text_buffer && handle->text_len > 0U) {
        qm_memcpy(buffer, handle->text_buffer, handle->text_len);
    }
    qm_free(handle->text_buffer);
    handle->text_buffer = buffer;
    handle->text_capacity = capacity;
    return QM_EOK;
}

/*
 * Accumulate one inbound WebSocket text frame (possibly fragmented) into the
 * text buffer, then dispatch the reassembled JSON on the final fragment.
 * Enforces the max text message size; oversized or empty messages are reported
 * and discarded.
 */
static void receive_text_chunk(lgeine_agent_handle_t handle,
                               const qm_websocket_event_data_t *data)
{
    uint32_t chunk_len;
    uint32_t required;
    int ret;

    if (data->data_len < 0) {
        return;
    }
    if (handle->text_discarding) {
        if (data->fin) reset_text_message(handle);
        return;
    }
    chunk_len = (uint32_t)data->data_len;
    if (chunk_len > max_text_size(handle) - handle->text_len) {
        handle->text_len = 0U;
        handle->text_discarding = QM_TRUE;
        emit_error(handle, LGEINE_AGENT_ERROR_PROTOCOL, -QM_EFULL,
                   "text message exceeds configured limit", NULL);
        if (data->fin) reset_text_message(handle);
        return;
    }
    required = handle->text_len + chunk_len + 1U;
    ret = grow_text_buffer(handle, required);
    if (ret != QM_EOK) {
        handle->text_len = 0U;
        handle->text_discarding = QM_TRUE;
        emit_error(handle,
                   ret == -QM_ENOMEM ? LGEINE_AGENT_ERROR_MEMORY : LGEINE_AGENT_ERROR_PROTOCOL,
                   ret, "text message buffer allocation failed", NULL);
        if (data->fin) reset_text_message(handle);
        return;
    }
    if (chunk_len > 0U && data->data_ptr) {
        qm_memcpy(handle->text_buffer + handle->text_len, data->data_ptr, chunk_len);
    }
    handle->text_len += chunk_len;
    handle->text_buffer[handle->text_len] = '\0';
    if (data->fin) {
        if (handle->text_len == 0U) {
            emit_error(handle, LGEINE_AGENT_ERROR_PROTOCOL, -QM_EINVAL,
                       "empty text message", handle->text_buffer);
        } else {
            (void)dispatch_server_message(handle, handle->text_buffer, handle->text_len);
        }
        reset_text_message(handle);
    }
}

/*
 * hello timer callback: if the session is still connected but not ready when it
 * fires, mark the handshake failed, trigger reconnection, and emit a hello
 * timeout error.
 */
static void hello_timeout_cb(qm_timer_t *timer, void *arg)
{
    lgeine_agent_handle_t handle = (lgeine_agent_handle_t)arg;
    qm_bool timed_out = QM_FALSE;

    (void)timer;
    if (!handle) {
        return;
    }
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (!handle->destroying && handle->connected && !handle->ready) {
        handle->hello_failed = QM_TRUE;
        timed_out = QM_TRUE;
    }
    qm_mutex_unlock(&handle->lock);
    if (timed_out) {
        emit_error(handle, LGEINE_AGENT_ERROR_HELLO_TIMEOUT, -QM_ETIMEOUT,
                   "helloReply timeout", NULL);
        request_reconnect_or_close(handle, LGEINE_WS_CLOSE_POLICY);
    }
}

/*
 * (Re)arm the one-shot hello timer with the configured timeout.
 */
static int start_hello_timer(lgeine_agent_handle_t handle)
{
    int ret;

    (void)qm_timer_stop(&handle->hello_timer);
    ret = qm_timer_change(&handle->hello_timer, (int)hello_timeout_ms(handle));
    if (ret == QM_EOK) {
        ret = qm_timer_start(&handle->hello_timer);
    }
    return ret;
}

/*
 * Deferred WebSocket close for a server `abort` with `reason="session_end"`.
 * The close must not run inside the WebSocket DATA callback, so it is deferred
 * one timer tick to a non-transport context where qm_websocket_client_close can
 * safely wait for the transport task to exit.
 */
static void session_end_close_cb(qm_timer_t *timer, void *arg)
{
    lgeine_agent_handle_t handle = (lgeine_agent_handle_t)arg;
    void *ws;
    int close_code;

    (void)timer;
    if (!handle) {
        return;
    }
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    ws = handle->ws;
    close_code = handle->deferred_close_code ? handle->deferred_close_code : LGEINE_WS_CLOSE_NORMAL;
    handle->deferred_close_code = 0;
    qm_mutex_unlock(&handle->lock);
    if (ws) {
        if (close_code == LGEINE_WS_CLOSE_NORMAL) {
            (void)qm_websocket_client_close(ws, network_timeout_ms(handle));
        } else {
            (void)qm_websocket_client_close_with_code(ws, close_code, NULL, 0,
                                                      network_timeout_ms(handle));
        }
    }
}

static void request_close_on_session_end(lgeine_agent_handle_t handle)
{
    (void)qm_timer_stop(&handle->close_timer);
    (void)qm_timer_start(&handle->close_timer);
}

/*
 * WebSocket transport event dispatcher.
 *
 * Runs on the transport task: CONNECTED drives the hello handshake, DATA routes
 * binary (audio) and text (JSON) frames, DISCONNECTED/ERROR reset state and
 * notify the user.
 */
static void websocket_event(qm_websocket_event_id_t event_id,
                            qm_websocket_event_data_t *event_data,
                            void *user_context)
{
    lgeine_agent_handle_t handle = (lgeine_agent_handle_t)user_context;
    lgeine_agent_event_t event;
    int ret;

    if (!handle) {
        return;
    }
    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
            handle->connected = QM_TRUE;
            handle->ready = QM_FALSE;
            handle->hello_failed = QM_FALSE;
            handle->tts_active = QM_FALSE;
            handle->tts_sentence_active = QM_FALSE;
            handle->listen_active = QM_FALSE;
            handle->listen_recording = QM_FALSE;
            qm_free(handle->session_id);
            handle->session_id = NULL;
            qm_mutex_unlock(&handle->lock);
            reset_text_message(handle);
            reset_binary_message(handle);

            ret = send_hello(handle);
            if (ret != QM_EOK) {
                qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
                handle->hello_failed = QM_TRUE;
                qm_mutex_unlock(&handle->lock);
                emit_error(handle, LGEINE_AGENT_ERROR_WS, ret,
                           "hello send failed", NULL);
                request_reconnect_or_close(handle, LGEINE_WS_CLOSE_POLICY);
                break;
            }
            ret = start_hello_timer(handle);
            if (ret != QM_EOK) {
                qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
                handle->hello_failed = QM_TRUE;
                qm_mutex_unlock(&handle->lock);
                emit_error(handle, LGEINE_AGENT_ERROR_STATE, ret,
                           "hello timer start failed", NULL);
                request_reconnect_or_close(handle, LGEINE_WS_CLOSE_POLICY);
                break;
            }
            qm_memset(&event, 0, sizeof(event));
            event.id = LGEINE_AGENT_EVENT_TRANSPORT_CONNECTED;
            LGEINE_AGENT_LOGI(LOG_TAG, LGEINE_AGENT_LOG_LIFECYCLE,
                              "transport connected, hello sent");
            emit_event(handle, &event);
            break;
        case WEBSOCKET_EVENT_DATA:
            if (!event_data) {
                return;
            }
            if (event_data->opcode == LGEINE_WS_OPCODE_BINARY) {
                qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
                ret = ready_locked(handle) && handle->tts_sentence_active;
                qm_mutex_unlock(&handle->lock);
                if (!ret) {
                    reset_binary_message(handle);
                    return;
                }
                receive_binary_chunk(handle, event_data);
            } else if (event_data->opcode == LGEINE_WS_OPCODE_TEXT) {
                receive_text_chunk(handle, event_data);
            }
            break;
        case WEBSOCKET_EVENT_DISCONNECTED:
            (void)qm_timer_stop(&handle->hello_timer);
            qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
            handle->connected = QM_FALSE;
            handle->ready = QM_FALSE;
            handle->hello_failed = QM_FALSE;
            handle->tts_active = QM_FALSE;
            handle->tts_sentence_active = QM_FALSE;
            handle->listen_active = QM_FALSE;
            handle->listen_recording = QM_FALSE;
            qm_free(handle->session_id);
            handle->session_id = NULL;
            qm_mutex_unlock(&handle->lock);
            reset_text_message(handle);
            reset_binary_message(handle);
            qm_memset(&event, 0, sizeof(event));
            event.id = LGEINE_AGENT_EVENT_DISCONNECTED;
            LGEINE_AGENT_LOGI(LOG_TAG, LGEINE_AGENT_LOG_LIFECYCLE,
                              "transport disconnected");
            emit_event(handle, &event);
            break;
        case WEBSOCKET_EVENT_ERROR:
            emit_error(handle, LGEINE_AGENT_ERROR_WS,
                       event_data ? event_data->error_code : -QM_EIO,
                       "WebSocket error", NULL);
            break;
        default:
            break;
    }
}

/*
 * Allocate and initialize the agent handle: validates the config, creates the
 * mutex and both timers (hello timeout and deferred close). Returns NULL on any
 * failure after releasing partial allocations.
 */
lgeine_agent_handle_t lgeine_agent_init(const lgeine_agent_init_config_t *config)
{
    lgeine_agent_handle_t handle;
    int ret;

    if (!config || !config->event_cb ||
        config->config.network_timeout_ms > 2147483647U ||
        config->config.hello_timeout_ms > 2147483647U ||
        config->config.reconnect_interval_ms > 2147483647U ||
        config->config.ws_rx_size > 2147483647U ||
        config->config.max_text_message_size > 2147483647U ||
        (config->config.ws_rx_size != 0U && config->config.ws_rx_size < 128U) ||
        (config->config.max_text_message_size != 0U &&
         config->config.max_text_message_size < 256U) ||
        config->config.ws_stack_size < 0 || config->config.ws_priority < 0 ||
        config->config.send_queue_size < 0) {
        return NULL;
    }
    handle = (lgeine_agent_handle_t)qm_malloc(sizeof(*handle));
    if (!handle) {
        return NULL;
    }
    qm_memset(handle, 0, sizeof(*handle));
    handle->config = config->config;
    handle->event_cb = config->event_cb;
    handle->user_ctx = config->user_ctx;

    ret = qm_mutex_new(&handle->lock);
    if (ret != QM_EOK) {
        qm_free(handle);
        return NULL;
    }
    handle->lock_valid = QM_TRUE;
    ret = qm_timer_new(&handle->hello_timer, hello_timeout_cb, handle,
                       (int)hello_timeout_ms(handle), 0);
    if (ret != QM_EOK) {
        (void)qm_mutex_free(&handle->lock);
        qm_free(handle);
        return NULL;
    }
    handle->timer_valid = QM_TRUE;
    ret = qm_timer_new(&handle->close_timer, session_end_close_cb, handle, 1, 0);
    if (ret != QM_EOK) {
        (void)qm_timer_free(&handle->hello_timer);
        (void)qm_mutex_free(&handle->lock);
        qm_free(handle);
        return NULL;
    }
    handle->close_timer_valid = QM_TRUE;
    return handle;
}

/*
 * Attach the product, device, and authorization headers required by the
 * protocol handshake.
 */
static int configure_headers(void *ws, const lgeine_agent_start_params_t *params)
{
    size_t token_len = qm_strlen(params->access_token);
    char *auth;
    int ret;

    ret = qm_websocket_client_append_header(ws, "pid", params->pid);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_websocket_client_append_header(ws, "sn", params->sn);
    if (ret != QM_EOK) {
        return ret;
    }
    auth = (char *)qm_malloc(token_len + 8U);
    if (!auth) {
        return -QM_ENOMEM;
    }
    qm_snprintf(auth, token_len + 8U, "Bearer %s", params->access_token);
    ret = qm_websocket_client_append_header(ws, "Authorization", auth);
    qm_free(auth);
    return ret;
}

/*
 * Validate start parameters, create and configure the WebSocket client, then
 * start the transport task. The hello handshake happens asynchronously via the
 * CONNECTED event.
 */
int lgeine_agent_start(lgeine_agent_handle_t handle,
                       const lgeine_agent_start_params_t *params)
{
    qm_websocket_client_config_t ws_config;
    char *endpoint_uri = NULL;
    void *ws;
    int ret;

    if (!handle || !params || !valid_header_identity(params->pid) ||
        !valid_header_identity(params->sn) ||
        !params->access_token || params->access_token[0] == '\0' ||
        !valid_audio_info(&params->audio_info)) {
        return -QM_EINVAL;
    }
    ret = resolve_endpoint(params, &endpoint_uri);
    if (ret != QM_EOK) {
        return ret;
    }
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (handle->destroying || handle->ws || handle->starting) {
        qm_mutex_unlock(&handle->lock);
        qm_free(endpoint_uri);
        return -QM_EBUSY;
    }
    handle->starting = QM_TRUE;
    qm_mutex_unlock(&handle->lock);

    qm_memset(&ws_config, 0, sizeof(ws_config));
    ws_config.uri = endpoint_uri;
    ws_config.network_timeout_ms = (int)network_timeout_ms(handle);
    ws_config.reconnect_interval_ms = (int)reconnect_interval_ms(handle);
    ws_config.buffer_size = (int)ws_rx_size(handle);
    ws_config.task_stack = ws_stack_size(handle);
    ws_config.task_priority = ws_priority(handle);
    ws_config.send_queue_size = send_queue_size(handle);
    ws_config.disable_auto_reconnect = handle->config.disable_auto_reconnect;
    ws_config.disable_ping = handle->config.disable_ping;
    ws = qm_websocket_client_init(&ws_config);
    if (!ws) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->starting = QM_FALSE;
        qm_mutex_unlock(&handle->lock);
        qm_free(endpoint_uri);
        return -QM_EINIT;
    }
    ret = configure_headers(ws, params);
    if (ret != QM_EOK) goto fail;
    ret = qm_websocket_register_events(ws, WEBSOCKET_EVENT_MAX,
                                       websocket_event, handle);
    if (ret != QM_EOK) goto fail;

    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    handle->requested_audio = params->audio_info;
    handle->ws = ws;
    handle->endpoint_uri = endpoint_uri;
    handle->starting = QM_FALSE;
    qm_mutex_unlock(&handle->lock);
    ret = qm_websocket_client_start(ws);
    if (ret != QM_EOK) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->ws = NULL;
        handle->endpoint_uri = NULL;
        handle->starting = QM_FALSE;
        qm_mutex_unlock(&handle->lock);
        goto fail;
    }
    LGEINE_AGENT_LOGI(LOG_TAG, LGEINE_AGENT_LOG_LIFECYCLE,
                      "agent start requested");
    return QM_EOK;

fail:
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    handle->starting = QM_FALSE;
    qm_mutex_unlock(&handle->lock);
    qm_free(endpoint_uri);
    (void)qm_websocket_client_destroy(ws);
    return ret;
}

/*
 * Stop the current session: disarm timers, close and destroy the WebSocket,
 * then reset connection/session state. Idempotent when not started.
 */
int lgeine_agent_stop(lgeine_agent_handle_t handle)
{
    void *ws;
    int close_ret;
    int ret;

    if (!handle) {
        return -QM_EINVAL;
    }
    (void)qm_timer_stop(&handle->hello_timer);
    (void)qm_timer_stop(&handle->close_timer);
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (handle->starting) {
        qm_mutex_unlock(&handle->lock);
        return -QM_EBUSY;
    }
    ws = handle->ws;
    handle->connected = QM_FALSE;
    handle->ready = QM_FALSE;
    handle->hello_failed = QM_FALSE;
    handle->tts_active = QM_FALSE;
    handle->tts_sentence_active = QM_FALSE;
    handle->listen_active = QM_FALSE;
    handle->listen_recording = QM_FALSE;
    qm_mutex_unlock(&handle->lock);
    if (!ws) {
        return QM_EOK;
    }

    close_ret = qm_websocket_client_close(ws, network_timeout_ms(handle));
    if (close_ret != QM_EOK && close_ret != -QM_EIO) {
        return close_ret;
    }
    ret = qm_websocket_client_destroy(ws);
    if (ret != QM_EOK) {
        return ret;
    }
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (handle->ws == ws) {
        handle->ws = NULL;
    }
    qm_free(handle->endpoint_uri);
    handle->endpoint_uri = NULL;
    qm_free(handle->session_id);
    handle->session_id = NULL;
    qm_mutex_unlock(&handle->lock);
    reset_text_message(handle);
    LGEINE_AGENT_LOGI(LOG_TAG, LGEINE_AGENT_LOG_LIFECYCLE, "agent stopped");
    return QM_EOK;
}

/*
 * Tear down the handle: stop the session, free timers, buffers, and the mutex,
 * then release the handle memory. Non-reentrant.
 */
int lgeine_agent_destroy(lgeine_agent_handle_t handle)
{
    int ret;

    if (!handle) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
    if (handle->destroying || handle->starting) {
        qm_mutex_unlock(&handle->lock);
        return -QM_EBUSY;
    }
    handle->destroying = QM_TRUE;
    qm_mutex_unlock(&handle->lock);

    ret = lgeine_agent_stop(handle);
    if (ret != QM_EOK) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->destroying = QM_FALSE;
        qm_mutex_unlock(&handle->lock);
        return ret;
    }
    if (handle->timer_valid) {
        (void)qm_timer_stop(&handle->hello_timer);
        (void)qm_timer_free(&handle->hello_timer);
        handle->timer_valid = QM_FALSE;
    }
    if (handle->close_timer_valid) {
        (void)qm_timer_stop(&handle->close_timer);
        (void)qm_timer_free(&handle->close_timer);
        handle->close_timer_valid = QM_FALSE;
    }
    qm_free(handle->binary_buffer);
    qm_free(handle->text_buffer);
    qm_free(handle->session_id);
    if (handle->lock_valid) {
        (void)qm_mutex_free(&handle->lock);
    }
    qm_free(handle);
    return QM_EOK;
}

qm_bool lgeine_agent_is_ready(lgeine_agent_handle_t handle)
{
    return handle ? protocol_ready(handle) : QM_FALSE;
}

/*
 * Check an uplink PCM frame length against protocol 4.3:
 * len == rate * bits / 8 * channel * frameDuration / 1000.
 */
static qm_bool valid_pcm_frame_len(const lgeine_agent_audio_info_t *audio,
                                   uint32_t len)
{
    unsigned long long expected;

    expected = (unsigned long long)audio->rate * audio->bits *
               audio->channel * audio->frame_duration_ms / 8U / 1000U;
    return len == (uint32_t)expected;
}

/*
 * Upload one uplink audio frame. Enforces readiness and, for PCM, the protocol
 * frame length; Opus frame length is encoder-defined and not fixed.
 */
int lgeine_agent_send_audio(lgeine_agent_handle_t handle,
                            const void *data,
                            uint32_t len,
                            uint32_t timeout_ms)
{
    int ret;

    if (!handle || !data || len == 0U || len > 2147483647U) {
        return -QM_EINVAL;
    }
    /* Protocol state contention rejects admission; timeout only budgets the
     * subsequent WebSocket queue wait, which capture callers set to zero. */
    ret = qm_mutex_lock(&handle->lock, QM_NO_WAIT);
    if (ret != QM_EOK) return -QM_EBUSY;
    if (!ready_locked(handle) || !handle->listen_recording) {
        qm_mutex_unlock(&handle->lock);
        return -QM_EBUSY;
    }
    if (handle->requested_audio.format == LGEINE_AGENT_AUDIO_FORMAT_PCM &&
        !valid_pcm_frame_len(&handle->requested_audio, len)) {
        qm_mutex_unlock(&handle->lock);
        return -QM_EINVAL;
    }
    ret = qm_websocket_client_send_bin(handle->ws, (const char *)data, (int)len,
                                        timeout_ms);
    qm_mutex_unlock(&handle->lock);
    if (ret < QM_EOK) {
        return ret;
    }
    return (uint32_t)ret == len ? QM_EOK : -QM_EIO;
}

/*
 * Send a single-method (no extra fields) session message.
 */
static int send_simple_method(lgeine_agent_handle_t handle,
                              const char *method,
                              uint32_t timeout_ms)
{
    cJSON *root = make_method(method);

    int ret = root ? send_session_root(handle, root, timeout_ms) : -QM_ENOMEM;
    if (ret == QM_EOK) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->listen_active = QM_FALSE;
        handle->listen_recording = QM_FALSE;
        handle->tts_active = QM_FALSE;
        handle->tts_sentence_active = QM_FALSE;
        reset_binary_message(handle);
        qm_mutex_unlock(&handle->lock);
    }
    return ret;
}

int lgeine_agent_send_welcome(lgeine_agent_handle_t handle, uint32_t timeout_ms)
{
    return send_simple_method(handle, "welcome", timeout_ms);
}

int lgeine_agent_send_goodbye(lgeine_agent_handle_t handle, uint32_t timeout_ms)
{
    return send_simple_method(handle, "goodbye", timeout_ms);
}

int lgeine_agent_send_listen(lgeine_agent_handle_t handle,
                             lgeine_agent_listen_state_t state,
                             lgeine_agent_listen_mode_t mode,
                             const char *wake_word,
                             uint32_t timeout_ms)
{
    const char *state_name;
    const char *mode_name;
    cJSON *root;
    int ret;

    state_name = listen_state_name(state);
    mode_name = listen_mode_name(mode);
    if (!state_name || !mode_name ||
        (state == LGEINE_AGENT_LISTEN_STATE_DETECT && (!wake_word || wake_word[0] == '\0')) ||
        (state != LGEINE_AGENT_LISTEN_STATE_DETECT && wake_word)) {
        return -QM_EINVAL;
    }
    root = make_method("listen");
    if (!root) return -QM_ENOMEM;
    if (!cJSON_AddStringToObject(root, "state", state_name) ||
        !cJSON_AddStringToObject(root, "mode", mode_name) ||
        (wake_word && !cJSON_AddStringToObject(root, "text", wake_word))) {
        cJSON_Delete(root);
        return -QM_ENOMEM;
    }
    ret = send_session_root(handle, root, timeout_ms);
    if (ret == QM_EOK) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        /* start/detect/stop all keep the listen interaction open so a final
         * stt may still arrive; only start enables the uplink audio gate. */
        handle->listen_active = QM_TRUE;
        handle->listen_recording = (state == LGEINE_AGENT_LISTEN_STATE_START);
        handle->tts_active = QM_FALSE;
        handle->tts_sentence_active = QM_FALSE;
        reset_binary_message(handle);
        qm_mutex_unlock(&handle->lock);
    }
    return ret;
}

int lgeine_agent_send_speak(lgeine_agent_handle_t handle,
                            const char *text,
                            uint32_t timeout_ms)
{
    cJSON *root;
    int ret;

    if (!text || text[0] == '\0') {
        return -QM_EINVAL;
    }
    root = make_method("speak");
    if (!root) return -QM_ENOMEM;
    if (!cJSON_AddStringToObject(root, "text", text)) {
        cJSON_Delete(root);
        return -QM_ENOMEM;
    }
    ret = send_session_root(handle, root, timeout_ms);
    if (ret == QM_EOK) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->listen_active = QM_FALSE;
        handle->listen_recording = QM_FALSE;
        handle->tts_active = QM_FALSE;
        handle->tts_sentence_active = QM_FALSE;
        reset_binary_message(handle);
        qm_mutex_unlock(&handle->lock);
    }
    return ret;
}

int lgeine_agent_send_abort(lgeine_agent_handle_t handle,
                            lgeine_agent_abort_reason_t reason,
                            uint32_t timeout_ms)
{
    const char *reason_name = abort_reason_name(reason);
    cJSON *root;
    int ret;

    if (!reason_name) {
        return -QM_EINVAL;
    }
    root = make_method("abort");
    if (!root) return -QM_ENOMEM;
    if (!cJSON_AddStringToObject(root, "reason", reason_name)) {
        cJSON_Delete(root);
        return -QM_ENOMEM;
    }
    ret = send_session_root(handle, root, timeout_ms);
    if (ret == QM_EOK) {
        qm_mutex_lock(&handle->lock, QM_WAIT_FOREVER);
        handle->listen_active = QM_FALSE;
        handle->listen_recording = QM_FALSE;
        handle->tts_active = QM_FALSE;
        handle->tts_sentence_active = QM_FALSE;
        qm_mutex_unlock(&handle->lock);
    }
    return ret;
}
