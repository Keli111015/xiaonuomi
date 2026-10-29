#ifndef QM_AGENT_EXAMPLE_AUDIO_H
#define QM_AGENT_EXAMPLE_AUDIO_H

#include "qm_audio.h"
#include "qm_config.h"
#include "qm_errno.h"
#include "qm_string.h"

#define QM_AGENT_EXAMPLE_AUDIO_SAMPLE_RATE 16000U
#define QM_AGENT_EXAMPLE_AUDIO_GX8006_PACKET_DURATION_MS 40U
#define QM_AGENT_EXAMPLE_AUDIO_GX8006_PACKET_BYTES 80U
#if defined(CONFIG_QM_AUDIO_DEVICE_GX8006) && CONFIG_QM_AUDIO_DEVICE_GX8006
#define QM_AGENT_EXAMPLE_AUDIO_CAPTURE_FRAME_BYTES \
    QM_AGENT_EXAMPLE_AUDIO_GX8006_PACKET_BYTES
#else
#define QM_AGENT_EXAMPLE_AUDIO_CAPTURE_FRAME_BYTES 640U
#endif
#define QM_AGENT_EXAMPLE_AUDIO_CAPTURE_QUEUE_LEN 4U
#define QM_AGENT_EXAMPLE_AUDIO_STOP_TIMEOUT_MS 1000U

typedef struct {
    qm_audio_control_t *control;
    qm_bool initialized;
} qm_agent_example_audio_t;

static inline void qm_agent_example_audio_fill_stream_format(
    qm_audio_stream_format_t *format)
{
    if (format == NULL) {
        return;
    }

    qm_memset(format, 0, sizeof(*format));
    format->pcm.sample_rate_hz = QM_AGENT_EXAMPLE_AUDIO_SAMPLE_RATE;
    format->pcm.channels = 1U;
    format->pcm.sample = QM_AUDIO_PCM_S16;
#if defined(CONFIG_QM_AUDIO_DEVICE_GX8006) && CONFIG_QM_AUDIO_DEVICE_GX8006
    format->codec = QM_AUDIO_CODEC_OPUS;
    format->packet_duration_ms = QM_AGENT_EXAMPLE_AUDIO_GX8006_PACKET_DURATION_MS;
    format->packet_bytes = QM_AGENT_EXAMPLE_AUDIO_GX8006_PACKET_BYTES;
    format->max_packet_bytes = QM_AGENT_EXAMPLE_AUDIO_GX8006_PACKET_BYTES;
#else
    format->codec = QM_AUDIO_CODEC_PCM;
#endif
}

static inline int qm_agent_example_audio_init(qm_agent_example_audio_t *audio)
{
    qm_audio_config_t config;
    int ret;

    if (audio == NULL) {
        return -QM_EINVAL;
    }
    if (audio->control != NULL) {
        return QM_EOK;
    }

    qm_memset(&config, 0, sizeof(config));
    qm_agent_example_audio_fill_stream_format(&config.input_format);
    config.output_format = config.input_format;
    config.capture_frame_bytes = QM_AGENT_EXAMPLE_AUDIO_CAPTURE_FRAME_BYTES;
    config.capture_queue_len = QM_AGENT_EXAMPLE_AUDIO_CAPTURE_QUEUE_LEN;

    ret = qm_audio_init(&config);
    if (ret != QM_EOK) {
        return ret;
    }
    audio->initialized = QM_TRUE;

    ret = qm_audio_control_create(&audio->control);
    if (ret != QM_EOK) {
        (void)qm_audio_deinit(QM_AGENT_EXAMPLE_AUDIO_STOP_TIMEOUT_MS);
        audio->initialized = QM_FALSE;
    }
    return ret;
}

static inline int qm_agent_example_audio_deinit(qm_agent_example_audio_t *audio)
{
    int ret;

    if (audio == NULL) {
        return -QM_EINVAL;
    }
    if (audio->control != NULL) {
        ret = qm_audio_control_shutdown(audio->control,
                                        QM_AGENT_EXAMPLE_AUDIO_STOP_TIMEOUT_MS);
        if (ret != QM_EOK) {
            return ret;
        }
        ret = qm_audio_control_destroy(audio->control);
        if (ret != QM_EOK) {
            return ret;
        }
        audio->control = NULL;
    }
    if (!audio->initialized) {
        return QM_EOK;
    }

    ret = qm_audio_deinit(QM_AGENT_EXAMPLE_AUDIO_STOP_TIMEOUT_MS);
    if (ret == QM_EOK) {
        audio->initialized = QM_FALSE;
    }
    return ret;
}

#endif /* QM_AGENT_EXAMPLE_AUDIO_H */
