/**
 * @file qm_agent_transport_agora.c
 * @brief Agora transport adapter.
 *
 * The current implementation is a minimal RTC bridge: initialize SDK, join a
 * channel, send upstream PCM, and emit normalized connection events into qm_agent.
 *
 * 当前实现为精简 RTC 桥接：初始化 SDK、加入频道、发送上行 PCM，并向 qm_agent 发出
 * 标准化连接事件。流式业务消息由 qm_agent_agora_stream 解析后再标准化。
 */

#include "qm_agent_internal.h"

/* 1: let the SDK create its built-in encoder for the negotiated codec.
 * 0: disable the built-in encoder. Defaults to 1 when undefined.
 */
#ifndef CONFIG_QM_AGENT_AGORA_AUDIO_CODEC_SUP
#define CONFIG_QM_AGENT_AGORA_AUDIO_CODEC_SUP 1
#endif

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
#include "qm_agent_agora_stream.h"
#include "agora_rtc_api.h"

#define LOG_TAG "qm_agent_agora"
#define QM_AGENT_AGORA_SDK_LOG_PATH "io.agora.rtc_sdk"
#define QM_AGENT_AGORA_PCM_DURATION_MS 40U

#ifndef CONFIG_QM_AGENT_AGORA_SDK_LOG_DISABLE
#define CONFIG_QM_AGENT_AGORA_SDK_LOG_DISABLE 0
#endif

typedef struct qm_agent_agora_ctx qm_agent_agora_ctx_t;

struct qm_agent_agora_ctx {
    qm_agent_transport_event_cb_t event_cb;
    void *user_ctx;
    qm_agent_transport_config_t config;
    qm_agent_audio_format_t input_format;
    qm_agent_audio_format_t output_format;
    qm_agent_video_format_t input_video_format;
    qm_agent_video_format_t output_video_format;
#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
    connection_id_t conn_id;
    agora_rtc_event_handler_t event_handler;
    qm_agent_agora_stream_t *stream;
    qm_bool sdk_inited;
    qm_bool connected;
    qm_bool joined;
#endif
};

static int agora_log_printf(const char *fmt, ...)
{
    QM_UNUSED_ARG(fmt);
    return 0;
}

/*
 * Some Agora IoT SDK drops declare video_frame_info_t but omit the matching
 * send-video prototype from the public header. Keep C99 builds explicit while
 * still linking against SDKs that export the symbol.
 */
extern __agora_api__ int agora_rtc_send_video_data(connection_id_t conn_id,
                                                   const void *data_ptr,
                                                   size_t data_len,
                                                   video_frame_info_t *info_ptr);

static audio_data_type_e agora_audio_data_type(qm_agent_audio_codec_t codec)
{
    switch (codec) {
        case QM_AGENT_AUDIO_CODEC_OPUS:
            return AUDIO_DATA_TYPE_OPUS;
        case QM_AGENT_AUDIO_CODEC_G711A:
            return AUDIO_DATA_TYPE_PCMA;
        case QM_AGENT_AUDIO_CODEC_G711U:
            return AUDIO_DATA_TYPE_PCMU;
        case QM_AGENT_AUDIO_CODEC_G722:
            return AUDIO_DATA_TYPE_G722;
        case QM_AGENT_AUDIO_CODEC_PCM:
        case QM_AGENT_AUDIO_CODEC_NONE:
        default:
            return AUDIO_DATA_TYPE_PCM;
    }
}

static qm_agent_audio_codec_t qm_agent_audio_data_type(const qm_agent_audio_format_t *format)
{
    if (format == NULL) {
        return QM_AGENT_AUDIO_CODEC_NONE;
    }

    return format->data_type != QM_AGENT_AUDIO_CODEC_NONE ?
           format->data_type :
           format->codec;
}

static audio_codec_type_e agora_audio_codec_type(qm_agent_audio_codec_t codec)
{
    switch (codec) {
        case QM_AGENT_AUDIO_CODEC_PCM:
        case QM_AGENT_AUDIO_CODEC_OPUS:
            return AUDIO_CODEC_TYPE_OPUS;
        case QM_AGENT_AUDIO_CODEC_G711A:
            return AUDIO_CODEC_TYPE_G711A;
        case QM_AGENT_AUDIO_CODEC_G711U:
            return AUDIO_CODEC_TYPE_G711U;
        case QM_AGENT_AUDIO_CODEC_G722:
            return AUDIO_CODEC_TYPE_G722;
        case QM_AGENT_AUDIO_CODEC_NONE:
        case QM_AGENT_AUDIO_CODEC_MP3:
        case QM_AGENT_AUDIO_CODEC_CUSTOM:
        default:
            return AUDIO_CODEC_DISABLED;
    }
}

/**
 * @brief Validate the PCM capture geometry accepted by the Agora SDK bridge.
 * 校验 Agora SDK 桥接层可接受的 PCM 采集参数。
 *
 * qm_agent supplies raw PCM to this adapter and the SDK encodes to the selected
 * wire codec. Unsupported channel/rate/framing combinations fail before start.
 * qm_agent 向此适配层提供原始 PCM，再由 SDK 编码为协商载荷；不支持的声道、采样率和分帧
 * 组合会在 start 前失败。
 */
static int validate_uplink(const qm_agent_audio_format_t *format)
{
#if !CONFIG_QM_AGENT_AGORA_AUDIO_CODEC_SUP
    /* The SDK does not encode. Input must already be Opus packets in the device format. */
    if (!format || qm_agent_audio_data_type(format) != QM_AGENT_AUDIO_CODEC_OPUS ||
        format->codec != QM_AGENT_AUDIO_CODEC_OPUS) {
        return -QM_ENOSYS;
    }
    if (format->bits_per_sample != 16U || format->channel_num != 1U ||
        format->sample_rate != 16000U || format->frame_ms == 0U) {
        return -QM_EINVAL;
    }
    if (format->framing == QM_AGENT_AUDIO_FRAMING_OPUS_PACKET &&
        format->packet_bytes == 0U) {
        return QM_EOK;
    }
    if (format->framing == QM_AGENT_AUDIO_FRAMING_OPUS_CBR &&
        format->packet_bytes != 0U) {
        return QM_EOK;
    }
    return -QM_EINVAL;
#else
    if (!format || qm_agent_audio_data_type(format) != QM_AGENT_AUDIO_CODEC_PCM ||
        (format->codec != QM_AGENT_AUDIO_CODEC_OPUS &&
         format->codec != QM_AGENT_AUDIO_CODEC_G711A &&
         format->codec != QM_AGENT_AUDIO_CODEC_G711U &&
         format->codec != QM_AGENT_AUDIO_CODEC_G722)) {
        return -QM_ENOSYS;
    }
    if (format->bits_per_sample != 16U ||
        (format->channel_num != 1U && format->channel_num != 2U) ||
        format->sample_rate == 0U || format->frame_ms == 0U ||
        format->framing != QM_AGENT_AUDIO_FRAMING_STREAM || format->packet_bytes != 0U) {
        return -QM_EINVAL;
    }
    if ((format->codec == QM_AGENT_AUDIO_CODEC_G711A ||
         format->codec == QM_AGENT_AUDIO_CODEC_G711U) &&
        (format->sample_rate != 8000U || format->channel_num != 1U)) {
        return -QM_EINVAL;
    }
    if (format->codec == QM_AGENT_AUDIO_CODEC_G722 &&
        (format->sample_rate != 16000U || format->channel_num != 1U)) {
        return -QM_EINVAL;
    }
    return QM_EOK;
#endif
}

static const char *qm_agent_audio_codec_name(qm_agent_audio_codec_t codec)
{
    switch (codec) {
        case QM_AGENT_AUDIO_CODEC_NONE:
            return "NONE";
        case QM_AGENT_AUDIO_CODEC_PCM:
            return "PCM";
        case QM_AGENT_AUDIO_CODEC_OPUS:
            return "OPUS";
        case QM_AGENT_AUDIO_CODEC_G711A:
            return "G711A";
        case QM_AGENT_AUDIO_CODEC_G711U:
            return "G711U";
        case QM_AGENT_AUDIO_CODEC_G722:
            return "G722";
        case QM_AGENT_AUDIO_CODEC_MP3:
            return "MP3";
        case QM_AGENT_AUDIO_CODEC_CUSTOM:
            return "CUSTOM";
        default:
            return "UNKNOWN";
    }
}

static const char *agora_audio_codec_type_name(audio_codec_type_e codec_type)
{
    switch (codec_type) {
        case AUDIO_CODEC_DISABLED:
            return "DISABLED";
        case AUDIO_CODEC_TYPE_OPUS:
            return "OPUS";
        case AUDIO_CODEC_TYPE_G722:
            return "G722";
        case AUDIO_CODEC_TYPE_G711A:
            return "G711A";
        case AUDIO_CODEC_TYPE_G711U:
            return "G711U";
        default:
            return "UNKNOWN";
    }
}

static const char *agora_audio_data_type_name(int data_type)
{
    switch (data_type) {
        case AUDIO_DATA_TYPE_PCM:
            return "PCM";
        case AUDIO_DATA_TYPE_OPUS:
            return "OPUS";
        case AUDIO_DATA_TYPE_OPUSFB:
            return "OPUSFB";
        case AUDIO_DATA_TYPE_PCMA:
            return "PCMA";
        case AUDIO_DATA_TYPE_PCMU:
            return "PCMU";
        case AUDIO_DATA_TYPE_G722:
            return "G722";
        case AUDIO_DATA_TYPE_AACLC_8K:
            return "AACLC_8K";
        case AUDIO_DATA_TYPE_AACLC_16K:
            return "AACLC_16K";
        case AUDIO_DATA_TYPE_AACLC:
            return "AACLC";
        case AUDIO_DATA_TYPE_HEAAC:
            return "HEAAC";
        case AUDIO_DATA_TYPE_HEAAC2:
            return "HEAAC2";
        case AUDIO_DATA_TYPE_AACLC_8K_2CH:
            return "AACLC_8K_2CH";
        case AUDIO_DATA_TYPE_AACLC_16K_2CH:
            return "AACLC_16K_2CH";
        case AUDIO_DATA_TYPE_AACLC_2CH:
            return "AACLC_2CH";
        case AUDIO_DATA_TYPE_HEAAC_2CH:
            return "HEAAC_2CH";
        case AUDIO_DATA_TYPE_HEAAC2_2CH:
            return "HEAAC2_2CH";
        case AUDIO_DATA_TYPE_GENERIC:
            return "GENERIC";
        case 16:
            return "AACLC1";
        case 17:
            return "AACLC1_2CH";
        case 18:
            return "HWAAC";
        case 20:
            return "OPUSSWB";
        case 21:
            return "OPUSFB_2CH";
        default:
            return "UNKNOWN";
    }
}

static qm_agent_audio_format_t agora_audio_format_from_data_type(int data_type,
                                                                 const qm_agent_audio_format_t *base)
{
    qm_agent_audio_format_t format;

    qm_memset(&format, 0, sizeof(format));
    if (base != NULL) {
        qm_memcpy(&format, base, sizeof(format));
    }
    format.framing = QM_AGENT_AUDIO_FRAMING_STREAM;
    format.packet_bytes = 0U;

    switch (data_type) {
        case AUDIO_DATA_TYPE_PCM:
            format.codec = QM_AGENT_AUDIO_CODEC_PCM;
            format.data_type = QM_AGENT_AUDIO_CODEC_PCM;
            if (format.bits_per_sample == 0) {
                format.bits_per_sample = 16;
            }
            break;
        case AUDIO_DATA_TYPE_OPUS:
            format.codec = QM_AGENT_AUDIO_CODEC_OPUS;
            format.data_type = QM_AGENT_AUDIO_CODEC_OPUS;
            format.sample_rate = 16000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_OPUSFB:
            format.codec = QM_AGENT_AUDIO_CODEC_OPUS;
            format.data_type = QM_AGENT_AUDIO_CODEC_OPUS;
            format.sample_rate = 48000;
            format.channel_num = 1;
            break;
        case 20: /* AUDIO_DATA_TYPE_OPUSSWB: sample=32k, not present in all SDK drops. */
            format.codec = QM_AGENT_AUDIO_CODEC_OPUS;
            format.data_type = QM_AGENT_AUDIO_CODEC_OPUS;
            format.sample_rate = 32000;
            format.channel_num = 1;
            break;
        case 21: /* AUDIO_DATA_TYPE_OPUSFB_2CH: sample=48k, channels=2. */
            format.codec = QM_AGENT_AUDIO_CODEC_OPUS;
            format.data_type = QM_AGENT_AUDIO_CODEC_OPUS;
            format.sample_rate = 48000;
            format.channel_num = 2;
            break;
        case AUDIO_DATA_TYPE_PCMA:
            format.codec = QM_AGENT_AUDIO_CODEC_G711A;
            format.data_type = QM_AGENT_AUDIO_CODEC_G711A;
            format.sample_rate = 8000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_PCMU:
            format.codec = QM_AGENT_AUDIO_CODEC_G711U;
            format.data_type = QM_AGENT_AUDIO_CODEC_G711U;
            format.sample_rate = 8000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_G722:
            format.codec = QM_AGENT_AUDIO_CODEC_G722;
            format.data_type = QM_AGENT_AUDIO_CODEC_G722;
            format.sample_rate = 16000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_AACLC_8K:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 8000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_AACLC_16K:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 16000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_AACLC:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 48000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_HEAAC:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 32000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_HEAAC2:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 48000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_AACLC_8K_2CH:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 8000;
            format.channel_num = 2;
            break;
        case AUDIO_DATA_TYPE_AACLC_16K_2CH:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 16000;
            format.channel_num = 2;
            break;
        case AUDIO_DATA_TYPE_AACLC_2CH:
        case AUDIO_DATA_TYPE_HEAAC_2CH:
        case AUDIO_DATA_TYPE_HEAAC2_2CH:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 48000;
            format.channel_num = 2;
            break;
        case 16: /* AUDIO_DATA_TYPE_AACLC1: sample=44100. */
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 44100;
            format.channel_num = 1;
            break;
        case 17: /* AUDIO_DATA_TYPE_AACLC1_2CH: sample=48000, channels=2. */
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 48000;
            format.channel_num = 2;
            break;
        case 18: /* AUDIO_DATA_TYPE_HWAAC: sample=32k. */
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.sample_rate = 32000;
            format.channel_num = 1;
            break;
        case AUDIO_DATA_TYPE_GENERIC:
        default:
            format.codec = QM_AGENT_AUDIO_CODEC_CUSTOM;
            format.data_type = QM_AGENT_AUDIO_CODEC_CUSTOM;
            break;
    }

    if (format.codec == QM_AGENT_AUDIO_CODEC_OPUS) {
        format.framing = QM_AGENT_AUDIO_FRAMING_OPUS_PACKET;
    }
    return format;
}

static qm_agent_transport_event_cb_t s_agora_event_cb;
static void *s_agora_user_ctx;
static qm_agent_agora_stream_t *s_agora_stream;
static qm_agent_agora_ctx_t *s_agora_ctx;
static qm_agent_audio_format_t s_agora_output_format;
static qm_bool s_agora_connected_emitted;
static qm_bool s_agora_uplink_log_emitted;
static qm_bool s_agora_downlink_log_emitted;

static qm_bool agora_is_agent_uid(uint32_t uid)
{
    return (uid == 100U || uid == 200U) ? QM_TRUE : QM_FALSE;
}

static void agora_emit_event(const qm_agent_event_t *event)
{
    if (s_agora_event_cb && event) {
        s_agora_event_cb(event, s_agora_user_ctx);
    }
}

/**
 * @brief Forward a parsed Agora business-stream event to the session callback.
 * 将解析出的 Agora 业务流事件转发给会话回调。
 *
 * The stream parser has already normalized event semantics. event remains
 * borrowed from the parser and is consumed synchronously.
 * 流解析器已完成事件语义标准化。event 仍由解析器借用，并在此处同步消费。
 */
static void agora_stream_emit_event(const qm_agent_event_t *event, void *user_ctx)
{
    QM_UNUSED_ARG(user_ctx);
    agora_emit_event(event);
}

/**
 * @brief Emit one QM_AGENT_EVENT_CONNECTED for the active Agora session.
 * 为当前 Agora 会话仅发布一次 QM_AGENT_EVENT_CONNECTED。
 */
static void agora_emit_connected_once(void)
{
    qm_agent_event_t event = {0};

    if (s_agora_connected_emitted) {
        return;
    }

    s_agora_connected_emitted = QM_TRUE;
    event.id = QM_AGENT_EVENT_CONNECTED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    agora_emit_event(&event);
}

static void agora_emit_disconnected(void)
{
    qm_agent_event_t event = {0};

    s_agora_connected_emitted = QM_FALSE;
    event.id = QM_AGENT_EVENT_DISCONNECTED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    agora_emit_event(&event);
}

static void agora_on_join_channel_success(connection_id_t conn_id, uint32_t uid, int elapsed_ms)
{
    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "agora event join_success conn_id=%u uid=%u elapsed=%d",
            (unsigned int)conn_id,
            (unsigned int)uid,
            elapsed_ms);
    if (s_agora_ctx != NULL && s_agora_ctx->conn_id == conn_id) {
        s_agora_ctx->connected = QM_TRUE;
        s_agora_ctx->joined = QM_TRUE;
    }
    agora_emit_connected_once();
}

static void agora_on_reconnecting(connection_id_t conn_id)
{
    QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "agora event reconnecting conn_id=%u", (unsigned int)conn_id);
}

static void agora_on_connection_lost(connection_id_t conn_id)
{
    QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "agora event connection_lost conn_id=%u", (unsigned int)conn_id);
    if (s_agora_ctx != NULL && s_agora_ctx->conn_id == conn_id) {
        s_agora_ctx->connected = QM_FALSE;
        s_agora_ctx->joined = QM_FALSE;
    }
    agora_emit_disconnected();
}

static void agora_on_rejoin_channel_success(connection_id_t conn_id, uint32_t uid, int elapsed_ms)
{
    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "agora event rejoin_success conn_id=%u uid=%u elapsed=%d",
            (unsigned int)conn_id,
            (unsigned int)uid,
            elapsed_ms);
    if (s_agora_ctx != NULL && s_agora_ctx->conn_id == conn_id) {
        s_agora_ctx->connected = QM_TRUE;
        s_agora_ctx->joined = QM_TRUE;
    }
    agora_emit_connected_once();
}

static void agora_on_user_joined(connection_id_t conn_id, uint32_t uid, int elapsed_ms)
{
    qm_agent_event_t event = {0};

    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
            "agora event user_joined conn_id=%u uid=%u elapsed=%d",
            (unsigned int)conn_id,
            (unsigned int)uid,
            elapsed_ms);

    if (!agora_is_agent_uid(uid)) {
        return;
    }

    event.id = QM_AGENT_EVENT_REMOTE_USER_JOINED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.remote_user.uid = uid;
    agora_emit_event(&event);
}

static void agora_on_user_offline(connection_id_t conn_id, uint32_t uid, int reason)
{
    qm_agent_event_t event = {0};

    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
            "agora event user_offline conn_id=%u uid=%u reason=%d",
            (unsigned int)conn_id,
            (unsigned int)uid,
            reason);

    if (!agora_is_agent_uid(uid)) {
        return;
    }

    event.id = QM_AGENT_EVENT_REMOTE_USER_LEFT;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.code = reason;
    event.data.remote_user.uid = uid;
    event.data.remote_user.reason = reason;
    agora_emit_event(&event);
}

static void agora_on_user_mute_audio(connection_id_t conn_id, uint32_t uid, bool muted)
{
    qm_agent_event_t event = {0};

    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
            "agora event user_mute_audio conn_id=%u uid=%u muted=%d",
            (unsigned int)conn_id,
            (unsigned int)uid,
            muted ? 1 : 0);

    if (!agora_is_agent_uid(uid)) {
        return;
    }

    event.id = muted ? QM_AGENT_EVENT_REMOTE_AUDIO_MUTED :
                       QM_AGENT_EVENT_REMOTE_AUDIO_UNMUTED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.remote_user.uid = uid;
    event.data.remote_user.muted = muted ? QM_TRUE : QM_FALSE;
    agora_emit_event(&event);
}

static void agora_on_error(connection_id_t conn_id, int code, const char *msg)
{
    qm_agent_event_t event = {0};

    QM_AGENT_LOGE(LOG_TAG, "agora event error conn_id=%u code=%d msg=%s",
            (unsigned int)conn_id,
            code,
            msg ? msg : "");

    event.id = QM_AGENT_EVENT_ERROR;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.code = code;
    event.data.error.message = msg;
    agora_emit_event(&event);
}

/**
 * @brief Convert an Agora SDK audio callback to a borrowed qm_agent AUDIO event.
 * 将 Agora SDK 音频回调转换为借用的 qm_agent AUDIO 事件。
 *
 * Downlink byte ownership remains with the SDK, so the audio engine must copy
 * the bytes into its bounded PUSH operation before this callback returns.
 * 下行字节仍由 SDK 所有，音频引擎必须在本回调返回前复制到有界 PUSH 操作中。
 */
static void agora_on_audio_data(connection_id_t conn_id,
                                uint32_t uid,
                                uint16_t sent_ts,
                                const void *data_ptr,
                                size_t data_len,
                                const audio_frame_info_t *info_ptr)
{
    qm_agent_event_t event = {0};
    qm_agent_audio_format_t frame_format;
    int data_type = info_ptr ? (int)info_ptr->data_type : -1;

    if (data_ptr == NULL || data_len == 0 || data_len > UINT32_MAX) {
        return;
    }
    if (!agora_is_agent_uid(uid)) {
        return;
    }

    frame_format = info_ptr ?
                   agora_audio_format_from_data_type((int)info_ptr->data_type, &s_agora_output_format) :
                   s_agora_output_format;
#if !CONFIG_QM_AGENT_AGORA_AUDIO_CODEC_SUP
    /* Bytes are already Opus. Use the device format; decoding happens before the UART write. */
    if (info_ptr != NULL && info_ptr->data_type == AUDIO_DATA_TYPE_OPUS) {
        frame_format = s_agora_output_format;
    }
#endif

    if (!s_agora_downlink_log_emitted) {
        QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_DOWNLINK,
                "agora downlink first frame len=%u type=%d(%s) codec=%d(%s) rate=%u ch=%u",
                (unsigned int)data_len,
                data_type,
                agora_audio_data_type_name(data_type),
                (int)frame_format.codec,
                qm_agent_audio_codec_name(frame_format.codec),
                (unsigned int)frame_format.sample_rate,
                (unsigned int)frame_format.channel_num);
        s_agora_downlink_log_emitted = QM_TRUE;
    }

    event.id = QM_AGENT_EVENT_AUDIO;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.audio.data = (const uint8_t *)data_ptr;
    event.data.audio.len = (uint32_t)data_len;
    event.data.audio.format = frame_format;
    agora_emit_event(&event);
}

/**
 * @brief Copy one Agora business-stream message into the asynchronous parser.
 * 将一条 Agora 业务流消息复制到异步解析器。
 *
 * The SDK callback cannot parse or retain its input synchronously for an
 * unbounded period, so parser queue pressure is surfaced as a transport error.
 * SDK 回调不能无限期解析或保存输入，因此解析队列压力会转换为传输错误上报。
 */
static void agora_on_stream_message(connection_id_t conn_id,
                                    uint32_t uid,
                                    int stream_id,
                                    const char *data,
                                    size_t length,
                                    uint64_t sent_ts)
{
    QM_UNUSED_ARG(conn_id);
    QM_UNUSED_ARG(stream_id);
    QM_UNUSED_ARG(sent_ts);

    if (s_agora_stream == NULL || data == NULL || length == 0U || length > UINT32_MAX) {
        return;
    }
    if (!agora_is_agent_uid(uid)) {
        return;
    }

    (void)qm_agent_agora_stream_input(s_agora_stream, data, (uint32_t)length);
}

static void agora_init_event_handler(agora_rtc_event_handler_t *event_handler)
{
    if (event_handler == NULL) {
        return;
    }

    event_handler->on_join_channel_success = agora_on_join_channel_success;
    event_handler->on_reconnecting = agora_on_reconnecting;
    event_handler->on_connection_lost = agora_on_connection_lost;
    event_handler->on_rejoin_channel_success = agora_on_rejoin_channel_success;
    event_handler->on_user_joined = agora_on_user_joined;
    event_handler->on_user_offline = agora_on_user_offline;
    event_handler->on_user_mute_audio = agora_on_user_mute_audio;
    event_handler->on_error = agora_on_error;
    event_handler->on_audio_data = agora_on_audio_data;
    event_handler->on_stream_message = agora_on_stream_message;
}
#else
typedef struct qm_agent_agora_ctx {
    qm_agent_transport_event_cb_t event_cb;
    void *user_ctx;
    qm_agent_transport_config_t config;
    qm_agent_audio_format_t input_format;
    qm_agent_audio_format_t output_format;
    qm_agent_video_format_t input_video_format;
    qm_agent_video_format_t output_video_format;
} qm_agent_agora_ctx_t;
#endif

/**
 * @brief Allocate the Agora adapter and establish its single-SDK callback binding.
 * 分配 Agora 适配器并建立单实例 SDK 回调绑定。
 *
 * Some Agora SDK callbacks have no user context. The adapter therefore admits
 * only one live instance to avoid redirecting provider callbacks across sessions.
 * 部分 Agora SDK 回调不携带用户上下文，因此适配层只允许一个活动实例，避免供应商回调跨会话
 * 错投。
 */
static int agora_init_transport(void **transport_ctx,
                                const qm_agent_transport_config_t *config,
                                const qm_agent_audio_format_t *input_format,
                                const qm_agent_audio_format_t *output_format,
                                const qm_agent_video_format_t *input_video_format,
                                const qm_agent_video_format_t *output_video_format,
                                qm_agent_transport_event_cb_t event_cb,
                                void *user_ctx)
{
    qm_agent_agora_ctx_t *ctx;

    if (transport_ctx == NULL || config == NULL || event_cb == NULL) {
        return -QM_EINVAL;
    }

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
    int format_ret = validate_uplink(input_format);
    if (format_ret != QM_EOK) {
        return format_ret;
    }
    /* The SDK callbacks do not carry user context; do not overwrite a live
     * instance's global callback binding. */
    if (s_agora_ctx != NULL) {
        return -QM_EBUSY;
    }
#endif

    ctx = (qm_agent_agora_ctx_t *)qm_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }

    qm_memset(ctx, 0, sizeof(*ctx));
    qm_memcpy(&ctx->config, config, sizeof(*config));
    if (input_format) {
        qm_memcpy(&ctx->input_format, input_format, sizeof(*input_format));
    }
    if (output_format) {
        qm_memcpy(&ctx->output_format, output_format, sizeof(*output_format));
    }
    if (input_video_format) {
        qm_memcpy(&ctx->input_video_format, input_video_format, sizeof(*input_video_format));
    }
    if (output_video_format) {
        qm_memcpy(&ctx->output_video_format, output_video_format, sizeof(*output_video_format));
    }
    ctx->event_cb = event_cb;
    ctx->user_ctx = user_ctx;
#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
    int ret;

    ret = qm_agent_agora_stream_init(&ctx->stream, agora_stream_emit_event, user_ctx);
    if (ret != QM_EOK) {
        qm_free(ctx);
        return ret;
    }

    s_agora_event_cb = event_cb;
    s_agora_user_ctx = user_ctx;
    s_agora_stream = ctx->stream;
    s_agora_ctx = ctx;
    s_agora_output_format = ctx->output_format;
    s_agora_connected_emitted = QM_FALSE;
    s_agora_uplink_log_emitted = QM_FALSE;
    s_agora_downlink_log_emitted = QM_FALSE;
#endif
    *transport_ctx = ctx;
    return QM_EOK;
}

/**
 * @brief Tear down Agora SDK resources and the asynchronous stream parser.
 * 释放 Agora SDK 资源和异步流解析器。
 */
static int agora_deinit_transport(void *transport_ctx)
{
    qm_agent_agora_ctx_t *ctx = (qm_agent_agora_ctx_t *)transport_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
    if (ctx->sdk_inited) {
        int ret = agora_rtc_fini();
        if (ret < 0) {
            return -QM_EIO;
        }
        ctx->sdk_inited = QM_FALSE;
    }
    qm_agent_agora_stream_deinit(ctx->stream);
    ctx->stream = NULL;
    s_agora_event_cb = NULL;
    s_agora_user_ctx = NULL;
    s_agora_stream = NULL;
    s_agora_ctx = NULL;
    qm_memset(&s_agora_output_format, 0, sizeof(s_agora_output_format));
    s_agora_connected_emitted = QM_FALSE;
    s_agora_uplink_log_emitted = QM_FALSE;
    s_agora_downlink_log_emitted = QM_FALSE;
#endif
    qm_free(ctx);
    return QM_EOK;
}

/**
 * @brief Initialize the SDK and join the configured Agora channel.
 * 初始化 SDK 并加入配置的 Agora 频道。
 */
static int agora_start(void *transport_ctx)
{
    qm_agent_agora_ctx_t *ctx = (qm_agent_agora_ctx_t *)transport_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
    rtc_service_option_t *service_opt;
    rtc_channel_options_t *channel_options;
    int ret;

    service_opt = (rtc_service_option_t *)qm_malloc(sizeof(*service_opt));
    channel_options = (rtc_channel_options_t *)qm_malloc(sizeof(*channel_options));
    if (service_opt == NULL || channel_options == NULL) {
        qm_free(service_opt);
        qm_free(channel_options);
        return -QM_ENOMEM;
    }

    qm_memset(service_opt, 0, sizeof(*service_opt));
    qm_memset(channel_options, 0, sizeof(*channel_options));
    qm_memset(&ctx->event_handler, 0, sizeof(ctx->event_handler));
    agora_init_event_handler(&ctx->event_handler);

    service_opt->area_code = AREA_CODE_GLOB;
    service_opt->log_cfg.log_disable =
        CONFIG_QM_AGENT_AGORA_SDK_LOG_DISABLE ? true : false;
    service_opt->log_cfg.log_level = RTC_LOG_ERROR;
    service_opt->log_cfg.log_path = QM_AGENT_AGORA_SDK_LOG_PATH;
    service_opt->log_cfg.log_printf = agora_log_printf;
    service_opt->domain_limit = false;
    if (ctx->config.data.agora.license) {
        qm_strncpy(service_opt->license_value,
                   ctx->config.data.agora.license,
                   sizeof(service_opt->license_value) - 1);
    }

    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
            "agora sdk version=%s service_opt=%p handler=%p service_size=%u handler_size=%u",
            agora_rtc_get_version(),
            service_opt,
            &ctx->event_handler,
            (unsigned int)sizeof(*service_opt),
            (unsigned int)sizeof(ctx->event_handler));

    ret = agora_rtc_init(ctx->config.data.agora.app_id, &ctx->event_handler, service_opt);
    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "agora_rtc_init ret=%d", ret);
    if (ret < 0) {
        goto cleanup;
    }
    ctx->sdk_inited = QM_TRUE;

    ret = agora_rtc_create_connection(&ctx->conn_id);
    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "agora_rtc_create_connection ret=%d conn_id=%u", ret, (unsigned int)ctx->conn_id);
    if (ret < 0) {
        goto cleanup;
    }
    ctx->connected = QM_TRUE;

    channel_options->auto_subscribe_audio = ctx->config.data.agora.audio_enable;
    channel_options->auto_subscribe_video = ctx->config.data.agora.video_enable;
    channel_options->audio_codec_opt.audio_codec_type = agora_audio_codec_type(ctx->input_format.codec);
#if !CONFIG_QM_AGENT_AGORA_AUDIO_CODEC_SUP
    /* This 1.9.5 SDK has no acm_encoder. OPUS asserts in audio_stream_init. */
    channel_options->audio_codec_opt.audio_codec_type = AUDIO_CODEC_DISABLED;
#endif
    channel_options->audio_codec_opt.pcm_sample_rate = ctx->input_format.sample_rate;
    channel_options->audio_codec_opt.pcm_channel_num = ctx->input_format.channel_num;
    channel_options->audio_codec_opt.pcm_duration =
        ctx->input_format.frame_ms ? ctx->input_format.frame_ms : QM_AGENT_AGORA_PCM_DURATION_MS;

    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE,
            "agora join audio codec=%d(%s) sdk_codec=%d(%s) data_codec=%d(%s) data_type=%d(%s) rate=%u ch=%u frame_ms=%u",
            (int)ctx->input_format.codec,
            qm_agent_audio_codec_name(ctx->input_format.codec),
            (int)channel_options->audio_codec_opt.audio_codec_type,
            agora_audio_codec_type_name(channel_options->audio_codec_opt.audio_codec_type),
            (int)qm_agent_audio_data_type(&ctx->input_format),
            qm_agent_audio_codec_name(qm_agent_audio_data_type(&ctx->input_format)),
            (int)agora_audio_data_type(qm_agent_audio_data_type(&ctx->input_format)),
            agora_audio_data_type_name((int)agora_audio_data_type(qm_agent_audio_data_type(&ctx->input_format))),
            (unsigned int)ctx->input_format.sample_rate,
            (unsigned int)ctx->input_format.channel_num,
            (unsigned int)ctx->input_format.frame_ms);

    ret = agora_rtc_join_channel(ctx->conn_id,
                                 ctx->config.data.agora.channel_name,
                                 ctx->config.data.agora.uid,
                                 ctx->config.data.agora.token,
                                 channel_options);
    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "agora_rtc_join_channel ret=%d", ret);
    if (ret < 0) {
        goto cleanup;
    }

    ctx->joined = QM_TRUE;
    ret = QM_EOK;

cleanup:
    qm_free(service_opt);
    qm_free(channel_options);
    return ret;
#else
    return -QM_ENOSYS;
#endif
}

/**
 * @brief Leave the Agora channel and reset parsed stream state.
 * 离开 Agora 频道并重置已解析的流状态。
 */
static int agora_stop(void *transport_ctx)
{
    qm_agent_agora_ctx_t *ctx = (qm_agent_agora_ctx_t *)transport_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
    if (ctx->joined) {
        if (agora_rtc_leave_channel(ctx->conn_id) < 0) {
            return -QM_EIO;
        }
        ctx->joined = QM_FALSE;
    }
    qm_agent_agora_stream_reset(ctx->stream);
    if (ctx->connected) {
        if (agora_rtc_destroy_connection(ctx->conn_id) < 0) {
            return -QM_EIO;
        }
        ctx->connected = QM_FALSE;
    }
    return QM_EOK;
#else
    return QM_EOK;
#endif
}

/**
 * @brief Send one borrowed PCM frame through the Agora SDK.
 * 通过 Agora SDK 发送一个借用的 PCM 帧。
 */
static int agora_send_audio(void *transport_ctx, const void *data, uint32_t len)
{
    qm_agent_agora_ctx_t *ctx = (qm_agent_agora_ctx_t *)transport_ctx;

    if (ctx == NULL || data == NULL || len == 0) {
        return -QM_EINVAL;
    }

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
    audio_frame_info_t info;

    if (!ctx->joined) {
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_UPLINK, "drop audio before joined conn_id=%u len=%u",
                (unsigned int)ctx->conn_id,
                (unsigned int)len);
        return -QM_EINVAL;
    }

    qm_memset(&info, 0, sizeof(info));
    info.data_type = agora_audio_data_type(qm_agent_audio_data_type(&ctx->input_format));
    if (!s_agora_uplink_log_emitted) {
        QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_UPLINK,
                "agora uplink first frame len=%u type=%d(%s) codec=%d(%s) data_codec=%d(%s)",
                (unsigned int)len,
                (int)info.data_type,
                agora_audio_data_type_name((int)info.data_type),
                (int)ctx->input_format.codec,
                qm_agent_audio_codec_name(ctx->input_format.codec),
                (int)qm_agent_audio_data_type(&ctx->input_format),
                qm_agent_audio_codec_name(qm_agent_audio_data_type(&ctx->input_format)));
        s_agora_uplink_log_emitted = QM_TRUE;
    }
    return agora_rtc_send_audio_data(ctx->conn_id, data, len, &info);
#else
    return -QM_ENOSYS;
#endif
}

/**
 * @brief Send a borrowed raw video frame through the Agora SDK.
 * 通过 Agora SDK 发送借用的原始视频帧。
 */
static int agora_send_video(void *transport_ctx,
                            const void *data,
                            uint32_t len,
                            const qm_agent_video_format_t *format)
{
    qm_agent_agora_ctx_t *ctx = (qm_agent_agora_ctx_t *)transport_ctx;

    if (ctx == NULL || data == NULL || len == 0) {
        return -QM_EINVAL;
    }

#if defined(CONFIG_QM_AGENT_TRANSPORT_AGORA) && CONFIG_QM_AGENT_TRANSPORT_AGORA
    video_frame_info_t info;
    qm_agent_video_format_t frame_format;

    frame_format = format ? *format : ctx->input_video_format;
    if (frame_format.codec == QM_AGENT_VIDEO_CODEC_NONE &&
        ctx->config.data.agora.video_format.codec != QM_AGENT_VIDEO_CODEC_NONE) {
        frame_format = ctx->config.data.agora.video_format;
    }

    qm_memset(&info, 0, sizeof(info));
    switch (frame_format.codec) {
        case QM_AGENT_VIDEO_CODEC_H264:
            info.data_type = VIDEO_DATA_TYPE_H264;
            break;
        case QM_AGENT_VIDEO_CODEC_H265:
            info.data_type = VIDEO_DATA_TYPE_H265;
            break;
        case QM_AGENT_VIDEO_CODEC_YUV420:
            info.data_type = VIDEO_DATA_TYPE_YUV420;
            break;
        case QM_AGENT_VIDEO_CODEC_JPEG:
            info.data_type = VIDEO_DATA_TYPE_GENERIC_JPEG;
            break;
        case QM_AGENT_VIDEO_CODEC_CUSTOM:
        case QM_AGENT_VIDEO_CODEC_RGB:
        case QM_AGENT_VIDEO_CODEC_NONE:
        default:
            info.data_type = VIDEO_DATA_TYPE_GENERIC;
            break;
    }
    info.stream_type = VIDEO_STREAM_LOW;
    info.frame_type = VIDEO_FRAME_AUTO_DETECT;
    info.frame_rate = frame_format.frame_rate;
    switch (frame_format.rotation) {
        case 90:
            info.rotation = VIDEO_ORIENTATION_90;
            break;
        case 180:
            info.rotation = VIDEO_ORIENTATION_180;
            break;
        case 270:
            info.rotation = VIDEO_ORIENTATION_270;
            break;
        case 0:
        default:
            info.rotation = VIDEO_ORIENTATION_0;
            break;
    }
    return agora_rtc_send_video_data(ctx->conn_id, data, len, &info);
#else
    return -QM_ENOSYS;
#endif
}

static int agora_no_text(void *transport_ctx, const char *text)
{
    QM_UNUSED_ARG(transport_ctx);
    QM_UNUSED_ARG(text);
    return -QM_ENOSYS;
}

static int agora_interrupt(void *transport_ctx)
{
    QM_UNUSED_ARG(transport_ctx);
    return -QM_ENOSYS;
}

const qm_agent_transport_if_t *qm_agent_transport_agora(void)
{
    static const qm_agent_transport_if_t transport = {
        .name = "agora",
        .init = agora_init_transport,
        .deinit = agora_deinit_transport,
        .start = agora_start,
        .stop = agora_stop,
        .send_audio = agora_send_audio,
        .send_video = agora_send_video,
        .send_text = agora_no_text,
        .request_tts = agora_no_text,
        .interrupt = agora_interrupt,
        .asr_start_longform = NULL,
        .asr_stop_longform = NULL,
        .asr_set_realtime_enabled = NULL,
    };

    return &transport;
}
