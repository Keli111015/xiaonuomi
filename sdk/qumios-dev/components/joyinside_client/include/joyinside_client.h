/**
 * @file joyinside_client.h
 * @brief Public JoyInside realtime voice client API. / JoyInside 实时语音客户端公开 API。
 *
 * Event payloads are borrowed for the duration of their callback unless an
 * individual member documents a longer lifetime.
 *
 * @par 中文说明
 * 除非成员另有说明，事件及其载荷仅在回调期间有效。应用若需异步处理，必须先自行复制。
 */

#ifndef JOYINSIDE_CLIENT_H
#define JOYINSIDE_CLIENT_H

#include "qm_errno.h"
#include "qm_types.h"
#include "joyinside_client_log.h"

#ifndef CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY
#define CONFIG_JOYINSIDE_CLIENT_AUDIO_BINARY 1
#endif

#ifndef CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
#define CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief JoyInside network availability state. / JoyInside 网络可用状态。
 */
typedef enum {
    JOYINSIDE_CLIENT_NETWORK_DOWN = 0,
    JOYINSIDE_CLIENT_NETWORK_UP,
} joyinside_client_network_state_t;

/**
 * @brief Default audio sample rate used when voice config omits sample rate.
 */
#define JOYINSIDE_CLIENT_DEFAULT_SAMPLE_RATE 16000U

/**
 * @brief Additional audio sample rates accepted by JoyInside Client voice config.
 */
#define JOYINSIDE_CLIENT_SAMPLE_RATE_24K 24000U
#define JOYINSIDE_CLIENT_SAMPLE_RATE_32K 32000U

/**
 * @brief Valid PCM downlink frame range in milliseconds.
 */
#define JOYINSIDE_CLIENT_PCM_OUTPUT_FRAME_MIN_MS 60U
#define JOYINSIDE_CLIENT_PCM_OUTPUT_FRAME_MAX_MS 120U

/**
 * @brief Valid Opus downlink frame sizes in milliseconds.
 */
#define JOYINSIDE_CLIENT_OPUS_OUTPUT_FRAME_10_MS 10U
#define JOYINSIDE_CLIENT_OPUS_OUTPUT_FRAME_20_MS 20U
#define JOYINSIDE_CLIENT_OPUS_OUTPUT_FRAME_40_MS 40U
#define JOYINSIDE_CLIENT_OPUS_OUTPUT_FRAME_60_MS 60U

/**
 * @brief Maximum Opus uplink frame size accepted by the component.
 */
#define JOYINSIDE_CLIENT_OPUS_INPUT_FRAME_MAX_BYTES 480U

/**
 * @brief Valid activity/chat context lifetime range in minutes.
 */
#define JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_MIN 1U
#define JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_MAX 1440U

/**
 * @brief Common JoyInside Client contentType values.
 */
#define JOYINSIDE_CLIENT_CONTENT_TYPE_EVENT "EVENT"
#define JOYINSIDE_CLIENT_CONTENT_TYPE_ACTIVITY "ACTIVITY"
#define JOYINSIDE_CLIENT_CONTENT_TYPE_AUDIO "AUDIO"
#define JOYINSIDE_CLIENT_CONTENT_TYPE_TTS "TTS"
#define JOYINSIDE_CLIENT_CONTENT_TYPE_TEXT "TEXT"
#define JOYINSIDE_CLIENT_CONTENT_TYPE_PING "PING"
#define JOYINSIDE_CLIENT_CONTENT_TYPE_PONG "PONG"
#define JOYINSIDE_CLIENT_CONTENT_TYPE_ASR "ASR"
#define JOYINSIDE_CLIENT_CONTENT_TYPE_AGENT "AGENT"

/**
 * @brief Stable JoyInside Client server eventType values delivered through JOYINSIDE_CLIENT_EVENT_SERVER_EVENT.
 *
 * Custom and non-standard eventType values are exposed through
 * joyinside_client_event_t.payload.server_event.type_name and
 * joyinside_client_event_t.payload.server_event.common.event_data.
 */
#define JOYINSIDE_SERVER_EVENT_CFG_BOT_EVENT "CFG_BOT_EVENT"
#define JOYINSIDE_SERVER_EVENT_VOICE_CHAT_UPDATED "SERVER_VOICE_CHAT_UPDATED"
#define JOYINSIDE_SERVER_EVENT_TTS_SENTENCE_START "TTS_SENTENCE_START"
#define JOYINSIDE_SERVER_EVENT_TTS_COMPLETE "TTS_COMPLETE"
#define JOYINSIDE_SERVER_EVENT_TURN_COMPLETE "COMPLETE"
#define JOYINSIDE_SERVER_EVENT_CALL_AGENT_START "CALL_AGENT_START_EVENT"
#define JOYINSIDE_SERVER_EVENT_CALL_AGENT_INTERRUPTED "CALL_AGENT_INTERRUPTED"
#define JOYINSIDE_SERVER_EVENT_CALL_SKILL "CALL_SKILL_EVENT"
#define JOYINSIDE_SERVER_EVENT_EMPTY_CONTENT "EMPTY_CONTENT"
#define JOYINSIDE_SERVER_EVENT_USER_AUDIT_FAIL "USER_AUDIT_FAIL"
#define JOYINSIDE_SERVER_EVENT_REPEAT_CLIENT_SESSION "REPEAT_CLIENT_SESSION"
#define JOYINSIDE_SERVER_EVENT_CHAT_CONTEXT_RECEIVED "CLIENT_CHAT_CONTEXT_RECEIVED"

/**
 * @brief JoyInside Client client eventType values sent with contentType EVENT.
 */
#define JOYINSIDE_CLIENT_EVENT_INTERRUPT "CLIENT_INTERRUPT"
#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
#define JOYINSIDE_CLIENT_EVENT_AUDIO_FINISH "CLIENT_AUDIO_FINISH"
#endif
#define JOYINSIDE_CLIENT_EVENT_TEXT_TO_SPEECH "CLIENT_INPUT_TEXT_TO_SPEECH"
#define JOYINSIDE_CLIENT_EVENT_VOICE_CHAT_UPDATE "CLIENT_VOICE_CHAT_UPDATE"

/**
 * @brief JoyInside Client client activityType values sent with contentType ACTIVITY.
 */
#define JOYINSIDE_CLIENT_ACTIVITY_VOICE_CHAT_TRIGGER "VOICE_CHAT_TRIGGER"
#define JOYINSIDE_CLIENT_ACTIVITY_UPDATE_CHAT_CONTEXT "CLIENT_UPDATE_CHAT_CONTEXT"
#define JOYINSIDE_CLIENT_ACTIVITY_UPDATE_COORDINATES_CONTEXT "CLIENT_UPDATE_COORDINATES_CONTEXT"

/**
 * @brief Opaque JoyInside Client handle.
 */
#ifndef JOYINSIDE_CLIENT_HANDLE_T_DEFINED
#define JOYINSIDE_CLIENT_HANDLE_T_DEFINED
typedef struct joyinside_client *joyinside_client_handle_t;
#endif

/**
 * @brief JoyInside Client event id. / JoyInside 客户端事件类型。
 */
typedef enum {
    JOYINSIDE_CLIENT_EVENT_CONNECTED = 0,     /*!< Voice config is confirmed and JoyInside Client is ready */
    JOYINSIDE_CLIENT_EVENT_DISCONNECTED,      /*!< Ready JoyInside Client connection is closed */
    JOYINSIDE_CLIENT_EVENT_AUDIO,             /*!< Binary audio data was received */
    JOYINSIDE_CLIENT_EVENT_TEXT,              /*!< Text message was received */
    JOYINSIDE_CLIENT_EVENT_SERVER_EVENT,      /*!< Text message whose contentType is EVENT */
    JOYINSIDE_CLIENT_EVENT_ERROR,             /*!< Protocol or WebSocket error */
    JOYINSIDE_CLIENT_EVENT_VOICE_UPDATED, /*!< Runtime voice update confirmed. / 运行期语音配置已确认。 */
} joyinside_client_event_id_t;

/**
 * @brief Known server eventType values mapped from contentType EVENT. / 从 EVENT 内容映射的已知服务端事件类型。
 *
 * JOYINSIDE_SERVER_EVENT_TYPE_CUSTOM keeps custom eventType strings available
 * through joyinside_client_event_t.payload.server_event.type_name and custom payloads
 * through joyinside_client_event_t.payload.server_event.common.event_data.
 */
typedef enum {
    JOYINSIDE_SERVER_EVENT_TYPE_UNKNOWN = 0,       /*!< No server eventType parsed */
    JOYINSIDE_SERVER_EVENT_TYPE_CFG_BOT,           /*!< CFG_BOT_EVENT */
    JOYINSIDE_SERVER_EVENT_TYPE_VOICE_CHAT_UPDATED, /*!< SERVER_VOICE_CHAT_UPDATED, consumed internally */
    JOYINSIDE_SERVER_EVENT_TYPE_TTS_SENTENCE_START, /*!< TTS_SENTENCE_START */
    JOYINSIDE_SERVER_EVENT_TYPE_TTS_COMPLETE,      /*!< TTS_COMPLETE */
    JOYINSIDE_SERVER_EVENT_TYPE_TURN_COMPLETE,     /*!< COMPLETE */
    JOYINSIDE_SERVER_EVENT_TYPE_CALL_AGENT_START,  /*!< CALL_AGENT_START_EVENT */
    JOYINSIDE_SERVER_EVENT_TYPE_CALL_AGENT_INTERRUPTED, /*!< CALL_AGENT_INTERRUPTED */
    JOYINSIDE_SERVER_EVENT_TYPE_CALL_SKILL,        /*!< CALL_SKILL_EVENT */
    JOYINSIDE_SERVER_EVENT_TYPE_EMPTY_CONTENT,     /*!< EMPTY_CONTENT */
    JOYINSIDE_SERVER_EVENT_TYPE_USER_AUDIT_FAIL,   /*!< USER_AUDIT_FAIL */
    JOYINSIDE_SERVER_EVENT_TYPE_REPEAT_CLIENT_SESSION, /*!< REPEAT_CLIENT_SESSION */
    JOYINSIDE_SERVER_EVENT_TYPE_CHAT_CONTEXT_RECEIVED, /*!< CLIENT_CHAT_CONTEXT_RECEIVED */
    JOYINSIDE_SERVER_EVENT_TYPE_CUSTOM,            /*!< Custom or non-standard eventType */
} joyinside_server_event_type_t;

/**
 * @brief Known non-EVENT text contentType values. / 非 EVENT 文本消息的已知 contentType。
 */
typedef enum {
    JOYINSIDE_CLIENT_TEXT_TYPE_UNKNOWN = 0,  /*!< Unknown or unsupported contentType */
    JOYINSIDE_CLIENT_TEXT_TYPE_ASR = 1,      /*!< ASR */
    JOYINSIDE_CLIENT_TEXT_TYPE_AGENT = 2,    /*!< AGENT */
    JOYINSIDE_CLIENT_TEXT_TYPE_ACTIVITY = 3, /*!< ACTIVITY proactive text */
    JOYINSIDE_CLIENT_TEXT_TYPE_TTS = 4,      /*!< TTS audio text payload */
    JOYINSIDE_CLIENT_TEXT_TYPE_PONG = 5,     /*!< PONG */
} joyinside_client_text_type_t;

/**
 * @brief Known JoyInside expression codes carried by CALL_SKILL_EVENT. / CALL_SKILL_EVENT 携带的已知表情或动作编码。
 *
 * The protocol names the visual field eventData.data.args.EMOTION, although
 * its vocabulary also includes gestures such as nod and disagree. Applications
 * can use expression_code when they need the exact provider value, including a
 * future code not represented by this enum. The optional TTS_EMOTION field may
 * use either a bare code such as happy or the visual field's happy_1 form.
 */
typedef enum {
    JOYINSIDE_CLIENT_EXPRESSION_UNKNOWN = 0,   /*!< Missing or unrecognized code */
    JOYINSIDE_CLIENT_EXPRESSION_ANGRY,         /*!< angry_1 */
    JOYINSIDE_CLIENT_EXPRESSION_ANXIOUS,       /*!< anxious_1 */
    JOYINSIDE_CLIENT_EXPRESSION_BLAMED,        /*!< blamed_1 */
    JOYINSIDE_CLIENT_EXPRESSION_CONFUSED,      /*!< confused_1 */
    JOYINSIDE_CLIENT_EXPRESSION_COQUETRY,      /*!< coquetry_1 */
    JOYINSIDE_CLIENT_EXPRESSION_DISAPPOINT,    /*!< disappoint_1 */
    JOYINSIDE_CLIENT_EXPRESSION_DISDAINFUL,    /*!< disdainful_1 */
    JOYINSIDE_CLIENT_EXPRESSION_EXCITING,      /*!< exciting_1 */
    JOYINSIDE_CLIENT_EXPRESSION_EXPECT,        /*!< expect_1 */
    JOYINSIDE_CLIENT_EXPRESSION_FEAR,          /*!< fear_1 */
    JOYINSIDE_CLIENT_EXPRESSION_HAPPY,         /*!< happy_1 */
    JOYINSIDE_CLIENT_EXPRESSION_HATE,          /*!< hate_1 */
    JOYINSIDE_CLIENT_EXPRESSION_HESITATE,      /*!< hesitate_1 */
    JOYINSIDE_CLIENT_EXPRESSION_INQUISITIVE,   /*!< inquisitive_1 */
    JOYINSIDE_CLIENT_EXPRESSION_PEACE,         /*!< peace_1 */
    JOYINSIDE_CLIENT_EXPRESSION_SAD,           /*!< sad_1 */
    JOYINSIDE_CLIENT_EXPRESSION_SHOCK,         /*!< shock_1 */
    JOYINSIDE_CLIENT_EXPRESSION_SHY,           /*!< shy_1 */
    JOYINSIDE_CLIENT_EXPRESSION_SICK,          /*!< sick_1 */
    JOYINSIDE_CLIENT_EXPRESSION_SORROWFUL,     /*!< sorrowful_1 */
    JOYINSIDE_CLIENT_EXPRESSION_WRONGED,       /*!< wronged_1 */
    JOYINSIDE_CLIENT_EXPRESSION_DISAGREE,      /*!< disagree_1 */
    JOYINSIDE_CLIENT_EXPRESSION_NOD,           /*!< nod_1 */
} joyinside_client_expression_t;

/**
 * @brief JoyInside Client error category. / JoyInside 客户端错误类别。
 */
typedef enum {
    JOYINSIDE_CLIENT_ERROR_NONE = 0,          /*!< No error */
    JOYINSIDE_CLIENT_ERROR_WS,                /*!< WebSocket transport or protocol failed */
    JOYINSIDE_CLIENT_ERROR_PROTOCOL,          /*!< JoyInside Client payload is invalid */
    JOYINSIDE_CLIENT_ERROR_BUFFER,            /*!< Buffer is too small */
} joyinside_client_error_type_t;

/**
 * @brief Audio codec used in JoyInside Client voice config. / JoyInside 语音配置使用的音频编解码类型。
 */
typedef enum {
    JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS = 0,    /*!< Opus encoded audio */
    JOYINSIDE_CLIENT_AUDIO_CODEC_PCM,         /*!< PCM audio */
    JOYINSIDE_CLIENT_AUDIO_CODEC_MP3,         /*!< MP3 audio, valid only for downlink output */
} joyinside_client_audio_codec_t;

/**
 * @brief WebSocket network behavior. / WebSocket 网络行为配置。
 */
typedef struct {
    uint32_t timeout_ms;                  /*!< WebSocket network timeout in milliseconds, 0 uses CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS */
    uint32_t send_retry_count;            /*!< Text send queue timeout retry count, 0 uses CONFIG_JOYINSIDE_CLIENT_SEND_RETRY_COUNT */
    uint32_t send_retry_delay_ms;         /*!< Delay between text send retries, 0 uses CONFIG_JOYINSIDE_CLIENT_SEND_RETRY_DELAY_MS */
    uint32_t ack_timeout_ms;              /*!< Server ack wait timeout, 0 uses CONFIG_JOYINSIDE_CLIENT_ACK_TIMEOUT_MS */
} joyinside_client_network_config_t;

/**
 * @brief Buffer sizes used by JoyInside Client WebSocket component. / JoyInside WebSocket 组件的缓冲区配置。
 */
typedef struct {
    uint32_t ws_rx_size;                  /*!< WebSocket receive buffer size, 0 uses CONFIG_JOYINSIDE_CLIENT_WS_RX_SIZE */
    uint32_t max_text_size;               /*!< Complete JSON message byte limit, 0 uses CONFIG_JOYINSIDE_CLIENT_MAX_TEXT_SIZE */
} joyinside_client_buffer_config_t;

/**
 * @brief WebSocket/core task and send queue configuration. / WebSocket、核心任务及发送队列配置。
 */
typedef struct {
    int ws_stack_size;                    /*!< WebSocket task stack size, 0 uses CONFIG_JOYINSIDE_CLIENT_WS_TASK_STACK_SIZE */
    int ws_priority;                      /*!< WebSocket task priority, 0 uses CONFIG_JOYINSIDE_CLIENT_WS_TASK_PRIORITY */
    int core_stack_size;                  /*!< JoyInside Client core task stack size, 0 uses CONFIG_JOYINSIDE_CLIENT_CORE_TASK_STACK_SIZE */
    int core_priority;                    /*!< JoyInside Client core task priority, 0 uses CONFIG_JOYINSIDE_CLIENT_CORE_TASK_PRIORITY */
    int send_queue_size;                  /*!< WebSocket send queue size, 0 uses CONFIG_JOYINSIDE_CLIENT_SEND_QUEUE_SIZE */
} joyinside_client_task_config_t;

/**
 * @brief Application-level heartbeat configuration. / 应用层 PING/PONG 心跳配置。
 */
typedef struct {
    uint32_t interval_ms;                 /*!< PING interval in milliseconds, 0 uses CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_INTERVAL_MS */
    uint32_t timeout_ms;                  /*!< JSON PONG response timeout in milliseconds, 0 uses CONFIG_JOYINSIDE_CLIENT_HEARTBEAT_TIMEOUT_MS */
    qm_bool disabled;                     /*!< Disable internal JoyInside Client PING scheduling when QM_TRUE */
} joyinside_client_heartbeat_config_t;

/** @brief Audio wire representation. / 音频传输格式。 */
typedef enum {
    JOYINSIDE_CLIENT_AUDIO_TRANSPORT_DEFAULT = 0, /*!< Build default; VAD input uses Base64. / 构建默认值；VAD 上行使用 Base64。 */
    JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BINARY,      /*!< WebSocket binary frames. / WebSocket 二进制帧。 */
    JOYINSIDE_CLIENT_AUDIO_TRANSPORT_BASE64,      /*!< JSON with Base64 audio. / JSON 内的 Base64 音频。 */
} joyinside_client_audio_transport_t;

/**
 * @brief JoyInside Client voice chat audio configuration. / JoyInside 语音会话音频配置。
 */
typedef struct {
    joyinside_client_audio_codec_t input_codec; /*!< Uplink audio codec */
    uint32_t input_sample_rate;           /*!< Uplink sample rate, 0 uses JOYINSIDE_CLIENT_DEFAULT_SAMPLE_RATE */
    uint32_t input_frame_size;            /*!< Optional Opus uplink frame size in bytes */
    joyinside_client_audio_codec_t output_codec; /*!< Downlink audio codec */
    uint32_t output_sample_rate;          /*!< Downlink sample rate, 0 uses JOYINSIDE_CLIENT_DEFAULT_SAMPLE_RATE */
    uint32_t output_frame_size_ms;        /*!< Optional downlink frame size in ms, 0 omits frameSizeMs */
    qm_bool output_opus_cbr;              /*!< Opus CBR flag, always sent for Opus. / Opus 下明确发送 CBR 开关。 */
    float voice_speed;                    /*!< TTS speed 0.8–1.2; 0 omits the field. / 语速 0.8～1.2，0 省略。 */
    float voice_volume;                   /*!< TTS volume 0.5–10; 0 omits the field. / 音量 0.5～10，0 省略。 */
    const char *chat_role_code;           /*!< Optional chat role code */
    joyinside_client_audio_transport_t input_transport;  /*!< Uplink representation. / 上行传输格式。 */
    joyinside_client_audio_transport_t output_transport; /*!< Downlink representation. / 下行传输格式。 */
    qm_bool vad;                          /*!< End-side VAD; requires Base64 input. / 端侧 VAD，要求 Base64 上行。 */
    qm_bool output_stream;                /*!< Optional MP3 streaming flag, sent as audio.output.stream only for MP3 */
} joyinside_client_voice_config_t;

/**
 * @brief Voice chat configuration payload reported by CFG_BOT_EVENT. / CFG_BOT_EVENT 上报的语音配置参考值。
 *
 * CFG_BOT_EVENT is exposed to the application for reference only. The component
 * applies voice transport state only after SERVER_VOICE_CHAT_UPDATED.
 *
 * String pointers are borrowed from the received JSON and are valid only during
 * the event callback.
 *
 * @par 中文说明
 * CFG_BOT_EVENT 仅供应用参考。组件仅在收到并校验 SERVER_VOICE_CHAT_UPDATED 后切换实际传输参数。
 */
typedef struct {
    const char *voice_name;               /*!< eventData.timbre.voiceName, NULL if absent */
    float voice_speed;                    /*!< eventData.timbre.voiceSpeed, 0 if absent */
    const char *tts_aue;                  /*!< eventData.tts.aue, NULL if absent */
    uint32_t tts_bit;                     /*!< eventData.tts.bit, 0 if absent */
    uint32_t tts_channels;                /*!< eventData.tts.channels, 0 if absent */
    uint32_t tts_sample_rate;             /*!< eventData.tts.sr, 0 if absent */
    const char *device_model;             /*!< eventData.deviceModel, NULL if absent */
    const char *device_id;                /*!< eventData.deviceId, NULL if absent */
} joyinside_client_bot_config_t;

/**
 * @brief Streaming ASR result reported by contentType ASR. / contentType 为 ASR 时上报的流式识别结果。
 *
 * String pointers are borrowed from the received JSON and are valid only during
 * the event callback.
 */
typedef struct {
    const char *text;                      /*!< content.text, NULL if absent */
    const char *text_type;                 /*!< content.textType, for example IS_FINAL */
    const char *lang;                      /*!< content.lang, NULL if absent */
} joyinside_client_asr_result_t;

/**
 * @brief Streaming agent text response reported by contentType AGENT or ACTIVITY. / AGENT 或 ACTIVITY 的流式智能体文本结果。
 *
 * String pointers are borrowed from the received JSON and are valid only during
 * the event callback.
 */
typedef struct {
    const char *round_id;                  /*!< content.roundId, NULL if absent */
    const char *role;                      /*!< content.role, NULL if absent */
    const char *content;                   /*!< content.content, NULL if absent */
    const char *reasoning_content;         /*!< content.reasoningContent, NULL if absent */
    const char *finish_reason;             /*!< content.finishReason, NULL if absent */
} joyinside_client_agent_result_t;

/**
 * @brief Streaming TTS audio response reported by contentType TTS. / contentType 为 TTS 时上报的流式音频结果。
 *
 * String pointers are borrowed from the received JSON and are valid only during
 * the event callback.
 */
typedef struct {
    const char *round_id;                  /*!< content.roundId, NULL if absent */
    const char *audio_base64;              /*!< content.audioBase64, NULL if absent */
    const char *audio_aue;                 /*!< content.audioAue, NULL if absent */
    uint32_t audio_len;                    /*!< Decoded audio byte length dispatched through JOYINSIDE_CLIENT_EVENT_AUDIO */
    uint32_t audio_duration_ms;            /*!< content.audioDuration, 0 if absent */
    qm_bool finish;                        /*!< content.finish */
} joyinside_client_tts_result_t;

/**
 * @brief Voice chat configuration confirmed by SERVER_VOICE_CHAT_UPDATED. / SERVER_VOICE_CHAT_UPDATED 确认的语音协商结果。
 *
 * String pointers are borrowed from the received JSON and are valid only during
 * the event callback.
 */
typedef struct {
    qm_bool input_binary;                /*!< Negotiated uplink representation. / 已协商的上行格式。 */
    qm_bool output_binary;               /*!< Negotiated downlink representation. / 已协商的下行格式。 */
    qm_bool vad;                         /*!< eventData.audio.vad; omitted means false. / 缺省为 false。 */
    qm_bool output_stream;               /*!< eventData.audio.output.stream; omitted means false. / 缺省为 false。 */
    const char *input_codec;              /*!< eventData.audio.input.codec, NULL if absent */
    uint32_t input_sample_rate;           /*!< eventData.audio.input.sampleRate, 0 if absent */
    uint32_t input_frame_size;            /*!< eventData.audio.input.frameSize, 0 if absent */
    const char *output_codec;             /*!< eventData.audio.output.codec, NULL if absent */
    uint32_t output_sample_rate;          /*!< eventData.audio.output.sampleRate, 0 if absent */
    uint32_t output_frame_size_ms;        /*!< eventData.audio.output.frameSizeMs, 0 if absent */
    qm_bool output_opus_cbr;              /*!< eventData.audio.output.enableOpusCbr */
} joyinside_client_voice_updated_t;

/**
 * @brief Common metadata parsed from server contentType EVENT messages. / 服务端 EVENT 消息的公共元数据。
 *
 * String pointers are borrowed from the received JSON and are valid only during
 * the event callback.
 */
typedef struct {
    const char *round_id;                 /*!< content.roundId, NULL if absent */
    const char *input;                    /*!< content.eventData.input, NULL if absent */
    const char *text;                     /*!< content.eventData.text, used by TTS_SENTENCE_START */
    const struct cJSON *event_data;       /*!< Borrowed content.eventData cJSON object for custom fields, NULL if absent */
    uint64_t start_time_ms;               /*!< content.eventData.startTime, used by CALL_AGENT_INTERRUPTED when present */
    uint64_t time_ms;                     /*!< content.eventData.time, 0 if absent */
} joyinside_server_event_t;

/**
 * @brief Structured CALL_SKILL_EVENT payload. / 结构化 CALL_SKILL_EVENT 载荷。
 *
 * Pointers and cJSON nodes are borrowed from the received JSON and are valid
 * only during the event callback. instructions points to the raw
 * eventData.data.instructions array; use joyinside_client_skill_instruction()
 * to read an entry without iterating cJSON manually.
 *
 * @par 中文说明
 * 所有字符串和 cJSON 节点均为借用数据，仅在事件回调内有效。使用
 * joyinside_client_skill_instruction() 读取 instructions，避免应用直接遍历 cJSON。
 */
typedef struct {
    const char *message;                  /*!< eventData.msg, NULL if absent */
    const char *domain;                   /*!< eventData.data.domain, NULL if absent */
    const char *intent_code;              /*!< eventData.data.intentCode, NULL if absent */
    const char *intent_classify;          /*!< eventData.data.intentClassify, NULL if absent */
    joyinside_client_expression_t expression;     /*!< Decoded eventData.data.args.EMOTION */
    const char *expression_code;                  /*!< Raw eventData.data.args.EMOTION, NULL if absent */
    joyinside_client_expression_t tts_expression; /*!< Decoded optional eventData.data.args.TTS_EMOTION */
    const char *tts_expression_code;              /*!< Raw optional eventData.data.args.TTS_EMOTION */
    const struct cJSON *args;             /*!< eventData.data.args object, NULL if absent */
    const struct cJSON *instructions;     /*!< eventData.data.instructions array, NULL if absent */
    uint32_t instruction_count;           /*!< Number of instructions, 0 if absent */
} joyinside_client_skill_t;

/**
 * @brief Raw WebSocket payload chunk metadata. / 原始 WebSocket 载荷分片元数据。
 */
typedef struct {
    const char *data;                     /*!< Payload data */
    uint32_t data_len;                    /*!< Current payload chunk length */
    uint32_t total_data_len;              /*!< Total WebSocket payload length */
    uint32_t current_data_offset;         /*!< Current chunk offset in total payload */
} joyinside_client_payload_chunk_t;

/**
 * @brief Common metadata parsed from JoyInside Client text messages. / JoyInside 文本消息的公共元数据。
 */
typedef struct {
    int code;                             /*!< JoyInside Client response code if present */
    const char *msg;                      /*!< Response message if present */
    const char *content_type;             /*!< Text message contentType if present */
    const char *mid;                      /*!< Message ID if present */
    const char *uid;                      /*!< User ID if present */
    const char *request_id;               /*!< Request ID if present */
    uint64_t timestamp_ms;                /*!< Server timestamp t if present */
} joyinside_client_message_meta_t;

/**
 * @brief Audio event payload. / 音频事件载荷。
 */
typedef struct {
    joyinside_client_payload_chunk_t chunk;       /*!< Binary audio payload chunk */
} joyinside_client_audio_event_t;

/**
 * @brief Text event payload for non-EVENT contentType messages. / 非 EVENT 文本消息的事件载荷。
 */
typedef struct {
    joyinside_client_payload_chunk_t chunk;       /*!< Raw text payload chunk */
    joyinside_client_message_meta_t meta;         /*!< Parsed text metadata */
    joyinside_client_text_type_t text_type;       /*!< Known text content type */
    union {
        joyinside_client_asr_result_t asr;        /*!< Parsed ASR result */
        joyinside_client_agent_result_t agent;    /*!< Parsed AGENT or ACTIVITY text */
        joyinside_client_tts_result_t tts;        /*!< Parsed TTS audio text payload */
    } content;                            /*!< Parsed content selected by text_type */
} joyinside_client_text_event_t;

/**
 * @brief Server EVENT payload. / 服务端 EVENT 事件载荷。
 */
typedef struct joyinside_server_event_payload {
    joyinside_client_payload_chunk_t chunk;       /*!< Raw text payload chunk */
    joyinside_client_message_meta_t meta;         /*!< Parsed text metadata */
    joyinside_server_event_type_t type;    /*!< Known server EVENT type, or CUSTOM/UNKNOWN */
    const char *type_name;                 /*!< Raw server eventType string */
    joyinside_server_event_t common;       /*!< Common EVENT metadata */
    union {
        joyinside_client_bot_config_t bot_config; /*!< Parsed CFG_BOT_EVENT reference config */
        joyinside_client_voice_updated_t voice_updated; /*!< Internal SERVER_VOICE_CHAT_UPDATED config, not externally dispatched */
        joyinside_client_skill_t skill;    /*!< Parsed CALL_SKILL_EVENT structured payload */
    } content;                            /*!< Parsed content selected by type */
} joyinside_server_event_payload_t;

/**
 * @brief Error event payload. / 错误事件载荷。
 */
typedef struct {
    joyinside_client_error_type_t type;           /*!< Error category */
    int code;                              /*!< Component or transport error code */
} joyinside_client_error_event_t;

/**
 * @brief JoyInside Client event payload. / JoyInside 客户端回调事件。
 *
 * Pointers inside this structure are owned by the component and are valid only
 * during the event callback. Copy data in the callback if it must be used later.
 */
typedef struct {
    joyinside_client_event_id_t event_id;        /*!< Event id */
    joyinside_client_handle_t handle;            /*!< JoyInside Client handle */
    void *user_context;                   /*!< User context from config */
    union {
        joyinside_client_audio_event_t audio;     /*!< Valid for JOYINSIDE_CLIENT_EVENT_AUDIO */
        joyinside_client_text_event_t text;       /*!< Valid for JOYINSIDE_CLIENT_EVENT_TEXT */
        joyinside_server_event_payload_t server_event; /*!< Valid for JOYINSIDE_CLIENT_EVENT_SERVER_EVENT */
        joyinside_client_error_event_t error;     /*!< Valid for JOYINSIDE_CLIENT_EVENT_ERROR */
    } payload;                            /*!< Event-specific payload */
} joyinside_client_event_t;

/**
 * @brief Receive one JoyInside Client event. / 接收一条 JoyInside 客户端事件。
 *
 * @param[in] event Borrowed event, valid only for this callback.
 *
 * The callback runs on the WebSocket or core worker. It must not block, retain
 * pointers from @p event, or call joyinside_client_stop() or
 * joyinside_client_destroy().
 *
 * @par 中文说明
 * 回调运行在 WebSocket 或核心工作线程。不得阻塞、保存 event 内部指针，或在回调中调用
 * joyinside_client_stop()、joyinside_client_destroy()。
 */
typedef void (*joyinside_client_event_cb_t)(const joyinside_client_event_t *event);

/**
 * @brief JoyInside Client configuration. / JoyInside 客户端长生命周期配置。
 *
 * Long-lived runtime overrides. Zero-valued numeric fields use
 * CONFIG_JOYINSIDE_CLIENT_* defaults from qm_config/Kconfig.
 * joyinside_client_init() copies voice.chat_role_code. The callback and user
 * context are borrowed and must remain valid until the application quiesces
 * callbacks or destroys the client.
 *
 * @par 中文说明
 * 数值字段为 0 时使用 Kconfig 默认值。chat_role_code 会被组件复制；回调和 user_context
 * 为借用数据，必须在应用停止回调或销毁客户端前保持有效。
 */
typedef struct {
    joyinside_client_network_config_t network;         /*!< WebSocket network options */
    joyinside_client_buffer_config_t buffer;           /*!< WebSocket buffer sizes */
    joyinside_client_task_config_t task;               /*!< WebSocket task and queue options */
    joyinside_client_heartbeat_config_t heartbeat;     /*!< Internal application-level PING options */
    joyinside_client_voice_config_t voice;             /*!< Default voice chat options */
    void *user_context;                            /*!< User context returned in callbacks */
    joyinside_client_event_cb_t on_event;              /*!< Event callback, optional */
    qm_bool include_uid_in_messages;               /*!< Also put uid in message bodies, default QM_FALSE */
} joyinside_client_config_t;

/**
 * @brief Supply a valid token before each connection attempt, including retries. / 每次连接或重试前提供有效 Token。
 *
 * @param[in] context Application-owned token provider context.
 * @param[in] force_refresh QM_TRUE after an Authorization rejection.
 * @param[out] token Output buffer for a NUL-terminated token.
 * @param[in,out] token_len Input capacity and output length excluding NUL.
 * @return QM_EOK on success, negative QUMI error code on failure.
 *
 * Runs on the client core worker without client locks. Use bounded I/O and do
 * not stop or destroy the client here. The context must outlive start/stop.
 *
 * @par 中文说明
 * 回调在未持有客户端锁的核心工作线程中执行。实现必须使用有界 I/O，且不得在回调中停止或销毁客户端。
 * token_context 必须至少存活到本次 start/stop 生命周期结束。
 */
typedef int (*joyinside_client_token_provider_t)(void *context, qm_bool force_refresh,
                                                 char *token, uint32_t *token_len);

/**
 * @brief WebSocket start parameters. / WebSocket 会话启动参数。
 *
 * Per-start runtime parameters copied by joyinside_client_start(). requestId
 * and per-message mid are generated internally; sessionId is generated as
 * botId_uuid unless a reusable session_id is supplied.
 * The access token can be obtained by joyinside_client_auth or by an
 * application-specific backend. All string fields are copied; token_context
 * remains borrowed until the client is stopped.
 *
 * @par 中文说明
 * requestId 和每条消息的 mid 由组件生成；sessionId 可由调用方复用。除 token_context 外，所有字符串
 * 都会在 start 时复制。未提供 token_provider 时必须提供 access_token。
 */
typedef struct {
    const char *bot_id;                   /*!< Bot ID, required */
    const char *access_token;             /*!< Copied static token; required only without token_provider */
    joyinside_client_token_provider_t token_provider; /*!< Optional token source, called before every connection */
    void *token_context;                 /*!< Borrowed token provider context */
    const char *uid;                      /*!< User ID, required and copied into the WebSocket URI */
    const char *session_id;               /*!< Optional reusable session ID, NULL generates botId_uuid */
    const char *feature;                  /*!< Optional feature query parameter, e.g. AUDIO_BOOK_V2 */
    const char *client_type;              /*!< Optional clientType query parameter, e.g. AUDIO_BOOK */
    const char *location;                 /*!< Optional location query parameter */
    qm_bool need_asr_partial;             /*!< Optional needAsrPartial=true for visual dialogue */
} joyinside_client_start_params_t;

/**
 * @brief Create JoyInside Client handle. / 创建 JoyInside 客户端句柄。
 *
 * @param[in] config JoyInside Client configuration
 * @return JoyInside Client handle on success, NULL on failure
 */
joyinside_client_handle_t joyinside_client_init(const joyinside_client_config_t *config);

/**
 * @brief Start the asynchronous client connection lifecycle. / 异步启动客户端连接生命周期。
 *
 * QM_EOK means accepted. Only EVENT_CONNECTED confirms negotiated readiness.
 * Callbacks can run on the WS or core worker; serialize shared application state.
 *
 * @par 中文说明
 * QM_EOK 仅表示启动请求已接收；只有 JOYINSIDE_CLIENT_EVENT_CONNECTED 表示语音协商完成且可收发音频。
 * 回调可来自 WebSocket 或核心工作线程，应用必须同步保护共享状态。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] params WebSocket start parameters
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_start(joyinside_client_handle_t handle, const joyinside_client_start_params_t *params);

/**
 * @brief Stop and join client workers; extension registrations are preserved. / 停止并等待客户端工作线程退出，保留扩展注册。
 *
 * Serialize lifecycle calls and quiesce external senders first. Never call from
 * a component callback. A timeout retains a valid handle and must be retried.
 * Attached media receives cancellation; its backend must still acknowledge stop.
 *
 * @par 中文说明
 * 生命周期调用必须串行，且应先停止外部发送。不得在组件回调内调用。超时后句柄仍有效，完成在途操作后可重试。
 * 已挂载媒体会收到取消请求，但仍需等待播放器后端确认停止。
 *
 * @param[in] handle JoyInside Client handle
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_stop(joyinside_client_handle_t handle);

/**
 * @brief Pause or resume JoyInside network I/O without stopping the client task. / 暂停或恢复网络 I/O，不停止客户端任务。
 *
 * @param[in] handle JoyInside Client handle.
 * @param[in] state Requested network state.
 * @return QM_EOK on success, negative error code on failure.
 */
int joyinside_client_network_update(joyinside_client_handle_t handle,
                                    joyinside_client_network_state_t state);

/**
 * @brief Destroy JoyInside Client handle after workers stop. / 在工作线程停止后销毁客户端句柄。
 *
 * Returns -QM_EBUSY while a media controller is attached. With media: stop the
 * client, destroy media, then destroy the client. Errors retain a valid handle.
 *
 * @par 中文说明
 * 挂载媒体控制器时返回 -QM_EBUSY。正确顺序为先 stop 客户端、再销毁媒体控制器、最后销毁客户端。
 * 失败不会使句柄失效。
 *
 * @param[in] handle JoyInside Client handle
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_destroy(joyinside_client_handle_t handle);

/**
 * @brief Copy one audio chunk into bounded provider-owned storage. / 复制一帧上行音频到组件预分配的有界存储。
 *
 * The negotiated input_transport selects binary frames or Base64 AUDIO messages.
 * 已协商的 input_transport 决定使用二进制帧或 Base64 AUDIO 消息。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] data Audio data
 * @param[in] len Audio data length
 * The existing core task owns encoding and protocol sequencing. This call
 * never waits for a lock, queue space, or network I/O. Input remains borrowed
 * only for this call; failure retains no bytes. Serialize destruction with
 * all callers. Accepted data may be cancelled on disconnect or stop.
 *
 * @par 中文说明
 * 核心工作线程异步完成编码与发送；函数返回后调用方可以立即复用 data。容量不足返回 -QM_EFULL，
 * 锁竞争返回 -QM_EBUSY；断线或停止时，已接收但未发送的旧连接音频可能被丢弃。
 * @return QM_EOK if copied; -QM_EFULL for capacity, -QM_EBUSY for admission,
 * or another negative QUMI error. Acceptance is not a server acknowledgement.
 */
int joyinside_client_send_audio(joyinside_client_handle_t handle,
                                   const void *data,
                                   uint32_t len);

/**
 * @brief Send text message through WebSocket. / 通过 WebSocket 发送原始文本协议消息。
 *
 * This public API is valid only after JOYINSIDE_CLIENT_EVENT_CONNECTED. The component
 * uses an internal path for the automatic voice update sent before that event.
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] text Null-terminated text message
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_send_text(joyinside_client_handle_t handle,
                                  const char *text,
                                  uint32_t timeout_ms);

/**
 * @brief Send CLIENT_INTERRUPT to cancel current agent output. / 发送 CLIENT_INTERRUPT 以打断当前智能体输出。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_send_interrupt(joyinside_client_handle_t handle,
                                       uint32_t timeout_ms);

#if CONFIG_JOYINSIDE_CLIENT_MANUAL_CALL
/**
 * @brief Send CLIENT_AUDIO_FINISH to mark the end of manual audio upload. / 发送 CLIENT_AUDIO_FINISH 标记手动音频上传结束。
 *
 * Queued after accepted audio; only the existing core task sends the marker.
 * @param[in] handle JoyInside Client handle
 * @param[in] timeout_ms Admission lock budget in milliseconds; zero only tries.
 * Queue capacity is checked without waiting regardless of this budget.
 * @return QM_EOK after acceptance, -QM_EBUSY/-QM_EFULL when not accepted,
 * or another negative error code. Acceptance does not imply server delivery.
 */
int joyinside_client_send_audio_finish(joyinside_client_handle_t handle,
                                          uint32_t timeout_ms);
#endif

/**
 * @brief Send TEXT input to chat with the agent. / 发送用户文本输入以触发智能体对话。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] input User text input
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_send_text_input(joyinside_client_handle_t handle,
                                        const char *input,
                                        uint32_t timeout_ms);

/**
 * @brief Send CLIENT_INPUT_TEXT_TO_SPEECH event. / 发送 CLIENT_INPUT_TEXT_TO_SPEECH 请求。
 *
 * The text is synthesized to speech and does not trigger an agent chat turn.
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] text Text to synthesize
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_request_tts(joyinside_client_handle_t handle,
                                    const char *text,
                                    uint32_t timeout_ms);

/**
 * @brief Trigger VOICE_CHAT_TRIGGER activity. / 上报 VOICE_CHAT_TRIGGER 活动事件。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] kv_data_json JSON object string used as kvData, NULL sends {}
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_trigger_voice_chat(joyinside_client_handle_t handle,
                                           const char *kv_data_json,
                                           uint32_t timeout_ms);

/**
 * @brief Send CLIENT_UPDATE_CHAT_CONTEXT activity. / 发送 CLIENT_UPDATE_CHAT_CONTEXT 上下文更新事件。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] effective_time_minutes Context lifetime in minutes, valid range
 *            JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_MIN to JOYINSIDE_CLIENT_CONTEXT_EFFECTIVE_MINUTES_MAX
 * @param[in] kv_data_json JSON object string used as kvData, NULL sends {}
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_update_chat_context(joyinside_client_handle_t handle,
                                            uint32_t effective_time_minutes,
                                            const char *kv_data_json,
                                            uint32_t timeout_ms);

/**
 * @brief Submit a complete voice configuration. / 提交完整语音配置。
 *
 * Copies the configuration, including role text. QM_EOK means accepted; wait
 * for EVENT_VOICE_UPDATED or EVENT_CONNECTED after recovery before sending again. Updates interrupt the current
 * turn, discard queued audio, and survive reconnect. Only one update may be
 * pending. Call while connected; a failed validation changes no state.
 * 复制配置和角色字符串。成功仅表示受理；收到 VOICE_UPDATED 或恢复连接的 CONNECTED 后再继续发送。
 * 更新会打断当前轮次、丢弃排队音频，并在重连时重用。仅允许一个待确认请求。
 * 数值 0 和 NULL 可选字段按协议省略；更改可选参数时应明确赋值。
 *
 * @param[in] handle Client handle. / 客户端句柄。
 * @param[in] voice Complete requested configuration. / 完整目标配置。
 * @return QM_EOK, -QM_EBUSY, or a negative error. / 成功、忙或其他负错误码。
 */
int joyinside_client_update_voice(joyinside_client_handle_t handle,
                                 const joyinside_client_voice_config_t *voice);

/**
 * @brief Begin a new end-side VAD round. / 开始一个端侧 VAD 语音轮次。
 *
 * Only relevant when voice.vad is QM_TRUE. Each AUDIO chunk of a round carries
 * the same content.mid; call this when VAD detects a new speech segment so the
 * server can correlate the downlink round id. Queued chunks retain their round.
 * Downlink completion does not change the active upload round.
 *
 * @par 中文说明
 * 仅在 voice.vad 为 QM_TRUE 时使用。端侧检测到新的语音段时调用，以便同一轮 AUDIO 复用 mid。
 * 已入队音频固定其轮次；下行完成事件不会改变正在上传的轮次。断线会丢弃旧轮次。
 *
 * @param[in] handle JoyInside Client handle
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_vad_round_begin(joyinside_client_handle_t handle);

/**
 * @brief Send CLIENT_UPDATE_COORDINATES_CONTEXT activity. / 发送 CLIENT_UPDATE_COORDINATES_CONTEXT 坐标上下文事件。
 *
 * Requires JoyInside operation support. lat and lng are strings per the
 * handbook and are placed in content.coordinates.
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] lat Latitude string
 * @param[in] lng Longitude string
 * @param[in] timeout_ms Send queue wait timeout in milliseconds
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_update_coordinates_context(joyinside_client_handle_t handle,
                                                 const char *lat,
                                                 const char *lng,
                                                 uint32_t timeout_ms);

/**
 * @brief Check whether JoyInside Client is ready after voice update confirmation. / 查询语音协商确认后的就绪状态。
 *
 * @param[in] handle JoyInside Client handle
 * @return QM_TRUE if ready, QM_FALSE otherwise
 */
qm_bool joyinside_client_is_connected(joyinside_client_handle_t handle);

/**
 * @brief Set user context returned by event callbacks. / 设置事件回调中返回的用户上下文。
 *
 * Serialize this update with application callback processing when the context
 * object itself is being replaced or released.
 *
 * @par 中文说明
 * 当替换或释放上下文对象本身时，应用必须与回调处理串行化，防止回调访问悬空指针。
 *
 * @param[in] handle JoyInside Client handle
 * @param[in] user_context User context
 * @return QM_EOK on success, negative error code on failure
 */
int joyinside_client_set_user_context(joyinside_client_handle_t handle, void *user_context);

/**
 * @brief Get user context stored in JoyInside Client handle. / 获取客户端句柄中保存的用户上下文。
 *
 * @param[in] handle JoyInside Client handle
 * @return User context, or NULL
 */
void *joyinside_client_get_user_context(joyinside_client_handle_t handle);

/**
 * @brief Get the current session uid (may be NULL). / 获取当前会话 UID，可能为 NULL。
 *
 * @param[in] handle JoyInside Client handle
 * @return Session uid string or NULL. The returned pointer remains valid until
 *         the next start, stop, or destroy operation on @p handle.
 */
const char *joyinside_client_get_uid(joyinside_client_handle_t handle);

/**
 * @brief Borrowed instruction view for device actions and alarm V2. / 设备动作及闹钟 V2 的借用指令视图。
 * All pointers expire when the event callback returns. / 所有指针仅在事件回调内有效。
 */
typedef struct {
    const char *type;              /*!< Alarm type; NULL for ordinary actions. / 闹钟类型，普通动作为空。 */
    const char *operation;         /*!< Alarm operation or ordinary action. / 闹钟 operation 或普通 action。 */
    const char *description;       /*!< Optional desc. / 可选描述。 */
    const struct cJSON *params;    /*!< Alarm params or ordinary args object. / 闹钟 params 或普通 args 对象。 */
} joyinside_client_instruction_t;

/**
 * @brief Read and validate one instruction. / 读取并校验一条指令。
 * @param[in] skill Borrowed skill payload. / 借用技能载荷。
 * @param[in] index Zero-based index. / 从 0 开始的下标。
 * @param[out] instruction Borrowed view, valid during this callback. / 仅本次回调有效的借用视图。
 * @return QM_EOK, -QM_ENOENT, or -QM_EINVAL. / 成功、不存在或格式错误。
 */
int joyinside_client_skill_instruction(const joyinside_client_skill_t *skill,
                                       uint32_t index,
                                       joyinside_client_instruction_t *instruction);

#ifdef __cplusplus
}
#endif

#endif
