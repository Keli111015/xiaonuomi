#include "adapter/qm_audio_codec.h"

#include "opus.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_string.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdint.h>

#define LOG_TAG "playback_enc"

#define PLAYBACK_ENCODER_RATE             16000U
#define PLAYBACK_ENCODER_CHANNELS         1U
#define PLAYBACK_ENCODER_FRAME_MS         20U
#define PLAYBACK_ENCODER_SAMPLES          ((PLAYBACK_ENCODER_RATE / 1000U) * PLAYBACK_ENCODER_FRAME_MS)
#define PLAYBACK_ENCODER_FRAME_BYTES      (PLAYBACK_ENCODER_SAMPLES * sizeof(int16_t) * PLAYBACK_ENCODER_CHANNELS)
#define PLAYBACK_ENCODER_MAX_PACKET_BYTES 255U
#define PLAYBACK_ENCODER_BITRATE          64000

typedef struct {
    OpusEncoder *opus;
    qm_audio_stream_format_t format;
    qm_audio_encoder_emit_packet_fn emit;
    void *emit_ctx;
    uint8_t *pcm;
    uint8_t *packet;
    uint32_t used;
    uint32_t sequence;
    uint64_t input_samples;
    uint64_t encoded_samples;
    int error;
    qm_bool sealed;
} playback_encoder_t;

int qm_audio_encoder_finish(void *ctx);

static int validate_format(const qm_audio_stream_format_t *format)
{
    if (format == NULL || format->codec != QM_AUDIO_CODEC_OPUS ||
        format->pcm.sample_rate_hz != PLAYBACK_ENCODER_RATE ||
        format->pcm.channels != PLAYBACK_ENCODER_CHANNELS ||
        format->pcm.sample != QM_AUDIO_PCM_S16 ||
        format->packet_duration_ms != PLAYBACK_ENCODER_FRAME_MS ||
        format->packet_bytes != 0U ||
        format->max_packet_bytes == 0U ||
        format->max_packet_bytes > PLAYBACK_ENCODER_MAX_PACKET_BYTES ||
        format->framing != QM_AUDIO_CODEC_FRAMING_STREAM) {
        return -QM_ENOSYS;
    }
    return QM_EOK;
}

int qm_audio_encoder_check_format(const qm_audio_stream_format_t *output)
{
    return validate_format(output);
}

static int emit_frame(playback_encoder_t *encoder)
{
    qm_audio_data_t packet;
    int encoded;

    encoded = opus_encode(encoder->opus,
                          (const opus_int16 *)encoder->pcm,
                          PLAYBACK_ENCODER_SAMPLES,
                          encoder->packet,
                          (opus_int32)encoder->format.max_packet_bytes);
    if (encoded <= 0 || (uint32_t)encoded > encoder->format.max_packet_bytes) {
        encoder->error = -QM_EIO;
        return encoder->error;
    }

    qm_memset(&packet, 0, sizeof(packet));
    packet.data = encoder->packet;
    packet.size = (uint32_t)encoded;
    packet.timestamp_ms = (uint32_t)(encoder->encoded_samples * 1000U /
                                     PLAYBACK_ENCODER_RATE);
    packet.duration_ms = PLAYBACK_ENCODER_FRAME_MS;
    packet.sequence = encoder->sequence++;
    packet.flags = QM_AUDIO_DATA_TIMESTAMP_VALID | QM_AUDIO_DATA_DURATION_VALID;
    encoder->used = 0U;
    encoder->encoded_samples += PLAYBACK_ENCODER_SAMPLES;
    if (encoder->emit(&packet, encoder->emit_ctx) != QM_EOK) {
        encoder->error = -QM_EINTR;
        return encoder->error;
    }
    if (encoder->sequence == 1U) {
        QM_LOGI(LOG_TAG, "resource opus first packet len=%d pcm=%u",
                encoded, (unsigned)PLAYBACK_ENCODER_FRAME_BYTES);
        /* The encoder runs on the shared audio_source worker. Record the
         * remaining headroom once, at the deepest point of the encode path,
         * so the stack size can be tuned from measured data. */
        QM_LOGI(LOG_TAG, "audio_source stack free=%u bytes",
                (unsigned)uxTaskGetStackHighWaterMark(NULL));
    }
    return QM_EOK;
}

int qm_audio_encoder_open(void **ctx,
                          const qm_audio_stream_format_t *output,
                          qm_audio_encoder_emit_packet_fn emit,
                          void *emit_ctx)
{
    playback_encoder_t *encoder;
    int opus_error = OPUS_OK;

    if (ctx == NULL || emit == NULL || validate_format(output) != QM_EOK) {
        return -QM_EINVAL;
    }
    *ctx = NULL;
    encoder = (playback_encoder_t *)qm_calloc(1U, sizeof(*encoder));
    if (encoder == NULL) {
        return -QM_ENOMEM;
    }
    encoder->format = *output;
    encoder->emit = emit;
    encoder->emit_ctx = emit_ctx;
    encoder->opus = opus_encoder_create((int)PLAYBACK_ENCODER_RATE,
                                        (int)PLAYBACK_ENCODER_CHANNELS,
                                        OPUS_APPLICATION_AUDIO,
                                        &opus_error);
    encoder->pcm = (uint8_t *)qm_malloc(PLAYBACK_ENCODER_FRAME_BYTES);
    encoder->packet = (uint8_t *)qm_malloc(output->max_packet_bytes);
    if (encoder->opus == NULL || opus_error != OPUS_OK ||
        encoder->pcm == NULL || encoder->packet == NULL) {
        if (encoder->opus != NULL) opus_encoder_destroy(encoder->opus);
        if (encoder->pcm != NULL) qm_free(encoder->pcm);
        if (encoder->packet != NULL) qm_free(encoder->packet);
        qm_free(encoder);
        return -QM_ENOMEM;
    }
    (void)opus_encoder_ctl(encoder->opus, OPUS_SET_COMPLEXITY(0));
    (void)opus_encoder_ctl(encoder->opus, OPUS_SET_VBR(0));
    (void)opus_encoder_ctl(encoder->opus, OPUS_SET_DTX(0));
    (void)opus_encoder_ctl(encoder->opus, OPUS_SET_BITRATE(PLAYBACK_ENCODER_BITRATE));
    *ctx = encoder;
    QM_LOGI(LOG_TAG, "resource encoder ready rate=%u frame_ms=%u max_packet=%u",
            (unsigned)PLAYBACK_ENCODER_RATE,
            (unsigned)PLAYBACK_ENCODER_FRAME_MS,
            (unsigned)output->max_packet_bytes);
    return QM_EOK;
}

int qm_audio_encoder_push(void *ctx, const qm_audio_data_t *pcm)
{
    playback_encoder_t *encoder = (playback_encoder_t *)ctx;
    const uint8_t *data;
    uint32_t left;

    if (encoder == NULL || pcm == NULL ||
        (pcm->size != 0U && pcm->data == NULL) ||
        (pcm->flags & ~(uint32_t)QM_AUDIO_DATA_END_OF_STREAM) != 0U ||
        (pcm->size % (sizeof(int16_t) * PLAYBACK_ENCODER_CHANNELS)) != 0U) {
        return -QM_EINVAL;
    }
    if (encoder->error != QM_EOK) return encoder->error;
    if (encoder->sealed) return -QM_EBUSY;

    data = (const uint8_t *)pcm->data;
    left = pcm->size;
    encoder->input_samples += left / (sizeof(int16_t) * PLAYBACK_ENCODER_CHANNELS);
    while (left != 0U) {
        uint32_t amount = PLAYBACK_ENCODER_FRAME_BYTES - encoder->used;
        if (amount > left) amount = left;
        qm_memcpy(encoder->pcm + encoder->used, data, amount);
        encoder->used += amount;
        data += amount;
        left -= amount;
        if (encoder->used == PLAYBACK_ENCODER_FRAME_BYTES) {
            int ret = emit_frame(encoder);
            if (ret != QM_EOK) return ret;
        }
    }
    if ((pcm->flags & QM_AUDIO_DATA_END_OF_STREAM) != 0U) {
        return qm_audio_encoder_finish(encoder);
    }
    return QM_EOK;
}

int qm_audio_encoder_finish(void *ctx)
{
    playback_encoder_t *encoder = (playback_encoder_t *)ctx;

    if (encoder == NULL) return -QM_EINVAL;
    if (encoder->error != QM_EOK) return encoder->error;
    if (encoder->sealed) return QM_EOK;
    encoder->sealed = QM_TRUE;
    if (encoder->used == 0U && encoder->input_samples == 0U) return QM_EOK;
    if (encoder->used != 0U) {
        qm_memset(encoder->pcm + encoder->used, 0,
                  PLAYBACK_ENCODER_FRAME_BYTES - encoder->used);
        return emit_frame(encoder);
    }
    return QM_EOK;
}

int qm_audio_encoder_close(void *ctx)
{
    playback_encoder_t *encoder = (playback_encoder_t *)ctx;

    if (encoder == NULL) return -QM_EINVAL;
    if (encoder->opus != NULL) opus_encoder_destroy(encoder->opus);
    if (encoder->pcm != NULL) qm_free(encoder->pcm);
    if (encoder->packet != NULL) qm_free(encoder->packet);
    qm_free(encoder);
    return QM_EOK;
}
