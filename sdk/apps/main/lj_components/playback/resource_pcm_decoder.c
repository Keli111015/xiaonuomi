#include "resource_pcm_decoder.h"

#include "opus.h"
#include "qm.h"
#include "qm_log.h"

#include <string.h>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LOG_TAG "res_pcm"

#define RESOURCE_PCM_RATE             16000U
#define RESOURCE_PCM_FRAME_MS         20U
#define RESOURCE_PCM_SAMPLES          ((RESOURCE_PCM_RATE / 1000U) * RESOURCE_PCM_FRAME_MS)
#define RESOURCE_PCM_FRAME_BYTES      (RESOURCE_PCM_SAMPLES * sizeof(int16_t))
#define RESOURCE_OPUS_PACKET_BYTES    160U /* 64 kbps CBR at 20 ms */
#define RESOURCE_OPUS_BITRATE_BPS     ((RESOURCE_OPUS_PACKET_BYTES * 8U * 1000U) / RESOURCE_PCM_FRAME_MS)
#define RESOURCE_OPUS_MAX_PACKET      1500U
#define RESOURCE_DECODER_STACK_BYTES  (30 * 1024)
#define RESOURCE_DECODER_PRIORITY     20
#define RESOURCE_DECODER_STOP_MS      2000U

/* playback owns one qm_player instance, so one internal TCB is sufficient. */
static StaticTask_t s_decoder_task_tcb;

typedef struct {
    qm_player_decoder_read_fn read;
    void *io_ctx;
    qm_player_decoder_frame_sink_t sink;
    qm_player_decoder_event_cb_t event_cb;
    void *event_ctx;
    qm_audio_format_t format;
    OpusEncoder *encoder;
    qm_task_t task;
    StackType_t *task_stack;
    qm_sem_t done;
    qm_sem_t wake;
    qm_mutex_t lock;
    qm_bool task_started;
    qm_bool stop_requested;
    qm_bool paused;
    qm_bool first_packet_logged;
} resource_pcm_decoder_t;

static void notify_event(resource_pcm_decoder_t *decoder,
                         qm_player_decoder_event_type_t type,
                         int error)
{
    qm_player_decoder_event_t event;

    if (decoder == NULL || decoder->event_cb == NULL) {
        return;
    }
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.error = error;
    decoder->event_cb(&event, decoder->event_ctx);
}

static qm_bool should_stop(resource_pcm_decoder_t *decoder)
{
    qm_bool stop;

    qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
    stop = decoder->stop_requested;
    qm_mutex_unlock(&decoder->lock);
    return stop;
}

static int wait_resume(resource_pcm_decoder_t *decoder)
{
    for (;;) {
        qm_bool paused;
        qm_bool stop;

        qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
        paused = decoder->paused;
        stop = decoder->stop_requested;
        qm_mutex_unlock(&decoder->lock);
        if (stop) {
            return -QM_EINTR;
        }
        if (!paused) {
            return QM_EOK;
        }
        if (qm_sem_wait(&decoder->wake, QM_WAIT_FOREVER) != QM_EOK) {
            return -QM_EINTR;
        }
    }
}

static void decoder_task(void *arg)
{
    resource_pcm_decoder_t *decoder = (resource_pcm_decoder_t *)arg;
    uint8_t pcm[RESOURCE_PCM_FRAME_BYTES];
    uint8_t opus[RESOURCE_OPUS_MAX_PACKET];
    uint32_t pcm_len = 0U;
    qm_bool failed = QM_FALSE;

    while (!should_stop(decoder)) {
        int ret;
        int encoded;
        qm_player_pcm_frame_t frame;

        ret = wait_resume(decoder);
        if (ret != QM_EOK) {
            break;
        }
        ret = decoder->read(decoder->io_ctx,
                            pcm + pcm_len,
                            RESOURCE_PCM_FRAME_BYTES - pcm_len,
                            QM_WAIT_FOREVER);
        if (ret == -QM_EINTR) {
            break;
        }
        if (ret == -QM_ETIMEOUT) {
            continue;
        }
        if (ret < 0) {
            QM_LOGW(LOG_TAG, "source read failed ret=%d", ret);
            failed = QM_TRUE;
            break;
        }
        if (ret == 0) {
            if (pcm_len != 0U) {
                QM_LOGW(LOG_TAG, "discard incomplete pcm frame len=%u",
                        (unsigned)pcm_len);
            }
            break;
        }
        pcm_len += (uint32_t)ret;
        if (pcm_len < RESOURCE_PCM_FRAME_BYTES) {
            continue;
        }

        encoded = opus_encode(decoder->encoder,
                              (const opus_int16 *)pcm,
                              RESOURCE_PCM_SAMPLES,
                              opus,
                              (opus_int32)sizeof(opus));
        pcm_len = 0U;
        if (encoded < 0) {
            QM_LOGE(LOG_TAG, "opus encode failed err=%d", encoded);
            failed = QM_TRUE;
            break;
        }
        if (!decoder->first_packet_logged) {
            decoder->first_packet_logged = QM_TRUE;
            QM_LOGI(LOG_TAG, "resource opus first packet len=%d expected=%u",
                    encoded, (unsigned int)RESOURCE_OPUS_PACKET_BYTES);
        }
        if (encoded != (int)RESOURCE_OPUS_PACKET_BYTES) {
            QM_LOGE(LOG_TAG, "resource opus packet size mismatch len=%d expected=%u",
                    encoded, (unsigned int)RESOURCE_OPUS_PACKET_BYTES);
            failed = QM_TRUE;
            break;
        }

        memset(&frame, 0, sizeof(frame));
        frame.data = opus;
        frame.len = (uint32_t)encoded;
        frame.sample_count = RESOURCE_PCM_SAMPLES;
        frame.pts_ms = QM_PLAYER_PTS_UNKNOWN;
        ret = decoder->sink.push(decoder->sink.ctx, &frame);
        if (ret != QM_EOK) {
            if (ret != -QM_EINTR) {
                QM_LOGW(LOG_TAG, "frame push failed ret=%d", ret);
                failed = QM_TRUE;
            }
            break;
        }
    }

    if (!should_stop(decoder)) {
        notify_event(decoder,
                     failed ? QM_PLAYER_DECODER_EVENT_ERROR : QM_PLAYER_DECODER_EVENT_END,
                     failed ? -QM_EIO : QM_EOK);
    }

    /* Keep the session alive until decoder_stop has acknowledged the task.
     * The player may finalize immediately after the terminal event. */
    while (!should_stop(decoder)) {
        if (qm_sem_wait(&decoder->wake, QM_WAIT_FOREVER) != QM_EOK) {
            break;
        }
    }
    qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
    decoder->task_started = QM_FALSE;
    qm_mutex_unlock(&decoder->lock);
    (void)qm_sem_signal(&decoder->done);
    qm_task_exit(NULL);
}

static int decoder_open(void **session, const qm_player_decoder_config_t *config)
{
    resource_pcm_decoder_t *decoder;
    int opus_error = OPUS_OK;

    if (session == NULL || config == NULL || config->read == NULL ||
        config->frame_sink.push == NULL) {
        return -QM_EINVAL;
    }
    decoder = (resource_pcm_decoder_t *)qm_malloc(sizeof(*decoder));
    if (decoder == NULL) {
        return -QM_ENOMEM;
    }
    memset(decoder, 0, sizeof(*decoder));
    decoder->read = config->read;
    decoder->io_ctx = config->io_ctx;
    decoder->sink = config->frame_sink;
    decoder->event_cb = config->event_cb;
    decoder->event_ctx = config->event_ctx;
    decoder->format.codec = QM_AUDIO_CODEC_OPUS;
    decoder->format.data_type = QM_AUDIO_CODEC_OPUS;
    decoder->format.sample_rate = RESOURCE_PCM_RATE;
    decoder->format.channel_num = 1U;
    decoder->format.bits_per_sample = 16U;
    decoder->format.frame_ms = RESOURCE_PCM_FRAME_MS;

    if (qm_mutex_new(&decoder->lock) != QM_EOK ||
        qm_sem_new(&decoder->done, 0) != QM_EOK ||
        qm_sem_new(&decoder->wake, 0) != QM_EOK ||
        !qm_mutex_is_valid(&decoder->lock) ||
        !qm_sem_is_valid(&decoder->done) ||
        !qm_sem_is_valid(&decoder->wake)) {
        QM_LOGE(LOG_TAG, "decoder synchronization init failed");
        if (qm_sem_is_valid(&decoder->wake)) (void)qm_sem_free(&decoder->wake);
        if (qm_sem_is_valid(&decoder->done)) (void)qm_sem_free(&decoder->done);
        if (qm_mutex_is_valid(&decoder->lock)) (void)qm_mutex_free(&decoder->lock);
        qm_free(decoder);
        return -QM_ENOMEM;
    }
    decoder->encoder = opus_encoder_create((int)RESOURCE_PCM_RATE, 1,
                                           OPUS_APPLICATION_AUDIO,
                                           &opus_error);
    if (decoder->encoder == NULL || opus_error != OPUS_OK) {
        QM_LOGE(LOG_TAG, "opus encoder create failed err=%d", opus_error);
        (void)qm_sem_free(&decoder->wake);
        (void)qm_sem_free(&decoder->done);
        (void)qm_mutex_free(&decoder->lock);
        qm_free(decoder);
        return -QM_EINIT;
    }
    (void)opus_encoder_ctl(decoder->encoder, OPUS_SET_COMPLEXITY(0));
    (void)opus_encoder_ctl(decoder->encoder, OPUS_SET_VBR(0));
    (void)opus_encoder_ctl(decoder->encoder,
                           OPUS_SET_BITRATE((int)RESOURCE_OPUS_BITRATE_BPS));
    *session = decoder;
    return QM_EOK;
}

static int decoder_get_format(void *session, qm_audio_format_t *format)
{
    resource_pcm_decoder_t *decoder = (resource_pcm_decoder_t *)session;

    if (decoder == NULL || format == NULL) {
        return -QM_EINVAL;
    }
    *format = decoder->format;
    return QM_EOK;
}

static int decoder_start(void *session)
{
    resource_pcm_decoder_t *decoder = (resource_pcm_decoder_t *)session;

    if (decoder == NULL) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
    if (decoder->task_started) {
        qm_mutex_unlock(&decoder->lock);
        return -QM_EBUSY;
    }
    decoder->stop_requested = QM_FALSE;
    decoder->paused = QM_FALSE;
    decoder->task_started = QM_TRUE;
    qm_mutex_unlock(&decoder->lock);
    if (decoder->task_stack == NULL) {
        decoder->task_stack = (StackType_t *)heap_caps_malloc(
            RESOURCE_DECODER_STACK_BYTES,
            MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (decoder->task_stack == NULL) {
            QM_LOGE(LOG_TAG, "decoder task stack alloc failed bytes=%u",
                    (unsigned int)RESOURCE_DECODER_STACK_BYTES);
            qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
            decoder->task_started = QM_FALSE;
            qm_mutex_unlock(&decoder->lock);
            return -QM_ENOMEM;
        }
    }
    decoder->task.hdl = xTaskCreateStatic(
        decoder_task,
        "res_pcm",
        RESOURCE_DECODER_STACK_BYTES / sizeof(StackType_t),
        decoder,
        RESOURCE_DECODER_PRIORITY,
        decoder->task_stack,
        &s_decoder_task_tcb);
    if (decoder->task.hdl == NULL) {
        QM_LOGE(LOG_TAG, "decoder task create failed stack=%u",
                (unsigned int)RESOURCE_DECODER_STACK_BYTES);
        heap_caps_free(decoder->task_stack);
        decoder->task_stack = NULL;
        qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
        decoder->task_started = QM_FALSE;
        qm_mutex_unlock(&decoder->lock);
        return -QM_ENOMEM;
    }
    QM_LOGI(LOG_TAG, "decoder task started stack=%u alloc=psram",
            (unsigned int)RESOURCE_DECODER_STACK_BYTES);
    return QM_EOK;
}

static int decoder_pause(void *session)
{
    resource_pcm_decoder_t *decoder = (resource_pcm_decoder_t *)session;

    if (decoder == NULL) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
    decoder->paused = QM_TRUE;
    qm_mutex_unlock(&decoder->lock);
    return QM_EOK;
}

static int decoder_resume(void *session)
{
    resource_pcm_decoder_t *decoder = (resource_pcm_decoder_t *)session;

    if (decoder == NULL) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
    decoder->paused = QM_FALSE;
    qm_mutex_unlock(&decoder->lock);
    (void)qm_sem_signal(&decoder->wake);
    return QM_EOK;
}

static int decoder_stop(void *session)
{
    resource_pcm_decoder_t *decoder = (resource_pcm_decoder_t *)session;
    qm_bool started;

    if (decoder == NULL) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&decoder->lock, QM_WAIT_FOREVER);
    decoder->stop_requested = QM_TRUE;
    started = decoder->task_started;
    qm_mutex_unlock(&decoder->lock);
    (void)qm_sem_signal(&decoder->wake);
    if (started && qm_sem_wait(&decoder->done, RESOURCE_DECODER_STOP_MS) != QM_EOK) {
        QM_LOGW(LOG_TAG, "decoder task stop timeout");
        return -QM_ETIMEOUT;
    }
    return QM_EOK;
}

static int decoder_close(void *session)
{
    resource_pcm_decoder_t *decoder = (resource_pcm_decoder_t *)session;
    int ret;

    if (decoder == NULL) {
        return -QM_EINVAL;
    }
    ret = decoder_stop(decoder);
    if (ret != QM_EOK) {
        return ret;
    }
    if (decoder->encoder != NULL) {
        opus_encoder_destroy(decoder->encoder);
    }
    if (decoder->task_stack != NULL) {
        heap_caps_free(decoder->task_stack);
        decoder->task_stack = NULL;
    }
    (void)qm_sem_free(&decoder->wake);
    (void)qm_sem_free(&decoder->done);
    (void)qm_mutex_free(&decoder->lock);
    qm_free(decoder);
    return QM_EOK;
}

static int decoder_input_eof(void *session)
{
    return session != NULL ? QM_EOK : -QM_EINVAL;
}

static const qm_player_decoder_if_t s_decoder_if = {
    .name = "resource_pcm_opus",
    .open = decoder_open,
    .get_format = decoder_get_format,
    .start = decoder_start,
    .pause = decoder_pause,
    .resume = decoder_resume,
    .stop = decoder_stop,
    .close = decoder_close,
    .input_eof = decoder_input_eof,
};

const qm_player_decoder_if_t *resource_pcm_decoder_get_if(void)
{
    return &s_decoder_if;
}
