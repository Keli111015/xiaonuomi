/** @file qm_audio_format.h
 * @brief Shared stateless media validation. / 共享无状态媒体校验。
 */
#ifndef QM_AUDIO_FORMAT_H
#define QM_AUDIO_FORMAT_H
#include "qm_audio_types.h"

/** @brief Return PCM bytes per sample. / 返回每个 PCM 采样的字节数。 */
static inline uint32_t qm_audio_pcm_sample_bytes(qm_audio_pcm_sample_t sample)
{
    switch (sample) {
        case QM_AUDIO_PCM_S16:
            return 2U;
        case QM_AUDIO_PCM_S24:
            return 3U;
        case QM_AUDIO_PCM_S32:
            return 4U;
        default:
            return 0U;
    }
}

/** @brief Return PCM bytes per sample frame. / 返回每个 PCM 采样帧的字节数。 */
static inline uint32_t qm_audio_pcm_frame_bytes(const qm_audio_pcm_format_t *format)
{
    uint32_t sample_bytes;

    if (format == NULL || format->channels == 0U) {
        return 0U;
    }
    sample_bytes = qm_audio_pcm_sample_bytes(format->sample);
    if (sample_bytes == 0U || sample_bytes > UINT32_MAX / format->channels) {
        return 0U;
    }
    return sample_bytes * (uint32_t)format->channels;
}

/** @brief Validate a PCM format. / 校验 PCM 格式。 */
static inline qm_bool qm_audio_pcm_format_valid(const qm_audio_pcm_format_t *format)
{
    return (format != NULL && format->sample_rate_hz != 0U &&
            format->channels != 0U &&
            qm_audio_pcm_sample_bytes(format->sample) != 0U) ? QM_TRUE : QM_FALSE;
}

/** @brief Validate a configured native stream. / 校验固定原生流格式。 */
static inline qm_bool qm_audio_encoded_bytes(const qm_audio_stream_format_t *format)
{
    return format != NULL && format->codec != QM_AUDIO_CODEC_PCM &&
           format->max_packet_bytes == 0U;
}

static inline qm_bool qm_audio_encoded_packets(const qm_audio_stream_format_t *format)
{
    return format != NULL && format->codec != QM_AUDIO_CODEC_PCM &&
           format->max_packet_bytes != 0U;
}

static inline qm_bool qm_audio_pcm_equal(const qm_audio_pcm_format_t *a, const qm_audio_pcm_format_t *b)
{
    return a->sample_rate_hz == b->sample_rate_hz && a->channels == b->channels &&
           a->sample == b->sample;
}

static inline qm_bool qm_audio_codec_is_private(qm_audio_codec_t codec)
{
    /* Cast through a signed type so a caller-supplied -1 cannot become a
     * product codec merely because the compiler chose an unsigned enum type. */
    return (int32_t)codec >= (int32_t)QM_AUDIO_CODEC_PRIVATE_BASE;
}

static inline qm_bool qm_audio_stream_format_valid(const qm_audio_stream_format_t *format)
{
    if (format == NULL) return QM_FALSE;
    if (format->codec == QM_AUDIO_CODEC_PCM) {
        return qm_audio_pcm_format_valid(&format->pcm) && format->packet_bytes == 0U &&
               format->max_packet_bytes == 0U && format->packet_duration_ms == 0U &&
               format->framing == QM_AUDIO_CODEC_FRAMING_STREAM;
    }
    if (!((format->codec > QM_AUDIO_CODEC_PCM && format->codec < QM_AUDIO_CODEC_AUTO) ||
          qm_audio_codec_is_private(format->codec))) return QM_FALSE;
    if (!qm_audio_pcm_format_valid(&format->pcm) &&
        (format->pcm.sample_rate_hz != 0U || format->pcm.channels != 0U ||
         format->pcm.sample != QM_AUDIO_PCM_S16)) return QM_FALSE;
    /* Ogg/raw-header/CBR are Opus-specific input framings.  A native stream
     * profile using another codec must use STREAM or explicit PACKET units. */
    if ((format->framing == QM_AUDIO_CODEC_FRAMING_OPUS_OGG ||
         format->framing == QM_AUDIO_CODEC_FRAMING_OPUS_RAW_HEADER ||
         format->framing == QM_AUDIO_CODEC_FRAMING_OPUS_CBR) &&
        format->codec != QM_AUDIO_CODEC_OPUS) return QM_FALSE;
    if (qm_audio_encoded_bytes(format))
        return format->packet_bytes == 0U && format->packet_duration_ms == 0U &&
               (format->framing == QM_AUDIO_CODEC_FRAMING_STREAM ||
                format->framing == QM_AUDIO_CODEC_FRAMING_OPUS_OGG ||
                format->framing == QM_AUDIO_CODEC_FRAMING_OPUS_RAW_HEADER);
    return format->packet_bytes <= format->max_packet_bytes &&
           format->framing == QM_AUDIO_CODEC_FRAMING_STREAM;
}

/** @brief Validate one native encoded packet without guessing packet borders. */
static inline qm_bool qm_audio_packet_valid(const qm_audio_stream_format_t *format, const qm_audio_data_t *packet)
{
    return qm_audio_encoded_packets(format) &&
           packet != NULL && packet->data != NULL && packet->size != 0U &&
           packet->size <= format->max_packet_bytes &&
           (format->packet_bytes == 0U || packet->size == format->packet_bytes) &&
           (packet->flags & ~(QM_AUDIO_DATA_TIMESTAMP_VALID |
                             QM_AUDIO_DATA_DURATION_VALID |
                             QM_AUDIO_DATA_DISCONTINUITY)) == 0U &&
           (format->packet_duration_ms == 0U ||
            (packet->flags & QM_AUDIO_DATA_DURATION_VALID) == 0U ||
            packet->duration_ms == format->packet_duration_ms);
}

/** @brief Compare negotiated profiles field-by-field, without padding bytes. */
static inline qm_bool qm_audio_stream_format_equal(const qm_audio_stream_format_t *a, const qm_audio_stream_format_t *b)
{
    return a != NULL && b != NULL && a->codec == b->codec &&
           a->pcm.sample_rate_hz == b->pcm.sample_rate_hz &&
           a->pcm.channels == b->pcm.channels && a->pcm.sample == b->pcm.sample &&
           a->packet_bytes == b->packet_bytes &&
           a->max_packet_bytes == b->max_packet_bytes &&
           a->packet_duration_ms == b->packet_duration_ms && a->framing == b->framing;
}

/** @brief Convert frames to bounded milliseconds. / 将帧数转换为有界毫秒值，长媒体不回绕。 */
static inline uint32_t qm_audio_frames_to_ms(uint64_t frames, uint32_t rate)
{
    uint64_t ms;
    if (rate == 0U) return 0U;
    if (frames / rate > UINT32_MAX / 1000U) return UINT32_MAX;
    ms = frames / rate * 1000U + frames % rate * 1000U / rate;
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

/** @brief Normalize an adapter result to qm_errno. / 将适配器结果归一化为 qm_errno。 */
static inline int qm_audio_normalize_result(int ret)
{
    return (ret == QM_EOK) ? QM_EOK : ((ret > QM_EOK) ? -QM_EIO : ret);
}

#endif
