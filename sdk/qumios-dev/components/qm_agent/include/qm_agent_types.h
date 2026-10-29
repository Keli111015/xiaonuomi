#ifndef QM_AGENT_TYPES_H
#define QM_AGENT_TYPES_H

/**
 * @file qm_agent_types.h
 * @brief Common qm_agent public types.
 *
 * qm_agent 公共类型，涵盖会话状态、传输选择、音视频格式与回调约定。除非接口明确
 * 说明会复制，配置中的字符串和上下文指针均由调用方在会话期间持有。
 */

#include "qm_errno.h"
#include "qm_types.h"
#include "qm_agent_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque qm_agent session handle.
 * 不透明的 qm_agent 会话句柄，仅可传给本组件公开接口，不可解引用。
 */
typedef struct qm_agent *qm_agent_handle_t;

/** @brief Availability of the underlying IP network, independent of session state. 底层 IP 网络可用性，与会话状态无关。 */
typedef enum {
    QM_AGENT_NETWORK_DOWN = 0,
    QM_AGENT_NETWORK_UP,
} qm_agent_network_state_t;

/**
 * @brief Lifecycle state of a qm_agent session.
 * qm_agent 会话生命周期状态。状态切换通过 QM_AGENT_EVENT_STATE_CHANGED 发布。
 */
typedef enum {
    /** @brief Invalid or unknown state. */
    QM_AGENT_STATE_NONE = 0,

    /** @brief Handle allocated but not fully initialized. */
    QM_AGENT_STATE_IDLE,

    /** @brief Session is initialized and can be started. */
    QM_AGENT_STATE_READY,

    /** @brief Session start sequence is in progress. */
    QM_AGENT_STATE_STARTING,

    /** @brief Session is connected and running. */
    QM_AGENT_STATE_RUNNING,

    /** @brief Session stop sequence is in progress. */
    QM_AGENT_STATE_STOPPING,

    /** @brief Session entered an unrecoverable runtime error state. */
    QM_AGENT_STATE_ERROR,
} qm_agent_state_t;

/**
 * @brief Transport backend type.
 * 传输后端类型。CUSTOM 表示应用通过 qm_agent_config_t::transport_if 提供实现。
 */
typedef enum {
    /** @brief No transport selected. */
    QM_AGENT_TRANSPORT_NONE = 0,

    /** @brief Agora RTC transport adapter. */
    QM_AGENT_TRANSPORT_AGORA,

    /** @brief baidu_chat_agent WebSocket transport adapter. */
    QM_AGENT_TRANSPORT_BAIDU,

    /** @brief JoyInside client transport adapter. */
    QM_AGENT_TRANSPORT_JOYINSIDE,

    /** @brief lgeine voice WebSocket transport adapter. */
    QM_AGENT_TRANSPORT_LGEINE,

    /** @brief Application-supplied transport implementation. */
    QM_AGENT_TRANSPORT_CUSTOM,
} qm_agent_transport_type_t;

/**
 * @brief Audio codec used by qm_agent input or output streams.
 * qm_agent 上下行音频流使用的编码格式。codec 表示协商格式，data_type 表示实际载荷格式。
 */
typedef enum {
    /** @brief No codec specified. */
    QM_AGENT_AUDIO_CODEC_NONE = 0,

    /** @brief Linear PCM audio. */
    QM_AGENT_AUDIO_CODEC_PCM,

    /** @brief Opus audio. */
    QM_AGENT_AUDIO_CODEC_OPUS,

    /** @brief G.711 A-law audio. */
    QM_AGENT_AUDIO_CODEC_G711A,

    /** @brief G.711 mu-law audio. */
    QM_AGENT_AUDIO_CODEC_G711U,

    /** @brief G.722 audio. */
    QM_AGENT_AUDIO_CODEC_G722,

    /** @brief MP3 audio. */
    QM_AGENT_AUDIO_CODEC_MP3,

    /** @brief Application-defined codec. */
    QM_AGENT_AUDIO_CODEC_CUSTOM,
} qm_agent_audio_codec_t;

/** @brief Encoded payload framing, independent of the cloud protocol. 编码载荷分帧方式，与云端协议无关。 */
typedef enum {
    /** @brief PCM or a self-framed byte stream such as MP3. */
    QM_AGENT_AUDIO_FRAMING_STREAM = 0,
    /** @brief Ogg pages containing Opus packets. */
    QM_AGENT_AUDIO_FRAMING_OPUS_OGG,
    /** @brief Opus packets with a 4-byte BE length and 4-byte range check. */
    QM_AGENT_AUDIO_FRAMING_OPUS_RAW_HEADER,
    /** @brief Raw Opus packets of exactly packet_bytes bytes. */
    QM_AGENT_AUDIO_FRAMING_OPUS_CBR,
    /** @brief One complete raw Opus packet per AUDIO event; no stream header. */
    QM_AGENT_AUDIO_FRAMING_OPUS_PACKET,
} qm_agent_audio_framing_t;

/**
 * @brief Video codec or payload type used by qm_agent video streams.
 * qm_agent 视频流使用的编码或载荷类型。
 */
typedef enum {
    /** @brief No video codec specified. */
    QM_AGENT_VIDEO_CODEC_NONE = 0,

    /** @brief JPEG still image or frame payload. */
    QM_AGENT_VIDEO_CODEC_JPEG,

    /** @brief H.264 encoded video. */
    QM_AGENT_VIDEO_CODEC_H264,

    /** @brief H.265 encoded video. */
    QM_AGENT_VIDEO_CODEC_H265,

    /** @brief Raw RGB video frame. */
    QM_AGENT_VIDEO_CODEC_RGB,

    /** @brief Raw YUV420 video frame. */
    QM_AGENT_VIDEO_CODEC_YUV420,

    /** @brief Application-defined video payload. */
    QM_AGENT_VIDEO_CODEC_CUSTOM,
} qm_agent_video_codec_t;

/**
 * @brief Normalized remote agent state.
 * 标准化后的远端 Agent 对话状态，用于应用层显示或驱动采集策略。
 */
typedef enum {
    /** @brief Unknown or unavailable agent state. */
    QM_AGENT_DIALOG_STATE_NONE = 0,

    /** @brief Agent is idle and not speaking. */
    QM_AGENT_DIALOG_STATE_SILENT,

    /** @brief Agent is accepting user speech. */
    QM_AGENT_DIALOG_STATE_LISTENING,

    /** @brief Agent is processing a user request. */
    QM_AGENT_DIALOG_STATE_THINKING,

    /** @brief Agent is producing speech output. */
    QM_AGENT_DIALOG_STATE_SPEAKING,
} qm_agent_dialog_state_t;

/**
 * @brief Microphone upload behavior while playback is active.
 * 播放期间的麦克风上传策略。该策略表达意图，最终准入仍由音频引擎和会话状态共同决定。
 */
typedef enum {
    /** @brief Use the audio engine/driver default behavior. */
    QM_AGENT_CAPTURE_POLICY_AUTO = 0,

    /** @brief Keep microphone capture/upload enabled during playback. */
    QM_AGENT_CAPTURE_POLICY_KEEP,

    /** @brief Pause microphone capture/upload during playback. */
    QM_AGENT_CAPTURE_POLICY_PAUSE,
} qm_agent_capture_policy_t;

/**
 * @brief Audio stream format descriptor.
 * 音频流格式描述。缺省字段由 qm_agent 根据音频引擎能力补全；OPUS 必须提供明确的 framing。
 */
typedef struct {
    /** @brief Negotiated wire codec; an SDK may encode PCM input to this codec. */
    qm_agent_audio_codec_t codec;

    /** @brief Actual callback/send_audio payload codec. NONE falls back to codec. */
    qm_agent_audio_codec_t data_type;

    /** @brief Sample rate in Hz. */
    uint32_t sample_rate;

    /** @brief Number of audio channels. */
    uint16_t channel_num;

    /** @brief Bits per audio sample. */
    uint16_t bits_per_sample;

    /** @brief Preferred frame duration in milliseconds. */
    uint16_t frame_ms;

    /** @brief Actual payload framing. Opus requires an explicit framing value. */
    qm_agent_audio_framing_t framing;

    /** @brief Fixed Opus packet length for OPUS_CBR; zero for other framing. */
    uint32_t packet_bytes;
} qm_agent_audio_format_t;

/**
 * @brief Video stream format descriptor.
 * 视频流格式描述。帧数据的实际所有权由视频驱动和传输适配器接口约定。
 */
typedef struct {
    /** @brief Video codec or payload type. */
    qm_agent_video_codec_t codec;

    /** @brief Frame width in pixels. */
    uint16_t width;

    /** @brief Frame height in pixels. */
    uint16_t height;

    /** @brief Preferred frame rate. Zero means backend-specific default. */
    uint16_t frame_rate;

    /** @brief Clockwise frame rotation in degrees: 0, 90, 180, or 270. */
    uint16_t rotation;
} qm_agent_video_format_t;

/**
 * @brief Direction-of-arrival information reported by capable audio drivers.
 * 支持该能力的音频驱动上报的声源方向信息。角度范围和置信度含义以具体驱动为准。
 */
typedef struct {
    /** @brief Azimuth in degrees. Valid range is driver-specific, commonly 0..359. */
    int16_t azimuth;

    /** @brief Elevation in degrees when available. Set has_elevation to false otherwise. */
    int16_t elevation;

    /** @brief Whether elevation carries valid data. */
    qm_bool has_elevation;

    /** @brief Confidence percentage, 0..100 when known. */
    uint8_t confidence;

    /** @brief Whether confidence carries valid data. */
    qm_bool has_confidence;
} qm_agent_doa_info_t;

#if defined(CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP) && CONFIG_QM_AGENT_AUTH_CHANNEL_HTTP
/** @brief Built-in HTTP authentication environment. 内置 HTTP 认证环境。 */
typedef enum {
    /** @brief Development endpoint. */
    QM_AGENT_AUTH_ENV_DEV = 0,
    /** @brief Test endpoint. */
    QM_AGENT_AUTH_ENV_TEST,
    /** @brief Production endpoint. */
    QM_AGENT_AUTH_ENV_PROD,
} qm_agent_auth_env_t;

/** @brief HTTP authentication configuration; strings are borrowed by Agent. HTTP 认证配置；字符串由 Agent 借用至会话结束。 */
typedef struct {
    /** @brief Environment used when base_url is not set. */
    qm_agent_auth_env_t env;
    /** @brief Optional base URL override. */
    const char *base_url;
    /** @brief Product identifier used by the signing payload. */
    uint32_t product_id;
    /** @brief Product secret used for signing. */
    const char *product_secret;
    /** @brief Device serial number. */
    const char *sn;
    /** @brief HTTP request timeout in milliseconds; zero uses the default. */
    uint32_t timeout_ms;
    /** @brief Retry limit; zero uses the default. */
    uint32_t retry_num;
} qm_agent_auth_http_config_t;
#endif

#ifdef __cplusplus
}
#endif

#endif
