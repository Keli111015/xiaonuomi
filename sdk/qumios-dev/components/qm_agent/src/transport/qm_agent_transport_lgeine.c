/**
 * @file qm_agent_transport_lgeine.c
 * @brief qm_agent adapter for the lgeine voice WebSocket protocol.
 *
 * 适配层只负责语义转换和会话时序；WebSocket 分帧、认证请求头、sessionId 校验及协议解析
 * 仍由 lgeine_agent 负责。供应商回调载荷只在回调期间借用。
 */

#include "qm_agent_internal.h"
#if defined(CONFIG_LGEINE_AGENT_SUPPORT) && CONFIG_LGEINE_AGENT_SUPPORT
#include "lgeine_agent.h"
#endif

#if defined(CONFIG_QM_AGENT_TRANSPORT_LGEINE) && CONFIG_QM_AGENT_TRANSPORT_LGEINE && \
    defined(CONFIG_LGEINE_AGENT_SUPPORT) && CONFIG_LGEINE_AGENT_SUPPORT
#define QM_AGENT_HAS_LGEINE_TRANSPORT 1
#else
#define QM_AGENT_HAS_LGEINE_TRANSPORT 0
#endif

typedef struct {
    qm_agent_transport_event_cb_t event_cb;
    void *user_ctx;
    qm_agent_transport_config_t config;
    qm_agent_audio_format_t input_format;
    qm_agent_audio_format_t output_format;
    qm_agent_video_format_t input_video_format;
    qm_agent_video_format_t output_video_format;
#if QM_AGENT_HAS_LGEINE_TRANSPORT
    lgeine_agent_handle_t agent;
#else
    void *agent;
#endif
    qm_bool qm_connected;
    qm_bool listen_active;
    qm_bool realtime_enabled;
} qm_agent_lgeine_ctx_t;

#if QM_AGENT_HAS_LGEINE_TRANSPORT

static void lgeine_emit(qm_agent_lgeine_ctx_t *ctx, const qm_agent_event_t *event)
{
    if (ctx && ctx->event_cb && event) {
        ctx->event_cb(event, ctx->user_ctx);
    }
}

static void lgeine_emit_error(qm_agent_lgeine_ctx_t *ctx, int code, const char *message)
{
    qm_agent_event_t event;

    if (!ctx) {
        return;
    }
    qm_memset(&event, 0, sizeof(event));
    event.id = QM_AGENT_EVENT_ERROR;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.code = code;
    event.data.error.message = message;
    lgeine_emit(ctx, &event);
}

static qm_agent_audio_codec_t resolve_codec(const qm_agent_audio_format_t *format)
{
    if (!format) {
        return QM_AGENT_AUDIO_CODEC_NONE;
    }
    return format->data_type != QM_AGENT_AUDIO_CODEC_NONE ?
           format->data_type : format->codec;
}

/**
 * @brief Convert a validated qm_agent PCM descriptor to lgeine_agent format.
 * 将已校验的 qm_agent PCM 描述转换为 lgeine_agent 格式。
 *
 * lgeine_agent does not encode capture data, so this boundary accepts only
 * PCM stream payloads and rejects unsupported geometry before start().
 * lgeine_agent 不编码采集数据，因此此边界只接受 PCM 流载荷，并在 start() 前拒绝不支持的
 * 音频参数。
 */
static int to_lgeine_audio(const qm_agent_audio_format_t *format,
                           lgeine_agent_audio_info_t *audio)
{
    qm_agent_audio_codec_t codec;

    if (!format || !audio || format->sample_rate == 0U ||
        format->channel_num < 1U || format->channel_num > 2U ||
        format->bits_per_sample != 16U || format->frame_ms == 0U ||
        format->framing != QM_AGENT_AUDIO_FRAMING_STREAM || format->packet_bytes != 0U) {
        return -QM_EINVAL;
    }
    codec = resolve_codec(format);
    /* lgeine_agent has no encoder; the shared capture path sends PCM. */
    if (codec != QM_AGENT_AUDIO_CODEC_PCM || format->codec != QM_AGENT_AUDIO_CODEC_PCM) {
        return -QM_ENOSYS;
    }
    qm_memset(audio, 0, sizeof(*audio));
    audio->format = LGEINE_AGENT_AUDIO_FORMAT_PCM;
    audio->rate = format->sample_rate;
    audio->bits = format->bits_per_sample;
    audio->channel = format->channel_num;
    audio->frame_duration_ms = format->frame_ms;
    return QM_EOK;
}

/**
 * @brief Convert an lgeine_agent downlink descriptor to qm_agent format.
 * 将 lgeine_agent 下行格式描述转换为 qm_agent 格式。
 *
 * Opus downlink is exposed as one complete packet per event, so it uses
 * QM_AGENT_AUDIO_FRAMING_OPUS_PACKET rather than a byte stream framing.
 * Opus 下行以每事件一个完整包交付，因此使用 QM_AGENT_AUDIO_FRAMING_OPUS_PACKET，而不是
 * 字节流分帧方式。
 */
static int from_lgeine_audio(const lgeine_agent_audio_info_t *audio,
                             qm_agent_audio_format_t *format)
{
    if (!audio || !format ||
        (audio->format != LGEINE_AGENT_AUDIO_FORMAT_OPUS &&
         audio->format != LGEINE_AGENT_AUDIO_FORMAT_PCM) ||
        audio->rate == 0U || audio->channel < 1U || audio->channel > 2U ||
        audio->bits == 0U || audio->channel > UINT16_MAX ||
        audio->bits > UINT16_MAX || audio->frame_duration_ms == 0U ||
        audio->frame_duration_ms > UINT16_MAX) {
        return -QM_EINVAL;
    }
    format->codec = audio->format == LGEINE_AGENT_AUDIO_FORMAT_OPUS ?
                   QM_AGENT_AUDIO_CODEC_OPUS : QM_AGENT_AUDIO_CODEC_PCM;
    format->data_type = format->codec;
    format->sample_rate = audio->rate;
    format->channel_num = (uint16_t)audio->channel;
    format->bits_per_sample = (uint16_t)audio->bits;
    format->frame_ms = (uint16_t)audio->frame_duration_ms;
    format->framing = format->codec == QM_AGENT_AUDIO_CODEC_OPUS ?
                      QM_AGENT_AUDIO_FRAMING_OPUS_PACKET : QM_AGENT_AUDIO_FRAMING_STREAM;
    format->packet_bytes = 0U;
    return QM_EOK;
}

static lgeine_agent_listen_mode_t lgeine_listen_mode(const qm_agent_lgeine_ctx_t *ctx)
{
    return ctx->realtime_enabled ? LGEINE_AGENT_LISTEN_MODE_REALTIME :
                                    LGEINE_AGENT_LISTEN_MODE_MANUAL;
}

static int lgeine_start_listen(qm_agent_lgeine_ctx_t *ctx)
{
    int ret;

    if (!ctx || !ctx->agent) {
        return -QM_EINVAL;
    }
    if (ctx->listen_active) {
        return QM_EOK;
    }
    ret = lgeine_agent_send_listen(ctx->agent,
                                   LGEINE_AGENT_LISTEN_STATE_START,
                                   lgeine_listen_mode(ctx), NULL, 0U);
    if (ret == QM_EOK) {
        ctx->listen_active = QM_TRUE;
    }
    return ret;
}

static int lgeine_stop_listen(qm_agent_lgeine_ctx_t *ctx)
{
    int ret;

    if (!ctx->listen_active) {
        return QM_EOK;
    }
    ret = lgeine_agent_send_listen(ctx->agent,
                                   LGEINE_AGENT_LISTEN_STATE_STOP,
                                   lgeine_listen_mode(ctx), NULL, 0U);
    if (ret == QM_EOK) {
        ctx->listen_active = QM_FALSE;
    }
    return ret;
}

/**
 * @brief Normalize a borrowed lgeine_agent event and forward it to qm_agent.
 * 标准化借用的 lgeine_agent 事件并转发给 qm_agent。
 *
 * Source text and media pointers remain owned by lgeine_agent. The normalized
 * event is emitted synchronously and must not retain those pointers after the
 * callback returns.
 * 源文本和媒体指针仍由 lgeine_agent 所有。标准化事件同步发布，回调返回后不得保留这些指针。
 */
static void lgeine_on_event(lgeine_agent_handle_t agent,
                            const lgeine_agent_event_t *source,
                            void *user_ctx)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)user_ctx;
    qm_agent_event_t event;

    QM_UNUSED_ARG(agent);
    if (!ctx || !source) {
        return;
    }
    qm_memset(&event, 0, sizeof(event));
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;

    switch (source->id) {
        case LGEINE_AGENT_EVENT_READY:
            if (from_lgeine_audio(&source->data.ready.audio_info,
                                  &ctx->output_format) != QM_EOK) {
                lgeine_emit_error(ctx, -QM_EINVAL, "invalid lgeine output audio format");
                return;
            }
            {
                int ret = lgeine_start_listen(ctx);
                if (ret != QM_EOK) {
                    lgeine_emit_error(ctx, ret, "lgeine listen start failed");
                    return;
                }
            }
            if (!ctx->qm_connected) {
                ctx->qm_connected = QM_TRUE;
                event.id = QM_AGENT_EVENT_CONNECTED;
                lgeine_emit(ctx, &event);
            }
            break;
        case LGEINE_AGENT_EVENT_DISCONNECTED:
            if (ctx->qm_connected) {
                event.id = QM_AGENT_EVENT_DISCONNECTED;
                lgeine_emit(ctx, &event);
            }
            ctx->qm_connected = QM_FALSE;
            ctx->listen_active = QM_FALSE;
            break;
        case LGEINE_AGENT_EVENT_AUDIO:
            event.id = QM_AGENT_EVENT_AUDIO;
            event.data.audio.data = source->data.audio.data;
            event.data.audio.len = source->data.audio.data_len;
            event.data.audio.format = ctx->output_format;
            lgeine_emit(ctx, &event);
            break;
        case LGEINE_AGENT_EVENT_STT:
            event.id = QM_AGENT_EVENT_ASR;
            event.data.asr.text = source->data.stt.text;
            event.data.asr.final = source->data.stt.is_final;
            lgeine_emit(ctx, &event);
            break;
        case LGEINE_AGENT_EVENT_TTS:
            if (source->data.tts.state == LGEINE_AGENT_TTS_STATE_START) {
                /* BEGIN carries only stable format metadata. Audio bytes remain
                 * borrowed in later AUDIO events and follow this boundary.
                 * BEGIN 只携带稳定的格式元数据；音频字节仍在后续 AUDIO 事件中借用，并遵循该边界。 */
                event.id = QM_AGENT_EVENT_TTS_INPUT_BEGIN;
                event.data.audio.format = ctx->output_format;
                lgeine_emit(ctx, &event);
                qm_memset(&event.data, 0, sizeof(event.data));
            }
            event.id = QM_AGENT_EVENT_TTS;
            event.data.tts.text = source->data.tts.text;
            event.data.tts.final = source->data.tts.state == LGEINE_AGENT_TTS_STATE_STOP;
            lgeine_emit(ctx, &event);
            if (source->data.tts.state == LGEINE_AGENT_TTS_STATE_START ||
                source->data.tts.state == LGEINE_AGENT_TTS_STATE_SENTENCE_START) {
                event.id = QM_AGENT_EVENT_AGENT_STATE;
                event.data.agent_state.state = QM_AGENT_DIALOG_STATE_SPEAKING;
                event.data.agent_state.text = source->data.tts.type_name;
                lgeine_emit(ctx, &event);
            } else if (source->data.tts.state == LGEINE_AGENT_TTS_STATE_STOP) {
                /* The protocol delivers stop after the last binary message. Input
                 * completion seals provider ingress only; local PUSH and speaker
                 * drain continue until Audio reports its terminal.
                 * 协议在最后一个二进制消息后发送 stop。输入完成只封闭供应商入口；本地 PUSH 和扬声器
                 * 仍继续排空，直到 Audio 报告终态。 */
                qm_memset(&event.data, 0, sizeof(event.data));
                event.id = QM_AGENT_EVENT_TTS_INPUT_COMPLETE;
                lgeine_emit(ctx, &event);
                event.id = QM_AGENT_EVENT_AGENT_STATE;
                event.data.agent_state.state = QM_AGENT_DIALOG_STATE_SILENT;
                event.data.agent_state.text = "tts_stop";
                lgeine_emit(ctx, &event);
                if (ctx->qm_connected && !ctx->listen_active) {
                    int ret = lgeine_start_listen(ctx);
                    if (ret != QM_EOK) {
                        lgeine_emit_error(ctx, ret, "lgeine listen restart failed");
                    }
                }
            }
            break;
        case LGEINE_AGENT_EVENT_ABORT:
            event.id = QM_AGENT_EVENT_AGENT_ABORTED;
            lgeine_emit(ctx, &event);
            ctx->listen_active = QM_FALSE;
            event.id = QM_AGENT_EVENT_AGENT_STATE;
            event.data.agent_state.state = QM_AGENT_DIALOG_STATE_SILENT;
            event.data.agent_state.text = "abort";
            lgeine_emit(ctx, &event);
            if (source->data.abort.reason == LGEINE_AGENT_SERVER_ABORT_SERVER_POLICY &&
                ctx->qm_connected) {
                int ret = lgeine_start_listen(ctx);
                if (ret != QM_EOK) {
                    lgeine_emit_error(ctx, ret, "lgeine listen restart failed");
                }
            }
            break;
        case LGEINE_AGENT_EVENT_ERROR:
            event.id = QM_AGENT_EVENT_ERROR;
            event.code = source->data.error.code;
            event.data.error.message = source->data.error.message;
            lgeine_emit(ctx, &event);
            break;
        case LGEINE_AGENT_EVENT_TRANSPORT_CONNECTED:
        case LGEINE_AGENT_EVENT_LLM:
        default:
            break;
    }
}

/**
 * @brief Allocate the lgeine adapter and bind its provider callback.
 * 分配 lgeine 适配器并绑定供应商回调。
 *
 * Configuration strings are copied as pointers and remain borrowed from the
 * owning qm_agent configuration for the session lifetime.
 * 配置字符串只复制指针，在整个会话期间继续由 qm_agent 配置的调用方持有。
 */
static int lgeine_transport_init(void **transport_ctx,
                                 const qm_agent_transport_config_t *config,
                                 const qm_agent_audio_format_t *input_format,
                                 const qm_agent_audio_format_t *output_format,
                                 const qm_agent_video_format_t *input_video_format,
                                 const qm_agent_video_format_t *output_video_format,
                                 qm_agent_transport_event_cb_t event_cb,
                                 void *user_ctx)
{
    qm_agent_lgeine_ctx_t *ctx;
    lgeine_agent_init_config_t init_config;
    lgeine_agent_audio_info_t audio;
    int ret;

    if (!transport_ctx || !config || !input_format || !event_cb) {
        return -QM_EINVAL;
    }
    ret = to_lgeine_audio(input_format, &audio);
    if (ret != QM_EOK) {
        return ret;
    }
    /* The uri is optional: a NULL uri makes lgeine_agent build the endpoint
     * from the configured environment. Product/device identifiers and access
     * token are required; the token is filled in by qm_agent_apply_auth before
     * init. */
    if (!config->data.lgeine.pid || !config->data.lgeine.sn ||
        !config->data.lgeine.access_token) {
        return -QM_EINVAL;
    }
    ctx = (qm_agent_lgeine_ctx_t *)qm_malloc(sizeof(*ctx));
    if (!ctx) {
        return -QM_ENOMEM;
    }
    qm_memset(ctx, 0, sizeof(*ctx));
    qm_memcpy(&ctx->config, config, sizeof(*config));
    qm_memcpy(&ctx->input_format, input_format, sizeof(*input_format));
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
    ctx->realtime_enabled = QM_TRUE;
    qm_memset(&init_config, 0, sizeof(init_config));
    init_config.event_cb = lgeine_on_event;
    init_config.user_ctx = ctx;
    init_config.config.disable_auto_reconnect = config->data.lgeine.disable_auto_reconnect;
    init_config.config.disable_ping = config->data.lgeine.disable_ping;
    ctx->agent = lgeine_agent_init(&init_config);
    if (!ctx->agent) {
        qm_free(ctx);
        return -QM_EINIT;
    }
    *transport_ctx = ctx;
    (void)audio;
    return QM_EOK;
}

/**
 * @brief Destroy the provider only after it has stopped all callbacks.
 * 仅在供应商已停止全部回调后销毁其上下文。
 *
 * A failed destroy leaves the adapter context valid so qm_agent can retry the
 * required provider shutdown barrier.
 * 销毁失败时保留有效的适配器上下文，qm_agent 可重试供应商关闭屏障。
 */
static int lgeine_transport_deinit(void *transport_ctx)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;

    if (!ctx) {
        return -QM_EINVAL;
    }
    if (ctx->agent) {
        int ret = lgeine_agent_destroy(ctx->agent);
        if (ret != QM_EOK) {
            return ret;
        }
        ctx->agent = NULL;
    }
    qm_free(ctx);
    return QM_EOK;
}

/**
 * @brief Start an lgeine session with identity and negotiated capture format.
 * 使用身份信息和协商采集格式启动 lgeine 会话。
 */
static int lgeine_transport_start(void *transport_ctx)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;
    const qm_agent_lgeine_config_t *config;
    lgeine_agent_start_params_t params;
    int ret;

    if (!ctx || !ctx->agent) {
        return -QM_EINVAL;
    }
    config = &ctx->config.data.lgeine;
    if (config->env < (int32_t)LGEINE_AGENT_ENV_DEFAULT ||
        config->env > (int32_t)LGEINE_AGENT_ENV_PROD) {
        return -QM_EINVAL;
    }
    qm_memset(&params, 0, sizeof(params));
    params.uri = config->uri;
    params.env = (lgeine_agent_env_t)config->env;
    params.pid = config->pid;
    params.sn = config->sn;
    params.access_token = config->access_token;
    ret = to_lgeine_audio(&ctx->input_format, &params.audio_info);
    if (ret != QM_EOK) {
        return ret;
    }
    return lgeine_agent_start(ctx->agent, &params);
}

static int lgeine_transport_stop(void *transport_ctx)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;

    if (!ctx || !ctx->agent) {
        return -QM_EINVAL;
    }
    ctx->qm_connected = QM_FALSE;
    ctx->listen_active = QM_FALSE;
    return lgeine_agent_stop(ctx->agent);
}

/**
 * @brief Forward one borrowed PCM frame to lgeine_agent.
 * 向 lgeine_agent 转发一个借用的 PCM 帧。
 *
 * The provider must consume or copy data before returning, as required by the
 * qm_agent transport interface.
 * 按 qm_agent 传输接口约定，供应商必须在返回前消费或复制 data。
 */
static int lgeine_transport_send_audio(void *transport_ctx, const void *data, uint32_t len)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;

    if (!ctx || !ctx->agent || !data || len == 0U) {
        return -QM_EINVAL;
    }
    return lgeine_agent_send_audio(ctx->agent, data, len, 0U);
}

/**
 * @brief Report that lgeine does not provide qm_agent video uplink.
 * lgeine 未提供 qm_agent 视频上行能力，调用始终返回 -QM_ENOSYS。
 */
static int lgeine_transport_send_video(void *transport_ctx, const void *data, uint32_t len,
                                       const qm_agent_video_format_t *format)
{
    QM_UNUSED_ARG(transport_ctx);
    QM_UNUSED_ARG(data);
    QM_UNUSED_ARG(len);
    QM_UNUSED_ARG(format);
    return -QM_ENOSYS;
}

static int lgeine_transport_send_text(void *transport_ctx, const char *text)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;
    int ret;

    if (!ctx || !ctx->agent || !text || text[0] == '\0') {
        return -QM_EINVAL;
    }
    ret = lgeine_stop_listen(ctx);
    if (ret != QM_EOK) {
        return ret;
    }
    return lgeine_agent_send_speak(ctx->agent, text, 0U);
}

static int lgeine_transport_request_tts(void *transport_ctx, const char *text)
{
    return lgeine_transport_send_text(transport_ctx, text);
}

/**
 * @brief Abort active lgeine output, then restore listening when connected.
 * 中止活动的 lgeine 输出，并在连接仍有效时恢复监听。
 */
static int lgeine_transport_interrupt(void *transport_ctx)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;
    int ret;

    if (!ctx || !ctx->agent) {
        return -QM_EINVAL;
    }
    ret = lgeine_agent_send_abort(ctx->agent,
                                  LGEINE_AGENT_ABORT_REASON_USER_STOP, 0U);
    if (ret == QM_EOK) {
        ctx->listen_active = QM_FALSE;
    }
    if (ret == QM_EOK && ctx->qm_connected) {
        ret = lgeine_start_listen(ctx);
    }
    return ret;
}

static int lgeine_transport_asr_start(void *transport_ctx, uint32_t timeout_ms)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;

    QM_UNUSED_ARG(timeout_ms);
    if (!ctx || !ctx->agent) {
        return -QM_EINVAL;
    }
    ctx->realtime_enabled = QM_TRUE;
    return lgeine_start_listen(ctx);
}

static int lgeine_transport_asr_stop(void *transport_ctx, uint32_t timeout_ms)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;

    QM_UNUSED_ARG(timeout_ms);
    if (!ctx || !ctx->agent) {
        return -QM_EINVAL;
    }
    return lgeine_stop_listen(ctx);
}

static int lgeine_transport_asr_realtime(void *transport_ctx, qm_bool enabled,
                                         uint32_t timeout_ms)
{
    qm_agent_lgeine_ctx_t *ctx = (qm_agent_lgeine_ctx_t *)transport_ctx;
    int ret;

    QM_UNUSED_ARG(timeout_ms);
    if (!ctx || !ctx->agent) {
        return -QM_EINVAL;
    }
    ctx->realtime_enabled = enabled;
    if (!ctx->listen_active) {
        return QM_EOK;
    }
    ret = lgeine_stop_listen(ctx);
    return ret == QM_EOK ? lgeine_start_listen(ctx) : ret;
}

#endif

const qm_agent_transport_if_t *qm_agent_transport_lgeine(void)
{
    static const qm_agent_transport_if_t transport = {
        .name = "lgeine",
#if QM_AGENT_HAS_LGEINE_TRANSPORT
        .init = lgeine_transport_init,
        .deinit = lgeine_transport_deinit,
        .start = lgeine_transport_start,
        .stop = lgeine_transport_stop,
        .send_audio = lgeine_transport_send_audio,
        .send_video = lgeine_transport_send_video,
        .send_text = lgeine_transport_send_text,
        .request_tts = lgeine_transport_request_tts,
        .interrupt = lgeine_transport_interrupt,
        .asr_start_longform = lgeine_transport_asr_start,
        .asr_stop_longform = lgeine_transport_asr_stop,
        .asr_set_realtime_enabled = lgeine_transport_asr_realtime,
#else
        .init = NULL,
        .deinit = NULL,
        .start = NULL,
        .stop = NULL,
        .send_audio = NULL,
        .send_video = NULL,
        .send_text = NULL,
        .request_tts = NULL,
        .interrupt = NULL,
        .asr_start_longform = NULL,
        .asr_stop_longform = NULL,
        .asr_set_realtime_enabled = NULL,
#endif
    };

    return &transport;
}
