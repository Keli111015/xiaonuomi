/**
 * @file qm_audio_pipeline.c
 * @brief Source ingestion, passthrough, and codec conversion pipeline. / 输入源读取、透传与编解码转换管线。
 *
 * Finite source-to-fixed-format pipeline. One run owns one source, an optional
 * decoder/encoder pair, and exactly one PCM, byte, or packet sink; Service
 * admission and device scheduling remain outside it.
 */
#include "qm_audio_priv.h"
#include "qm_string.h"
#include "qm_utils_timer.h"

#if CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT
#include "qm_fs.h"
#endif

#if CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT
#include "qm_http_client.h"
#endif

/** @brief Pipeline processing stages used for error attribution. / 用于错误归因的管线处理阶段。 */
enum { PIPELINE_OPEN, PIPELINE_MEDIA, PIPELINE_DECODE, PIPELINE_READ,
       PIPELINE_DELIVER, PIPELINE_FLUSH, PIPELINE_EOS };

/** @brief Private state for one opened built-in HTTP source. / 一个已打开内置 HTTP 输入源的私有状态。 */
#if CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT
typedef struct {
    qm_http_client_handle_t client;   /* persistent client for the opened URL */
    qm_audio_pipeline_t *pipeline;
    char *etag;
    /* Content-Range bookkeeping for a ranged (resume) open. / 范围(续播)打开时的 Content-Range 记录。 */
    uint32_t range_start;             /* first byte of this partial body */
    uint32_t range_total;             /* full representation size, 0 when unknown */
    qm_bool range_valid;              /* a Content-Range header was parsed */
} http_source_ctx_t;
#endif

#ifndef CONFIG_QM_AUDIO_CONTROL_PUSH_BUFFER_BYTES
#define CONFIG_QM_AUDIO_CONTROL_PUSH_BUFFER_BYTES 16384U
#endif

int qm_audio_push_source_create(uint32_t capacity, qm_audio_push_source_t **out_source)
{
    qm_audio_push_source_t *source;
    int ret;

    if (out_source == NULL) return -QM_EINVAL;
    *out_source = NULL;
    if (capacity == 0U) capacity = CONFIG_QM_AUDIO_CONTROL_PUSH_BUFFER_BYTES;
    if (capacity == 0U) return -QM_EINVAL;

    source = (qm_audio_push_source_t *)qm_calloc(1U, sizeof(*source));
    if (source == NULL) return -QM_ENOMEM;
    source->buffer = (uint8_t *)qm_malloc(capacity);
    if (source->buffer == NULL) {
        qm_free(source);
        return -QM_ENOMEM;
    }
    ret = qm_mutex_new(&source->lock);
    if (ret != QM_EOK) goto fail;
    ret = qm_sem_new(&source->data_sem, 0U);
    if (ret != QM_EOK) goto fail;
    ret = qm_sem_new(&source->space_sem, 0U);
    if (ret != QM_EOK) goto fail;
    source->capacity = capacity;
    *out_source = source;
    return QM_EOK;

fail:
    if (qm_sem_is_valid(&source->space_sem)) (void)qm_sem_free(&source->space_sem);
    if (qm_sem_is_valid(&source->data_sem)) (void)qm_sem_free(&source->data_sem);
    if (qm_mutex_is_valid(&source->lock)) (void)qm_mutex_free(&source->lock);
    qm_free(source->buffer);
    qm_free(source);
    return ret;
}

void qm_audio_push_source_cancel(qm_audio_push_source_t *source)
{
    if (source == NULL || !qm_mutex_is_valid(&source->lock)) return;
    (void)qm_mutex_lock(&source->lock, QM_WAIT_FOREVER);
    source->cancelled = QM_TRUE;
    (void)qm_mutex_unlock(&source->lock);
    if (qm_sem_is_valid(&source->data_sem)) (void)qm_sem_signal(&source->data_sem);
    if (qm_sem_is_valid(&source->space_sem)) (void)qm_sem_signal(&source->space_sem);
}

void qm_audio_push_source_interrupt(qm_audio_push_source_t *source)
{
    if (source == NULL || !qm_mutex_is_valid(&source->lock)) return;
    (void)qm_mutex_lock(&source->lock, QM_WAIT_FOREVER);
    if (!source->cancelled) source->interrupted = QM_TRUE;
    (void)qm_mutex_unlock(&source->lock);
    if (qm_sem_is_valid(&source->data_sem)) (void)qm_sem_signal(&source->data_sem);
}

void qm_audio_push_source_destroy(qm_audio_push_source_t *source)
{
    if (source == NULL) return;
    qm_audio_push_source_cancel(source);
    if (qm_sem_is_valid(&source->space_sem)) (void)qm_sem_free(&source->space_sem);
    if (qm_sem_is_valid(&source->data_sem)) (void)qm_sem_free(&source->data_sem);
    if (qm_mutex_is_valid(&source->lock)) (void)qm_mutex_free(&source->lock);
    qm_free(source->buffer);
    qm_free(source);
}

static void ring_put(qm_audio_push_source_t *source, const void *data, uint32_t size)
{
    uint32_t first = source->capacity - source->write_index;
    if (first > size) first = size;
    if (first != 0U) qm_memcpy(source->buffer + source->write_index, data, first);
    if (first < size) qm_memcpy(source->buffer, (const uint8_t *)data + first, size - first);
    source->write_index = (source->write_index + size) % source->capacity;
    source->count += size;
}

static int push_write(qm_audio_push_source_t *source, const void *header, uint32_t header_size, const void *data, uint32_t size, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;

    if (source == NULL || data == NULL || size == 0U ||
        !qm_mutex_is_valid(&source->lock)) {
        return -QM_EINVAL;
    }
    if (header_size > source->capacity || size > source->capacity - header_size)
        return -QM_EFULL;
    if (timeout_ms != QM_WAIT_FOREVER) {
        qm_utils_time_countdown_ms(&deadline, timeout_ms);
    }
    for (;;) {
        uint32_t free_bytes;
        uint32_t left;

        (void)qm_mutex_lock(&source->lock, QM_WAIT_FOREVER);
        if (source->cancelled) {
            (void)qm_mutex_unlock(&source->lock);
            return -QM_EINTR;
        }
        if (source->finished) {
            (void)qm_mutex_unlock(&source->lock);
            return -QM_EBUSY;
        }
        free_bytes = source->capacity - source->count;
        if (free_bytes >= size + header_size) {
            ring_put(source, header, header_size);
            ring_put(source, data, size);
            (void)qm_mutex_unlock(&source->lock);
            if (qm_sem_is_valid(&source->data_sem)) (void)qm_sem_signal(&source->data_sem);
            return QM_EOK;
        }
        (void)qm_mutex_unlock(&source->lock);
        if (timeout_ms == QM_NO_WAIT) return -QM_EFULL;
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (left == 0U || !qm_sem_is_valid(&source->space_sem) ||
            qm_sem_wait(&source->space_sem, left) != QM_EOK) {
            return -QM_ETIMEOUT;
        }
    }
}

int qm_audio_push_source_write(qm_audio_push_source_t *source, const void *data, uint32_t size, uint32_t timeout_ms)
{
    return push_write(source, NULL, 0U, data, size, timeout_ms);
}

int qm_audio_push_source_write_packet(qm_audio_push_source_t *source, const qm_audio_data_t *packet, uint32_t timeout_ms)
{
    qm_audio_packet_header_t header = {0};
    if (packet == NULL) return -QM_EINVAL;
    header.timestamp_ms = packet->timestamp_ms;
    header.size = packet->size;
    header.duration_ms = packet->duration_ms;
    header.sequence = packet->sequence;
    header.flags = packet->flags;
    return push_write(source, &header, sizeof(header), packet->data, packet->size, timeout_ms);
}

int qm_audio_push_source_finish(qm_audio_push_source_t *source)
{
    if (source == NULL || !qm_mutex_is_valid(&source->lock)) return -QM_EINVAL;
    (void)qm_mutex_lock(&source->lock, QM_WAIT_FOREVER);
    if (source->cancelled) {
        (void)qm_mutex_unlock(&source->lock);
        return -QM_EINTR;
    }
    source->finished = QM_TRUE;
    (void)qm_mutex_unlock(&source->lock);
    if (qm_sem_is_valid(&source->data_sem)) (void)qm_sem_signal(&source->data_sem);
    return QM_EOK;
}

int qm_audio_push_source_read(qm_audio_push_source_t *source, void *data, uint32_t capacity, uint32_t timeout_ms, uint32_t *read_size)
{
    qm_utils_time_t deadline;

    if (source == NULL || data == NULL || capacity == 0U ||
        read_size == NULL || !qm_mutex_is_valid(&source->lock)) return -QM_EINVAL;
    *read_size = 0U;
    if (timeout_ms != QM_WAIT_FOREVER) {
        qm_utils_time_countdown_ms(&deadline, timeout_ms);
    }
    for (;;) {
        uint32_t count;
        uint32_t first;
        uint32_t left;

        (void)qm_mutex_lock(&source->lock, QM_WAIT_FOREVER);
        if (source->cancelled) {
            (void)qm_mutex_unlock(&source->lock);
            return -QM_EINTR;
        }
        if (source->interrupted) {
            source->interrupted = QM_FALSE;
            (void)qm_mutex_unlock(&source->lock);
            return -QM_ETIMEOUT;
        }
        count = source->count < capacity ? source->count : capacity;
        if (count != 0U) {
            first = source->capacity - source->read_index;
            if (first > count) first = count;
            qm_memcpy(data, source->buffer + source->read_index, first);
            if (first < count) {
                qm_memcpy((uint8_t *)data + first, source->buffer, count - first);
            }
            source->read_index = (source->read_index + count) % source->capacity;
            source->count -= count;
            (void)qm_mutex_unlock(&source->lock);
            *read_size = count;
            if (qm_sem_is_valid(&source->space_sem))
                (void)qm_sem_signal(&source->space_sem);
            return QM_EOK;
        }
        if (source->finished) {
            (void)qm_mutex_unlock(&source->lock);
            return QM_EOK;
        }
        (void)qm_mutex_unlock(&source->lock);
        if (timeout_ms == QM_NO_WAIT) return -QM_ETIMEOUT;
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (left == 0U || !qm_sem_is_valid(&source->data_sem) ||
            qm_sem_wait(&source->data_sem, left) != QM_EOK) {
            return -QM_ETIMEOUT;
        }
    }
}

static void pipeline_event(qm_audio_pipeline_t *pipeline, qm_audio_event_type_t type, int result)
{
    qm_audio_event_t event;
    qm_audio_event_cb_t callback;
    void *context;

    if (pipeline == NULL || !qm_mutex_is_valid(&pipeline->lock)) {
        return;
    }
    /* Pipeline teardown replaces this sink while the worker may be finishing;
     * take a consistent snapshot before invoking the borrowed callback. */
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    callback = pipeline->event_sink.callback;
    context = pipeline->event_sink.context;
    event.generation = pipeline->generation;
    event.position_ms = type == QM_AUDIO_EVENT_SEEKED ? pipeline->seek_position_ms : 0U;
    (void)qm_mutex_unlock(&pipeline->lock);
    if (callback == NULL) return;
    event.type = type;
    event.request_id = QM_AUDIO_INVALID_REQUEST_ID;
    event.result = (qm_err_t)result;
    event.reason = 0U;
    event.operation_id = QM_AUDIO_INVALID_REQUEST_ID;
    event.timestamp_ms = qm_now_ms();
    qm_memset(&event.wake, 0, sizeof(event.wake));
    callback(&event, context);
}

int qm_audio_pipeline_set_event_sink(qm_audio_pipeline_t *pipeline, qm_audio_event_cb_t callback, void *context)
{
    if (pipeline == NULL || !qm_mutex_is_valid(&pipeline->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    pipeline->event_sink.callback = callback;
    pipeline->event_sink.context = context;
    (void)qm_mutex_unlock(&pipeline->lock);
    return QM_EOK;
}

static int copy_string(char **out, const char *value)
{
    char *copy;

    if (out == NULL || value == NULL) {
        return -QM_EINVAL;
    }
    copy = (char *)qm_malloc((unsigned int)(qm_strlen(value) + 1U));
    if (copy == NULL) {
        return -QM_ENOMEM;
    }
    qm_strcpy(copy, value);
    *out = copy;
    return QM_EOK;
}

/* Processing contexts and packet assembly belong to one run generation. Closing
 * them never flushes cancelled input, so seek/restart cannot inherit old media. */
static int release_processing(qm_audio_pipeline_t *pipeline)
{
#if CONFIG_QM_AUDIO_ENCODER_SUPPORT
    int ret;
#endif

    if (pipeline == NULL) return -QM_EINVAL;
#if CONFIG_QM_AUDIO_ENCODER_SUPPORT
    if (pipeline->encoder_ctx != NULL) {
        ret = qm_audio_normalize_result(qm_audio_encoder_close(pipeline->encoder_ctx));
        if (ret != QM_EOK) return ret;
        pipeline->encoder_ctx = NULL;
    }
#endif
    qm_free(pipeline->packet_buffer);
    pipeline->packet_buffer = NULL;
    pipeline->packet_capacity = 0U;
    pipeline->packet_used = 0U;
    pipeline->packet_expected = 0U;
    pipeline->packet_time_ms = 0U;
    pipeline->packet_duration_total_ms = 0U;
    qm_memset(&pipeline->packet_meta, 0, sizeof(pipeline->packet_meta));
    pipeline->packet_duration_known = QM_TRUE;
    return QM_EOK;
}

static int create_processing(qm_audio_pipeline_t *pipeline);

static int pipeline_free(qm_audio_pipeline_t *pipeline)
{
    int ret;

    if (pipeline == NULL) {
        return -QM_EINVAL;
    }
    /* Provider close failures retain their context for a retry.  Do this
     * before invalidating the Pipeline lock/handle; destroy must leave the
     * object usable when cleanup is not yet complete. */
    ret = release_processing(pipeline);
    if (ret != QM_EOK) {
        return ret;
    }
    if (qm_sem_is_valid(&pipeline->state_sem)) {
        (void)qm_sem_free(&pipeline->state_sem);
    }
    if (qm_sem_is_valid(&pipeline->wake_sem)) {
        (void)qm_sem_free(&pipeline->wake_sem);
    }
    if (qm_sem_is_valid(&pipeline->capture_sem)) {
        (void)qm_sem_free(&pipeline->capture_sem);
    }
    if (qm_mutex_is_valid(&pipeline->lock)) {
        (void)qm_mutex_free(&pipeline->lock);
    }
    if (pipeline->uri != NULL) {
        qm_free(pipeline->uri);
    }
    if (pipeline->file_path != NULL) {
        qm_free(pipeline->file_path);
    }
    qm_free(pipeline->http_etag);
    qm_free(pipeline->chunk);
    qm_free(pipeline);
    return QM_EOK;
}

static qm_bool cancelled(qm_audio_pipeline_t *pipeline)
{
    qm_bool value;

    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    value = pipeline->cancel_requested;
    (void)qm_mutex_unlock(&pipeline->lock);
    return value;
}

/**
 * @brief Check whether execution is on this Pipeline's source worker. / 检查当前是否在该 Pipeline 的输入源任务中执行。
 *
 * Only the source worker may acknowledge a pooled session's pause. A codec
 * callback can wait independently, but cannot claim the source has quiesced. */
#define PIPELINE_YIELD 1

/** @brief Caller holds `pipeline->lock`. / 调用方持有 `pipeline->lock`。 */
static qm_bool on_source_worker(qm_audio_pipeline_t *pipeline)
{
    return pipeline->execution_task != NULL &&
           qm_task_is_current(pipeline->execution_task);
}

/** @brief Check the emission gate while holding Pipeline lock. / 持有 Pipeline 锁时检查输出门控。 */
static qm_bool output_closed(qm_audio_pipeline_t *pipeline)
{
    return pipeline->cancel_requested ||
           pipeline->state == QM_AUDIO_PIPELINE_FINISHED ||
           pipeline->state == QM_AUDIO_PIPELINE_CANCELLED ||
           pipeline->state == QM_AUDIO_PIPELINE_ERROR;
}

static int wait_if_paused(qm_audio_pipeline_t *pipeline)
{
    for (;;) {
        int ret;
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        if (output_closed(pipeline)) {
            (void)qm_mutex_unlock(&pipeline->lock);
            return -QM_EINTR;
        }
        /* A synchronous decoder emission already belongs to the current push.
         * Finish that bounded call; acknowledge pause at the next checkpoint. */
        if (on_source_worker(pipeline)) {
            (void)qm_mutex_unlock(&pipeline->lock);
            return QM_EOK;
        }
        if (!pipeline->paused) {
            (void)qm_mutex_unlock(&pipeline->lock);
            return QM_EOK;
        }
        pipeline->pause_waiters++;
        (void)qm_mutex_unlock(&pipeline->lock);
        ret = qm_sem_wait(&pipeline->wake_sem, QM_WAIT_FOREVER);
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        pipeline->pause_waiters--;
        (void)qm_mutex_unlock(&pipeline->lock);
        if (ret != QM_EOK) return -QM_EINTR;
    }
}

/** @brief Yield execution without closing source or codec state. / 让出执行权，但不关闭输入源或编解码状态。 */
static int worker_checkpoint(qm_audio_pipeline_t *pipeline)
{
    qm_bool notify;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->cancel_requested) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EINTR;
    }
    if (pipeline->paused) {
        notify = !pipeline->pause_notified;
        pipeline->pause_notified = QM_TRUE;
        pipeline->state = QM_AUDIO_PIPELINE_PAUSED;
        (void)qm_mutex_unlock(&pipeline->lock);
        if (notify) {
            /* The executor publishes quiescence after this call returns.
             * 执行器在本次调用返回后才发布源任务已静止的事实。 */
            (void)qm_sem_signal(&pipeline->state_sem);
            pipeline_event(pipeline, QM_AUDIO_EVENT_PAUSED, QM_EOK);
        }
        return PIPELINE_YIELD;
    }
    notify = pipeline->resume_pending;
    pipeline->resume_pending = QM_FALSE;
    (void)qm_mutex_unlock(&pipeline->lock);
    if (notify) pipeline_event(pipeline, QM_AUDIO_EVENT_RESUMED, QM_EOK);
    return QM_EOK;
}

/** @brief Wake all pause waiters while holding the Pipeline lock. / 持有 Pipeline 锁时唤醒全部暂停等待者。 */
static void wake_paused(qm_audio_pipeline_t *pipeline)
{
    uint32_t i;
    for (i = 0U; i < pipeline->pause_waiters; ++i) {
        (void)qm_sem_signal(&pipeline->wake_sem);
    }
}

/** @brief Publish output quiescence to observers. / 向观察者发布输出已静止的事实。 */
static void finish_emit(qm_audio_pipeline_t *pipeline)
{
    qm_audio_t *audio = NULL;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    pipeline->emit_inflight--;
    if (pipeline->emit_inflight == 0U &&
        (pipeline->paused || pipeline->cancel_requested)) audio = pipeline->executor;
    (void)qm_sem_signal(&pipeline->state_sem);
    (void)qm_mutex_unlock(&pipeline->lock);
    if (audio != NULL) qm_audio_event_wake(audio);
}

/**
 * @brief Gate decoded PCM behind the pause and cancellation rules. / 使解码 PCM 遵从暂停与取消规则。
 *
 * A decoder may emit from its own worker. Keep its PCM behind the same pause
 * gate and count in-flight writes before acknowledging output quiescence. */
static int emit_bytes(qm_audio_pipeline_t *pipeline, const void *data, uint32_t size)
{
    qm_audio_data_t block = {
        .data = data,
        .size = size,
        .flags = QM_AUDIO_DATA_NONE,
    };
    int ret = wait_if_paused(pipeline);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (output_closed(pipeline)) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EINTR;
    }
    pipeline->emit_inflight++;
    (void)qm_mutex_unlock(&pipeline->lock);
    ret = pipeline->config.write_bytes(&block, pipeline->config.user_ctx);
    finish_emit(pipeline);
    return qm_audio_normalize_result(ret);
}

static int emit_packet(const qm_audio_data_t *packet, void *context)
{
    qm_audio_pipeline_t *pipeline = context;
    int ret;
    if (!qm_audio_packet_valid(&pipeline->config.output_format, packet)) return -QM_EINVAL;
    ret = wait_if_paused(pipeline);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (output_closed(pipeline)) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EINTR;
    }
    pipeline->emit_inflight++;
    (void)qm_mutex_unlock(&pipeline->lock);
    ret = pipeline->config.write_packet(packet, pipeline->config.user_ctx);
    finish_emit(pipeline);
    return qm_audio_normalize_result(ret);
}

static int emit_decoded(const qm_audio_data_t *buffer, void *context)
{
    qm_audio_pipeline_t *pipeline = context;
    qm_audio_data_t output;
    uint32_t frame_bytes = qm_audio_pcm_frame_bytes(&pipeline->config.output_format.pcm);
    uint32_t frames, skipped;
    qm_bool seeked = QM_FALSE;
    int ret;

    if (buffer == NULL || buffer->size % frame_bytes != 0U ||
        (buffer->size != 0U && buffer->data == NULL)) return -QM_EINVAL;
    ret = wait_if_paused(pipeline);
    if (ret != QM_EOK) return ret;
    output = *buffer;
    frames = buffer->size / frame_bytes;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (output_closed(pipeline)) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EINTR;
    }
    skipped = pipeline->skip_frames < frames ? (uint32_t)pipeline->skip_frames : frames;
    pipeline->skip_frames -= skipped;
    pipeline->decoded_frames += frames;
    output.size -= skipped * frame_bytes;
    if (output.data != NULL) output.data = (const uint8_t *)output.data + skipped * frame_bytes;
    if ((buffer->flags & QM_AUDIO_DATA_END_OF_STREAM) != 0U) {
        /* Only decoded EOF supplies a measured duration, including discarded
         * seek preroll. Reading the last compressed byte is not this boundary. */
        pipeline->duration_ms = qm_audio_frames_to_ms(pipeline->decoded_frames,
            pipeline->config.output_format.pcm.sample_rate_hz);
        pipeline->progress_flags |= QM_AUDIO_PROGRESS_DURATION_VALID;
        pipeline->progress_flags &= ~QM_AUDIO_PROGRESS_DURATION_ESTIMATED;
    }
    if ((pipeline->progress_flags & QM_AUDIO_PROGRESS_SEEKING) != 0U &&
        (output.size != 0U || (buffer->flags & QM_AUDIO_DATA_END_OF_STREAM) != 0U)) {
        pipeline->seek_position_ms = qm_audio_frames_to_ms(
            pipeline->decoded_frames - output.size / frame_bytes,
            pipeline->config.output_format.pcm.sample_rate_hz);
        pipeline->progress_flags &= ~QM_AUDIO_PROGRESS_SEEKING;
        seeked = QM_TRUE;
    }
    (void)qm_mutex_unlock(&pipeline->lock);
    if (seeked) pipeline_event(pipeline, QM_AUDIO_EVENT_SEEKED, QM_EOK);
    if (output.size == 0U && output.flags == QM_AUDIO_DATA_NONE) return QM_EOK;

    for (;;) {
        ret = wait_if_paused(pipeline);
        if (ret != QM_EOK) return ret;
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        if (output_closed(pipeline) ||
            (pipeline->paused && !on_source_worker(pipeline))) {
            (void)qm_mutex_unlock(&pipeline->lock);
            continue;
        }
        pipeline->emit_inflight++;
        (void)qm_mutex_unlock(&pipeline->lock);
#if CONFIG_QM_AUDIO_ENCODER_SUPPORT
        if (pipeline->encoder_ctx != NULL)
            ret = qm_audio_encoder_push(pipeline->encoder_ctx, &output);
        else
#endif
            ret = pipeline->config.write_pcm(&output, pipeline->config.user_ctx);
        finish_emit(pipeline);
        return ret;
    }
}

/** @brief Exclude paused wall time from decoder I/O budget. / 暂停期间的墙钟时间不消耗解码器 I/O 预算。 */
static int decoder_wait_ready(qm_audio_pipeline_t *pipeline, uint32_t *generation, qm_utils_time_t *deadline, uint32_t timeout_ms)
{
    uint32_t current;
    int ret = worker_checkpoint(pipeline);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    current = pipeline->pause_generation;
    (void)qm_mutex_unlock(&pipeline->lock);
    if (*generation != current && timeout_ms != QM_WAIT_FOREVER) {
        qm_utils_time_countdown_ms(deadline, timeout_ms);
    }
    *generation = current;
    return QM_EOK;
}

#if CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT
static qm_bool http_interrupted(void *user)
{
    http_source_ctx_t *ctx = user;
    return cancelled(ctx->pipeline);
}

/**
 * @brief Parse "bytes <start>-<end>/<total>" from a Content-Range header. / 解析 Content-Range 头的 "bytes <start>-<end>/<total>"。
 *
 * Only the fields a resumed open needs are extracted: the first byte of the
 * partial body and the full representation size. A wildcard total leaves the
 * total unknown. / 仅提取续播打开所需的字段：分片首字节与完整表示大小。通配 total 时
 * 总大小未知。
 */
static int http_parse_content_range(const char *value, uint32_t *start, uint32_t *total)
{
    const char *prefix = "bytes ";
    const char *p;
    const char *slash;
    uint32_t parsed_start = 0U;
    uint32_t parsed_total = 0U;
    qm_bool have_start = QM_FALSE;

    if (value == NULL || start == NULL || total == NULL) {
        return -QM_EINVAL;
    }
    if (qm_strncmp(value, prefix, (int)qm_strlen(prefix)) != 0) {
        return -QM_EINVAL;
    }
    p = value + qm_strlen(prefix);
    slash = qm_strchr(p, '/');
    if (slash == NULL) {
        return -QM_EINVAL;
    }
    /* Parse the first byte of the range up to '-'. */
    while (p < slash && *p >= '0' && *p <= '9') {
        if (parsed_start > (UINT32_MAX - (uint32_t)(*p - '0')) / 10U) {
            return -QM_EINVAL;
        }
        parsed_start = parsed_start * 10U + (uint32_t)(*p - '0');
        have_start = QM_TRUE;
        ++p;
    }
    if (!have_start) {
        return -QM_EINVAL;
    }
    if (slash[1] != '*' && slash[1] != '\0') {
        const char *t = slash + 1;

        while (*t >= '0' && *t <= '9') {
            if (parsed_total > (UINT32_MAX - (uint32_t)(*t - '0')) / 10U) {
                return -QM_EINVAL;
            }
            parsed_total = parsed_total * 10U + (uint32_t)(*t - '0');
            ++t;
        }
    }
    *start = parsed_start;
    *total = parsed_total;
    return QM_EOK;
}

/**
 * @brief Track a strong ETag and discard it across redirects. / 跟踪强 ETag，并在重定向时丢弃它。
 *
 * A strong validator permits byte-for-byte URI replay after a seek; weak ETags
 * do not establish that identity. 强校验器允许 seek 后逐字节重放 URI；弱 ETag
 * 无法建立这种一致性。
 */
static int http_header(const qm_http_client_event_t *event)
{
    http_source_ctx_t *ctx = event->user;
    if (event->event_id == QM_HTTP_CLIENT_EVENT_REDIRECT) {
        qm_free(ctx->etag);
        ctx->etag = NULL;
        ctx->range_valid = QM_FALSE;
    } else if (event->event_id == QM_HTTP_CLIENT_EVENT_HEADER &&
               event->header_name != NULL && event->header_value != NULL &&
               qm_strcasecmp(event->header_name, "ETag") == 0) {
        qm_free(ctx->etag);
        ctx->etag = NULL;
        /** @brief Reject weak validators for replay identity. / 拒绝弱校验器作为重放一致性依据。 */
        if (event->header_value[0] == '"') return copy_string(&ctx->etag, event->header_value);
    } else if (event->event_id == QM_HTTP_CLIENT_EVENT_HEADER &&
               event->header_name != NULL && event->header_value != NULL &&
               qm_strcasecmp(event->header_name, "Content-Range") == 0) {
        /* A resumed open must confirm where the body starts and how large the
         * full representation is. / 续播打开必须确认分片起点与完整表示大小。 */
        ctx->range_valid = QM_FALSE;
        if (http_parse_content_range(event->header_value, &ctx->range_start,
                                     &ctx->range_total) == QM_EOK) {
            ctx->range_valid = QM_TRUE;
        }
    }
    return QM_EOK;
}

/** @brief Format a "bytes=<start>-" Range request value. / 生成 "bytes=<start>-" 形式的 Range 请求值。 */
static int http_format_range_value(char *out, uint32_t capacity, uint32_t start)
{
    char digits[10];
    int count = 0;
    int len = 0;
    uint32_t value = start;

    do {
        digits[count++] = (char)('0' + (value % 10U));
        value /= 10U;
    } while (value != 0U);
    /* "bytes=" (6) + digits + '-' + NUL */
    if (capacity < (uint32_t)(6 + count + 2)) {
        return -QM_EINVAL;
    }
    qm_memcpy(out, "bytes=", 6U);
    len = 6;
    while (count > 0) {
        out[len++] = digits[--count];
    }
    out[len++] = '-';
    out[len] = '\0';
    return QM_EOK;
}

static int http_source_open(qm_audio_pipeline_t *pipeline)
{
    qm_http_client_config_t config = {0};
    qm_http_header_t headers[3];
    http_source_ctx_t *ctx;
    uint32_t expected_size = pipeline->config.source.value.uri.expected_size;
    uint32_t range_start = 0U;
    char range_value[24];
    int header_count = 0;
    int status;
    int ret;

    if (pipeline->resume_valid && pipeline->resume_point.offset != 0U) {
        /* 压缩流在断点前回退一段容器上下文供 SDK 断点恢复; 裸 PCM 精确从断点续抓。 */
        if (pipeline->needs_decoder) {
            range_start = pipeline->resume_point.offset > QM_AUDIO_RESUME_BACKOFF_BYTES ?
                          pipeline->resume_point.offset - QM_AUDIO_RESUME_BACKOFF_BYTES : 0U;
        } else {
            range_start = pipeline->resume_point.offset;
        }
    }

    ctx = qm_calloc(1U, sizeof(*ctx));
    if (ctx == NULL) return -QM_ENOMEM;
    ctx->pipeline = pipeline;
    config.method = QM_HTTP_GET;
    config.url = pipeline->config.source.value.uri.url;
    config.timeout_ms = CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS;
    /* Static resources carry an exact identity body size.  Unknown URI callers
     * keep streaming compatibility without inheriting the HTTP small-body default. */
    config.max_body_len = expected_size != 0U ? expected_size : UINT32_MAX;
    config.on_event = http_header;
    config.interrupted = http_interrupted;
    config.user = ctx;
    headers[header_count++] = (qm_http_header_t){"Accept-Encoding", "identity"};
    if (pipeline->http_etag != NULL) {
        headers[header_count++] = (qm_http_header_t){"If-Match", pipeline->http_etag};
    }
    if (range_start != 0U &&
        http_format_range_value(range_value, sizeof(range_value), range_start) == QM_EOK) {
        headers[header_count++] = (qm_http_header_t){"Range", range_value};
    } else {
        /* No usable Range value: fall through to a full fetch. */
        range_start = 0U;
    }
    config.headers = headers;
    config.header_count = header_count;
    ctx->client = qm_http_client_init(&config);
    if (ctx->client == NULL) { ret = -QM_ENOMEM; goto fail; }
    ret = qm_http_client_open(ctx->client, 0);
    if (ret != QM_EOK) goto fail;
    ret = qm_http_client_fetch_headers(ctx->client);
    if (ret == -QM_EFULL && expected_size != 0U) ret = -QM_EIO;
    if (ret != QM_EOK) goto fail;
    status = qm_http_client_get_status_code(ctx->client);

    if (range_start != 0U) {
        /* A resumed fetch is valid only when the server honored the range for
         * the requested start and the reported full size is known and consistent.
         * A wildcard total (0) cannot establish the representation size that a
         * resumed session anchors its decoder input on, so it refetches too. */
        if (status == 206 && ctx->range_valid && ctx->range_total != 0U &&
            ctx->range_start == range_start &&
            (expected_size == 0U || ctx->range_total == expected_size)) {
            pipeline->http_length = ctx->range_total;
            if (ctx->etag != NULL) {
                if (pipeline->http_etag == NULL) {
                    ret = copy_string(&pipeline->http_etag, ctx->etag);
                    if (ret != QM_EOK) goto fail;
                }
                (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
                pipeline->progress_flags |= QM_AUDIO_PROGRESS_SEEKABLE;
                (void)qm_mutex_unlock(&pipeline->lock);
            }
            pipeline->source_ctx = ctx;
            pipeline->source_open = QM_TRUE;
            return QM_EOK;
        }
        /* Range unsupported or rejected: refetch the whole body from byte zero
         * in the same run instead of failing playback. */
        if (qm_http_client_cleanup(ctx->client) != QM_EOK) {
            pipeline->source_ctx = ctx;
            pipeline->source_open = QM_TRUE;
            return -QM_EIO;
        }
        qm_free(ctx->etag);
        qm_free(ctx);
        pipeline->resume_valid = QM_FALSE;
        pipeline->resume_point.offset = 0U;
        return http_source_open(pipeline);
    }

    /* This path asks for a complete representation, never an unvalidated
     * partial response. Restart-and-decode does not require Range support. */
    if (status != 200) { ret = -QM_EIO; goto fail; }
    if (expected_size != 0U &&
        (!qm_http_client_has_content_length(ctx->client) ||
         qm_http_client_get_content_length(ctx->client) != expected_size)) {
        ret = -QM_EIO;
        goto fail;
    }
    if (pipeline->http_etag != NULL &&
        (ctx->etag == NULL || qm_strcmp(pipeline->http_etag, ctx->etag) != 0 ||
         !qm_http_client_has_content_length(ctx->client) ||
         pipeline->http_length != qm_http_client_get_content_length(ctx->client))) {
        ret = -QM_EIO;
        goto fail;
    }
    if (ctx->etag != NULL && qm_http_client_has_content_length(ctx->client)) {
        if (pipeline->http_etag == NULL) {
            ret = copy_string(&pipeline->http_etag, ctx->etag);
            if (ret != QM_EOK) goto fail;
            pipeline->http_length = qm_http_client_get_content_length(ctx->client);
        }
        /* A native packet route can restart only at byte zero.  It has no
         * packet-index seek implementation, so advertising generic URI replay
         * as seekable would replay a packet stream from the wrong timeline. */
        if (!pipeline->packet_passthrough && !pipeline->byte_passthrough) {
            (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
            pipeline->progress_flags |= QM_AUDIO_PROGRESS_SEEKABLE;
            (void)qm_mutex_unlock(&pipeline->lock);
        }
    }
    pipeline->source_ctx = ctx;
    pipeline->source_open = QM_TRUE;
    return QM_EOK;
fail:
    if (ctx->client != NULL && qm_http_client_cleanup(ctx->client) != QM_EOK) {
        /* Keep callback context alive until the normal close/retry path has
         * actually released the client, including a failed open. */
        pipeline->source_ctx = ctx;
        pipeline->source_open = QM_TRUE;
        return ret;
    }
    qm_free(ctx->etag);
    qm_free(ctx);
    return ret;
}
#endif

/**
 * @brief Open the configured source and retain its read cursor. / 打开配置的输入源并保存读取游标。
 *
 * Dispatching on
 * MEMORY, PUSH, CUSTOM, FILE, or URI. FILE and URI are opened in-process through
 * qm_fs and the built-in HTTP client; they no longer need a product source
 * backend. */
static int source_open(qm_audio_pipeline_t *pipeline)
{
    const qm_audio_source_t *source = &pipeline->config.source;
    int ret;

    switch (source->type) {
        case QM_AUDIO_SOURCE_MEMORY:
            pipeline->source_ctx = (void *)source->value.memory.data;
            return QM_EOK;
        case QM_AUDIO_SOURCE_PUSH:
            if (source->value.push.context == NULL) return -QM_EINVAL;
            pipeline->source_ctx = source->value.push.context;
            pipeline->source_open = QM_TRUE;
            return QM_EOK;
        case QM_AUDIO_SOURCE_CUSTOM:
            if (source->value.custom.open == NULL ||
                source->value.custom.read == NULL ||
                source->value.custom.cancel == NULL ||
                source->value.custom.close == NULL) {
                return -QM_EINVAL;
            }
            ret = qm_audio_normalize_result(source->value.custom.open(
                source->value.custom.ctx, CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS));
            if (ret != QM_EOK) {
                return ret;
            }
            pipeline->source_ctx = source->value.custom.ctx;
            pipeline->source_open = QM_TRUE;
            return QM_EOK;
        case QM_AUDIO_SOURCE_FILE:
#if CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT
        {
            qm_fs_file_t file;

            ret = qm_fs_open(source->value.file.path, QM_FS_OPEN_READ, &file);
            if (ret != QM_EOK) {
                return ret;
            }
            pipeline->source_ctx = file;
            pipeline->source_open = QM_TRUE;
            return QM_EOK;
        }
#else
            return -QM_ENOSYS;
#endif
        case QM_AUDIO_SOURCE_URI:
#if CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT
            return http_source_open(pipeline);
#else
            return -QM_ENOSYS;
#endif
        default:
            return -QM_ENOSYS;
    }
}

static int source_read(qm_audio_pipeline_t *pipeline, void *buffer, uint32_t capacity, uint32_t *read_size)
{
    const qm_audio_source_t *source = &pipeline->config.source;
    int ret;

    *read_size = 0U;
    if (source->type == QM_AUDIO_SOURCE_MEMORY) {
        uintptr_t base = (uintptr_t)source->value.memory.data;
        uintptr_t current = (uintptr_t)pipeline->source_ctx;
        uintptr_t end = base + source->value.memory.size;
        uint32_t remaining;
        uint32_t count;

        if (current >= end) {
            return QM_EOK;
        }
        remaining = (uint32_t)(end - current);
        count = remaining < capacity ? remaining : capacity;
        qm_memcpy(buffer, (const void *)current, count);
        pipeline->source_ctx = (void *)(current + count);
        *read_size = count;
        return QM_EOK;
    }
    if (source->type == QM_AUDIO_SOURCE_PUSH) {
        return qm_audio_push_source_read(
            (qm_audio_push_source_t *)pipeline->source_ctx,
            buffer, capacity, CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS,
            read_size);
    }
    if (source->type == QM_AUDIO_SOURCE_CUSTOM) {
        ret = source->value.custom.read(source->value.custom.ctx, buffer, capacity,
                                         CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS,
                                         read_size);
        if (*read_size > capacity) {
            return -QM_EIO;
        }
        return qm_audio_normalize_result(ret);
    }
    if (source->type == QM_AUDIO_SOURCE_FILE) {
#if CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT
        /* qm_fs_read returns QM_EOK with *read_size == 0 for EOF directly;
         * only a nonzero read_size represents data. */
        ret = qm_fs_read(pipeline->source_ctx, buffer, capacity, read_size);
        if (ret != QM_EOK) {
            return ret;
        }
        return (*read_size > capacity) ? -QM_EIO : QM_EOK;
#else
        return -QM_ENOSYS;
#endif
    }
    if (source->type == QM_AUDIO_SOURCE_URI) {
#if CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT
        http_source_ctx_t *http = (http_source_ctx_t *)pipeline->source_ctx;

        ret = qm_http_client_read(http->client, (char *)buffer, (int)capacity);
        if (ret <= 0) {
            /* 0 means EOF and passes through; negative (including -QM_ETIMEOUT)
             * reports an error or a temporary lack of data. */
            return ret;
        }
        *read_size = (uint32_t)ret;
        return QM_EOK;
#else
        return -QM_ENOSYS;
#endif
    }
    return -QM_ENOSYS;
}

static int source_close(qm_audio_pipeline_t *pipeline)
{
    const qm_audio_source_t *source = &pipeline->config.source;
    int ret = QM_EOK;

    if (!pipeline->source_open) {
        return QM_EOK;
    }
    if (source->type == QM_AUDIO_SOURCE_CUSTOM) {
        ret = source->value.custom.close(source->value.custom.ctx);
    } else if (source->type == QM_AUDIO_SOURCE_PUSH) {
        /* Control owns the push context and releases it after Pipeline destroy. */
        ret = QM_EOK;
    } else if (source->type == QM_AUDIO_SOURCE_FILE) {
#if CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT
        ret = qm_fs_close(pipeline->source_ctx);
#else
        ret = -QM_ENOSYS;
#endif
    } else if (source->type == QM_AUDIO_SOURCE_URI) {
#if CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT
        http_source_ctx_t *http = (http_source_ctx_t *)pipeline->source_ctx;

        ret = qm_http_client_cleanup(http->client);
        if (ret == QM_EOK) {
            /* Free the wrapper only after a successful release; on failure keep
             * it so a later retry sees the still-open source instead of a
             * dangling pointer. */
            qm_free(http->etag);
            qm_free(http);
        }
#else
        ret = -QM_ENOSYS;
#endif
    }
    if (ret != QM_EOK) {
        return qm_audio_normalize_result(ret);
    }
    pipeline->source_open = QM_FALSE;
    pipeline->source_ctx = NULL;
    return QM_EOK;
}

static int decoder_close(qm_audio_pipeline_t *pipeline);

/** @brief Derive stream length from source rather than codec hint. / 从输入源而非业务编解码提示推导流长度。 */
static int prepare_media(qm_audio_pipeline_t *pipeline)
{
    uint32_t size = 0U;
    qm_bool known = QM_FALSE;
    const qm_audio_source_t *source = &pipeline->config.source;
    uint32_t frame_bytes = qm_audio_pcm_frame_bytes(&pipeline->config.output_format.pcm);
    uint32_t rate = pipeline->config.output_format.pcm.sample_rate_hz;
    if (!pipeline->needs_decoder && pipeline->config.input_format.pcm.sample_rate_hz != 0U) {
        frame_bytes = qm_audio_pcm_frame_bytes(&pipeline->config.input_format.pcm);
        rate = pipeline->config.input_format.pcm.sample_rate_hz;
    }
    if (source->type == QM_AUDIO_SOURCE_MEMORY) {
        size = source->value.memory.size;
        known = QM_TRUE;
    }
#if CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT
    if (source->type == QM_AUDIO_SOURCE_FILE &&
        qm_fs_size(pipeline->source_ctx, &size) == QM_EOK) known = QM_TRUE;
#endif
#if CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT
    if (source->type == QM_AUDIO_SOURCE_URI) {
        http_source_ctx_t *http = pipeline->source_ctx;
        /* A ranged (resume) open has a partial Content-Length; the recorded
         * full size is the stream length the decoder must see. */
        if (pipeline->http_length != 0U) {
            size = pipeline->http_length;
            known = QM_TRUE;
        } else if (qm_http_client_has_content_length(http->client)) {
            size = qm_http_client_get_content_length(http->client);
            known = QM_TRUE;
        }
    }
#endif
    if (pipeline->packet_passthrough || pipeline->byte_passthrough) return QM_EOK;
    if (pipeline->needs_decoder) {
        pipeline->decoder_config.input_size = known ? size : 0U;
        /* Hand the retained record to the decoder so it continues from the
         * recorded breakpoint; the decoder copies whatever it retains. */
        pipeline->decoder_config.resume =
            pipeline->resume_valid ? &pipeline->resume_point : NULL;
        return QM_EOK;
    }
    if (known) {
        if (size % frame_bytes != 0U) return -QM_EINVAL;
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        pipeline->duration_ms = qm_audio_frames_to_ms(size / frame_bytes, rate);
        pipeline->progress_flags |= QM_AUDIO_PROGRESS_DURATION_VALID;
        (void)qm_mutex_unlock(&pipeline->lock);
    }
    /* Raw PCM has a direct byte-to-frame mapping. Compressed input uses the
     * same decoder from the start; never approximate VBR offsets by percent. */
    if (known && pipeline->skip_frames != 0U) {
        uint64_t frames = pipeline->skip_frames;
        uint64_t offset;
        if (frames > size / frame_bytes) frames = size / frame_bytes;
        offset = frames * frame_bytes;
        if (source->type == QM_AUDIO_SOURCE_MEMORY) {
            pipeline->source_ctx = (void *)((const uint8_t *)source->value.memory.data + offset);
        }
#if CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT
        else if (source->type == QM_AUDIO_SOURCE_FILE && offset <= INT32_MAX) {
            int ret = qm_fs_seek(pipeline->source_ctx, (int32_t)offset, QM_FS_SEEK_SET);
            if (ret != QM_EOK) return ret;
        }
#endif
        else return QM_EOK; /* Bounded sequential reads still reach the target. */
        pipeline->decoded_frames = frames;
        pipeline->skip_frames = 0U;
    }
    return QM_EOK;
}

static void update_decoder_duration(qm_audio_pipeline_t *pipeline)
{
#if CONFIG_QM_AUDIO_DECODER_SUPPORT
    uint32_t duration;
    if (pipeline->decoder_open &&
        qm_audio_decoder_get_duration(pipeline->decoder_ctx, &duration) == QM_EOK) {
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        if ((pipeline->progress_flags & QM_AUDIO_PROGRESS_DURATION_VALID) == 0U ||
            (pipeline->progress_flags & QM_AUDIO_PROGRESS_DURATION_ESTIMATED) != 0U) {
            pipeline->duration_ms = duration;
            pipeline->progress_flags |= QM_AUDIO_PROGRESS_DURATION_VALID |
                                        QM_AUDIO_PROGRESS_DURATION_ESTIMATED;
        }
        (void)qm_mutex_unlock(&pipeline->lock);
    }
#else
    (void)pipeline;
#endif
}

void qm_audio_pipeline_get_progress(qm_audio_pipeline_t *pipeline, uint32_t *duration_ms, uint32_t *flags)
{
    *duration_ms = *flags = 0U;
    if (pipeline == NULL) return;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    *duration_ms = pipeline->duration_ms;
    *flags = pipeline->progress_flags;
    (void)qm_mutex_unlock(&pipeline->lock);
}

int qm_audio_pipeline_capture_resume(qm_audio_pipeline_t *pipeline,
                                     qm_audio_resume_point_t *resume, uint32_t timeout_ms)
{
    uint64_t frames;
    uint32_t sample_rate;
    uint32_t frame_bytes;

    if (pipeline == NULL || resume == NULL) return -QM_EINVAL;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->closing || pipeline->capture_active) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_ENOSYS;
    }
    frames = pipeline->decoded_frames;
    sample_rate = pipeline->config.output_format.pcm.sample_rate_hz;

    if (!pipeline->needs_decoder) {
        /* 裸 PCM 直通无需解码器状态: 断点即已消费的源字节偏移, 恢复时精确从该
         * 偏移续抓。透传路径不按帧计数, 无法给出字节断点, 明确拒绝。 */
        frame_bytes = qm_audio_pcm_frame_bytes(&pipeline->config.output_format.pcm);
        if (pipeline->byte_passthrough || pipeline->packet_passthrough ||
            frame_bytes == 0U || sample_rate == 0U) {
            (void)qm_mutex_unlock(&pipeline->lock);
            return -QM_ENOSYS;
        }
        resume->offset = frames > UINT32_MAX / frame_bytes ?
                         UINT32_MAX : (uint32_t)(frames * frame_bytes);
        resume->position_ms = qm_audio_frames_to_ms(frames, sample_rate);
        resume->state_size = 0U;
        (void)qm_mutex_unlock(&pipeline->lock);
        return QM_EOK;
    }

#if CONFIG_QM_AUDIO_DECODER_SUPPORT
    {
        void *decoder_ctx;
        int ret;

        if (!pipeline->decoder_open || pipeline->decoder_ctx == NULL) {
            (void)qm_mutex_unlock(&pipeline->lock);
            return -QM_ENOSYS;
        }
        decoder_ctx = pipeline->decoder_ctx;
        pipeline->capture_active = QM_TRUE;
        (void)qm_mutex_unlock(&pipeline->lock);

        /* decoder 层已在 qma_dec worker 上串行化捕获；capture_active 保证本调用
         * 期间 decoder_close 不会释放 decoder_ctx。 */
        ret = qm_audio_normalize_result(
            qm_audio_decoder_capture_resume(decoder_ctx, resume, timeout_ms));

        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        pipeline->capture_active = QM_FALSE;
        (void)qm_mutex_unlock(&pipeline->lock);
        (void)qm_sem_signal(&pipeline->capture_sem);

        if (ret != QM_EOK) return ret;
        resume->position_ms = sample_rate != 0U ?
            qm_audio_frames_to_ms(frames, sample_rate) : 0U;
        return QM_EOK;
    }
#else
    (void)timeout_ms;
    (void)qm_mutex_unlock(&pipeline->lock);
    return -QM_ENOSYS;
#endif
}

/** @brief Caller holds Pipeline lock after Source-owned cleanup. / 调用方在 Source 所有的清理后持有 Pipeline 锁。 */
static void reset_rewind_locked(qm_audio_pipeline_t *pipeline, uint32_t position_ms, uint32_t generation)
{
    pipeline->executor = NULL;
    if (!pipeline->execution_active) pipeline->execution_task = NULL;
    pipeline->execution_started = QM_FALSE;
    pipeline->execution_stage = PIPELINE_OPEN;
    pipeline->chunk_size = pipeline->chunk_offset = pipeline->raw_carry = 0U;
    pipeline->read_result = QM_EOK;
    pipeline->decoder_budget_active = QM_FALSE;
    pipeline->cancel_requested = pipeline->paused = pipeline->pause_notified = QM_FALSE;
    pipeline->resume_pending = QM_FALSE;
    pipeline->rewind_cancelled = QM_FALSE;
    pipeline->state = QM_AUDIO_PIPELINE_NEW;
    pipeline->result = QM_EOK;
    pipeline->generation = generation;
    pipeline->decoded_frames = 0U;
    /* A rewind refetches from byte zero, so a consumed resume record must not
     * steer the reopened source back to a mid-stream range. */
    pipeline->resume_valid = QM_FALSE;
    pipeline->skip_frames = (uint64_t)position_ms *
        pipeline->config.output_format.pcm.sample_rate_hz / 1000U;
    pipeline->progress_flags |= QM_AUDIO_PROGRESS_SEEKING;
}

/**
 * @brief Join a cancelled run before reusing Pipeline state. / 复用 Pipeline 状态前必须等待已取消运行退出。
 *
 * A cancelled run must be joined before reuse: old decoder callbacks and
 * source reads can no longer mutate the new timeline or deliver stale PCM. */
int qm_audio_pipeline_rewind(qm_audio_pipeline_t *pipeline, uint32_t position_ms, uint32_t generation)
{
    int ret;
    if (pipeline == NULL || pipeline->packet_passthrough || pipeline->byte_passthrough)
        return -QM_ENOSYS;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->task_started || pipeline->execution_active) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EBUSY;
    }
    (void)qm_mutex_unlock(&pipeline->lock);
    ret = decoder_close(pipeline);
    if (ret != QM_EOK) return ret;
    ret = source_close(pipeline);
    if (ret != QM_EOK) return ret;
    ret = release_processing(pipeline);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    reset_rewind_locked(pipeline, position_ms, generation);
    pipeline->rewind_pending = QM_FALSE;
    pipeline->rewind_complete = QM_FALSE;
    pipeline->rewind_result = QM_EOK;
    (void)qm_mutex_unlock(&pipeline->lock);
    return QM_EOK;
}

/** @brief Run retryable rewind cleanup on the shared Source worker. / 在共享 Source 任务上执行可重试的回退清理。 */
static void execute_rewind(qm_audio_pipeline_t *pipeline)
{
    int ret;

    ret = decoder_close(pipeline);
    if (ret == QM_EOK) ret = source_close(pipeline);
    if (ret == QM_EOK) ret = release_processing(pipeline);

    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (ret == QM_EOK) {
        /* The latest drag position may arrive while close is in progress. */
        if (pipeline->rewind_cancelled) {
            pipeline->executor = NULL;
            pipeline->execution_started = QM_FALSE;
            pipeline->state = QM_AUDIO_PIPELINE_CANCELLED;
            pipeline->result = -QM_EINTR;
        } else {
            reset_rewind_locked(pipeline, pipeline->rewind_position_ms,
                                pipeline->rewind_generation);
        }
    } else {
        pipeline->executor = NULL;
        pipeline->execution_started = QM_FALSE;
        if (pipeline->rewind_cancelled) {
            pipeline->state = QM_AUDIO_PIPELINE_CANCELLED;
            pipeline->result = -QM_EINTR;
            ret = -QM_EINTR;
        } else {
            pipeline->state = QM_AUDIO_PIPELINE_ERROR;
            pipeline->result = ret;
        }
    }
    pipeline->rewind_pending = QM_FALSE;
    pipeline->rewind_complete = QM_TRUE;
    pipeline->rewind_result = ret;
    (void)qm_sem_signal(&pipeline->state_sem);
    (void)qm_mutex_unlock(&pipeline->lock);
}

int qm_audio_pipeline_rewind_request(qm_audio_t *audio, qm_audio_pipeline_t *pipeline, uint32_t position_ms, uint32_t generation)
{
    int ret;

    if (audio == NULL || pipeline == NULL || !qm_mutex_is_valid(&pipeline->lock)) {
        return -QM_EINVAL;
    }
    if (pipeline->packet_passthrough || pipeline->byte_passthrough) return -QM_ENOSYS;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->rewind_pending) {
        pipeline->rewind_position_ms = position_ms;
        pipeline->rewind_generation = generation;
        pipeline->rewind_complete = QM_FALSE;
        (void)qm_mutex_unlock(&pipeline->lock);
        return QM_EOK;
    }
    if (pipeline->rewind_complete &&
        pipeline->rewind_position_ms == position_ms &&
        pipeline->rewind_generation == generation) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return QM_EOK;
    }
    if (pipeline->task_started || pipeline->execution_active ||
        (!pipeline->rewind_complete &&
         pipeline->state != QM_AUDIO_PIPELINE_FINISHED &&
         pipeline->state != QM_AUDIO_PIPELINE_CANCELLED &&
         pipeline->state != QM_AUDIO_PIPELINE_ERROR)) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EBUSY;
    }
    /* A terminal run has left the Source registry, but retains the old
     * executor pointer until its replacement is prepared. */
    pipeline->executor = NULL;
    pipeline->rewind_pending = QM_TRUE;
    pipeline->rewind_complete = QM_FALSE;
    pipeline->rewind_cancelled = QM_FALSE;
    pipeline->rewind_position_ms = position_ms;
    pipeline->rewind_generation = generation;
    pipeline->rewind_result = QM_EOK;
    pipeline->state = QM_AUDIO_PIPELINE_RUNNING;
    pipeline->task_started = QM_TRUE;
    (void)qm_mutex_unlock(&pipeline->lock);

    ret = qm_audio_executor_submit(audio, pipeline);
    if (ret == QM_EOK) return QM_EOK;

    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    pipeline->executor = NULL;
    pipeline->rewind_pending = QM_FALSE;
    pipeline->rewind_complete = QM_TRUE;
    pipeline->rewind_result = ret;
    pipeline->task_started = QM_FALSE;
    pipeline->state = QM_AUDIO_PIPELINE_ERROR;
    pipeline->result = ret;
    (void)qm_sem_signal(&pipeline->state_sem);
    (void)qm_mutex_unlock(&pipeline->lock);
    return ret;
}

int qm_audio_pipeline_rewind_result(qm_audio_pipeline_t *pipeline, int *result)
{
    if (pipeline == NULL || result == NULL || !qm_mutex_is_valid(&pipeline->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (!pipeline->rewind_complete || pipeline->execution_active || pipeline->task_started) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EBUSY;
    }
    *result = pipeline->rewind_result;
    (void)qm_mutex_unlock(&pipeline->lock);
    return QM_EOK;
}

static int decoder_close(qm_audio_pipeline_t *pipeline)
{
    int ret;

    if (!pipeline->decoder_open) {
        return QM_EOK;
    }
#if CONFIG_QM_AUDIO_DECODER_SUPPORT
    /* 与续播点捕获互斥：先等进行中的捕获退出，再置 closing 拒绝新捕获，之后才在
     * 锁外释放上下文。释放后在同一个锁临界区内落下关闭状态，使捕获要么看到
     * closing=TRUE，要么看到 decoder_ctx=NULL，绝不会读到半关闭快照。 */
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    while (pipeline->capture_active) {
        (void)qm_mutex_unlock(&pipeline->lock);
        (void)qm_sem_wait(&pipeline->capture_sem, QM_WAIT_FOREVER);
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    }
    pipeline->closing = QM_TRUE;
    (void)qm_mutex_unlock(&pipeline->lock);

    ret = qm_audio_normalize_result(qm_audio_decoder_close(pipeline->decoder_ctx));
    if (ret != QM_EOK) {
        /* close 失败保留上下文供重试，同时放开捕获。 */
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        pipeline->closing = QM_FALSE;
        (void)qm_mutex_unlock(&pipeline->lock);
        return ret;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    pipeline->decoder_open = QM_FALSE;
    pipeline->decoder_ctx = NULL;
    pipeline->closing = QM_FALSE;
    (void)qm_mutex_unlock(&pipeline->lock);
#else
    ret = QM_EOK;
    pipeline->decoder_open = QM_FALSE;
    pipeline->decoder_ctx = NULL;
#endif
    return ret;
}

static int emit_raw(qm_audio_pipeline_t *pipeline, const void *data, uint32_t size, uint32_t flags)
{
    qm_audio_data_t block = {0};

    block.data = data;
    block.size = size;
    block.timestamp_ms = 0U;
    block.flags = flags;
    return emit_decoded(&block, pipeline);
}

/* TOC timing is sufficient to reject a mismatched fixed device profile. Full
 * entropy decoding remains the codec's responsibility. Reject fractional-ms
 * packets instead of rounding their media time into a different profile. */
static int opus_duration(const uint8_t *data, uint32_t size, uint32_t *duration_ms)
{
    uint32_t frame_units, count, total;
    if (data == NULL || size < 1U) return -QM_EINVAL;
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
        if (size < 2U) return -QM_EINVAL;
        count = data[1] & 0x3fU;
    }
    total = frame_units * count; /* units of 2.5 ms, exact until final conversion */
    if (count == 0U || total > 48U || (total & 1U) != 0U) return -QM_ENOSYS;
    *duration_ms = total * 5U / 2U;
    return QM_EOK;
}

static uint32_t packet_header_size(const qm_audio_pipeline_t *pipeline)
{
    switch (pipeline->config.input_format.framing) {
    case QM_AUDIO_CODEC_FRAMING_PACKET: return sizeof(qm_audio_packet_header_t);
    case QM_AUDIO_CODEC_FRAMING_OPUS_RAW_HEADER: return 8U;
    default: return 0U;
    }
}

static int deliver_packets(qm_audio_pipeline_t *pipeline)
{
    const qm_audio_stream_format_t *format = &pipeline->config.output_format;
    uint32_t header_size = packet_header_size(pipeline);
    while (pipeline->chunk_offset < pipeline->chunk_size) {
        const uint8_t *data = pipeline->chunk + pipeline->chunk_offset;
        uint32_t size = pipeline->chunk_size - pipeline->chunk_offset;
        uint32_t needed, take;
        int ret = worker_checkpoint(pipeline);
        if (ret != QM_EOK) return ret;
        if (pipeline->packet_expected == 0U) {
            pipeline->packet_expected = header_size ? header_size :
                pipeline->config.input_format.packet_bytes;
            qm_memset(&pipeline->packet_meta, 0, sizeof(pipeline->packet_meta));
        }
        needed = pipeline->packet_expected - pipeline->packet_used;
        take = needed < size ? needed : size;
        qm_memcpy(pipeline->packet_buffer + pipeline->packet_used, data, take);
        pipeline->packet_used += take;
        pipeline->chunk_offset += take;
        if (pipeline->packet_used != pipeline->packet_expected) continue;
        if (header_size != 0U && pipeline->packet_expected == header_size) {
            uint32_t bytes;
            if (pipeline->config.input_format.framing == QM_AUDIO_CODEC_FRAMING_PACKET) {
                qm_audio_packet_header_t header;
                qm_memcpy(&header, pipeline->packet_buffer, sizeof(header));
                bytes = header.size;
                pipeline->packet_meta.timestamp_ms = header.timestamp_ms;
                pipeline->packet_meta.duration_ms = header.duration_ms;
                pipeline->packet_meta.sequence = header.sequence;
                pipeline->packet_meta.flags = header.flags;
            } else {
                const uint8_t *p = pipeline->packet_buffer;
                bytes = ((uint32_t)p[0] << 24U) | ((uint32_t)p[1] << 16U) |
                        ((uint32_t)p[2] << 8U) | p[3];
            }
            if (bytes == 0U || bytes > format->max_packet_bytes) return -QM_EINVAL;
            if (pipeline->config.input_format.packet_bytes != 0U &&
                bytes != pipeline->config.input_format.packet_bytes) return -QM_EINVAL;
            pipeline->packet_expected += bytes;
            continue;
        }
        qm_audio_data_t packet = pipeline->packet_meta;
        packet.data = pipeline->packet_buffer + header_size;
        packet.size = pipeline->packet_expected - header_size;
        uint32_t duration = 0U;
        /* Only a codec-specific byte framing needs payload inspection. Explicit
         * PACKET input already owns its boundaries and metadata. */
        if (pipeline->config.input_format.framing == QM_AUDIO_CODEC_FRAMING_OPUS_CBR) {
            ret = opus_duration(packet.data, packet.size, &duration);
            if (ret != QM_EOK) return ret;
            if ((format->packet_duration_ms != 0U &&
                 duration != format->packet_duration_ms) ||
                (format->pcm.channels != 0U &&
                 ((((const uint8_t *)packet.data)[0] & 4U) != 0U ? 2U : 1U) !=
                  format->pcm.channels))
                return -QM_ENOSYS;
            packet.duration_ms = duration;
            packet.flags |= QM_AUDIO_DATA_DURATION_VALID;
        } else if (packet.flags & QM_AUDIO_DATA_DURATION_VALID) {
            duration = packet.duration_ms;
        } else if (format->packet_duration_ms != 0U) {
            duration = format->packet_duration_ms;
            packet.duration_ms = duration;
            packet.flags |= QM_AUDIO_DATA_DURATION_VALID;
        } else {
            pipeline->packet_duration_known = QM_FALSE;
        }
        if (pipeline->packet_duration_known &&
            (packet.flags & QM_AUDIO_DATA_TIMESTAMP_VALID) == 0U) {
            packet.timestamp_ms = pipeline->packet_time_ms;
            packet.flags |= QM_AUDIO_DATA_TIMESTAMP_VALID;
        }
        ret = emit_packet(&packet, pipeline);
        if (ret != QM_EOK) return ret;
        pipeline->packet_time_ms = packet.timestamp_ms > UINT64_MAX - duration ?
            UINT64_MAX : packet.timestamp_ms + duration;
        pipeline->packet_duration_total_ms =
            pipeline->packet_duration_total_ms > UINT64_MAX - duration ?
            UINT64_MAX : pipeline->packet_duration_total_ms + duration;
        pipeline->packet_used = pipeline->packet_expected = 0U;
    }
    return QM_EOK;
}

/**
 * @brief Feed a complete source read into the decoder. / 将一次完整输入源读取喂给解码器。
 *
 * A decoder may accept only
 * a prefix when its input FIFO is full; retry the unconsumed suffix within one
 * total budget instead of dropping bytes or replaying an accepted prefix. */
static void decoder_budget_start(qm_audio_pipeline_t *pipeline)
{
    if (pipeline->decoder_budget_active) return;
    pipeline->decoder_budget_active = QM_TRUE;
    pipeline->decoder_pause_generation = 0U;
    qm_utils_time_countdown_ms(&pipeline->decoder_deadline,
                               CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS);
}

static int decoder_push_all(qm_audio_pipeline_t *pipeline)
{
    uint32_t timeout_ms = CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS;
    decoder_budget_start(pipeline);
    while (pipeline->chunk_offset < pipeline->chunk_size) {
        uint32_t consumed = 0U;
        uint32_t wait_ms;
        int ret = decoder_wait_ready(pipeline, &pipeline->decoder_pause_generation,
                                      &pipeline->decoder_deadline, timeout_ms);
        if (ret != QM_EOK) return ret;
        wait_ms = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                  qm_utils_time_left(&pipeline->decoder_deadline);
        if (wait_ms == 0U) return -QM_ETIMEOUT;
#if CONFIG_QM_AUDIO_DECODER_SUPPORT
        ret = qm_audio_normalize_result(qm_audio_decoder_push(
            pipeline->decoder_ctx, pipeline->chunk + pipeline->chunk_offset,
            pipeline->chunk_size - pipeline->chunk_offset, wait_ms, &consumed));
#else
        ret = -QM_ENOSYS;
#endif
        if (consumed > pipeline->chunk_size - pipeline->chunk_offset) return -QM_EIO;
        pipeline->chunk_offset += consumed;
        if (ret != QM_EOK && ret != -QM_ETIMEOUT) return ret;
        if (pipeline->chunk_offset == pipeline->chunk_size) return QM_EOK;
        if (ret == QM_EOK && consumed == 0U) return -QM_EIO;
    }
    return QM_EOK;
}

#if CONFIG_QM_AUDIO_DECODER_SUPPORT
static int decoder_flush_all(qm_audio_pipeline_t *pipeline)
{
    uint32_t timeout_ms = CONFIG_QM_AUDIO_PIPELINE_READ_TIMEOUT_MS;
    decoder_budget_start(pipeline);
    /* The decoder flush reports ETIMEOUT only after spending its own budget,
     * which is the caller's remaining budget, so the wait_ms check below ends
     * the loop right away; yielding here would only delay that return. / 解码
     * flush 只在自身预算(即本循环的剩余预算)耗尽后返回 ETIMEOUT，下一轮的
     * wait_ms 检查随即收口，因此无需额外让出 CPU。 */
    for (;;) {
        uint32_t wait_ms;
        int ret = decoder_wait_ready(pipeline, &pipeline->decoder_pause_generation,
                                      &pipeline->decoder_deadline, timeout_ms);
        if (ret != QM_EOK) return ret;
        wait_ms = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                  qm_utils_time_left(&pipeline->decoder_deadline);
        if (wait_ms == 0U) return -QM_ETIMEOUT;
        ret = qm_audio_normalize_result(qm_audio_decoder_flush(pipeline->decoder_ctx, wait_ms));
        if (ret != -QM_ETIMEOUT) return ret;
    }
}
#endif


/** @brief Preserve session progress while yielding only the execution slot. / 进度属于会话；YIELD 只释放执行槽位。 */
static int run_pipeline(qm_audio_pipeline_t *pipeline)
{
    uint32_t frame_bytes = (pipeline->packet_passthrough || pipeline->byte_passthrough) ?
        1U : qm_audio_pcm_frame_bytes(&pipeline->config.output_format.pcm);
    if (!pipeline->needs_decoder && pipeline->config.input_format.pcm.sample_rate_hz != 0U)
        frame_bytes = qm_audio_pcm_frame_bytes(&pipeline->config.input_format.pcm);
    int close_ret;
    int ret;
    for (;;) {
        ret = worker_checkpoint(pipeline);
        if (ret != QM_EOK) break;
        if (pipeline->execution_stage == PIPELINE_OPEN) {
            if (frame_bytes == 0U) { ret = -QM_EINVAL; break; }
            ret = source_open(pipeline);
            if (ret != QM_EOK) break;
            pipeline->execution_stage = PIPELINE_MEDIA;
        } else if (pipeline->execution_stage == PIPELINE_MEDIA) {
            ret = prepare_media(pipeline);
            if (ret != QM_EOK) break;
            pipeline->execution_stage = PIPELINE_DECODE;
        } else if (pipeline->execution_stage == PIPELINE_DECODE) {
            if (pipeline->needs_decoder) {
#if CONFIG_QM_AUDIO_DECODER_SUPPORT
                ret = qm_audio_normalize_result(qm_audio_decoder_open(
                    &pipeline->decoder_ctx, &pipeline->config.output_format.pcm,
                    emit_decoded, pipeline, &pipeline->decoder_config));
                if (ret != QM_EOK || pipeline->decoder_ctx == NULL) {
                    ret = ret == QM_EOK ? -QM_EIO : ret;
                    if (pipeline->resume_valid && ret == -QM_ENOSYS) {
                        /* The decoder cannot continue this stream: drop the
                         * resume record and refetch from byte zero in the same
                         * run instead of failing playback. */
                        pipeline->resume_valid = QM_FALSE;
                        pipeline->decoder_config.resume = NULL;
                        ret = source_close(pipeline);
                        if (ret != QM_EOK) break;
                        pipeline->execution_stage = PIPELINE_OPEN;
                        continue;
                    }
                    break;
                }
                pipeline->decoder_open = QM_TRUE;
                /* The record is consumed once the decoder accepted it. */
                pipeline->resume_valid = QM_FALSE;
#else
                ret = -QM_ENOSYS;
                break;
#endif
            }
            pipeline->chunk_capacity = CONFIG_QM_AUDIO_PIPELINE_CHUNK_BYTES;
            if (pipeline->chunk_capacity < frame_bytes) pipeline->chunk_capacity = frame_bytes;
            pipeline->chunk = qm_malloc(pipeline->chunk_capacity);
            if (pipeline->chunk == NULL) { ret = -QM_ENOMEM; break; }
            pipeline->execution_stage = PIPELINE_READ;
        } else if (pipeline->execution_stage == PIPELINE_READ) {
            uint32_t size = 0U;
            pipeline->read_result = source_read(pipeline,
                pipeline->chunk + pipeline->raw_carry,
                pipeline->chunk_capacity - pipeline->raw_carry, &size);
            if (size > pipeline->chunk_capacity - pipeline->raw_carry) { ret = -QM_EIO; break; }
            if (size == 0U && pipeline->read_result == -QM_ETIMEOUT) continue;
            if (size == 0U && pipeline->read_result != QM_EOK) { ret = pipeline->read_result; break; }
            pipeline->decoder_budget_active = QM_FALSE;
            if (size == 0U) {
                if (pipeline->raw_carry != 0U) { ret = -QM_EINVAL; break; }
                pipeline->execution_stage = pipeline->decoder_open ? PIPELINE_FLUSH : PIPELINE_EOS;
            } else {
                pipeline->chunk_size = pipeline->raw_carry + size;
                pipeline->chunk_offset = 0U;
                pipeline->execution_stage = PIPELINE_DELIVER;
            }
        } else if (pipeline->execution_stage == PIPELINE_DELIVER) {
            if (pipeline->byte_passthrough) {
                ret = emit_bytes(pipeline, pipeline->chunk, pipeline->chunk_size);
            } else if (pipeline->packet_passthrough) {
                ret = deliver_packets(pipeline);
            } else if (pipeline->decoder_open) {
                ret = decoder_push_all(pipeline);
                update_decoder_duration(pipeline);
            } else {
                uint32_t consumed = pipeline->chunk_size - pipeline->chunk_size % frame_bytes;
                ret = consumed == 0U ? QM_EOK :
                      emit_raw(pipeline, pipeline->chunk, consumed, QM_AUDIO_DATA_NONE);
                if (ret == QM_EOK) {
                    pipeline->raw_carry = pipeline->chunk_size - consumed;
                    if (pipeline->raw_carry != 0U)
                        qm_memmove(pipeline->chunk, pipeline->chunk + consumed, pipeline->raw_carry);
                }
            }
            if (ret != QM_EOK) break;
            if (pipeline->read_result != QM_EOK && pipeline->read_result != -QM_ETIMEOUT) {
                ret = pipeline->read_result;
                break;
            }
            pipeline->execution_stage = PIPELINE_READ;
        } else if (pipeline->execution_stage == PIPELINE_FLUSH) {
#if CONFIG_QM_AUDIO_DECODER_SUPPORT
            ret = decoder_flush_all(pipeline);
            update_decoder_duration(pipeline);
#else
            ret = -QM_ENOSYS;
#endif
            if (ret != QM_EOK) break;
            pipeline->execution_stage = PIPELINE_EOS;
        } else {
            if (pipeline->byte_passthrough) {
                ret = QM_EOK;
            } else if (pipeline->packet_passthrough) {
                ret = pipeline->packet_used == 0U ? QM_EOK : -QM_EINVAL;
                if (ret == QM_EOK && pipeline->packet_duration_known) {
                    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
                    pipeline->duration_ms = pipeline->packet_duration_total_ms > UINT32_MAX ?
                        UINT32_MAX : (uint32_t)pipeline->packet_duration_total_ms;
                    pipeline->progress_flags |= QM_AUDIO_PROGRESS_DURATION_VALID;
                    (void)qm_mutex_unlock(&pipeline->lock);
                }
            } else
                ret = emit_raw(pipeline, NULL, 0U, QM_AUDIO_DATA_END_OF_STREAM);
            break;
        }
    }
    /* A preserving pause keeps source, decoder, and partial-frame state. The
     * executor releases only its worker slot. */
    if (ret == PIPELINE_YIELD) return ret;
    qm_free(pipeline->chunk);
    pipeline->chunk = NULL;
    close_ret = decoder_close(pipeline);
    if (ret == QM_EOK) ret = close_ret;
    close_ret = source_close(pipeline);
    if (ret == QM_EOK) ret = close_ret;
    return ret;
}

/**
 * @brief Commit terminal state and emit exactly one terminal event. / 提交终止状态并且只发送一次终止事件。
 *
 * Lock the terminal state and emit exactly one terminal event, mapping
 * cancellation into CANCELLED, success into FINISHED, else ERROR. */
static void pipeline_complete(qm_audio_pipeline_t *pipeline, int result)
{
    qm_audio_event_type_t event_type;

    if (pipeline == NULL || !qm_mutex_is_valid(&pipeline->lock)) {
        return;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->state == QM_AUDIO_PIPELINE_FINISHED ||
        pipeline->state == QM_AUDIO_PIPELINE_CANCELLED ||
        pipeline->state == QM_AUDIO_PIPELINE_ERROR) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return;
    }
    result = qm_audio_normalize_result(result);
    pipeline->task_started = QM_TRUE;
    pipeline->result = result;
    if (pipeline->cancel_requested || result == -QM_EINTR) {
        pipeline->state = QM_AUDIO_PIPELINE_CANCELLED;
        pipeline->result = -QM_EINTR;
        event_type = QM_AUDIO_EVENT_CANCELLED;
    } else if (result == QM_EOK) {
        pipeline->state = QM_AUDIO_PIPELINE_FINISHED;
        event_type = QM_AUDIO_EVENT_FINISHED;
    } else {
        pipeline->state = QM_AUDIO_PIPELINE_ERROR;
        event_type = QM_AUDIO_EVENT_ERROR;
    }
    wake_paused(pipeline);
    (void)qm_mutex_unlock(&pipeline->lock);
    pipeline_event(pipeline, event_type, pipeline->result);
    /* Keep the handle alive until the terminal callback has returned. The
     * executor clears task_started after it removes the registry entry. */
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->executor == NULL) pipeline->task_started = QM_FALSE;
    (void)qm_sem_signal(&pipeline->state_sem);
    (void)qm_mutex_unlock(&pipeline->lock);
}

int qm_audio_pipeline_prepare(qm_audio_pipeline_t *pipeline)
{
    int ret;

    if (pipeline == NULL || !qm_mutex_is_valid(&pipeline->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->state != QM_AUDIO_PIPELINE_NEW) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EBUSY;
    }
    /* Capability checks happen during create, but provider contexts and
     * packet buffers are allocated only once the caller has admitted this
     * run. Queued Service operations therefore retain only the route plan. */
    ret = create_processing(pipeline);
    if (ret != QM_EOK) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return ret;
    }
    pipeline->rewind_pending = QM_FALSE;
    pipeline->rewind_complete = QM_FALSE;
    pipeline->rewind_cancelled = QM_FALSE;
    pipeline->rewind_result = QM_EOK;
    pipeline->state = QM_AUDIO_PIPELINE_RUNNING;
    pipeline->task_started = QM_TRUE;
    (void)qm_mutex_unlock(&pipeline->lock);
    return QM_EOK;
}

qm_bool qm_audio_pipeline_execute(qm_audio_pipeline_t *pipeline)
{
    qm_bool rewind;
    int ret;
    if (pipeline == NULL || !qm_mutex_is_valid(&pipeline->lock)) return QM_TRUE;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    rewind = pipeline->rewind_pending;
    (void)qm_mutex_unlock(&pipeline->lock);
    if (rewind) {
        execute_rewind(pipeline);
        return QM_TRUE;
    }
    if (!pipeline->execution_started) {
        pipeline->execution_started = QM_TRUE;
        if (!cancelled(pipeline)) pipeline_event(pipeline, QM_AUDIO_EVENT_STARTED, QM_EOK);
    }
    ret = run_pipeline(pipeline);
    /* Yield is non-terminal: the shared executor clears execution_active and
     * may assign its resident worker to another runnable Pipeline. */
    if (ret == PIPELINE_YIELD) return QM_FALSE;
    pipeline_complete(pipeline, ret);
    return QM_TRUE;
}

void qm_audio_pipeline_fail(qm_audio_pipeline_t *pipeline, int result)
{
    pipeline_complete(pipeline, result == QM_EOK ? -QM_EIO : qm_audio_normalize_result(result));
}

/* One fixed route, selected before admission. Opaque passthrough does not
 * consult codec providers; conversion never changes PCM geometry. */
typedef struct {
    qm_bool packets, bytes, decode;
    qm_audio_decoder_config_t decoder;
} route_plan_t;

/* Keep codec-specific duration declarations at the route boundary so platform
 * decoders cannot silently interpret the same malformed input differently.
 * Complete native PACKET profiles are the one generic exception. */
static int validate_input_declaration(const qm_audio_codec_config_t *input)
{
    if (input == NULL) return -QM_EINVAL;
    if ((input->framing == QM_AUDIO_CODEC_FRAMING_OPUS_OGG ||
         input->framing == QM_AUDIO_CODEC_FRAMING_OPUS_RAW_HEADER ||
         input->framing == QM_AUDIO_CODEC_FRAMING_OPUS_CBR) &&
        input->codec != QM_AUDIO_CODEC_OPUS) return -QM_EINVAL;
    if (input->packet_duration_ms == 0U) return QM_EOK;
    /* A complete native PACKET profile may carry a product-defined duration
     * for passthrough, regardless of codec identity. */
    if (input->framing == QM_AUDIO_CODEC_FRAMING_PACKET) return QM_EOK;
    if (input->codec != QM_AUDIO_CODEC_OPUS) return -QM_EINVAL;
    switch (input->framing) {
    case QM_AUDIO_CODEC_FRAMING_OPUS_RAW_HEADER:
    case QM_AUDIO_CODEC_FRAMING_OPUS_CBR:
    case QM_AUDIO_CODEC_FRAMING_PACKET:
        return QM_EOK;
    default:
        return -QM_EINVAL;
    }
}

static int build_route(const qm_audio_pipeline_config_t *config, route_plan_t *plan)
{
    const qm_audio_codec_config_t *in = &config->input_format;
    const qm_audio_stream_format_t *out = &config->output_format;
    qm_bool same_geometry = qm_audio_pcm_equal(&in->pcm, &out->pcm);
    qm_memset(plan, 0, sizeof(*plan));
    if ((unsigned int)in->framing > QM_AUDIO_CODEC_FRAMING_PACKET) return -QM_EINVAL;
    {
        int declaration_ret = validate_input_declaration(in);
        if (declaration_ret != QM_EOK) return declaration_ret;
    }
    if (in->codec != QM_AUDIO_CODEC_PCM && in->codec != QM_AUDIO_CODEC_AUTO &&
        !((in->codec > QM_AUDIO_CODEC_PCM && in->codec < QM_AUDIO_CODEC_AUTO) ||
          qm_audio_codec_is_private(in->codec))) return -QM_EINVAL;
    if (in->framing == QM_AUDIO_CODEC_FRAMING_PACKET &&
        config->source.type != QM_AUDIO_SOURCE_PUSH) return -QM_EINVAL;
    if (in->pcm.sample_rate_hz != 0U && !qm_audio_pcm_format_valid(&in->pcm))
        return -QM_EINVAL;
    if (in->pcm.sample_rate_hz == 0U &&
        (in->pcm.channels != 0U || in->pcm.sample != QM_AUDIO_PCM_S16)) return -QM_EINVAL;
    if (qm_audio_encoded_bytes(out)) {
        if (in->codec != out->codec || in->framing != out->framing ||
            in->packet_bytes != 0U || in->packet_duration_ms != 0U ||
            !same_geometry) return -QM_ENOSYS;
        plan->bytes = QM_TRUE;
        return QM_EOK;
    }
    if (qm_audio_encoded_packets(out) && in->codec == out->codec &&
        same_geometry && in->packet_duration_ms == out->packet_duration_ms &&
        (out->packet_bytes == 0U ? in->packet_bytes <= out->max_packet_bytes :
                                  in->packet_bytes == out->packet_bytes)) {
        if (in->framing == QM_AUDIO_CODEC_FRAMING_PACKET ||
            (in->codec == QM_AUDIO_CODEC_OPUS &&
             in->framing == QM_AUDIO_CODEC_FRAMING_OPUS_CBR && in->packet_bytes != 0U)) {
            plan->packets = QM_TRUE;
            return QM_EOK;
        }
    }
    /* Unknown MP3 geometry is checked on first decoded output. */
    if (!qm_audio_pcm_format_valid(&out->pcm)) return -QM_ENOSYS;
    if (in->pcm.sample_rate_hz != 0U && !same_geometry) return -QM_ENOSYS;
    if (in->framing == QM_AUDIO_CODEC_FRAMING_PACKET) return -QM_ENOSYS;
    if (in->codec == QM_AUDIO_CODEC_PCM) {
        if (in->framing != QM_AUDIO_CODEC_FRAMING_STREAM ||
            in->packet_bytes != 0U || in->packet_duration_ms != 0U) return -QM_EINVAL;
    } else {
        if ((in->codec != QM_AUDIO_CODEC_OPUS &&
             in->framing != QM_AUDIO_CODEC_FRAMING_STREAM) ||
            ((in->framing == QM_AUDIO_CODEC_FRAMING_OPUS_CBR) !=
             (in->packet_bytes != 0U))) return -QM_EINVAL;
        switch (in->codec) {
        case QM_AUDIO_CODEC_AUTO: plan->decoder.codec_hint = NULL; break;
        case QM_AUDIO_CODEC_MP3: plan->decoder.codec_hint = "mp3"; break;
        case QM_AUDIO_CODEC_OPUS: plan->decoder.codec_hint = "opus"; break;
        case QM_AUDIO_CODEC_G711A: plan->decoder.codec_hint = "pcma"; break;
        case QM_AUDIO_CODEC_G711U: plan->decoder.codec_hint = "pcmu"; break;
        case QM_AUDIO_CODEC_G722: plan->decoder.codec_hint = "g722"; break;
        default: return -QM_ENOSYS;
        }
        plan->decoder.framing = (qm_audio_decoder_framing_t)in->framing;
        plan->decoder.packet_bytes = in->packet_bytes;
        plan->decoder.packet_duration_ms = in->packet_duration_ms;
        plan->decode = QM_TRUE;
#if CONFIG_QM_AUDIO_DECODER_SUPPORT
        int ret = qm_audio_decoder_check_format(&out->pcm, &plan->decoder);
        if (ret != QM_EOK) return ret;
#else
        return -QM_ENOSYS;
#endif
    }
    if (out->codec != QM_AUDIO_CODEC_PCM) {
#if CONFIG_QM_AUDIO_ENCODER_SUPPORT
        return qm_audio_encoder_check_format(out);
#else
        return -QM_ENOSYS;
#endif
    }
    return QM_EOK;
}

/* Pause retains stages; rewind recreates them from the immutable route. */
static int create_processing(qm_audio_pipeline_t *pipeline)
{
    const qm_audio_stream_format_t *out = &pipeline->config.output_format;
    if (pipeline->encoder_ctx != NULL || pipeline->packet_buffer != NULL) return -QM_EBUSY;
    pipeline->packet_duration_known = QM_TRUE;
    if (pipeline->packet_passthrough) {
        uint32_t header = packet_header_size(pipeline);
        if (out->max_packet_bytes > UINT32_MAX - header) return -QM_EINVAL;
        pipeline->packet_capacity = out->max_packet_bytes + header;
        pipeline->packet_buffer = qm_malloc(pipeline->packet_capacity);
        return pipeline->packet_buffer != NULL ? QM_EOK : -QM_ENOMEM;
    }
    if (!pipeline->byte_passthrough && out->codec != QM_AUDIO_CODEC_PCM) {
#if CONFIG_QM_AUDIO_ENCODER_SUPPORT
        return qm_audio_encoder_open(&pipeline->encoder_ctx, out, emit_packet, pipeline);
#else
        return -QM_ENOSYS;
#endif
    }
    return QM_EOK;
}

int qm_audio_pipeline_create(const qm_audio_pipeline_config_t *config, qm_audio_pipeline_t **out_pipeline)
{
    qm_audio_pipeline_t *pipeline;
    route_plan_t plan;
    int ret;

    if (out_pipeline == NULL) {
        return -QM_EINVAL;
    }
    *out_pipeline = NULL;
    if (config == NULL || !qm_audio_stream_format_valid(&config->output_format) ||
        ((config->write_pcm != NULL) + (config->write_packet != NULL) +
         (config->write_bytes != NULL) != 1) ||
        (config->output_format.codec == QM_AUDIO_CODEC_PCM ? config->write_pcm == NULL :
         qm_audio_encoded_bytes(&config->output_format) ? config->write_bytes == NULL :
                                                        config->write_packet == NULL)) {
        return -QM_EINVAL;
    }
    if (config->source.type > QM_AUDIO_SOURCE_CUSTOM) {
        return -QM_EINVAL;
    }
    if (config->source.type == QM_AUDIO_SOURCE_MEMORY &&
        config->source.value.memory.size != 0U &&
        config->source.value.memory.data == NULL) {
        return -QM_EINVAL;
    }
    if (config->source.type == QM_AUDIO_SOURCE_FILE &&
        config->source.value.file.path == NULL) {
        return -QM_EINVAL;
    }
    if (config->source.type == QM_AUDIO_SOURCE_FILE &&
        !CONFIG_QM_AUDIO_SOURCE_FILE_SUPPORT) {
        return -QM_ENOSYS;
    }
    if (config->source.type == QM_AUDIO_SOURCE_URI &&
        config->source.value.uri.url == NULL) {
        return -QM_EINVAL;
    }
    if (config->source.type == QM_AUDIO_SOURCE_URI &&
        !CONFIG_QM_AUDIO_SOURCE_URI_SUPPORT) {
        return -QM_ENOSYS;
    }
    if (config->source.type == QM_AUDIO_SOURCE_PUSH &&
        config->source.value.push.context == NULL) {
        return -QM_EINVAL;
    }
    if (config->source.type == QM_AUDIO_SOURCE_CUSTOM &&
        (config->source.value.custom.open == NULL ||
         config->source.value.custom.read == NULL ||
         config->source.value.custom.cancel == NULL ||
         config->source.value.custom.close == NULL)) {
        return -QM_EINVAL;
    }
    ret = build_route(config, &plan);
    if (ret != QM_EOK) return ret;
    pipeline = (qm_audio_pipeline_t *)qm_calloc(1U, sizeof(*pipeline));
    if (pipeline == NULL) {
        return -QM_ENOMEM;
    }
    pipeline->config = *config;
    pipeline->packet_passthrough = plan.packets;
    pipeline->byte_passthrough = plan.bytes;
    pipeline->needs_decoder = plan.decode;
    pipeline->decoder_config = plan.decoder;
    if (config->source.type == QM_AUDIO_SOURCE_FILE) {
        ret = copy_string(&pipeline->file_path, config->source.value.file.path);
        if (ret != QM_EOK) {
            (void)pipeline_free(pipeline);
            return ret;
        }
        pipeline->config.source.value.file.path = pipeline->file_path;
    }
    if (config->source.type == QM_AUDIO_SOURCE_URI) {
        ret = copy_string(&pipeline->uri, config->source.value.uri.url);
        if (ret != QM_EOK) {
            (void)pipeline_free(pipeline);
            return ret;
        }
        pipeline->config.source.value.uri.url = pipeline->uri;
    }
    /* Snapshot and detach the borrowed resume record: the caller may release it
     * once create returns, and only the internal copy is consulted afterwards.
     * Only the URI route can position its source at the recorded offset, so any
     * other source ignores the record and restarts from its own start. / 复制并
     * 摘除借用的续播记录：调用方可在 create 返回后释放，之后只使用内部副本。只有 URI
     * 路径能把源定位到记录偏移，其他源会忽略该记录并从自身起点重播。 */
    if (config->resume != NULL && config->resume->offset != 0U &&
        config->source.type == QM_AUDIO_SOURCE_URI &&
        !pipeline->packet_passthrough && !pipeline->byte_passthrough) {
        pipeline->resume_point = *config->resume;
        pipeline->resume_valid = QM_TRUE;
        /* Baseline decode progress at the recorded position so the measured
         * duration and seek baseline include the not-re-decoded prefix. */
        if (config->output_format.pcm.sample_rate_hz != 0U) {
            pipeline->decoded_frames = (uint64_t)pipeline->resume_point.position_ms *
                config->output_format.pcm.sample_rate_hz / 1000U;
        }
    }
    pipeline->config.resume = NULL;
    ret = qm_mutex_new(&pipeline->lock);
    if (ret != QM_EOK) {
        (void)pipeline_free(pipeline);
        return -QM_EIO;
    }
    ret = qm_sem_new(&pipeline->state_sem, 0);
    if (ret != QM_EOK) {
        (void)pipeline_free(pipeline);
        return -QM_EIO;
    }
    ret = qm_sem_new(&pipeline->wake_sem, 0);
    if (ret != QM_EOK) {
        (void)pipeline_free(pipeline);
        return -QM_EIO;
    }
    ret = qm_sem_new(&pipeline->capture_sem, 0);
    if (ret != QM_EOK) {
        (void)pipeline_free(pipeline);
        return -QM_EIO;
    }
    pipeline->state = QM_AUDIO_PIPELINE_NEW;
    pipeline->generation = 1U;
    if (!pipeline->packet_passthrough && !pipeline->byte_passthrough &&
        (config->source.type == QM_AUDIO_SOURCE_MEMORY ||
         config->source.type == QM_AUDIO_SOURCE_FILE)) {
        pipeline->progress_flags = QM_AUDIO_PROGRESS_SEEKABLE;
    }
    if (config->source.type == QM_AUDIO_SOURCE_MEMORY && !pipeline->needs_decoder &&
        !pipeline->packet_passthrough && !pipeline->byte_passthrough &&
        config->source.value.memory.size % qm_audio_pcm_frame_bytes(&config->output_format.pcm) == 0U) {
        pipeline->duration_ms = qm_audio_frames_to_ms(config->source.value.memory.size /
            qm_audio_pcm_frame_bytes(&config->output_format.pcm), config->output_format.pcm.sample_rate_hz);
        pipeline->progress_flags |= QM_AUDIO_PROGRESS_DURATION_VALID;
    }
    *out_pipeline = pipeline;
    return QM_EOK;
}

int qm_audio_pipeline_pause(qm_audio_pipeline_t *pipeline)
{
    qm_audio_push_source_t *push_source = NULL;

    if (pipeline == NULL) return -QM_EINVAL;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->state != QM_AUDIO_PIPELINE_RUNNING) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EBUSY;
    }
    pipeline->paused = QM_TRUE;
    pipeline->pause_generation++;
    pipeline->pause_notified = QM_FALSE;
    if (pipeline->config.source.type == QM_AUDIO_SOURCE_PUSH) {
        push_source = (qm_audio_push_source_t *)pipeline->config.source.value.push.context;
    }
    (void)qm_mutex_unlock(&pipeline->lock);
    /* PUSH reads can block without an executor wake. Interrupting them keeps
     * their queued bytes intact and returns control to the pause checkpoint. */
    qm_audio_push_source_interrupt(push_source);
    qm_audio_executor_wake(pipeline);
    return QM_EOK;
}

int qm_audio_pipeline_resume(qm_audio_pipeline_t *pipeline)
{
    if (pipeline == NULL) return -QM_EINVAL;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->state != QM_AUDIO_PIPELINE_PAUSED &&
        !(pipeline->state == QM_AUDIO_PIPELINE_RUNNING && pipeline->paused)) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EBUSY;
    }
    pipeline->paused = QM_FALSE;
    if (pipeline->state == QM_AUDIO_PIPELINE_PAUSED) {
        pipeline->resume_pending = QM_TRUE;
        pipeline->state = QM_AUDIO_PIPELINE_RUNNING;
    }
    wake_paused(pipeline);
    qm_audio_executor_wake(pipeline);
    (void)qm_mutex_unlock(&pipeline->lock);
    return QM_EOK;
}

int qm_audio_pipeline_wait_paused(qm_audio_pipeline_t *pipeline, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;

    if (pipeline == NULL || !qm_sem_is_valid(&pipeline->state_sem)) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        qm_audio_pipeline_state_t state;
        uint32_t left;

        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        state = pipeline->state;
        if (state == QM_AUDIO_PIPELINE_PAUSED && pipeline->pause_notified &&
            pipeline->emit_inflight == 0U && !pipeline->execution_active) {
            (void)qm_mutex_unlock(&pipeline->lock);
            return QM_EOK;
        }
        if (state == QM_AUDIO_PIPELINE_CANCELLED) {
            (void)qm_mutex_unlock(&pipeline->lock);
            return -QM_EINTR;
        }
        if (state == QM_AUDIO_PIPELINE_ERROR) {
            int result = pipeline->result;

            (void)qm_mutex_unlock(&pipeline->lock);
            return result != QM_EOK ? result : -QM_EIO;
        }
        (void)qm_mutex_unlock(&pipeline->lock);
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (left == 0U || qm_sem_wait(&pipeline->state_sem, left) != QM_EOK) {
            return -QM_ETIMEOUT;
        }
    }
}

int qm_audio_pipeline_cancel(qm_audio_pipeline_t *pipeline)
{
    qm_audio_source_cancel_fn cancel_source = NULL;
    qm_audio_push_source_t *push_source = NULL;
    void *source_ctx = NULL;
    qm_bool complete_new;

    if (pipeline == NULL) return -QM_EINVAL;
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    /* A completed or in-flight Source-owned rewind has already revoked the
     * old run. Do not turn its freshly reset NEW state into a terminal run. */
    if (pipeline->rewind_pending || pipeline->rewind_complete) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return QM_EOK;
    }
    if (pipeline->state == QM_AUDIO_PIPELINE_FINISHED ||
        pipeline->state == QM_AUDIO_PIPELINE_CANCELLED ||
        pipeline->state == QM_AUDIO_PIPELINE_ERROR || pipeline->cancel_requested) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return QM_EOK;
    }
    complete_new = pipeline->state == QM_AUDIO_PIPELINE_NEW;
    pipeline->cancel_requested = QM_TRUE;
    pipeline->paused = QM_FALSE;
    if (pipeline->config.source.type == QM_AUDIO_SOURCE_PUSH) {
        push_source = pipeline->config.source.value.push.context;
    } else if (pipeline->config.source.type == QM_AUDIO_SOURCE_CUSTOM) {
        cancel_source = pipeline->config.source.value.custom.cancel;
        source_ctx = pipeline->config.source.value.custom.ctx;
    }
    wake_paused(pipeline);
    qm_audio_executor_wake(pipeline);
    (void)qm_mutex_unlock(&pipeline->lock);
    /* Cancellation only wakes I/O; the source worker owns close. / 取消仅唤醒
     * I/O，关闭仍归源任务所有，回调不得在 Pipeline 锁内执行。 */
    if (push_source != NULL) qm_audio_push_source_cancel(push_source);
    if (cancel_source != NULL) cancel_source(source_ctx);
    if (complete_new) pipeline_complete(pipeline, -QM_EINTR);
    return QM_EOK;
}

int qm_audio_pipeline_cancel_for_stop(qm_audio_pipeline_t *pipeline)
{
    if (pipeline == NULL || !qm_mutex_is_valid(&pipeline->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->rewind_pending) {
        pipeline->rewind_cancelled = QM_TRUE;
        pipeline->cancel_requested = QM_TRUE;
        (void)qm_mutex_unlock(&pipeline->lock);
        qm_audio_executor_wake(pipeline);
        return QM_EOK;
    }
    if (pipeline->rewind_complete) {
        pipeline->rewind_cancelled = QM_TRUE;
        pipeline->cancel_requested = QM_TRUE;
        pipeline->paused = QM_FALSE;
        pipeline->executor = NULL;
        pipeline->state = QM_AUDIO_PIPELINE_CANCELLED;
        pipeline->result = -QM_EINTR;
        pipeline->rewind_result = -QM_EINTR;
        (void)qm_sem_signal(&pipeline->state_sem);
        (void)qm_mutex_unlock(&pipeline->lock);
        return QM_EOK;
    }
    (void)qm_mutex_unlock(&pipeline->lock);
    return qm_audio_pipeline_cancel(pipeline);
}

int qm_audio_pipeline_wait(qm_audio_pipeline_t *pipeline, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;

    if (pipeline == NULL || !qm_sem_is_valid(&pipeline->state_sem))
        return -QM_EINVAL;
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        qm_audio_pipeline_state_t state;
        qm_bool active;
        uint32_t left;

        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        state = pipeline->state;
        active = pipeline->task_started;
        (void)qm_mutex_unlock(&pipeline->lock);
        if ((state == QM_AUDIO_PIPELINE_FINISHED ||
             state == QM_AUDIO_PIPELINE_CANCELLED ||
             state == QM_AUDIO_PIPELINE_ERROR) && !active) {
            return QM_EOK;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline);
        if (left == 0U || qm_sem_wait(&pipeline->state_sem, left) != QM_EOK) return -QM_ETIMEOUT;
    }
}

int qm_audio_pipeline_get_state(const qm_audio_pipeline_t *pipeline, qm_audio_pipeline_state_t *state, int *result)
{
    qm_audio_pipeline_t *mutable_pipeline = (qm_audio_pipeline_t *)pipeline;

    if (pipeline == NULL || state == NULL || !qm_mutex_is_valid(&mutable_pipeline->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&mutable_pipeline->lock, QM_WAIT_FOREVER);
    *state = mutable_pipeline->state;
    if (result != NULL) {
        *result = mutable_pipeline->result;
    }
    (void)qm_mutex_unlock(&mutable_pipeline->lock);
    return QM_EOK;
}

int qm_audio_pipeline_destroy(qm_audio_pipeline_t *pipeline)
{
    int ret;

    if (pipeline == NULL || !qm_mutex_is_valid(&pipeline->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->state == QM_AUDIO_PIPELINE_RUNNING ||
        pipeline->state == QM_AUDIO_PIPELINE_PAUSED ||
        pipeline->task_started) {
        (void)qm_mutex_unlock(&pipeline->lock);
        return -QM_EBUSY;
    }
    (void)qm_mutex_unlock(&pipeline->lock);
    ret = decoder_close(pipeline);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = source_close(pipeline);
    if (ret != QM_EOK) {
        return ret;
    }
    return pipeline_free(pipeline);
}
