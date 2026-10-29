#ifndef BAIDU_CHAT_AGENT_H
#define BAIDU_CHAT_AGENT_H

#include "qm_errno.h"
#include "qm_types.h"
#include "baidu_chat_agent_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief baidu_chat_agent only depends on QUMI component capabilities.
 *
 * The component boundary is intentionally limited to QUMI basic OS, memory,
 * logging, and network transport abstractions. Upper layers should not couple
 * this API to platform-specific socket, RTOS, TLS, or vendor SDK details.
 */

/**
 * @brief Default network timeout used when the caller omits timeout_ms.
 */
#define BAIDU_CHAT_AGENT_DEFAULT_TIMEOUT_MS 5000U

/**
 * @brief Known Baidu realtime interaction message prefixes.
 */
#define BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL "[Q]:"
#define BAIDU_CHAT_AGENT_PREFIX_ASR_MID "[Q]:[M]:"
#define BAIDU_CHAT_AGENT_PREFIX_ASR_FINAL_APPEND "[Q]:[C]:"
#define BAIDU_CHAT_AGENT_PREFIX_ASR_MID_APPEND "[Q]:[M]:[C]:"
#define BAIDU_CHAT_AGENT_PREFIX_AGENT_FINAL "[A]:"
#define BAIDU_CHAT_AGENT_PREFIX_AGENT_MID "[A]:[M]:"
#define BAIDU_CHAT_AGENT_PREFIX_AGENT_HANGUP "[A]:[H]:"
#define BAIDU_CHAT_AGENT_PREFIX_BREAK "[B]:"
#define BAIDU_CHAT_AGENT_PREFIX_TEXT_TO_AGENT "[T]:"
#define BAIDU_CHAT_AGENT_PREFIX_TEXT_TO_TTS "[TTS]:"
#define BAIDU_CHAT_AGENT_PREFIX_FUNCTION_CALL "[F]:"
#define BAIDU_CHAT_AGENT_PREFIX_EVENT "[E]:"
#define BAIDU_CHAT_AGENT_PREFIX_SET "[SET]:"

/**
 * @brief Common Baidu realtime interaction event payloads.
 */
#define BAIDU_CHAT_AGENT_EVENT_ASR_START_LONGTEXT_REC "[CMD]:[ASR_START_LONGTEXT_REC]"
#define BAIDU_CHAT_AGENT_EVENT_ASR_STOP_LONGTEXT_REC "[CMD]:[ASR_STOP_LONGTEXT_REC]"
#define BAIDU_CHAT_AGENT_EVENT_ASR_ENABLE_REALTIME "[CMD]:[ASR_ENABLE_REALTIME]"
#define BAIDU_CHAT_AGENT_EVENT_ASR_DISABLE_REALTIME "[CMD]:[ASR_DISABLE_REALTIME]"
#define BAIDU_CHAT_AGENT_EVENT_PLAY_AUDIO "[PLAY_AUDIO]"
#define BAIDU_CHAT_AGENT_EVENT_TTS_BEGIN_SPEAKING "[TTS_BEGIN_SPEAKING]"
#define BAIDU_CHAT_AGENT_EVENT_TTS_END_SPEAKING "[TTS_END_SPEAKING]"
#define BAIDU_CHAT_AGENT_EVENT_VOICE_COMING "[VOICE_COMING]"
#define BAIDU_CHAT_AGENT_EVENT_VOICE_DISAPPEAR "[VOICE_DISAPPEAR]"
#define BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_BEGIN "[REMOTE_PLAYER_BEGIN]"
#define BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_END "[REMOTE_PLAYER_END]"
#define BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_STOP "[CMD]:[REMOTE_PLAYER]:[STOP]"
#define BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_PAUSE "[CMD]:[REMOTE_PLAYER]:[PAUSE]"
#define BAIDU_CHAT_AGENT_EVENT_REMOTE_PLAYER_RESUME "[CMD]:[REMOTE_PLAYER]:[RESUME]"
#define BAIDU_CHAT_AGENT_EVENT_MEDIA_READY "[MEDIA]:[READY]:"

/**
 * @brief Common Baidu realtime interaction set payloads.
 */
#define BAIDU_CHAT_AGENT_SET_MEDIA_GENERATE_DISABLE "[MEDIA_GENERATE_MODE]:[FALSE]"

/**
 * @brief Opaque baidu_chat_agent handle.
 */
typedef struct baidu_chat_agent *baidu_chat_agent_handle_t;

/**
 * @brief baidu_chat_agent event id.
 */
typedef enum {
    BAIDU_CHAT_AGENT_EVENT_CONNECTED = 0,   /*!< WebSocket session is ready */
    BAIDU_CHAT_AGENT_EVENT_DISCONNECTED,    /*!< WebSocket session was closed */
    BAIDU_CHAT_AGENT_EVENT_AUDIO,           /*!< Downlink binary audio frame was received */
    BAIDU_CHAT_AGENT_EVENT_TEXT,            /*!< Unclassified text frame was received */
    BAIDU_CHAT_AGENT_EVENT_ASR,             /*!< User ASR text was received */
    BAIDU_CHAT_AGENT_EVENT_AGENT_TEXT,      /*!< Agent text was received */
    BAIDU_CHAT_AGENT_EVENT_BREAK,           /*!< Break message was received */
    BAIDU_CHAT_AGENT_EVENT_FUNCTION_CALL,   /*!< Function call message was received */
    BAIDU_CHAT_AGENT_EVENT_EVENT_TEXT,      /*!< Event text was received */
    BAIDU_CHAT_AGENT_EVENT_SET_TEXT,        /*!< Config text was received */
    BAIDU_CHAT_AGENT_EVENT_LICENSE,         /*!< License handshake state was updated */
    BAIDU_CHAT_AGENT_EVENT_ERROR,           /*!< Component or transport error occurred */
} baidu_chat_agent_event_id_t;

/**
 * @brief Classified text message type derived from prefix matching.
 */
typedef enum {
    BAIDU_CHAT_AGENT_TEXT_TYPE_UNKNOWN = 0,   /*!< Unknown or unsupported text */
    BAIDU_CHAT_AGENT_TEXT_TYPE_ASR_FINAL,     /*!< [Q]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_ASR_MID,       /*!< [Q]:[M]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_ASR_FINAL_APPEND, /*!< [Q]:[C]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_ASR_MID_APPEND,   /*!< [Q]:[M]:[C]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_AGENT_FINAL,   /*!< [A]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_AGENT_MID,     /*!< [A]:[M]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_AGENT_HANGUP,  /*!< [A]:[H]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_BREAK,         /*!< [B]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_TEXT_TO_AGENT, /*!< [T]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_TEXT_TO_TTS,   /*!< [TTS]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_FUNCTION_CALL, /*!< [F]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_EVENT,         /*!< [E]: */
    BAIDU_CHAT_AGENT_TEXT_TYPE_SET,           /*!< [SET]: */
} baidu_chat_agent_text_type_t;

/**
 * @brief baidu_chat_agent error category.
 */
typedef enum {
    BAIDU_CHAT_AGENT_ERROR_NONE = 0,     /*!< No error */
    BAIDU_CHAT_AGENT_ERROR_WS,           /*!< WebSocket transport or protocol error */
    BAIDU_CHAT_AGENT_ERROR_PROTOCOL,     /*!< Message payload or prefix classification error */
    BAIDU_CHAT_AGENT_ERROR_STATE,        /*!< API was called in an invalid state */
    BAIDU_CHAT_AGENT_ERROR_MEMORY,       /*!< Memory allocation failed */
} baidu_chat_agent_error_type_t;

/**
 * @brief Structured semantic classification for known `[E]:` and `[SET]:` payloads.
 */
typedef enum {
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_NONE = 0,             /*!< No known semantic match */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_ASR_START_LONGTEXT_REC, /*!< [E]:[CMD]:[ASR_START_LONGTEXT_REC] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_ASR_STOP_LONGTEXT_REC,  /*!< [E]:[CMD]:[ASR_STOP_LONGTEXT_REC] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_ASR_ENABLE_REALTIME,    /*!< [E]:[CMD]:[ASR_ENABLE_REALTIME] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_ASR_DISABLE_REALTIME,   /*!< [E]:[CMD]:[ASR_DISABLE_REALTIME] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_PLAY_AUDIO,             /*!< [E]:[PLAY_AUDIO] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_TTS_BEGIN_SPEAKING,     /*!< [E]:[TTS_BEGIN_SPEAKING] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_TTS_END_SPEAKING,       /*!< [E]:[TTS_END_SPEAKING] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_VOICE_COMING,           /*!< [E]:[VOICE_COMING] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_VOICE_DISAPPEAR,        /*!< [E]:[VOICE_DISAPPEAR] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_BEGIN,    /*!< [E]:[REMOTE_PLAYER_BEGIN] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_END,      /*!< [E]:[REMOTE_PLAYER_END] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_STOP,     /*!< [E]:[CMD]:[REMOTE_PLAYER]:[STOP] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_PAUSE,    /*!< [E]:[CMD]:[REMOTE_PLAYER]:[PAUSE] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_REMOTE_PLAYER_RESUME,   /*!< [E]:[CMD]:[REMOTE_PLAYER]:[RESUME] */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_MEDIA_READY,            /*!< [E]:[MEDIA]:[READY]:... */
    BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_SET_MEDIA_GENERATE_DISABLE,   /*!< [SET]:[MEDIA_GENERATE_MODE]:[FALSE] */
} baidu_chat_agent_message_semantic_t;

/**
 * @brief License handshake state derived from `[E]:[LIC]:...` messages.
 */
typedef enum {
    BAIDU_CHAT_AGENT_LICENSE_STATE_NONE = 0,   /*!< Unknown license state */
    BAIDU_CHAT_AGENT_LICENSE_STATE_MUST,       /*!< Server requires license activation */
    BAIDU_CHAT_AGENT_LICENSE_STATE_ACTIVE_SENT,/*!< Client sent `[LIC]:[ACTIVE]` */
    BAIDU_CHAT_AGENT_LICENSE_STATE_PASS,       /*!< Server reported license activation success */
    BAIDU_CHAT_AGENT_LICENSE_STATE_FAILED,     /*!< Server reported license activation failure */
} baidu_chat_agent_license_state_t;

/**
 * @brief Baidu realtime interaction authentication mode.
 */
typedef enum {
    BAIDU_CHAT_AGENT_AUTH_MODE_INSTANCE_TOKEN = 0, /*!< Query uses a/id/t/ac/[ptime]/[plen] */
    BAIDU_CHAT_AGENT_AUTH_MODE_API_KEY_SECRET,    /*!< Query uses a/ak/sk/ac/[ptime]/[plen]/[cfg] */
} baidu_chat_agent_auth_mode_t;

/**
 * @brief Remote player command for `[E]:[CMD]:[REMOTE_PLAYER]:...`.
 */
typedef enum {
    BAIDU_CHAT_AGENT_REMOTE_PLAYER_STOP = 0,   /*!< Send `[E]:[CMD]:[REMOTE_PLAYER]:[STOP]` */
    BAIDU_CHAT_AGENT_REMOTE_PLAYER_PAUSE,      /*!< Send `[E]:[CMD]:[REMOTE_PLAYER]:[PAUSE]` */
    BAIDU_CHAT_AGENT_REMOTE_PLAYER_RESUME,     /*!< Send `[E]:[CMD]:[REMOTE_PLAYER]:[RESUME]` */
} baidu_chat_agent_remote_player_op_t;

/**
 * @brief baidu_chat_agent runtime configuration.
 */
typedef struct {
    uint32_t timeout_ms;              /*!< Network timeout in milliseconds, 0 uses component default */
    uint32_t reconnect_interval_ms;   /*!< Reconnect interval in milliseconds, 0 uses component default */
    uint32_t ws_rx_size;              /*!< WebSocket receive buffer size, 0 uses component default */
    int ws_stack_size;                /*!< WebSocket task stack size, 0 uses component default */
    int ws_priority;                  /*!< WebSocket task priority, 0 uses component default */
    int send_queue_size;              /*!< WebSocket send queue size, 0 uses component default */
    qm_bool disable_auto_reconnect;   /*!< Disable WebSocket auto reconnect when QM_TRUE */
    qm_bool disable_ping;             /*!< Disable WebSocket keepalive ping when QM_TRUE */
} baidu_chat_agent_config_t;

/**
 * @brief Dynamic connection parameters for one baidu_chat_agent session.
 *
 * The component builds the final WebSocket URI from these documented query
 * fields instead of exposing redundant raw transport parameters to upper
 * layers.
 */
typedef struct {
    baidu_chat_agent_auth_mode_t auth_mode;  /*!< Connection authentication mode */
    const char *app_id;                      /*!< Query field `a` */
    const char *audio_codec;                 /*!< Query field `ac`, allowed values: raw/raw16k/pcma/pcmu/g722/opus */
    uint32_t packet_time_ms;                 /*!< Query field `ptime`, only for opus, allowed values: 20/40/60, 0 omits it */
    uint32_t packet_max_len;                 /*!< Query field `plen`, only for opus, 0 omits it */
    const char *headers;                     /*!< Optional extra HTTP headers */
    const char *user_id;                     /*!< Optional user identifier for upper-layer correlation; reused as license `uId` when auto-responding to `[E]:[LIC]:[MUST]:...` */
    const char *device_id;                   /*!< Optional device identifier for upper-layer correlation; reused as license `devId` when auto-responding to `[E]:[LIC]:[MUST]:...` */
    const char *license_key;                 /*!< Optional purchased license key; reused as license `licKey` when auto-responding to `[E]:[LIC]:[MUST]:...` */
    union {
        struct {
            const char *instance_id;         /*!< Query field `id`, required in instance-token mode */
            const char *token;               /*!< Query field `t`, required in instance-token mode */
        } instance_token;
        struct {
            const char *access_key;          /*!< Query field `ak`, required in api-key-secret mode */
            const char *secret_key;          /*!< Query field `sk`, required in api-key-secret mode */
            const char *cfg;                 /*!< Query field `cfg`, only used in api-key-secret mode; URL-encoded when present; JSON is validated structurally. Currently recognized keys include `audiocodec`, `language`, `samplerate`, `channels`, `dfda`, and `aopt`; `cfg.audiocodec` must match `ac` when present */
        } api_key_secret;
    } auth;
} baidu_chat_agent_start_params_t;

/**
 * @brief Error payload delivered with BAIDU_CHAT_AGENT_EVENT_ERROR.
 */
typedef struct {
    baidu_chat_agent_error_type_t type; /*!< Error category */
    int code;                           /*!< Component or transport error code */
    const char *message;                /*!< Borrowed error message string */
} baidu_chat_agent_error_t;

/**
 * @brief Text payload delivered by text-related events.
 */
typedef struct {
    baidu_chat_agent_text_type_t type;  /*!< Classified text type */
    baidu_chat_agent_message_semantic_t semantic; /*!< Structured semantic classification for known `[E]:` and `[SET]:` payloads */
    const char *prefix;                 /*!< Matched prefix, or NULL for unknown text */
    const char *text;                   /*!< Borrowed text payload without prefix */
    uint32_t text_len;                  /*!< Text payload length in bytes */
    qm_bool final;                      /*!< QM_TRUE when the text is a final result */
} baidu_chat_agent_text_t;

/**
 * @brief Audio payload delivered by BAIDU_CHAT_AGENT_EVENT_AUDIO.
 */
typedef struct {
    const uint8_t *data;            /*!< Borrowed audio chunk data */
    uint32_t data_len;              /*!< Current audio chunk length */
    uint32_t total_data_len;        /*!< Total payload length reported by WebSocket */
    uint32_t current_data_offset;   /*!< Current payload offset reported by WebSocket */
} baidu_chat_agent_audio_t;

/**
 * @brief License payload delivered by BAIDU_CHAT_AGENT_EVENT_LICENSE.
 */
typedef struct {
    baidu_chat_agent_license_state_t state; /*!< License handshake state */
    const char *text;                       /*!< Borrowed license detail text without `[E]:` prefix */
    uint32_t text_len;                      /*!< License detail text length in bytes */
} baidu_chat_agent_license_t;

/**
 * @brief baidu_chat_agent event payload.
 */
typedef struct {
    baidu_chat_agent_event_id_t id; /*!< Event id */
    union {
        baidu_chat_agent_text_t text;
        baidu_chat_agent_audio_t audio;
        baidu_chat_agent_license_t license;
        baidu_chat_agent_error_t error;
    } data;
} baidu_chat_agent_event_t;

/**
 * @brief baidu_chat_agent event callback.
 *
 * Payload pointers in @p event are borrowed and are valid only during the
 * callback.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] event Event payload
 * @param[in] user_ctx User context passed at init time
 */
typedef void (*baidu_chat_agent_event_cb_t)(baidu_chat_agent_handle_t handle,
                                            const baidu_chat_agent_event_t *event,
                                            void *user_ctx);

/**
 * @brief baidu_chat_agent init configuration.
 */
typedef struct {
    baidu_chat_agent_config_t config;      /*!< Runtime configuration */
    baidu_chat_agent_event_cb_t event_cb;  /*!< Required event callback */
    void *user_ctx;                        /*!< User context returned in callback */
} baidu_chat_agent_init_config_t;

/**
 * @brief Create a baidu_chat_agent handle.
 *
 * @param[in] config Init configuration
 * @return baidu_chat_agent handle on success, NULL on failure
 */
baidu_chat_agent_handle_t baidu_chat_agent_init(const baidu_chat_agent_init_config_t *config);

/**
 * @brief Get global baidu_chat_agent log categories.
 *
 * @return Active component log mask.
 */

/**
 * @brief Set global baidu_chat_agent log categories.
 *
 * This only controls logs emitted by baidu_chat_agent and does not change
 * logging in qm_websocket_client or other components.
 *
 * @param[in] log_flags Bitmask of baidu_chat_agent_log_flags_t.
 * @return QM_EOK on success, or -QM_EINVAL on invalid flags.
 */

/**
 * @brief Start a baidu_chat_agent session.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] params Dynamic start parameters
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_start(baidu_chat_agent_handle_t handle,
                           const baidu_chat_agent_start_params_t *params);

/**
 * @brief Stop the active baidu_chat_agent session.
 *
 * @param[in] handle baidu_chat_agent handle
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_stop(baidu_chat_agent_handle_t handle);

/**
 * @brief Destroy a baidu_chat_agent handle and release all owned resources.
 *
 * @param[in] handle baidu_chat_agent handle
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_destroy(baidu_chat_agent_handle_t handle);

/**
 * @brief Send uplink binary audio.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] data Audio buffer
 * @param[in] len Audio buffer length in bytes
 * @param[in] timeout_ms Finite queue wait in milliseconds; zero tries without waiting.
 * Accepted bytes are copied before return. Queue pressure returns -QM_EFULL
 * or -QM_EBUSY; the caller must not retry from a capture callback.
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_send_audio(baidu_chat_agent_handle_t handle,
                                const uint8_t *data,
                                uint32_t len,
                                uint32_t timeout_ms);

/**
 * @brief Send text to the AI agent using the `[T]:` prefix.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] text Input text
 * @param[in] timeout_ms Queue wait timeout in milliseconds, 0 uses configured default
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_send_text(baidu_chat_agent_handle_t handle,
                               const char *text,
                               uint32_t timeout_ms);

/**
 * @brief Send text directly to TTS using the `[TTS]:` prefix.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] text Input text
 * @param[in] timeout_ms Queue wait timeout in milliseconds, 0 uses configured default
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_send_text_to_tts(baidu_chat_agent_handle_t handle,
                                      const char *text,
                                      uint32_t timeout_ms);

/**
 * @brief Send an event command using the `[E]:` prefix.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] event Event text without prefix
 * @param[in] timeout_ms Queue wait timeout in milliseconds, 0 uses configured default
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_send_event(baidu_chat_agent_handle_t handle,
                                const char *event,
                                uint32_t timeout_ms);

/**
 * @brief Send `[E]:[CMD]:[ASR_START_LONGTEXT_REC]`.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] timeout_ms Queue wait timeout in milliseconds, 0 uses configured default
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_send_asr_start_longtext_rec(baidu_chat_agent_handle_t handle,
                                                 uint32_t timeout_ms);

/**
 * @brief Send `[E]:[CMD]:[ASR_STOP_LONGTEXT_REC]`.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] timeout_ms Queue wait timeout in milliseconds, 0 uses configured default
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_send_asr_stop_longtext_rec(baidu_chat_agent_handle_t handle,
                                                uint32_t timeout_ms);

/**
 * @brief Enable or disable ASR realtime event mode.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] enabled QM_TRUE sends enable, QM_FALSE sends disable
 * @param[in] timeout_ms Queue wait timeout in milliseconds, 0 uses configured default
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_set_asr_realtime_enabled(baidu_chat_agent_handle_t handle,
                                              qm_bool enabled,
                                              uint32_t timeout_ms);

/**
 * @brief Send a remote player control event.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] op Remote player operation
 * @param[in] timeout_ms Queue wait timeout in milliseconds, 0 uses configured default
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_send_remote_player_control(baidu_chat_agent_handle_t handle,
                                                baidu_chat_agent_remote_player_op_t op,
                                                uint32_t timeout_ms);

/**
 * @brief Send a break command using the `[B]:` prefix.
 *
 * @param[in] handle baidu_chat_agent handle
 * @param[in] reason Optional break reason, NULL sends an empty payload
 * @param[in] timeout_ms Queue wait timeout in milliseconds, 0 uses configured default
 * @return QM_EOK on success, negative error code on failure
 */
int baidu_chat_agent_send_break(baidu_chat_agent_handle_t handle,
                                const char *reason,
                                uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
