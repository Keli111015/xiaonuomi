/**
 * @file qm_agent_transport_baidu_chat_agent.c
 * @brief qm_agent adapter for the baidu_chat_agent WebSocket component.
 *
 * 适配器复用 baidu_chat_agent WebSocket 实现，并在边界处转换供应商事件、音频格式和错误。
 * 从供应商回调取得的载荷只在回调期间有效，不能被 Agent 控制任务直接保留。
 */

#include "qm_agent_internal.h"

#if defined(CONFIG_QM_AGENT_TRANSPORT_BAIDU) && CONFIG_QM_AGENT_TRANSPORT_BAIDU && \
    defined(CONFIG_BAIDU_CHAT_AGENT_SUPPORT) && CONFIG_BAIDU_CHAT_AGENT_SUPPORT
#include "baidu_chat_agent.h"
#define QM_AGENT_HAS_BAIDU_CHAT_AGENT_TRANSPORT 1
#else
#define QM_AGENT_HAS_BAIDU_CHAT_AGENT_TRANSPORT 0
#endif

#if QM_AGENT_HAS_BAIDU_CHAT_AGENT_TRANSPORT
#define LOG_TAG "qm_agent_baidu_ws"

typedef struct {
    qm_agent_transport_event_cb_t event_cb;
    void *user_ctx;
    qm_agent_transport_config_t config;
    qm_agent_audio_format_t input_format;
    qm_agent_audio_format_t output_format;
    qm_agent_video_format_t input_video_format;
    qm_agent_video_format_t output_video_format;
    baidu_chat_agent_handle_t agent;
    qm_bool ws_connected;
    qm_bool license_passed;
    qm_bool qm_connected;
} qm_agent_baidu_chat_agent_ctx_t;

static void baidu_chat_agent_emit(qm_agent_baidu_chat_agent_ctx_t *ctx,
                                  const qm_agent_event_t *event);

static qm_bool baidu_chat_agent_text_has_prefix(const char *text, const char *prefix)
{
    uint32_t prefix_len;

    if (text == NULL || prefix == NULL) {
        return QM_FALSE;
    }

    prefix_len = qm_strlen(prefix);
    return (qm_strncmp(text, prefix, prefix_len) == 0) ? QM_TRUE : QM_FALSE;
}

static qm_bool baidu_chat_agent_event_text_contains(const char *text, const char *needle)
{
    if (text == NULL || needle == NULL) {
        return QM_FALSE;
    }

    return (qm_strstr(text, needle) != NULL) ? QM_TRUE : QM_FALSE;
}

static void baidu_chat_agent_emit_error(qm_agent_baidu_chat_agent_ctx_t *ctx,
                                        int code,
                                        const char *message)
{
    qm_agent_event_t event;

    if (ctx == NULL) {
        return;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = QM_AGENT_EVENT_ERROR;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.code = code;
    event.data.error.message = message;
    baidu_chat_agent_emit(ctx, &event);
}

static void baidu_chat_agent_emit(qm_agent_baidu_chat_agent_ctx_t *ctx,
                                  const qm_agent_event_t *event)
{
    if (ctx != NULL && ctx->event_cb != NULL && event != NULL) {
        ctx->event_cb(event, ctx->user_ctx);
    }
}

/**
 * @brief Emit a single normalized connection event after both provider gates pass.
 * 在两个供应商准入条件均满足后仅发布一次标准化连接事件。
 *
 * baidu_chat_agent reports WebSocket connectivity and license activation
 * independently; qm_agent is connected only when both are ready.
 * baidu_chat_agent 独立上报 WebSocket 连接和授权激活；只有二者完成后 qm_agent 才视为已连接。
 */
static void emit_connected_if_ready(qm_agent_baidu_chat_agent_ctx_t *ctx)
{
    qm_agent_event_t event;

    if (ctx == NULL || ctx->qm_connected == QM_TRUE ||
        ctx->ws_connected == QM_FALSE || ctx->license_passed == QM_FALSE) {
        return;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = QM_AGENT_EVENT_CONNECTED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    ctx->qm_connected = QM_TRUE;
    baidu_chat_agent_emit(ctx, &event);
}

static qm_agent_audio_codec_t resolve_audio_codec(const qm_agent_audio_format_t *format)
{
    if (format == NULL) {
        return QM_AGENT_AUDIO_CODEC_NONE;
    }

    if (format->data_type != QM_AGENT_AUDIO_CODEC_NONE) {
        return format->data_type;
    }

    return format->codec;
}

static const char *baidu_chat_agent_codec_name(const qm_agent_audio_format_t *format)
{
    qm_agent_audio_codec_t codec;

    if (format == NULL) {
        return NULL;
    }

    codec = resolve_audio_codec(format);
    switch (codec) {
        case QM_AGENT_AUDIO_CODEC_PCM:
            if (format->sample_rate == 16000U &&
                format->channel_num == 1U &&
                format->bits_per_sample == 16U) {
                return "raw16k";
            }
            if (format->sample_rate == 8000U &&
                format->channel_num == 1U &&
                format->bits_per_sample == 16U) {
                return "raw";
            }
            return NULL;
        case QM_AGENT_AUDIO_CODEC_OPUS:
            return "opus";
        case QM_AGENT_AUDIO_CODEC_G711A:
            return "pcma";
        case QM_AGENT_AUDIO_CODEC_G711U:
            return "pcmu";
        case QM_AGENT_AUDIO_CODEC_G722:
            return "g722";
        default:
            return NULL;
    }
}

/**
 * @brief Validate one codec geometry that the Baidu adapter can pass through.
 * 校验 Baidu 适配器可以原样传递的编解码参数。
 *
 * The backend negotiates one audio codec in both directions and this adapter
 * has no PCM encoder, so capture and output formats must remain compatible.
 * 后端在两个方向协商同一音频编码，且本适配器不含 PCM 编码器，因此采集和输出格式必须兼容。
 */
static int validate_audio(const qm_agent_audio_format_t *input,
                          const qm_agent_audio_format_t *output)
{
    if (!input || !output) {
        return -QM_EINVAL;
    }
    /* ac negotiates both directions; this adapter has no PCM encoder. */
    if (input->codec != QM_AGENT_AUDIO_CODEC_PCM ||
        resolve_audio_codec(input) != QM_AGENT_AUDIO_CODEC_PCM ||
        resolve_audio_codec(output) != QM_AGENT_AUDIO_CODEC_PCM) {
        return -QM_ENOSYS;
    }
    if (baidu_chat_agent_codec_name(input) == NULL ||
        baidu_chat_agent_codec_name(output) == NULL ||
        input->sample_rate != output->sample_rate ||
        input->framing != QM_AGENT_AUDIO_FRAMING_STREAM ||
        output->framing != QM_AGENT_AUDIO_FRAMING_STREAM ||
        input->packet_bytes != 0U || output->packet_bytes != 0U) {
        return -QM_EINVAL;
    }
    return QM_EOK;
}

static qm_agent_dialog_state_t map_agent_state(baidu_chat_agent_text_type_t type)
{
    switch (type) {
        case BAIDU_CHAT_AGENT_TEXT_TYPE_AGENT_HANGUP:
        case BAIDU_CHAT_AGENT_TEXT_TYPE_BREAK:
            return QM_AGENT_DIALOG_STATE_SILENT;
        default:
            return QM_AGENT_DIALOG_STATE_NONE;
    }
}

static void emit_agent_state_if_needed(qm_agent_baidu_chat_agent_ctx_t *ctx,
                                       baidu_chat_agent_text_type_t type,
                                       const char *text)
{
    qm_agent_event_t event;
    qm_agent_dialog_state_t state;

    state = map_agent_state(type);
    if (state == QM_AGENT_DIALOG_STATE_NONE) {
        return;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = QM_AGENT_EVENT_AGENT_STATE;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.agent_state.state = state;
    event.data.agent_state.text = text;
    baidu_chat_agent_emit(ctx, &event);
}

static void emit_agent_state_text(qm_agent_baidu_chat_agent_ctx_t *ctx,
                                  qm_agent_dialog_state_t state,
                                  const char *text)
{
    qm_agent_event_t event;

    if (ctx == NULL || state == QM_AGENT_DIALOG_STATE_NONE) {
        return;
    }

    qm_memset(&event, 0, sizeof(event));
    event.id = QM_AGENT_EVENT_AGENT_STATE;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.agent_state.state = state;
    event.data.agent_state.text = text;
    baidu_chat_agent_emit(ctx, &event);
}

static void emit_input_begin(qm_agent_baidu_chat_agent_ctx_t *ctx)
{
    qm_agent_event_t event = {0};

    /* Send the control boundary before SPEAKING so Agent can preadmit PUSH.
     * 在 SPEAKING 前发送控制边界，使 Agent 可以预先准入 PUSH。 */
    event.id = QM_AGENT_EVENT_TTS_INPUT_BEGIN;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.audio.format = ctx->output_format;
    baidu_chat_agent_emit(ctx, &event);
    emit_agent_state_text(ctx, QM_AGENT_DIALOG_STATE_SPEAKING, "tts_begin_speak");
}

static void emit_input_complete(qm_agent_baidu_chat_agent_ctx_t *ctx)
{
    qm_agent_event_t event = {0};

    /* Provider input has ended; this is not local audible completion. Audio
     * emits the later playback terminal after its local drain finishes.
     * 供应商输入已经结束，但这不是本地可听播放完成；本地排空完成后由 Audio 发出播放终态。 */
    event.id = QM_AGENT_EVENT_TTS_INPUT_COMPLETE;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    baidu_chat_agent_emit(ctx, &event);
    emit_agent_state_text(ctx, QM_AGENT_DIALOG_STATE_SILENT, "tts_end_speak");
}

static void handle_event_text(qm_agent_baidu_chat_agent_ctx_t *ctx,
                              const baidu_chat_agent_text_t *text_event)
{
    const char *text;
    if (ctx == NULL || text_event == NULL || text_event->text == NULL) {
        return;
    }

    text = text_event->text;

    switch (text_event->semantic) {
        case BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_TTS_BEGIN_SPEAKING:
            emit_input_begin(ctx);
            return;
        case BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_TTS_END_SPEAKING:
            emit_input_complete(ctx);
            return;
        case BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_VOICE_COMING:
            emit_agent_state_text(ctx, QM_AGENT_DIALOG_STATE_LISTENING, "voice_coming");
            return;
        case BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_VOICE_DISAPPEAR:
            emit_agent_state_text(ctx, QM_AGENT_DIALOG_STATE_SILENT, "voice_disappear");
            return;
        case BAIDU_CHAT_AGENT_MESSAGE_SEMANTIC_EVENT_MEDIA_READY:
            emit_agent_state_text(ctx, QM_AGENT_DIALOG_STATE_THINKING, "media_ready");
            return;
        default:
            break;
    }

    /* Older provider revisions encode these lifecycle markers in text. Keep the
     * prefix fallback so their stream boundaries remain normalized.
     * 旧版供应商会将这些生命周期标记编码在文本中；保留前缀回退以标准化其流边界。 */
    if (baidu_chat_agent_text_has_prefix(text, "[LIC]:[MUST]:")) {
        emit_agent_state_text(ctx, QM_AGENT_DIALOG_STATE_THINKING, "license_must");
        return;
    }

    if (baidu_chat_agent_text_has_prefix(text, "[LIC]:[RES]:[PASS]:")) {
        return;
    }

    if (baidu_chat_agent_text_has_prefix(text, "[LIC]:[RES]:[FAILED]:")) {
        return;
    }

    if (baidu_chat_agent_text_has_prefix(text, "[TTS_BEGIN_SPEAK")) {
        emit_input_begin(ctx);
        return;
    }

    if (baidu_chat_agent_text_has_prefix(text, "[TTS_END_SPEAK")) {
        emit_input_complete(ctx);
        return;
    }

    if (baidu_chat_agent_text_has_prefix(text, "[ASR_BEGIN")) {
        emit_agent_state_text(ctx, QM_AGENT_DIALOG_STATE_LISTENING, "asr_begin");
        return;
    }

    if (baidu_chat_agent_text_has_prefix(text, "[ASR_END")) {
        emit_agent_state_text(ctx, QM_AGENT_DIALOG_STATE_THINKING, "asr_end");
        return;
    }

    if (baidu_chat_agent_event_text_contains(text, "Timeout")) {
        baidu_chat_agent_emit_error(ctx, -QM_ETIMEOUT, text);
        return;
    }
}

/**
 * @brief Translate a borrowed baidu_chat_agent callback into qm_agent events.
 * 将借用的 baidu_chat_agent 回调转换为 qm_agent 事件。
 *
 * TTS begin/audio/complete events preserve provider order so the audio engine
 * can distinguish network input completion from speaker playback completion.
 * TTS 的 begin/audio/complete 事件保留供应商顺序，使音频引擎可区分网络输入结束与扬声器
 * 实际播放结束。
 */
static void qm_agent_baidu_chat_agent_event_cb(baidu_chat_agent_handle_t handle,
                                               const baidu_chat_agent_event_t *event,
                                               void *user_ctx)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)user_ctx;
    qm_agent_event_t mapped_event;

    QM_UNUSED_ARG(handle);

    if (ctx == NULL || event == NULL) {
        return;
    }

    qm_memset(&mapped_event, 0, sizeof(mapped_event));
    mapped_event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;

    switch (event->id) {
        case BAIDU_CHAT_AGENT_EVENT_CONNECTED:
            ctx->ws_connected = QM_TRUE;
            emit_connected_if_ready(ctx);
            break;
        case BAIDU_CHAT_AGENT_EVENT_DISCONNECTED:
            if (ctx->qm_connected == QM_TRUE) {
                mapped_event.id = QM_AGENT_EVENT_DISCONNECTED;
                baidu_chat_agent_emit(ctx, &mapped_event);
            }
            ctx->ws_connected = QM_FALSE;
            ctx->license_passed = QM_FALSE;
            ctx->qm_connected = QM_FALSE;
            break;
        case BAIDU_CHAT_AGENT_EVENT_AUDIO:
            mapped_event.id = QM_AGENT_EVENT_AUDIO;
            mapped_event.data.audio.data = event->data.audio.data;
            mapped_event.data.audio.len = event->data.audio.data_len;
            mapped_event.data.audio.format = ctx->output_format;
            baidu_chat_agent_emit(ctx, &mapped_event);
            break;
        case BAIDU_CHAT_AGENT_EVENT_ASR:
            mapped_event.id = QM_AGENT_EVENT_ASR;
            mapped_event.data.asr.text = event->data.text.text;
            mapped_event.data.asr.final = event->data.text.final;
            baidu_chat_agent_emit(ctx, &mapped_event);
            break;
        case BAIDU_CHAT_AGENT_EVENT_AGENT_TEXT:
            mapped_event.id = QM_AGENT_EVENT_AGENT_TEXT;
            mapped_event.data.agent_text.text = event->data.text.text;
            mapped_event.data.agent_text.final = event->data.text.final;
            baidu_chat_agent_emit(ctx, &mapped_event);
            emit_agent_state_if_needed(ctx,
                                       event->data.text.type,
                                       event->data.text.text);
            break;
        case BAIDU_CHAT_AGENT_EVENT_BREAK:
            mapped_event.id = QM_AGENT_EVENT_AGENT_ABORTED;
            baidu_chat_agent_emit(ctx, &mapped_event);
            emit_agent_state_if_needed(ctx,
                                       event->data.text.type,
                                       event->data.text.text);
            break;
        case BAIDU_CHAT_AGENT_EVENT_ERROR:
            mapped_event.id = QM_AGENT_EVENT_ERROR;
            mapped_event.code = event->data.error.code;
            mapped_event.data.error.message = event->data.error.message;
            baidu_chat_agent_emit(ctx, &mapped_event);
            break;
        case BAIDU_CHAT_AGENT_EVENT_EVENT_TEXT:
            handle_event_text(ctx, &event->data.text);
            break;
        case BAIDU_CHAT_AGENT_EVENT_LICENSE:
            if (event->data.license.state == BAIDU_CHAT_AGENT_LICENSE_STATE_PASS) {
                ctx->license_passed = QM_TRUE;
                emit_connected_if_ready(ctx);
            } else if (event->data.license.state == BAIDU_CHAT_AGENT_LICENSE_STATE_FAILED) {
                ctx->license_passed = QM_FALSE;
            } else if (event->data.license.state == BAIDU_CHAT_AGENT_LICENSE_STATE_MUST) {
                ctx->license_passed = QM_FALSE;
            }
            break;
        case BAIDU_CHAT_AGENT_EVENT_TEXT:
        case BAIDU_CHAT_AGENT_EVENT_FUNCTION_CALL:
        case BAIDU_CHAT_AGENT_EVENT_SET_TEXT:
        default:
            break;
    }
}

/**
 * @brief Allocate the Baidu adapter and initialize the provider callback bridge.
 * 分配 Baidu 适配器并初始化供应商回调桥接层。
 */
static int baidu_chat_agent_transport_init(void **transport_ctx,
                                           const qm_agent_transport_config_t *config,
                                           const qm_agent_audio_format_t *input_format,
                                           const qm_agent_audio_format_t *output_format,
                                           const qm_agent_video_format_t *input_video_format,
                                           const qm_agent_video_format_t *output_video_format,
                                           qm_agent_transport_event_cb_t event_cb,
                                           void *user_ctx)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx;
    baidu_chat_agent_init_config_t init_config;
    int ret;

    if (transport_ctx == NULL || config == NULL || input_format == NULL ||
        output_format == NULL || event_cb == NULL) {
        return -QM_EINVAL;
    }
    ret = validate_audio(input_format, output_format);
    if (ret != QM_EOK) {
        return ret;
    }

    ctx = (qm_agent_baidu_chat_agent_ctx_t *)qm_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }

    qm_memset(ctx, 0, sizeof(*ctx));
    qm_memcpy(&ctx->config, config, sizeof(*config));
    if (input_format != NULL) {
        qm_memcpy(&ctx->input_format, input_format, sizeof(*input_format));
    }
    if (output_format != NULL) {
        qm_memcpy(&ctx->output_format, output_format, sizeof(*output_format));
    }
    if (input_video_format != NULL) {
        qm_memcpy(&ctx->input_video_format, input_video_format, sizeof(*input_video_format));
    }
    if (output_video_format != NULL) {
        qm_memcpy(&ctx->output_video_format, output_video_format, sizeof(*output_video_format));
    }
    ctx->event_cb = event_cb;
    ctx->user_ctx = user_ctx;

    qm_memset(&init_config, 0, sizeof(init_config));
    init_config.event_cb = qm_agent_baidu_chat_agent_event_cb;
    init_config.user_ctx = ctx;
    ctx->agent = baidu_chat_agent_init(&init_config);
    if (ctx->agent == NULL) {
        qm_free(ctx);
        return -QM_EINIT;
    }

    *transport_ctx = ctx;
    return QM_EOK;
}

/**
 * @brief Destroy the Baidu provider after session callbacks are quiesced.
 * 在会话回调静默后销毁 Baidu 供应商对象。
 */
static int baidu_chat_agent_transport_deinit(void *transport_ctx)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;
    int ret = QM_EOK;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    if (ctx->agent != NULL) {
        ret = baidu_chat_agent_destroy(ctx->agent);
        if (ret != QM_EOK) {
            return ret;
        }
        ctx->agent = NULL;
    }

    qm_free(ctx);
    return ret;
}

static int baidu_chat_agent_transport_start(void *transport_ctx)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;
    baidu_chat_agent_start_params_t params;
    const char *audio_codec;
    const char *configured_audio_codec;

    if (ctx == NULL || ctx->agent == NULL) {
        return -QM_EINVAL;
    }

    ctx->ws_connected = QM_FALSE;
    ctx->license_passed = QM_FALSE;
    ctx->qm_connected = QM_FALSE;
    audio_codec = baidu_chat_agent_codec_name(&ctx->input_format);
    if (audio_codec == NULL) {
        return -QM_EINVAL;
    }
    configured_audio_codec = ctx->config.data.baidu_chat_agent.audio_codec;
    if (configured_audio_codec != NULL && configured_audio_codec[0] != '\0') {
        if (qm_strcmp(configured_audio_codec, audio_codec) != 0) {
            QM_AGENT_LOGE(LOG_TAG,
                          "audio codec mismatch auth=%s local=%s",
                          configured_audio_codec,
                          audio_codec);
            return -QM_EINVAL;
        }
        audio_codec = configured_audio_codec;
    }

    qm_memset(&params, 0, sizeof(params));
    params.auth_mode = BAIDU_CHAT_AGENT_AUTH_MODE_INSTANCE_TOKEN;
    params.app_id = ctx->config.data.baidu_chat_agent.app_id;
    params.audio_codec = audio_codec;
    /** @brief The baidu_chat_agent layer only accepts ptime for opus sessions. */
    if (resolve_audio_codec(&ctx->input_format) == QM_AGENT_AUDIO_CODEC_OPUS) {
        params.packet_time_ms = ctx->input_format.frame_ms;
        params.packet_max_len = ctx->config.data.baidu_chat_agent.packet_max_len;
    }
    params.auth.instance_token.instance_id =
        ctx->config.data.baidu_chat_agent.instance_id;
    params.auth.instance_token.token =
        ctx->config.data.baidu_chat_agent.token;
    params.headers = ctx->config.data.baidu_chat_agent.headers;
    params.user_id = ctx->config.data.baidu_chat_agent.user_id;
    params.device_id = ctx->config.data.baidu_chat_agent.device_id;
    params.license_key = ctx->config.data.baidu_chat_agent.license_key;

    return baidu_chat_agent_start(ctx->agent, &params);
}

static int baidu_chat_agent_transport_stop(void *transport_ctx)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    if (ctx == NULL || ctx->agent == NULL) {
        return -QM_EINVAL;
    }

    return baidu_chat_agent_stop(ctx->agent);
}

/**
 * @brief Forward a borrowed upstream frame to baidu_chat_agent.
 * 向 baidu_chat_agent 转发借用的上行音频帧。
 */
static int baidu_chat_agent_transport_send_audio(void *transport_ctx,
                                                 const void *data,
                                                 uint32_t len)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    if (ctx == NULL || ctx->agent == NULL || data == NULL || len == 0U) {
        return -QM_EINVAL;
    }

    return baidu_chat_agent_send_audio(ctx->agent, (const uint8_t *)data, len, 0U);
}

/**
 * @brief Reject video because baidu_chat_agent expects image locators, not raw frames.
 * 拒绝视频上行，因为 baidu_chat_agent 接受图片定位符而不是原始帧。
 */
static int baidu_chat_agent_transport_send_video(void *transport_ctx,
                                                 const void *data,
                                                 uint32_t len,
                                                 const qm_agent_video_format_t *format)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    QM_UNUSED_ARG(data);
    QM_UNUSED_ARG(len);
    QM_UNUSED_ARG(format);

    if (ctx == NULL || ctx->agent == NULL) {
        return -QM_EINVAL;
    }

    /** @brief qm_agent video uploads are raw frame payloads, not image locator strings. */
    return -QM_ENOSYS;
}

static int baidu_chat_agent_transport_send_text(void *transport_ctx, const char *text)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    if (ctx == NULL || ctx->agent == NULL || text == NULL) {
        return -QM_EINVAL;
    }

    return baidu_chat_agent_send_text(ctx->agent, text, 0U);
}

static int baidu_chat_agent_transport_request_tts(void *transport_ctx, const char *text)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    if (ctx == NULL || ctx->agent == NULL || text == NULL) {
        return -QM_EINVAL;
    }

    return baidu_chat_agent_send_text_to_tts(ctx->agent, text, 0U);
}

static int baidu_chat_agent_transport_interrupt(void *transport_ctx)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    if (ctx == NULL || ctx->agent == NULL) {
        return -QM_EINVAL;
    }

    return baidu_chat_agent_send_break(ctx->agent, "", 0U);
}

static int baidu_chat_agent_transport_asr_start_longform(void *transport_ctx,
                                                         uint32_t timeout_ms)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    if (ctx == NULL || ctx->agent == NULL) {
        return -QM_EINVAL;
    }

    return baidu_chat_agent_send_asr_start_longtext_rec(ctx->agent, timeout_ms);
}

static int baidu_chat_agent_transport_asr_stop_longform(void *transport_ctx,
                                                        uint32_t timeout_ms)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    if (ctx == NULL || ctx->agent == NULL) {
        return -QM_EINVAL;
    }

    return baidu_chat_agent_send_asr_stop_longtext_rec(ctx->agent, timeout_ms);
}

static int baidu_chat_agent_transport_asr_set_realtime_enabled(void *transport_ctx,
                                                               qm_bool enabled,
                                                               uint32_t timeout_ms)
{
    qm_agent_baidu_chat_agent_ctx_t *ctx = (qm_agent_baidu_chat_agent_ctx_t *)transport_ctx;

    if (ctx == NULL || ctx->agent == NULL) {
        return -QM_EINVAL;
    }

    return baidu_chat_agent_set_asr_realtime_enabled(ctx->agent,
                                                     enabled,
                                                     timeout_ms);
}
#endif

const qm_agent_transport_if_t *qm_agent_transport_baidu_chat_agent(void)
{
    static const qm_agent_transport_if_t transport = {
        .name = "baidu_chat_agent",
#if QM_AGENT_HAS_BAIDU_CHAT_AGENT_TRANSPORT
        .init = baidu_chat_agent_transport_init,
        .deinit = baidu_chat_agent_transport_deinit,
        .start = baidu_chat_agent_transport_start,
        .stop = baidu_chat_agent_transport_stop,
        .send_audio = baidu_chat_agent_transport_send_audio,
        .send_video = baidu_chat_agent_transport_send_video,
        .send_text = baidu_chat_agent_transport_send_text,
        .request_tts = baidu_chat_agent_transport_request_tts,
        .interrupt = baidu_chat_agent_transport_interrupt,
        .asr_start_longform = baidu_chat_agent_transport_asr_start_longform,
        .asr_stop_longform = baidu_chat_agent_transport_asr_stop_longform,
        .asr_set_realtime_enabled = baidu_chat_agent_transport_asr_set_realtime_enabled,
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
