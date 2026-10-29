/**
 * @file qm_agent_transport_joyinside.c
 * @brief JoyInside transport adapter. JoyInside 传输适配层。
 *
 * It keeps the JoyInside client API contained in this file and exposes
 * normalized qm_agent events/commands to the rest of the component.
 * 本文件隔离 JoyInside Client API，并向 qm_agent 其余部分提供标准化事件和命令。
 * JoyInside 回调携带的文本、音视频缓冲区只在回调期间借用，适配层在向上转发前不保留它们。
 */

#include "qm_agent_internal.h"

#if defined(CONFIG_QM_AGENT_TRANSPORT_JOYINSIDE) && CONFIG_QM_AGENT_TRANSPORT_JOYINSIDE && \
    defined(CONFIG_JOYINSIDE_CLIENT_SUPPORT) && CONFIG_JOYINSIDE_CLIENT_SUPPORT
#define QM_AGENT_HAS_JOYINSIDE_TRANSPORT 1
#else
#define QM_AGENT_HAS_JOYINSIDE_TRANSPORT 0
#endif

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
#include "joyinside_client.h"
#endif

#define LOG_TAG "qm_agent_joyinside"

typedef struct {
    qm_agent_transport_event_cb_t event_cb;
    void *user_ctx;
    qm_agent_transport_config_t config;
    qm_agent_audio_format_t input_format;
    qm_agent_audio_format_t output_format;
    qm_bool output_probe_pending;
    uint32_t output_offset;
    qm_agent_video_format_t input_video_format;
    qm_agent_video_format_t output_video_format;
#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    joyinside_client_handle_t client;
#endif
} qm_agent_joyinside_ctx_t;

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
/**
 * @brief Validate the format that can pass through JoyInside unchanged.
 *
 * @par 中文说明
 * 校验可由 JoyInside 原样传输的音频格式。
 *
 * joyinside_client forwards supplied bytes unchanged. It can carry PCM and a
 * fixed-size Opus payload, but cannot reconstruct variable packet boundaries.
 */
static int validate_audio(const qm_agent_audio_format_t *format, qm_bool input)
{
    qm_agent_audio_codec_t payload;

    if (!format || format->channel_num != 1U || format->bits_per_sample != 16U ||
        (format->sample_rate != JOYINSIDE_CLIENT_DEFAULT_SAMPLE_RATE &&
         format->sample_rate != JOYINSIDE_CLIENT_SAMPLE_RATE_24K &&
         format->sample_rate != JOYINSIDE_CLIENT_SAMPLE_RATE_32K)) {
        return -QM_EINVAL;
    }
    payload = format->data_type != QM_AGENT_AUDIO_CODEC_NONE ?
              format->data_type : format->codec;
    if (input && format->codec != payload) {
        return -QM_ENOSYS;
    }
    if (payload == QM_AGENT_AUDIO_CODEC_OPUS) {
        /* Binary callbacks may split a packet. Only known fixed packet sizes
         * can be represented as a byte stream without fabricating headers. */
        return format->framing == QM_AGENT_AUDIO_FRAMING_OPUS_CBR &&
               format->packet_bytes != 0U ? QM_EOK : -QM_ENOSYS;
    }
    if (format->framing != QM_AGENT_AUDIO_FRAMING_STREAM || format->packet_bytes != 0U) {
        return -QM_EINVAL;
    }
    return payload == QM_AGENT_AUDIO_CODEC_PCM ||
           (!input && payload == QM_AGENT_AUDIO_CODEC_MP3) ? QM_EOK : -QM_ENOSYS;
}

static qm_agent_audio_codec_t joyinside_effective_codec(
    const qm_agent_audio_format_t *format)
{
    if (format == NULL) {
        return QM_AGENT_AUDIO_CODEC_NONE;
    }
    return format->data_type != QM_AGENT_AUDIO_CODEC_NONE ?
           format->data_type : format->codec;
}

static int joyinside_map_audio_codec(const qm_agent_audio_format_t *format,
                                     qm_bool input,
                                     joyinside_client_audio_codec_t *codec)
{
    qm_agent_audio_codec_t effective;

    if (format == NULL || codec == NULL) {
        return -QM_EINVAL;
    }
    effective = joyinside_effective_codec(format);
    if (effective == QM_AGENT_AUDIO_CODEC_PCM) {
        *codec = JOYINSIDE_CLIENT_AUDIO_CODEC_PCM;
        return QM_EOK;
    }
    if (effective == QM_AGENT_AUDIO_CODEC_OPUS) {
        *codec = JOYINSIDE_CLIENT_AUDIO_CODEC_OPUS;
        return QM_EOK;
    }
    if (!input && effective == QM_AGENT_AUDIO_CODEC_MP3) {
        *codec = JOYINSIDE_CLIENT_AUDIO_CODEC_MP3;
        return QM_EOK;
    }
    return -QM_ENOSYS;
}

static uint32_t joyinside_output_frame_ms(const qm_agent_audio_format_t *format)
{
    uint32_t frame_ms;
    qm_agent_audio_codec_t codec;

    if (format == NULL) {
        return 0;
    }

    codec = joyinside_effective_codec(format);

    frame_ms = format->frame_ms;
    if (frame_ms == 0U) {
        return 0;
    }

    if (codec == QM_AGENT_AUDIO_CODEC_OPUS) {
        return (frame_ms == 10U || frame_ms == 20U || frame_ms == 40U || frame_ms == 60U) ?
               frame_ms :
               0U;
    }

    if (codec == QM_AGENT_AUDIO_CODEC_PCM) {
        return (frame_ms >= 60U && frame_ms <= 120U) ? frame_ms : 0U;
    }

    return 0U;
}
#endif

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
static void joyinside_emit(qm_agent_joyinside_ctx_t *ctx, const qm_agent_event_t *event)
{
    if (ctx && ctx->event_cb && event) {
        ctx->event_cb(event, ctx->user_ctx);
    }
}

static qm_bool joyinside_text_present(const char *text)
{
    return text != NULL && text[0] != '\0' ? QM_TRUE : QM_FALSE;
}

static qm_agent_text_finish_reason_t joyinside_finish_reason(const char *reason)
{
    if (!joyinside_text_present(reason)) {
        return QM_AGENT_TEXT_FINISH_REASON_NONE;
    }
    if (qm_strncmp(reason, "stop", 5U) == 0) {
        return QM_AGENT_TEXT_FINISH_REASON_STOP;
    }
    if (qm_strncmp(reason, "error", 6U) == 0) {
        return QM_AGENT_TEXT_FINISH_REASON_ERROR;
    }
    if (qm_strncmp(reason, "audit", 6U) == 0) {
        return QM_AGENT_TEXT_FINISH_REASON_AUDIT;
    }
    if (qm_strncmp(reason, "content_filter", 15U) == 0) {
        return QM_AGENT_TEXT_FINISH_REASON_CONTENT_FILTER;
    }
    return QM_AGENT_TEXT_FINISH_REASON_UNKNOWN;
}

/**
 * @brief Map a JoyInside code to a QUMI facial expression. 将 JoyInside 编码映射为 QUMI 面部表情。
 *
 * The mapping intentionally merges provider-specific near-synonyms. The raw
 * provider code remains in the emitted event for a product-specific override.
 * 映射会合并供应商的近义编码；原始编码仍保留在事件中，供产品级覆盖使用。
 *
 * @param[in] expression Parsed JoyInside expression code. 已解析的 JoyInside 表情编码。
 * @return QUMI facial expression, or UNKNOWN when no visual enum applies. QUMI 面部表情；不适用时为 UNKNOWN。
 */
static qm_agent_expression_t joyinside_visual_expression(
    joyinside_client_expression_t expression)
{
    switch (expression) {
        case JOYINSIDE_CLIENT_EXPRESSION_ANGRY:
            return QM_AGENT_EXPRESSION_ANGRY;
        case JOYINSIDE_CLIENT_EXPRESSION_ANXIOUS:
            return QM_AGENT_EXPRESSION_ANXIOUS;
        case JOYINSIDE_CLIENT_EXPRESSION_BLAMED:
            return QM_AGENT_EXPRESSION_ANGRY;
        case JOYINSIDE_CLIENT_EXPRESSION_SAD:
        case JOYINSIDE_CLIENT_EXPRESSION_SORROWFUL:
        case JOYINSIDE_CLIENT_EXPRESSION_WRONGED:
            return QM_AGENT_EXPRESSION_SAD;
        case JOYINSIDE_CLIENT_EXPRESSION_CONFUSED:
            return QM_AGENT_EXPRESSION_CONFUSED;
        case JOYINSIDE_CLIENT_EXPRESSION_COQUETRY:
            return QM_AGENT_EXPRESSION_AFFECTIONATE;
        case JOYINSIDE_CLIENT_EXPRESSION_DISAPPOINT:
            return QM_AGENT_EXPRESSION_DISAPPOINTED;
        case JOYINSIDE_CLIENT_EXPRESSION_DISDAINFUL:
        case JOYINSIDE_CLIENT_EXPRESSION_HATE:
            return QM_AGENT_EXPRESSION_DISGUSTED;
        case JOYINSIDE_CLIENT_EXPRESSION_EXCITING:
            return QM_AGENT_EXPRESSION_EXCITED;
        case JOYINSIDE_CLIENT_EXPRESSION_EXPECT:
            return QM_AGENT_EXPRESSION_EXPECTANT;
        case JOYINSIDE_CLIENT_EXPRESSION_FEAR:
            return QM_AGENT_EXPRESSION_FEAR;
        case JOYINSIDE_CLIENT_EXPRESSION_HAPPY:
            return QM_AGENT_EXPRESSION_HAPPY;
        case JOYINSIDE_CLIENT_EXPRESSION_HESITATE:
            return QM_AGENT_EXPRESSION_HESITANT;
        case JOYINSIDE_CLIENT_EXPRESSION_INQUISITIVE:
            return QM_AGENT_EXPRESSION_CURIOUS;
        case JOYINSIDE_CLIENT_EXPRESSION_PEACE:
            return QM_AGENT_EXPRESSION_CALM;
        case JOYINSIDE_CLIENT_EXPRESSION_SHOCK:
            return QM_AGENT_EXPRESSION_SURPRISED;
        case JOYINSIDE_CLIENT_EXPRESSION_SHY:
            return QM_AGENT_EXPRESSION_SHY;
        case JOYINSIDE_CLIENT_EXPRESSION_SICK:
            return QM_AGENT_EXPRESSION_BORED;
        case JOYINSIDE_CLIENT_EXPRESSION_DISAGREE:
        case JOYINSIDE_CLIENT_EXPRESSION_NOD:
        case JOYINSIDE_CLIENT_EXPRESSION_UNKNOWN:
        default:
            return QM_AGENT_EXPRESSION_UNKNOWN;
    }
}

/**
 * @brief Map an explicit JoyInside action to a QUMI gesture. 将明确的 JoyInside 动作映射为 QUMI 姿态动作。
 *
 * The provider defines nod_1 as nodding and disagree_1 as head shaking.
 * 供应商协议将 nod_1 定义为点头，将 disagree_1 定义为摇头。
 *
 * @param[in] expression Parsed JoyInside expression code. 已解析的 JoyInside 表情编码。
 * @return QUMI gesture, or NONE when no explicit action exists. QUMI 姿态动作；无明确动作时为 NONE。
 */
static qm_agent_gesture_t joyinside_gesture(
    joyinside_client_expression_t expression)
{
    switch (expression) {
        case JOYINSIDE_CLIENT_EXPRESSION_NOD:
            return QM_AGENT_GESTURE_NOD;
        case JOYINSIDE_CLIENT_EXPRESSION_DISAGREE:
            return QM_AGENT_GESTURE_SHAKE_HEAD;
        default:
            return QM_AGENT_GESTURE_NONE;
    }
}

/**
 * @brief Classify a JoyInside expression into the QUMI top-level taxonomy. 将 JoyInside 表情归入 QUMI 一级分类。
 *
 * This table covers all 23 documented JoyInside EMOTION values. It preserves
 * social meanings such as blamed_1, wronged_1, and disdainful_1
 * even when their closest visual fallback is a basic facial expression.
 * 此表覆盖 JoyInside 文档定义的全部 23 个 EMOTION 值。即使视觉回退为基础表情，
 * 仍保留 blamed_1、wronged_1 和 disdainful_1 的社会认知语义。
 *
 * @param[in] expression Parsed JoyInside expression code. 已解析的 JoyInside 表情编码。
 * @return QUMI top-level category. QUMI 一级分类。
 */
static qm_agent_expression_category_t joyinside_expression_category(
    joyinside_client_expression_t expression)
{
    switch (expression) {
        case JOYINSIDE_CLIENT_EXPRESSION_PEACE:
            return QM_AGENT_EXPR_CATEGORY_FACIAL_STATE;
        case JOYINSIDE_CLIENT_EXPRESSION_ANGRY:
        case JOYINSIDE_CLIENT_EXPRESSION_ANXIOUS:
        case JOYINSIDE_CLIENT_EXPRESSION_DISAPPOINT:
        case JOYINSIDE_CLIENT_EXPRESSION_EXCITING:
        case JOYINSIDE_CLIENT_EXPRESSION_FEAR:
        case JOYINSIDE_CLIENT_EXPRESSION_HAPPY:
        case JOYINSIDE_CLIENT_EXPRESSION_HATE:
        case JOYINSIDE_CLIENT_EXPRESSION_SAD:
        case JOYINSIDE_CLIENT_EXPRESSION_SHOCK:
        case JOYINSIDE_CLIENT_EXPRESSION_SORROWFUL:
            return QM_AGENT_EXPR_CATEGORY_AFFECT;
        case JOYINSIDE_CLIENT_EXPRESSION_BLAMED:
        case JOYINSIDE_CLIENT_EXPRESSION_CONFUSED:
        case JOYINSIDE_CLIENT_EXPRESSION_COQUETRY:
        case JOYINSIDE_CLIENT_EXPRESSION_DISDAINFUL:
        case JOYINSIDE_CLIENT_EXPRESSION_EXPECT:
        case JOYINSIDE_CLIENT_EXPRESSION_HESITATE:
        case JOYINSIDE_CLIENT_EXPRESSION_INQUISITIVE:
        case JOYINSIDE_CLIENT_EXPRESSION_SHY:
        case JOYINSIDE_CLIENT_EXPRESSION_WRONGED:
            return QM_AGENT_EXPR_CATEGORY_SOCIAL_COGNITIVE;
        case JOYINSIDE_CLIENT_EXPRESSION_SICK:
            return QM_AGENT_EXPR_CATEGORY_AFFECT;
        case JOYINSIDE_CLIENT_EXPRESSION_DISAGREE:
        case JOYINSIDE_CLIENT_EXPRESSION_NOD:
            return QM_AGENT_EXPR_CATEGORY_GESTURE;
        case JOYINSIDE_CLIENT_EXPRESSION_UNKNOWN:
        default:
            return QM_AGENT_EXPR_CATEGORY_UNKNOWN;
    }
}

/**
 * @brief Opus TOC packet duration in milliseconds, 0 when unparseable.
 * Opus TOC 包时长（毫秒），无法解析时为 0。
 *
 * The TOC encodes frame count and per-frame units, so a legal Opus packet at
 * the negotiated duration is distinguishable from wire wrapper bytes.
 * TOC 编码了帧数与单帧单元数，因此可区分“协商时长下的合法 Opus 包”与“线上包头字节”。
 */
static uint32_t joyinside_opus_duration_ms(const uint8_t *data, uint32_t size)
{
    uint32_t frame_units;
    uint32_t count;
    uint32_t total;

    if (data == NULL || size < 1U) return 0U;
    if (data[0] & 0x80U) frame_units = 1U << ((data[0] >> 3U) & 3U);
    else if ((data[0] & 0x60U) == 0x60U) frame_units = (data[0] & 0x08U) ? 8U : 4U;
    else {
        uint32_t mode = (data[0] >> 3U) & 3U;
        frame_units = mode == 3U ? 24U : (4U << mode);
    }
    count = data[0] & 3U;
    if (count == 0U) count = 1U;
    else if (count < 3U) count = 2U;
    else {
        if (size < 2U) return 0U;
        count = data[1] & 0x3fU;
    }
    total = frame_units * count; /* units of 2.5 ms / 以 2.5ms 为单位 */
    if (total == 0U || total > 48U || (total & 1U) != 0U) return 0U;
    return total * 5U / 2U;
}

/**
 * @brief Convert JoyInside Client callbacks to normalized qm_agent events. 将 JoyInside Client 回调转换为标准 qm_agent 事件。
 *
 * @param[in] event Borrowed callback event, valid only during this call. 借用的回调事件，仅在本次调用期间有效。
 *
 * @note The mapped audio/text pointers remain borrowed. qm_agent must consume or copy them before this callback returns.
 *       映射后的音频和文本指针仍为借用数据，qm_agent 必须在回调返回前消费或复制。
 */
static void joyinside_on_event(const joyinside_client_event_t *event)
{
    qm_agent_joyinside_ctx_t *ctx;
    qm_agent_event_t agent_event = {0};

    if (event == NULL) {
        return;
    }

    ctx = (qm_agent_joyinside_ctx_t *)event->user_context;
    if (ctx == NULL) {
        return;
    }

    agent_event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    /* Convert JoyInside-specific events into the qm_agent event vocabulary.
     * / 将 JoyInside 特有事件归一化为 qm_agent 事件词汇。 */
    switch (event->event_id) {
        case JOYINSIDE_CLIENT_EVENT_CONNECTED:
            agent_event.id = QM_AGENT_EVENT_CONNECTED;
            joyinside_emit(ctx, &agent_event);
            break;
        case JOYINSIDE_CLIENT_EVENT_DISCONNECTED:
            agent_event.id = QM_AGENT_EVENT_DISCONNECTED;
            joyinside_emit(ctx, &agent_event);
            break;
        case JOYINSIDE_CLIENT_EVENT_AUDIO:
        {
            const uint8_t *chunk = (const uint8_t *)event->payload.audio.chunk.data;
            uint32_t chunk_len = event->payload.audio.chunk.data_len;

            /* The first frame reveals the Opus packet length the server chose.
             * The wire chunk may carry a non-Opus wrapper prefix; a legal Opus
             * packet at the negotiated duration anchors the real payload.
             * 首帧揭示服务端选定的 Opus 包长；线上 chunk 可能带非 Opus 包头，
             * 以“协商时长下的合法 Opus 包”定位真实负载。 */
            if (ctx->output_probe_pending && chunk_len != 0U) {
                uint32_t native_bytes = ctx->output_format.packet_bytes;
                uint32_t head_ms = joyinside_opus_duration_ms(chunk, chunk_len);
                uint32_t tail_ms = (native_bytes != 0U && chunk_len > native_bytes) ?
                    joyinside_opus_duration_ms(chunk + (chunk_len - native_bytes),
                                               native_bytes) : 0U;
                uint32_t offset = 0U;
                uint32_t size = chunk_len;

                if (head_ms != ctx->output_format.frame_ms &&
                    tail_ms == ctx->output_format.frame_ms) {
                    offset = chunk_len - native_bytes;
                    size = native_bytes;
                }
                ctx->output_probe_pending = QM_FALSE;
                ctx->output_offset = offset;
                ctx->output_format.packet_bytes = size;
                QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_DOWNLINK,
                              "downlink opus probed len=%u head_ms=%u tail_ms=%u offset=%u "
                              "packet_bytes=%u frame_ms=%u head=%02x%02x%02x%02x",
                              (unsigned int)chunk_len, (unsigned int)head_ms,
                              (unsigned int)tail_ms, (unsigned int)offset, (unsigned int)size,
                              (unsigned int)ctx->output_format.frame_ms,
                              (unsigned int)chunk[0], chunk_len > 1U ? (unsigned int)chunk[1] : 0U,
                              chunk_len > 2U ? (unsigned int)chunk[2] : 0U,
                              chunk_len > 3U ? (unsigned int)chunk[3] : 0U);
                /* The reply boundary was held back until the wire size is known.
                 * 回复边界一直保留到线上包长确定，此处补发。 */
                agent_event.id = QM_AGENT_EVENT_TTS_INPUT_BEGIN;
                agent_event.data.audio.format = ctx->output_format;
                joyinside_emit(ctx, &agent_event);
                qm_memset(&agent_event.data, 0, sizeof(agent_event.data));
            }
            if (chunk_len <= ctx->output_offset) {
                break;
            }
            agent_event.id = QM_AGENT_EVENT_AUDIO;
            agent_event.data.audio.data = (const uint8_t *)(chunk + ctx->output_offset);
            agent_event.data.audio.len = chunk_len - ctx->output_offset;
            agent_event.data.audio.format = ctx->output_format;
            joyinside_emit(ctx, &agent_event);
            break;
        }
        case JOYINSIDE_CLIENT_EVENT_TEXT:
            if (event->payload.text.text_type == JOYINSIDE_CLIENT_TEXT_TYPE_ASR) {
                /* IS_START and intermediate text are useful for vision and UI,
                 * but must not close the normalized ASR stream. IS_START 与中间
                 * 文本可用于视觉和 UI，不能结束归一化 ASR 流。 */
                agent_event.id = QM_AGENT_EVENT_ASR;
                agent_event.data.asr.text = event->payload.text.content.asr.text;
                agent_event.data.asr.final = event->payload.text.content.asr.text_type &&
                    !qm_strcmp(event->payload.text.content.asr.text_type, "IS_FINAL");
            } else if (event->payload.text.text_type == JOYINSIDE_CLIENT_TEXT_TYPE_AGENT ||
                       event->payload.text.text_type == JOYINSIDE_CLIENT_TEXT_TYPE_ACTIVITY) {
                agent_event.id = QM_AGENT_EVENT_AGENT_TEXT;
                agent_event.data.agent_text.text = event->payload.text.content.agent.content;
                agent_event.data.agent_text.final = joyinside_text_present(
                    event->payload.text.content.agent.finish_reason);
                agent_event.data.agent_text.finish_reason = joyinside_finish_reason(
                    event->payload.text.content.agent.finish_reason);
            } else {
                break;
            }
            joyinside_emit(ctx, &agent_event);
            break;
        case JOYINSIDE_CLIENT_EVENT_SERVER_EVENT:
            if (event->payload.server_event.type == JOYINSIDE_SERVER_EVENT_TYPE_CALL_SKILL &&
                (event->payload.server_event.content.skill.instruction_count ||
                 !joyinside_text_present(event->payload.server_event.content.skill.expression_code))) {
                /* One CALL_SKILL envelope can contain a command and an
                 * expression. Emit the complete command first, then clear the
                 * borrowed union before preparing the optional expression.
                 * 一条 CALL_SKILL 可同时含指令和表情；先派发完整指令，再清空借用
                 * union 后构造可选表情事件。 */
                agent_event.id = QM_AGENT_EVENT_COMMAND;
                agent_event.data.command.domain = event->payload.server_event.content.skill.domain;
                agent_event.data.command.json = event->payload.server_event.chunk.data;
                joyinside_emit(ctx, &agent_event);
                qm_memset(&agent_event.data, 0, sizeof(agent_event.data));
            }
            if (event->payload.server_event.type ==
                JOYINSIDE_SERVER_EVENT_TYPE_CALL_AGENT_START) {
                /* The reply boundary arrives before any audio byte, so an Opus
                 * wire packet length is still unknown. Skip admission here; the
                 * first downlink frame admits the stream with the probed size.
                 * 该回复边界先于任何音频字节到达，此时 Opus 线上包长仍未知；此处不做准入，
                 * 由首个下行帧用探测到的包长准入流。 */
                if (!ctx->output_probe_pending) {
                    agent_event.id = QM_AGENT_EVENT_TTS_INPUT_BEGIN;
                    agent_event.data.audio.format = ctx->output_format;
                    joyinside_emit(ctx, &agent_event);
                }
            } else if (event->payload.server_event.type ==
                JOYINSIDE_SERVER_EVENT_TYPE_CALL_AGENT_INTERRUPTED) {
                agent_event.id = QM_AGENT_EVENT_AGENT_ABORTED;
                joyinside_emit(ctx, &agent_event);
            } else if (event->payload.server_event.type ==
                       JOYINSIDE_SERVER_EVENT_TYPE_TTS_COMPLETE) {
                /* The server finished sending audio. Local queued data and the
                 * DAC tail are still owned by qm_agent's playback worker. */
                agent_event.id = QM_AGENT_EVENT_TTS_INPUT_COMPLETE;
                joyinside_emit(ctx, &agent_event);
            } else if (event->payload.server_event.type ==
                       JOYINSIDE_SERVER_EVENT_TYPE_TTS_SENTENCE_START) {
                agent_event.id = QM_AGENT_EVENT_TTS;
                agent_event.data.tts.text = event->payload.server_event.common.text;
                agent_event.data.tts.final = QM_FALSE;
                joyinside_emit(ctx, &agent_event);
            } else if (event->payload.server_event.type ==
                       JOYINSIDE_SERVER_EVENT_TYPE_CALL_SKILL &&
                       joyinside_text_present(
                           event->payload.server_event.content.skill.expression_code)) {
                agent_event.id = QM_AGENT_EVENT_EXPRESSION;
                agent_event.data.expression.visual = joyinside_visual_expression(
                    event->payload.server_event.content.skill.expression);
                agent_event.data.expression.gesture = joyinside_gesture(
                    event->payload.server_event.content.skill.expression);
                agent_event.data.expression.category = joyinside_expression_category(
                    event->payload.server_event.content.skill.expression);
                agent_event.data.expression.speech_expression =
                    joyinside_visual_expression(
                        event->payload.server_event.content.skill.tts_expression);
                agent_event.data.expression.speech_category =
                    joyinside_expression_category(
                        event->payload.server_event.content.skill.tts_expression);
                agent_event.data.expression.provider_code =
                    event->payload.server_event.content.skill.expression_code;
                agent_event.data.expression.provider_speech_code =
                    event->payload.server_event.content.skill.tts_expression_code;
                joyinside_emit(ctx, &agent_event);
            }
            break;
        case JOYINSIDE_CLIENT_EVENT_ERROR:
            agent_event.id = QM_AGENT_EVENT_ERROR;
            agent_event.code = event->payload.error.code;
            agent_event.data.error.message = "joyinside transport error";
            joyinside_emit(ctx, &agent_event);
            break;
        default:
            break;
    }
}
#endif

/**
 * @brief Create one adapter context and its underlying JoyInside client.
 *
 * @par 中文说明
 * 创建一个适配层上下文及其底层 JoyInside 客户端。
 *
 * Static audio shape is set here; bot ID, UID and access token are session
 * inputs and are supplied by start(). / 静态音频参数在此设置，会话身份在 start 时提供。
 */
static int joyinside_init(void **transport_ctx,
                          const qm_agent_transport_config_t *config,
                          const qm_agent_audio_format_t *input_format,
                          const qm_agent_audio_format_t *output_format,
                          const qm_agent_video_format_t *input_video_format,
                          const qm_agent_video_format_t *output_video_format,
                          qm_agent_transport_event_cb_t event_cb,
                          void *user_ctx)
{
    qm_agent_joyinside_ctx_t *ctx;

    if (transport_ctx == NULL || config == NULL || event_cb == NULL) {
        return -QM_EINVAL;
    }

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    int format_ret = validate_audio(input_format, QM_TRUE);
    if (format_ret == QM_EOK) {
        format_ret = validate_audio(output_format, QM_FALSE);
    }
    if (format_ret != QM_EOK) {
        return format_ret;
    }
#endif

    ctx = (qm_agent_joyinside_ctx_t *)qm_malloc(sizeof(*ctx));
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
#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
        /* One JoyInside binary frame carries exactly one Opus packet, but the
         * server chooses the packet length and it is not negotiated, so the
         * fixed native profile cannot describe the wire size. Probe it from the
         * first downlink frame before admitting a stream.
         * JoyInside 下行每个二进制帧恰好承载一个 Opus 包，但包长由服务端决定且不参与协商，
         * 原生固定 profile 无法描述该长度；因此在准入流之前用首个下行帧探测。 */
        ctx->output_probe_pending =
            joyinside_effective_codec(&ctx->output_format) == QM_AGENT_AUDIO_CODEC_OPUS;
#endif
    }
    if (input_video_format) {
        qm_memcpy(&ctx->input_video_format, input_video_format, sizeof(*input_video_format));
    }
    if (output_video_format) {
        qm_memcpy(&ctx->output_video_format, output_video_format, sizeof(*output_video_format));
    }
    ctx->event_cb = event_cb;
    ctx->user_ctx = user_ctx;

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    joyinside_client_config_t ji_config;
    int codec_ret;

    qm_memset(&ji_config, 0, sizeof(ji_config));
    ji_config.user_context = ctx;
    ji_config.on_event = joyinside_on_event;
    ji_config.network.timeout_ms = config->data.joyinside.timeout_ms;
    ji_config.voice.chat_role_code = config->data.joyinside.chat_role_code;
    ji_config.voice.output_stream = config->data.joyinside.output_stream;
    ji_config.voice.input_sample_rate = ctx->input_format.sample_rate;
    ji_config.voice.output_sample_rate = ctx->output_format.sample_rate;
    codec_ret = joyinside_map_audio_codec(&ctx->input_format,
                                          QM_TRUE,
                                          &ji_config.voice.input_codec);
    if (codec_ret != QM_EOK) {
        qm_free(ctx);
        return codec_ret;
    }
    codec_ret = joyinside_map_audio_codec(&ctx->output_format,
                                          QM_FALSE,
                                          &ji_config.voice.output_codec);
    if (codec_ret != QM_EOK) {
        qm_free(ctx);
        return codec_ret;
    }
    ji_config.voice.output_frame_size_ms = joyinside_output_frame_ms(&ctx->output_format);
    ji_config.voice.input_frame_size =
        ctx->input_format.framing == QM_AGENT_AUDIO_FRAMING_OPUS_CBR ?
        ctx->input_format.packet_bytes : 0U;
    ji_config.voice.output_opus_cbr =
        ctx->output_format.framing == QM_AGENT_AUDIO_FRAMING_OPUS_CBR;

    /* Static voice/audio shape belongs to init; session identity belongs to
     * start. / 静态语音参数在 init 设置，会话身份在 start 设置。 */
    ctx->client = joyinside_client_init(&ji_config);
    if (ctx->client == NULL) {
        qm_free(ctx);
        return -QM_EINIT;
    }
#endif

    *transport_ctx = ctx;
    return QM_EOK;
}

/**
 * @brief Destroy the underlying client only after its workers have stopped.
 *
 * @par 中文说明
 * 仅在底层客户端工作线程停止后销毁它。
 *
 * A failed destroy keeps the context reachable so qm_agent can retry once the
 * asynchronous teardown is complete. / 销毁失败时保留上下文，以便异步退出完成后重试。
 */
static int joyinside_deinit(void *transport_ctx)
{
    qm_agent_joyinside_ctx_t *ctx = (qm_agent_joyinside_ctx_t *)transport_ctx;
    int ret = QM_EOK;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    if (ctx->client) {
        ret = joyinside_client_destroy(ctx->client);
        if (ret != QM_EOK) {
            /* Keep the transport context reachable so qm_agent can retry
             * teardown after a WebSocket worker finishes unwinding. */
            return ret;
        }
        ctx->client = NULL;
    }
#endif
    qm_free(ctx);
    return ret;
}

/**
 * @brief Start the underlying JoyInside session from qm_agent transport fields.
 *
 * @par 中文说明
 * 使用 qm_agent transport 字段启动底层 JoyInside 会话。
 *
 * Session identity, feature flags and the token provider are forwarded intact.
 * 转发会话身份、功能标志和 Token 提供回调。
 */
static int joyinside_start(void *transport_ctx)
{
    qm_agent_joyinside_ctx_t *ctx = (qm_agent_joyinside_ctx_t *)transport_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    joyinside_client_start_params_t params;
    qm_memset(&params, 0, sizeof(params));
    params.bot_id = ctx->config.data.joyinside.bot_id;
    params.access_token = ctx->config.data.joyinside.access_token;
    params.uid = ctx->config.data.joyinside.uid;
    params.session_id = ctx->config.data.joyinside.session_id;
    params.feature = ctx->config.data.joyinside.feature;
    params.client_type = ctx->config.data.joyinside.client_type;
    params.location = ctx->config.data.joyinside.location;
    params.need_asr_partial = ctx->config.data.joyinside.need_asr_partial;
    params.token_provider = ctx->config.data.joyinside.token_provider;
    params.token_context = ctx->config.data.joyinside.token_context;
    /* Per-session identity fields are supplied at start time by this SDK.
     * / 本 SDK 在 start 阶段提供每次会话的身份字段。 */
    return joyinside_client_start(ctx->client, &params);
#else
    return -QM_ENOSYS;
#endif
}

static int joyinside_stop(void *transport_ctx)
{
    qm_agent_joyinside_ctx_t *ctx = (qm_agent_joyinside_ctx_t *)transport_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    return joyinside_client_stop(ctx->client);
#else
    return QM_EOK;
#endif
}

/**
 * @brief Propagate the network gate without destroying the JoyInside session.
 *
 * @par 中文说明
 * 传播网络开关，但不销毁 JoyInside 会话。
 */
static int joyinside_network_update(void *transport_ctx, qm_agent_network_state_t state)
{
    qm_agent_joyinside_ctx_t *ctx = (qm_agent_joyinside_ctx_t *)transport_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    return joyinside_client_network_update(
        ctx->client,
        state == QM_AGENT_NETWORK_UP ? JOYINSIDE_CLIENT_NETWORK_UP : JOYINSIDE_CLIENT_NETWORK_DOWN);
#else
    (void)state;
    return -QM_ENOSYS;
#endif
}

/**
 * @brief Forward one already validated PCM/Opus audio payload.
 *
 * @par 中文说明
 * 转发一帧已校验的 PCM/Opus 音频载荷。
 *
 * QM_EOK means the lower client accepted a bounded copy locally, not that the
 * server received it. / QM_EOK 仅表示底层客户端已本地接收有界副本，不表示服务端已收到。
 */
static int joyinside_send_audio(void *transport_ctx, const void *data, uint32_t len)
{
    qm_agent_joyinside_ctx_t *ctx = (qm_agent_joyinside_ctx_t *)transport_ctx;

    if (ctx == NULL || data == NULL || len == 0) {
        return -QM_EINVAL;
    }

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    return joyinside_client_send_audio(ctx->client, data, len);
#else
    return -QM_ENOSYS;
#endif
}

/**
 * @brief Report that the JoyInside adapter has no video uplink implementation.
 * JoyInside 适配器未实现视频上行，所有视频帧均返回 -QM_ENOSYS。
 */
static int joyinside_send_video(void *transport_ctx,
                                const void *data,
                                uint32_t len,
                                const qm_agent_video_format_t *format)
{
    QM_UNUSED_ARG(transport_ctx);
    QM_UNUSED_ARG(data);
    QM_UNUSED_ARG(len);
    QM_UNUSED_ARG(format);
    return -QM_ENOSYS;
}

/**
 * @brief Map qm_agent user text to JoyInside TEXT input.
 *
 * @par 中文说明
 * 将 qm_agent 用户文本映射为 JoyInside TEXT 输入。
 */
static int joyinside_send_text(void *transport_ctx, const char *text)
{
    qm_agent_joyinside_ctx_t *ctx = (qm_agent_joyinside_ctx_t *)transport_ctx;

    if (ctx == NULL || text == NULL) {
        return -QM_EINVAL;
    }

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    int ret;

    ret = joyinside_client_send_text_input(ctx->client,
                                           text,
                                           ctx->config.data.joyinside.timeout_ms);
    return ret >= QM_EOK ? QM_EOK : ret;
#else
    return -QM_ENOSYS;
#endif
}

/**
 * @brief Map a local qm_agent TTS request to JoyInside synthesis.
 *
 * @par 中文说明
 * 将本地 qm_agent TTS 请求映射为 JoyInside 合成请求。
 */
static int joyinside_request_tts(void *transport_ctx, const char *text)
{
    qm_agent_joyinside_ctx_t *ctx = (qm_agent_joyinside_ctx_t *)transport_ctx;

    if (ctx == NULL || text == NULL) {
        return -QM_EINVAL;
    }

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    int ret;

    ret = joyinside_client_request_tts(ctx->client,
                                       text,
                                       ctx->config.data.joyinside.timeout_ms);
    return ret >= QM_EOK ? QM_EOK : ret;
#else
    return -QM_ENOSYS;
#endif
}

/**
 * @brief Map qm_agent interruption to JoyInside CLIENT_INTERRUPT.
 *
 * @par 中文说明
 * 将 qm_agent 打断映射为 JoyInside CLIENT_INTERRUPT。
 */
static int joyinside_interrupt(void *transport_ctx)
{
    qm_agent_joyinside_ctx_t *ctx = (qm_agent_joyinside_ctx_t *)transport_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

#if QM_AGENT_HAS_JOYINSIDE_TRANSPORT
    int ret;

    ret = joyinside_client_send_interrupt(ctx->client,
                                          ctx->config.data.joyinside.timeout_ms);
    return ret >= QM_EOK ? QM_EOK : ret;
#else
    return -QM_ENOSYS;
#endif
}

const qm_agent_transport_if_t *qm_agent_transport_joyinside(void)
{
    static const qm_agent_transport_if_t transport = {
        .name = "joyinside",
        .init = joyinside_init,
        .deinit = joyinside_deinit,
        .start = joyinside_start,
        .stop = joyinside_stop,
        .network_update = joyinside_network_update,
        .send_audio = joyinside_send_audio,
        .send_video = joyinside_send_video,
        .send_text = joyinside_send_text,
        .request_tts = joyinside_request_tts,
        .interrupt = joyinside_interrupt,
        .asr_start_longform = NULL,
        .asr_stop_longform = NULL,
        .asr_set_realtime_enabled = NULL,
    };

    return &transport;
}
