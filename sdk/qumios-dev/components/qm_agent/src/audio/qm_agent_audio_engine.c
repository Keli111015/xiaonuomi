/**
 * @file qm_agent_audio_engine.c
 * @brief qm_agent bridge to the shared qm_audio Control facade.
 *
 * The Agent owns no media buffer and no playback worker. A downlink stream is
 * represented by one qm_audio PUSH operation; transport callbacks write bytes
 * directly into that operation's bounded ring. Only the Agent control task
 * creates, seals, stops, and releases the operation.
 *
 * qm_agent 与共享 qm_audio Control 外观之间的桥接层。Agent 不拥有媒体缓冲区或播放
 * 线程；传输回调将下行数据写入有界 PUSH 操作，只有控制任务可以创建、封口、停止和释放。
 */

#include "qm_agent_internal.h"

#define LOG_TAG "qm_agent_audio"

#ifndef CONFIG_QM_AGENT_AUDIO_OPERATION_TIMEOUT_MS
#define CONFIG_QM_AGENT_AUDIO_OPERATION_TIMEOUT_MS 1000U
#endif
#ifndef CONFIG_QM_AGENT_AUDIO_ACQUIRE_TIMEOUT_MS
#define CONFIG_QM_AGENT_AUDIO_ACQUIRE_TIMEOUT_MS 1000U
#endif
#ifndef CONFIG_QM_AGENT_AUDIO_RETRY_BASE_MS
#define CONFIG_QM_AGENT_AUDIO_RETRY_BASE_MS 10U
#endif
#ifndef CONFIG_QM_AGENT_AUDIO_RETRY_MAX_MS
#define CONFIG_QM_AGENT_AUDIO_RETRY_MAX_MS 200U
#endif
/* 保留清理的最大连续重试次数: 下层长期拒绝归还操作(如 api_users 未归还)时,
 * 无界重试会让本 Agent 永久停在该操作上并卡死后续播放, 故必须有界放弃。 */
#ifndef CONFIG_QM_AGENT_AUDIO_CLEANUP_MAX_RETRY
#define CONFIG_QM_AGENT_AUDIO_CLEANUP_MAX_RETRY 20U
#endif
/* 下行音频写入 qm_audio 有界 PUSH 环的最长等待(毫秒)。环满属于背压: 有界等待让播放端
 * 排空后仍能写入该帧, 从而把网络突发限速到播放速率; 仅真正停滞超过该时限才丢帧。 */
#ifndef CONFIG_QM_AGENT_AUDIO_ENQUEUE_WAIT_MS
#define CONFIG_QM_AGENT_AUDIO_ENQUEUE_WAIT_MS 200U
#endif

#define QM_AGENT_AUDIO_STOP_REASON_ABORT 1U
#define QM_AGENT_AUDIO_STOP_REASON_SHUTDOWN 2U

static uint32_t pcm_sample_bytes(qm_audio_pcm_sample_t sample)
{
    switch (sample) {
        case QM_AUDIO_PCM_S16: return 2U;
        case QM_AUDIO_PCM_S24: return 3U;
        case QM_AUDIO_PCM_S32: return 4U;
        default: return 0U;
    }
}

static uint16_t pcm_sample_bits(qm_audio_pcm_sample_t sample)
{
    switch (sample) {
        case QM_AUDIO_PCM_S16: return 16U;
        case QM_AUDIO_PCM_S24: return 24U;
        case QM_AUDIO_PCM_S32: return 32U;
        default: return 0U;
    }
}

static qm_bool pcm_format_valid(const qm_audio_pcm_format_t *format)
{
    return (format != NULL && format->sample_rate_hz != 0U &&
            format->channels != 0U && pcm_sample_bytes(format->sample) != 0U) ?
           QM_TRUE : QM_FALSE;
}

static qm_agent_audio_codec_t effective_codec(const qm_agent_audio_format_t *format)
{
    if (format == NULL) return QM_AGENT_AUDIO_CODEC_NONE;
    return format->data_type != QM_AGENT_AUDIO_CODEC_NONE ?
           format->data_type : format->codec;
}

static void fill_pcm_wire_format(qm_agent_audio_format_t *dst,
                                 const qm_audio_pcm_format_t *format)
{
    if (dst == NULL || format == NULL) return;
    qm_memset(dst, 0, sizeof(*dst));
    dst->codec = QM_AGENT_AUDIO_CODEC_PCM;
    dst->data_type = QM_AGENT_AUDIO_CODEC_PCM;
    dst->sample_rate = format->sample_rate_hz;
    dst->channel_num = format->channels;
    dst->bits_per_sample = pcm_sample_bits(format->sample);
    dst->frame_ms = 20U;
}

static int pcm_from_bits(uint16_t bits, qm_audio_pcm_sample_t *sample)
{
    if (sample == NULL) return -QM_EINVAL;
    switch (bits) {
        case 16U: *sample = QM_AUDIO_PCM_S16; return QM_EOK;
        case 24U: *sample = QM_AUDIO_PCM_S24; return QM_EOK;
        case 32U: *sample = QM_AUDIO_PCM_S32; return QM_EOK;
        default: return -QM_EINVAL;
    }
}

static int fill_native_wire_format(qm_agent_audio_format_t *dst,
                                   const qm_audio_stream_format_t *format)
{
    if (dst == NULL || format == NULL || !pcm_format_valid(&format->pcm)) {
        return -QM_EINVAL;
    }
    qm_memset(dst, 0, sizeof(*dst));
    dst->sample_rate = format->pcm.sample_rate_hz;
    dst->channel_num = format->pcm.channels;
    dst->bits_per_sample = pcm_sample_bits(format->pcm.sample);
    if (format->codec == QM_AUDIO_CODEC_PCM) {
        dst->codec = QM_AGENT_AUDIO_CODEC_PCM;
        dst->data_type = QM_AGENT_AUDIO_CODEC_PCM;
        dst->frame_ms = 20U;
        return QM_EOK;
    }
    if (format->codec != QM_AUDIO_CODEC_OPUS || format->packet_duration_ms == 0U ||
        format->packet_duration_ms > UINT16_MAX ||
        format->max_packet_bytes == 0U ||
        format->packet_bytes > format->max_packet_bytes) {
        return -QM_ENOSYS;
    }
    dst->codec = QM_AGENT_AUDIO_CODEC_OPUS;
    dst->data_type = QM_AGENT_AUDIO_CODEC_OPUS;
    dst->frame_ms = (uint16_t)format->packet_duration_ms;
    /* packet_bytes 为 0 表示一包就是一次读写，长度看缓冲区。非 0 仍是定长。 */
    dst->framing = format->packet_bytes == 0U ?
        QM_AGENT_AUDIO_FRAMING_OPUS_PACKET : QM_AGENT_AUDIO_FRAMING_OPUS_CBR;
    dst->packet_bytes = format->packet_bytes;
    return QM_EOK;
}

static int resolve_format_with_fallback(qm_agent_audio_format_t *output,
                                        const qm_agent_audio_format_t *input,
                                        const qm_agent_audio_format_t *fallback)
{
    qm_agent_audio_format_t resolved;

    if (output == NULL || fallback == NULL) return -QM_EINVAL;
    /* codec/data_type NONE denotes an omitted descriptor. Copy the complete
     * native fallback so framing and fixed packet length are not lost on an
     * encoded device. */
    resolved = input != NULL &&
               (input->codec != QM_AGENT_AUDIO_CODEC_NONE ||
                input->data_type != QM_AGENT_AUDIO_CODEC_NONE) ? *input : *fallback;
    if (resolved.codec == QM_AGENT_AUDIO_CODEC_NONE) {
        resolved.codec = resolved.data_type != QM_AGENT_AUDIO_CODEC_NONE ?
                         resolved.data_type : fallback->codec;
    }
    if (resolved.data_type == QM_AGENT_AUDIO_CODEC_NONE) {
        resolved.data_type = resolved.codec;
    }
    if ((unsigned int)resolved.codec > QM_AGENT_AUDIO_CODEC_CUSTOM ||
        (unsigned int)resolved.data_type > QM_AGENT_AUDIO_CODEC_CUSTOM ||
        resolved.codec == QM_AGENT_AUDIO_CODEC_NONE) {
        return -QM_EINVAL;
    }
    if (resolved.sample_rate == 0U) resolved.sample_rate = fallback->sample_rate;
    if (resolved.channel_num == 0U) resolved.channel_num = fallback->channel_num;
    if (resolved.bits_per_sample == 0U) {
        resolved.bits_per_sample = fallback->bits_per_sample;
    }
    if (resolved.frame_ms == 0U) resolved.frame_ms = fallback->frame_ms;
    if (resolved.sample_rate == 0U || resolved.channel_num == 0U ||
        resolved.bits_per_sample == 0U ||
        (unsigned int)resolved.framing > QM_AGENT_AUDIO_FRAMING_OPUS_PACKET ||
        ((resolved.framing == QM_AGENT_AUDIO_FRAMING_OPUS_CBR) !=
         (resolved.packet_bytes != 0U)) ||
        (resolved.framing != QM_AGENT_AUDIO_FRAMING_STREAM &&
         effective_codec(&resolved) != QM_AGENT_AUDIO_CODEC_OPUS)) {
        return -QM_EINVAL;
    }
    *output = resolved;
    return QM_EOK;
}

/**
 * @brief Resolve omitted wire-format fields from the fixed hardware PCM format.
 * 根据固定硬件 PCM 格式补全省略的传输格式字段。
 *
 * This helper validates framing but never silently changes a caller-selected
 * codec. Adapters can therefore reject unsupported encoding before a session
 * starts.
 * 本函数校验分帧方式，但不会静默更改调用方选择的编码；适配器因此可以在会话启动前拒绝不支持的
 * 编码配置。
 */
int qm_agent_audio_engine_resolve_format(qm_agent_audio_format_t *output,
                                         const qm_agent_audio_format_t *input,
                                         const qm_audio_pcm_format_t *hardware)
{
    qm_agent_audio_format_t fallback;

    if (output == NULL || hardware == NULL) return -QM_EINVAL;
    fill_pcm_wire_format(&fallback, hardware);
    return resolve_format_with_fallback(output, input, &fallback);
}

int qm_agent_audio_engine_resolve_stream_format(
    qm_agent_audio_format_t *output,
    const qm_agent_audio_format_t *input,
    const qm_audio_stream_format_t *hardware)
{
    qm_agent_audio_format_t fallback;
    int ret;

    ret = fill_native_wire_format(&fallback, hardware);
    if (ret != QM_EOK) return ret;
    return resolve_format_with_fallback(output, input, &fallback);
}

static qm_bool formats_equal(const qm_agent_audio_format_t *left,
                             const qm_agent_audio_format_t *right)
{
    if (left == NULL || right == NULL) return QM_FALSE;
    return effective_codec(left) == effective_codec(right) &&
           left->sample_rate == right->sample_rate &&
           left->channel_num == right->channel_num &&
           left->bits_per_sample == right->bits_per_sample &&
           left->frame_ms == right->frame_ms &&
           left->framing == right->framing &&
           left->packet_bytes == right->packet_bytes;
}

static qm_bool wire_matches_native(const qm_agent_audio_format_t *wire,
                                   const qm_audio_stream_format_t *native)
{
    qm_agent_audio_format_t expected;

    return fill_native_wire_format(&expected, native) == QM_EOK &&
           formats_equal(wire, &expected);
}

static int build_codec(const qm_agent_audio_format_t *format,
                       qm_audio_codec_config_t *codec_config)
{
    qm_agent_audio_codec_t codec;
    int ret;

    if (format == NULL || codec_config == NULL) return -QM_EINVAL;
    codec = effective_codec(format);
    qm_memset(codec_config, 0, sizeof(*codec_config));
    ret = pcm_from_bits(format->bits_per_sample, &codec_config->pcm.sample);
    if (ret != QM_EOK || format->sample_rate == 0U || format->channel_num == 0U) {
        return -QM_EINVAL;
    }
    codec_config->pcm.sample_rate_hz = format->sample_rate;
    codec_config->pcm.channels = format->channel_num;
    switch (codec) {
        case QM_AGENT_AUDIO_CODEC_PCM: codec_config->codec = QM_AUDIO_CODEC_PCM; break;
        case QM_AGENT_AUDIO_CODEC_OPUS: codec_config->codec = QM_AUDIO_CODEC_OPUS; break;
        case QM_AGENT_AUDIO_CODEC_G711A: codec_config->codec = QM_AUDIO_CODEC_G711A; break;
        case QM_AGENT_AUDIO_CODEC_G711U: codec_config->codec = QM_AUDIO_CODEC_G711U; break;
        case QM_AGENT_AUDIO_CODEC_G722: codec_config->codec = QM_AUDIO_CODEC_G722; break;
        case QM_AGENT_AUDIO_CODEC_MP3: codec_config->codec = QM_AUDIO_CODEC_MP3; break;
        default: return -QM_ENOSYS;
    }
    if (codec == QM_AGENT_AUDIO_CODEC_PCM) {
        return format->framing == QM_AGENT_AUDIO_FRAMING_STREAM &&
               format->packet_bytes == 0U ? QM_EOK : -QM_EINVAL;
    }
    if (codec != QM_AGENT_AUDIO_CODEC_OPUS) {
        return format->framing == QM_AGENT_AUDIO_FRAMING_STREAM &&
               format->packet_bytes == 0U ? QM_EOK : -QM_EINVAL;
    }
    codec_config->packet_duration_ms = format->frame_ms;
    switch (format->framing) {
        case QM_AGENT_AUDIO_FRAMING_STREAM:
            return codec == QM_AGENT_AUDIO_CODEC_OPUS ? -QM_EINVAL : QM_EOK;
        case QM_AGENT_AUDIO_FRAMING_OPUS_OGG:
            codec_config->framing = QM_AUDIO_CODEC_FRAMING_OPUS_OGG;
            break;
        case QM_AGENT_AUDIO_FRAMING_OPUS_RAW_HEADER:
            codec_config->framing = QM_AUDIO_CODEC_FRAMING_OPUS_RAW_HEADER;
            break;
        case QM_AGENT_AUDIO_FRAMING_OPUS_CBR:
            codec_config->framing = QM_AUDIO_CODEC_FRAMING_OPUS_CBR;
            codec_config->packet_bytes = format->packet_bytes;
            break;
        case QM_AGENT_AUDIO_FRAMING_OPUS_PACKET:
            codec_config->framing = QM_AUDIO_CODEC_FRAMING_PACKET;
            break;
        default:
            return -QM_EINVAL;
    }
    return QM_EOK;
}

static uint32_t next_generation(uint32_t generation)
{
    generation++;
    return generation == QM_AUDIO_INVALID_GENERATION ? 1U : generation;
}

static void emit_playback_event(qm_agent_handle_t handle,
                                qm_agent_event_id_t id,
                                uint32_t generation,
                                int code)
{
    qm_agent_event_t event;

    qm_memset(&event, 0, sizeof(event));
    event.id = id;
    event.source = QM_AGENT_EVENT_SOURCE_AUDIO;
    event.code = code;
    event.data.tts_playback.generation = generation;
    qm_agent_emit(handle, &event);
}

/**
 * @brief Stop, wait for, then release one owned Audio operation.
 * 依次停止、等待并释放一个由 Agent 持有的音频操作。
 *
 * Busy or timeout results retain operation ownership for qm_agent_ctl retry;
 * callers must not clear its ID before this helper succeeds.
 * 遇到忙碌或超时时，操作所有权保留给 qm_agent_ctl 重试；本函数成功前调用方不得清除操作 ID。
 */
static int release_operation(qm_agent_handle_t handle,
                             qm_audio_operation_id_t operation,
                             qm_bool stop,
                             uint32_t reason)
{
    int ret;

    if (handle == NULL || operation == QM_AUDIO_INVALID_REQUEST_ID) {
        return QM_EOK;
    }
    if (stop) {
        ret = qm_audio_control_stop(handle->audio.control, operation, reason);
        if (ret != QM_EOK && ret != -QM_EINVAL) return ret;
    }
    ret = qm_audio_control_wait(handle->audio.control,
                                operation,
                                CONFIG_QM_AGENT_AUDIO_OPERATION_TIMEOUT_MS);
    if (ret == -QM_ETIMEOUT || ret == -QM_EBUSY) return ret;
    return qm_audio_control_release(handle->audio.control,
                                    operation,
                                    CONFIG_QM_AGENT_AUDIO_OPERATION_TIMEOUT_MS);
}

/* Caller holds the bridge lock. User intent is never changed by an internal
 * playback, transport, or dialog transition.
 * 调用方持有桥接锁；内部播放、传输或对话状态变化都不能改写用户上传意愿。 */
static void refresh_upload_locked(qm_agent_handle_t handle)
{
    qm_agent_audio_engine_t *audio = &handle->audio;
    qm_bool speaking = audio->dialog_state == QM_AGENT_DIALOG_STATE_SPEAKING ||
                       audio->playback_started;

    audio->upload_enabled = audio->user_upload_enabled && audio->transport_ready &&
        !audio->capture_closing && audio->dialog_state != QM_AGENT_DIALOG_STATE_THINKING &&
        (!speaking || handle->config.transport_capture_policy == QM_AGENT_CAPTURE_POLICY_KEEP);
}

static void schedule_audio(qm_agent_handle_t handle, qm_bool retry)
{
    uint16_t delay = 0U;

    if (retry) {
        (void)backoff_algorithm_get_next_delay(&handle->audio.cleanup_backoff, &delay);
        /* Avoid a zero-jitter busy loop while a callback/worker unwinds. */
        /* 回调或工作线程退出期间避免零抖动导致忙等。 */
        if (delay == 0U) delay = 1U;
    }
    (void)qm_agent_control_schedule(handle, QM_AGENT_CONTROL_AUDIO_RECONCILE,
                                    delay, 0U, 0U);
}

/**
 * @brief Admit one borrowed microphone frame without blocking capture delivery.
 * 无阻塞地准入一个借用的麦克风帧。
 *
 * The shared audio capture callback must remain bounded. It only asks the
 * transport to consume/copy data and leaves retries and lifecycle work to the
 * serialized Agent control task.
 * 共享音频采集回调必须有界执行；它只要求传输消费/复制数据，重试和生命周期操作交由串行的
 * Agent 控制任务处理。
 */
static void on_capture_frame(const qm_audio_data_t *frame, void *user_ctx)
{
    qm_agent_handle_t handle = (qm_agent_handle_t)user_ctx;
    qm_bool allowed = QM_FALSE;

    if (handle == NULL || frame == NULL || frame->data == NULL ||
        frame->size == 0U || !handle->audio.runtime_valid) {
        return;
    }
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    allowed = handle->audio.upload_enabled && handle->audio.transport_ready &&
              !handle->audio.capture_closing;
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (allowed) {
        /* The provider copies the borrowed frame before this callback returns. */
        /* 供应商必须在本回调返回前复制借用的帧数据。 */
        (void)qm_agent_transport_send_audio(handle, frame->data, frame->size);
    }
}

/* Encoded capture is already packetized by the native device. Preserve that
 * boundary at the Agent ingress: one send_audio call corresponds to one
 * complete device packet, while the transport remains responsible for copying
 * the borrowed bytes before this callback returns. */
static void on_capture_packet(const qm_audio_data_t *packet, void *user_ctx)
{
    qm_agent_handle_t handle = (qm_agent_handle_t)user_ctx;
    qm_bool allowed = QM_FALSE;

    if (handle == NULL || packet == NULL || packet->data == NULL ||
        packet->size == 0U || !handle->audio.runtime_valid) {
        return;
    }
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    allowed = handle->audio.upload_enabled && handle->audio.transport_ready &&
              !handle->audio.capture_closing;
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (allowed) {
        (void)qm_agent_transport_send_audio(handle, packet->data, packet->size);
    }
}

/* Control callbacks only retain value facts for the Agent control task.
 * Control 回调只保留值类型事实，后续工作统一由 Agent 控制任务执行。 */
static void on_audio_event(const qm_audio_event_t *event, void *user_ctx)
{
    qm_agent_handle_t handle = (qm_agent_handle_t)user_ctx;
    qm_audio_event_t copy;

    if (handle == NULL || event == NULL || !handle->audio.runtime_valid) return;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    if (!handle->audio.playback_pending &&
        (handle->audio.playback_operation == QM_AUDIO_INVALID_REQUEST_ID ||
         (event->operation_id != handle->audio.playback_operation &&
          event->request_id != handle->audio.playback_operation))) {
        (void)qm_mutex_unlock(&handle->audio.lock);
        return;
    }
    copy = *event;
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (qm_agent_control_post_audio_event(handle, &copy) != QM_EOK) {
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                      "drop audio lifecycle event type=%d op=%u",
                      (int)event->type, (unsigned int)event->operation_id);
    }
}

void qm_agent_audio_engine_set_transport_ready(qm_agent_handle_t handle,
                                                qm_bool ready)
{
    if (handle == NULL || !handle->audio.runtime_valid) return;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.transport_ready = ready;
    refresh_upload_locked(handle);
    (void)qm_mutex_unlock(&handle->audio.lock);
    schedule_audio(handle, QM_FALSE);
}

qm_bool qm_agent_audio_engine_upload_allowed(qm_agent_handle_t handle)
{
    qm_bool allowed = QM_FALSE;

    if (handle == NULL || !handle->audio.runtime_valid) return QM_FALSE;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    allowed = handle->audio.upload_enabled && handle->audio.transport_ready &&
              !handle->audio.capture_closing;
    (void)qm_mutex_unlock(&handle->audio.lock);
    return allowed;
}

qm_bool qm_agent_audio_engine_stream_active(qm_agent_handle_t handle)
{
    qm_bool active = QM_FALSE;

    if (handle == NULL || !handle->audio.runtime_valid) return QM_FALSE;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    active = handle->audio.playback_operation != QM_AUDIO_INVALID_REQUEST_ID ||
             handle->audio.playback_pending;
    (void)qm_mutex_unlock(&handle->audio.lock);
    return active;
}

/**
 * @brief Create the single bounded PUSH operation for a new downlink stream.
 * 为新的下行流创建唯一的有界 PUSH 操作。
 *
 * A stream format becomes immutable after the operation is admitted. The
 * bridge lock keeps first-frame writes, EOS, abort, and operation ownership
 * ordered against each other.
 * 操作准入后流格式即不可变。桥接锁保证首帧写入、EOS、中断和操作所有权之间的顺序。
 */
int qm_agent_audio_engine_begin_stream(qm_agent_handle_t handle,
                                       const qm_agent_audio_format_t *format)
{
    qm_audio_control_play_request_t request;
    qm_audio_codec_config_t codec;
    qm_agent_audio_format_t resolved;
    qm_audio_operation_id_t operation = QM_AUDIO_INVALID_REQUEST_ID;
    qm_audio_event_t pending_event;
    uint32_t generation;
    qm_bool replay_event = QM_FALSE;
    qm_bool finish_pending = QM_FALSE;
    int ret;

    if (handle == NULL || format == NULL || !handle->audio.runtime_valid) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.stream_begin_pending = QM_FALSE;
    (void)qm_mutex_unlock(&handle->audio.lock);
    ret = qm_agent_audio_engine_resolve_stream_format(&resolved, format,
                                                      &handle->audio.output_stream_format);
    if (ret != QM_EOK) return ret;
    ret = build_codec(&resolved, &codec);
    if (ret != QM_EOK) return ret;

    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    if (!handle->audio.stream_accepting ||
        handle->audio.playback_operation != QM_AUDIO_INVALID_REQUEST_ID ||
        handle->audio.playback_pending) {
        (void)qm_mutex_unlock(&handle->audio.lock);
        return -QM_EBUSY;
    }
    generation = next_generation(handle->audio.stream_generation);
    handle->audio.stream_generation = generation;
    handle->audio.remote_abort_pending = QM_FALSE;
    backoff_algorithm_init(&handle->audio.cleanup_backoff,
        CONFIG_QM_AGENT_AUDIO_RETRY_BASE_MS, CONFIG_QM_AGENT_AUDIO_RETRY_MAX_MS,
        BACKOFF_ALGORITHM_RETRY_FOREVER);
    handle->audio.stream_format = resolved;
    handle->audio.stream_format_valid = QM_TRUE;
    handle->audio.stream_finished = QM_FALSE;
    handle->audio.stream_error_pending = QM_FALSE;
    handle->audio.stream_error = QM_EOK;
    handle->audio.playback_pending = QM_TRUE;
    handle->audio.cleanup_stop = QM_FALSE;
    handle->audio.pending_operation = QM_AUDIO_INVALID_REQUEST_ID;
    handle->audio.pending_event_valid = QM_FALSE;
    qm_memset(&pending_event, 0, sizeof(pending_event));
    (void)qm_mutex_unlock(&handle->audio.lock);

    qm_memset(&request, 0, sizeof(request));
    request.priority = handle->audio.playback.priority;
    request.admission = handle->audio.playback.policy;
    request.admission_timeout_ms = handle->audio.playback.acquire_timeout_ms;
    request.source.type = QM_AUDIO_SOURCE_PUSH;
    request.source.value.push.buffer_bytes = 0U;
    request.codec = codec;
    ret = qm_audio_control_play(handle->audio.control, &request, &operation);

    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.playback_pending = QM_FALSE;
    if (ret == QM_EOK && operation != QM_AUDIO_INVALID_REQUEST_ID) {
        handle->audio.playback_operation = operation;
        handle->audio.playback_generation = generation;
        handle->audio.stream_accepting = QM_TRUE;
        finish_pending = handle->audio.stream_finish_pending;
        handle->audio.stream_finish_pending = QM_FALSE;
        if (handle->audio.pending_event_valid &&
            handle->audio.pending_operation == operation) {
            pending_event = handle->audio.pending_event;
            replay_event = QM_TRUE;
        }
        /* A lifecycle event posted during play() is replayed after the
         * operation ID becomes authoritative.
         * play() 内上报的生命周期事件会在操作 ID 确认后重放。 */
        handle->audio.pending_event_valid = QM_FALSE;
        handle->audio.pending_operation = operation;
    } else {
        handle->audio.stream_format_valid = QM_FALSE;
        handle->audio.stream_accepting = QM_TRUE;
        handle->audio.stream_finish_pending = QM_FALSE;
        handle->audio.pending_event_valid = QM_FALSE;
        handle->audio.pending_operation = QM_AUDIO_INVALID_REQUEST_ID;
    }
    /* Seal under the bridge lock so a concurrent writer cannot append bytes
     * after an EOS observed before the first transport frame. The PUSH
     * implementation only records the marker and does not call listeners.
     * 在桥接锁内封口，防止首帧到达前已收到 EOS 后并发写入继续追加字节；PUSH 实现只记录
     * 该标记，不会在这里调用监听器。 */
    if (ret == QM_EOK && operation != QM_AUDIO_INVALID_REQUEST_ID &&
        finish_pending) {
        ret = qm_audio_control_stream_finish(handle->audio.control, operation);
        if (ret == QM_EOK) {
            handle->audio.stream_finished = QM_TRUE;
        }
    }
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (ret != QM_EOK) return ret;
    return replay_event ? qm_agent_audio_engine_handle_event(handle,
                                                              &pending_event) :
                          QM_EOK;
}

/**
 * @brief Queue one coalesced admission request for a spontaneous downlink.
 * 为自发下行流排入一条合并后的准入请求。
 *
 * The copied generation lets qm_agent_ctl discard a request after an abort or
 * replacement stream has moved the boundary.
 * 复制的 generation 使 qm_agent_ctl 能在中断或新流切换边界后丢弃过期请求。
 */
int qm_agent_audio_engine_request_stream_begin(
    qm_agent_handle_t handle,
    const qm_agent_audio_format_t *format)
{
    qm_bool should_post = QM_FALSE;
    uint32_t generation = 0U;
    int ret;

    if (handle == NULL || format == NULL || !handle->audio.runtime_valid) {
        return -QM_EINVAL;
    }

    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    if (handle->audio.playback_operation == QM_AUDIO_INVALID_REQUEST_ID &&
        !handle->audio.playback_pending &&
        !handle->audio.stream_begin_pending &&
        handle->audio.stream_accepting) {
        handle->audio.stream_begin_pending = QM_TRUE;
        generation = handle->audio.stream_generation;
        should_post = QM_TRUE;
    }
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (!should_post) return QM_EOK;

    ret = qm_agent_control_post_audio_begin(handle, format, generation);
    if (ret != QM_EOK) {
        (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
        handle->audio.stream_begin_pending = QM_FALSE;
        (void)qm_mutex_unlock(&handle->audio.lock);
    }
    return ret;
}

/**
 * @brief Copy one borrowed downlink block into the active bounded PUSH stream.
 * 将一个借用的下行数据块复制到活动的有界 PUSH 流。
 *
 * The call uses QM_NO_WAIT and returns pressure to the transport immediately;
 * only a genuine write error escalates to asynchronous terminal cleanup.
 * 本调用使用 QM_NO_WAIT，立即将队列压力返回给传输；只有真正的写入错误才升级为
 * 异步终态清理，背压/暂态信号（环满、流已封口、超时、被打断）原样返回。
 */
int qm_agent_audio_engine_enqueue_transport(qm_agent_handle_t handle,
                                            const void *data,
                                            uint32_t len,
                                            const qm_agent_audio_format_t *format)
{
    qm_audio_operation_id_t operation = QM_AUDIO_INVALID_REQUEST_ID;
    qm_agent_audio_format_t resolved = {0};
    qm_audio_data_t packet = {0};
    qm_bool post_error = QM_FALSE;
    uint32_t generation = QM_AUDIO_INVALID_GENERATION;
    int ret = QM_EOK;

    if (handle == NULL || data == NULL || len == 0U ||
        !handle->audio.runtime_valid) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    operation = handle->audio.playback_operation;
    if (operation == QM_AUDIO_INVALID_REQUEST_ID ||
        !handle->audio.stream_accepting || handle->audio.stream_finished ||
        !handle->audio.stream_format_valid || handle->audio.stream_error_pending) {
        qm_bool first = !handle->audio.stream_error_pending;
        qm_bool accepting = handle->audio.stream_accepting;
        qm_bool finished = handle->audio.stream_finished;
        qm_bool fmt_valid = handle->audio.stream_format_valid;
        (void)qm_mutex_unlock(&handle->audio.lock);
        /* 日志宏在关闭日志的构建(CONFIG_QM_LOG_SUPPORT=0)里展开为空, 显式引用这些
         * 仅用于日志的值, 否则会触发 -Werror,-Wunused-variable。 */
        (void)accepting;
        (void)finished;
        (void)fmt_valid;
        /* 只在本流首次被拒时上报，后续帧已被上面的错误标记短路。 */
        if (first) {
            QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_DOWNLINK,
                          "enqueue gated op=%u accepting=%d finished=%d fmt=%d",
                          (unsigned)operation, (int)accepting, (int)finished, (int)fmt_valid);
        }
        return -QM_EBUSY;
    }
    if (format != NULL) {
        ret = qm_agent_audio_engine_resolve_stream_format(
            &resolved, format, &handle->audio.output_stream_format);
        if (ret != QM_EOK || !formats_equal(&resolved, &handle->audio.stream_format)) {
            (void)qm_mutex_unlock(&handle->audio.lock);
            return -QM_EINVAL;
        }
    }
    /* 写入 qm_audio 的有界 PUSH 环。环满属于背压: 有界等待让播放端排空后该帧仍能写入,
     * 从而把网络突发限速到播放速率(阻塞 WS 接收任务 → TCP 回窗让服务端降速), 仅真正
     * 停滞超过 CONFIG_QM_AGENT_AUDIO_ENQUEUE_WAIT_MS 才丢帧。等待期间保持桥接锁, 避免
     * 中断/释放在校验与写入之间更改操作 ID; 排空只由播放管线任务完成(不取本锁), 故不会
     * 死锁。本回调运行在 WS 接收任务而非 qm_audio 事件任务, 因此允许有界阻塞。 */
    if (handle->audio.stream_format.framing == QM_AGENT_AUDIO_FRAMING_OPUS_PACKET) {
        packet.data = data;
        packet.size = len;
        packet.duration_ms = handle->audio.stream_format.frame_ms;
        packet.flags = QM_AUDIO_DATA_DURATION_VALID;
        ret = qm_audio_control_stream_write_packet(handle->audio.control, operation,
                                                   &packet, CONFIG_QM_AGENT_AUDIO_ENQUEUE_WAIT_MS);
    } else {
        ret = qm_audio_control_stream_write(handle->audio.control, operation,
                                            data, len, CONFIG_QM_AGENT_AUDIO_ENQUEUE_WAIT_MS);
    }
    /* 只有真正的流错误才升级为终态。环满(-QM_EFULL)、流已封口(-QM_EBUSY)、
     * 超时(-QM_ETIMEOUT)、被打断(-QM_EINTR) 都是背压/暂态信号：网络下行一旦
     * 突发就会命中，若把它们当流错误会伪造终态 ERROR 并打断整段播放。
     * 这些值原样返回给传输层丢弃该帧即可，播放可自行恢复。 */
    if (ret != QM_EOK && !handle->audio.stream_error_pending &&
        ret != -QM_EFULL && ret != -QM_EBUSY &&
        ret != -QM_ETIMEOUT && ret != -QM_EINTR) {
        handle->audio.stream_error_pending = QM_TRUE;
        handle->audio.stream_error = ret;
        generation = handle->audio.stream_generation;
        post_error = QM_TRUE;
    }
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (post_error) {
        (void)qm_agent_control_post_audio_error(handle, ret, generation);
    }
    return ret;
}

/**
 * @brief Seal network input while allowing local buffered playback to drain.
 * 封闭网络输入，同时允许本地已缓冲播放继续排空。
 */
int qm_agent_audio_engine_finish_stream_input(qm_agent_handle_t handle)
{
    qm_audio_operation_id_t operation;

    if (handle == NULL || !handle->audio.runtime_valid) return -QM_EINVAL;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    operation = handle->audio.playback_operation;
    if (!handle->audio.stream_accepting && !handle->audio.capture_closing) {
        handle->audio.stream_accepting = QM_TRUE;
        handle->audio.remote_abort_pending = QM_FALSE;
        (void)qm_mutex_unlock(&handle->audio.lock);
        return QM_EOK;
    }
    if (operation == QM_AUDIO_INVALID_REQUEST_ID) {
        handle->audio.stream_finish_pending = handle->audio.stream_begin_pending ||
                                              handle->audio.playback_pending;
        (void)qm_mutex_unlock(&handle->audio.lock);
        return QM_EOK;
    }
    if (handle->audio.stream_finished) {
        (void)qm_mutex_unlock(&handle->audio.lock);
        return QM_EOK;
    }
    /* stream_finish only seals the bounded source and does not invoke an
     * application callback, so keeping the bridge lock across it prevents a
     * concurrent writer from racing the EOS marker.
     * stream_finish 只封闭有界数据源且不会调用应用回调；保持桥接锁可避免并发写入与 EOS
     * 标记竞争。 */
    {
        int ret = qm_audio_control_stream_finish(handle->audio.control, operation);

        if (ret == QM_EOK) handle->audio.stream_finished = QM_TRUE;
        (void)qm_mutex_unlock(&handle->audio.lock);
        return ret;
    }
}

static qm_bool audio_event_terminal(qm_audio_event_type_t type)
{
    return (type == QM_AUDIO_EVENT_FINISHED ||
            type == QM_AUDIO_EVENT_CANCELLED ||
            type == QM_AUDIO_EVENT_PREEMPTED ||
            type == QM_AUDIO_EVENT_ERROR) ? QM_TRUE : QM_FALSE;
}

/**
 * @brief Apply a normalized qm_audio lifecycle event on the Agent path.
 * 在 Agent 路径上处理标准化 qm_audio 生命周期事件。
 *
 * Some audio controls can report STARTED before play() returns its operation
 * ID. The bridge retains one pending event and replays it after admission to
 * preserve a deterministic lifecycle.
 * 部分音频 Control 可能在 play() 返回操作 ID 前报告 STARTED。桥接层保留一个待处理事件，并在
 * 准入后重放它，以保持确定的生命周期顺序。
 */
int qm_agent_audio_engine_handle_event(qm_agent_handle_t handle,
                                       const qm_audio_event_t *event)
{
    qm_audio_operation_id_t operation;
    uint32_t generation;
    qm_bool terminal;

    if (handle == NULL || event == NULL || !handle->audio.runtime_valid) {
        return -QM_EINVAL;
    }
    operation = event->operation_id != QM_AUDIO_INVALID_REQUEST_ID ?
                event->operation_id : event->request_id;
    if (operation == QM_AUDIO_INVALID_REQUEST_ID) return QM_EOK;

    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    if (handle->audio.playback_pending) {
        /* play() may publish an event before returning its operation ID. Keep
         * one event and replay it after admission; a later terminal event for
         * the same observed ID supersedes a non-terminal one.
         * play() 可能在返回操作 ID 前发布事件；先保留一个事件并在准入后重放，同一观察 ID
         * 的后续终态事件会覆盖非终态事件。 */
        if (!handle->audio.pending_event_valid) {
            handle->audio.pending_event = *event;
            handle->audio.pending_operation = operation;
            handle->audio.pending_event_valid = QM_TRUE;
        } else if (handle->audio.pending_operation == operation &&
                   !audio_event_terminal(handle->audio.pending_event.type) &&
                   audio_event_terminal(event->type)) {
            handle->audio.pending_event = *event;
        }
        (void)qm_mutex_unlock(&handle->audio.lock);
        return QM_EOK;
    }
    if (handle->audio.playback_operation != operation) {
        (void)qm_mutex_unlock(&handle->audio.lock);
        return QM_EOK;
    }
    terminal = audio_event_terminal(event->type);
    generation = handle->audio.playback_generation;
    if (event->type == QM_AUDIO_EVENT_STARTED && !handle->audio.cleanup_pending) {
        qm_bool first = !handle->audio.playback_started;
        handle->audio.playback_started = QM_TRUE;
        refresh_upload_locked(handle);
        (void)qm_mutex_unlock(&handle->audio.lock);
        if (first) emit_playback_event(handle, QM_AGENT_EVENT_TTS_PLAYBACK_STARTED,
                                       generation, QM_EOK);
        schedule_audio(handle, QM_FALSE);
        return QM_EOK;
    }
    if (terminal && !handle->audio.cleanup_pending) {
        handle->audio.stream_accepting = QM_FALSE;
        handle->audio.playback_started = QM_FALSE;
        handle->audio.cleanup_pending = QM_TRUE;
        handle->audio.cleanup_notify = QM_TRUE;
        handle->audio.cleanup_event = *event;
        handle->audio.cleanup_retry = 0U;
        handle->audio.remote_abort_pending = event->type != QM_AUDIO_EVENT_FINISHED;
        refresh_upload_locked(handle);
    }
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (terminal) {
        /* 终态事件决定本次播放的收尾方式，并携带下层给出的失败原因。 */
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                      "audio terminal event type=%d result=%d op=%u",
                      (int)event->type, (int)event->result, (unsigned)operation);
    }
    return terminal ? qm_agent_audio_engine_reconcile(handle) : QM_EOK;
}

/* Cleanup is serialized on qm_agent_ctl. Failure retains every field needed
 * to retry, including the result and generation delivered to the TTS owner.
 * 清理由 qm_agent_ctl 串行执行；失败时保留重试所需的全部字段，包括交给 TTS 所有者的结果和
 * generation。 */
static int finish_cleanup(qm_agent_handle_t handle)
{
    qm_audio_operation_id_t operation;
    qm_audio_event_t event;
    uint32_t generation;
    qm_bool notify;
    qm_bool stop;
    int ret;

    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    if (!handle->audio.cleanup_pending) {
        (void)qm_mutex_unlock(&handle->audio.lock);
        return QM_EOK;
    }
    operation = handle->audio.playback_operation;
    generation = handle->audio.playback_generation;
    event = handle->audio.cleanup_event;
    notify = handle->audio.cleanup_notify;
    stop = handle->audio.cleanup_stop;
    (void)qm_mutex_unlock(&handle->audio.lock);

    ret = release_operation(handle, operation, stop, QM_AGENT_AUDIO_STOP_REASON_ABORT);
    if (ret != QM_EOK) {
        uint16_t attempt;
        qm_bool give_up;

        (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
        if (handle->audio.cleanup_retry < CONFIG_QM_AGENT_AUDIO_CLEANUP_MAX_RETRY) {
            handle->audio.cleanup_retry++;
        }
        attempt = handle->audio.cleanup_retry;
        (void)qm_mutex_unlock(&handle->audio.lock);
        give_up = attempt >= CONFIG_QM_AGENT_AUDIO_CLEANUP_MAX_RETRY;
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_CONTROL,
                      "audio cleanup retained op=%u ret=%d attempt=%u give_up=%d",
                      (unsigned)operation, ret, (unsigned)attempt, (int)give_up);
        if (!give_up) {
            schedule_audio(handle, QM_TRUE);
            return ret;
        }
        /* 有界放弃: 下层长时间拒绝归还该操作(如内部 api_users 未归还)时, 继续
         * 重试只会让本 Agent 永久停在旧操作上并卡死后续播放。此处按已收到的
         * 下行终态收尾, 交由上层重建下一次播放。 */
        QM_AGENT_LOGE(LOG_TAG, "audio cleanup gave up op=%u ret=%d, forcing terminal",
                      (unsigned)operation, ret);
    }
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.playback_operation = QM_AUDIO_INVALID_REQUEST_ID;
    handle->audio.pending_operation = QM_AUDIO_INVALID_REQUEST_ID;
    handle->audio.playback_generation = QM_AUDIO_INVALID_GENERATION;
    handle->audio.stream_generation = next_generation(handle->audio.stream_generation);
    handle->audio.stream_begin_pending = QM_FALSE;
    handle->audio.stream_format_valid = QM_FALSE;
    handle->audio.stream_finished = QM_FALSE;
    handle->audio.stream_finish_pending = QM_FALSE;
    handle->audio.stream_error_pending = QM_FALSE;
    handle->audio.playback_started = QM_FALSE;
    handle->audio.cleanup_pending = QM_FALSE;
    handle->audio.cleanup_retry = 0U;
    handle->audio.pending_event_valid = QM_FALSE;
    if (notify && !stop && event.type == QM_AUDIO_EVENT_FINISHED &&
        !handle->audio.capture_closing) handle->audio.stream_accepting = QM_TRUE;
    refresh_upload_locked(handle);
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (notify) {
        emit_playback_event(handle,
            event.type == QM_AUDIO_EVENT_FINISHED ? QM_AGENT_EVENT_TTS_PLAYBACK_COMPLETE :
                                                   QM_AGENT_EVENT_TTS_PLAYBACK_FAILED,
            generation, event.type == QM_AUDIO_EVENT_FINISHED ? QM_EOK :
                        (event.result != QM_EOK ? event.result : -QM_EINTR));
    }
    return QM_EOK;
}

/**
 * @brief Reconcile retained cleanup, remote abort, and capture gating on qm_agent_ctl.
 * 在 qm_agent_ctl 上协调保留清理、远端中断和采集门控。
 *
 * Cleanup fields are retained until the underlying Control release succeeds,
 * so transient failures can be retried without losing the affected generation.
 * 清理字段会保留到下层 Control 成功释放为止，因此瞬时失败可重试且不会丢失受影响 generation。
 */
int qm_agent_audio_engine_reconcile(qm_agent_handle_t handle)
{
    const qm_agent_transport_if_t *transport;
    qm_audio_capture_subscription_t *capture;
    qm_bool enabled;
    qm_bool remote_abort;
    int ret;

    if (handle == NULL || !handle->audio.runtime_valid) return QM_EOK;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    remote_abort = handle->audio.remote_abort_pending && handle->audio.transport_ready &&
                   !handle->audio.capture_closing;
    handle->audio.remote_abort_pending = QM_FALSE;
    (void)qm_mutex_unlock(&handle->audio.lock);
    transport = handle->config.transport_if;
    if (remote_abort && transport != NULL && transport->interrupt != NULL) {
        ret = transport->interrupt(handle->transport_ctx);
        if (ret == -QM_EBUSY || ret == -QM_EFULL || ret == -QM_ETIMEOUT) {
            (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
            handle->audio.remote_abort_pending = QM_TRUE;
            (void)qm_mutex_unlock(&handle->audio.lock);
            schedule_audio(handle, QM_TRUE);
        }
    }
    ret = finish_cleanup(handle);
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    refresh_upload_locked(handle);
    capture = handle->audio.capture_closing ? NULL : handle->audio.capture_sub;
    enabled = handle->audio.upload_enabled;
    (void)qm_mutex_unlock(&handle->audio.lock);
    /* All enable/unsubscribe calls share the control task. In particular a
     * public user toggle cannot enable a subscription already being freed.
     * 所有 enable/unsubscribe 均在控制任务执行，避免公开用户开关重新启用正在释放的订阅。 */
    if (capture != NULL && qm_audio_capture_enable(capture, enabled) != QM_EOK) {
        schedule_audio(handle, QM_TRUE);
    }
    return ret;
}

/**
 * @brief Abort the current downlink stream and invalidate delayed packets.
 * 中断当前下行流，并使延迟到达的数据包失效。
 *
 * wait_for_ack keeps admission closed until a provider interruption boundary;
 * the stream generation prevents old packets from attaching to a replacement.
 * wait_for_ack 会在供应商中断边界前保持准入关闭；stream generation 防止旧包附着到新流。
 */
int qm_agent_audio_engine_abort_stream(qm_agent_handle_t handle, qm_bool wait_for_ack)
{
    if (handle == NULL || !handle->audio.runtime_valid) return -QM_EINVAL;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.stream_generation = next_generation(handle->audio.stream_generation);
    handle->audio.stream_accepting = !wait_for_ack && !handle->audio.capture_closing;
    if (!wait_for_ack) handle->audio.remote_abort_pending = QM_FALSE;
    handle->audio.stream_begin_pending = QM_FALSE;
    handle->audio.stream_finished = QM_FALSE;
    handle->audio.stream_format_valid = QM_FALSE;
    handle->audio.stream_error_pending = QM_FALSE;
    handle->audio.stream_finish_pending = QM_FALSE;
    handle->audio.pending_event_valid = QM_FALSE;
    handle->audio.playback_started = QM_FALSE;
    if (handle->audio.playback_operation != QM_AUDIO_INVALID_REQUEST_ID) {
        if (!handle->audio.cleanup_pending) {
            handle->audio.cleanup_pending = QM_TRUE;
            handle->audio.cleanup_notify = QM_FALSE;
            handle->audio.cleanup_retry = 0U;
        }
        handle->audio.cleanup_stop = QM_TRUE;
    }
    refresh_upload_locked(handle);
    (void)qm_mutex_unlock(&handle->audio.lock);
    schedule_audio(handle, QM_FALSE);
    return finish_cleanup(handle);
}

/**
 * @brief Convert a data-path write error into retained terminal cleanup.
 * 将数据路径写入错误转换为可保留重试的终态清理。
 *
 * A stale generation is intentionally harmless because its stream has already
 * been replaced or cancelled.
 * 过期 generation 被有意忽略，因为对应流已经被替换或取消。
 */
int qm_agent_audio_engine_fail_stream(qm_agent_handle_t handle, int code,
                                      uint32_t generation)
{
    qm_audio_event_t event = {0};

    if (handle == NULL || !handle->audio.runtime_valid) return -QM_EINVAL;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    if (generation != handle->audio.stream_generation || handle->audio.cleanup_pending ||
        handle->audio.playback_operation == QM_AUDIO_INVALID_REQUEST_ID) {
        (void)qm_mutex_unlock(&handle->audio.lock);
        return QM_EOK;
    }
    event.type = QM_AUDIO_EVENT_ERROR;
    event.operation_id = handle->audio.playback_operation;
    event.result = code;
    handle->audio.cleanup_stop = QM_TRUE;
    (void)qm_mutex_unlock(&handle->audio.lock);
    return qm_agent_audio_engine_handle_event(handle, &event);
}

/**
 * @brief Mark a provider reply boundary without discarding a live PUSH stream.
 * 标记供应商回复边界，同时不丢弃仍在运行的 PUSH 流。
 *
 * When old cleanup is pending, later frames retry admission after release;
 * this keeps a new reply from being confused with delayed old audio.
 * 旧流清理尚未完成时，后续帧会在释放后重新请求准入，避免新回复和延迟旧音频混淆。
 */
int qm_agent_audio_engine_begin_reply(qm_agent_handle_t handle,
                                      const qm_agent_audio_format_t *format)
{
    qm_bool live;
    int ret;

    if (handle == NULL || format == NULL || !handle->audio.runtime_valid) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    live = (handle->audio.playback_operation != QM_AUDIO_INVALID_REQUEST_ID ||
            handle->audio.playback_pending) && handle->audio.stream_accepting &&
           !handle->audio.stream_finished && !handle->audio.cleanup_pending;
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (live) return QM_EOK;

    /* Record the new boundary even if the previous operation is still being
     * released. Subsequent frames can request admission once cleanup ends.
     * 即使前一操作仍在释放也要记录新边界；清理结束后，后续帧即可请求准入。 */
    ret = qm_agent_audio_engine_abort_stream(handle, QM_FALSE);
    if (ret != QM_EOK) return ret;
    return qm_agent_audio_engine_begin_stream(handle, format);
}

uint32_t qm_agent_audio_engine_stream_generation(qm_agent_handle_t handle)
{
    uint32_t generation = QM_AUDIO_INVALID_GENERATION;

    if (handle == NULL || !handle->audio.runtime_valid) return generation;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    generation = handle->audio.stream_generation;
    (void)qm_mutex_unlock(&handle->audio.lock);
    return generation;
}

/**
 * @brief Acquire shared audio formats and initialize the per-session bridge.
 * 获取共享音频格式并初始化每会话桥接状态。
 */
int qm_agent_audio_engine_open(qm_agent_handle_t handle)
{
    qm_audio_stream_format_t input_format;
    qm_audio_stream_format_t output_format;
    int ret;

    if (handle == NULL || handle->config.audio.control == NULL) return -QM_EINVAL;
    handle->audio.playback = (qm_agent_audio_playback_config_t){
        .priority = QM_AUDIO_PRIORITY_HIGH,
        .policy = QM_AUDIO_ADMISSION_PREEMPT_LOWER,
        .acquire_timeout_ms = CONFIG_QM_AGENT_AUDIO_ACQUIRE_TIMEOUT_MS,
    };
    if (handle->config.audio.playback != NULL) {
        handle->audio.playback = *handle->config.audio.playback;
    }
    if ((unsigned)handle->audio.playback.priority > QM_AUDIO_PRIORITY_CRITICAL ||
        (unsigned)handle->audio.playback.policy > QM_AUDIO_ADMISSION_PREEMPT_CURRENT ||
        handle->audio.playback.acquire_timeout_ms == 0U ||
        (handle->audio.playback.acquire_timeout_ms >= UINT32_MAX / 2U &&
         handle->audio.playback.acquire_timeout_ms != QM_WAIT_FOREVER)) return -QM_EINVAL;
    handle->config.audio.playback = &handle->audio.playback;
    handle->audio.control = handle->config.audio.control;
    ret = qm_audio_get_output_stream_format(&output_format);
    if (ret != QM_EOK || fill_native_wire_format(&handle->audio.transport_output_format,
                                                  &output_format) != QM_EOK) {
        return ret != QM_EOK ? ret : -QM_EINVAL;
    }
    handle->audio.output_stream_format = output_format;
    handle->audio.output_format = output_format.pcm;
    qm_memset(&input_format, 0, sizeof(input_format));
    ret = qm_audio_get_input_stream_format(&input_format);
    if (ret == QM_EOK) {
        if (fill_native_wire_format(&handle->audio.transport_input_format,
                                    &input_format) != QM_EOK) return -QM_EINVAL;
        handle->audio.input_stream_format = input_format;
        handle->audio.input_format = input_format.pcm;
    } else if (ret == -QM_ENOSYS && !handle->config.audio.capture_enabled) {
        qm_memset(&handle->audio.input_stream_format, 0,
                  sizeof(handle->audio.input_stream_format));
        qm_memset(&handle->audio.input_format, 0, sizeof(handle->audio.input_format));
    } else {
        return ret;
    }
    ret = qm_agent_audio_engine_resolve_stream_format(
        &handle->audio.transport_output_format,
        &handle->config.audio.downlink_format,
        &output_format);
    if (ret != QM_EOK) return ret;
    if (handle->config.audio.capture_enabled) {
        ret = qm_agent_audio_engine_resolve_stream_format(
            &handle->audio.transport_input_format,
            &handle->config.audio.uplink_format,
            &input_format);
        if (ret != QM_EOK || !wire_matches_native(&handle->audio.transport_input_format,
                                                   &input_format)) {
            return ret != QM_EOK ? ret : -QM_ENOSYS;
        }
    }
    ret = qm_mutex_new(&handle->audio.lock);
    if (ret != QM_EOK) return ret;
    handle->audio.lock_valid = QM_TRUE;
    handle->audio.runtime_valid = QM_TRUE;
    handle->audio.user_upload_enabled = QM_TRUE;
    backoff_algorithm_init(&handle->audio.cleanup_backoff,
        CONFIG_QM_AGENT_AUDIO_RETRY_BASE_MS, CONFIG_QM_AGENT_AUDIO_RETRY_MAX_MS,
        BACKOFF_ALGORITHM_RETRY_FOREVER);
    handle->audio.stream_generation = 1U;
    handle->audio.stream_accepting = QM_FALSE;
    handle->audio.playback_operation = QM_AUDIO_INVALID_REQUEST_ID;
    handle->audio.pending_operation = QM_AUDIO_INVALID_REQUEST_ID;
    handle->audio.stream_begin_pending = QM_FALSE;
    return QM_EOK;
}

/**
 * @brief Subscribe to audio lifecycle events and optionally microphone capture.
 * 订阅音频生命周期事件，并按配置订阅麦克风采集。
 */
int qm_agent_audio_engine_start(qm_agent_handle_t handle)
{
    qm_audio_capture_config_t config = {0};
    qm_audio_capture_subscription_t *capture = NULL;
    int ret = QM_EOK;

    if (handle == NULL || !handle->audio.runtime_valid ||
        handle->audio.audio_subscription_valid) {
        return -QM_EBUSY;
    }
    ret = qm_audio_control_subscribe(handle->audio.control, on_audio_event, handle,
                                     &handle->audio.audio_subscription);
    if (ret != QM_EOK) {
        return ret;
    }
    handle->audio.audio_subscription_valid = QM_TRUE;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.stream_accepting = QM_TRUE;
    handle->audio.stream_begin_pending = QM_FALSE;
    handle->audio.capture_closing = QM_FALSE;
    handle->audio.dialog_state = QM_AGENT_DIALOG_STATE_SILENT;
    handle->audio.upload_enabled = QM_FALSE;
    handle->audio.transport_ready = QM_FALSE;
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (!handle->config.audio.capture_enabled) {
        return QM_EOK;
    }

    config.delivery = QM_AUDIO_CAPTURE_DELIVERY_CALLBACK;
    if (handle->audio.input_stream_format.codec == QM_AUDIO_CODEC_PCM) {
        config.frame_cb = on_capture_frame;
    } else {
        config.packet_cb = on_capture_packet;
    }
    config.user_ctx = handle;
    ret = qm_audio_capture_subscribe(qm_audio_get_capture(), &config, &capture);
    if (ret != QM_EOK) {
        (void)qm_audio_control_unsubscribe(handle->audio.control,
                                           handle->audio.audio_subscription);
        handle->audio.audio_subscription = NULL;
        handle->audio.audio_subscription_valid = QM_FALSE;
        return ret;
    }
    handle->audio.capture_sub = capture;
    return QM_EOK;
}

/**
 * @brief Close audio admission, unsubscribe callbacks, and abort active playback.
 * 关闭音频准入、取消回调订阅并中断活动播放。
 */
int qm_agent_audio_engine_stop(qm_agent_handle_t handle)
{
    qm_audio_capture_subscription_t *capture;
    qm_audio_subscription_t subscription;
    int ret;

    if (handle == NULL || !handle->audio.runtime_valid) return -QM_EINVAL;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.capture_closing = QM_TRUE;
    handle->audio.remote_abort_pending = QM_FALSE;
    handle->audio.upload_enabled = QM_FALSE;
    handle->audio.transport_ready = QM_FALSE;
    capture = handle->audio.capture_sub;
    subscription = handle->audio.audio_subscription;
    (void)qm_mutex_unlock(&handle->audio.lock);
    if (capture != NULL) {
        ret = qm_audio_capture_unsubscribe(capture);
        if (ret != QM_EOK) return ret;
        (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
        handle->audio.capture_sub = NULL;
        (void)qm_mutex_unlock(&handle->audio.lock);
    }
    if (subscription != NULL) {
        ret = qm_audio_control_unsubscribe(handle->audio.control, subscription);
        if (ret != QM_EOK) return ret;
        (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
        handle->audio.audio_subscription = NULL;
        handle->audio.audio_subscription_valid = QM_FALSE;
        (void)qm_mutex_unlock(&handle->audio.lock);
    }
    return qm_agent_audio_engine_abort_stream(handle, QM_TRUE);
}

int qm_agent_audio_engine_close(qm_agent_handle_t handle)
{
    int ret;

    if (handle == NULL) return -QM_EINVAL;
    if (!handle->audio.runtime_valid) return QM_EOK;
    ret = qm_agent_audio_engine_stop(handle);
    if (ret != QM_EOK) return ret;
    if (handle->audio.lock_valid) {
        (void)qm_mutex_free(&handle->audio.lock);
        handle->audio.lock_valid = QM_FALSE;
    }
    handle->audio.runtime_valid = QM_FALSE;
    handle->audio.control = NULL;
    return QM_EOK;
}

void qm_agent_set_upload_enabled(qm_agent_handle_t handle, qm_bool enabled)
{
    if (handle == NULL || !handle->audio.runtime_valid) return;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.user_upload_enabled = enabled;
    refresh_upload_locked(handle);
    (void)qm_mutex_unlock(&handle->audio.lock);
    schedule_audio(handle, QM_FALSE);
}

void qm_agent_audio_engine_set_dialog_state(qm_agent_handle_t handle,
                                           qm_agent_dialog_state_t state)
{
    if (handle == NULL || !handle->audio.runtime_valid) return;
    (void)qm_mutex_lock(&handle->audio.lock, QM_WAIT_FOREVER);
    handle->audio.dialog_state = state;
    refresh_upload_locked(handle);
    (void)qm_mutex_unlock(&handle->audio.lock);
    schedule_audio(handle, QM_FALSE);
}
