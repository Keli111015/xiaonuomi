/* ESP32 synchronous MP3 stream and framed Opus decoder. / ESP32 同步 MP3/Opus 解码适配。 */
#include "qm_config.h"
#include "adapter/qm_audio_codec.h"
#include "qm_kernel.h"
#include "qm_string.h"

#if defined(CONFIG_QM_AUDIO_DECODER_SUPPORT) && CONFIG_QM_AUDIO_DECODER_SUPPORT
#include "esp_audio_simple_dec.h"
#include "esp_mp3_dec.h"
#include "esp_opus_dec.h"
#include <sys/lock.h>

#define MP3_PCM_BUFFER_BYTES (1152U * 2U * sizeof(int16_t))
#define OPUS_MAX_PACKET_BYTES 1275U
#define OPUS_MAX_DURATION_MS 120U
#define RAW_OPUS_HEADER_BYTES 8U
#define DECODER_WORK_BYTES 1024U

typedef struct {
    void *codec;
    qm_audio_pcm_format_t output;
    qm_audio_pcm_format_t decoded;
    qm_audio_decoder_emit_pcm_fn emit;
    void *emit_ctx;
    uint8_t *pcm;
    uint32_t pcm_capacity;
    uint8_t *packet;
    uint32_t packet_capacity;
    uint32_t packet_size;
    uint32_t packet_used;
    uint8_t header[RAW_OPUS_HEADER_BYTES];
    uint32_t header_used;
    uint8_t tail;
    qm_bool tail_valid;
    qm_bool opus;
    qm_bool format_known;
    qm_bool sealed;
    uint64_t output_frames;
    uint64_t input_bytes;
    int error;
    qm_audio_decoder_framing_t framing;
    uint32_t packet_duration_ms;
} decoder_t;

/* Simple MP3 parsing requires the SDK's MP3 registry. Register once under a
 * process-lifetime lock; never unregister while another decoder could use it.
 * Opus bypasses the global registry entirely. */
static _lock_t registry_lock;
static qm_bool mp3_registered;

static int ensure_mp3(void)
{
    int ret = QM_EOK;
    _lock_acquire(&registry_lock);
    if (!mp3_registered) {
        esp_audio_err_t err = esp_mp3_dec_register();
        if (err == ESP_AUDIO_ERR_OK || err == ESP_AUDIO_ERR_ALREADY_EXIST) mp3_registered = QM_TRUE;
        else ret = err == ESP_AUDIO_ERR_MEM_LACK ? -QM_ENOMEM : -QM_EIO;
    }
    _lock_release(&registry_lock);
    return ret;
}

int qm_audio_decoder_check_format(const qm_audio_pcm_format_t *output,
                                  const qm_audio_decoder_config_t *config)
{
    if (!output) return -QM_EINVAL;
    if (!config) return -QM_ENOSYS;
    if (output->sample != QM_AUDIO_PCM_S16 || output->channels < 1U ||
        output->channels > 2U || output->sample_rate_hz < 8000U ||
        output->sample_rate_hz > 48000U) return -QM_ENOSYS;
    if (!config->codec_hint) return -QM_ENOSYS;
    if (qm_strcmp(config->codec_hint, "mp3") == 0) {
        if (config->framing != QM_AUDIO_DECODER_FRAMING_STREAM ||
            config->packet_duration_ms != 0U) return -QM_ENOSYS;
        return QM_EOK;
    }
    if (qm_strcmp(config->codec_hint, "opus") != 0) return -QM_ENOSYS;
    switch (output->sample_rate_hz) {
    case 8000U: case 12000U: case 16000U: case 24000U: case 48000U: break;
    default: return -QM_ENOSYS;
    }
    if (config->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR)
        return config->packet_bytes && config->packet_bytes <= OPUS_MAX_PACKET_BYTES &&
               config->packet_duration_ms <= OPUS_MAX_DURATION_MS ? QM_EOK : -QM_EINVAL;
    if (config->framing == QM_AUDIO_DECODER_FRAMING_OPUS_RAW_HEADER)
        return config->packet_duration_ms <= OPUS_MAX_DURATION_MS ? QM_EOK : -QM_EINVAL;
    return -QM_ENOSYS;
}

static int emit_pcm(decoder_t *d, const qm_audio_data_t *pcm)
{
    int ret = d->emit(pcm, d->emit_ctx);
    if (ret != QM_EOK) return d->error = ret < 0 ? ret : -QM_EIO;
    d->output_frames += pcm->size / (d->output.channels * sizeof(int16_t));
    return QM_EOK;
}

static int decoded_pcm(decoder_t *d, uint32_t size, uint32_t rate, uint8_t channels,
                        uint8_t bits)
{
    if (!size) return QM_EOK;
    if (bits != 16U || !channels || size % (channels * sizeof(int16_t)))
        return d->error = -QM_EIO;
    qm_audio_pcm_format_t format = {rate, channels, QM_AUDIO_PCM_S16};
    if (!d->format_known) {
        d->decoded = format;
        if (rate != d->output.sample_rate_hz || channels != d->output.channels)
            return d->error = -QM_ENOSYS;
        d->format_known = QM_TRUE;
    } else if (format.sample_rate_hz != d->decoded.sample_rate_hz ||
               format.channels != d->decoded.channels) {
        /* Midstream format changes must start a new negotiated run. */
        return d->error = -QM_ENOSYS;
    }
    qm_audio_data_t pcm = {
        .data = d->pcm,
        .size = size,
        .flags = QM_AUDIO_DATA_NONE,
    };
    return emit_pcm(d, &pcm);
}

int qm_audio_decoder_open(void **ctx, const qm_audio_pcm_format_t *output,
                          qm_audio_decoder_emit_pcm_fn emit, void *emit_ctx,
                          void *user_ctx)
{
    if (!ctx) return -QM_EINVAL;
    *ctx = NULL;
    if (!emit) return -QM_EINVAL;
    const qm_audio_decoder_config_t *config = user_ctx;
    int ret = qm_audio_decoder_check_format(output, config);
    if (ret != QM_EOK) return ret;
    decoder_t *d = qm_malloc(sizeof(*d));
    if (!d) return -QM_ENOMEM;
    qm_memset(d, 0, sizeof(*d));
    d->output = *output; d->emit = emit; d->emit_ctx = emit_ctx;
    d->opus = qm_strcmp(config->codec_hint, "opus") == 0;
    d->framing = config->framing;
    d->packet_duration_ms = config->packet_duration_ms;
    d->pcm_capacity = d->opus ? output->sample_rate_hz * output->channels *
        sizeof(int16_t) * OPUS_MAX_DURATION_MS / 1000U : MP3_PCM_BUFFER_BYTES;
    d->pcm = qm_malloc(d->pcm_capacity);
    if (!d->pcm) { ret = -QM_ENOMEM; goto fail; }
    esp_audio_err_t err;
    if (d->opus) {
        d->packet_capacity = config->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR ?
            config->packet_bytes : OPUS_MAX_PACKET_BYTES;
        d->packet_size = config->framing == QM_AUDIO_DECODER_FRAMING_OPUS_CBR ? config->packet_bytes : 0U;
        d->packet = qm_malloc(d->packet_capacity);
        if (!d->packet) { ret = -QM_ENOMEM; goto fail; }
        esp_opus_dec_cfg_t cfg = ESP_OPUS_DEC_CONFIG_DEFAULT();
        cfg.sample_rate = output->sample_rate_hz; cfg.channel = output->channels;
        cfg.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_120_MS;
        cfg.self_delimited = false;
        err = esp_opus_dec_open(&cfg, sizeof(cfg), &d->codec);
    } else {
        ret = ensure_mp3();
        if (ret != QM_EOK) goto fail;
        esp_audio_simple_dec_cfg_t cfg = {.dec_type = ESP_AUDIO_SIMPLE_DEC_TYPE_MP3};
        err = esp_audio_simple_dec_open(&cfg, &d->codec);
    }
    if (err != ESP_AUDIO_ERR_OK) {
        ret = err == ESP_AUDIO_ERR_MEM_LACK ? -QM_ENOMEM : -QM_EIO;
        goto fail;
    }
    *ctx = d;
    return QM_EOK;
fail:
    qm_audio_decoder_close(d);
    return ret;
}

static int decode_mp3(decoder_t *d, const uint8_t *data, uint32_t size,
                       qm_bool eos, uint32_t *consumed)
{
    *consumed = 0U;
    while (*consumed < size) {
        esp_audio_simple_dec_raw_t raw = {
            .buffer = (uint8_t *)data + *consumed, .len = size - *consumed, .eos = eos,
        };
        esp_audio_simple_dec_out_t out = {.buffer = d->pcm, .len = d->pcm_capacity};
        esp_audio_err_t err = esp_audio_simple_dec_process(d->codec, &raw, &out);
        if (raw.consumed > raw.len || out.decoded_size > d->pcm_capacity)
            return d->error = -QM_EIO;
        *consumed += raw.consumed;
        if (err != ESP_AUDIO_ERR_OK) return d->error = -QM_EIO;
        if (out.decoded_size) {
            esp_audio_simple_dec_info_t info = {0};
            if (esp_audio_simple_dec_get_info(d->codec, &info) != ESP_AUDIO_ERR_OK)
                return d->error = -QM_EIO;
            int ret = decoded_pcm(d, out.decoded_size, info.sample_rate,
                                  info.channel, info.bits_per_sample);
            if (ret != QM_EOK) return d->error = ret;
        }
        if (!raw.consumed && !out.decoded_size) return d->error = -QM_EIO;
    }
    return QM_EOK;
}

static int decode_opus(decoder_t *d)
{
    esp_audio_dec_in_raw_t raw = {.buffer = d->packet, .len = d->packet_size};
    esp_audio_dec_out_frame_t out = {.buffer = d->pcm, .len = d->pcm_capacity};
    esp_audio_dec_info_t info = {0};
    esp_audio_err_t err = esp_opus_dec_decode(d->codec, &raw, &out, &info);
    if (err != ESP_AUDIO_ERR_OK || raw.consumed != d->packet_size ||
        !out.decoded_size || out.decoded_size > d->pcm_capacity)
        return d->error = -QM_EIO;
    if (d->packet_duration_ms != 0U) {
        uint32_t frame_bytes;
        uint64_t frames;
        if (info.sample_rate == 0U || info.channel == 0U ||
            out.decoded_size % ((uint32_t)info.channel * sizeof(int16_t)) != 0U)
            return d->error = -QM_EIO;
        frame_bytes = (uint32_t)info.channel * sizeof(int16_t);
        frames = out.decoded_size / frame_bytes;
        if (frames * 1000U != (uint64_t)d->packet_duration_ms * info.sample_rate)
            return d->error = -QM_ENOSYS;
    }
    return decoded_pcm(d, out.decoded_size, info.sample_rate, info.channel, info.bits_per_sample);
}

int qm_audio_decoder_push(void *ctx, const void *data, uint32_t size,
                          uint32_t timeout_ms, uint32_t *consumed)
{
    (void)timeout_ms; /* Synchronous CPU work; callbacks own cancellable I/O waits. */
    if (!consumed) return -QM_EINVAL;
    *consumed = 0U;
    decoder_t *d = ctx;
    if (!d || (size && !data)) return -QM_EINVAL;
    if (d->error) return d->error;
    if (d->sealed) return -QM_EBUSY;
    if (!size) return QM_EOK;
    const uint8_t *bytes = data;
    if (!d->opus) {
        /* Retain one byte so natural EOF is submitted with real input and
         * eos=true, as required by SDK parsers. close deliberately drops it. */
        if (d->tail_valid) {
            uint32_t used = 0;
            int ret = decode_mp3(d, &d->tail, 1U, QM_FALSE, &used);
            if (ret != QM_EOK) return ret;
            d->tail_valid = QM_FALSE;
        }
        while (*consumed + 1U < size) {
            uint32_t count = size - *consumed - 1U;
            if (count > DECODER_WORK_BYTES) count = DECODER_WORK_BYTES;
            uint32_t used = 0;
            int ret = decode_mp3(d, bytes + *consumed, count, QM_FALSE, &used);
            *consumed += used; d->input_bytes += used;
            if (ret != QM_EOK) return ret;
        }
        d->tail = bytes[size - 1U]; d->tail_valid = QM_TRUE;
        ++*consumed; ++d->input_bytes;
        return QM_EOK;
    }
    while (*consumed < size) {
        if (d->framing == QM_AUDIO_DECODER_FRAMING_OPUS_RAW_HEADER && !d->packet_size) {
            while (d->header_used < RAW_OPUS_HEADER_BYTES && *consumed < size)
                d->header[d->header_used++] = bytes[(*consumed)++];
            if (d->header_used != RAW_OPUS_HEADER_BYTES) break;
            d->packet_size = ((uint32_t)d->header[0] << 24U) |
                ((uint32_t)d->header[1] << 16U) | ((uint32_t)d->header[2] << 8U) | d->header[3];
            if (!d->packet_size || d->packet_size > d->packet_capacity)
                return d->error = -QM_EINVAL;
            /* The range-decoder field is framing metadata; the installed SDK
             * does not expose final_range validation. Opus decoder validates data. */
        }
        uint32_t count = d->packet_size - d->packet_used;
        if (count > size - *consumed) count = size - *consumed;
        qm_memcpy(d->packet + d->packet_used, bytes + *consumed, count);
        d->packet_used += count; *consumed += count; d->input_bytes += count;
        if (d->packet_used == d->packet_size) {
            int ret = decode_opus(d);
            if (ret != QM_EOK) return d->error = ret;
            d->packet_used = 0U; d->header_used = 0U;
            if (d->framing == QM_AUDIO_DECODER_FRAMING_OPUS_RAW_HEADER) d->packet_size = 0U;
        }
    }
    return QM_EOK;
}

int qm_audio_decoder_flush(void *ctx, uint32_t timeout_ms)
{
    (void)timeout_ms;
    decoder_t *d = ctx;
    if (!d) return -QM_EINVAL;
    if (d->error) return d->error;
    if (d->sealed) return QM_EOK;
    d->sealed = QM_TRUE;
    if (d->opus && (d->packet_used || d->header_used)) return d->error = -QM_EINVAL;
    if (!d->opus && d->tail_valid) {
        uint32_t used = 0;
        int ret = decode_mp3(d, &d->tail, 1U, QM_TRUE, &used);
        d->tail_valid = QM_FALSE;
        if (ret != QM_EOK) return ret;
    }
    if (d->input_bytes && !d->format_known) return d->error = -QM_EIO;
    return QM_EOK;
}

int qm_audio_decoder_close(void *ctx)
{
    decoder_t *d = ctx;
    if (!d) return -QM_EINVAL;
    if (d->codec) {
        if (d->opus) esp_opus_dec_close(d->codec);
        else esp_audio_simple_dec_close(d->codec);
    }
    if (d->pcm) qm_free(d->pcm);
    if (d->packet) qm_free(d->packet);
    qm_free(d);
    return QM_EOK;
}

int qm_audio_decoder_get_duration(void *ctx, uint32_t *duration_ms)
{
    decoder_t *d = ctx;
    if (!d || !duration_ms) return -QM_EINVAL;
    if (!d->sealed || d->error) return -QM_ENOSYS;
    uint64_t ms = d->output_frames * 1000U / d->output.sample_rate_hz;
    *duration_ms = ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
    return QM_EOK;
}

/* 本平台为软件解码, 不提供硬件断点续解: 续播记录请求明确失败, 由调用方
 * 回退为从流头完整重启。 */
int qm_audio_decoder_capture_resume(void *ctx, qm_audio_resume_point_t *resume,
                                    uint32_t timeout_ms)
{
    (void)ctx; (void)timeout_ms; (void)resume;
    return -QM_ENOSYS;
}
#else
int qm_audio_decoder_check_format(const qm_audio_pcm_format_t *f, const qm_audio_decoder_config_t *c)
{ (void)f; (void)c; return -QM_ENOSYS; }
int qm_audio_decoder_open(void **c, const qm_audio_pcm_format_t *f,
                          qm_audio_decoder_emit_pcm_fn e, void *u, void *a)
{ if (c) *c = NULL; (void)f; (void)e; (void)u; (void)a; return -QM_ENOSYS; }
int qm_audio_decoder_push(void *c, const void *p, uint32_t n, uint32_t t, uint32_t *used)
{ (void)c; (void)p; (void)n; (void)t; if (used) *used = 0U; return -QM_ENOSYS; }
int qm_audio_decoder_flush(void *c, uint32_t t) { (void)c; (void)t; return -QM_ENOSYS; }
int qm_audio_decoder_close(void *c) { (void)c; return -QM_ENOSYS; }
int qm_audio_decoder_get_duration(void *c, uint32_t *m) { (void)c; (void)m; return -QM_ENOSYS; }
int qm_audio_decoder_capture_resume(void *c, qm_audio_resume_point_t *r, uint32_t t)
{ (void)c; (void)t; (void)r; return -QM_ENOSYS; }
#endif
