/* ESP32 synchronous Opus encoder. / ESP32 同步 Opus 编码适配。 */
#include "qm_config.h"
#include "adapter/qm_audio_codec.h"
#include "qm_kernel.h"
#include "qm_string.h"

#if defined(CONFIG_QM_AUDIO_ENCODER_SUPPORT) && CONFIG_QM_AUDIO_ENCODER_SUPPORT
#include "esp_opus_enc.h"

/* RFC 6716 maximum bytes per single Opus frame. This adapter uses <=60 ms. */
#define OPUS_MAX_PACKET_BYTES 1275U
#define OPUS_TIMEBASE_HZ 48000U
#define OPUS_HEAD_BYTES 19U
/* AUDIO mode uses the Opus 6.5 ms lookahead, including codec-internal delay. */
#define OPUS_AUDIO_LOOKAHEAD_US 6500U

typedef struct {
    void *codec;
    qm_audio_stream_format_t format;
    qm_audio_encoder_emit_packet_fn emit;
    void *emit_ctx;
    uint8_t *pcm;
    uint8_t *packet;
    uint32_t frame_bytes;
    uint32_t packet_capacity;
    uint32_t used;
    uint32_t sequence;
    uint32_t lookahead_samples;
    uint64_t input_samples;
    uint64_t encoded_samples;
    int error;
    qm_bool sealed;
} encoder_t;

static int make_config(const qm_audio_stream_format_t *f, esp_opus_enc_config_t *cfg)
{
    if (!f || !cfg) return -QM_EINVAL;
    if (f->codec != QM_AUDIO_CODEC_OPUS || f->pcm.sample != QM_AUDIO_PCM_S16 ||
        f->pcm.channels < 1U || f->pcm.channels > 2U ||
        f->packet_duration_ms == 0U ||
        f->max_packet_bytes == 0U || f->max_packet_bytes > OPUS_MAX_PACKET_BYTES ||
        f->packet_bytes > f->max_packet_bytes) return -QM_ENOSYS;
    switch (f->pcm.sample_rate_hz) {
    case 8000U: case 12000U: case 16000U: case 24000U: case 48000U: break;
    default: return -QM_ENOSYS;
    }
    *cfg = (esp_opus_enc_config_t)ESP_OPUS_ENC_CONFIG_DEFAULT();
    switch (f->packet_duration_ms) {
    case 5U: cfg->frame_duration = ESP_OPUS_ENC_FRAME_DURATION_5_MS; break;
    case 10U: cfg->frame_duration = ESP_OPUS_ENC_FRAME_DURATION_10_MS; break;
    case 20U: cfg->frame_duration = ESP_OPUS_ENC_FRAME_DURATION_20_MS; break;
    case 40U: cfg->frame_duration = ESP_OPUS_ENC_FRAME_DURATION_40_MS; break;
    case 60U: cfg->frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS; break;
    default: return -QM_ENOSYS;
    }
    cfg->sample_rate = f->pcm.sample_rate_hz;
    cfg->channel = f->pcm.channels;
    cfg->application_mode = ESP_OPUS_ENC_APPLICATION_AUDIO;
    cfg->enable_vbr = false;
    /* Fixed bitrate keeps every packet inside the advertised sink bound. */
    uint32_t bytes = f->packet_bytes ? f->packet_bytes : f->max_packet_bytes;
    uint64_t scaled = (uint64_t)bytes * 8U * 1000U;
    if (scaled % f->packet_duration_ms) return -QM_ENOSYS;
    uint64_t bitrate = scaled / f->packet_duration_ms;
    if (bitrate < 6000U || bitrate > 510000U) return -QM_ENOSYS;
    cfg->bitrate = (int)bitrate;
    return QM_EOK;
}

int qm_audio_encoder_check_format(const qm_audio_stream_format_t *output)
{
    esp_opus_enc_config_t cfg;
    return make_config(output, &cfg);
}

int qm_audio_encoder_open(void **ctx, const qm_audio_stream_format_t *output,
                         qm_audio_encoder_emit_packet_fn emit, void *emit_ctx)
{
    if (!ctx) return -QM_EINVAL;
    *ctx = NULL;
    if (!emit) return -QM_EINVAL;
    esp_opus_enc_config_t cfg;
    int ret = make_config(output, &cfg);
    if (ret != QM_EOK) return ret;
    encoder_t *e = qm_malloc(sizeof(*e));
    if (!e) return -QM_ENOMEM;
    qm_memset(e, 0, sizeof(*e));
    e->format = *output;
    e->emit = emit;
    e->emit_ctx = emit_ctx;
    esp_audio_err_t err = esp_opus_enc_open(&cfg, sizeof(cfg), &e->codec);
    if (err != ESP_AUDIO_ERR_OK) {
        ret = err == ESP_AUDIO_ERR_MEM_LACK ? -QM_ENOMEM : -QM_ENOSYS;
        goto fail;
    }
    int in_bytes = 0, out_bytes = 0;
    if (esp_opus_enc_get_frame_size(e->codec, &in_bytes, &out_bytes) != ESP_AUDIO_ERR_OK ||
        in_bytes <= 0 || out_bytes <= 0 || out_bytes > (int)OPUS_MAX_PACKET_BYTES) {
        ret = -QM_EIO;
        goto fail;
    }
    e->frame_bytes = (uint32_t)in_bytes;
    uint32_t expected = (uint32_t)((uint64_t)output->pcm.sample_rate_hz *
                                  output->packet_duration_ms / 1000U) *
                        output->pcm.channels * sizeof(int16_t);
    if (e->frame_bytes != expected) { ret = -QM_EIO; goto fail; }
    e->packet_capacity = (uint32_t)out_bytes;
    e->pcm = qm_malloc(e->frame_bytes);
    e->packet = qm_malloc(e->packet_capacity);
    if (!e->pcm || !e->packet) { ret = -QM_ENOMEM; goto fail; }
    /* Espressif does not expose OPUS_GET_LOOKAHEAD. Prefer the OpusHead
     * preskip when available; otherwise AUDIO mode's specified delay applies.
     * Raw GX8006 packets cannot carry a trim marker, so delay/padding is audible. */
    e->lookahead_samples = (uint32_t)((uint64_t)output->pcm.sample_rate_hz *
                                    OPUS_AUDIO_LOOKAHEAD_US / 1000000U);
    esp_audio_enc_info_t info = {0};
    if (esp_opus_enc_get_info(e->codec, &info) == ESP_AUDIO_ERR_OK &&
        info.codec_spec_info && info.spec_info_len >= OPUS_HEAD_BYTES &&
        qm_memcmp(info.codec_spec_info, "OpusHead", 8U) == 0) {
        uint32_t preskip = info.codec_spec_info[10] | ((uint32_t)info.codec_spec_info[11] << 8U);
        e->lookahead_samples = (uint32_t)(((uint64_t)preskip * output->pcm.sample_rate_hz +
                                         OPUS_TIMEBASE_HZ - 1U) / OPUS_TIMEBASE_HZ);
    }
    *ctx = e;
    return QM_EOK;
fail:
    qm_audio_encoder_close(e);
    return ret;
}

static int encode_frame(encoder_t *e)
{
    esp_audio_enc_in_frame_t in = {.buffer = e->pcm, .len = e->frame_bytes};
    esp_audio_enc_out_frame_t out = {.buffer = e->packet, .len = e->packet_capacity};
    esp_audio_err_t ret = esp_opus_enc_process(e->codec, &in, &out);
    if (ret != ESP_AUDIO_ERR_OK || !out.encoded_bytes ||
        out.encoded_bytes > e->format.max_packet_bytes ||
        (e->format.packet_bytes && out.encoded_bytes != e->format.packet_bytes)) {
        return e->error = -QM_EIO;
    }
    qm_audio_data_t packet = {
        .data = e->packet,
        .size = out.encoded_bytes,
        .timestamp_ms = e->encoded_samples * 1000U / e->format.pcm.sample_rate_hz,
        .duration_ms = e->format.packet_duration_ms,
        .sequence = e->sequence++,
        .flags = QM_AUDIO_DATA_TIMESTAMP_VALID | QM_AUDIO_DATA_DURATION_VALID,
    };
    e->used = 0U;
    e->encoded_samples += e->frame_bytes / (sizeof(int16_t) * e->format.pcm.channels);
    int status = e->emit(&packet, e->emit_ctx);
    if (status != QM_EOK) e->error = status < 0 ? status : -QM_EIO;
    return e->error;
}

int qm_audio_encoder_push(void *ctx, const qm_audio_data_t *pcm)
{
    encoder_t *e = ctx;
    if (!e || !pcm || (pcm->size && !pcm->data)) return -QM_EINVAL;
    if (e->error) return e->error;
    if (e->sealed) return -QM_EBUSY;
    if (pcm->size % (sizeof(int16_t) * e->format.pcm.channels) ||
        (pcm->flags & ~QM_AUDIO_DATA_END_OF_STREAM)) return -QM_EINVAL;
    const uint8_t *data = pcm->data;
    uint32_t left = pcm->size;
    e->input_samples += left / (sizeof(int16_t) * e->format.pcm.channels);
    while (left) {
        uint32_t amount = e->frame_bytes - e->used;
        if (amount > left) amount = left;
        qm_memcpy(e->pcm + e->used, data, amount);
        e->used += amount; data += amount; left -= amount;
        if (e->used == e->frame_bytes) {
            int ret = encode_frame(e);
            if (ret != QM_EOK) return ret;
        }
    }
    return (pcm->flags & QM_AUDIO_DATA_END_OF_STREAM) ?
        qm_audio_encoder_finish(e) : QM_EOK;
}

int qm_audio_encoder_finish(void *ctx)
{
    encoder_t *e = ctx;
    if (!e) return -QM_EINVAL;
    if (e->error) return e->error;
    if (e->sealed) return QM_EOK;
    e->sealed = QM_TRUE;
    if (!e->input_samples) return QM_EOK;
    uint64_t required = e->input_samples + e->lookahead_samples;
    while (e->encoded_samples < required) {
        qm_memset(e->pcm + e->used, 0, e->frame_bytes - e->used);
        int ret = encode_frame(e);
        if (ret != QM_EOK) return ret;
    }
    return QM_EOK;
}

int qm_audio_encoder_close(void *ctx)
{
    encoder_t *e = ctx;
    if (!e) return -QM_EINVAL;
    if (e->codec) esp_opus_enc_close(e->codec);
    if (e->pcm) qm_free(e->pcm);
    if (e->packet) qm_free(e->packet);
    qm_free(e);
    return QM_EOK;
}
#else
int qm_audio_encoder_check_format(const qm_audio_stream_format_t *f)
{ (void)f; return -QM_ENOSYS; }
int qm_audio_encoder_open(void **c, const qm_audio_stream_format_t *f,
                         qm_audio_encoder_emit_packet_fn e, void *u)
{ if (c) *c = NULL; (void)f; (void)e; (void)u; return -QM_ENOSYS; }
int qm_audio_encoder_push(void *c, const qm_audio_data_t *p)
{ (void)c; (void)p; return -QM_ENOSYS; }
int qm_audio_encoder_finish(void *c) { (void)c; return -QM_ENOSYS; }
int qm_audio_encoder_close(void *c) { (void)c; return -QM_ENOSYS; }
#endif
