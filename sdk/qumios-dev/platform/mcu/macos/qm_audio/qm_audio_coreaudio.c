#include "qm_audio_coreaudio.h"
#include "qm_audio_permission.h"

#include "qm_errno.h"
#include "qm_log.h"
#include "qm_ringbuf.h"

#include <AudioToolbox/AudioToolbox.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "macos audio"

#ifndef CONFIG_QM_MACOS_AUDIO_INPUT_BUFFER_COUNT
#define CONFIG_QM_MACOS_AUDIO_INPUT_BUFFER_COUNT 4
#endif

#ifndef CONFIG_QM_MACOS_AUDIO_OUTPUT_BUFFER_COUNT
#define CONFIG_QM_MACOS_AUDIO_OUTPUT_BUFFER_COUNT 6
#endif

#ifndef CONFIG_QM_MACOS_AUDIO_CAPTURE_RING_FRAMES
#define CONFIG_QM_MACOS_AUDIO_CAPTURE_RING_FRAMES 8
#endif

#ifndef CONFIG_QM_MACOS_AUDIO_PERMISSION_TIMEOUT_MS
#define CONFIG_QM_MACOS_AUDIO_PERMISSION_TIMEOUT_MS 30000U
#endif

#define QM_MACOS_AUDIO_VOLUME_MAX 100U

typedef struct {
    AudioQueueRef input_queue;
    AudioQueueRef output_queue;
    AudioQueueBufferRef input_buffers[CONFIG_QM_MACOS_AUDIO_INPUT_BUFFER_COUNT];
    AudioQueueBufferRef output_buffers[CONFIG_QM_MACOS_AUDIO_OUTPUT_BUFFER_COUNT];
    qm_bool output_buffer_busy[CONFIG_QM_MACOS_AUDIO_OUTPUT_BUFFER_COUNT];

    pthread_mutex_t capture_lock;
    pthread_cond_t capture_cond;
    pthread_mutex_t output_lock;
    pthread_cond_t output_cond;
    qm_bool capture_lock_ready;
    qm_bool capture_cond_ready;
    qm_bool output_lock_ready;
    qm_bool output_cond_ready;

    qm_ringbuf_t capture_ring;
    uint8_t *capture_storage;
    uint8_t *capture_discard;
    uint32_t capture_capacity;
    uint32_t capture_count;
    uint32_t capture_frame_bytes;
    uint32_t output_frame_bytes;
    uint32_t output_sample_bytes;
    uint64_t capture_dropped_bytes;

    qm_bool capture_closing;
    qm_bool input_started;
    qm_bool input_starting;
    qm_bool capture_paused;
    int capture_error;

    qm_bool output_started;
    qm_bool output_resetting;
    qm_bool output_closing;
    uint32_t volume;
    qm_bool muted;
} macos_audio_ctx_t;

static qm_audio_codec_t payload_codec(const qm_audio_format_t *format)
{
    if (format == NULL) {
        return QM_AUDIO_CODEC_NONE;
    }
    return format->data_type != QM_AUDIO_CODEC_NONE ? format->data_type : format->codec;
}

static int build_pcm_format(const qm_audio_format_t *format,
                            AudioStreamBasicDescription *description,
                            uint32_t *frame_bytes,
                            uint32_t *sample_bytes)
{
    uint64_t bytes_per_sample;
    uint64_t bytes_per_frame;
    uint64_t bytes_per_audio_frame;

    if (format == NULL || description == NULL || frame_bytes == NULL ||
        sample_bytes == NULL || payload_codec(format) != QM_AUDIO_CODEC_PCM ||
        format->sample_rate == 0U || format->channel_num == 0U ||
        format->bits_per_sample == 0U || (format->bits_per_sample % 8U) != 0U ||
        format->frame_ms == 0U) {
        return -QM_EINVAL;
    }

    bytes_per_sample = format->bits_per_sample / 8U;
    bytes_per_audio_frame = bytes_per_sample * format->channel_num;
    bytes_per_frame = (uint64_t)format->sample_rate * bytes_per_audio_frame *
                      format->frame_ms;
    if (bytes_per_sample > UINT32_MAX || bytes_per_audio_frame > UINT32_MAX ||
        bytes_per_frame == 0U || (bytes_per_frame % 1000U) != 0U ||
        (bytes_per_frame / 1000U) > INT_MAX) {
        return -QM_EINVAL;
    }

    memset(description, 0, sizeof(*description));
    description->mSampleRate = format->sample_rate;
    description->mFormatID = kAudioFormatLinearPCM;
    description->mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                                kLinearPCMFormatFlagIsPacked |
                                kAudioFormatFlagsNativeEndian;
    description->mBytesPerPacket = (UInt32)bytes_per_audio_frame;
    description->mFramesPerPacket = 1U;
    description->mBytesPerFrame = (UInt32)bytes_per_audio_frame;
    description->mChannelsPerFrame = format->channel_num;
    description->mBitsPerChannel = format->bits_per_sample;

    *frame_bytes = (uint32_t)(bytes_per_frame / 1000U);
    *sample_bytes = (uint32_t)bytes_per_audio_frame;
    return QM_EOK;
}

static void set_capture_error(macos_audio_ctx_t *ctx, int error)
{
    if (ctx == NULL || !ctx->capture_lock_ready) {
        return;
    }

    pthread_mutex_lock(&ctx->capture_lock);
    if (!ctx->capture_closing) {
        ctx->capture_error = error;
    }
    if (ctx->capture_cond_ready) {
        pthread_cond_broadcast(&ctx->capture_cond);
    }
    pthread_mutex_unlock(&ctx->capture_lock);
}

static void input_callback(void *user_data,
                           AudioQueueRef queue,
                           AudioQueueBufferRef buffer,
                           const AudioTimeStamp *start_time,
                           UInt32 packet_count,
                           const AudioStreamPacketDescription *packet_descriptions)
{
    macos_audio_ctx_t *ctx = (macos_audio_ctx_t *)user_data;
    uint32_t incoming;
    uint32_t free_bytes;
    uint8_t *incoming_data;
    qm_bool requeue;
    OSStatus status;

    (void)start_time;
    (void)packet_count;
    (void)packet_descriptions;

    if (ctx == NULL || buffer == NULL) {
        return;
    }

    pthread_mutex_lock(&ctx->capture_lock);
    requeue = !ctx->capture_closing;
    incoming = (uint32_t)buffer->mAudioDataByteSize;
    incoming_data = (uint8_t *)buffer->mAudioData;
    if (requeue && !ctx->capture_paused && incoming > 0U) {
        if (incoming > ctx->capture_capacity) {
            incoming_data += incoming - ctx->capture_capacity;
            incoming = ctx->capture_capacity;
        }

        free_bytes = ctx->capture_capacity - ctx->capture_count;
        while (free_bytes < incoming && ctx->capture_count > 0U) {
            uint32_t drop = ctx->capture_count < ctx->capture_frame_bytes ?
                            ctx->capture_count : ctx->capture_frame_bytes;
            int popped = qm_ringbuf_pop(&ctx->capture_ring, ctx->capture_discard, (int)drop);

            if (popped <= 0) {
                break;
            }
            ctx->capture_count -= (uint32_t)popped;
            ctx->capture_dropped_bytes += (uint32_t)popped;
            free_bytes = ctx->capture_capacity - ctx->capture_count;
        }

        if (free_bytes >= incoming) {
            int pushed = qm_ringbuf_push(&ctx->capture_ring,
                                         incoming_data,
                                         (int)incoming);
            if (pushed > 0) {
                ctx->capture_count += (uint32_t)pushed;
                pthread_cond_signal(&ctx->capture_cond);
            }
        }
    }
    pthread_mutex_unlock(&ctx->capture_lock);

    if (requeue) {
        status = AudioQueueEnqueueBuffer(queue, buffer, 0U, NULL);
        if (status != noErr) {
            set_capture_error(ctx, -QM_EIO);
        }
    }
}

static void output_callback(void *user_data,
                            AudioQueueRef queue,
                            AudioQueueBufferRef buffer)
{
    macos_audio_ctx_t *ctx = (macos_audio_ctx_t *)user_data;
    uint32_t index;

    (void)queue;
    if (ctx == NULL || buffer == NULL) {
        return;
    }

    pthread_mutex_lock(&ctx->output_lock);
    for (index = 0U; index < CONFIG_QM_MACOS_AUDIO_OUTPUT_BUFFER_COUNT; ++index) {
        if (ctx->output_buffers[index] == buffer) {
            ctx->output_buffer_busy[index] = QM_FALSE;
            pthread_cond_broadcast(&ctx->output_cond);
            break;
        }
    }
    pthread_mutex_unlock(&ctx->output_lock);
}

static uint64_t dispose_context(macos_audio_ctx_t *ctx)
{
    uint64_t dropped = 0U;

    if (ctx == NULL) {
        return 0U;
    }

    if (ctx->capture_lock_ready) {
        pthread_mutex_lock(&ctx->capture_lock);
        ctx->capture_closing = QM_TRUE;
        if (ctx->capture_cond_ready) {
            pthread_cond_broadcast(&ctx->capture_cond);
        }
        pthread_mutex_unlock(&ctx->capture_lock);
    }
    if (ctx->output_lock_ready) {
        pthread_mutex_lock(&ctx->output_lock);
        ctx->output_closing = QM_TRUE;
        if (ctx->output_cond_ready) {
            pthread_cond_broadcast(&ctx->output_cond);
        }
        pthread_mutex_unlock(&ctx->output_lock);
    }

    if (ctx->input_queue != NULL) {
        (void)AudioQueueStop(ctx->input_queue, true);
        (void)AudioQueueDispose(ctx->input_queue, true);
        ctx->input_queue = NULL;
    }
    if (ctx->output_queue != NULL) {
        (void)AudioQueueStop(ctx->output_queue, true);
        (void)AudioQueueDispose(ctx->output_queue, true);
        ctx->output_queue = NULL;
    }
    dropped = ctx->capture_dropped_bytes;

    if (ctx->output_cond_ready) {
        pthread_cond_destroy(&ctx->output_cond);
    }
    if (ctx->output_lock_ready) {
        pthread_mutex_destroy(&ctx->output_lock);
    }
    if (ctx->capture_cond_ready) {
        pthread_cond_destroy(&ctx->capture_cond);
    }
    if (ctx->capture_lock_ready) {
        pthread_mutex_destroy(&ctx->capture_lock);
    }
    free(ctx->capture_discard);
    free(ctx->capture_storage);
    free(ctx);
    return dropped;
}

static int create_input_queue(macos_audio_ctx_t *ctx,
                              const qm_audio_format_t *format)
{
    AudioStreamBasicDescription description;
    uint32_t sample_bytes;
    uint64_t storage_size;
    uint32_t index;
    OSStatus status;
    int ret;

    ret = build_pcm_format(format, &description, &ctx->capture_frame_bytes, &sample_bytes);
    if (ret != QM_EOK) {
        return ret;
    }

    storage_size = (uint64_t)ctx->capture_frame_bytes *
                   CONFIG_QM_MACOS_AUDIO_CAPTURE_RING_FRAMES + 1U;
    if (storage_size > UINT32_MAX) {
        return -QM_EINVAL;
    }
    ctx->capture_storage = (uint8_t *)calloc(1U, (size_t)storage_size);
    ctx->capture_discard = (uint8_t *)malloc(ctx->capture_frame_bytes);
    if (ctx->capture_storage == NULL || ctx->capture_discard == NULL) {
        return -QM_ENOMEM;
    }
    if (qm_ringbuf_init(&ctx->capture_ring,
                        ctx->capture_storage,
                        (uint32_t)storage_size) != QM_EOK) {
        return -QM_EINIT;
    }
    ctx->capture_capacity = (uint32_t)storage_size - 1U;

    ret = qm_macos_microphone_authorize(CONFIG_QM_MACOS_AUDIO_PERMISSION_TIMEOUT_MS);
    if (ret != QM_EOK) {
        QM_LOGE(LOG_TAG, "microphone permission unavailable ret=%d", ret);
        return ret;
    }

    status = AudioQueueNewInput(&description,
                                input_callback,
                                ctx,
                                NULL,
                                NULL,
                                0U,
                                &ctx->input_queue);
    if (status != noErr) {
        QM_LOGE(LOG_TAG, "AudioQueueNewInput failed status=%d", (int)status);
        return -QM_EIO;
    }

    for (index = 0U; index < CONFIG_QM_MACOS_AUDIO_INPUT_BUFFER_COUNT; ++index) {
        status = AudioQueueAllocateBuffer(ctx->input_queue,
                                          ctx->capture_frame_bytes,
                                          &ctx->input_buffers[index]);
        if (status == noErr) {
            status = AudioQueueEnqueueBuffer(ctx->input_queue,
                                             ctx->input_buffers[index],
                                             0U,
                                             NULL);
        }
        if (status != noErr) {
            QM_LOGE(LOG_TAG, "input buffer setup failed status=%d index=%u",
                    (int)status, (unsigned int)index);
            return -QM_EIO;
        }
    }
    return QM_EOK;
}

static int create_output_queue(macos_audio_ctx_t *ctx,
                               const qm_audio_format_t *format)
{
    AudioStreamBasicDescription description;
    uint32_t index;
    OSStatus status;
    int ret;

    ret = build_pcm_format(format,
                           &description,
                           &ctx->output_frame_bytes,
                           &ctx->output_sample_bytes);
    if (ret != QM_EOK) {
        return ret;
    }

    status = AudioQueueNewOutput(&description,
                                 output_callback,
                                 ctx,
                                 NULL,
                                 NULL,
                                 0U,
                                 &ctx->output_queue);
    if (status != noErr) {
        QM_LOGE(LOG_TAG, "AudioQueueNewOutput failed status=%d", (int)status);
        return -QM_EIO;
    }

    for (index = 0U; index < CONFIG_QM_MACOS_AUDIO_OUTPUT_BUFFER_COUNT; ++index) {
        status = AudioQueueAllocateBuffer(ctx->output_queue,
                                          ctx->output_frame_bytes,
                                          &ctx->output_buffers[index]);
        if (status != noErr) {
            QM_LOGE(LOG_TAG, "output buffer setup failed status=%d index=%u",
                    (int)status, (unsigned int)index);
            return -QM_EIO;
        }
    }
    return QM_EOK;
}

static int coreaudio_open(void **driver_ctx,
                          const qm_audio_format_t *input_format,
                          const qm_audio_format_t *output_format,
                          qm_audio_event_cb_t event_cb,
                          void *user_ctx)
{
    macos_audio_ctx_t *ctx;
    int ret;

    (void)event_cb;
    (void)user_ctx;
    if (driver_ctx == NULL || (input_format == NULL && output_format == NULL)) {
        return -QM_EINVAL;
    }

    ctx = (macos_audio_ctx_t *)calloc(1U, sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    ctx->volume = QM_MACOS_AUDIO_VOLUME_MAX;

    if (pthread_mutex_init(&ctx->capture_lock, NULL) != 0) {
        dispose_context(ctx);
        return -QM_EINIT;
    }
    ctx->capture_lock_ready = QM_TRUE;
    if (pthread_cond_init(&ctx->capture_cond, NULL) != 0) {
        dispose_context(ctx);
        return -QM_EINIT;
    }
    ctx->capture_cond_ready = QM_TRUE;
    if (pthread_mutex_init(&ctx->output_lock, NULL) != 0) {
        dispose_context(ctx);
        return -QM_EINIT;
    }
    ctx->output_lock_ready = QM_TRUE;
    if (pthread_cond_init(&ctx->output_cond, NULL) != 0) {
        dispose_context(ctx);
        return -QM_EINIT;
    }
    ctx->output_cond_ready = QM_TRUE;

    if (input_format != NULL) {
        ret = create_input_queue(ctx, input_format);
        if (ret != QM_EOK) {
            dispose_context(ctx);
            return ret;
        }
    }
    if (output_format != NULL) {
        ret = create_output_queue(ctx, output_format);
        if (ret != QM_EOK) {
            dispose_context(ctx);
            return ret;
        }
    }

    QM_LOGI(LOG_TAG, "CoreAudio PCM ready capture_frame=%u playback_frame=%u",
            (unsigned int)ctx->capture_frame_bytes,
            (unsigned int)ctx->output_frame_bytes);
    *driver_ctx = ctx;
    return QM_EOK;
}

static int coreaudio_close(void *driver_ctx)
{
    macos_audio_ctx_t *ctx = (macos_audio_ctx_t *)driver_ctx;
    uint64_t dropped;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    dropped = dispose_context(ctx);
    if (dropped > 0U) {
        QM_LOGW(LOG_TAG, "capture dropped bytes=%llu", (unsigned long long)dropped);
    }
    return QM_EOK;
}

static int ensure_input_started(macos_audio_ctx_t *ctx)
{
    qm_bool pause_after_start;
    OSStatus status;

    pthread_mutex_lock(&ctx->capture_lock);
    if (ctx->capture_closing) {
        pthread_mutex_unlock(&ctx->capture_lock);
        return -QM_EINTR;
    }
    if (ctx->capture_paused || ctx->input_started) {
        pthread_mutex_unlock(&ctx->capture_lock);
        return QM_EOK;
    }
    if (ctx->input_starting) {
        while (ctx->input_starting && !ctx->capture_closing) {
            pthread_cond_wait(&ctx->capture_cond, &ctx->capture_lock);
        }
        status = ctx->input_started ? noErr : (OSStatus)-1;
        pthread_mutex_unlock(&ctx->capture_lock);
        return status == noErr ? QM_EOK : -QM_EIO;
    }
    ctx->input_starting = QM_TRUE;
    pthread_mutex_unlock(&ctx->capture_lock);

    status = AudioQueueStart(ctx->input_queue, NULL);

    pthread_mutex_lock(&ctx->capture_lock);
    ctx->input_starting = QM_FALSE;
    if (status == noErr) {
        ctx->input_started = QM_TRUE;
    } else {
        ctx->capture_error = -QM_EIO;
    }
    pause_after_start = ctx->capture_paused;
    pthread_cond_broadcast(&ctx->capture_cond);
    pthread_mutex_unlock(&ctx->capture_lock);

    if (status != noErr) {
        QM_LOGE(LOG_TAG, "AudioQueueStart input failed status=%d", (int)status);
        return -QM_EIO;
    }
    if (pause_after_start) {
        (void)AudioQueuePause(ctx->input_queue);
    }
    return QM_EOK;
}

static int wait_for_capture(macos_audio_ctx_t *ctx, uint32_t timeout_ms)
{
    int wait_ret = 0;

    while (ctx->capture_count < ctx->capture_frame_bytes && !ctx->capture_closing &&
           ctx->capture_error == QM_EOK) {
        if (timeout_ms == 0U) {
            return 0;
        }
        if (timeout_ms == UINT32_MAX) {
            wait_ret = pthread_cond_wait(&ctx->capture_cond, &ctx->capture_lock);
        } else {
            struct timespec relative = {
                .tv_sec = timeout_ms / 1000U,
                .tv_nsec = (long)(timeout_ms % 1000U) * 1000000L,
            };
            wait_ret = pthread_cond_timedwait_relative_np(&ctx->capture_cond,
                                                          &ctx->capture_lock,
                                                          &relative);
        }
        if (wait_ret == ETIMEDOUT) {
            return 0;
        }
        if (wait_ret != 0) {
            return -QM_EIO;
        }
    }
    return QM_EOK;
}

static int coreaudio_read(void *driver_ctx, void *data, uint32_t len, uint32_t timeout_ms)
{
    macos_audio_ctx_t *ctx = (macos_audio_ctx_t *)driver_ctx;
    int ret;
    int popped;

    if (ctx == NULL || data == NULL) {
        return -QM_EINVAL;
    }
    if (ctx->input_queue == NULL) {
        return -QM_ENOSYS;
    }
    if (len < ctx->capture_frame_bytes) {
        return -QM_EINVAL;
    }

    ret = ensure_input_started(ctx);
    if (ret != QM_EOK) {
        return ret;
    }

    pthread_mutex_lock(&ctx->capture_lock);
    ret = wait_for_capture(ctx, timeout_ms);
    if (ret != QM_EOK || ctx->capture_count < ctx->capture_frame_bytes) {
        if (ctx->capture_closing) {
            ret = -QM_EINTR;
        } else if (ctx->capture_error != QM_EOK) {
            ret = ctx->capture_error;
        }
        pthread_mutex_unlock(&ctx->capture_lock);
        return ret;
    }

    popped = qm_ringbuf_pop(&ctx->capture_ring, (uint8_t *)data,
                            (int)ctx->capture_frame_bytes);
    if (popped > 0) {
        ctx->capture_count -= (uint32_t)popped;
    }
    pthread_mutex_unlock(&ctx->capture_lock);
    return popped == (int)ctx->capture_frame_bytes ? popped : -QM_EIO;
}

static int acquire_output_buffer(macos_audio_ctx_t *ctx, uint32_t *output_index)
{
    uint32_t index;

    pthread_mutex_lock(&ctx->output_lock);
    for (;;) {
        if (ctx->output_closing) {
            pthread_mutex_unlock(&ctx->output_lock);
            return -QM_EINTR;
        }
        if (!ctx->output_resetting) {
            for (index = 0U; index < CONFIG_QM_MACOS_AUDIO_OUTPUT_BUFFER_COUNT; ++index) {
                if (!ctx->output_buffer_busy[index]) {
                    ctx->output_buffer_busy[index] = QM_TRUE;
                    *output_index = index;
                    pthread_mutex_unlock(&ctx->output_lock);
                    return QM_EOK;
                }
            }
        }
        if (pthread_cond_wait(&ctx->output_cond, &ctx->output_lock) != 0) {
            pthread_mutex_unlock(&ctx->output_lock);
            return -QM_EIO;
        }
    }
}

static void release_output_buffer(macos_audio_ctx_t *ctx, uint32_t index)
{
    pthread_mutex_lock(&ctx->output_lock);
    ctx->output_buffer_busy[index] = QM_FALSE;
    pthread_cond_broadcast(&ctx->output_cond);
    pthread_mutex_unlock(&ctx->output_lock);
}

static int stop_playback(macos_audio_ctx_t *ctx);

static int coreaudio_write(void *driver_ctx, const void *data, uint32_t len)
{
    macos_audio_ctx_t *ctx = (macos_audio_ctx_t *)driver_ctx;
    const uint8_t *cursor = (const uint8_t *)data;
    uint32_t remaining = len;

    if (ctx == NULL || data == NULL || len == 0U) {
        return -QM_EINVAL;
    }
    if (ctx->output_queue == NULL) {
        return -QM_ENOSYS;
    }
    if (len > INT_MAX || (len % ctx->output_sample_bytes) != 0U) {
        return -QM_EINVAL;
    }

    while (remaining > 0U) {
        AudioQueueBufferRef buffer;
        uint32_t index;
        uint32_t chunk = remaining < ctx->output_frame_bytes ?
                         remaining : ctx->output_frame_bytes;
        qm_bool start_queue;
        OSStatus status;
        int ret;

        ret = acquire_output_buffer(ctx, &index);
        if (ret != QM_EOK) {
            return ret;
        }
        buffer = ctx->output_buffers[index];
        memcpy(buffer->mAudioData, cursor, chunk);
        buffer->mAudioDataByteSize = chunk;
        status = AudioQueueEnqueueBuffer(ctx->output_queue, buffer, 0U, NULL);
        if (status != noErr) {
            release_output_buffer(ctx, index);
            QM_LOGE(LOG_TAG, "output enqueue failed status=%d", (int)status);
            (void)stop_playback(ctx);
            return -QM_EIO;
        }

        pthread_mutex_lock(&ctx->output_lock);
        start_queue = !ctx->output_started;
        if (start_queue) {
            ctx->output_started = QM_TRUE;
        }
        pthread_mutex_unlock(&ctx->output_lock);
        if (start_queue) {
            status = AudioQueueStart(ctx->output_queue, NULL);
            if (status != noErr) {
                pthread_mutex_lock(&ctx->output_lock);
                ctx->output_started = QM_FALSE;
                pthread_mutex_unlock(&ctx->output_lock);
                QM_LOGE(LOG_TAG, "AudioQueueStart output failed status=%d", (int)status);
                (void)stop_playback(ctx);
                return -QM_EIO;
            }
        }

        cursor += chunk;
        remaining -= chunk;
    }
    return (int)len;
}

static int stop_playback(macos_audio_ctx_t *ctx)
{
    uint32_t index;
    OSStatus status;

    if (ctx->output_queue == NULL) {
        return -QM_ENOSYS;
    }

    pthread_mutex_lock(&ctx->output_lock);
    ctx->output_resetting = QM_TRUE;
    pthread_mutex_unlock(&ctx->output_lock);

    status = AudioQueueStop(ctx->output_queue, true);
    if (status == noErr) {
        status = AudioQueueReset(ctx->output_queue);
    }

    pthread_mutex_lock(&ctx->output_lock);
    for (index = 0U; index < CONFIG_QM_MACOS_AUDIO_OUTPUT_BUFFER_COUNT; ++index) {
        ctx->output_buffer_busy[index] = QM_FALSE;
    }
    ctx->output_started = QM_FALSE;
    ctx->output_resetting = QM_FALSE;
    pthread_cond_broadcast(&ctx->output_cond);
    pthread_mutex_unlock(&ctx->output_lock);

    if (status != noErr) {
        QM_LOGE(LOG_TAG, "stop playback failed status=%d", (int)status);
        return -QM_EIO;
    }
    return QM_EOK;
}

static int set_capture_policy(macos_audio_ctx_t *ctx, qm_audio_capture_policy_t policy)
{
    qm_bool should_pause;
    qm_bool queue_started;
    OSStatus status = noErr;

    if (ctx->input_queue == NULL) {
        return -QM_ENOSYS;
    }
    if (policy != QM_AUDIO_CAPTURE_POLICY_AUTO &&
        policy != QM_AUDIO_CAPTURE_POLICY_KEEP &&
        policy != QM_AUDIO_CAPTURE_POLICY_PAUSE) {
        return -QM_EINVAL;
    }

    should_pause = policy == QM_AUDIO_CAPTURE_POLICY_PAUSE ? QM_TRUE : QM_FALSE;
    pthread_mutex_lock(&ctx->capture_lock);
    ctx->capture_paused = should_pause;
    queue_started = ctx->input_started;
    if (should_pause) {
        (void)qm_ringbuf_flush(&ctx->capture_ring);
        ctx->capture_count = 0U;
    }
    pthread_cond_broadcast(&ctx->capture_cond);
    pthread_mutex_unlock(&ctx->capture_lock);

    if (queue_started) {
        status = should_pause ? AudioQueuePause(ctx->input_queue) :
                                AudioQueueStart(ctx->input_queue, NULL);
    }
    return status == noErr ? QM_EOK : -QM_EIO;
}

static int set_volume(macos_audio_ctx_t *ctx, uint32_t volume)
{
    Float32 scalar;
    OSStatus status;

    if (ctx->output_queue == NULL) {
        return -QM_ENOSYS;
    }
    if (volume > QM_MACOS_AUDIO_VOLUME_MAX) {
        return -QM_EINVAL;
    }
    pthread_mutex_lock(&ctx->output_lock);
    ctx->volume = volume;
    scalar = ctx->muted ? 0.0F : (Float32)volume / (Float32)QM_MACOS_AUDIO_VOLUME_MAX;
    pthread_mutex_unlock(&ctx->output_lock);

    status = AudioQueueSetParameter(ctx->output_queue, kAudioQueueParam_Volume, scalar);
    return status == noErr ? QM_EOK : -QM_EIO;
}

static int set_mute(macos_audio_ctx_t *ctx, qm_bool muted)
{
    Float32 scalar;
    OSStatus status;

    if (ctx->output_queue == NULL) {
        return -QM_ENOSYS;
    }
    pthread_mutex_lock(&ctx->output_lock);
    ctx->muted = muted ? QM_TRUE : QM_FALSE;
    scalar = ctx->muted ? 0.0F :
             (Float32)ctx->volume / (Float32)QM_MACOS_AUDIO_VOLUME_MAX;
    pthread_mutex_unlock(&ctx->output_lock);

    status = AudioQueueSetParameter(ctx->output_queue, kAudioQueueParam_Volume, scalar);
    return status == noErr ? QM_EOK : -QM_EIO;
}

static int coreaudio_control(void *driver_ctx, qm_audio_ctrl_t ctrl, void *arg)
{
    macos_audio_ctx_t *ctx = (macos_audio_ctx_t *)driver_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }

    switch (ctrl) {
        case QM_AUDIO_CTRL_SET_VOLUME:
            return arg != NULL ? set_volume(ctx, *(const uint32_t *)arg) : -QM_EINVAL;
        case QM_AUDIO_CTRL_SET_MUTE:
            return arg != NULL ? set_mute(ctx, *(const qm_bool *)arg) : -QM_EINVAL;
        case QM_AUDIO_CTRL_STOP_PLAYBACK:
            return stop_playback(ctx);
        case QM_AUDIO_CTRL_SET_CAPTURE_POLICY:
            return arg != NULL ?
                   set_capture_policy(ctx, *(const qm_audio_capture_policy_t *)arg) :
                   -QM_EINVAL;
        default:
            return -QM_EINVAL;
    }
}

static const qm_audio_driver_if_t s_coreaudio_driver = {
    .name = "macos_coreaudio_pcm",
    .open = coreaudio_open,
    .close = coreaudio_close,
    .read = coreaudio_read,
    .write = coreaudio_write,
    .control = coreaudio_control,
};

const qm_audio_driver_if_t *qm_macos_coreaudio_driver(void)
{
    return &s_coreaudio_driver;
}
