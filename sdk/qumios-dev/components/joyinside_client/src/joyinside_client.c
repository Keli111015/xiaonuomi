/**
 * @file joyinside_client.c
 * @brief JoyInside WebSocket lifecycle, protocol dispatch, and core worker.
 *
 * The WebSocket worker owns transport callbacks. The core worker serializes
 * protocol deadlines and queued audio, so public calls never retain caller data.
 */

#include "joyinside_client.h"
#include "joyinside_client_audiobook.h"
#include "joyinside_client_print.h"

#include "cJSON.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_websocket_client.h"
#include "qm_utils_timer.h"
#include "qm_utils_uuid.h"
#include "joyinside_client_log.h"
#include "backoff_algorithm.h"
#include "joyinside_client_internal.h"

#include "qm_utils_base64.h"

#ifndef CONFIG_JOYINSIDE_CLIENT_WS_RX_SIZE
#define CONFIG_JOYINSIDE_CLIENT_WS_RX_SIZE 4096
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS
#define CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_SEND_RETRY_COUNT
#define CONFIG_JOYINSIDE_CLIENT_SEND_RETRY_COUNT 2
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_SEND_RETRY_DELAY_MS
#define CONFIG_JOYINSIDE_CLIENT_SEND_RETRY_DELAY_MS 100
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_ACK_TIMEOUT_MS
#define CONFIG_JOYINSIDE_CLIENT_ACK_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_MAX_TEXT_SIZE
#define CONFIG_JOYINSIDE_CLIENT_MAX_TEXT_SIZE 65536U
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_TOKEN_SIZE
#define CONFIG_JOYINSIDE_CLIENT_TOKEN_SIZE 4096U
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_AUTO_RECONNECT
#define CONFIG_JOYINSIDE_CLIENT_AUTO_RECONNECT 1
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_WS_TASK_STACK_SIZE
#ifdef CONFIG_JOYINSIDE_CLIENT_TASK_STACK_SIZE
#define CONFIG_JOYINSIDE_CLIENT_WS_TASK_STACK_SIZE CONFIG_JOYINSIDE_CLIENT_TASK_STACK_SIZE
#else
#define CONFIG_JOYINSIDE_CLIENT_WS_TASK_STACK_SIZE 8192
#endif
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_WS_TASK_PRIORITY
#ifdef CONFIG_JOYINSIDE_CLIENT_TASK_PRIORITY
#define CONFIG_JOYINSIDE_CLIENT_WS_TASK_PRIORITY CONFIG_JOYINSIDE_CLIENT_TASK_PRIORITY
#else
#define CONFIG_JOYINSIDE_CLIENT_WS_TASK_PRIORITY 5
#endif
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_CORE_TASK_STACK_SIZE
#ifdef CONFIG_JOYINSIDE_CLIENT_TASK_STACK_SIZE
#define CONFIG_JOYINSIDE_CLIENT_CORE_TASK_STACK_SIZE CONFIG_JOYINSIDE_CLIENT_TASK_STACK_SIZE
#else
#define CONFIG_JOYINSIDE_CLIENT_CORE_TASK_STACK_SIZE 4096
#endif
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_CORE_TASK_PRIORITY
#ifdef CONFIG_JOYINSIDE_CLIENT_TASK_PRIORITY
#define CONFIG_JOYINSIDE_CLIENT_CORE_TASK_PRIORITY CONFIG_JOYINSIDE_CLIENT_TASK_PRIORITY
#else
#define CONFIG_JOYINSIDE_CLIENT_CORE_TASK_PRIORITY 5
#endif
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_SEND_QUEUE_SIZE
#define CONFIG_JOYINSIDE_CLIENT_SEND_QUEUE_SIZE 8
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_INTERVAL_MS
#define CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_INTERVAL_MS 30000
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_TIMEOUT_MS
#define CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_TIMEOUT_MS 3000
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_MAX_FAILURES
#define CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_MAX_FAILURES 3U
#endif

#define JOYINSIDE_CLIENT_VOICE_WS_URL "wss://ws.joyinside.com/soulmate/voiceChat/v1"
#define JOYINSIDE_CLIENT_UUID36_LEN 36
#define JOYINSIDE_CLIENT_UUID36_BUFFER_LEN (JOYINSIDE_CLIENT_UUID36_LEN + 1U)
#define JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN 1U
/* PCM output frames are nominally 60-120 ms. Larger inter-arrival gaps can
 * drain the application playback cbuf and produce audible stalls. */
#define JOYINSIDE_CLIENT_AUDIO_GAP_WARN_MS 250U
#define JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR '\0'
#define JOYINSIDE_CLIENT_CSTR_FIRST_INDEX 0U
#define JOYINSIDE_CLIENT_U32_DECIMAL_TEXT_LEN 11U
#define JOYINSIDE_CLIENT_SNPRINTF_COUNT_ONLY 0U
#define JOYINSIDE_CLIENT_SNPRINTF_ERROR_MAX 0
#define JOYINSIDE_CLIENT_RESPONSE_CODE_OK 200
#define JOYINSIDE_CLIENT_WS_OPCODE_BINARY 2
#define JOYINSIDE_CLIENT_STRCMP_EQUAL 0
#define JOYINSIDE_CLIENT_MEMSET_ZERO 0
#define JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN 0U
#define JOYINSIDE_CLIENT_PAYLOAD_OFFSET_START 0U
#define JOYINSIDE_CLIENT_UINT_ABSENT 0U
#define JOYINSIDE_CLIENT_UINT64_ABSENT 0ULL
#define JOYINSIDE_CLIENT_UNSIGNED_VALUE_MIN 0
#define JOYINSIDE_CLIENT_SAMPLE_RATE_OMITTED 0U
#define JOYINSIDE_CLIENT_FRAME_SIZE_OMITTED 0U
#define JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_OMITTED 0U
#define JOYINSIDE_CLIENT_SEND_FIRST_ATTEMPT 0U

#define JOYINSIDE_CLIENT_BASE64_PAD_BYTES 2U
#define JOYINSIDE_CLIENT_BASE64_GROUP_CHARS 4U
#define JOYINSIDE_CLIENT_BASE64_GROUP_BYTES 3U
#define JOYINSIDE_CLIENT_BASE64_PAD_CHAR '='
#define JOYINSIDE_CLIENT_JSON_BOOL_TRUE 1
#define JOYINSIDE_CLIENT_FLOAT_ZERO 0.0f
#ifndef CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN
#define CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN 8U
#endif
#ifndef CONFIG_JOYINSIDE_CLIENT_AUDIO_PACKET_BYTES
#define CONFIG_JOYINSIDE_CLIENT_AUDIO_PACKET_BYTES 4096U
#endif
#define JOYINSIDE_CLIENT_TTS_TEXT_MAX_BYTES 1000U
#define JOYINSIDE_CLIENT_CORE_QUEUE_LEN (CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN + 1U)
#define JOYINSIDE_CLIENT_TEXT_INITIAL_CAPACITY 256U
#define JOYINSIDE_CLIENT_RECONNECT_BASE_MS 1000U
#define JOYINSIDE_CLIENT_RECONNECT_MAX_MS 30000U
#ifndef CONFIG_JOYINSIDE_CLIENT_CORE_STOP_TIMEOUT_MS
#define CONFIG_JOYINSIDE_CLIENT_CORE_STOP_TIMEOUT_MS 6000U
#endif

#define LOG_TAG "joyinside_client"

static int normalize_voice(joyinside_client_voice_config_t *voice);
static void free_voice_request(joyinside_client_voice_config_t *voice);

static volatile uint32_t g_joyinside_client_log_flags = JOYINSIDE_CLIENT_KCONFIG_LOG_FLAGS;

typedef enum {
    JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT = 1,
    JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_PONG,
    JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_RESET,
    JOYINSIDE_CLIENT_CORE_EVENT_ACK_TIMEOUT,
    JOYINSIDE_CLIENT_CORE_EVENT_RECONNECT,
    JOYINSIDE_CLIENT_CORE_EVENT_CONTEXT_REPORT,
    JOYINSIDE_CLIENT_CORE_EVENT_AUDIOBOOK_PING,
    JOYINSIDE_CLIENT_CORE_EVENT_VOICE_UPDATE,
    JOYINSIDE_CLIENT_CORE_EVENT_STOP,
    JOYINSIDE_CLIENT_CORE_EVENT_AUDIO,
    JOYINSIDE_CLIENT_CORE_EVENT_AUDIO_FINISH,
} joyinside_client_core_event_id_t;

typedef struct {
    uint32_t len;
    uint32_t index;
    char mid[JOYINSIDE_CLIENT_UUID36_BUFFER_LEN];
    qm_bool busy;
    qm_bool binary;
    uint8_t data[CONFIG_JOYINSIDE_CLIENT_AUDIO_PACKET_BYTES];
} joyinside_client_audio_slot_t;

typedef struct {
    joyinside_client_core_event_id_t id;
    char mid[JOYINSIDE_CLIENT_UUID36_BUFFER_LEN];
    joyinside_client_audio_slot_t *audio;
    uint32_t generation;
} joyinside_client_core_event_t;

struct joyinside_client {
    joyinside_client_config_t config;
    joyinside_client_voice_config_t *queued_voice; /* Owned until worker/stop consumes it. / 工作线程或停止流程负责释放。 */
    joyinside_client_extensions_t extensions;
    void *ws;
    joyinside_client_network_state_t network_state;
    qm_task_t core_task;
    qm_queue_t core_queue;
    /** @brief Admission lock never held during protocol encoding or I/O. */
    qm_mutex_t core_lock;
    /** @brief Serializes encoding with connection invalidation, never capture. */
    qm_mutex_t audio_send_lock;
    /** @brief Joins concurrent control sends before freeing the transport. */
    qm_mutex_t tx_lock;
    qm_sem_t core_wake;
    qm_bool core_wake_pending;
    uint32_t core_queue_count;
    uint32_t core_pending;
    joyinside_client_audio_slot_t *audio_slots;
    uint32_t audio_generation;
    uint32_t audio_index;
    qm_bool audio_accepting;
    qm_bool audio_binary;
    qm_bool core_exited;
    qm_sem_t core_exit_sem;
    /* Deadlines are polled by the core worker; no OS timer retains this handle. */
    qm_utils_time_t protocol_deadline[JOYINSIDE_CLIENT_CORE_EVENT_STOP];
    uint32_t protocol_period_ms[JOYINSIDE_CLIENT_CORE_EVENT_STOP];
    uint32_t protocol_active;
    int (*audiobook_ping)(joyinside_client_handle_t handle);
    char *bot_id;
    char *uid;
    char *feature;
    char *client_type;
    char *location;
    qm_bool need_asr_partial;
    char *session_id;
    char *request_id;
    char *access_token;
    joyinside_client_token_provider_t token_provider;
    void *token_context;
    qm_bool force_token_refresh;
    qm_bool connection_attempted;
    qm_bool session_replaced;
    /* Reconnect scheduling belongs to the core task, not the WS transport. */
    qm_bool reconnect_scheduled;
    qm_utils_time_t reconnect_deadline;
    backoff_algorithm_ctx_t reconnect_backoff;
    /* Only the WebSocket worker touches text assembly while running. */
    char *rx_text;
    uint32_t rx_text_len;
    uint32_t rx_text_capacity;
    uint32_t rx_frame_offset;
    uint32_t rx_frame_length;
    qm_bool rx_discard;
    /* The core task exclusively owns heartbeat state below. */
    char heartbeat_mid[JOYINSIDE_CLIENT_UUID36_BUFFER_LEN];
    qm_utils_time_t heartbeat_ping_timer;
    qm_utils_time_t heartbeat_deadline;
    qm_bool connected;
    qm_bool output_binary;
    qm_bool ack_pending;
    qm_bool core_task_valid;
    qm_bool core_queue_valid;
    qm_bool core_exit_sem_valid;
    qm_bool heartbeat_pending;
    uint8_t heartbeat_fail_count;
    qm_bool core_stop_requested;
    uint32_t audio_rx_packets;
    uint32_t audio_rx_last_ms;
    uint32_t audio_rx_gap_max_ms;
    uint32_t audio_rx_gap_warn_count;
    /* End-side VAD round transport: every AUDIO chunk of one round carries the
     * same content.mid, which the server echoes as the downlink round id. */
    char vad_round_mid[JOYINSIDE_CLIENT_UUID36_BUFFER_LEN];
    /* Last CLIENT_UPDATE_CHAT_CONTEXT payload for re-reporting on reconnect. */
    char *saved_context_kv_data;
    uint32_t saved_context_effective_minutes;
};

joyinside_client_extensions_t *joyinside_client_extensions(joyinside_client_handle_t handle)
{
    return handle ? &handle->extensions : NULL;
}

qm_bool joyinside_client_running(joyinside_client_handle_t handle)
{
    return handle && handle->ws != NULL;
}

static __attribute__((unused)) const char *bool_text(qm_bool value)
{
    return value ? "true" : "false";
}

static __attribute__((unused)) const char *safe_text(const char *value)
{
    return value ? value : "null";
}

static void audio_rx_stats_reset(joyinside_client_handle_t handle)
{
    if (handle == NULL) {
        return;
    }

    handle->audio_rx_packets = 0U;
    handle->audio_rx_last_ms = 0U;
    handle->audio_rx_gap_max_ms = 0U;
    handle->audio_rx_gap_warn_count = 0U;
}

static void audio_rx_stats_note_packet(joyinside_client_handle_t handle, uint32_t len)
{
    (void)len;
    uint32_t now_ms;
    uint32_t gap_ms;

    if (handle == NULL) {
        return;
    }

    now_ms = qm_now_ms();
    if (handle->audio_rx_last_ms != 0U) {
        gap_ms = now_ms - handle->audio_rx_last_ms;
        if (gap_ms > handle->audio_rx_gap_max_ms) {
            handle->audio_rx_gap_max_ms = gap_ms;
        }
        if (gap_ms >= JOYINSIDE_CLIENT_AUDIO_GAP_WARN_MS) {
            handle->audio_rx_gap_warn_count++;
            JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_STATS,
                                  "downlink PCM gap=%lums len=%lu packets=%lu max=%lums warnings=%lu",
                                  (unsigned long)gap_ms,
                                  (unsigned long)len,
                                  (unsigned long)handle->audio_rx_packets,
                                  (unsigned long)handle->audio_rx_gap_max_ms,
                                  (unsigned long)handle->audio_rx_gap_warn_count);
        }
    }
    handle->audio_rx_packets++;
    handle->audio_rx_last_ms = now_ms;
}

static void audio_rx_stats_finish(joyinside_client_handle_t handle)
{
    if (handle == NULL || handle->audio_rx_packets == 0U) {
        return;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_STATS,
                          "downlink PCM stats packets=%lu max_gap=%lums gap_warnings=%lu",
                          (unsigned long)handle->audio_rx_packets,
                          (unsigned long)handle->audio_rx_gap_max_ms,
                          (unsigned long)handle->audio_rx_gap_warn_count);
    audio_rx_stats_reset(handle);
}

static __attribute__((unused)) const char *present_text(const char *value)
{
    return value && value[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ?
           "yes" : "no";
}

static qm_bool audio_binary_configured(void)
{
#if CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY
    return QM_TRUE;
#else
    return QM_FALSE;
#endif
}

static __attribute__((unused)) qm_bool manual_call_configured(void)
{
#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
    return QM_TRUE;
#else
    return QM_FALSE;
#endif
}

static qm_bool auto_reconnect_configured(void)
{
#if CONFIG_JOYINSIDE_CLIENT_AUTO_RECONNECT
    return QM_TRUE;
#else
    return QM_FALSE;
#endif
}

static void log_session_state(const char *phase, joyinside_client_handle_t handle)
{
    if (!phase || !handle) {
        return;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "%s bot=%s uid=%s session=%s request=%s manual_call=%s",
            phase, safe_text(handle->bot_id), safe_text(handle->uid),
            safe_text(handle->session_id), safe_text(handle->request_id),
            bool_text(manual_call_configured()));
}

static const char *codec_name(joyinside_client_audio_codec_t codec)
{
    switch (codec) {
    case JOYINSIDE_CLIENT_AUDIO_CODEC_PCM:
        return "pcm";
    case JOYINSIDE_CLIENT_AUDIO_CODEC_MP3:
        return "mp3";
    case JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS:
        return "opus";
    default:
        return "opus";
    }
}

static qm_bool valid_input_codec(joyinside_client_audio_codec_t codec)
{
    return codec == JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS || codec == JOYINSIDE_CLIENT_AUDIO_CODEC_PCM;
}

static qm_bool valid_output_codec(joyinside_client_audio_codec_t codec)
{
    return codec == JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS || codec == JOYINSIDE_CLIENT_AUDIO_CODEC_PCM ||
           codec == JOYINSIDE_CLIENT_AUDIO_CODEC_MP3;
}

static qm_bool valid_sample_rate(uint32_t sample_rate)
{
    return sample_rate == JOYINSIDE_CLIENT_DEFAULT_SAMPLE_RATE ||
           sample_rate == JOYINSIDE_CLIENT_SAMPLE_RATE_24K ||
           sample_rate == JOYINSIDE_CLIENT_SAMPLE_RATE_32K;
}

static qm_bool valid_output_frame_ms(joyinside_client_audio_codec_t codec, uint32_t frame_ms)
{
    if (frame_ms == JOYINSIDE_CLIENT_FRAME_SIZE_OMITTED) {
        return QM_TRUE;
    }
    if (codec == JOYINSIDE_CLIENT_AUDIO_CODEC_PCM) {
        return frame_ms >= JOYINSIDE_CLIENT_PCM_OUTPUT_FRAME_MIN_MS &&
               frame_ms <= JOYINSIDE_CLIENT_PCM_OUTPUT_FRAME_MAX_MS;
    }
    if (codec == JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS) {
        return frame_ms == JOYINSIDE_CLIENT_OPUS_OUTPUT_FRAME_10_MS ||
               frame_ms == JOYINSIDE_CLIENT_OPUS_OUTPUT_FRAME_20_MS ||
               frame_ms == JOYINSIDE_CLIENT_OPUS_OUTPUT_FRAME_40_MS ||
               frame_ms == JOYINSIDE_CLIENT_OPUS_OUTPUT_FRAME_60_MS;
    }
    return QM_FALSE;
}

static int add_uint_string(cJSON *object, const char *name, uint32_t value)
{
    char text[JOYINSIDE_CLIENT_U32_DECIMAL_TEXT_LEN];
    int len;

    len = qm_snprintf(text, sizeof(text), "%lu", (unsigned long)value);
    if (len <= JOYINSIDE_CLIENT_SNPRINTF_ERROR_MAX || len >= (int)sizeof(text)) {
        return -QM_EIO;
    }
    return cJSON_AddStringToObject(object, name, text) ? QM_EOK : -QM_ENOMEM;
}

static int parse_u32_text(const char *text, uint32_t *value)
{
    uint32_t len;

    if (!text || !value) {
        return -QM_EINVAL;
    }

    len = qm_strlen(text);
    if (len == JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN || len > UINT8_MAX) {
        return -QM_EINVAL;
    }
    return qm_str2uint((char *)text, (uint8_t)len, value);
}

static int parse_u64_text(const char *text, uint64_t *value)
{
    uint32_t len;

    if (!text || !value) {
        return -QM_EINVAL;
    }

    len = qm_strlen(text);
    if (len == JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN || len > UINT8_MAX) {
        return -QM_EINVAL;
    }
    return qm_str2uint64((char *)text, (uint8_t)len, value);
}

static int parse_object_or_empty(const char *json, cJSON **out)
{
    if (!out) {
        return -QM_EINVAL;
    }
    *out = NULL;

    if (json && json[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        *out = cJSON_Parse(json);
        if (!*out || !cJSON_IsObject(*out)) {
            cJSON_Delete(*out);
            *out = NULL;
            return -QM_EINVAL;
        }
        return QM_EOK;
    }

    *out = cJSON_CreateObject();
    return *out ? QM_EOK : -QM_ENOMEM;
}

static uint32_t heartbeat_interval_ms(joyinside_client_handle_t handle)
{
    return handle->config.heartbeat.interval_ms ? handle->config.heartbeat.interval_ms :
                                                  CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_INTERVAL_MS;
}

static uint32_t heartbeat_timeout_ms(joyinside_client_handle_t handle)
{
    if (handle->config.heartbeat.timeout_ms) {
        return handle->config.heartbeat.timeout_ms;
    }
    return CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_TIMEOUT_MS;
}

static uint32_t heartbeat_tick_ms(joyinside_client_handle_t handle)
{
    uint32_t interval_ms = heartbeat_interval_ms(handle);
    uint32_t timeout_ms = heartbeat_timeout_ms(handle);

    if (timeout_ms == JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN || timeout_ms >= interval_ms) {
        return interval_ms;
    }
    return timeout_ms;
}

static uint32_t network_timeout_ms(joyinside_client_handle_t handle)
{
    return handle->config.network.timeout_ms ? handle->config.network.timeout_ms :
                                               CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS;
}

static uint32_t send_retry_count(joyinside_client_handle_t handle)
{
    return handle->config.network.send_retry_count ? handle->config.network.send_retry_count :
                                                     CONFIG_JOYINSIDE_CLIENT_SEND_RETRY_COUNT;
}

static uint32_t send_retry_delay_ms(joyinside_client_handle_t handle)
{
    return handle->config.network.send_retry_delay_ms ? handle->config.network.send_retry_delay_ms :
                                                        CONFIG_JOYINSIDE_CLIENT_SEND_RETRY_DELAY_MS;
}

static uint32_t ack_timeout_ms(joyinside_client_handle_t handle)
{
    return handle->config.network.ack_timeout_ms ?
           handle->config.network.ack_timeout_ms :
           CONFIG_JOYINSIDE_CLIENT_ACK_TIMEOUT_MS;
}

static uint32_t base64_decoded_len(const char *base64, uint32_t base64_len)
{
    uint32_t decoded_len;

    if (!base64 || base64_len == JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN ||
        base64_len % JOYINSIDE_CLIENT_BASE64_GROUP_CHARS != JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN) {
        return JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN;
    }

    decoded_len = (base64_len / JOYINSIDE_CLIENT_BASE64_GROUP_CHARS) *
                  JOYINSIDE_CLIENT_BASE64_GROUP_BYTES;
    if (base64[base64_len - 1U] == JOYINSIDE_CLIENT_BASE64_PAD_CHAR) {
        decoded_len--;
    }
    if (base64[base64_len - 2U] == JOYINSIDE_CLIENT_BASE64_PAD_CHAR) {
        decoded_len--;
    }
    return decoded_len;
}

static void core_task(void *arg);
static void process_audio(joyinside_client_handle_t handle,
                          const joyinside_client_core_event_t *event);
static int context_report_resend(joyinside_client_handle_t handle);
static int schedule_context_deadline(joyinside_client_handle_t handle);
static void cancel_context_deadline(joyinside_client_handle_t handle);
static int send_event_internal(joyinside_client_handle_t handle,
                               const char *event_type, const char *event_data_json,
                               uint32_t timeout_ms, qm_bool allow_before_ready);
/* Caller holds core_lock; encoding transitions also hold audio_send_lock.
 * 调用方持有 core_lock；涉及编码的状态切换还持有 audio_send_lock。 */
static void audio_admission_locked(joyinside_client_handle_t handle, qm_bool enabled)
{
    if (!enabled) {
        handle->audio_generation++;
        handle->audio_index = 0U;
        handle->vad_round_mid[0] = '\0';
    }
    handle->audio_accepting = enabled && !handle->core_stop_requested;
    handle->audio_binary = handle->config.voice.input_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY;
}

static void audio_admission(joyinside_client_handle_t handle, qm_bool enabled)
{
    qm_mutex_lock(&handle->audio_send_lock, QM_WAIT_FOREVER);
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    audio_admission_locked(handle, enabled);
    qm_mutex_unlock(&handle->core_lock);
    qm_mutex_unlock(&handle->audio_send_lock);
}
/* Publish transport readiness atomically across WS, core and application tasks. */
static qm_bool clear_connection_ready(joyinside_client_handle_t handle)
{
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    qm_bool previous = handle->connected;
    handle->connected = QM_FALSE;
    handle->output_binary = QM_FALSE;
    qm_mutex_unlock(&handle->core_lock);
    return previous;
}

static qm_bool connection_allowed(joyinside_client_handle_t handle)
{
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    qm_bool allowed = handle->network_state == JOYINSIDE_CLIENT_NETWORK_UP && !handle->session_replaced;
    qm_mutex_unlock(&handle->core_lock);
    return allowed;
}

static qm_bool core_stop_requested(joyinside_client_handle_t handle);
static int core_event_send(joyinside_client_handle_t handle,
                           joyinside_client_core_event_id_t id,
                           const char *mid);
/* Caller holds core_lock; coalesce wakes during continuous audio traffic. */
static int wake_core_locked(joyinside_client_handle_t handle)
{
    if (handle->core_wake_pending) return QM_EOK;
    int ret = qm_sem_signal(&handle->core_wake);
    if (ret == QM_EOK) handle->core_wake_pending = QM_TRUE;
    return ret;
}
static void schedule_protocol(joyinside_client_handle_t handle,
                              joyinside_client_core_event_id_t event,
                              uint32_t delay_ms, uint32_t period_ms)
{
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    qm_utils_time_countdown_ms(&handle->protocol_deadline[event], delay_ms);
    handle->protocol_period_ms[event] = period_ms;
    handle->protocol_active |= 1U << event;
    if (handle->core_queue_valid) (void)wake_core_locked(handle);
    qm_mutex_unlock(&handle->core_lock);
}

static void cancel_protocol(joyinside_client_handle_t handle,
                            joyinside_client_core_event_id_t event)
{
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->protocol_active &= ~(1U << event);
    handle->core_pending &= ~(1U << event);
    qm_mutex_unlock(&handle->core_lock);
}

int joyinside_client_schedule_audiobook_ping(joyinside_client_handle_t handle,
                                            uint32_t period_ms,
                                            int (*send_ping)(joyinside_client_handle_t))
{
    if (!handle) return -QM_EINVAL;
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->audiobook_ping = send_ping;
    qm_mutex_unlock(&handle->core_lock);
    if (period_ms && send_ping) {
        schedule_protocol(handle, JOYINSIDE_CLIENT_CORE_EVENT_AUDIOBOOK_PING, period_ms, period_ms);
    } else {
        cancel_protocol(handle, JOYINSIDE_CLIENT_CORE_EVENT_AUDIOBOOK_PING);
    }
    return QM_EOK;
}
static int send_heartbeat_ping(joyinside_client_handle_t handle);
static int handle_heartbeat_failure(joyinside_client_handle_t handle, const char *reason, int code);
static int handle_ack_timeout(joyinside_client_handle_t handle);
static void schedule_reconnect(joyinside_client_handle_t handle);
static void connect_once(joyinside_client_handle_t handle);
static void cancel_ack_deadline(joyinside_client_handle_t handle);
static void reset_heartbeat_state(joyinside_client_handle_t handle);
static int handle_heartbeat_pong(joyinside_client_handle_t handle, const char *mid);
static int schedule_ack_deadline(joyinside_client_handle_t handle);
static int send_voice_update(joyinside_client_handle_t handle,
                             const joyinside_client_voice_config_t *voice,
                             uint32_t timeout_ms);
static int update_voice_internal(joyinside_client_handle_t handle,
                                 qm_bool initial,
                                 uint32_t timeout_ms);
static void dispatch_event(joyinside_client_handle_t handle,
                           joyinside_client_event_id_t id,
                           const char *data,
                           uint32_t data_len,
                           const qm_websocket_event_data_t *ws_event);

static void dispatch_error(joyinside_client_handle_t handle,
                           joyinside_client_error_type_t type,
                           int code);
static int send_text_raw(joyinside_client_handle_t handle,
                         const char *text,
                         uint32_t timeout_ms);

static int print_and_send(joyinside_client_handle_t handle,
                          cJSON *root,
                          uint32_t timeout_ms,
                          qm_bool allow_before_ready)
{
    cJSON *content_type_item;
    const char *content_type = NULL;
    char *json;
    uint32_t json_len;
    int ret;

    if (!handle || !root) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }

    content_type_item = cJSON_GetObjectItemCaseSensitive(root, "contentType");
    if (cJSON_IsString(content_type_item)) {
        content_type = content_type_item->valuestring;
    }

    json = cJSON_PrintUnformatted(root);
    if (!json) {
        cJSON_Delete(root);
        return -QM_ENOMEM;
    }

    json_len = (uint32_t)qm_strlen(json);
    (void)json_len;
    (void)content_type;
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send json type=%s len=%lu timeout=%lums",
            safe_text(content_type), (unsigned long)json_len, (unsigned long)timeout_ms);
    ret = allow_before_ready ? send_text_raw(handle, json, timeout_ms) :
          joyinside_client_send_text(handle, json, timeout_ms);
    if (ret < QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "send json failed type=%s ret=%d", safe_text(content_type), ret);
    }
    cJSON_Delete(root);
    cJSON_free(json);
    return ret;
}

static int add_common_fields(joyinside_client_handle_t handle,
                             cJSON *root,
                             const char *content_type,
                             qm_bool include_uid)
{
    char mid[JOYINSIDE_CLIENT_UUID36_BUFFER_LEN];

    if (!handle || !root || !content_type ||
        content_type[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return -QM_EINVAL;
    }

    qm_utils_random_uuid(mid);
    if (!cJSON_AddStringToObject(root, "mid", mid) ||
        !cJSON_AddStringToObject(root, "contentType", content_type)) {
        return -QM_ENOMEM;
    }
    if (include_uid && handle->config.include_uid_in_messages &&
        handle->uid && !cJSON_AddStringToObject(root, "uid", handle->uid)) {
        return -QM_ENOMEM;
    }
    return QM_EOK;
}

static void free_config(joyinside_client_config_t *config)
{
    if (!config) {
        return;
    }

    qm_free((void *)config->voice.chat_role_code);
    qm_memset(config, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(*config));
}

static void clear_pending_ack(joyinside_client_handle_t handle)
{
    if (!handle) {
        return;
    }

    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->ack_pending = QM_FALSE;
    qm_mutex_unlock(&handle->core_lock);
}

static void vad_round_mid_reset(joyinside_client_handle_t handle)
{
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->vad_round_mid[0] = '\0';
    qm_mutex_unlock(&handle->core_lock);
}

/**
 * @brief Start one application-detected VAD speech segment. / 开始一段应用检测到的 VAD 语音段。
 *
 * @details The lock makes the new mid and reset index visible together to the
 * audio admission path. Frames already admitted retain their copied mid/index;
 * this function never rewrites a queued frame. 锁保证新 mid 与重置后的序号同时
 * 对音频准入可见；已入队帧保留自己的 mid/index，不会被本函数改写。
 */
int joyinside_client_vad_round_begin(joyinside_client_handle_t handle)
{
    if (!handle) return -QM_EINVAL;
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    int ret = !handle->config.voice.vad ? -QM_EINVAL :
              (!handle->audio_accepting ? -QM_EBUSY : QM_EOK);
    if (ret == QM_EOK) {
        qm_utils_random_uuid(handle->vad_round_mid);
        handle->audio_index = 0U;
    }
    qm_mutex_unlock(&handle->core_lock);
    return ret;
}

static int copy_config(joyinside_client_config_t *dst,
                       const joyinside_client_config_t *src)
{
    int ret = -QM_ENOMEM;

    if (!dst || !src) {
        return -QM_EINVAL;
    }

    *dst = *src;
    ret = normalize_voice(&dst->voice);
    dst->voice.chat_role_code = NULL;
    if (ret != QM_EOK) return ret;
    ret = -QM_ENOMEM;

    /* String ownership is explicit: config strings are copied with qm_strdup. */
    if (src->voice.chat_role_code) {
        dst->voice.chat_role_code = qm_strdup(src->voice.chat_role_code);
        if (!dst->voice.chat_role_code) goto fail;
    }
    return QM_EOK;

fail:
    free_config(dst);
    return ret;
}

static void clear_start_state(joyinside_client_handle_t handle)
{
    if (!handle) {
        return;
    }

    qm_free(handle->bot_id);
    qm_free(handle->uid);
    qm_free(handle->feature);
    qm_free(handle->client_type);
    qm_free(handle->location);
    qm_free(handle->session_id);
    qm_free(handle->request_id);
    qm_free(handle->access_token);
    handle->bot_id = NULL;
    handle->uid = NULL;
    handle->feature = NULL;
    handle->client_type = NULL;
    handle->location = NULL;
    handle->need_asr_partial = QM_FALSE;
    handle->session_id = NULL;
    handle->request_id = NULL;
    handle->access_token = NULL;
    handle->token_provider = NULL;
    handle->token_context = NULL;
    handle->force_token_refresh = QM_FALSE;
    handle->connection_attempted = QM_FALSE;
    handle->session_replaced = QM_FALSE;
    handle->heartbeat_mid[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    vad_round_mid_reset(handle);
    handle->output_binary = QM_FALSE;
    clear_pending_ack(handle);
}

/* Destroy a WebSocket only after its worker has acknowledged shutdown. */
static int stop_destroy_ws(joyinside_client_handle_t handle)
{
    int ret;

    if (!handle || !handle->ws) {
        return QM_EOK;
    }

    ret = qm_websocket_client_close(handle->ws, handle->config.network.timeout_ms);
    if (ret != QM_EOK && ret != -QM_EIO) {
        return ret;
    }

    qm_mutex_lock(&handle->tx_lock, QM_WAIT_FOREVER);
    ret = qm_websocket_client_destroy(handle->ws);
    if (ret != QM_EOK) {
        qm_mutex_unlock(&handle->tx_lock);
        return ret;
    }

    handle->ws = NULL;
    qm_mutex_unlock(&handle->tx_lock);
    return QM_EOK;
}

static int make_session_id(const char *bot_id, char **out)
{
    char uuid[JOYINSIDE_CLIENT_UUID36_BUFFER_LEN];
    int len;

    if (!bot_id || !out) {
        return -QM_EINVAL;
    }
    *out = NULL;

    qm_utils_random_uuid(uuid);
    len = qm_snprintf(NULL, JOYINSIDE_CLIENT_SNPRINTF_COUNT_ONLY, "%s_%s", bot_id, uuid);
    if (len <= JOYINSIDE_CLIENT_SNPRINTF_ERROR_MAX) {
        return -QM_EIO;
    }

    *out = (char *)qm_malloc((uint32_t)len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
    if (!*out) {
        return -QM_ENOMEM;
    }
    qm_snprintf(*out, (int)((uint32_t)len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN), "%s_%s", bot_id, uuid);
    return QM_EOK;
}

static int make_request_id(char **out)
{
    char uuid[JOYINSIDE_CLIENT_UUID36_BUFFER_LEN];

    if (!out) {
        return -QM_EINVAL;
    }
    *out = NULL;

    qm_utils_random_uuid(uuid);
    *out = qm_strdup(uuid);
    return *out ? QM_EOK : -QM_ENOMEM;
}

static int make_uid(const char *uid, char **out)
{
    if (!out || !uid || uid[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] ==
                     JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return -QM_EINVAL;
    }
    *out = NULL;

    *out = qm_strdup(uid);
    return *out ? QM_EOK : -QM_ENOMEM;
}

/**
 * @brief Copy one start request into a coherent WebSocket session state.
 *
 * @par 中文说明
 * 将一次启动请求复制为一致的 WebSocket 会话状态。
 *
 * sessionId is intentionally retained for multi-turn context, while requestId
 * is regenerated for every new connection attempt.
 */
static int set_start_state(joyinside_client_handle_t handle,
                           const joyinside_client_start_params_t *params)
{
    char *bot_id = NULL;
    char *uid = NULL;
    char *feature = NULL;
    char *client_type = NULL;
    char *location = NULL;
    char *session_id = NULL;
    char *request_id = NULL;
    char *access_token = NULL;
    int ret;

    if (!handle || !params || !params->bot_id || !params->uid ||
        params->bot_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        params->uid[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        (!params->token_provider && (!params->access_token || !params->access_token[0]))) {
        return -QM_EINVAL;
    }

    bot_id = qm_strdup(params->bot_id);
    if (!bot_id) {
        return -QM_ENOMEM;
    }

    if (params->session_id &&
        params->session_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        /* Reuse the caller-provided sessionId for multi-turn continuity. */
        session_id = qm_strdup(params->session_id);
        if (!session_id) {
            ret = -QM_ENOMEM;
            goto fail;
        }
    } else {
        ret = make_session_id(bot_id, &session_id);
        if (ret != QM_EOK) goto fail;
    }
    ret = make_request_id(&request_id);
    if (ret != QM_EOK) goto fail;
    if (!params->token_provider) {
        access_token = qm_strdup(params->access_token);
        if (!access_token) { ret = -QM_ENOMEM; goto fail; }
    }
    ret = make_uid(params->uid, &uid);
    if (ret != QM_EOK) goto fail;
    if (params->feature &&
        params->feature[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        feature = qm_strdup(params->feature);
        if (!feature) {
            ret = -QM_ENOMEM;
            goto fail;
        }
    }
    if (params->client_type &&
        params->client_type[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        client_type = qm_strdup(params->client_type);
        if (!client_type) {
            ret = -QM_ENOMEM;
            goto fail;
        }
    }
    if (params->location &&
        params->location[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        location = qm_strdup(params->location);
        if (!location) {
            ret = -QM_ENOMEM;
            goto fail;
        }
    }

    clear_start_state(handle);
    handle->bot_id = bot_id;
    handle->uid = uid;
    handle->feature = feature;
    handle->client_type = client_type;
    handle->location = location;
    handle->need_asr_partial = params->need_asr_partial;
    handle->session_id = session_id;
    handle->request_id = request_id;
    handle->access_token = access_token;
    handle->token_provider = params->token_provider;
    handle->token_context = params->token_context;
    return QM_EOK;

fail:
    qm_free(bot_id);
    qm_free(uid);
    qm_free(feature);
    qm_free(client_type);
    qm_free(location);
    qm_free(session_id);
    qm_free(request_id);
    qm_free(access_token);
    return ret;
}

/* JoyInside Client uses both numeric and string forms for response code in examples. */
static int json_get_code(cJSON *root, int *code)
{
    cJSON *value;

    if (!root || !code) {
        return -QM_EINVAL;
    }

    value = cJSON_GetObjectItemCaseSensitive(root, "code");
    if (!value) {
        *code = JOYINSIDE_CLIENT_RESPONSE_CODE_OK;
        return QM_EOK;
    }
    if (cJSON_IsNumber(value)) {
        *code = value->valueint;
        return QM_EOK;
    }
    if (cJSON_IsString(value) && value->valuestring) {
        uint32_t parsed;
        int ret = parse_u32_text(value->valuestring, &parsed);
        if (ret == QM_EOK) {
            *code = (int)parsed;
        }
        return ret;
    }
    return -QM_EINVAL;
}

static uint32_t json_get_uint(cJSON *object, const char *name)
{
    cJSON *value;

    if (!object || !name) {
        return JOYINSIDE_CLIENT_UINT_ABSENT;
    }

    value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(value) && value->valuedouble >= JOYINSIDE_CLIENT_UNSIGNED_VALUE_MIN &&
        value->valuedouble <= UINT32_MAX) {
        return (uint32_t)value->valuedouble;
    }
    if (cJSON_IsString(value) && value->valuestring) {
        uint32_t parsed;
        return parse_u32_text(value->valuestring, &parsed) == QM_EOK ?
               parsed : JOYINSIDE_CLIENT_UINT_ABSENT;
    }
    return JOYINSIDE_CLIENT_UINT_ABSENT;
}

static uint64_t json_get_uint64(cJSON *object, const char *name)
{
    cJSON *value;

    if (!object || !name) {
        return JOYINSIDE_CLIENT_UINT64_ABSENT;
    }

    value = cJSON_GetObjectItemCaseSensitive(object, name);
    if (cJSON_IsNumber(value) && value->valuedouble >= JOYINSIDE_CLIENT_UNSIGNED_VALUE_MIN &&
        value->valuedouble < (double)UINT64_MAX) {
        return (uint64_t)value->valuedouble;
    }
    if (cJSON_IsString(value) && value->valuestring) {
        uint64_t parsed;
        return parse_u64_text(value->valuestring, &parsed) == QM_EOK ?
               parsed : JOYINSIDE_CLIENT_UINT64_ABSENT;
    }
    return JOYINSIDE_CLIENT_UINT64_ABSENT;
}

static joyinside_server_event_type_t server_event_type_from_string(const char *event_type)
{
    if (!event_type || event_type[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return JOYINSIDE_SERVER_EVENT_TYPE_UNKNOWN;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_CFG_BOT_EVENT) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_CFG_BOT;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_VOICE_CHAT_UPDATED) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_VOICE_CHAT_UPDATED;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_TTS_COMPLETE) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_TTS_COMPLETE;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_TURN_COMPLETE) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_TURN_COMPLETE;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_CALL_AGENT_START) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_CALL_AGENT_START;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_CALL_AGENT_INTERRUPTED) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_CALL_AGENT_INTERRUPTED;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_CALL_SKILL) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_CALL_SKILL;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_TTS_SENTENCE_START) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_TTS_SENTENCE_START;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_EMPTY_CONTENT) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_EMPTY_CONTENT;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_USER_AUDIT_FAIL) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_USER_AUDIT_FAIL;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_REPEAT_CLIENT_SESSION) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_REPEAT_CLIENT_SESSION;
    }
    if (qm_strcmp(event_type, JOYINSIDE_SERVER_EVENT_CHAT_CONTEXT_RECEIVED) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_SERVER_EVENT_TYPE_CHAT_CONTEXT_RECEIVED;
    }
    return JOYINSIDE_SERVER_EVENT_TYPE_CUSTOM;
}

static joyinside_client_text_type_t text_type_from_string(const char *content_type)
{
    if (!content_type ||
        content_type[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return JOYINSIDE_CLIENT_TEXT_TYPE_UNKNOWN;
    }
    if (qm_strcmp(content_type, JOYINSIDE_CLIENT_CONTENT_TYPE_ASR) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_CLIENT_TEXT_TYPE_ASR;
    }
    if (qm_strcmp(content_type, JOYINSIDE_CLIENT_CONTENT_TYPE_AGENT) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_CLIENT_TEXT_TYPE_AGENT;
    }
    if (qm_strcmp(content_type, JOYINSIDE_CLIENT_CONTENT_TYPE_ACTIVITY) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_CLIENT_TEXT_TYPE_ACTIVITY;
    }
    if (qm_strcmp(content_type, JOYINSIDE_CLIENT_CONTENT_TYPE_TTS) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_CLIENT_TEXT_TYPE_TTS;
    }
    if (qm_strcmp(content_type, JOYINSIDE_CLIENT_CONTENT_TYPE_PONG) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
        return JOYINSIDE_CLIENT_TEXT_TYPE_PONG;
    }
    return JOYINSIDE_CLIENT_TEXT_TYPE_UNKNOWN;
}

static void parse_bot_config(cJSON *content, joyinside_client_bot_config_t *config)
{
    cJSON *event_data;
    cJSON *timbre;
    cJSON *tts;
    cJSON *item;

    if (!content || !config) {
        return;
    }

    event_data = cJSON_GetObjectItemCaseSensitive(content, "eventData");
    if (!cJSON_IsObject(event_data)) {
        return;
    }

    timbre = cJSON_GetObjectItemCaseSensitive(event_data, "timbre");
    if (cJSON_IsObject(timbre)) {
        item = cJSON_GetObjectItemCaseSensitive(timbre, "voiceName");
        if (cJSON_IsString(item)) {
            config->voice_name = item->valuestring;
        }
        item = cJSON_GetObjectItemCaseSensitive(timbre, "voiceSpeed");
        if (cJSON_IsNumber(item)) {
            config->voice_speed = (float)item->valuedouble;
        }
    }

    tts = cJSON_GetObjectItemCaseSensitive(event_data, "tts");
    if (cJSON_IsObject(tts)) {
        item = cJSON_GetObjectItemCaseSensitive(tts, "aue");
        if (cJSON_IsString(item)) {
            config->tts_aue = item->valuestring;
        }
        config->tts_bit = json_get_uint(tts, "bit");
        config->tts_channels = json_get_uint(tts, "channels");
        config->tts_sample_rate = json_get_uint(tts, "sr");
    }

    item = cJSON_GetObjectItemCaseSensitive(event_data, "deviceModel");
    if (cJSON_IsString(item)) {
        config->device_model = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(event_data, "deviceId");
    if (cJSON_IsString(item)) {
        config->device_id = item->valuestring;
    }
}

static void parse_asr_result(cJSON *content, joyinside_client_asr_result_t *asr)
{
    cJSON *item;

    if (!content || !asr) {
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive(content, "text");
    if (cJSON_IsString(item)) {
        asr->text = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(content, "textType");
    if (cJSON_IsString(item)) {
        asr->text_type = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(content, "lang");
    if (cJSON_IsString(item)) {
        asr->lang = item->valuestring;
    }
}

static void parse_agent_result(cJSON *content, joyinside_client_agent_result_t *agent)
{
    cJSON *item;

    if (!content || !agent) {
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive(content, "roundId");
    if (cJSON_IsString(item)) {
        agent->round_id = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(content, "role");
    if (cJSON_IsString(item)) {
        agent->role = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(content, "content");
    if (cJSON_IsString(item)) {
        agent->content = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(content, "reasoningContent");
    if (cJSON_IsString(item)) {
        agent->reasoning_content = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(content, "finishReason");
    if (cJSON_IsString(item)) {
        agent->finish_reason = item->valuestring;
    }
}

static void parse_tts_result(cJSON *content, joyinside_client_tts_result_t *tts)
{
    cJSON *item;
    uint32_t base64_len;

    if (!content || !tts) {
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive(content, "roundId");
    if (cJSON_IsString(item)) {
        tts->round_id = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(content, "audioBase64");
    if (cJSON_IsString(item)) {
        tts->audio_base64 = item->valuestring;
        base64_len = (uint32_t)qm_strlen(tts->audio_base64);
        tts->audio_len = base64_decoded_len(tts->audio_base64, base64_len);
    }
    item = cJSON_GetObjectItemCaseSensitive(content, "audioAue");
    if (cJSON_IsString(item)) {
        tts->audio_aue = item->valuestring;
    }
    tts->audio_duration_ms = json_get_uint(content, "audioDuration");

    item = cJSON_GetObjectItemCaseSensitive(content, "finish");
    if (cJSON_IsBool(item)) {
        tts->finish = cJSON_IsTrue(item) ? QM_TRUE : QM_FALSE;
    }
}

static qm_bool audio_receiving(joyinside_client_handle_t handle, qm_bool binary)
{
    /* Do not deliver a packet decoded under an old or unconfirmed output
     * representation while a voice update is in flight. 协商中或协商失败的旧
     * 下行格式不能进入应用音频路径。 */
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    qm_bool allowed = handle->connected && !handle->ack_pending && !handle->queued_voice &&
                      !handle->core_stop_requested && handle->output_binary == binary;
    qm_mutex_unlock(&handle->core_lock);
    return allowed;
}

static void dispatch_tts_audio(joyinside_client_handle_t handle,
                               const joyinside_client_tts_result_t *tts,
                               const qm_websocket_event_data_t *ws_event)
{
    uint8_t *audio;
    uint32_t base64_len;
    uint32_t output_len = JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN;
    int ret;

    if (!handle || !audio_receiving(handle, QM_FALSE) || !tts || !tts->audio_base64 ||
        tts->audio_len == JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN) {
        return;
    }

    base64_len = (uint32_t)qm_strlen(tts->audio_base64);
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK, "tts audio decode start base64_len=%lu expected_len=%lu duration=%lums finish=%s",
            (unsigned long)base64_len, (unsigned long)tts->audio_len,
            (unsigned long)tts->audio_duration_ms, bool_text(tts->finish));
    audio = (uint8_t *)qm_malloc(tts->audio_len);
    if (!audio) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "tts audio decode alloc failed len=%lu", (unsigned long)tts->audio_len);
        dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_BUFFER, -QM_ENOMEM);
        return;
    }

    ret = qm_utils_base64decode((const uint8_t *)tts->audio_base64, base64_len,
                                tts->audio_len, audio, &output_len);
    if (ret != QM_EOK || output_len == JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN) {
        qm_free(audio);
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "tts audio decode failed ret=%d output_len=%lu",
                ret, (unsigned long)output_len);
        dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_PROTOCOL, -QM_EINVAL);
        return;
    }

    (void)ws_event;
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK, "tts audio decoded len=%lu", (unsigned long)output_len);
    dispatch_event(handle, JOYINSIDE_CLIENT_EVENT_AUDIO, (const char *)audio, output_len, NULL);
    qm_free(audio);
}

static cJSON *binary_field(cJSON *audio, const char *direction)
{
    cJSON *global = cJSON_GetObjectItemCaseSensitive(audio, "binary");
    return global ? global : cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(audio, direction), "binary");
}

static void parse_voice_updated(cJSON *content, joyinside_client_voice_updated_t *updated)
{
    cJSON *event_data;
    cJSON *audio;
    cJSON *input;
    cJSON *output;
    cJSON *item;

    if (!content || !updated) {
        return;
    }

    event_data = cJSON_GetObjectItemCaseSensitive(content, "eventData");
    audio = cJSON_GetObjectItemCaseSensitive(event_data, "audio");
    if (!cJSON_IsObject(audio)) {
        return;
    }

    updated->input_binary = cJSON_IsTrue(binary_field(audio, "input"));
    updated->output_binary = cJSON_IsTrue(binary_field(audio, "output"));
    updated->vad = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(audio, "vad"));
    input = cJSON_GetObjectItemCaseSensitive(audio, "input");
    if (cJSON_IsObject(input)) {
        item = cJSON_GetObjectItemCaseSensitive(input, "codec");
        if (cJSON_IsString(item)) {
            updated->input_codec = item->valuestring;
        }
        updated->input_sample_rate = json_get_uint(input, "sampleRate");
        updated->input_frame_size = json_get_uint(input, "frameSize");
    }

    output = cJSON_GetObjectItemCaseSensitive(audio, "output");
    if (cJSON_IsObject(output)) {
        item = cJSON_GetObjectItemCaseSensitive(output, "codec");
        if (cJSON_IsString(item)) {
            updated->output_codec = item->valuestring;
        }
        updated->output_stream = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(output, "stream"));
        updated->output_sample_rate = json_get_uint(output, "sampleRate");
        updated->output_frame_size_ms = json_get_uint(output, "frameSizeMs");
        item = cJSON_GetObjectItemCaseSensitive(output, "enableOpusCbr");
        if (cJSON_IsBool(item)) {
            updated->output_opus_cbr = cJSON_IsTrue(item) ? QM_TRUE : QM_FALSE;
        }
    }
}

static void parse_server_event(cJSON *content, joyinside_server_event_t *event)
{
    cJSON *event_data;
    cJSON *item;

    if (!content || !event) {
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive(content, "roundId");
    if (cJSON_IsString(item)) {
        event->round_id = item->valuestring;
    }

    event_data = cJSON_GetObjectItemCaseSensitive(content, "eventData");
    if (!cJSON_IsObject(event_data)) {
        /* CLIENT_CHAT_CONTEXT_RECEIVED uses activityData instead of eventData. */
        event_data = cJSON_GetObjectItemCaseSensitive(content, "activityData");
    }
    if (!cJSON_IsObject(event_data)) {
        return;
    }
    event->event_data = event_data;

    item = cJSON_GetObjectItemCaseSensitive(event_data, "input");
    if (cJSON_IsString(item)) {
        event->input = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(event_data, "text");
    if (cJSON_IsString(item)) {
        event->text = item->valuestring;
    }
    event->start_time_ms = json_get_uint64(event_data, "startTime");
    event->time_ms = json_get_uint64(event_data, "time");
}

static joyinside_client_expression_t joyinside_client_expression_from_code(const char *code)
{
    static const struct {
        const char *code;
        joyinside_client_expression_t expression;
    } known_expressions[] = {
        {"angry", JOYINSIDE_CLIENT_EXPRESSION_ANGRY},
        {"anxious", JOYINSIDE_CLIENT_EXPRESSION_ANXIOUS},
        {"blamed", JOYINSIDE_CLIENT_EXPRESSION_BLAMED},
        {"confused", JOYINSIDE_CLIENT_EXPRESSION_CONFUSED},
        {"coquetry", JOYINSIDE_CLIENT_EXPRESSION_COQUETRY},
        {"disappoint", JOYINSIDE_CLIENT_EXPRESSION_DISAPPOINT},
        {"disdainful", JOYINSIDE_CLIENT_EXPRESSION_DISDAINFUL},
        {"exciting", JOYINSIDE_CLIENT_EXPRESSION_EXCITING},
        {"expect", JOYINSIDE_CLIENT_EXPRESSION_EXPECT},
        {"fear", JOYINSIDE_CLIENT_EXPRESSION_FEAR},
        {"happy", JOYINSIDE_CLIENT_EXPRESSION_HAPPY},
        {"hate", JOYINSIDE_CLIENT_EXPRESSION_HATE},
        {"hesitate", JOYINSIDE_CLIENT_EXPRESSION_HESITATE},
        {"inquisitive", JOYINSIDE_CLIENT_EXPRESSION_INQUISITIVE},
        {"peace", JOYINSIDE_CLIENT_EXPRESSION_PEACE},
        {"sad", JOYINSIDE_CLIENT_EXPRESSION_SAD},
        {"shock", JOYINSIDE_CLIENT_EXPRESSION_SHOCK},
        {"shy", JOYINSIDE_CLIENT_EXPRESSION_SHY},
        {"sick", JOYINSIDE_CLIENT_EXPRESSION_SICK},
        {"sorrowful", JOYINSIDE_CLIENT_EXPRESSION_SORROWFUL},
        {"wronged", JOYINSIDE_CLIENT_EXPRESSION_WRONGED},
        {"disagree", JOYINSIDE_CLIENT_EXPRESSION_DISAGREE},
        {"nod", JOYINSIDE_CLIENT_EXPRESSION_NOD},
    };
    uint32_t index;

    if (code == NULL) {
        return JOYINSIDE_CLIENT_EXPRESSION_UNKNOWN;
    }
    for (index = 0U; index < sizeof(known_expressions) / sizeof(known_expressions[0]); index++) {
        uint32_t code_length = qm_strlen(known_expressions[index].code);

        if (qm_strncmp(code, known_expressions[index].code, code_length) ==
                JOYINSIDE_CLIENT_STRCMP_EQUAL &&
            (code[code_length] == '\0' ||
             (code[code_length] == '_' && code[code_length + 1U] == '1' &&
              code[code_length + 2U] == '\0'))) {
            return known_expressions[index].expression;
        }
    }
    return JOYINSIDE_CLIENT_EXPRESSION_UNKNOWN;
}

static joyinside_client_expression_t speech_expression_from_text(const char *text)
{
    /* TTS_EMOTION is a spoken description in documented examples, whereas
     * EMOTION is a provider code. Keep their parsers separate so a new visual
     * code cannot accidentally change speech rendering. 文档中的 TTS_EMOTION
     * 是语义描述，EMOTION 才是供应商编码，两者须独立解析。 */
    static const struct { const char *text; joyinside_client_expression_t expression; } meanings[] = {
        {"高兴", JOYINSIDE_CLIENT_EXPRESSION_HAPPY}, {"开心", JOYINSIDE_CLIENT_EXPRESSION_HAPPY},
        {"愤怒", JOYINSIDE_CLIENT_EXPRESSION_ANGRY}, {"悲伤", JOYINSIDE_CLIENT_EXPRESSION_SAD},
        {"伤心", JOYINSIDE_CLIENT_EXPRESSION_SAD}, {"恐惧", JOYINSIDE_CLIENT_EXPRESSION_FEAR},
        {"厌恶", JOYINSIDE_CLIENT_EXPRESSION_HATE}, {"惊讶", JOYINSIDE_CLIENT_EXPRESSION_SHOCK},
        {"平静", JOYINSIDE_CLIENT_EXPRESSION_PEACE}, {"中性", JOYINSIDE_CLIENT_EXPRESSION_PEACE},
    };
    if (!text) return JOYINSIDE_CLIENT_EXPRESSION_UNKNOWN;
    for (uint32_t i = 0; i < sizeof(meanings) / sizeof(meanings[0]); ++i)
        if (!qm_strcmp(text, meanings[i].text)) return meanings[i].expression;
    return joyinside_client_expression_from_code(text);
}

static void parse_call_skill(cJSON *content, joyinside_client_skill_t *skill)
{
    cJSON *event_data;
    cJSON *data;
    cJSON *item;
    cJSON *entry;
    uint32_t count = 0U;

    if (!content || !skill) {
        return;
    }
    qm_memset(skill, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(*skill));

    event_data = cJSON_GetObjectItemCaseSensitive(content, "eventData");
    if (!cJSON_IsObject(event_data)) {
        return;
    }
    item = cJSON_GetObjectItemCaseSensitive(event_data, "msg");
    if (cJSON_IsString(item)) {
        skill->message = item->valuestring;
    }
    data = cJSON_GetObjectItemCaseSensitive(event_data, "data");
    if (!cJSON_IsObject(data)) {
        return;
    }

    item = cJSON_GetObjectItemCaseSensitive(data, "domain");
    if (cJSON_IsString(item)) {
        skill->domain = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(data, "intentCode");
    if (cJSON_IsString(item)) {
        skill->intent_code = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(data, "intentClassify");
    if (cJSON_IsString(item)) {
        skill->intent_classify = item->valuestring;
    }
    skill->args = cJSON_GetObjectItemCaseSensitive(data, "args");
    if (cJSON_IsObject(skill->args)) {
        item = cJSON_GetObjectItemCaseSensitive(skill->args, "EMOTION");
        if (cJSON_IsString(item)) {
            skill->expression_code = item->valuestring;
            skill->expression = joyinside_client_expression_from_code(skill->expression_code);
        }
        item = cJSON_GetObjectItemCaseSensitive(skill->args, "TTS_EMOTION");
        if (cJSON_IsString(item)) {
            skill->tts_expression_code = item->valuestring;
            skill->tts_expression = speech_expression_from_text(
                skill->tts_expression_code);
        }
    }
    skill->instructions = cJSON_GetObjectItemCaseSensitive(data, "instructions");
    if (cJSON_IsArray(skill->instructions)) {
        cJSON_ArrayForEach(entry, (cJSON *)skill->instructions) {
            count++;
        }
    }
    skill->instruction_count = count;
}

/**
 * @brief Extract one borrowed normal or alarm-V2 instruction. / 提取一条借用的普通或闹钟 V2 指令。
 *
 * @details The domain selects the mutually exclusive wire shape. Keeping this
 * decision here prevents application code from treating alarm params as a
 * generic action/args payload. domain 决定两种互斥的协议结构，避免应用将
 * alarm 的 params 误读成通用 action/args。
 */
int joyinside_client_skill_instruction(const joyinside_client_skill_t *skill,
                                       uint32_t index,
                                       joyinside_client_instruction_t *instruction)
{
    if (!skill || !instruction) return -QM_EINVAL;
    qm_memset(instruction, 0, sizeof(*instruction));
    if (!cJSON_IsArray(skill->instructions) || index >= skill->instruction_count || index > INT32_MAX)
        return -QM_ENOENT;
    const cJSON *entry = cJSON_GetArrayItem(skill->instructions, (int)index);
    qm_bool alarm = skill->domain && !qm_strcmp(skill->domain, "alarm");
    instruction->type = alarm ? cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "type")) : NULL;
    instruction->operation = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, alarm ? "operation" : "action"));
    instruction->description = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(entry, "desc"));
    instruction->params = cJSON_GetObjectItemCaseSensitive(entry, alarm ? "params" : "args");
    if (!instruction->operation || !instruction->operation[0] || !cJSON_IsObject(instruction->params) ||
        (alarm && (!instruction->type || !instruction->type[0]))) return -QM_EINVAL;
    return QM_EOK;
}

static void fill_chunk(joyinside_client_payload_chunk_t *chunk,
                       const char *data,
                       uint32_t data_len,
                       const qm_websocket_event_data_t *ws_event)
{
    if (!chunk) {
        return;
    }

    chunk->data = data;
    chunk->data_len = data_len;
    chunk->total_data_len = ws_event ? (uint32_t)ws_event->payload_len : data_len;
    chunk->current_data_offset = ws_event ? (uint32_t)ws_event->payload_offset :
                                           JOYINSIDE_CLIENT_PAYLOAD_OFFSET_START;
}

static void parse_message_meta(cJSON *root, joyinside_client_message_meta_t *meta)
{
    cJSON *item;

    if (!root || !meta) {
        return;
    }

    json_get_code(root, &meta->code);
    meta->timestamp_ms = json_get_uint64(root, "t");
    item = cJSON_GetObjectItemCaseSensitive(root, "msg");
    if (cJSON_IsString(item)) {
        meta->msg = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "mid");
    if (cJSON_IsString(item)) {
        meta->mid = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "uid");
    if (cJSON_IsString(item)) {
        meta->uid = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "requestId");
    if (cJSON_IsString(item)) {
        meta->request_id = item->valuestring;
    }
    item = cJSON_GetObjectItemCaseSensitive(root, "contentType");
    if (cJSON_IsString(item)) {
        meta->content_type = item->valuestring;
    }
}

static qm_bool ack_bool(cJSON *object, const char *key, qm_bool expected)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    return value ? cJSON_IsBool(value) && cJSON_IsTrue(value) == !!expected : !expected;
}

static qm_bool ack_number(cJSON *object, const char *key, float expected)
{
    const cJSON *value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (expected == 0.0f) return QM_TRUE;
    double difference = cJSON_GetNumberValue(value) - expected;
    return cJSON_IsNumber(value) && difference > -0.0001 && difference < 0.0001;
}

/* Caller holds audio_send_lock and core_lock. A missing optional value only
 * matches an omitted request; an explicitly requested non-default must echo.
 * 调用方持有两个状态锁；可选字段仅在请求省略时允许 ACK 缺省，显式请求必须回显。 */
static qm_bool voice_ack_matches(joyinside_client_handle_t handle, cJSON *content,
                                  const joyinside_client_voice_updated_t *voice)
{
    cJSON *data = cJSON_GetObjectItemCaseSensitive(content, "eventData");
    cJSON *audio = cJSON_GetObjectItemCaseSensitive(data, "audio");
    cJSON *output = cJSON_GetObjectItemCaseSensitive(audio, "output");
    cJSON *timbre = cJSON_GetObjectItemCaseSensitive(audio, "timbre");
    const char *role = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(
        cJSON_GetObjectItemCaseSensitive(data, "chat"), "roleCode"));
    const joyinside_client_voice_config_t *expected = &handle->config.voice;
    return cJSON_IsBool(binary_field(audio, "input")) && cJSON_IsBool(binary_field(audio, "output")) &&
        voice->input_binary == (expected->input_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY) &&
        voice->output_binary == (expected->output_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY) &&
        voice->input_codec && !qm_strcmp(voice->input_codec, codec_name(expected->input_codec)) &&
        voice->output_codec && !qm_strcmp(voice->output_codec, codec_name(expected->output_codec)) &&
        voice->input_sample_rate == expected->input_sample_rate &&
        voice->output_sample_rate == expected->output_sample_rate &&
        (!expected->input_frame_size || expected->input_frame_size == voice->input_frame_size) &&
        (!expected->output_frame_size_ms || expected->output_frame_size_ms == voice->output_frame_size_ms) &&
        ack_bool(audio, "vad", expected->vad) && ack_bool(output, "stream", expected->output_stream) &&
        ack_bool(output, "enableOpusCbr", expected->output_opus_cbr) &&
        ack_number(timbre, "voiceSpeed", expected->voice_speed) &&
        ack_number(timbre, "voiceVolume", expected->voice_volume) &&
        (!expected->chat_role_code || !expected->chat_role_code[0] ||
         (role && !qm_strcmp(role, expected->chat_role_code)));
}

/**
 * @brief Parse and synchronously dispatch one normalized JoyInside event.
 *
 * @par 中文说明
 * 解析并同步派发一条标准化 JoyInside 事件。
 *
 * The cJSON root and all pointers exposed through @p event are released when
 * this function returns. Callback consumers must copy asynchronous payloads.
 */
static void dispatch_event(joyinside_client_handle_t handle,
                        joyinside_client_event_id_t id,
                        const char *data,
                        uint32_t data_len,
                        const qm_websocket_event_data_t *ws_event)
{
    joyinside_client_event_t event;
    cJSON *root = NULL;
    cJSON *content = NULL;
    cJSON *item = NULL;
    joyinside_client_message_meta_t *meta = NULL;
    joyinside_client_payload_chunk_t *chunk = NULL;

    if (!handle) {
        return;
    }

    qm_memset(&event, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(event));
    event.event_id = id;
    event.handle = handle;
    event.user_context = handle->config.user_context;
    if (id == JOYINSIDE_CLIENT_EVENT_AUDIO) {
        fill_chunk(&event.payload.audio.chunk, data, data_len, ws_event);
    }

    /* Parse text metadata before dispatch so applications do not duplicate JSON parsing. */
    if ((id == JOYINSIDE_CLIENT_EVENT_TEXT || id == JOYINSIDE_CLIENT_EVENT_SERVER_EVENT) && data &&
        data_len > JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN) {
        JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_PAYLOAD, "recv text raw len=%lu data=%.*s",
                (unsigned long)data_len, (int)data_len, data);
        root = cJSON_ParseWithLengthOpts(data, data_len + 1U, NULL, 1);
        if (cJSON_IsObject(root)) {
            parse_message_meta(root, &event.payload.text.meta);
            /* Handbook 6.1: a normalized non-200 downlink code is logged and
             * excluded from business dispatch. */
            if (event.payload.text.meta.code != JOYINSIDE_CLIENT_RESPONSE_CODE_OK) {
                JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK,
                        "downlink code=%d msg=%s request=%s ignored, not dispatched",
                        event.payload.text.meta.code,
                        safe_text(event.payload.text.meta.msg),
                        safe_text(event.payload.text.meta.request_id));
                cJSON_Delete(root);
                return;
            }
            if (event.payload.text.meta.content_type &&
                qm_strcmp(event.payload.text.meta.content_type,
                          JOYINSIDE_CLIENT_CONTENT_TYPE_EVENT) == JOYINSIDE_CLIENT_STRCMP_EQUAL) {
                event.event_id = JOYINSIDE_CLIENT_EVENT_SERVER_EVENT;
                meta = &event.payload.server_event.meta;
                chunk = &event.payload.server_event.chunk;
                *meta = event.payload.text.meta;
                fill_chunk(chunk, data, data_len, ws_event);
                content = cJSON_GetObjectItemCaseSensitive(root, "content");
                item = cJSON_GetObjectItemCaseSensitive(content, "eventType");
                if (!cJSON_IsString(item)) {
                    /* CLIENT_CHAT_CONTEXT_RECEIVED carries activityType instead of eventType. */
                    item = cJSON_GetObjectItemCaseSensitive(content, "activityType");
                }
                if (cJSON_IsString(item)) {
                    event.payload.server_event.type_name = item->valuestring;
                }
                event.payload.server_event.type =
                    server_event_type_from_string(event.payload.server_event.type_name);
                parse_server_event(content, &event.payload.server_event.common);
                if (event.payload.server_event.type == JOYINSIDE_SERVER_EVENT_TYPE_CFG_BOT) {
                    parse_bot_config(content, &event.payload.server_event.content.bot_config);
                    /* Server defaults are informational; only SERVER_VOICE_CHAT_UPDATED is authoritative. */
                    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK, "CFG_BOT_EVENT received, voice config not applied");
                } else if (event.payload.server_event.type ==
                           JOYINSIDE_SERVER_EVENT_TYPE_VOICE_CHAT_UPDATED) {
                    parse_voice_updated(content, &event.payload.server_event.content.voice_updated);
                    /* Serialize acknowledgement with audio admission: from this
                     * point a successful ACK publishes both the new downlink
                     * representation and the renewed uplink generation. ACK
                     * 与音频准入串行，成功时同时发布新下行格式和新上行 generation。 */
                    qm_mutex_lock(&handle->audio_send_lock, QM_WAIT_FOREVER);
                    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
                    qm_bool pending = handle->ack_pending && !handle->core_stop_requested;
                    qm_bool matches = pending && voice_ack_matches(handle, content, &event.payload.server_event.content.voice_updated);
                    qm_bool was_connected = handle->connected;
                    if (matches) {
                        handle->ack_pending = QM_FALSE;
                        handle->connected = QM_TRUE;
                        handle->output_binary = event.payload.server_event.content.voice_updated.output_binary;
                        audio_admission_locked(handle, QM_TRUE);
                    }
                    qm_mutex_unlock(&handle->core_lock);
                    if (matches) cancel_ack_deadline(handle);
                    qm_mutex_unlock(&handle->audio_send_lock);
                    if (pending && !matches) {
                        dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_PROTOCOL, -QM_EINVAL);
                        (void)core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_ACK_TIMEOUT, NULL);
                    } else if (matches) {
                        dispatch_event(handle, was_connected ? JOYINSIDE_CLIENT_EVENT_VOICE_UPDATED :
                                       JOYINSIDE_CLIENT_EVENT_CONNECTED, NULL, 0U, ws_event);
                        (void)core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_CONTEXT_REPORT, NULL);
                        (void)schedule_context_deadline(handle);
                    }
                    cJSON_Delete(root);
                    return;
                } else if (event.payload.server_event.type ==
                           JOYINSIDE_SERVER_EVENT_TYPE_CALL_SKILL) {
                    parse_call_skill(content, &event.payload.server_event.content.skill);
                    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK,
                            "CALL_SKILL_EVENT domain=%s intent=%s classify=%s instructions=%lu",
                            safe_text(event.payload.server_event.content.skill.domain),
                            safe_text(event.payload.server_event.content.skill.intent_code),
                            safe_text(event.payload.server_event.content.skill.intent_classify),
                            (unsigned long)event.payload.server_event.content.skill.instruction_count);
                }
                if (event.payload.server_event.type == JOYINSIDE_SERVER_EVENT_TYPE_REPEAT_CLIENT_SESSION) {
                    /* Do not fight the replacement connection in a reconnect loop. */
                    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
                    handle->session_replaced = QM_TRUE;
                    qm_mutex_unlock(&handle->core_lock);
                    audio_admission(handle, QM_FALSE);
                    (void)qm_websocket_client_network_update(handle->ws, QM_WEBSOCKET_NETWORK_DOWN);
                }
                JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK,
                        "event server type=%d name=%s code=%d msg=%s request=%s mid=%s len=%lu offset=%lu total=%lu",
                        event.payload.server_event.type,
                        safe_text(event.payload.server_event.type_name),
                        meta->code, safe_text(meta->msg),
                        safe_text(meta->request_id), safe_text(meta->mid),
                        (unsigned long)data_len,
                        (unsigned long)chunk->current_data_offset,
                        (unsigned long)chunk->total_data_len);
                if (event.payload.server_event.type ==
                    JOYINSIDE_SERVER_EVENT_TYPE_TTS_COMPLETE) {
                    audio_rx_stats_finish(handle);
                }
            } else {
                fill_chunk(&event.payload.text.chunk, data, data_len, ws_event);
                event.payload.text.text_type =
                    text_type_from_string(event.payload.text.meta.content_type);
                content = cJSON_GetObjectItemCaseSensitive(root, "content");
                switch (event.payload.text.text_type) {
                case JOYINSIDE_CLIENT_TEXT_TYPE_PONG:
                    if (core_event_send(handle,
                                                 JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_PONG,
                                                 event.payload.text.meta.mid) != QM_EOK) {
                        JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
                                "heartbeat PONG dropped while core stopped mid=%s",
                                safe_text(event.payload.text.meta.mid));
                    }
                    break;
                case JOYINSIDE_CLIENT_TEXT_TYPE_ASR:
                    audio_rx_stats_reset(handle);
                    parse_asr_result(content, &event.payload.text.content.asr);
                    break;
                case JOYINSIDE_CLIENT_TEXT_TYPE_AGENT:
                case JOYINSIDE_CLIENT_TEXT_TYPE_ACTIVITY:
                    parse_agent_result(content, &event.payload.text.content.agent);
                    break;
                case JOYINSIDE_CLIENT_TEXT_TYPE_TTS:
                    parse_tts_result(content, &event.payload.text.content.tts);
                    dispatch_tts_audio(handle, &event.payload.text.content.tts, ws_event);
                    break;
                default:
                    break;
                }
                int extension_ret = joyinside_client_audiobook_handle_message(handle, root);
                if (extension_ret == QM_EOK) extension_ret = joyinside_client_print_handle_message(handle, root);
                if (extension_ret != QM_EOK) dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_PROTOCOL, extension_ret);
                JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK,
                        "event text type=%d content_type=%s code=%d msg=%s request=%s mid=%s len=%lu offset=%lu total=%lu",
                        event.payload.text.text_type,
                        safe_text(event.payload.text.meta.content_type),
                        event.payload.text.meta.code,
                        safe_text(event.payload.text.meta.msg),
                        safe_text(event.payload.text.meta.request_id),
                        safe_text(event.payload.text.meta.mid),
                        (unsigned long)data_len,
                        (unsigned long)event.payload.text.chunk.current_data_offset,
                        (unsigned long)event.payload.text.chunk.total_data_len);
            }
        } else {
            JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_DOWNLINK, "event text parse failed len=%lu", (unsigned long)data_len);
            dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_PROTOCOL, -QM_EINVAL);
            cJSON_Delete(root);
            return;
        }
    }

    if (handle->config.on_event) {
        handle->config.on_event(&event);
    }
    cJSON_Delete(root);
}

static void dispatch_error(joyinside_client_handle_t handle,
                           joyinside_client_error_type_t type,
                           int code)
{
    joyinside_client_event_t event;

    if (!handle || !handle->config.on_event) {
        return;
    }

    qm_memset(&event, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(event));
    event.event_id = JOYINSIDE_CLIENT_EVENT_ERROR;
    event.handle = handle;
    event.user_context = handle->config.user_context;
    event.payload.error.type = type;
    event.payload.error.code = code;
    JOYINSIDE_CLIENT_LOGE(LOG_TAG, "event error type=%d code=%d", type, code);
    handle->config.on_event(&event);
}

static void reset_rx(joyinside_client_handle_t handle)
{
    handle->rx_text_len = 0U;
    handle->rx_frame_offset = 0U;
    handle->rx_frame_length = 0U;
    handle->rx_discard = QM_FALSE;
}

/**
 * @brief Reassemble bounded JSON text across WebSocket receive callbacks.
 *
 * @par 中文说明
 * 在 WebSocket 多次接收回调之间重组受限大小的 JSON 文本。
 *
 * payload_offset is frame-relative while fin is message-relative. A continuation
 * frame starts at offset zero, and one frame may span multiple receive callbacks.
 * 错误或超限后丢弃至 FIN，确保下一条消息可恢复解析。
 */
static void receive_text(joyinside_client_handle_t handle,
                          const qm_websocket_event_data_t *event)
{
    uint32_t limit = handle->config.buffer.max_text_size ?
        handle->config.buffer.max_text_size : CONFIG_JOYINSIDE_CLIENT_MAX_TEXT_SIZE;
    uint32_t needed;
    int error = -QM_EINVAL;
    if (handle->rx_discard) {
        if (event->fin) reset_rx(handle);
        return;
    }
    if (event->data_len < 0 || event->payload_offset < 0 || event->payload_len < 0 ||
        event->payload_offset > event->payload_len ||
        event->data_len > event->payload_len - event->payload_offset ||
        (event->data_len && !event->data_ptr)) goto fail;
    if (event->payload_offset == 0 &&
        handle->rx_frame_offset == handle->rx_frame_length) {
        handle->rx_frame_offset = 0U;
        handle->rx_frame_length = (uint32_t)event->payload_len;
    }
    if ((uint32_t)event->payload_offset != handle->rx_frame_offset ||
        (uint32_t)event->payload_len != handle->rx_frame_length) goto fail;
    if (handle->rx_text_len > limit ||
        (uint32_t)event->data_len > limit - handle->rx_text_len) {
        error = -QM_EFULL;
        goto fail;
    }
    needed = handle->rx_text_len + (uint32_t)event->data_len;
    if (needed + 1U > handle->rx_text_capacity) {
        uint32_t capacity = handle->rx_text_capacity ? handle->rx_text_capacity :
                            JOYINSIDE_CLIENT_TEXT_INITIAL_CAPACITY;
        char *buffer;
        while (capacity <= needed && capacity < limit + 1U) {
            capacity = capacity > (limit + 1U) / 2U ? limit + 1U : capacity * 2U;
        }
        buffer = qm_malloc(capacity);
        if (!buffer) { error = -QM_ENOMEM; goto fail; }
        if (handle->rx_text_len) qm_memcpy(buffer, handle->rx_text, handle->rx_text_len);
        qm_free(handle->rx_text);
        handle->rx_text = buffer;
        handle->rx_text_capacity = capacity;
    }
    if (event->data_len) {
        qm_memcpy(handle->rx_text + handle->rx_text_len, event->data_ptr, (uint32_t)event->data_len);
    }
    handle->rx_text_len = needed;
    handle->rx_frame_offset += (uint32_t)event->data_len;
    if (event->fin) {
        qm_websocket_event_data_t complete = *event;
        if (handle->rx_frame_offset != handle->rx_frame_length || !needed) goto fail;
        handle->rx_text[needed] = '\0';
        complete.data_ptr = handle->rx_text;
        complete.data_len = (int)needed;
        complete.payload_len = (int)needed;
        complete.payload_offset = 0;
        reset_rx(handle);
        dispatch_event(handle, JOYINSIDE_CLIENT_EVENT_TEXT, complete.data_ptr, needed, &complete);
    }
    return;
fail:
    reset_rx(handle);
    handle->rx_discard = !event->fin;
    dispatch_error(handle, error == -QM_EINVAL ? JOYINSIDE_CLIENT_ERROR_PROTOCOL :
                   JOYINSIDE_CLIENT_ERROR_BUFFER, error);
}

/**
 * @brief Bridge WebSocket transport events into JoyInside client events.
 *
 * @par 中文说明
 * 将 WebSocket 传输事件转换为 JoyInside 客户端事件。
 *
 * A transport connection is not exposed as connected until the client voice
 * update has been acknowledged, which keeps application audio admission aligned
 * with the negotiated format.
 */
static void websocket_event_handler(qm_websocket_event_id_t event_id,
                        qm_websocket_event_data_t *event_data,
                        void *user_context)
{
    joyinside_client_handle_t handle = (joyinside_client_handle_t)user_context;
    int ret;

    if (!handle) {
        return;
    }

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        reset_rx(handle);
        if (core_stop_requested(handle)) break;
        audio_admission(handle, QM_FALSE);
        audio_rx_stats_reset(handle);
        (void)core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_RESET,
                                       NULL);
        JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "websocket connected, updating voice before external notify");
        cancel_ack_deadline(handle);
        clear_pending_ack(handle);
        /* Gate readiness on CLIENT_VOICE_CHAT_UPDATE ack before notifying applications. */
        ret = update_voice_internal(handle, QM_TRUE, network_timeout_ms(handle));
        if (ret < QM_EOK) {
            JOYINSIDE_CLIENT_LOGE(LOG_TAG, "auto update voice failed ret=%d", ret);
            dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_WS, ret);
            (void)qm_websocket_client_reconnect(handle->ws);
        }
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
    {
        reset_rx(handle);
        if (handle->extensions.on_disconnect) {
            handle->extensions.on_disconnect(handle->extensions.media_context);
        }
        audio_admission(handle, QM_FALSE);
        qm_bool was_connected = clear_connection_ready(handle);
        (void)core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_RESET,
                                       NULL);
        cancel_ack_deadline(handle);
        clear_pending_ack(handle);
        JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "websocket disconnected");
        if (was_connected) {
            dispatch_event(handle, JOYINSIDE_CLIENT_EVENT_DISCONNECTED, NULL,
                           JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN, event_data);
        }
        (void)core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_RECONNECT, NULL);
        break;
    }
    case WEBSOCKET_EVENT_DATA:
        if (event_data && event_data->opcode == JOYINSIDE_CLIENT_WS_OPCODE_BINARY &&
            event_data->data_len >= 0) {
            if (!audio_receiving(handle, QM_TRUE)) break;
            audio_rx_stats_note_packet(handle, (uint32_t)event_data->data_len);
            dispatch_event(handle, JOYINSIDE_CLIENT_EVENT_AUDIO, event_data->data_ptr,
                        (uint32_t)event_data->data_len, event_data);
        } else if (event_data && event_data->opcode == 1) {
            receive_text(handle, event_data);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
    {
        qm_bool was_connected = clear_connection_ready(handle);
        if (handle->extensions.on_disconnect) {
            handle->extensions.on_disconnect(handle->extensions.media_context);
        }
        reset_rx(handle);
        if (event_data && (event_data->error_info.handshake_status_code == 401 ||
                           event_data->error_info.handshake_status_code == 403)) {
            handle->force_token_refresh = QM_TRUE;
        }
        audio_admission(handle, QM_FALSE);
        (void)core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_RESET,
                                       NULL);
        cancel_ack_deadline(handle);
        clear_pending_ack(handle);
        if (was_connected) {
            dispatch_event(handle, JOYINSIDE_CLIENT_EVENT_DISCONNECTED, NULL, 0U, event_data);
        }
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "websocket error code=%d", event_data ? event_data->error_code : -QM_EIO);
        dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_WS, event_data ? event_data->error_code : -QM_EIO);
        (void)core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_RECONNECT, NULL);
        break;
    }
    default:
        break;
    }
}

static int core_event_send(joyinside_client_handle_t handle,
                           joyinside_client_core_event_id_t id,
                           const char *mid)
{
    int ret;
    if (!handle || id < JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT ||
        id > JOYINSIDE_CLIENT_CORE_EVENT_STOP) return -QM_EINVAL;
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    if (!handle->core_queue_valid || handle->core_stop_requested) {
        qm_mutex_unlock(&handle->core_lock);
        return -QM_EBUSY;
    }
    (void)mid;
    if (id == JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_PONG &&
        handle->heartbeat_mid[0] == '\0') {
        qm_mutex_unlock(&handle->core_lock);
        return QM_EOK;
    }
    handle->core_pending |= 1U << id;
    ret = wake_core_locked(handle);
    qm_mutex_unlock(&handle->core_lock);
    return ret;
}

static qm_bool core_stop_requested(joyinside_client_handle_t handle)
{
    if (!handle) {
        return QM_TRUE;
    }

    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    qm_bool stopped = handle->core_stop_requested;
    qm_mutex_unlock(&handle->core_lock);
    return stopped;
}

static void set_core_stop_requested(joyinside_client_handle_t handle, qm_bool requested)
{
    if (!handle) {
        return;
    }

    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->core_stop_requested = requested;
    qm_mutex_unlock(&handle->core_lock);
}

/**
 * @brief Promote expired protocol deadlines before dequeuing audio.
 *
 * @par 中文说明
 * 在取出音频前提升已到期的协议任务。
 *
 * Called with core_lock held so high-rate audio traffic cannot starve heartbeat,
 * acknowledgement, context, or media keepalive deadlines.
 */
static uint32_t poll_protocol_deadlines(joyinside_client_handle_t handle)
{
    uint32_t wait_ms = QM_WAIT_FOREVER;
    for (uint32_t id = 1U; id < JOYINSIDE_CLIENT_CORE_EVENT_STOP; ++id) {
        if (!(handle->protocol_active & (1U << id))) continue;
        uint32_t left = qm_utils_time_left(&handle->protocol_deadline[id]);
        if (!left) {
            handle->core_pending |= 1U << id;
            if (handle->protocol_period_ms[id]) {
                qm_utils_time_countdown_ms(&handle->protocol_deadline[id], handle->protocol_period_ms[id]);
                left = handle->protocol_period_ms[id];
            } else {
                handle->protocol_active &= ~(1U << id);
                continue;
            }
        }
        if (left < wait_ms) wait_ms = left;
    }
    return wait_ms;
}

/**
 * @brief Run serialized protocol scheduling and uplink audio processing.
 *
 * @par 中文说明
 * 串行执行协议定时任务与上行音频处理。
 *
 * Deadline events take priority over the bounded audio queue. This worker owns
 * retry timing and send ordering; callbacks must never wait for it to finish.
 */
static void core_task(void *arg)
{
    static const joyinside_client_core_event_id_t priority[] = {
        JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_RESET,
        JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_PONG,
        JOYINSIDE_CLIENT_CORE_EVENT_ACK_TIMEOUT,
        JOYINSIDE_CLIENT_CORE_EVENT_VOICE_UPDATE,
        JOYINSIDE_CLIENT_CORE_EVENT_RECONNECT,
        JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT,
        JOYINSIDE_CLIENT_CORE_EVENT_CONTEXT_REPORT,
        JOYINSIDE_CLIENT_CORE_EVENT_AUDIOBOOK_PING,
        JOYINSIDE_CLIENT_CORE_EVENT_STOP,
    };
    joyinside_client_handle_t handle = (joyinside_client_handle_t)arg;
    joyinside_client_core_event_t event;
    unsigned int size = 0;
    int ret;

    if (!handle) {
        qm_task_exit(NULL);
        return;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "core task started");
    for (;;) {
        qm_memset(&event, 0, sizeof(event));
        qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
        if (handle->core_stop_requested) {
            qm_mutex_unlock(&handle->core_lock);
            break;
        }
        uint32_t wait_ms = poll_protocol_deadlines(handle);
        ret = -QM_EEMPTY;
        for (uint32_t i = 0; i < sizeof(priority) / sizeof(priority[0]); ++i) {
            uint32_t id = priority[i];
            if ((handle->core_pending & (1U << id)) == 0U) continue;
            handle->core_pending &= ~(1U << id);
            event.id = (joyinside_client_core_event_id_t)id;
            qm_memcpy(event.mid, handle->heartbeat_mid, sizeof(event.mid));
            ret = QM_EOK;
            break;
        }
        if (ret != QM_EOK && handle->core_queue_count != 0U) {
            size = sizeof(event);
            ret = qm_queue_recv(&handle->core_queue, &event, &size, QM_NO_WAIT);
            if (ret == QM_EOK) handle->core_queue_count--;
        }
        if (ret != QM_EOK) handle->core_wake_pending = QM_FALSE;
        qm_mutex_unlock(&handle->core_lock);
        if (ret != QM_EOK) {
            uint32_t reconnect_wait = handle->reconnect_scheduled ?
                qm_utils_time_left(&handle->reconnect_deadline) : QM_WAIT_FOREVER;
            if (reconnect_wait < wait_ms) wait_ms = reconnect_wait;
            if (reconnect_wait == 0U && handle->reconnect_scheduled) {
                handle->reconnect_scheduled = QM_FALSE;
                connect_once(handle);
            } else {
                (void)qm_sem_wait(&handle->core_wake, wait_ms);
            }
            continue;
        }
        switch (event.id) {
        case JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT:
            if (!core_stop_requested(handle)) {
                if (handle->heartbeat_pending &&
                    qm_utils_time_is_expired(&handle->heartbeat_deadline)) {
                    (void)handle_heartbeat_failure(handle, "PONG timeout", -QM_ETIMEOUT);
                } else if (!handle->heartbeat_pending &&
                           qm_utils_time_is_expired(&handle->heartbeat_ping_timer)) {
                    (void)send_heartbeat_ping(handle);
                }
            }
            break;
        case JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_PONG:
            if (!core_stop_requested(handle)) {
                (void)handle_heartbeat_pong(handle, event.mid);
            }
            break;
        case JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_RESET:
            if (!core_stop_requested(handle)) {
                reset_heartbeat_state(handle);
            }
            break;
        case JOYINSIDE_CLIENT_CORE_EVENT_ACK_TIMEOUT:
            if (!core_stop_requested(handle)) {
                (void)handle_ack_timeout(handle);
            }
            break;
        case JOYINSIDE_CLIENT_CORE_EVENT_CONTEXT_REPORT:
            if (joyinside_client_is_connected(handle)) (void)context_report_resend(handle);
            break;
        case JOYINSIDE_CLIENT_CORE_EVENT_AUDIOBOOK_PING: {
            qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
            int (*ping)(joyinside_client_handle_t) = handle->audiobook_ping;
            qm_mutex_unlock(&handle->core_lock);
            if (joyinside_client_is_connected(handle) && ping) (void)ping(handle);
            break;
        }
        case JOYINSIDE_CLIENT_CORE_EVENT_VOICE_UPDATE:
            {
                ret = update_voice_internal(handle, QM_FALSE, network_timeout_ms(handle));
                if (ret != QM_EOK && !core_stop_requested(handle)) {
                    dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_WS, ret);
                    if (handle->ws) (void)qm_websocket_client_reconnect(handle->ws);
                }
            }
            break;
        case JOYINSIDE_CLIENT_CORE_EVENT_STOP:
            set_core_stop_requested(handle, QM_TRUE);
            break;
        case JOYINSIDE_CLIENT_CORE_EVENT_RECONNECT:
            schedule_reconnect(handle);
            break;
        case JOYINSIDE_CLIENT_CORE_EVENT_AUDIO:
        case JOYINSIDE_CLIENT_CORE_EVENT_AUDIO_FINISH:
            process_audio(handle, &event);
            break;
        default:
            JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "unknown core event=%d", event.id);
            break;
        }
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "core task stopped");
    if (handle->core_exit_sem_valid) {
        (void)qm_sem_signal(&handle->core_exit_sem);
    }
    qm_task_exit(NULL);
}

static int start_core(joyinside_client_handle_t handle)
{
    int ret;

    if (!handle) {
        return -QM_EINVAL;
    }

    if (handle->core_task_valid) {
        return QM_EOK;
    }

    handle->core_stop_requested = QM_FALSE;
    handle->core_exited = QM_FALSE;
    handle->core_queue_count = 0U;
    handle->core_pending = 0U;
    handle->core_wake_pending = QM_FALSE;
    handle->audio_index = 0U;
    handle->reconnect_scheduled = QM_FALSE;
    handle->protocol_active = 0U;
    reset_heartbeat_state(handle);
    if (!handle->config.heartbeat.disabled && heartbeat_interval_ms(handle)) {
        schedule_protocol(handle, JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT,
                           heartbeat_tick_ms(handle), heartbeat_tick_ms(handle));
    }
    backoff_algorithm_init(&handle->reconnect_backoff, JOYINSIDE_CLIENT_RECONNECT_BASE_MS,
                            JOYINSIDE_CLIENT_RECONNECT_MAX_MS, BACKOFF_ALGORITHM_RETRY_FOREVER);

    handle->audio_slots = qm_calloc(CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN,
                                    sizeof(*handle->audio_slots));
    if (!handle->audio_slots) return -QM_ENOMEM;
    ret = qm_sem_new(&handle->core_wake, 0U);
    if (ret != QM_EOK) {
        qm_free(handle->audio_slots);
        handle->audio_slots = NULL;
        return ret;
    }

    ret = qm_queue_new(&handle->core_queue, JOYINSIDE_CLIENT_CORE_QUEUE_LEN, sizeof(joyinside_client_core_event_t));
    if (ret != QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "core queue create failed ret=%d", ret);
        qm_sem_free(&handle->core_wake);
        qm_free(handle->audio_slots);
        handle->audio_slots = NULL;
        return ret;
    }
    handle->core_queue_valid = QM_TRUE;

    ret = qm_sem_new(&handle->core_exit_sem, 0);
    if (ret != QM_EOK) {
        (void)qm_queue_free(&handle->core_queue);
        qm_sem_free(&handle->core_wake);
        qm_free(handle->audio_slots);
        handle->audio_slots = NULL;
        handle->core_queue_valid = QM_FALSE;
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "core exit sem create failed ret=%d", ret);
        return ret;
    }
    handle->core_exit_sem_valid = QM_TRUE;

    handle->core_task_valid = QM_TRUE;
    ret = qm_task_new(&handle->core_task, "joyinside_client_core", core_task, handle,
                      handle->config.task.core_stack_size ? handle->config.task.core_stack_size :
                                                            CONFIG_JOYINSIDE_CLIENT_CORE_TASK_STACK_SIZE,
                      handle->config.task.core_priority ? handle->config.task.core_priority :
                                                          CONFIG_JOYINSIDE_CLIENT_CORE_TASK_PRIORITY);
    if (ret != QM_EOK) {
        handle->core_task_valid = QM_FALSE;
        (void)qm_sem_free(&handle->core_exit_sem);
        handle->core_exit_sem_valid = QM_FALSE;
        (void)qm_queue_free(&handle->core_queue);
        handle->core_queue_valid = QM_FALSE;
        qm_sem_free(&handle->core_wake);
        qm_free(handle->audio_slots);
        handle->audio_slots = NULL;
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "core task create failed ret=%d", ret);
        return ret;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "core started queue=%u task_stack=%lu task_prio=%lu",
            (unsigned int)JOYINSIDE_CLIENT_CORE_QUEUE_LEN,
            (unsigned long)(handle->config.task.core_stack_size ?
                            handle->config.task.core_stack_size :
                            CONFIG_JOYINSIDE_CLIENT_CORE_TASK_STACK_SIZE),
            (unsigned long)(handle->config.task.core_priority ?
                            handle->config.task.core_priority :
                            CONFIG_JOYINSIDE_CLIENT_CORE_TASK_PRIORITY));
    return QM_EOK;
}

static int stop_core(joyinside_client_handle_t handle)
{
    if (!handle) {
        return -QM_EINVAL;
    }
    if (handle->core_task_valid && qm_task_is_current(&handle->core_task)) return -QM_EBUSY;
    if (handle->core_task_valid && handle->core_queue_valid) {
        /* Stop must not wait for the encoding lock before starting its budget. */
        qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
        handle->audio_accepting = QM_FALSE;
        handle->core_stop_requested = QM_TRUE;
        (void)wake_core_locked(handle);
        qm_mutex_unlock(&handle->core_lock);
        if (!handle->core_exited) {
            int ret = qm_sem_wait(&handle->core_exit_sem, CONFIG_JOYINSIDE_CLIENT_CORE_STOP_TIMEOUT_MS);
            if (ret != QM_EOK) return ret;
            handle->core_exited = QM_TRUE;
        }
        int ret = qm_task_exit(&handle->core_task);
        if (ret != QM_EOK) return ret;
        handle->core_task_valid = QM_FALSE;
    }
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    if (handle->core_exit_sem_valid) {
        (void)qm_sem_free(&handle->core_exit_sem);
        handle->core_exit_sem_valid = QM_FALSE;
    }
    if (handle->core_queue_valid) {
        (void)qm_queue_free(&handle->core_queue);
        handle->core_queue_valid = QM_FALSE;
        (void)qm_sem_free(&handle->core_wake);
        qm_free(handle->audio_slots);
        handle->audio_slots = NULL;
    }
    free_voice_request(handle->queued_voice);
    handle->queued_voice = NULL;
    /* Reset heartbeat state only after the core task and queue have stopped. */
    qm_mutex_unlock(&handle->core_lock);
    reset_heartbeat_state(handle);
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "core stopped");
    return QM_EOK;
}

static void reset_heartbeat_state(joyinside_client_handle_t handle)
{
    if (!handle) {
        return;
    }
    handle->heartbeat_pending = QM_FALSE;
    handle->heartbeat_fail_count = 0;
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->heartbeat_mid[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    handle->core_pending &= ~(1U << JOYINSIDE_CLIENT_CORE_EVENT_HEARTBEAT_PONG);
    qm_mutex_unlock(&handle->core_lock);
    qm_utils_time_init(&handle->heartbeat_deadline);
    qm_utils_time_countdown_ms(&handle->heartbeat_ping_timer, heartbeat_interval_ms(handle));
}

static void cancel_ack_deadline(joyinside_client_handle_t handle)
{
    if (handle) cancel_protocol(handle, JOYINSIDE_CLIENT_CORE_EVENT_ACK_TIMEOUT);
}

static int schedule_ack_deadline(joyinside_client_handle_t handle)
{
    if (!handle) return -QM_EINVAL;
    if (ack_timeout_ms(handle)) {
        schedule_protocol(handle, JOYINSIDE_CLIENT_CORE_EVENT_ACK_TIMEOUT, ack_timeout_ms(handle), 0U);
    }
    return QM_EOK;
}

/* RFC 3986 percent-encoding for WebSocket URI query parameters. */
static uint32_t uri_encoded_length(const char *src)
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

static int uri_encode_copy(char *dst, uint32_t dst_len, const char *src)
{
    static const char hex[] = "0123456789ABCDEF";
    uint32_t offset = 0U;
    unsigned char ch;

    if (dst == NULL || dst_len == 0U) {
        return -QM_EINVAL;
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
            dst[offset++] = hex[(ch >> 4U) & 0x0FU];
            dst[offset++] = hex[ch & 0x0FU];
        }
        src++;
    }
    dst[offset] = '\0';
    return QM_EOK;
}

/* Length of "&name=value" for an encoded value, 0 when value is absent. */
static uint32_t uri_query_part_len(const char *name, const char *value)
{
    uint32_t value_len;

    if (!name || !value || value[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return 0U;
    }
    value_len = uri_encoded_length(value);
    if (value_len == 0U) {
        return 0U;
    }
    return 1U + (uint32_t)qm_strlen(name) + 1U + value_len;
}

/* Append "&name=<encoded value>" to buf at *offset, advancing *offset. */
static int uri_query_append_encoded(char *buf, uint32_t cap, uint32_t *offset,
                                    const char *name, const char *value)
{
    uint32_t part_len;
    uint32_t value_len;
    int ret;

    if (buf == NULL || offset == NULL) {
        return -QM_EINVAL;
    }
    part_len = uri_query_part_len(name, value);
    if (part_len == 0U) {
        return QM_EOK;
    }
    if (*offset + part_len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN > cap) {
        return -QM_EFULL;
    }
    value_len = uri_encoded_length(value);
    buf[(*offset)++] = '&';
    qm_memcpy(buf + *offset, name, qm_strlen(name));
    *offset += (uint32_t)qm_strlen(name);
    buf[(*offset)++] = '=';
    ret = uri_encode_copy(buf + *offset, cap - *offset, value);
    if (ret != QM_EOK) {
        return ret;
    }
    *offset += value_len;
    return QM_EOK;
}

/* sessionId spans reconnects; requestId is replaced before every handshake. */
static int build_ws_uri(joyinside_client_handle_t handle, char **uri)
{
    char *buf;
    uint32_t cap;
    uint32_t offset = 0U;
    uint32_t base_len;
    int ret;

    if (!handle || !uri || !handle->bot_id || !handle->session_id || !handle->request_id) {
        return -QM_EINVAL;
    }
    *uri = NULL;

    base_len = (uint32_t)qm_strlen(JOYINSIDE_CLIENT_VOICE_WS_URL);
    cap = base_len + 1U; /* '?' */
    cap += uri_query_part_len("botId", handle->bot_id);
    cap += uri_query_part_len("sessionId", handle->session_id);
    cap += uri_query_part_len("requestId", handle->request_id);
    cap += uri_query_part_len("uid", handle->uid);
#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
    cap += (uint32_t)qm_strlen("&needManualCall=true");
#endif
    cap += uri_query_part_len("feature", handle->feature);
    cap += uri_query_part_len("clientType", handle->client_type);
    cap += uri_query_part_len("location", handle->location);
    if (handle->need_asr_partial) {
        cap += (uint32_t)qm_strlen("&needAsrPartial=true");
    }
    cap += JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN;

    buf = (char *)qm_malloc(cap);
    if (!buf) {
        return -QM_ENOMEM;
    }

    qm_memcpy(buf, JOYINSIDE_CLIENT_VOICE_WS_URL, base_len);
    offset = base_len;
    buf[offset++] = '?';
    ret = uri_query_append_encoded(buf, cap, &offset, "botId", handle->bot_id);
    if (ret == QM_EOK) {
        ret = uri_query_append_encoded(buf, cap, &offset, "sessionId", handle->session_id);
    }
    if (ret == QM_EOK) {
        ret = uri_query_append_encoded(buf, cap, &offset, "requestId", handle->request_id);
    }
    if (ret == QM_EOK) {
        ret = uri_query_append_encoded(buf, cap, &offset, "uid", handle->uid);
    }
#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
    if (ret == QM_EOK) {
        if (offset + (uint32_t)qm_strlen("&needManualCall=true") +
                JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN > cap) {
            ret = -QM_EFULL;
        } else {
            qm_memcpy(buf + offset, "&needManualCall=true",
                      (uint32_t)qm_strlen("&needManualCall=true"));
            offset += (uint32_t)qm_strlen("&needManualCall=true");
        }
    }
#endif
    if (ret == QM_EOK) {
        ret = uri_query_append_encoded(buf, cap, &offset, "feature", handle->feature);
    }
    if (ret == QM_EOK) {
        ret = uri_query_append_encoded(buf, cap, &offset, "clientType", handle->client_type);
    }
    if (ret == QM_EOK) {
        ret = uri_query_append_encoded(buf, cap, &offset, "location", handle->location);
    }
    if (ret == QM_EOK && handle->need_asr_partial) {
        if (offset + (uint32_t)qm_strlen("&needAsrPartial=true") +
                JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN > cap) {
            ret = -QM_EFULL;
        } else {
            qm_memcpy(buf + offset, "&needAsrPartial=true",
                      (uint32_t)qm_strlen("&needAsrPartial=true"));
            offset += (uint32_t)qm_strlen("&needAsrPartial=true");
        }
    }
    if (ret != QM_EOK) {
        qm_free(buf);
        return ret;
    }
    buf[offset] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    *uri = buf;
    return QM_EOK;
}

/**
 * @brief Refresh connection identity immediately before each WebSocket dial.
 *
 * @par 中文说明
 * 在每次 WebSocket 拨号前刷新连接身份。
 *
 * Token retrieval runs without the client lock. A new requestId is generated
 * for this connection while the existing sessionId is retained for continuity.
 */
static int prepare_connection(void *ws, void *context)
{
    joyinside_client_handle_t handle = context;
    char *token = NULL;
    const char *use_token = handle->access_token;
    char *uri = NULL;
    char *headers = NULL;
    char *request_id = NULL;
    uint32_t token_len = CONFIG_JOYINSIDE_CLIENT_TOKEN_SIZE;
    int ret;
    int length;

    if (handle->token_provider) {
        token = qm_malloc(token_len);
        if (!token) return -QM_ENOMEM;
        qm_memset(token, 0, token_len);
        ret = handle->token_provider(handle->token_context, handle->force_token_refresh,
                                      token, &token_len);
        if (ret != QM_EOK) goto out;
        if (!token_len || token_len >= CONFIG_JOYINSIDE_CLIENT_TOKEN_SIZE ||
            token[token_len] != '\0' || qm_strlen(token) != token_len) {
            ret = -QM_EINVAL;
            goto out;
        }
        use_token = token;
    }
    for (const char *p = use_token; *p; ++p) {
        if (*p == '\r' || *p == '\n') { ret = -QM_EINVAL; goto out; }
    }
    ret = make_request_id(&request_id);
    if (ret != QM_EOK) goto out;
    qm_free(handle->request_id);
    handle->request_id = request_id;
    request_id = NULL;
    ret = build_ws_uri(handle, &uri);
    if (ret != QM_EOK) goto out;
    length = qm_snprintf(NULL, 0, "Authorization: Bearer %s\r\n", use_token);
    if (length <= 0) { ret = -QM_EINVAL; goto out; }
    headers = qm_malloc((uint32_t)length + 1U);
    if (!headers) { ret = -QM_ENOMEM; goto out; }
    qm_snprintf(headers, length + 1, "Authorization: Bearer %s\r\n", use_token);
    ret = qm_websocket_client_set_uri(ws, uri);
    if (ret == QM_EOK) ret = qm_websocket_client_set_headers(ws, headers);
    if (ret == QM_EOK) {
        handle->force_token_refresh = QM_FALSE;
        if (token) {
            /* Keep the actual connected token for session-bound HTTP requests.
             * 保存实际建连 Token，供同会话 HTTP 请求快照使用。 */
            qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
            qm_free(handle->access_token);
            handle->access_token = token;
            token = NULL;
            qm_mutex_unlock(&handle->core_lock);
        }
    }
out:
    qm_free(request_id);
    qm_free(headers);
    qm_free(uri);
    qm_free(token);
    return ret;
}

static void schedule_reconnect(joyinside_client_handle_t handle)
{
    uint16_t delay = 0U;
    if (!connection_allowed(handle) || core_stop_requested(handle) ||
        (handle->connection_attempted && !auto_reconnect_configured())) {
        handle->reconnect_scheduled = QM_FALSE;
        return;
    }
    if (handle->reconnect_scheduled) return;
    if (handle->connection_attempted &&
        backoff_algorithm_get_next_delay(&handle->reconnect_backoff, &delay) != BACKOFF_ALGORITHM_SUCCESS) return;
    qm_utils_time_countdown_ms(&handle->reconnect_deadline, delay);
    handle->reconnect_scheduled = QM_TRUE;
}

static void connect_once(joyinside_client_handle_t handle)
{
    int ret;
    if (!handle->ws || core_stop_requested(handle) || !connection_allowed(handle)) return;
    audio_admission(handle, QM_FALSE);
    /* Join the old WS worker before mutating its endpoint or credentials. */
    ret = qm_websocket_client_stop(handle->ws);
    if (ret != QM_EOK) goto failed;
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->core_pending &= ~((1U << JOYINSIDE_CLIENT_CORE_EVENT_RECONNECT) |
                              (1U << JOYINSIDE_CLIENT_CORE_EVENT_ACK_TIMEOUT));
    qm_mutex_unlock(&handle->core_lock);
    handle->connection_attempted = QM_TRUE;
    reset_rx(handle);
    ret = prepare_connection(handle->ws, handle);
    if (ret != QM_EOK) goto failed;
    if (core_stop_requested(handle) || !connection_allowed(handle)) return;
    ret = qm_websocket_client_start(handle->ws);
    if (ret == QM_EOK) return;
failed:
    dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_WS, ret);
    schedule_reconnect(handle);
}

joyinside_client_handle_t joyinside_client_init(const joyinside_client_config_t *config)
{
    joyinside_client_handle_t handle;
    int ret;

    if (!config || config->buffer.max_text_size >= INT32_MAX ||
        config->buffer.ws_rx_size > INT32_MAX) {
        return NULL;
    }

    handle = (joyinside_client_handle_t)qm_malloc(sizeof(*handle));
    if (!handle) {
        return NULL;
    }
    qm_memset(handle, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(*handle));
    handle->network_state = JOYINSIDE_CLIENT_NETWORK_UP;
    ret = copy_config(&handle->config, config);
    if (ret != QM_EOK) {
        qm_free(handle);
        return NULL;
    }
    if (qm_mutex_new(&handle->core_lock) != QM_EOK) {
        free_config(&handle->config);
        qm_free(handle);
        return NULL;
    }
    if (qm_mutex_new(&handle->audio_send_lock) != QM_EOK) {
        qm_mutex_free(&handle->core_lock);
        free_config(&handle->config);
        qm_free(handle);
        return NULL;
    }
    if (qm_mutex_new(&handle->tx_lock) != QM_EOK) {
        qm_mutex_free(&handle->audio_send_lock);
        qm_mutex_free(&handle->core_lock);
        free_config(&handle->config);
        qm_free(handle);
        return NULL;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE,
            "init ok ws_rx=%lu ws_stack=%lu ws_prio=%lu core_stack=%lu core_prio=%lu queue=%lu net_timeout=%lums send_retry=%lu delay=%lums voice_ack_timeout=%lums heartbeat_disabled=%s heartbeat_interval=%lums voice_in=%s/%lu voice_out=%s/%lu role_code=%s",
            (unsigned long)(handle->config.buffer.ws_rx_size ? handle->config.buffer.ws_rx_size :
                            CONFIG_JOYINSIDE_CLIENT_WS_RX_SIZE),
            (unsigned long)(handle->config.task.ws_stack_size ?
                            handle->config.task.ws_stack_size :
                            CONFIG_JOYINSIDE_CLIENT_WS_TASK_STACK_SIZE),
            (unsigned long)(handle->config.task.ws_priority ?
                            handle->config.task.ws_priority :
                            CONFIG_JOYINSIDE_CLIENT_WS_TASK_PRIORITY),
            (unsigned long)(handle->config.task.core_stack_size ?
                            handle->config.task.core_stack_size :
                            CONFIG_JOYINSIDE_CLIENT_CORE_TASK_STACK_SIZE),
            (unsigned long)(handle->config.task.core_priority ?
                            handle->config.task.core_priority :
                            CONFIG_JOYINSIDE_CLIENT_CORE_TASK_PRIORITY),
            (unsigned long)(handle->config.task.send_queue_size ? handle->config.task.send_queue_size :
                            CONFIG_JOYINSIDE_CLIENT_SEND_QUEUE_SIZE),
            (unsigned long)(handle->config.network.timeout_ms ? handle->config.network.timeout_ms :
                            CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS),
            (unsigned long)send_retry_count(handle),
            (unsigned long)send_retry_delay_ms(handle),
            (unsigned long)ack_timeout_ms(handle),
            bool_text(handle->config.heartbeat.disabled),
            (unsigned long)heartbeat_interval_ms(handle),
            codec_name(handle->config.voice.input_codec),
            (unsigned long)handle->config.voice.input_sample_rate,
            codec_name(handle->config.voice.output_codec),
            (unsigned long)handle->config.voice.output_sample_rate,
            present_text(handle->config.voice.chat_role_code));
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "audio transport=%s",
            audio_binary_configured() ? "websocket-binary" : "text-base64");
    return handle;
}

int joyinside_client_start(joyinside_client_handle_t handle,
                              const joyinside_client_start_params_t *params)
{
    qm_websocket_client_config_t ws_cfg;
    int ret;

    if (!handle || !params || !params->bot_id ||
        params->bot_id[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        (!params->token_provider && (!params->access_token || !params->access_token[0]))) {
        return -QM_EINVAL;
    }
    if (handle->ws) {
        JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "start rejected: websocket already active");
        return -QM_EBUSY;
    }

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "start request bot=%s uid=%s access_token=%s manual_call=%s",
            safe_text(params->bot_id), safe_text(params->uid), present_text(params->access_token),
            bool_text(manual_call_configured()));

    ret = set_start_state(handle, params);
    if (ret != QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "start state create failed ret=%d", ret);
        return ret;
    }
    log_session_state("session prepared", handle);

    qm_memset(&ws_cfg, JOYINSIDE_CLIENT_MEMSET_ZERO, sizeof(ws_cfg));
    ws_cfg.uri = JOYINSIDE_CLIENT_VOICE_WS_URL;
    ws_cfg.task_stack = handle->config.task.ws_stack_size ? handle->config.task.ws_stack_size :
                                                            CONFIG_JOYINSIDE_CLIENT_WS_TASK_STACK_SIZE;
    ws_cfg.task_priority = handle->config.task.ws_priority ? handle->config.task.ws_priority :
                                                              CONFIG_JOYINSIDE_CLIENT_WS_TASK_PRIORITY;
    ws_cfg.buffer_size = (int)(handle->config.buffer.ws_rx_size ? handle->config.buffer.ws_rx_size :
                                                                 CONFIG_JOYINSIDE_CLIENT_WS_RX_SIZE);
    ws_cfg.network_timeout_ms = (int)(handle->config.network.timeout_ms ? handle->config.network.timeout_ms :
                                                                         CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS);
    /* JoyInside owns the complete retry, token and voice-negotiation lifecycle. */
    ws_cfg.disable_auto_reconnect = QM_TRUE;
    /* JoyInside owns protocol-level PING/PONG timeout and reconnect handling. */
    ws_cfg.disable_ping = QM_TRUE;
    ws_cfg.keepalive_ping_interval_ms = 0;
    ws_cfg.user_context = handle;
    ws_cfg.send_queue_size = handle->config.task.send_queue_size ? handle->config.task.send_queue_size :
                                                                   CONFIG_JOYINSIDE_CLIENT_SEND_QUEUE_SIZE;

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE,
            "websocket init config buffer=%d stack=%d prio=%d queue=%d timeout=%dms ws_ping=%s auto_reconnect=%s",
            ws_cfg.buffer_size, ws_cfg.task_stack, ws_cfg.task_priority,
            ws_cfg.send_queue_size, ws_cfg.network_timeout_ms,
            ws_cfg.disable_ping ? "disabled" : "enabled",
            bool_text(auto_reconnect_configured()));
    handle->ws = qm_websocket_client_init(&ws_cfg);
    if (!handle->ws) {
        clear_start_state(handle);
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "websocket init failed");
        return -QM_ENOMEM;
    }

    ret = qm_websocket_client_network_update(
        handle->ws,
        connection_allowed(handle) ?
        QM_WEBSOCKET_NETWORK_UP : QM_WEBSOCKET_NETWORK_DOWN);
    if (ret != QM_EOK) {
        int cleanup_ret = stop_destroy_ws(handle);
        if (cleanup_ret != QM_EOK) {
            return cleanup_ret;
        }
        clear_start_state(handle);
        return ret;
    }

    ret = qm_websocket_register_events(handle->ws, WEBSOCKET_EVENT_MAX, websocket_event_handler, handle);
    if (ret != QM_EOK) {
        int cleanup_ret = stop_destroy_ws(handle);
        if (cleanup_ret != QM_EOK) {
            JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE,
                    "websocket register cleanup deferred ret=%d", cleanup_ret);
            return cleanup_ret;
        }
        clear_start_state(handle);
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "websocket event register failed ret=%d", ret);
        return ret;
    }

    ret = start_core(handle);
    if (ret != QM_EOK) {
        int cleanup_ret = stop_destroy_ws(handle);
        if (cleanup_ret != QM_EOK) {
            JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE,
                    "websocket start cleanup deferred ret=%d", cleanup_ret);
            return cleanup_ret;
        }
        clear_start_state(handle);
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "core start failed ret=%d", ret);
        return ret;
    }

    ret = core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_RECONNECT, NULL);
    if (ret != QM_EOK) {
        int stop_ret = stop_core(handle);
        if (stop_ret != QM_EOK) return stop_ret;
        int cleanup_ret = stop_destroy_ws(handle);
        if (cleanup_ret != QM_EOK) {
            JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE,
                    "core start cleanup deferred ret=%d", cleanup_ret);
            return cleanup_ret;
        }
        clear_start_state(handle);
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "websocket start failed ret=%d", ret);
        return ret;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "start ok");
    return QM_EOK;
}

int joyinside_client_stop(joyinside_client_handle_t handle)
{
    int ret = QM_EOK;

    if (!handle) {
        return -QM_EINVAL;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "stop request connected=%s", bool_text(joyinside_client_is_connected(handle)));
    set_core_stop_requested(handle, QM_TRUE);
    if (handle->extensions.on_disconnect)
        handle->extensions.on_disconnect(handle->extensions.media_context);
    ret = stop_core(handle);
    if (ret != QM_EOK) return ret;
    (void)clear_connection_ready(handle);
    clear_pending_ack(handle);
    if (handle->ws) {
        ret = stop_destroy_ws(handle);
        if (ret != QM_EOK) {
            /* The WebSocket task still owns handle->ws after a timeout. Keep
             * the object reachable and let the caller retry shutdown instead
             * of starting a second task with the same platform name. */
            JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE,
                    "stop deferred: websocket still running ret=%d", ret);
            return ret;
        }
    }
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->protocol_active = 0U;
    qm_mutex_unlock(&handle->core_lock);
    joyinside_client_audiobook_stop_ping(handle);
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "stop done ret=%d", ret == -QM_EIO ? QM_EOK : ret);
    return ret == -QM_EIO ? QM_EOK : ret;
}

int joyinside_client_network_update(joyinside_client_handle_t handle,
                                    joyinside_client_network_state_t state)
{
    qm_websocket_network_state_t websocket_state;
    joyinside_client_network_state_t previous;

    if (handle == NULL) {
        return -QM_EINVAL;
    }
    if (state == JOYINSIDE_CLIENT_NETWORK_DOWN) {
        audio_admission(handle, QM_FALSE);
        websocket_state = QM_WEBSOCKET_NETWORK_DOWN;
    } else if (state == JOYINSIDE_CLIENT_NETWORK_UP) {
        websocket_state = QM_WEBSOCKET_NETWORK_UP;
    } else {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    previous = handle->network_state;
    handle->network_state = state;
    qm_mutex_unlock(&handle->core_lock);
    qm_mutex_lock(&handle->tx_lock, QM_WAIT_FOREVER);
    int ret = handle->ws ? qm_websocket_client_network_update(handle->ws, websocket_state) : QM_EOK;
    qm_mutex_unlock(&handle->tx_lock);
    if (ret == QM_EOK && previous != state) {
        (void)core_event_send(handle, JOYINSIDE_CLIENT_CORE_EVENT_RECONNECT, NULL);
    }
    return ret;
}

int joyinside_client_destroy(joyinside_client_handle_t handle)
{
    int ret;

    if (!handle) {
        return -QM_EINVAL;
    }
    /* The media backend may still have asynchronous callbacks targeting us. */
    if (handle->extensions.media_context) return -QM_EBUSY;
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "destroy request");
    set_core_stop_requested(handle, QM_TRUE);
    ret = stop_core(handle);
    if (ret != QM_EOK) return ret;
    if (handle->ws) {
        ret = stop_destroy_ws(handle);
        if (ret != QM_EOK) {
            JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE,
                    "destroy deferred: websocket still running ret=%d", ret);
            return ret;
        }
        handle->ws = NULL;
    }
    joyinside_client_audiobook_unregister(handle);
    joyinside_client_print_unregister(handle);
    clear_start_state(handle);
    clear_pending_ack(handle);
    qm_free(handle->rx_text);
    qm_free(handle->saved_context_kv_data);
    handle->saved_context_kv_data = NULL;
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_LIFECYCLE, "destroy done");
    free_config(&handle->config);
    qm_mutex_free(&handle->tx_lock);
    qm_mutex_free(&handle->audio_send_lock);
    qm_mutex_free(&handle->core_lock);
    qm_free(handle);
    return QM_EOK;
}

static int send_audio_binary(joyinside_client_handle_t handle,
                             const void *data,
                             uint32_t len,
                             uint32_t timeout_ms)
{
    int ret;

    if (!handle || !handle->ws || !data || len == 0U || len > INT32_MAX) {
        return -QM_EINVAL;
    }
    /* Binary channel is audio-only and is not resent; duplicates would corrupt a stream. */
    ret = qm_websocket_client_send_bin(handle->ws, (const char *)data, (int)len, timeout_ms);
    if (ret < QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "send binary audio failed ret=%d", ret);
    }
    return ret < QM_EOK ? ret : (ret == (int)len ? QM_EOK : -QM_EIO);
}

static int send_text_raw(joyinside_client_handle_t handle,
                         const char *text,
                         uint32_t timeout_ms)
{
    int ret = -QM_EINVAL;
    uint32_t attempt;
    uint32_t retry_count;
    uint32_t retry_delay_ms;
    uint32_t len;

    if (!handle || !text) {
        return -QM_EINVAL;
    }

    len = qm_strlen(text);
    if (len == 0U || len > INT32_MAX) return -QM_EINVAL;
    retry_count = send_retry_count(handle);
    retry_delay_ms = send_retry_delay_ms(handle);
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send text len=%lu timeout=%lums",
            (unsigned long)len, (unsigned long)timeout_ms);
    JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_PAYLOAD, "send text raw=%s", text);

    /* Only retry enqueue timeout; once queued, transport errors are reported asynchronously. */
    for (attempt = JOYINSIDE_CLIENT_SEND_FIRST_ATTEMPT; attempt <= retry_count; ++attempt) {
        ret = qm_mutex_lock(&handle->tx_lock, timeout_ms);
        if (ret != QM_EOK) return ret;
        if (!handle->ws || core_stop_requested(handle)) ret = -QM_EBUSY;
        else ret = qm_websocket_client_send_text(handle->ws, text, (int)len, timeout_ms);
        qm_mutex_unlock(&handle->tx_lock);
        if (ret != -QM_ETIMEOUT || timeout_ms == QM_NO_WAIT) {
            break;
        }
        if (attempt >= retry_count) {
            break;
        }
        JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send text timeout retry=%lu/%lu delay=%lums",
                (unsigned long)(attempt + 1U), (unsigned long)retry_count,
                (unsigned long)retry_delay_ms);
        if (retry_delay_ms) {
            qm_msleep((int)retry_delay_ms);
        }
    }

    if (ret < QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "send text failed ret=%d retry_count=%lu", ret,
                (unsigned long)retry_count);
    }
    return ret < QM_EOK ? ret : (ret == (int)len ? QM_EOK : -QM_EIO);
}

int joyinside_client_send_text(joyinside_client_handle_t handle,
                                  const char *text,
                                  uint32_t timeout_ms)
{
    if (!joyinside_client_is_connected(handle)) {
        JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send text rejected before joyinside_client connected");
        return -QM_EINVAL;
    }
    return send_text_raw(handle, text, timeout_ms);
}

static int send_audio_text(joyinside_client_handle_t handle,
                           const void *data,
                           uint32_t len,
                           int index, const char *round_mid,
                           uint32_t timeout_ms)
{
    cJSON *root = NULL;
    cJSON *content = NULL;
    char *base64 = NULL;
    uint32_t base64_len;
    uint32_t output_len = JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN;
    int ret = QM_EOK;

    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send audio json len=%lu index=%d timeout=%lums",
            (unsigned long)len, index, (unsigned long)timeout_ms);
    if (len > (UINT32_MAX - JOYINSIDE_CLIENT_BASE64_PAD_BYTES) /
                  JOYINSIDE_CLIENT_BASE64_GROUP_CHARS * JOYINSIDE_CLIENT_BASE64_GROUP_BYTES) {
        return -QM_EINVAL;
    }

    base64_len = JOYINSIDE_CLIENT_BASE64_GROUP_CHARS *
                 ((len + JOYINSIDE_CLIENT_BASE64_PAD_BYTES) / JOYINSIDE_CLIENT_BASE64_GROUP_BYTES);
    base64 = (char *)qm_malloc(base64_len + JOYINSIDE_CLIENT_CSTR_TERMINATOR_LEN);
    if (!base64) {
        return -QM_ENOMEM;
    }

    ret = qm_utils_base64encode((const uint8_t *)data, len, base64_len,
                                (uint8_t *)base64, &output_len);
    if (ret != QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "audio base64 encode failed ret=%d", ret);
        ret = -QM_EIO;
        goto cleanup;
    }
    base64[output_len] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_STATS, "audio base64 encoded input_len=%lu base64_len=%lu",
            (unsigned long)len, (unsigned long)output_len);

    root = cJSON_CreateObject();
    content = cJSON_CreateObject();
    if (!root || !content) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = add_common_fields(handle, root, JOYINSIDE_CLIENT_CONTENT_TYPE_AUDIO, QM_TRUE);
    if (ret != QM_EOK ||
        !cJSON_AddStringToObject(content, "audioBase64", base64) ||
        !cJSON_AddNumberToObject(content, "index", index)) {
        ret = ret != QM_EOK ? ret : -QM_ENOMEM;
        goto cleanup;
    }
    /* Hardware/audio handbook 5.3.7: end-side VAD mode tags every AUDIO chunk of a round with
     * the same content.mid, which the server echoes as the downlink round id. */
    if (round_mid[0] && !cJSON_AddStringToObject(content, "mid", round_mid)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (!cJSON_AddItemToObject(root, "content", content)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    content = NULL;

    ret = print_and_send(handle, root, timeout_ms, QM_FALSE);
    root = NULL;

cleanup:
    cJSON_Delete(root);
    cJSON_Delete(content);
    qm_free(base64);
    return ret;
}

/**
 * @brief Send one accepted audio slot or its ordered manual-finish marker.
 *
 * @par 中文说明
 * 发送一帧已接收音频，或按顺序发送其手动结束标记。
 *
 * The core worker owns the slot release path. Audio from a disconnected session
 * is intentionally discarded rather than replayed on a replacement connection.
 */
static void process_audio(joyinside_client_handle_t handle,
                          const joyinside_client_core_event_t *event)
{
    joyinside_client_audio_slot_t *slot = event->audio;
    qm_mutex_lock(&handle->audio_send_lock, QM_WAIT_FOREVER);
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    qm_bool valid = handle->audio_accepting &&
                    event->generation == handle->audio_generation;
    qm_mutex_unlock(&handle->core_lock);
    if (valid && slot) {
        (void)(slot->binary ? send_audio_binary(handle, slot->data, slot->len, 0U) :
               send_audio_text(handle, slot->data, slot->len, (int)slot->index, slot->mid, 0U));
    }
#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
    if (valid && event->id == JOYINSIDE_CLIENT_CORE_EVENT_AUDIO_FINISH) {
        (void)send_event_internal(handle, JOYINSIDE_CLIENT_EVENT_AUDIO_FINISH, NULL, 0U, QM_FALSE);
    }
#endif
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    if (slot) slot->busy = QM_FALSE;
    qm_mutex_unlock(&handle->core_lock);
    qm_mutex_unlock(&handle->audio_send_lock);
    /* Uplink audio is lossy: a dropped frame (e.g. transient -QM_EBUSY on a
     * non-blocking queue send) must never escalate into a transport error that
     * tears down the session. Real transport failures are reported separately
     * by the websocket client's own disconnect/error events. */
}

int joyinside_client_send_audio(joyinside_client_handle_t handle,
                                const void *data, uint32_t len)
{
    joyinside_client_core_event_t event = {0};
    int ret;
    if (!handle || !data || len == 0U || len > CONFIG_JOYINSIDE_CLIENT_AUDIO_PACKET_BYTES) {
        return -QM_EINVAL;
    }
    if (qm_mutex_lock(&handle->core_lock, QM_NO_WAIT) != QM_EOK) return -QM_EBUSY;
    if (!handle->audio_accepting || !handle->core_queue_valid || handle->core_stop_requested || handle->audio_index >= INT32_MAX) {
        qm_mutex_unlock(&handle->core_lock);
        return -QM_EBUSY;
    }
    /* Leave queue space for ordered stream-end commands. */
    if (handle->core_queue_count < JOYINSIDE_CLIENT_CORE_QUEUE_LEN - 1U) {
        for (uint32_t i = 0U; i < CONFIG_JOYINSIDE_CLIENT_AUDIO_QUEUE_LEN; ++i) {
            if (!handle->audio_slots[i].busy) {
                event.audio = &handle->audio_slots[i];
                break;
            }
        }
    }
    if (!event.audio) {
        qm_mutex_unlock(&handle->core_lock);
        return -QM_EFULL;
    }
    event.id = JOYINSIDE_CLIENT_CORE_EVENT_AUDIO;
    event.generation = handle->audio_generation;
    event.audio->busy = QM_TRUE;
    event.audio->len = len;
    event.audio->binary = handle->audio_binary;
    event.audio->index = handle->audio_index;
    event.audio->mid[0] = '\0';
    if (handle->config.voice.vad) {
        if (!handle->vad_round_mid[0]) qm_utils_random_uuid(handle->vad_round_mid);
        qm_memcpy(event.audio->mid, handle->vad_round_mid, sizeof(event.audio->mid));
    }
    qm_memcpy(event.audio->data, data, len);
    ret = qm_queue_send(&handle->core_queue, &event, sizeof(event));
    if (ret == QM_EOK) {
        handle->core_queue_count++;
        handle->audio_index++;
        (void)wake_core_locked(handle);
    } else {
        event.audio->busy = QM_FALSE;
    }
    qm_mutex_unlock(&handle->core_lock);
    return ret;
}

static int send_event_internal(joyinside_client_handle_t handle,
                               const char *event_type,
                               const char *event_data_json,
                               uint32_t timeout_ms,
                               qm_bool allow_before_ready)
{
    cJSON *root = NULL;
    cJSON *content = NULL;
    cJSON *event_data = NULL;
    int ret = QM_EOK;

    if (!handle || !event_type ||
        event_type[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return -QM_EINVAL;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send event type=%s event_data=%s timeout=%lums",
            event_type, present_text(event_data_json), (unsigned long)timeout_ms);

    root = cJSON_CreateObject();
    content = cJSON_CreateObject();
    if (!root || !content) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    if (event_data_json) {
        event_data = cJSON_Parse(event_data_json);
        if (!event_data || !cJSON_IsObject(event_data)) {
            ret = -QM_EINVAL;
            goto cleanup;
        }
    }

    ret = add_common_fields(handle, root, JOYINSIDE_CLIENT_CONTENT_TYPE_EVENT, QM_TRUE);
    if (ret != QM_EOK || !cJSON_AddStringToObject(content, "eventType", event_type)) {
        ret = ret != QM_EOK ? ret : -QM_ENOMEM;
        goto cleanup;
    }
    if (event_data) {
        if (!cJSON_AddItemToObject(content, "eventData", event_data)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        event_data = NULL;
    }
    if (!cJSON_AddItemToObject(root, "content", content)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    content = NULL;

    ret = print_and_send(handle, root, timeout_ms, allow_before_ready);
    root = NULL;

cleanup:
    cJSON_Delete(root);
    cJSON_Delete(content);
    cJSON_Delete(event_data);
    return ret;
}

static int send_ping_internal(joyinside_client_handle_t handle,
                              uint32_t timeout_ms)
{
    cJSON *root = NULL;
    cJSON *mid = NULL;
    int ret;

    if (!handle) {
        return -QM_EINVAL;
    }
    JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "send ping timeout=%lums", (unsigned long)timeout_ms);

    root = cJSON_CreateObject();
    if (!root) {
        return -QM_ENOMEM;
    }

    ret = add_common_fields(handle, root, JOYINSIDE_CLIENT_CONTENT_TYPE_PING, QM_TRUE);
    if (ret != QM_EOK) {
        cJSON_Delete(root);
        return ret;
    }
    mid = cJSON_GetObjectItemCaseSensitive(root, "mid");
    if (!cJSON_IsString(mid)) {
        cJSON_Delete(root);
        return -QM_EINVAL;
    }
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    (void)qm_snprintf(handle->heartbeat_mid, (int)sizeof(handle->heartbeat_mid), "%s", mid->valuestring);
    qm_mutex_unlock(&handle->core_lock);
    return print_and_send(handle, root, timeout_ms, QM_FALSE);
}

static int handle_heartbeat_pong(joyinside_client_handle_t handle, const char *mid)
{
    (void)mid;
    /* Only one PING is outstanding on this connection. The protocol does not
     * require the server-generated PONG mid to echo the PING mid. */
    if (!handle || !handle->heartbeat_pending) {
        JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
                "heartbeat pong ignored mid=%s", safe_text(mid));
        return -QM_EINVAL;
    }

    handle->heartbeat_pending = QM_FALSE;
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->heartbeat_mid[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    qm_mutex_unlock(&handle->core_lock);
    qm_utils_time_init(&handle->heartbeat_deadline);
    handle->heartbeat_fail_count = 0;
    JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
            "heartbeat pong received mid=%s", mid);
    return QM_EOK;
}

static int send_heartbeat_ping(joyinside_client_handle_t handle)
{
    int ret;

    if (!joyinside_client_is_connected(handle) || !handle->ws) {
        return QM_EOK;
    }

    if (handle->heartbeat_pending) {
        JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
                "heartbeat ping skipped: previous PONG is still pending");
        return QM_EOK;
    }

    /* Heartbeat work must not block the core queue behind streamed audio. If the
     * WebSocket send queue is full, the next heartbeat tick retries the enqueue. */
    ret = send_ping_internal(handle, QM_NO_WAIT);
    if (ret < QM_EOK) {
        if (ret == -QM_ETIMEOUT || ret == -QM_EFULL || ret == -QM_EBUSY) {
            qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
            handle->heartbeat_mid[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
            qm_mutex_unlock(&handle->core_lock);
            qm_utils_time_init(&handle->heartbeat_deadline);
            JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
                    "heartbeat ping enqueue deferred");
            return ret;
        }
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "heartbeat ping failed ret=%d", ret);
        handle->heartbeat_pending = QM_TRUE;
        return handle_heartbeat_failure(handle, "ping send failed", ret);
    } else {
        if (heartbeat_timeout_ms(handle) != JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN) {
            handle->heartbeat_pending = QM_TRUE;
            qm_utils_time_countdown_ms(&handle->heartbeat_deadline, heartbeat_timeout_ms(handle));
        }
        qm_utils_time_countdown_ms(&handle->heartbeat_ping_timer, heartbeat_interval_ms(handle));
        JOYINSIDE_CLIENT_LOGD(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
                "heartbeat ping sent response_timeout=%lums next_ping=%lums",
                (unsigned long)heartbeat_timeout_ms(handle),
                (unsigned long)heartbeat_interval_ms(handle));
    }
    return ret;
}

static int handle_heartbeat_failure(joyinside_client_handle_t handle, const char *reason, int code)
{
    (void)reason;
    qm_bool was_connected;
    int ret;

    if (!handle) {
        return QM_EOK;
    }

    if (!handle->heartbeat_pending) {
        return QM_EOK;
    }
    handle->heartbeat_pending = QM_FALSE;
    if (handle->heartbeat_fail_count < CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_MAX_FAILURES) {
        handle->heartbeat_fail_count++;
    }
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    handle->heartbeat_mid[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] = JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR;
    qm_mutex_unlock(&handle->core_lock);
    qm_utils_time_init(&handle->heartbeat_deadline);

    if (handle->heartbeat_fail_count < CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_MAX_FAILURES) {
        qm_utils_time_countdown_ms(&handle->heartbeat_ping_timer,
                                   heartbeat_timeout_ms(handle));
        JOYINSIDE_CLIENT_LOGW(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
                "heartbeat failure %u/%u: %s, retrying",
                (unsigned int)handle->heartbeat_fail_count,
                (unsigned int)CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_MAX_FAILURES,
                safe_text(reason));
        return code;
    }

    audio_admission(handle, QM_FALSE);
    was_connected = clear_connection_ready(handle);
    cancel_ack_deadline(handle);
    clear_pending_ack(handle);
    JOYINSIDE_CLIENT_LOGE(LOG_TAG, "heartbeat failure: %s, reconnecting", safe_text(reason));

    if (was_connected) {
        dispatch_event(handle, JOYINSIDE_CLIENT_EVENT_DISCONNECTED, NULL,
                       JOYINSIDE_CLIENT_PAYLOAD_EMPTY_LEN, NULL);
    }
    if (!handle->ws) {
        dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_WS, code);
        return code;
    }
    ret = qm_websocket_client_reconnect(handle->ws);
    if (ret != QM_EOK) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "heartbeat reconnect request failed ret=%d", ret);
        dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_WS, ret);
    }
    return ret;
}

static int handle_ack_timeout(joyinside_client_handle_t handle)
{
    if (!handle) return -QM_EINVAL;
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    qm_bool pending = handle->ack_pending;
    handle->ack_pending = QM_FALSE;
    qm_mutex_unlock(&handle->core_lock);
    if (!pending) return QM_EOK;
    /* A voice update is not idempotent: replaying it can interrupt a turn.
     * Rebuild the connection instead of resending on the existing channel. */
    cancel_ack_deadline(handle);
    clear_pending_ack(handle);
    audio_admission(handle, QM_FALSE);
    dispatch_error(handle, JOYINSIDE_CLIENT_ERROR_PROTOCOL, -QM_ETIMEOUT);
    if (handle->ws && qm_websocket_client_is_connected(handle->ws)) {
        (void)qm_websocket_client_reconnect(handle->ws);
    }
    return -QM_ETIMEOUT;
}

int joyinside_client_send_interrupt(joyinside_client_handle_t handle,
                                       uint32_t timeout_ms)
{
    return send_event_internal(handle, JOYINSIDE_CLIENT_EVENT_INTERRUPT, NULL, timeout_ms, QM_FALSE);
}

#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
int joyinside_client_send_audio_finish(joyinside_client_handle_t handle,
                                          uint32_t timeout_ms)
{
    joyinside_client_core_event_t event = {0};
    int ret;
    if (!handle) return -QM_EINVAL;
    ret = qm_mutex_lock(&handle->core_lock, timeout_ms);
    if (ret != QM_EOK) return ret;
    if (!handle->audio_accepting || !handle->core_queue_valid || handle->core_stop_requested) {
        ret = -QM_EBUSY;
    } else if (handle->core_queue_count >= JOYINSIDE_CLIENT_CORE_QUEUE_LEN) {
        ret = -QM_EFULL;
    } else {
        event.id = JOYINSIDE_CLIENT_CORE_EVENT_AUDIO_FINISH;
        event.generation = handle->audio_generation;
        ret = qm_queue_send(&handle->core_queue, &event, sizeof(event));
        if (ret == QM_EOK) {
            handle->core_queue_count++;
        handle->audio_index = 0U;
        handle->vad_round_mid[0] = '\0';
            (void)wake_core_locked(handle);
        }
    }
    qm_mutex_unlock(&handle->core_lock);
    return ret;
}
#endif

int joyinside_client_send_text_input(joyinside_client_handle_t handle,
                                        const char *input,
                                        uint32_t timeout_ms)
{
    cJSON *root = NULL;
    cJSON *content = NULL;
    int ret = QM_EOK;

    if (!handle || !input ||
        input[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return -QM_EINVAL;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send text input len=%lu timeout=%lums",
            (unsigned long)qm_strlen(input), (unsigned long)timeout_ms);

    root = cJSON_CreateObject();
    content = cJSON_CreateObject();
    if (!root || !content) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = add_common_fields(handle, root, JOYINSIDE_CLIENT_CONTENT_TYPE_TEXT, QM_TRUE);
    if (ret != QM_EOK || !cJSON_AddStringToObject(content, "input", input)) {
        ret = ret != QM_EOK ? ret : -QM_ENOMEM;
        goto cleanup;
    }
    if (!cJSON_AddItemToObject(root, "content", content)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    content = NULL;

    ret = print_and_send(handle, root, timeout_ms, QM_FALSE);
    root = NULL;

cleanup:
    cJSON_Delete(root);
    cJSON_Delete(content);
    return ret;
}

int joyinside_client_request_tts(joyinside_client_handle_t handle,
                                    const char *text,
                                    uint32_t timeout_ms)
{
    cJSON *event_data = NULL;
    char *event_data_json = NULL;
    int ret = QM_EOK;

    if (!text || text[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        qm_strlen(text) >= JOYINSIDE_CLIENT_TTS_TEXT_MAX_BYTES) {
        return -QM_EINVAL;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "request tts text_len=%lu timeout=%lums",
            (unsigned long)qm_strlen(text), (unsigned long)timeout_ms);

    event_data = cJSON_CreateObject();
    if (!event_data) {
        return -QM_ENOMEM;
    }
    if (!cJSON_AddStringToObject(event_data, "text", text)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    event_data_json = cJSON_PrintUnformatted(event_data);
    if (!event_data_json) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = send_event_internal(handle, JOYINSIDE_CLIENT_EVENT_TEXT_TO_SPEECH,
                              event_data_json, timeout_ms, QM_FALSE);

cleanup:
    cJSON_Delete(event_data);
    cJSON_free(event_data_json);
    return ret;
}

static int send_activity_content(joyinside_client_handle_t handle,
                                 cJSON *content,
                                 uint32_t timeout_ms)
{
    cJSON *root = NULL;
    int ret = QM_EOK;

    if (!handle || !content) {
        cJSON_Delete(content);
        return -QM_EINVAL;
    }

    root = cJSON_CreateObject();
    if (!root) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = add_common_fields(handle, root, JOYINSIDE_CLIENT_CONTENT_TYPE_ACTIVITY, QM_FALSE);
    if (ret != QM_EOK) {
        goto cleanup;
    }
    if (!cJSON_AddItemToObject(root, "content", content)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    content = NULL;

    ret = print_and_send(handle, root, timeout_ms, QM_FALSE);
    root = NULL;

cleanup:
    cJSON_Delete(root);
    cJSON_Delete(content);
    return ret;
}

/* Keep generic Activity framing private; public APIs expose stable business actions only. */
static int send_activity_internal(joyinside_client_handle_t handle,
                                  const char *activity_type,
                                  const char *kv_data_json,
                                  uint32_t effective_time_minutes,
                                  uint32_t timeout_ms)
{
    cJSON *content = NULL;
    cJSON *kv_data = NULL;
    int ret = QM_EOK;

    if (!handle || !activity_type ||
        activity_type[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return -QM_EINVAL;
    }
    if (effective_time_minutes > JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_MAX) {
        return -QM_EINVAL;
    }
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_UPLINK, "send activity type=%s kv_data=%s effective=%lumin timeout=%lums",
            activity_type, present_text(kv_data_json),
            (unsigned long)effective_time_minutes, (unsigned long)timeout_ms);

    ret = parse_object_or_empty(kv_data_json, &kv_data);
    if (ret != QM_EOK) {
        return ret;
    }

    content = cJSON_CreateObject();
    if (!content) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (!cJSON_AddStringToObject(content, "activityType", activity_type) ||
        !cJSON_AddItemToObject(content, "kvData", kv_data)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    kv_data = NULL;

    if (effective_time_minutes &&
        !cJSON_AddNumberToObject(content, "effectiveTimeMinutes", effective_time_minutes)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = send_activity_content(handle, content, timeout_ms);
    content = NULL;

cleanup:
    cJSON_Delete(content);
    cJSON_Delete(kv_data);
    return ret;
}

int joyinside_client_trigger_voice_chat(joyinside_client_handle_t handle,
                                           const char *kv_data_json,
                                           uint32_t timeout_ms)
{
    return send_activity_internal(handle, JOYINSIDE_CLIENT_ACTIVITY_VOICE_CHAT_TRIGGER,
                                  kv_data_json,
                                  JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_OMITTED,
                                  timeout_ms);
}

/* ---- CLIENT_UPDATE_CHAT_CONTEXT re-report (handbook 11.4) ---- */
#define JOYINSIDE_CLIENT_CONTEXT_REPORT_MARGIN_MS 30000U
#define JOYINSIDE_CLIENT_CONTEXT_REPORT_MIN_PERIOD_MS 1000U

static uint32_t context_report_period_ms(uint32_t effective_minutes)
{
    uint32_t total_ms;

    if (effective_minutes == JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_OMITTED) {
        return JOYINSIDE_CLIENT_CONTEXT_REPORT_MIN_PERIOD_MS;
    }
    total_ms = effective_minutes * 60U * 1000U;
    if (total_ms <= JOYINSIDE_CLIENT_CONTEXT_REPORT_MARGIN_MS) {
        return JOYINSIDE_CLIENT_CONTEXT_REPORT_MIN_PERIOD_MS;
    }
    return total_ms - JOYINSIDE_CLIENT_CONTEXT_REPORT_MARGIN_MS;
}

static void cancel_context_deadline(joyinside_client_handle_t handle)
{
    if (handle) cancel_protocol(handle, JOYINSIDE_CLIENT_CORE_EVENT_CONTEXT_REPORT);
}

static int schedule_context_deadline(joyinside_client_handle_t handle)
{
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    uint32_t period = handle->saved_context_kv_data ?
        context_report_period_ms(handle->saved_context_effective_minutes) : 0U;
    qm_mutex_unlock(&handle->core_lock);
    if (period) schedule_protocol(handle, JOYINSIDE_CLIENT_CORE_EVENT_CONTEXT_REPORT, period, period);
    else cancel_context_deadline(handle);
    return QM_EOK;
}

static int context_report_resend(joyinside_client_handle_t handle)
{
    if (!joyinside_client_is_connected(handle)) return -QM_EINVAL;
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    char *copy = handle->saved_context_kv_data ? qm_strdup(handle->saved_context_kv_data) : NULL;
    uint32_t effective_minutes = handle->saved_context_effective_minutes;
    qm_mutex_unlock(&handle->core_lock);
    if (!copy) return -QM_ENOENT;
    int ret = send_activity_internal(handle, JOYINSIDE_CLIENT_ACTIVITY_UPDATE_CHAT_CONTEXT,
                                     copy, effective_minutes, QM_NO_WAIT);
    qm_free(copy);
    return ret;
}

int joyinside_client_update_chat_context(joyinside_client_handle_t handle,
                                            uint32_t effective_time_minutes,
                                            const char *kv_data_json,
                                            uint32_t timeout_ms)
{
    if (!handle || effective_time_minutes < JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_MIN ||
        effective_time_minutes > JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_MAX) return -QM_EINVAL;
    char *copy = qm_strdup(kv_data_json && kv_data_json[0] ? kv_data_json : "{}");
    if (!copy) return -QM_ENOMEM;
    int ret = send_activity_internal(handle, JOYINSIDE_CLIENT_ACTIVITY_UPDATE_CHAT_CONTEXT,
                                     copy, effective_time_minutes, timeout_ms);
    if (ret != QM_EOK) { qm_free(copy); return ret; }
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    char *old = handle->saved_context_kv_data;
    handle->saved_context_kv_data = copy;
    handle->saved_context_effective_minutes = effective_time_minutes;
    qm_mutex_unlock(&handle->core_lock);
    qm_free(old);
    return schedule_context_deadline(handle);
}

int joyinside_client_update_coordinates_context(joyinside_client_handle_t handle,
                                                 const char *lat,
                                                 const char *lng,
                                                 uint32_t timeout_ms)
{
    cJSON *content = NULL;
    cJSON *coordinates = NULL;
    int ret = QM_EOK;

    if (!handle || !lat || !lng ||
        lat[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR ||
        lng[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] == JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        return -QM_EINVAL;
    }

    content = cJSON_CreateObject();
    coordinates = cJSON_CreateObject();
    if (!content || !coordinates) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (!cJSON_AddStringToObject(content, "activityType",
                                 JOYINSIDE_CLIENT_ACTIVITY_UPDATE_COORDINATES_CONTEXT) ||
        !cJSON_AddStringToObject(coordinates, "lat", lat) ||
        !cJSON_AddStringToObject(coordinates, "lng", lng) ||
        !cJSON_AddItemToObject(content, "coordinates", coordinates)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    coordinates = NULL;

    ret = send_activity_content(handle, content, timeout_ms);
    content = NULL;

cleanup:
    cJSON_Delete(content);
    cJSON_Delete(coordinates);
    return ret;
}

/**
 * @brief Resolve defaults and validate a full voice request. / 解析默认值并校验完整语音请求。
 *
 * @details This produces the canonical configuration stored for reconnect.
 * DEFAULT is resolved once; VAD deliberately chooses Base64 input because the
 * documented round mid is a JSON field. 本函数产出重连时复用的规范配置；
 * DEFAULT 只在此解析，VAD 必须选择 Base64 上行以携带 JSON 轮次 mid。
 */
static int normalize_voice(joyinside_client_voice_config_t *voice)
{
    joyinside_client_voice_config_t use_voice = *voice;
    if (use_voice.input_sample_rate == JOYINSIDE_CLIENT_SAMPLE_RATE_OMITTED) {
        use_voice.input_sample_rate = JOYINSIDE_CLIENT_DEFAULT_SAMPLE_RATE;
    }
    if (use_voice.output_sample_rate == JOYINSIDE_CLIENT_SAMPLE_RATE_OMITTED) {
        use_voice.output_sample_rate = JOYINSIDE_CLIENT_DEFAULT_SAMPLE_RATE;
    }
    if (!valid_input_codec(use_voice.input_codec) || !valid_output_codec(use_voice.output_codec)) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "update voice invalid codec input=%d output=%d",
                use_voice.input_codec, use_voice.output_codec);
        return -QM_EINVAL;
    }
    if (!valid_sample_rate(use_voice.input_sample_rate) ||
        !valid_sample_rate(use_voice.output_sample_rate)) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "update voice invalid sample rate input=%lu output=%lu",
                (unsigned long)use_voice.input_sample_rate,
                (unsigned long)use_voice.output_sample_rate);
        return -QM_EINVAL;
    }
    if ((use_voice.voice_speed != 0.0f && !(use_voice.voice_speed >= 0.8f && use_voice.voice_speed <= 1.2f)) ||
        (use_voice.voice_volume != 0.0f && !(use_voice.voice_volume >= 0.5f && use_voice.voice_volume <= 10.0f))) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "update voice invalid timbre speed or volume");
        return -QM_EINVAL;
    }
    if (use_voice.input_frame_size &&
        (use_voice.input_codec != JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS ||
         use_voice.input_frame_size > JOYINSIDE_CLIENT_OPUS_INPUT_FRAME_MAX_BYTES)) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "update voice invalid input frame codec=%s frame=%lu",
                codec_name(use_voice.input_codec),
                (unsigned long)use_voice.input_frame_size);
        return -QM_EINVAL;
    }
    if (use_voice.output_opus_cbr && use_voice.output_codec != JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "update voice invalid opus cbr with output codec=%s",
                codec_name(use_voice.output_codec));
        return -QM_EINVAL;
    }
    if (!valid_output_frame_ms(use_voice.output_codec, use_voice.output_frame_size_ms)) {
        JOYINSIDE_CLIENT_LOGE(LOG_TAG, "update voice invalid output frame codec=%s frame_ms=%lu",
                codec_name(use_voice.output_codec),
                (unsigned long)use_voice.output_frame_size_ms);
        return -QM_EINVAL;
    }
    if (use_voice.input_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_DEFAULT)
        use_voice.input_transport = use_voice.vad || !audio_binary_configured() ?
            JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64 : JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY;
    if (use_voice.output_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_DEFAULT)
        use_voice.output_transport = audio_binary_configured() ?
            JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY : JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64;
    if ((use_voice.input_transport != JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY &&
         use_voice.input_transport != JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64) ||
        (use_voice.output_transport != JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY &&
         use_voice.output_transport != JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64) ||
        (use_voice.vad && use_voice.input_transport != JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64) ||
        (use_voice.output_stream && use_voice.output_codec != JOYINSIDE_CLIENT_AUDIO_CODEC_MP3))
        return -QM_EINVAL;
    *voice = use_voice;
    return QM_EOK;
}

static int send_voice_update(joyinside_client_handle_t handle,
                             const joyinside_client_voice_config_t *voice,
                             uint32_t timeout_ms)
{
    joyinside_client_voice_config_t use_voice;
    cJSON *event_data = NULL;
    cJSON *audio = NULL;
    cJSON *input = NULL;
    cJSON *output = NULL;
    cJSON *timbre = NULL;
    cJSON *chat = NULL;
    char *event_data_json = NULL;
    int ret = QM_EOK;

    if (!handle) {
        return -QM_EINVAL;
    }

    if (voice) {
        use_voice = *voice;
    } else {
        use_voice = handle->config.voice;
    }
    ret = normalize_voice(&use_voice);
    if (ret != QM_EOK) return ret;
    JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL,
            "update voice input_binary=%s output_binary=%s input=%s/%lu frame=%lu output=%s/%lu frame_ms=%lu opus_cbr=%s timbre=%s chat_role=%s timeout=%lums",
            bool_text(use_voice.input_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY),
            bool_text(use_voice.output_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY),
            codec_name(use_voice.input_codec),
            (unsigned long)use_voice.input_sample_rate,
            (unsigned long)use_voice.input_frame_size,
            codec_name(use_voice.output_codec),
            (unsigned long)use_voice.output_sample_rate,
            (unsigned long)use_voice.output_frame_size_ms,
            bool_text(use_voice.output_opus_cbr),
            (use_voice.voice_speed != JOYINSIDE_CLIENT_FLOAT_ZERO ||
             use_voice.voice_volume != JOYINSIDE_CLIENT_FLOAT_ZERO) ? "yes" : "no",
            present_text(use_voice.chat_role_code),
            (unsigned long)timeout_ms);

    event_data = cJSON_CreateObject();
    audio = cJSON_CreateObject();
    input = cJSON_CreateObject();
    output = cJSON_CreateObject();
    if (!event_data || !audio || !input || !output) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    qm_bool input_binary = use_voice.input_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY;
    qm_bool output_binary = use_voice.output_transport == JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY;
    /* The protocol accepts one global flag only when both directions agree.
     * Directional fields preserve mixed transports without a second API.
     * 仅双向一致时发送全局 binary；混合格式使用方向字段，不另设重复接口。 */
    if (input_binary == output_binary) {
        if (!cJSON_AddBoolToObject(audio, "binary", input_binary)) { ret = -QM_ENOMEM; goto cleanup; }
    } else if (!cJSON_AddBoolToObject(input, "binary", input_binary) ||
               !cJSON_AddBoolToObject(output, "binary", output_binary)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    if (!cJSON_AddBoolToObject(audio, "vad", use_voice.vad ? JOYINSIDE_CLIENT_JSON_BOOL_TRUE : 0) ||
        !cJSON_AddStringToObject(input, "codec", codec_name(use_voice.input_codec))) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    ret = add_uint_string(input, "sampleRate", use_voice.input_sample_rate);
    if (ret != QM_EOK) {
        goto cleanup;
    }
    if (use_voice.input_frame_size) {
        ret = add_uint_string(input, "frameSize", use_voice.input_frame_size);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }

    if (!cJSON_AddStringToObject(output, "codec", codec_name(use_voice.output_codec))) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    ret = add_uint_string(output, "sampleRate", use_voice.output_sample_rate);
    if (ret != QM_EOK) {
        goto cleanup;
    }
    if (use_voice.output_codec == JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS) {
        if (!cJSON_AddBoolToObject(output, "enableOpusCbr", use_voice.output_opus_cbr)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    }
    if (use_voice.output_frame_size_ms) {
        ret = add_uint_string(output, "frameSizeMs", use_voice.output_frame_size_ms);
        if (ret != QM_EOK) {
            goto cleanup;
        }
    }
    /* Handbook 5.3/7.2: output.stream is MP3-only and requests streaming reception. */
    if (use_voice.output_codec == JOYINSIDE_CLIENT_AUDIO_CODEC_MP3) {
        if (!cJSON_AddBoolToObject(output, "stream", use_voice.output_stream)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
    }

    if (!cJSON_AddItemToObject(audio, "input", input)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    input = NULL;
    if (!cJSON_AddItemToObject(audio, "output", output)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    output = NULL;
    if (use_voice.voice_speed != JOYINSIDE_CLIENT_FLOAT_ZERO ||
        use_voice.voice_volume != JOYINSIDE_CLIENT_FLOAT_ZERO) {
        timbre = cJSON_CreateObject();
        if (!timbre) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        if (use_voice.voice_speed != JOYINSIDE_CLIENT_FLOAT_ZERO) {
            if (!cJSON_AddNumberToObject(timbre, "voiceSpeed", use_voice.voice_speed)) {
                ret = -QM_ENOMEM;
                goto cleanup;
            }
        }
        if (use_voice.voice_volume != JOYINSIDE_CLIENT_FLOAT_ZERO) {
            if (!cJSON_AddNumberToObject(timbre, "voiceVolume", use_voice.voice_volume)) {
                ret = -QM_ENOMEM;
                goto cleanup;
            }
        }
        if (!cJSON_AddItemToObject(audio, "timbre", timbre)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        timbre = NULL;
    }
    if (!cJSON_AddItemToObject(event_data, "audio", audio)) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }
    audio = NULL;

    if (use_voice.chat_role_code &&
        use_voice.chat_role_code[JOYINSIDE_CLIENT_CSTR_FIRST_INDEX] != JOYINSIDE_CLIENT_CSTR_TERMINATOR_CHAR) {
        chat = cJSON_CreateObject();
        if (!chat) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        if (!cJSON_AddStringToObject(chat, "roleCode", use_voice.chat_role_code) ||
            !cJSON_AddItemToObject(event_data, "chat", chat)) {
            ret = -QM_ENOMEM;
            goto cleanup;
        }
        chat = NULL;
    }

    event_data_json = cJSON_PrintUnformatted(event_data);
    if (!event_data_json) {
        ret = -QM_ENOMEM;
        goto cleanup;
    }

    ret = send_event_internal(handle, JOYINSIDE_CLIENT_EVENT_VOICE_CHAT_UPDATE,
                              event_data_json, timeout_ms, QM_TRUE);
    if (ret >= QM_EOK) {
        JOYINSIDE_CLIENT_LOGI(LOG_TAG, JOYINSIDE_CLIENT_LOG_CONTROL, "%s sent, waiting server confirmation",
                JOYINSIDE_CLIENT_EVENT_VOICE_CHAT_UPDATE);
    }

cleanup:
    cJSON_Delete(event_data);
    cJSON_Delete(audio);
    cJSON_Delete(input);
    cJSON_Delete(output);
    cJSON_Delete(timbre);
    cJSON_Delete(chat);
    cJSON_free(event_data_json);
    return ret;
}

static void free_voice_request(joyinside_client_voice_config_t *voice)
{
    /* queued_voice owns a duplicated role string; config.voice owns the prior
     * string until it is swapped into this request. queued_voice 持有复制的
     * role 字符串，config.voice 在交换前仍持有旧字符串。 */
    if (!voice) return;
    qm_free((void *)voice->chat_role_code);
    qm_free(voice);
}

/**
 * @brief Move one pending voice target into negotiation. / 将一个待更新目标转入协商。
 *
 * @details audio_send_lock prevents a frame encoded with the old format from
 * crossing the update boundary. The target is retained in config before the
 * ACK so reconnect can retry the same request. audio_send_lock 阻止旧格式帧
 * 跨越更新边界；ACK 前即保存目标，重连可继续协商该目标。
 */
static int update_voice_internal(joyinside_client_handle_t handle,
                                 qm_bool initial,
                                 uint32_t timeout_ms)
{
    if (!handle) return -QM_EINVAL;
    qm_mutex_lock(&handle->audio_send_lock, QM_WAIT_FOREVER);
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    joyinside_client_voice_config_t *request = handle->queued_voice;
    if (!initial && !request) {
        qm_mutex_unlock(&handle->core_lock);
        qm_mutex_unlock(&handle->audio_send_lock);
        return QM_EOK;
    }
    if (request) {
        const char *previous_role = handle->config.voice.chat_role_code;
        handle->config.voice = *request;
        request->chat_role_code = previous_role;
        handle->queued_voice = NULL;
    }
    audio_admission_locked(handle, QM_FALSE);
    qm_bool allowed = !handle->core_stop_requested && handle->ws && qm_websocket_client_is_connected(handle->ws);
    handle->ack_pending = allowed;
    qm_mutex_unlock(&handle->core_lock);
    free_voice_request(request);
    cancel_ack_deadline(handle);
    int ret = allowed ? send_voice_update(handle, &handle->config.voice, timeout_ms) : -QM_EBUSY;
    if (ret == QM_EOK) ret = schedule_ack_deadline(handle);
    if (ret != QM_EOK) clear_pending_ack(handle);
    qm_mutex_unlock(&handle->audio_send_lock);
    return ret;
}

/**
 * @brief Queue one owned runtime voice target. / 入队一个自持有的运行时语音目标。
 *
 * @details This entry point only copies and queues. The core task performs
 * socket I/O, so callers never block on network transmission. 入口只复制并
 * 入队，核心任务完成网络发送，调用方不会阻塞在 socket I/O 上。
 */
int joyinside_client_update_voice(joyinside_client_handle_t handle,
                                 const joyinside_client_voice_config_t *voice)
{
    if (!handle || !voice) return -QM_EINVAL;
    joyinside_client_voice_config_t *request = qm_malloc(sizeof(*request));
    if (!request) return -QM_ENOMEM;
    *request = *voice;
    int ret = normalize_voice(request);
    request->chat_role_code = NULL;
    if (ret == QM_EOK && voice->chat_role_code) {
        request->chat_role_code = qm_strdup(voice->chat_role_code);
        if (!request->chat_role_code) ret = -QM_ENOMEM;
    }
    if (ret != QM_EOK) { free_voice_request(request); return ret; }
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    if (!handle->connected || handle->ack_pending || handle->queued_voice ||
        !handle->core_queue_valid || handle->core_stop_requested) {
        ret = -QM_EBUSY;
    } else {
        handle->queued_voice = request;
        handle->audio_accepting = QM_FALSE;
        handle->core_pending |= 1U << JOYINSIDE_CLIENT_CORE_EVENT_VOICE_UPDATE;
        (void)wake_core_locked(handle);
        request = NULL;
    }
    qm_mutex_unlock(&handle->core_lock);
    free_voice_request(request);
    return ret;
}

uint32_t joyinside_client_get_log_flags(void)
{
    return g_joyinside_client_log_flags;
}

int joyinside_client_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)JOYINSIDE_CLIENT_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_joyinside_client_log_flags = log_flags;
    return QM_EOK;
}

qm_bool joyinside_client_is_connected(joyinside_client_handle_t handle)
{
    if (!handle) {
        return QM_FALSE;
    }
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    qm_bool connected = handle->connected && !handle->ack_pending && !handle->queued_voice;
    qm_mutex_unlock(&handle->core_lock);
    return connected;
}

int joyinside_client_set_user_context(joyinside_client_handle_t handle, void *user_context)
{
    if (!handle) {
        return -QM_EINVAL;
    }
    handle->config.user_context = user_context;
    return QM_EOK;
}

void *joyinside_client_get_user_context(joyinside_client_handle_t handle)
{
    return handle ? handle->config.user_context : NULL;
}

const char *joyinside_client_get_uid(joyinside_client_handle_t handle)
{
    return handle ? handle->uid : NULL;
}

/**
 * @brief Capture HTTP identity from the current ready WS session. / 快照当前就绪 WS 会话的 HTTP 身份。
 *
 * @details The Bearer string and JSON fields are copied before releasing the
 * lock, so an intervening reconnect cannot mix identities within one upload.
 * Bearer 字符串和 JSON 字段在解锁前复制，避免重连导致一次上传混入两套会话身份。
 */
int joyinside_client_http_context(joyinside_client_handle_t handle,
                                 cJSON *root, char **authorization)
{
    if (!handle || !cJSON_IsObject(root) || !authorization) return -QM_EINVAL;
    *authorization = NULL;
    char request_id[JOYINSIDE_CLIENT_UUID36_BUFFER_LEN];
    qm_utils_random_uuid(request_id);
    qm_mutex_lock(&handle->core_lock, QM_WAIT_FOREVER);
    int ret = -QM_EBUSY;
    if (handle->connected && !handle->core_stop_requested && handle->bot_id &&
        handle->session_id && handle->access_token && handle->access_token[0]) {
        int length = qm_snprintf(NULL, 0, "Bearer %s", handle->access_token);
        ret = -QM_ENOMEM;
        if (length > 0) *authorization = qm_malloc((uint32_t)length + 1U);
        if (*authorization) {
            qm_snprintf(*authorization, length + 1, "Bearer %s", handle->access_token);
            if (cJSON_AddStringToObject(root, "botId", handle->bot_id) &&
                cJSON_AddStringToObject(root, "sessionId", handle->session_id) &&
                cJSON_AddStringToObject(root, "requestId", request_id)) ret = QM_EOK;
        }
    }
    qm_mutex_unlock(&handle->core_lock);
    if (ret != QM_EOK) { qm_free(*authorization); *authorization = NULL; }
    return ret;
}
