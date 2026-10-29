#include "opus_pkt.h"

#include <string.h>

#include "opus.h"
#include "qm.h"
#include "qm_log.h"

#define LOG_TAG "opus"

/* 16 kHz / 单声道 / 20 ms。麦是 512 字节/16 ms，这里攒够 640 再编。 */
#define AGORA_OPUS_RATE        16000
#define AGORA_OPUS_CH          1
#define AGORA_OPUS_FRAME_MS    20
#define AGORA_OPUS_FRAME_SAMP  ((AGORA_OPUS_RATE / 1000) * AGORA_OPUS_FRAME_MS)
#define AGORA_OPUS_FRAME_BYTES (AGORA_OPUS_FRAME_SAMP * (int)sizeof(int16_t))
#define AGORA_OPUS_ACC_BYTES   (AGORA_OPUS_FRAME_BYTES * 2)
/* 16 kHz 下一包最长按 120 ms 解。 */
#define AGORA_OPUS_DEC_SAMP    ((AGORA_OPUS_RATE / 1000) * 120)

static OpusEncoder *s_enc;
static OpusDecoder *s_dec;
static qm_mutex_t s_lock;
static uint8_t s_lock_ok;
static uint8_t s_acc[AGORA_OPUS_ACC_BYTES];
static uint32_t s_acc_len;
static uint8_t s_enc_logged;
static uint8_t s_dec_logged;

static int codec_open(void)
{
    int err;

    if (s_enc != NULL && s_dec != NULL) {
        return QM_EOK;
    }
    if (!s_lock_ok) {
        if (qm_mutex_new(&s_lock) != QM_EOK) {
            return -QM_ENOMEM;
        }
        s_lock_ok = 1;
    }

    s_enc = opus_encoder_create(AGORA_OPUS_RATE, AGORA_OPUS_CH, OPUS_APPLICATION_VOIP, &err);
    if (s_enc == NULL) {
        QM_LOGE(LOG_TAG, "encoder create failed err=%d", err);
        return -QM_EINIT;
    }
    s_dec = opus_decoder_create(AGORA_OPUS_RATE, AGORA_OPUS_CH, &err);
    if (s_dec == NULL) {
        QM_LOGE(LOG_TAG, "decoder create failed err=%d", err);
        opus_encoder_destroy(s_enc);
        s_enc = NULL;
        return -QM_EINIT;
    }

    opus_encoder_ctl(s_enc, OPUS_SET_BITRATE(OPUS_PKT_BYTES * 8 * 1000 / OPUS_PKT_FRAME_MS));
    opus_encoder_ctl(s_enc, OPUS_SET_VBR(0));
    opus_encoder_ctl(s_enc, OPUS_SET_DTX(0));
    opus_encoder_ctl(s_enc, OPUS_SET_COMPLEXITY(0));
    QM_LOGI(LOG_TAG, "opus ready %d Hz mono %d ms frame=%d bytes",
            AGORA_OPUS_RATE, AGORA_OPUS_FRAME_MS, AGORA_OPUS_FRAME_BYTES);
    return QM_EOK;
}

int opus_pcm_to_packet(const uint8_t *pcm, uint32_t pcm_len,
                             uint8_t *opus, uint32_t opus_cap)
{
    int nbytes;
    int ret;

    if (opus == NULL || opus_cap == 0) {
        return -QM_EINVAL;
    }

    ret = codec_open();
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&s_lock, QM_WAIT_FOREVER);
    if (pcm != NULL && pcm_len > 0) {
        if (s_acc_len + pcm_len > sizeof(s_acc)) {
            QM_LOGW(LOG_TAG, "pcm acc overflow have=%u add=%u",
                    (unsigned)s_acc_len, (unsigned)pcm_len);
            s_acc_len = 0;
        }
        memcpy(s_acc + s_acc_len, pcm, pcm_len);
        s_acc_len += pcm_len;
    }
    if (s_acc_len < (uint32_t)AGORA_OPUS_FRAME_BYTES) {
        qm_mutex_unlock(&s_lock);
        return 0;
    }

    nbytes = opus_encode(s_enc,
                         (const opus_int16 *)s_acc,
                         AGORA_OPUS_FRAME_SAMP,
                         opus,
                         (opus_int32)opus_cap);
    s_acc_len -= (uint32_t)AGORA_OPUS_FRAME_BYTES;
    if (s_acc_len > 0) {
        memmove(s_acc, s_acc + AGORA_OPUS_FRAME_BYTES, s_acc_len);
    }
    qm_mutex_unlock(&s_lock);

    if (nbytes < 0) {
        QM_LOGE(LOG_TAG, "encode failed err=%d", nbytes);
        return -QM_ERROR;
    }
    if (!s_enc_logged && nbytes > 0) {
        s_enc_logged = 1;
        QM_LOGI(LOG_TAG, "uplink first opus %d bytes from %d pcm",
                nbytes, AGORA_OPUS_FRAME_BYTES);
    }
    return nbytes;
}

int opus_packet_to_pcm(const uint8_t *opus, uint32_t opus_len,
                             uint8_t *pcm, uint32_t pcm_cap)
{
    int samples;
    int ret;
    int pcm_bytes;

    if (opus == NULL || opus_len == 0 || pcm == NULL || pcm_cap < sizeof(int16_t)) {
        return -QM_EINVAL;
    }

    ret = codec_open();
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&s_lock, QM_WAIT_FOREVER);
    samples = opus_decode(s_dec,
                          opus,
                          (opus_int32)opus_len,
                          (opus_int16 *)pcm,
                          (int)(pcm_cap / sizeof(int16_t)),
                          0);
    qm_mutex_unlock(&s_lock);

    if (samples < 0) {
        QM_LOGE(LOG_TAG, "decode failed err=%d opus_len=%u", samples, (unsigned)opus_len);
        return -QM_ERROR;
    }
    pcm_bytes = samples * (int)sizeof(int16_t);
    if (!s_dec_logged && pcm_bytes > 0) {
        s_dec_logged = 1;
        QM_LOGI(LOG_TAG, "downlink first pcm %d bytes from opus %u",
                pcm_bytes, (unsigned)opus_len);
    }
    return pcm_bytes;
}
