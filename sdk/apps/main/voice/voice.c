#include "qm.h"
#include "qm_kv.h"
#include "qm_log.h"
#include "opus_pkt.h"
#include "qm_audio.h"
#include "voice.h"
#include "boot_sound.h"

#include <string.h>

#define LOG_TAG "voice"

/* 与物模型音量共用的 KV。值是 10..100，芯片寄存器换算在设备里做。 */
#define VOICE_KV_VOLUME    "volume"
#define VOICE_VOLUME_MIN   10
#define VOICE_VOLUME_MAX   100
#define VOICE_VOLUME_DEF   80

/* 麦一包最多 512 字节，16 kHz / 单声道 / S16。 */
#define VOICE_CAPTURE_BYTES 512
#define VOICE_CAPTURE_QUEUE 4

static qm_audio_control_t *g_control;
static qm_audio_subscription_t g_wake_sub;
static uint8_t g_ready;
static voice_wakeup_cb_t g_wakeup_cb;

static void voice_apply_saved_volume(void)
{
    uint8_t volume = VOICE_VOLUME_DEF;
    int len = (int)sizeof(volume);

    if (qm_kv_get(VOICE_KV_VOLUME, &volume, &len) == 0) {
        if (volume < VOICE_VOLUME_MIN) {
            volume = VOICE_VOLUME_MIN;
        } else if (volume > VOICE_VOLUME_MAX) {
            volume = VOICE_VOLUME_MAX;
        }
    }
    (void)qm_audio_set_volume(volume);
    QM_LOGI(LOG_TAG, "volume %u", (unsigned)volume);
}

static void on_wake(const qm_audio_event_t *event, void *user_ctx)
{
    (void)user_ctx;
    if (event == NULL || event->type != QM_AUDIO_EVENT_WAKE) {
        return;
    }
    if (g_wakeup_cb != NULL) {
        g_wakeup_cb();
    }
}

void voice_set_wakeup_cb(voice_wakeup_cb_t cb)
{
    g_wakeup_cb = cb;
}

static void fill_opus(qm_audio_stream_format_t *fmt)
{
    memset(fmt, 0, sizeof(*fmt));
    /* 芯片是 PCM。设备对外按包收 Opus，单包不超过 255。编解码在设备里完成。 */
    fmt->codec = QM_AUDIO_CODEC_OPUS;
    fmt->pcm.sample_rate_hz = 16000;
    fmt->pcm.channels = 1;
    fmt->pcm.sample = QM_AUDIO_PCM_S16;
    fmt->packet_duration_ms = OPUS_PKT_FRAME_MS;
    fmt->packet_bytes = 0;
    fmt->max_packet_bytes = OPUS_PKT_MAX;
    fmt->framing = QM_AUDIO_CODEC_FRAMING_STREAM;
}

qm_err_t voice_start(void)
{
    qm_audio_config_t audio_cfg;
    int ret;

    memset(&audio_cfg, 0, sizeof(audio_cfg));
    fill_opus(&audio_cfg.input_format);
    fill_opus(&audio_cfg.output_format);
    audio_cfg.capture_frame_bytes = VOICE_CAPTURE_BYTES;
    audio_cfg.capture_queue_len = VOICE_CAPTURE_QUEUE;

    ret = qm_audio_init(&audio_cfg);
    if (ret != QM_EOK) {
        QM_LOGD(LOG_TAG, "qm_audio_init failed ret=%d", ret);
        return ret;
    }
    ret = qm_audio_wake_subscribe(on_wake, NULL, &g_wake_sub);
    if (ret != QM_EOK) {
        QM_LOGD(LOG_TAG, "wake subscribe failed ret=%d", ret);
        (void)qm_audio_deinit(1000);
        return ret;
    }
    ret = qm_audio_control_create(&g_control);
    if (ret != QM_EOK) {
        QM_LOGD(LOG_TAG, "qm_audio_control_create failed ret=%d", ret);
        (void)qm_audio_wake_unsubscribe(g_wake_sub);
        (void)qm_audio_deinit(1000);
        g_control = NULL;
        return ret;
    }
    g_ready = 1;
    voice_apply_saved_volume();
    QM_LOGI(LOG_TAG, "qm_audio ready, opus 20ms");
    /* 音量已设。开机音效走自己的 Control，不占用 agent 这份。 */
    (void)boot_sound_start();
    return QM_EOK;
}

qm_audio_control_t *voice_get_control(void)
{
    return g_control;
}

int voice_audio_ready(void)
{
    return g_ready;
}
