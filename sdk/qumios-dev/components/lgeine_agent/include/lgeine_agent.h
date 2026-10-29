#ifndef LGEINE_AGENT_H
#define LGEINE_AGENT_H

#include "lgeine_agent_log.h"
#include "qm_errno.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Default helloReply timeout in milliseconds.
 */
#define LGEINE_AGENT_HELLO_TIMEOUT_MS 10000U

/**
 * @brief Opaque lgeine voice-platform agent handle.
 */
typedef struct lgeine_agent *lgeine_agent_handle_t;

/**
 * @brief Audio encoding values used by the wire protocol.
 */
typedef enum {
    LGEINE_AGENT_AUDIO_FORMAT_OPUS = 1, /*!< Opus-encoded audio */
    LGEINE_AGENT_AUDIO_FORMAT_PCM = 2,  /*!< Raw PCM audio */
} lgeine_agent_audio_format_t;

/**
 * @brief Negotiated uplink/downlink audio description.
 */
typedef struct {
    lgeine_agent_audio_format_t format; /*!< Opus or PCM */
    uint32_t rate;                      /*!< Sample rate in Hz */
    uint32_t bits;                      /*!< Bits per sample (source PCM for Opus) */
    uint32_t channel;                   /*!< 1 for mono, 2 for stereo */
    uint32_t frame_duration_ms;         /*!< Frame duration in milliseconds */
} lgeine_agent_audio_info_t;

/**
 * @brief Deployment environment used to build the default endpoint when
 *        start_params.uri is NULL.
 *
 * @note When `env` is left unset (`LGEINE_AGENT_ENV_DEFAULT`, the zero value),
 *       the component internally resolves it to the production environment.
 */
typedef enum {
    LGEINE_AGENT_ENV_DEFAULT = 0, /*!< Unset; resolved to production internally */
    LGEINE_AGENT_ENV_DEV = 1,     /*!< Development environment */
    LGEINE_AGENT_ENV_TEST = 2,    /*!< Test environment */
    LGEINE_AGENT_ENV_PROD = 3,    /*!< Production environment */
} lgeine_agent_env_t;

/**
 * @brief Device listen state sent with the `listen` method.
 */
typedef enum {
    LGEINE_AGENT_LISTEN_STATE_START = 1, /*!< Start listening */
    LGEINE_AGENT_LISTEN_STATE_STOP = 2,  /*!< Stop listening */
    LGEINE_AGENT_LISTEN_STATE_DETECT = 3, /*!< Report a detected wake word */
} lgeine_agent_listen_state_t;

/**
 * @brief Listen mode sent with the `listen` method.
 */
typedef enum {
    LGEINE_AGENT_LISTEN_MODE_AUTO = 1,     /*!< Automatic mode */
    LGEINE_AGENT_LISTEN_MODE_MANUAL = 2,   /*!< Manual push-to-talk mode */
    LGEINE_AGENT_LISTEN_MODE_REALTIME = 3, /*!< Realtime streaming mode */
} lgeine_agent_listen_mode_t;

/**
 * @brief Client-side abort reasons.
 */
typedef enum {
    LGEINE_AGENT_ABORT_REASON_USER_STOP = 1, /*!< User stopped the interaction */
    LGEINE_AGENT_ABORT_REASON_WAKEUP = 2,    /*!< A new wake word preempted the interaction */
} lgeine_agent_abort_reason_t;

/**
 * @brief Server-side abort reasons delivered via the `abort` event.
 */
typedef enum {
    LGEINE_AGENT_SERVER_ABORT_SERVER_POLICY = 1, /*!< Aborted by server policy */
    LGEINE_AGENT_SERVER_ABORT_SESSION_END = 2,   /*!< Session ended by the server */
} lgeine_agent_server_abort_reason_t;

/**
 * @brief TTS stream and sentence states.
 */
typedef enum {
    LGEINE_AGENT_TTS_STATE_START = 1,         /*!< TTS stream started */
    LGEINE_AGENT_TTS_STATE_SENTENCE_START = 2, /*!< A new sentence started */
    LGEINE_AGENT_TTS_STATE_SENTENCE_END = 3,   /*!< The current sentence ended */
    LGEINE_AGENT_TTS_STATE_STOP = 4,           /*!< TTS stream stopped */
} lgeine_agent_tts_state_t;

/**
 * @brief Classified TTS content type.
 */
typedef enum {
    LGEINE_AGENT_TTS_TYPE_UNKNOWN = 0, /*!< Unknown or unclassified type */
    LGEINE_AGENT_TTS_TYPE_CHAT = 1,    /*!< Chat reply */
    LGEINE_AGENT_TTS_TYPE_WELCOME = 2, /*!< Welcome message */
    LGEINE_AGENT_TTS_TYPE_GOODBYE = 3, /*!< Goodbye message */
    LGEINE_AGENT_TTS_TYPE_MUSIC = 4,   /*!< Music audio */
} lgeine_agent_tts_type_t;

/**
 * @brief Protocol and transport error categories.
 */
typedef enum {
    LGEINE_AGENT_ERROR_NONE = 0,           /*!< No error */
    LGEINE_AGENT_ERROR_WS,                 /*!< WebSocket transport error */
    LGEINE_AGENT_ERROR_HELLO_TIMEOUT,      /*!< helloReply timeout */
    LGEINE_AGENT_ERROR_REJECTED,           /*!< helloReply returned a non-zero code */
    LGEINE_AGENT_ERROR_PROTOCOL,           /*!< Invalid or unexpected protocol data */
    LGEINE_AGENT_ERROR_STATE,              /*!< Illegal state transition */
    LGEINE_AGENT_ERROR_MEMORY,             /*!< Memory allocation failure */
} lgeine_agent_error_type_t;

/**
 * @brief Events delivered by lgeine_agent.
 */
typedef enum {
    LGEINE_AGENT_EVENT_TRANSPORT_CONNECTED = 0, /*!< WebSocket transport established */
    LGEINE_AGENT_EVENT_READY,                    /*!< helloReply accepted, session negotiated */
    LGEINE_AGENT_EVENT_DISCONNECTED,             /*!< WebSocket transport lost */
    LGEINE_AGENT_EVENT_AUDIO,                    /*!< Binary downlink audio chunk */
    LGEINE_AGENT_EVENT_STT,                      /*!< Speech-to-text result */
    LGEINE_AGENT_EVENT_LLM,                      /*!< LLM emotion metadata */
    LGEINE_AGENT_EVENT_TTS,                      /*!< TTS state/type/subtitle */
    LGEINE_AGENT_EVENT_ABORT,                    /*!< Server-side abort reason */
    LGEINE_AGENT_EVENT_ERROR,                    /*!< Normalized component error */
} lgeine_agent_event_id_t;

/**
 * @brief Binary audio data delivered by the server.
 */
typedef struct {
    const uint8_t *data; /*!< Borrowed audio chunk */
    uint32_t data_len;   /*!< Current chunk length */
    qm_bool final;       /*!< True on the last chunk of the logical message */
} lgeine_agent_audio_t;

/**
 * @brief Ready event payload and negotiated downlink audio description.
 */
typedef struct {
    lgeine_agent_audio_info_t audio_info; /*!< Negotiated downlink format */
} lgeine_agent_ready_t;

/**
 * @brief Speech-to-text result from the server.
 */
typedef struct {
    const char *text; /*!< Borrowed complete hypothesis, not an incremental suffix */
    qm_bool is_final; /*!< True on the final result of a listen interaction */
} lgeine_agent_stt_t;

/**
 * @brief LLM emotion metadata from the server.
 */
typedef struct {
    const char *emotion; /*!< Borrowed emotion string */
} lgeine_agent_llm_t;

/**
 * @brief TTS state, type and sentence metadata from the server.
 */
typedef struct {
    lgeine_agent_tts_state_t state; /*!< Stream or sentence state */
    lgeine_agent_tts_type_t type;   /*!< Classified content type */
    const char *type_name;          /*!< Borrowed original type string */
    const char *text;               /*!< Borrowed subtitle bytes, or NULL */
} lgeine_agent_tts_t;

/**
 * @brief Server-side abort reason.
 */
typedef struct {
    lgeine_agent_server_abort_reason_t reason; /*!< Classified server abort reason */
} lgeine_agent_abort_t;

/**
 * @brief Normalized component error details.
 */
typedef struct {
    lgeine_agent_error_type_t type; /*!< Component error category */
    int code;                       /*!< Negative QUMI code or server helloReply code */
    const char *message;            /*!< Static or borrowed message */
} lgeine_agent_error_t;

/**
 * @brief Event payload.
 *
 * All pointers are borrowed and valid only during the callback. `raw_json` is
 * set for parsed server text messages. `session_id` is set when the message
 * carries the current session identifier.
 */
typedef struct {
    lgeine_agent_event_id_t id; /*!< Event discriminator */
    const char *session_id;     /*!< Borrowed session ID, when present */
    const char *raw_json;       /*!< Borrowed complete JSON message, when parsed */
    union {
        lgeine_agent_ready_t ready;
        lgeine_agent_audio_t audio;
        lgeine_agent_stt_t stt;
        lgeine_agent_llm_t llm;
        lgeine_agent_tts_t tts;
        lgeine_agent_abort_t abort;
        lgeine_agent_error_t error;
    } data; /*!< Payload selected by id */
} lgeine_agent_event_t;

/**
 * @brief Event callback.
 *
 * Callbacks arrive from the WebSocket task and the QMOS timer context. The
 * application must serialize its own shared state and must not call stop or
 * destroy from inside a callback.
 *
 * @param[in] handle Agent handle that emitted the event
 * @param[in] event Event payload, valid only during the callback
 * @param[in] user_ctx Context passed to lgeine_agent_init()
 */
typedef void (*lgeine_agent_event_cb_t)(lgeine_agent_handle_t handle,
                                        const lgeine_agent_event_t *event,
                                        void *user_ctx);

/**
 * @brief Runtime transport and buffer configuration.
 *
 * Zero-valued numeric fields use component defaults.
 */
typedef struct {
    uint32_t network_timeout_ms;    /*!< Zero uses Kconfig default */
    uint32_t hello_timeout_ms;      /*!< Zero uses LGEINE_AGENT_HELLO_TIMEOUT_MS */
    uint32_t reconnect_interval_ms; /*!< Zero uses Kconfig default */
    uint32_t ws_rx_size;            /*!< Zero uses Kconfig default */
    uint32_t max_text_message_size; /*!< Zero uses Kconfig default */
    int ws_stack_size;              /*!< Zero uses Kconfig default */
    int ws_priority;                /*!< Zero uses Kconfig default */
    int send_queue_size;            /*!< Zero uses Kconfig default */
    qm_bool disable_auto_reconnect; /*!< Do not reconnect after transport or hello failure */
    qm_bool disable_ping;           /*!< Disable WebSocket keepalive pings */
} lgeine_agent_config_t;

/**
 * @brief Initialization parameters.
 */
typedef struct {
    lgeine_agent_config_t config;     /*!< Zero-valued fields use component defaults */
    lgeine_agent_event_cb_t event_cb; /*!< Required event receiver */
    void *user_ctx;                   /*!< Borrowed callback context */
} lgeine_agent_init_config_t;

/**
 * @brief Parameters for one platform connection.
 *
 * @note `uri`, `pid`, `sn`, `access_token` and `audio_info` are copied by the
 *       component during start and only need to remain valid until start
 *       returns.
 */
typedef struct {
    const char *uri;             /*!< Optional WebSocket endpoint (wss://). When NULL,
                                      the endpoint is built from `env`. */
    lgeine_agent_env_t env;      /*!< Deployment environment used to build the
                                      endpoint when `uri` is NULL. */
    const char *pid;             /*!< Required product identifier, 1-64 ASCII chars */
    const char *sn;              /*!< Required device serial number, 1-64 ASCII chars */
    const char *access_token;    /*!< Required bearer token; the "Bearer " prefix is added */
    lgeine_agent_audio_info_t audio_info; /*!< Required uplink audio format */
} lgeine_agent_start_params_t;

/**
 * @brief Allocate an agent handle.
 *
 * The returned handle must be released with lgeine_agent_destroy().
 *
 * @param[in] config Initialization configuration. Must not be NULL and must
 *                   carry a non-NULL event_cb.
 * @return Allocated agent handle, or NULL on invalid arguments or allocation
 *         failure
 */
lgeine_agent_handle_t lgeine_agent_init(const lgeine_agent_init_config_t *config);

/**
 * @brief Create and start the asynchronous WebSocket transport.
 *
 * The connection and hello handshake run asynchronously; wait for
 * LGEINE_AGENT_EVENT_READY before sending audio or session commands.
 *
 * @param[in] handle Agent handle returned by lgeine_agent_init()
 * @param[in] params Connection parameters. Must not be NULL.
 * @return QM_EOK on success
 * @return -QM_EINVAL on invalid arguments
 * @return -QM_EBUSY if the agent is already started or destroying
 */
int lgeine_agent_start(lgeine_agent_handle_t handle,
                       const lgeine_agent_start_params_t *params);

/**
 * @brief Stop the transport and clear all negotiated session state.
 *
 * @param[in] handle Agent handle
 * @return QM_EOK on success
 * @return -QM_EINVAL if handle is NULL
 */
int lgeine_agent_stop(lgeine_agent_handle_t handle);

/**
 * @brief Stop the agent, free it, and invalidate the supplied handle.
 *
 * @param[in] handle Agent handle
 * @return QM_EOK on success
 * @return -QM_EINVAL if handle is NULL
 * @return -QM_EBUSY if destroy is called re-entrantly
 */
int lgeine_agent_destroy(lgeine_agent_handle_t handle);

/**
 * @brief Return true only after a valid helloReply has been received.
 *
 * @param[in] handle Agent handle
 * @return QM_TRUE when the agent is ready, QM_FALSE otherwise
 */
qm_bool lgeine_agent_is_ready(lgeine_agent_handle_t handle);

/**
 * @brief Queue one binary uplink audio frame for the negotiated session.
 *
 * The frame is sent asynchronously by the WebSocket task. When the uplink
 * audio format is PCM, the frame length must equal
 * `rate * bits / 8 * channel * frame_duration_ms / 1000`.
 *
 * @param[in] handle Agent handle
 * @param[in] data Audio frame bytes. Must not be NULL.
 * @param[in] len Audio frame length in bytes. Must be positive.
 * @param[in] timeout_ms Finite send queue wait in ms; zero tries without waiting.
 * @return QM_EOK on success
 * @return -QM_EINVAL on invalid arguments or mismatched PCM frame length
 * @return -QM_EBUSY if not ready or the protocol lock is contended; -QM_EFULL
 * if zero-wait queue admission fails. Accepted bytes are copied before return.
 */
int lgeine_agent_send_audio(lgeine_agent_handle_t handle,
                            const void *data,
                            uint32_t len,
                            uint32_t timeout_ms);

/**
 * @brief Start/stop listening or report a detected wake word.
 *
 * @param[in] handle Agent handle
 * @param[in] state Listen state to report
 * @param[in] mode Listen mode
 * @param[in] wake_word Detected wake word, or NULL unless state is DETECT
 * @param[in] timeout_ms Send queue wait timeout in ms; 0 uses network default
 * @return QM_EOK on success
 * @return -QM_EINVAL on invalid arguments
 * @return -QM_EBUSY if the agent is not ready
 */
int lgeine_agent_send_listen(lgeine_agent_handle_t handle,
                             lgeine_agent_listen_state_t state,
                             lgeine_agent_listen_mode_t mode,
                             const char *wake_word,
                             uint32_t timeout_ms);

/**
 * @brief Request text-to-speech playback of the given text.
 *
 * @param[in] handle Agent handle
 * @param[in] text Text to speak. Must not be NULL or empty.
 * @param[in] timeout_ms Send queue wait timeout in ms; 0 uses network default
 * @return QM_EOK on success
 * @return -QM_EINVAL if text is NULL or empty
 * @return -QM_EBUSY if the agent is not ready
 */
int lgeine_agent_send_speak(lgeine_agent_handle_t handle,
                            const char *text,
                            uint32_t timeout_ms);

/**
 * @brief Abort the current interaction with the given client-side reason.
 *
 * @param[in] handle Agent handle
 * @param[in] reason Client-side abort reason
 * @param[in] timeout_ms Send queue wait timeout in ms; 0 uses network default
 * @return QM_EOK on success
 * @return -QM_EINVAL on an unknown reason
 * @return -QM_EBUSY if the agent is not ready
 */
int lgeine_agent_send_abort(lgeine_agent_handle_t handle,
                            lgeine_agent_abort_reason_t reason,
                            uint32_t timeout_ms);

/**
 * @brief Request playback of the preset welcome message.
 *
 * @param[in] handle Agent handle
 * @param[in] timeout_ms Send queue wait timeout in ms; 0 uses network default
 * @return QM_EOK on success
 * @return -QM_EBUSY if the agent is not ready
 */
int lgeine_agent_send_welcome(lgeine_agent_handle_t handle,
                              uint32_t timeout_ms);

/**
 * @brief Request playback of the preset goodbye message.
 *
 * @param[in] handle Agent handle
 * @param[in] timeout_ms Send queue wait timeout in ms; 0 uses network default
 * @return QM_EOK on success
 * @return -QM_EBUSY if the agent is not ready
 */
int lgeine_agent_send_goodbye(lgeine_agent_handle_t handle,
                              uint32_t timeout_ms);

#ifdef __cplusplus
}
#endif

#endif
