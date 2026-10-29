#include "qm_audio_voiceio.h"

#include "qm_errno.h"
#include "qm_log.h"
#include "qm_ringbuf.h"
#include "qm_audio_permission.h"

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define LOG_TAG "macos voiceio"

#define VOICEIO_CAPTURE_RING_FRAMES 8U
#define VOICEIO_PLAYBACK_RING_FRAMES 12U
#define VOICEIO_MAX_RENDER_FRAMES 4096U
#define VOICEIO_VOLUME_MAX 100U

#ifndef CONFIG_QM_MACOS_AUDIO_PERMISSION_TIMEOUT_MS
#define CONFIG_QM_MACOS_AUDIO_PERMISSION_TIMEOUT_MS 30000U
#endif

typedef struct {
    AudioUnit unit;
    qm_bool unit_initialized;
    qm_bool unit_started;
    pthread_mutex_t capture_lock;
    pthread_cond_t capture_cond;
    pthread_mutex_t playback_lock;
    pthread_cond_t playback_cond;
    qm_bool capture_lock_ready;
    qm_bool capture_cond_ready;
    qm_bool playback_lock_ready;
    qm_bool playback_cond_ready;

    qm_ringbuf_t capture_ring;
    qm_ringbuf_t playback_ring;
    uint8_t *capture_storage;
    uint8_t *capture_discard;
    uint8_t *playback_storage;
    uint8_t *render_storage;
    uint32_t capture_capacity;
    uint32_t playback_capacity;
    uint32_t capture_count;
    uint32_t playback_count;
    uint32_t capture_frame_bytes;
    uint32_t playback_sample_bytes;
    uint32_t max_render_bytes;
    qm_bool capture_paused;
    qm_bool closing;
    int capture_error;
    uint32_t volume;
    qm_bool muted;
} voiceio_ctx_t;

static int build_format(const qm_audio_format_t *input,
                        const qm_audio_format_t *output,
                        AudioStreamBasicDescription *description,
                        uint32_t *capture_frame_bytes,
                        uint32_t *sample_bytes)
{
    uint64_t bytes_per_sample;
    uint64_t bytes_per_frame;

    if (input == NULL || output == NULL || description == NULL ||
        capture_frame_bytes == NULL || sample_bytes == NULL ||
        input->codec != QM_AUDIO_CODEC_PCM ||
        (input->data_type != QM_AUDIO_CODEC_NONE &&
         input->data_type != QM_AUDIO_CODEC_PCM) ||
        output->codec != QM_AUDIO_CODEC_PCM ||
        (output->data_type != QM_AUDIO_CODEC_NONE &&
         output->data_type != QM_AUDIO_CODEC_PCM) ||
        input->sample_rate == 0U || input->sample_rate != output->sample_rate ||
        input->channel_num != 1U || output->channel_num != 1U ||
        input->bits_per_sample != 16U || output->bits_per_sample != 16U ||
        input->frame_ms == 0U || output->frame_ms == 0U) {
        return -QM_EINVAL;
    }

    bytes_per_sample = input->bits_per_sample / 8U;
    bytes_per_frame = (uint64_t)input->sample_rate * bytes_per_sample * input->frame_ms;
    if (bytes_per_frame == 0U || (bytes_per_frame % 1000U) != 0U ||
        bytes_per_frame / 1000U > UINT32_MAX) {
        return -QM_EINVAL;
    }

    memset(description, 0, sizeof(*description));
    description->mSampleRate = input->sample_rate;
    description->mFormatID = kAudioFormatLinearPCM;
    description->mFormatFlags = kLinearPCMFormatFlagIsSignedInteger |
                                 kLinearPCMFormatFlagIsPacked |
                                 kAudioFormatFlagsNativeEndian;
    description->mBytesPerPacket = (UInt32)bytes_per_sample;
    description->mFramesPerPacket = 1U;
    description->mBytesPerFrame = (UInt32)bytes_per_sample;
    description->mChannelsPerFrame = 1U;
    description->mBitsPerChannel = 16U;
    *capture_frame_bytes = (uint32_t)(bytes_per_frame / 1000U);
    *sample_bytes = (uint32_t)bytes_per_sample;
    return QM_EOK;
}

static void signal_capture_error(voiceio_ctx_t *ctx, int error)
{
    pthread_mutex_lock(&ctx->capture_lock);
    if (!ctx->closing) {
        ctx->capture_error = error;
    }
    pthread_cond_broadcast(&ctx->capture_cond);
    pthread_mutex_unlock(&ctx->capture_lock);
}

static void capture_push(voiceio_ctx_t *ctx, const uint8_t *data, uint32_t len)
{
    uint32_t free_bytes;

    pthread_mutex_lock(&ctx->capture_lock);
    if (ctx->closing || ctx->capture_paused || len == 0U) {
        pthread_mutex_unlock(&ctx->capture_lock);
        return;
    }
    if (len > ctx->capture_capacity) {
        data += len - ctx->capture_capacity;
        len = ctx->capture_capacity;
    }
    free_bytes = ctx->capture_capacity - ctx->capture_count;
    while (free_bytes < len && ctx->capture_count > 0U) {
        uint32_t drop = ctx->capture_count < ctx->capture_frame_bytes ?
                        ctx->capture_count : ctx->capture_frame_bytes;
        int popped = qm_ringbuf_pop(&ctx->capture_ring, ctx->capture_discard, (int)drop);
        if (popped <= 0) {
            break;
        }
        ctx->capture_count -= (uint32_t)popped;
        free_bytes = ctx->capture_capacity - ctx->capture_count;
    }
    if (free_bytes >= len) {
        int pushed = qm_ringbuf_push(&ctx->capture_ring, (uint8_t *)data, (int)len);
        if (pushed > 0) {
            ctx->capture_count += (uint32_t)pushed;
            pthread_cond_signal(&ctx->capture_cond);
        }
    }
    pthread_mutex_unlock(&ctx->capture_lock);
}

static OSStatus voiceio_input_callback(void *user_data,
                                       AudioUnitRenderActionFlags *flags,
                                       const AudioTimeStamp *time,
                                       UInt32 bus,
                                       UInt32 frames,
                                       AudioBufferList *io_data)
{
    voiceio_ctx_t *ctx = (voiceio_ctx_t *)user_data;
    AudioBufferList list;
    OSStatus status;

    (void)flags;
    (void)time;
    (void)bus;
    (void)io_data;
    if (ctx == NULL || frames == 0U ||
        (uint64_t)frames * ctx->playback_sample_bytes > ctx->max_render_bytes) {
        return noErr;
    }

    memset(&list, 0, sizeof(list));
    list.mNumberBuffers = 1U;
    list.mBuffers[0].mNumberChannels = 1U;
    list.mBuffers[0].mDataByteSize = frames * ctx->playback_sample_bytes;
    list.mBuffers[0].mData = ctx->render_storage;
    status = AudioUnitRender(ctx->unit,
                             flags,
                             time,
                             1U,
                             frames,
                             &list);
    if (status == noErr) {
        capture_push(ctx, (const uint8_t *)ctx->render_storage,
                     list.mBuffers[0].mDataByteSize);
    } else {
        signal_capture_error(ctx, -QM_EIO);
    }
    return status;
}

static OSStatus voiceio_render_callback(void *user_data,
                                        AudioUnitRenderActionFlags *flags,
                                        const AudioTimeStamp *time,
                                        UInt32 bus,
                                        UInt32 frames,
                                        AudioBufferList *io_data)
{
    voiceio_ctx_t *ctx = (voiceio_ctx_t *)user_data;
    uint32_t requested;
    uint32_t copied = 0U;
    uint32_t volume;
    qm_bool muted;
    uint8_t *output;

    (void)flags;
    (void)time;
    (void)bus;
    if (ctx == NULL || io_data == NULL || io_data->mNumberBuffers == 0U ||
        io_data->mBuffers[0].mData == NULL) {
        return kAudio_ParamError;
    }
    requested = frames * ctx->playback_sample_bytes;
    output = (uint8_t *)io_data->mBuffers[0].mData;
    memset(output, 0, requested);

    pthread_mutex_lock(&ctx->playback_lock);
    volume = ctx->volume;
    muted = ctx->muted;
    if (!ctx->closing && !muted && ctx->playback_count > 0U) {
        copied = ctx->playback_count < requested ? ctx->playback_count : requested;
        copied = (uint32_t)qm_ringbuf_pop(&ctx->playback_ring, output, (int)copied);
        ctx->playback_count -= copied;
        if (copied > 0U) {
            pthread_cond_broadcast(&ctx->playback_cond);
        }
    }
    pthread_mutex_unlock(&ctx->playback_lock);

    if (!muted && volume < VOICEIO_VOLUME_MAX && copied > 0U) {
        int16_t *samples = (int16_t *)output;
        uint32_t sample_count = copied / sizeof(*samples);
        uint32_t index;
        for (index = 0U; index < sample_count; ++index) {
            samples[index] = (int16_t)(((int32_t)samples[index] *
                                       (int32_t)volume) /
                                       (int32_t)VOICEIO_VOLUME_MAX);
        }
    }
    io_data->mBuffers[0].mDataByteSize = requested;
    return noErr;
}

static void voiceio_destroy(voiceio_ctx_t *ctx)
{
    if (ctx == NULL) {
        return;
    }
    if (ctx->unit != NULL) {
        if (ctx->unit_started) {
            (void)AudioOutputUnitStop(ctx->unit);
        }
        if (ctx->unit_initialized) {
            (void)AudioUnitUninitialize(ctx->unit);
        }
        AudioComponentInstanceDispose(ctx->unit);
    }
    if (ctx->playback_cond_ready) {
        pthread_cond_destroy(&ctx->playback_cond);
    }
    if (ctx->playback_lock_ready) {
        pthread_mutex_destroy(&ctx->playback_lock);
    }
    if (ctx->capture_cond_ready) {
        pthread_cond_destroy(&ctx->capture_cond);
    }
    if (ctx->capture_lock_ready) {
        pthread_mutex_destroy(&ctx->capture_lock);
    }
    free(ctx->render_storage);
    free(ctx->playback_storage);
    free(ctx->capture_discard);
    free(ctx->capture_storage);
    free(ctx);
}

static int voiceio_open(void **driver_ctx,
                        const qm_audio_format_t *input_format,
                        const qm_audio_format_t *output_format,
                        qm_audio_event_cb_t event_cb,
                        void *user_ctx)
{
    AudioComponentDescription component_desc;
    AudioStreamBasicDescription stream_format;
    AURenderCallbackStruct callback;
    AudioComponent component;
    UInt32 enable = 1U;
    UInt32 bypass = 0U;
    UInt32 max_frames = VOICEIO_MAX_RENDER_FRAMES;
    uint32_t capture_frame_bytes;
    uint32_t sample_bytes;
    voiceio_ctx_t *ctx;
    uint64_t capture_storage_size;
    uint64_t playback_storage_size;
    OSStatus status;
    int ret;

    (void)event_cb;
    (void)user_ctx;
    if (driver_ctx == NULL) {
        return -QM_EINVAL;
    }
    ret = build_format(input_format,
                       output_format,
                       &stream_format,
                       &capture_frame_bytes,
                       &sample_bytes);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_macos_microphone_authorize(CONFIG_QM_MACOS_AUDIO_PERMISSION_TIMEOUT_MS);
    if (ret != QM_EOK) {
        return ret;
    }

    ctx = (voiceio_ctx_t *)calloc(1U, sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    ctx->capture_frame_bytes = capture_frame_bytes;
    ctx->playback_sample_bytes = sample_bytes;
    ctx->max_render_bytes = VOICEIO_MAX_RENDER_FRAMES * sample_bytes;
    ctx->volume = VOICEIO_VOLUME_MAX;

    capture_storage_size = (uint64_t)ctx->capture_frame_bytes *
                           VOICEIO_CAPTURE_RING_FRAMES + 1U;
    playback_storage_size = (uint64_t)ctx->capture_frame_bytes *
                            VOICEIO_PLAYBACK_RING_FRAMES + 1U;
    ctx->capture_storage = (uint8_t *)calloc(1U, (size_t)capture_storage_size);
    ctx->capture_discard = (uint8_t *)malloc(ctx->capture_frame_bytes);
    ctx->render_storage = (uint8_t *)calloc(1U, ctx->max_render_bytes);
    ctx->playback_ring = (qm_ringbuf_t){0};
    if (ctx->capture_storage == NULL || ctx->capture_discard == NULL ||
        ctx->render_storage == NULL || playback_storage_size > UINT32_MAX ||
        capture_storage_size > UINT32_MAX) {
        voiceio_destroy(ctx);
        return -QM_ENOMEM;
    }
    ctx->playback_storage = (uint8_t *)calloc(1U, (size_t)playback_storage_size);
    if (ctx->playback_storage == NULL ||
        qm_ringbuf_init(&ctx->capture_ring, ctx->capture_storage,
                        (uint32_t)capture_storage_size) != QM_EOK ||
        qm_ringbuf_init(&ctx->playback_ring, ctx->playback_storage,
                        (uint32_t)playback_storage_size) != QM_EOK) {
        voiceio_destroy(ctx);
        return -QM_ENOMEM;
    }
    ctx->capture_capacity = (uint32_t)capture_storage_size - 1U;
    ctx->playback_capacity = (uint32_t)playback_storage_size - 1U;

    if (pthread_mutex_init(&ctx->capture_lock, NULL) != 0) {
        voiceio_destroy(ctx);
        return -QM_EINIT;
    }
    ctx->capture_lock_ready = QM_TRUE;
    if (pthread_cond_init(&ctx->capture_cond, NULL) != 0) {
        voiceio_destroy(ctx);
        return -QM_EINIT;
    }
    ctx->capture_cond_ready = QM_TRUE;
    if (pthread_mutex_init(&ctx->playback_lock, NULL) != 0) {
        voiceio_destroy(ctx);
        return -QM_EINIT;
    }
    ctx->playback_lock_ready = QM_TRUE;
    if (pthread_cond_init(&ctx->playback_cond, NULL) != 0) {
        voiceio_destroy(ctx);
        return -QM_EINIT;
    }
    ctx->playback_cond_ready = QM_TRUE;

    memset(&component_desc, 0, sizeof(component_desc));
    component_desc.componentType = kAudioUnitType_Output;
    component_desc.componentSubType = kAudioUnitSubType_VoiceProcessingIO;
    component_desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    component = AudioComponentFindNext(NULL, &component_desc);
    if (component == NULL || AudioComponentInstanceNew(component, &ctx->unit) != noErr) {
        voiceio_destroy(ctx);
        return -QM_ENOSYS;
    }

    status = AudioUnitSetProperty(ctx->unit,
                                  kAudioOutputUnitProperty_EnableIO,
                                  kAudioUnitScope_Input,
                                  1U,
                                  &enable,
                                  sizeof(enable));
    if (status != noErr) {
        voiceio_destroy(ctx);
        return -QM_EIO;
    }
    status = AudioUnitSetProperty(ctx->unit,
                                  kAudioUnitProperty_StreamFormat,
                                  kAudioUnitScope_Input,
                                  0U,
                                  &stream_format,
                                  sizeof(stream_format));
    if (status == noErr) {
        status = AudioUnitSetProperty(ctx->unit,
                                      kAudioUnitProperty_StreamFormat,
                                      kAudioUnitScope_Output,
                                      1U,
                                      &stream_format,
                                      sizeof(stream_format));
    }
    if (status == noErr) {
        status = AudioUnitSetProperty(ctx->unit,
                                      kAUVoiceIOProperty_BypassVoiceProcessing,
                                      kAudioUnitScope_Global,
                                      0U,
                                      &bypass,
                                      sizeof(bypass));
    }
    if (status != noErr) {
        voiceio_destroy(ctx);
        return -QM_EIO;
    }

    memset(&callback, 0, sizeof(callback));
    callback.inputProc = voiceio_input_callback;
    callback.inputProcRefCon = ctx;
    status = AudioUnitSetProperty(ctx->unit,
                                  kAudioOutputUnitProperty_SetInputCallback,
                                  kAudioUnitScope_Global,
                                  1U,
                                  &callback,
                                  sizeof(callback));
    if (status == noErr) {
        callback.inputProc = voiceio_render_callback;
        status = AudioUnitSetProperty(ctx->unit,
                                      kAudioUnitProperty_SetRenderCallback,
                                      kAudioUnitScope_Input,
                                      0U,
                                      &callback,
                                      sizeof(callback));
    }
    if (status == noErr) {
        status = AudioUnitSetProperty(ctx->unit,
                                      kAudioUnitProperty_MaximumFramesPerSlice,
                                      kAudioUnitScope_Global,
                                      0U,
                                      &max_frames,
                                      sizeof(max_frames));
    }
    if (status == noErr) {
        status = AudioUnitInitialize(ctx->unit);
        if (status == noErr) {
            ctx->unit_initialized = QM_TRUE;
        }
    }
    if (status == noErr) {
        status = AudioOutputUnitStart(ctx->unit);
        if (status == noErr) {
            ctx->unit_started = QM_TRUE;
        }
    }
    if (status != noErr) {
        QM_LOGE(LOG_TAG, "VoiceProcessingIO start failed status=%d", (int)status);
        voiceio_destroy(ctx);
        return -QM_EIO;
    }
    *driver_ctx = ctx;
    QM_LOGI(LOG_TAG, "VoiceProcessingIO ready; echo cancellation enabled");
    return QM_EOK;
}

static int voiceio_close(void *driver_ctx)
{
    voiceio_ctx_t *ctx = (voiceio_ctx_t *)driver_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    pthread_mutex_lock(&ctx->capture_lock);
    ctx->closing = QM_TRUE;
    pthread_cond_broadcast(&ctx->capture_cond);
    pthread_mutex_unlock(&ctx->capture_lock);
    pthread_mutex_lock(&ctx->playback_lock);
    pthread_cond_broadcast(&ctx->playback_cond);
    pthread_mutex_unlock(&ctx->playback_lock);
    voiceio_destroy(ctx);
    return QM_EOK;
}

static int voiceio_read(void *driver_ctx, void *data, uint32_t len, uint32_t timeout_ms)
{
    voiceio_ctx_t *ctx = (voiceio_ctx_t *)driver_ctx;
    int wait_ret;
    int popped;

    if (ctx == NULL || data == NULL || len < ctx->capture_frame_bytes) {
        return -QM_EINVAL;
    }
    pthread_mutex_lock(&ctx->capture_lock);
    while (ctx->capture_count < ctx->capture_frame_bytes &&
           !ctx->closing && ctx->capture_error == QM_EOK) {
        if (timeout_ms == 0U) {
            pthread_mutex_unlock(&ctx->capture_lock);
            return 0;
        }
        if (timeout_ms == UINT32_MAX) {
            wait_ret = pthread_cond_wait(&ctx->capture_cond, &ctx->capture_lock);
        } else {
            struct timespec deadline;
            clock_gettime(CLOCK_REALTIME, &deadline);
            deadline.tv_sec += timeout_ms / 1000U;
            deadline.tv_nsec += (long)(timeout_ms % 1000U) * 1000000L;
            if (deadline.tv_nsec >= 1000000000L) {
                deadline.tv_sec++;
                deadline.tv_nsec -= 1000000000L;
            }
            wait_ret = pthread_cond_timedwait(&ctx->capture_cond,
                                              &ctx->capture_lock,
                                              &deadline);
        }
        if (wait_ret == ETIMEDOUT) {
            pthread_mutex_unlock(&ctx->capture_lock);
            return 0;
        }
        if (wait_ret != 0) {
            pthread_mutex_unlock(&ctx->capture_lock);
            return -QM_EIO;
        }
    }
    if (ctx->closing) {
        pthread_mutex_unlock(&ctx->capture_lock);
        return -QM_EINTR;
    }
    if (ctx->capture_error != QM_EOK) {
        int error = ctx->capture_error;
        pthread_mutex_unlock(&ctx->capture_lock);
        return error;
    }
    popped = qm_ringbuf_pop(&ctx->capture_ring, data, (int)ctx->capture_frame_bytes);
    if (popped > 0) {
        ctx->capture_count -= (uint32_t)popped;
    }
    pthread_mutex_unlock(&ctx->capture_lock);
    return popped == (int)ctx->capture_frame_bytes ? popped : -QM_EIO;
}

static int voiceio_write(void *driver_ctx, const void *data, uint32_t len)
{
    voiceio_ctx_t *ctx = (voiceio_ctx_t *)driver_ctx;
    const uint8_t *cursor = (const uint8_t *)data;
    uint32_t remaining = len;

    if (ctx == NULL || data == NULL || len == 0U ||
        (len % ctx->playback_sample_bytes) != 0U) {
        return -QM_EINVAL;
    }
    while (remaining > 0U) {
        uint32_t free_bytes;
        uint32_t chunk;
        int pushed;

        pthread_mutex_lock(&ctx->playback_lock);
        while (!ctx->closing && ctx->playback_count == ctx->playback_capacity) {
            if (pthread_cond_wait(&ctx->playback_cond, &ctx->playback_lock) != 0) {
                pthread_mutex_unlock(&ctx->playback_lock);
                return -QM_EIO;
            }
        }
        if (ctx->closing) {
            pthread_mutex_unlock(&ctx->playback_lock);
            return -QM_EINTR;
        }
        free_bytes = ctx->playback_capacity - ctx->playback_count;
        chunk = free_bytes < remaining ? free_bytes : remaining;
        pushed = qm_ringbuf_push(&ctx->playback_ring, (uint8_t *)cursor, (int)chunk);
        if (pushed > 0) {
            ctx->playback_count += (uint32_t)pushed;
        }
        pthread_mutex_unlock(&ctx->playback_lock);
        if (pushed <= 0) {
            return -QM_EIO;
        }
        cursor += pushed;
        remaining -= (uint32_t)pushed;
    }
    return (int)len;
}

static int voiceio_control(void *driver_ctx, qm_audio_ctrl_t ctrl, void *arg)
{
    voiceio_ctx_t *ctx = (voiceio_ctx_t *)driver_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    switch (ctrl) {
        case QM_AUDIO_CTRL_SET_VOLUME:
            if (arg == NULL || *(const uint32_t *)arg > VOICEIO_VOLUME_MAX) {
                return -QM_EINVAL;
            }
            pthread_mutex_lock(&ctx->playback_lock);
            ctx->volume = *(const uint32_t *)arg;
            pthread_mutex_unlock(&ctx->playback_lock);
            return QM_EOK;
        case QM_AUDIO_CTRL_SET_MUTE:
            if (arg == NULL) {
                return -QM_EINVAL;
            }
            pthread_mutex_lock(&ctx->playback_lock);
            ctx->muted = *(const qm_bool *)arg ? QM_TRUE : QM_FALSE;
            pthread_mutex_unlock(&ctx->playback_lock);
            return QM_EOK;
        case QM_AUDIO_CTRL_STOP_PLAYBACK:
            pthread_mutex_lock(&ctx->playback_lock);
            (void)qm_ringbuf_flush(&ctx->playback_ring);
            ctx->playback_count = 0U;
            pthread_cond_broadcast(&ctx->playback_cond);
            pthread_mutex_unlock(&ctx->playback_lock);
            return QM_EOK;
        case QM_AUDIO_CTRL_SET_CAPTURE_POLICY:
            if (arg == NULL || *(const qm_audio_capture_policy_t *)arg <
                                  QM_AUDIO_CAPTURE_POLICY_AUTO ||
                *(const qm_audio_capture_policy_t *)arg >
                                  QM_AUDIO_CAPTURE_POLICY_PAUSE) {
                return -QM_EINVAL;
            }
            pthread_mutex_lock(&ctx->capture_lock);
            ctx->capture_paused = *(const qm_audio_capture_policy_t *)arg ==
                                  QM_AUDIO_CAPTURE_POLICY_PAUSE;
            if (ctx->capture_paused) {
                (void)qm_ringbuf_flush(&ctx->capture_ring);
                ctx->capture_count = 0U;
            }
            pthread_cond_broadcast(&ctx->capture_cond);
            pthread_mutex_unlock(&ctx->capture_lock);
            return QM_EOK;
        default:
            return -QM_EINVAL;
    }
}

static const qm_audio_driver_if_t s_voiceio_driver = {
    .name = "macos_voiceprocessingio_pcm",
    .open = voiceio_open,
    .close = voiceio_close,
    .read = voiceio_read,
    .write = voiceio_write,
    .control = voiceio_control,
};

const qm_audio_driver_if_t *qm_macos_voiceio_driver(void)
{
    return &s_voiceio_driver;
}
