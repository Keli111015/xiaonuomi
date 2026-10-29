/*
 * qm_audio_device.c
 * 平台 PCM 设备实现: adapter/qm_audio_device.h 具名函数接口的 ac7911bx
 * 实现, 基于杰理 audio_server 的 "pcm" 虚拟解码(输出到 DAC)。
 *
 * 线程模型:
 * - Engine 渲染 worker 是 playback_write 的唯一调用者(带超时背压);
 * - 杰理解码器任务经 VFS fread 从渲染队列拉 PCM 送 DAC;
 * - 事件回调投递到 app_core 任务(与平台既有约定一致), 只做置位与唤醒。
 *
 * 能力范围:
 * - 渲染: 16bit 交织 PCM, 支持单/双声道与任意采样率;
 * - 采集: 平台统一 16k/mono/s16 输入, 构建选择 PUSH 或 PULL;
 * - 音量/静音: 通过 AUDIO_DEC_SET_VOLUME / DCTRL 请求下发, 会话未启动时仅
 *   记录, 下次会话打开时生效。
 */
#define BOOL_DEFINE_CONFLICT
#define CONFIG_QM_WITHOUT_STDIO_H_SUPPORT 1

#include "adapter/qm_audio_device.h"
#include "qm_audio_device_priv.h"
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
#include "qm_audio_capture.h"
#endif

#include "generic/circular_buf.h"
#include "os/os_api.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_utils_timer.h"
#include "server/audio_server.h"
#include "server/server_core.h"

/* 渲染队列容量 / 字节(maps to kconfig QM_AUDIO_DEVICE_HW_QUEUE_BYTES). */
#ifndef CONFIG_QM_AUDIO_DEVICE_HW_QUEUE_BYTES
#define CONFIG_QM_AUDIO_DEVICE_HW_QUEUE_BYTES (32U * 1024U)
#endif
/* 默认音量 0-100(maps to kconfig QM_AUDIO_DEVICE_HW_VOLUME_DEFAULT). */
#ifndef CONFIG_QM_AUDIO_DEVICE_HW_VOLUME_DEFAULT
#define CONFIG_QM_AUDIO_DEVICE_HW_VOLUME_DEFAULT 80U
#endif
/* 采集镜像缓冲容量 / 字节(maps to kconfig QM_AUDIO_DEVICE_HW_CAPTURE_BYTES). */
#ifndef CONFIG_QM_AUDIO_DEVICE_HW_CAPTURE_BYTES
#define CONFIG_QM_AUDIO_DEVICE_HW_CAPTURE_BYTES (8U * 1024U)
#endif
#ifndef CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES
#define CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES 640U
#endif
#ifndef CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS
#define CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS 1000U
#endif
/* drain 兜底 GET_STATUS 查询的最小/最大间隔 / ms(maps to kconfig). */
#ifndef CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MIN_MS
#define CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MIN_MS 10U
#endif
#ifndef CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MAX_MS
#define CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MAX_MS 100U
#endif

#define LOG_TAG "qm_audio_dac"


extern u32 timer_get_ms(void);

typedef struct {
    cbuffer_t queue;
    u8 *queue_buf;
    OS_SEM data_sem;   /* 有新 PCM 可读 */
    OS_SEM space_sem;  /* 队列空出空间 / 解码结束通知 */
    OS_MUTEX lock;
    struct server *server;
    qm_audio_stream_format_t output_stream_format;
    qm_audio_pcm_format_t output_format;
    qm_audio_delivery_t delivery;
    uint64_t consumed_bytes;
    uint8_t volume;
    uint8_t mute;
    qm_bool opened;
    qm_bool active;
    qm_bool draining;
    qm_bool paused;
    qm_bool abort_write;
    volatile qm_bool decoder_end_seen;
    /* ---- Capture PUSH ingress is owned by qm_audio_capture_feed(). ---- */
    qm_audio_stream_format_t input_stream_format;
    qm_audio_pcm_format_t input_format;
    cbuffer_t cap_queue;
    u8 *cap_buf;
    u8 *cap_read_buf;
    OS_SEM cap_sem;   /* 有新采集 PCM */
    OS_MUTEX cap_lock;
    volatile qm_bool cap_started;
    volatile qm_bool cap_abort;
    uint32_t cap_inflight;
} backend_ctx_t;

static backend_ctx_t s_backend;
static uint8_t s_global_volume = CONFIG_QM_AUDIO_DEVICE_HW_VOLUME_DEFAULT;
/* The frontend task survives device close, so its ingress lock is resident. */
static OS_MUTEX s_capture_ingress_lock;
static qm_bool s_capture_ingress_ready;

static uint32_t bck_bytes_per_second(const backend_ctx_t *ctx)
{
    if (ctx == NULL || ctx->output_format.sample_rate_hz == 0U ||
        ctx->output_format.channels == 0U) {
        return 0U;
    }
    return ctx->output_format.sample_rate_hz *
           (uint32_t)ctx->output_format.channels * 2U; /* S16 固定 */
}

static void bck_lock(backend_ctx_t *ctx)
{
    if (ctx != NULL && os_mutex_valid(&ctx->lock)) {
        (void)os_mutex_pend(&ctx->lock, 0);
    }
}

static void bck_unlock(backend_ctx_t *ctx)
{
    if (ctx != NULL && os_mutex_valid(&ctx->lock)) {
        (void)os_mutex_post(&ctx->lock);
    }
}

/* ============================ DAC 虚拟解码 VFS ============================ */

static int dac_vfs_read(void *file, void *data, u32 len)
{
    backend_ctx_t *ctx = (backend_ctx_t *)file;
    uint32_t read_len;

    if (ctx == NULL || data == NULL || len == 0U) {
        return 0;
    }
    for (;;) {
        bck_lock(ctx);
        if (!ctx->active || ctx->abort_write) {
            bck_unlock(ctx);
            return 0;
        }
        if (ctx->paused) {
            bck_unlock(ctx);
            return -2;
        }
        read_len = cbuf_get_data_size(&ctx->queue);
        if (read_len > len) {
            read_len = len;
        }
        if (read_len != 0U) {
            read_len = cbuf_read(&ctx->queue, data, read_len);
            ctx->consumed_bytes += read_len;
            bck_unlock(ctx);
            (void)os_sem_post(&ctx->space_sem);
            return (int)read_len;
        }
        if (ctx->draining) {
            bck_unlock(ctx);
            return 0;
        }
        if (ctx->delivery == QM_AUDIO_DELIVERY_REALTIME) {
            bck_unlock(ctx);
            return -2;
        }
        bck_unlock(ctx);
        /* 有限等待: 停止/唤醒可能与本任务的挂起产生竞态, 短等待保证可退出。 */
        (void)os_sem_pend(&ctx->data_sem, 1);
    }
}

static const struct audio_vfs_ops s_dac_vfs = {
    .fread = dac_vfs_read,
};

static void dac_server_event(void *priv, int argc, int *argv)
{
    backend_ctx_t *ctx = (backend_ctx_t *)priv;

    if (ctx == NULL || argc <= 0 || argv == NULL) {
        return;
    }
    if (argv[0] == AUDIO_SERVER_EVENT_END) {
        bck_lock(ctx);
        if (!ctx->paused) ctx->decoder_end_seen = QM_TRUE;
        bck_unlock(ctx);
        /* 唤醒正在等待的 drain。 */
        (void)os_sem_post(&ctx->space_sem);
    }
}

static void dac_server_close(backend_ctx_t *ctx)
{
    union audio_req req;

    if (ctx == NULL || ctx->server == NULL) {
        return;
    }
    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_STOP;
    (void)server_request(ctx->server, AUDIO_REQ_DEC, &req);
    server_close(ctx->server);
    ctx->server = NULL;
}

/* 解码器在首批 PCM 入队后才打开: 部分 SDK 在 AUDIO_DEC_OPEN 期间探测虚拟
 * PCM 源, 提前打开会因空队列无限等待。 */
static int dac_server_start(backend_ctx_t *ctx)
{
    union audio_req req;
    int ret;

    if (ctx->server != NULL) {
        return QM_EOK;
    }
    ctx->server = server_open("audio_server", "dec");
    if (ctx->server == NULL) {
        QM_LOGE(LOG_TAG, "dac server open failed");
        return -QM_EIO;
    }
    ctx->decoder_end_seen = QM_FALSE;
    server_register_event_handler_to_task(ctx->server, ctx, dac_server_event,
                                          "app_core");

    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_OPEN;
    req.dec.volume = ctx->mute ? 0U : ctx->volume;
    req.dec.priority = 1U;
    req.dec.output_buf_len = CONFIG_QM_AUDIO_DEVICE_HW_QUEUE_BYTES / 2U;
    req.dec.file = (FILE *)ctx;
    req.dec.vfs_ops = &s_dac_vfs;
    req.dec.dec_type = "pcm";
    req.dec.sample_source = "dac";
    req.dec.sample_rate = ctx->output_format.sample_rate_hz;
    req.dec.channel = (u8)ctx->output_format.channels;
    if (ctx->delivery == QM_AUDIO_DELIVERY_REALTIME) {
        req.dec.attr = AUDIO_ATTR_REAL_TIME;
    }
    ret = server_request(ctx->server, AUDIO_REQ_DEC, &req);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "dac decoder open failed ret=%d", ret);
        dac_server_close(ctx);
        return ret != 0 ? -QM_EIO : QM_EOK;
    }
    QM_LOGI(LOG_TAG, "dac decoder opened rate=%u ch=%u",
            (unsigned int)ctx->output_format.sample_rate_hz,
            (unsigned int)ctx->output_format.channels);

    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_START;
    req.dec.attr = AUDIO_ATTR_NO_WAIT_READY;
    ret = server_request(ctx->server, AUDIO_REQ_DEC, &req);
    if (ret != 0) {
        QM_LOGE(LOG_TAG, "dac decoder start failed ret=%d", ret);
        dac_server_close(ctx);
        return -QM_EIO;
    }
    return QM_EOK;
}

/* GET_STATUS 兜底: 部分 SDK 在虚拟 PCM EOF 后状态仍停留在 START。 */
static int dac_server_is_stopped(backend_ctx_t *ctx, qm_bool *stopped)
{
    union audio_req req;
    int ret;

    if (ctx == NULL || stopped == NULL) {
        return -QM_EINVAL;
    }
    if (ctx->server == NULL) {
        *stopped = QM_TRUE;
        return QM_EOK;
    }
    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_GET_STATUS;
    ret = server_request(ctx->server, AUDIO_REQ_DEC, &req);
    if (ret != 0) {
        return -QM_EIO;
    }
    *stopped = req.dec.status == AUDIO_DEC_STOP ? QM_TRUE : QM_FALSE;
    return QM_EOK;
}

/* ============================ 采集镜像生命周期 ============================ */

static int bck_capture_init(backend_ctx_t *ctx)
{
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    if (ctx->input_format.sample_rate_hz == 0U) {
        return QM_EOK;
    }
    ctx->cap_buf = (u8 *)qm_malloc(CONFIG_QM_AUDIO_DEVICE_HW_CAPTURE_BYTES);
    if (ctx->cap_buf == NULL) {
        return -QM_ENOMEM;
    }
    ctx->cap_read_buf = (u8 *)qm_malloc(CONFIG_QM_AUDIO_DEVICE_HW_CAPTURE_BYTES);
    if (ctx->cap_read_buf == NULL) {
        qm_free(ctx->cap_buf);
        ctx->cap_buf = NULL;
        return -QM_ENOMEM;
    }
    cbuf_init(&ctx->cap_queue, ctx->cap_buf,
              CONFIG_QM_AUDIO_DEVICE_HW_CAPTURE_BYTES);
    if (os_sem_create(&ctx->cap_sem, 0) != 0 ||
        os_mutex_create(&ctx->cap_lock) != 0) {
        goto fail;
    }
    return QM_EOK;

fail:
    if (os_mutex_valid(&ctx->cap_lock)) {
        (void)os_mutex_del(&ctx->cap_lock, OS_DEL_ALWAYS);
    }
    if (os_sem_valid(&ctx->cap_sem)) {
        (void)os_sem_del(&ctx->cap_sem, OS_DEL_ALWAYS);
    }
    qm_free(ctx->cap_buf);
    ctx->cap_buf = NULL;
    qm_free(ctx->cap_read_buf);
    ctx->cap_read_buf = NULL;
    return -QM_ENOMEM;
#else
    (void)ctx;
    return QM_EOK;
#endif
}

static void bck_capture_deinit(backend_ctx_t *ctx)
{
    ctx->cap_started = QM_FALSE;
    ctx->cap_abort = QM_FALSE;
    if (os_mutex_valid(&ctx->cap_lock)) {
        (void)os_mutex_del(&ctx->cap_lock, OS_DEL_ALWAYS);
    }
    if (os_sem_valid(&ctx->cap_sem)) {
        (void)os_sem_del(&ctx->cap_sem, OS_DEL_ALWAYS);
    }
    if (ctx->cap_buf != NULL) {
        qm_free(ctx->cap_buf);
        ctx->cap_buf = NULL;
    }
    if (ctx->cap_read_buf != NULL) {
        qm_free(ctx->cap_read_buf);
        ctx->cap_read_buf = NULL;
    }
}

/* ============================ 接口实现 ============================ */

int qm_audio_device_open(void **backend_ctx,
                          const qm_audio_device_config_t *config)
{
    backend_ctx_t *ctx = &s_backend;

    if (backend_ctx == NULL || config == NULL) {
        return -QM_EINVAL;
    }
    *backend_ctx = NULL;
    if (config->flags != 0U || config->output_format.max_packet_bytes != 0U ||
        config->output_format.packet_bytes != 0U ||
        config->output_format.packet_duration_ms != 0U ||
        config->input_format.max_packet_bytes != 0U ||
        config->input_format.packet_bytes != 0U ||
        config->input_format.packet_duration_ms != 0U) {
        return -QM_EINVAL;
    }
    /* 渲染仅支持 16bit 交织; 当前采集格式固定 16k/mono/s16。 */
    if (config->output_format.codec != QM_AUDIO_CODEC_PCM ||
        (config->output_format.pcm.sample_rate_hz != 0U &&
        ((config->output_format.pcm.channels != 1U &&
         config->output_format.pcm.channels != 2U) ||
         config->output_format.pcm.sample != QM_AUDIO_PCM_S16))) {
        QM_LOGE(LOG_TAG, "unsupported render format rate=%u ch=%u sample=%d",
                (unsigned int)config->output_format.pcm.sample_rate_hz,
                (unsigned int)config->output_format.pcm.channels,
                (int)config->output_format.pcm.sample);
        return -QM_ENOSYS;
    }
    if ((config->output_format.pcm.sample_rate_hz == 0U &&
         config->input_format.pcm.sample_rate_hz == 0U) ||
        CONFIG_QM_AUDIO_DEVICE_HW_QUEUE_BYTES < 4U ||
        CONFIG_QM_AUDIO_DEVICE_HW_QUEUE_BYTES % 4U != 0U ||
        CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES < 2U) {
        return -QM_EINVAL;
    }
    /* 采集启用时使用平台原生 16k/mono/s16 格式。 */
    if (config->input_format.codec != QM_AUDIO_CODEC_PCM ||
        (config->input_format.pcm.sample_rate_hz != 0U &&
        (config->input_format.pcm.sample_rate_hz != 16000U ||
         config->input_format.pcm.channels != 1U ||
         config->input_format.pcm.sample != QM_AUDIO_PCM_S16))) {
        QM_LOGE(LOG_TAG, "unsupported capture format rate=%u ch=%u sample=%d",
                (unsigned int)config->input_format.pcm.sample_rate_hz,
                (unsigned int)config->input_format.pcm.channels,
                (int)config->input_format.pcm.sample);
        return -QM_ENOSYS;
    }
    if (ctx->opened) {
        /* This adapter has one physical route and one lifecycle owner. */
        return -QM_EBUSY;
    }
    if (!s_capture_ingress_ready) {
        if (os_mutex_create(&s_capture_ingress_lock) != 0) {
            return -QM_ENOMEM;
        }
        s_capture_ingress_ready = QM_TRUE;
    }

    (void)os_mutex_pend(&s_capture_ingress_lock, 0);
    qm_memset(ctx, 0, sizeof(*ctx));
    ctx->output_stream_format = config->output_format;
    ctx->input_stream_format = config->input_format;
    ctx->output_format = config->output_format.pcm;
    ctx->input_format = config->input_format.pcm;
    ctx->queue_buf = (u8 *)qm_malloc(CONFIG_QM_AUDIO_DEVICE_HW_QUEUE_BYTES);
    if (ctx->queue_buf == NULL) {
        goto fail;
    }
    cbuf_init(&ctx->queue, ctx->queue_buf, CONFIG_QM_AUDIO_DEVICE_HW_QUEUE_BYTES);
    if (os_sem_create(&ctx->data_sem, 0) != 0 ||
        os_sem_create(&ctx->space_sem, 0) != 0 ||
        os_mutex_create(&ctx->lock) != 0) {
        goto fail;
    }
    if (bck_capture_init(ctx) != QM_EOK) {
        goto fail;
    }
    ctx->volume = s_global_volume;
    ctx->opened = QM_TRUE;
    (void)os_mutex_post(&s_capture_ingress_lock);
    *backend_ctx = ctx;
    QM_LOGI(LOG_TAG, "device ready rate=%u ch=%u cap=%u",
            (unsigned int)ctx->output_format.sample_rate_hz,
            (unsigned int)ctx->output_format.channels,
            ctx->input_format.sample_rate_hz != 0U ? 1U : 0U);
    return QM_EOK;

fail:
    bck_capture_deinit(ctx);
    if (os_mutex_valid(&ctx->lock)) {
        (void)os_mutex_del(&ctx->lock, OS_DEL_ALWAYS);
    }
    if (os_sem_valid(&ctx->space_sem)) {
        (void)os_sem_del(&ctx->space_sem, OS_DEL_ALWAYS);
    }
    if (os_sem_valid(&ctx->data_sem)) {
        (void)os_sem_del(&ctx->data_sem, OS_DEL_ALWAYS);
    }
    if (ctx->queue_buf != NULL) {
        qm_free(ctx->queue_buf);
    }
    qm_memset(ctx, 0, sizeof(*ctx));
    (void)os_mutex_post(&s_capture_ingress_lock);
    return -QM_ENOMEM;
}

int qm_audio_device_close(void *backend_ctx)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return QM_EOK;
    }
    if (qm_audio_device_capture_stop(ctx) != QM_EOK) {
        return -QM_ETIMEOUT;
    }
    (void)os_mutex_pend(&s_capture_ingress_lock, 0);
    ctx->opened = QM_FALSE;
    bck_lock(ctx);
    ctx->active = QM_FALSE;
    ctx->draining = QM_FALSE;
    ctx->paused = QM_FALSE;
    ctx->abort_write = QM_TRUE;
    ctx->decoder_end_seen = QM_FALSE;
    cbuf_clear(&ctx->queue);
    bck_unlock(ctx);
    (void)os_sem_post(&ctx->data_sem);
    (void)os_sem_post(&ctx->space_sem);
    dac_server_close(ctx);
    bck_capture_deinit(ctx);
    if (os_mutex_valid(&ctx->lock)) {
        (void)os_mutex_del(&ctx->lock, OS_DEL_ALWAYS);
    }
    if (os_sem_valid(&ctx->space_sem)) {
        (void)os_sem_del(&ctx->space_sem, OS_DEL_ALWAYS);
    }
    if (os_sem_valid(&ctx->data_sem)) {
        (void)os_sem_del(&ctx->data_sem, OS_DEL_ALWAYS);
    }
    if (ctx->queue_buf != NULL) {
        qm_free(ctx->queue_buf);
    }
    qm_memset(ctx, 0, sizeof(*ctx));
    (void)os_mutex_post(&s_capture_ingress_lock);
    return QM_EOK;
}

int qm_audio_device_get_caps(void *backend_ctx, qm_audio_device_caps_t *caps)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;

    if (ctx == NULL || caps == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    qm_memset(caps, 0, sizeof(*caps));
    caps->input_format = ctx->input_stream_format;
    caps->output_format = ctx->output_stream_format;
    caps->flags = QM_AUDIO_DEVICE_CAP_PLAYBACK |
                  QM_AUDIO_DEVICE_CAP_VOLUME |
                  QM_AUDIO_DEVICE_CAP_MUTE |
                  QM_AUDIO_DEVICE_CAP_DRAIN |
                  QM_AUDIO_DEVICE_CAP_ABORT_WRITE |
                  QM_AUDIO_DEVICE_CAP_POSITION;
    if (ctx->output_format.sample_rate_hz == 0U) {
        caps->flags = 0U;
    }
    if (ctx->input_format.sample_rate_hz != 0U) {
        caps->flags |= QM_AUDIO_DEVICE_CAP_CAPTURE |
                       QM_AUDIO_DEVICE_CAP_ABORT_READ;
    }
    return QM_EOK;
}

int qm_audio_device_playback_start(void *backend_ctx,
                                  qm_audio_delivery_t delivery)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;

    if (ctx == NULL ||
        (delivery != QM_AUDIO_DELIVERY_RELIABLE &&
         delivery != QM_AUDIO_DELIVERY_REALTIME)) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    if (ctx->output_format.sample_rate_hz == 0U) {
        return -QM_ENOSYS;
    }
    if (ctx->active) {
        return -QM_EBUSY;
    }
    /* 上个会话可能异常中断: 复位并关闭旧解码会话。 */
    bck_lock(ctx);
    ctx->active = QM_FALSE;
    ctx->draining = QM_FALSE;
    ctx->paused = QM_FALSE;
    ctx->abort_write = QM_TRUE;
    cbuf_clear(&ctx->queue);
    bck_unlock(ctx);
    (void)os_sem_post(&ctx->data_sem);
    (void)os_sem_post(&ctx->space_sem);
    if (ctx->server != NULL) {
        dac_server_close(ctx);
    }
    bck_lock(ctx);
    cbuf_clear(&ctx->queue);
    ctx->consumed_bytes = 0U;
    ctx->delivery = delivery;
    ctx->draining = QM_FALSE;
    ctx->paused = QM_FALSE;
    ctx->abort_write = QM_FALSE;
    ctx->active = QM_TRUE;
    bck_unlock(ctx);
    /* 解码器由 playback_write 在首批 PCM 入队后懒启动, 规避空源探测阻塞。 */
    return QM_EOK;
}

int qm_audio_device_playback_write(void *backend_ctx,
                                  const qm_audio_data_t *input,
                                  uint32_t timeout_ms)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    const u8 *cursor;
    qm_utils_time_t deadline;
    uint32_t accepted = 0U;

    if (ctx == NULL || input == NULL || input->data == NULL || input->size == 0U) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    if (ctx->output_format.sample_rate_hz == 0U) {
        return -QM_ENOSYS;
    }
    if (ctx->output_stream_format.codec != QM_AUDIO_CODEC_PCM ||
        input->size % (ctx->output_format.channels * 2U) != 0U ||
        (input->flags & ~(QM_AUDIO_DATA_DISCONTINUITY)) != 0U ||
        input->timestamp_ms != 0U || input->duration_ms != 0U || input->sequence != 0U) {
        return -QM_EINVAL;
    }
    cursor = (const u8 *)input->data;
    if (timeout_ms != QM_WAIT_FOREVER) {
        qm_utils_time_countdown_ms(&deadline, timeout_ms);
    }
    while (accepted < input->size) {
        uint32_t chunk;

        bck_lock(ctx);
        if (!ctx->active || ctx->abort_write) {
            bck_unlock(ctx);
            return -QM_EINTR;
        }
        if (ctx->draining) {
            bck_unlock(ctx);
            return -QM_EINTR;
        }
        if (ctx->delivery == QM_AUDIO_DELIVERY_REALTIME &&
            cbuf_is_write_able(&ctx->queue, input->size - accepted) == 0U) {
            /* 实时模式丢弃积压, 只保留最新数据。 */
            cbuf_clear(&ctx->queue);
        }
        chunk = cbuf_write(&ctx->queue, (void *)(cursor + accepted),
                           input->size - accepted);
        bck_unlock(ctx);
        if (chunk != 0U) {
            accepted += chunk;
            /* 懒启动解码会话(见 dac_server_start 注释)。 */
            if (ctx->server == NULL && dac_server_start(ctx) != QM_EOK) {
                bck_lock(ctx);
                ctx->active = QM_FALSE;
                ctx->abort_write = QM_TRUE;
                bck_unlock(ctx);
                return -QM_EIO;
            }
            (void)os_sem_post(&ctx->data_sem);
            continue;
        }
        if (ctx->delivery == QM_AUDIO_DELIVERY_REALTIME) {
            bck_lock(ctx);
            ctx->active = QM_FALSE;
            ctx->abort_write = QM_TRUE;
            bck_unlock(ctx);
            (void)os_sem_post(&ctx->data_sem);
            (void)os_sem_post(&ctx->space_sem);
            return -QM_ETIMEOUT;
        }
        if (timeout_ms != QM_WAIT_FOREVER &&
            qm_utils_time_is_expired(&deadline)) {
            break;
        }
        (void)os_sem_pend(&ctx->space_sem,
                          timeout_ms == QM_WAIT_FOREVER ? 0 : 1);
    }
    if (accepted != input->size) {
        bck_lock(ctx);
        ctx->active = QM_FALSE;
        ctx->abort_write = QM_TRUE;
        bck_unlock(ctx);
        (void)os_sem_post(&ctx->data_sem);
        (void)os_sem_post(&ctx->space_sem);
        return -QM_ETIMEOUT;
    }
    return QM_EOK;
}

int qm_audio_device_playback_drain(void *backend_ctx, uint32_t timeout_ms)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    qm_utils_time_t deadline;
    qm_utils_time_t next_query;
    uint32_t query_interval = CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MIN_MS;
    uint32_t last_status_log_ms = 0U;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    if (ctx->output_format.sample_rate_hz == 0U) {
        return -QM_ENOSYS;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    qm_utils_time_countdown_ms(&next_query, 0U); /* 首次立即查询 */
    for (;;) {
        uint32_t queued;
        qm_bool stopped = QM_FALSE;
        int ret;

        bck_lock(ctx);
        if (!ctx->active || ctx->abort_write) {
            bck_unlock(ctx);
            return -QM_EINTR;
        }
        ctx->draining = QM_TRUE;
        queued = cbuf_get_data_size(&ctx->queue);
        bck_unlock(ctx);
        (void)os_sem_post(&ctx->data_sem);
        if (queued == 0U) {
            qm_bool queried = QM_FALSE;
            bck_lock(ctx);
            stopped = ctx->decoder_end_seen;
            bck_unlock(ctx);
            ret = QM_EOK;
            /* GET_STATUS 兜底: 部分 SDK 虚拟 PCM 源不发 END 事件。主完成信号是
             * AUDIO_SERVER_EVENT_END 置位的 decoder_end_seen, 兜底查询带指数退避,
             * 避免每次停流都以一个 tick 的间隔向 audio_server 发同步 IPC 请求。 */
            if (!stopped && qm_utils_time_is_expired(&next_query)) {
                ret = dac_server_is_stopped(ctx, &stopped);
                queried = QM_TRUE;
                if (query_interval < CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MAX_MS) {
                    query_interval *= 2U;
                    if (query_interval > CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MAX_MS) {
                        query_interval = CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MAX_MS;
                    }
                }
                qm_utils_time_countdown_ms(&next_query, query_interval);
            }
            /* 轮询兜底避免逐毫秒刷日志。 */
            if (last_status_log_ms == 0U ||
                (uint32_t)(timer_get_ms() - last_status_log_ms) >= 500U ||
                ret != QM_EOK || stopped) {
                QM_LOGI(LOG_TAG, "drain status queued=0 ret=%d stopped=%u queried=%u",
                        ret, (unsigned int)stopped, (unsigned int)queried);
                last_status_log_ms = timer_get_ms();
            }
            if (ret != QM_EOK) {
                return ret;
            }
            if (stopped) {
                QM_LOGI(LOG_TAG, "drain done queued=0 status=stopped");
                return QM_EOK;
            }
        } else {
            /* 队列有数据说明解码器在消费, 恢复兜底查询的敏感度。 */
            query_interval = CONFIG_QM_AUDIO_DEVICE_DRAIN_QUERY_MIN_MS;
        }
        if (timeout_ms != QM_WAIT_FOREVER && qm_utils_time_is_expired(&deadline)) {
            QM_LOGW(LOG_TAG, "drain timeout queued=%u consumed=%u",
                    (unsigned int)queued, (unsigned int)ctx->consumed_bytes);
            return -QM_ETIMEOUT;
        }
        (void)os_sem_pend(&ctx->space_sem, 1);
    }
}

int qm_audio_device_playback_pause(void *backend_ctx)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    union audio_req req = {0};
    int ret = QM_EOK;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    if (!ctx->active) {
        return -QM_EBUSY;
    }
    bck_lock(ctx);
    ctx->paused = QM_TRUE;
    bck_unlock(ctx);
    (void)os_sem_post(&ctx->data_sem);
    if (ctx->server != NULL) {
        req.dec.cmd = AUDIO_DEC_PAUSE;
        ret = server_request(ctx->server, AUDIO_REQ_DEC, &req);
    }
    if (ret != 0) {
        bck_lock(ctx);
        ctx->paused = QM_FALSE;
        bck_unlock(ctx);
    }
    return ret == 0 ? QM_EOK : -QM_EIO;
}

int qm_audio_device_playback_resume(void *backend_ctx)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    union audio_req req = {0};
    int ret = QM_EOK;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    if (!ctx->active) {
        return -QM_EBUSY;
    }
    bck_lock(ctx);
    ctx->paused = QM_FALSE;
    ctx->decoder_end_seen = QM_FALSE;
    bck_unlock(ctx);
    if (ctx->server != NULL) {
        req.dec.cmd = AUDIO_DEC_START;
        ret = server_request(ctx->server, AUDIO_REQ_DEC, &req);
    }
    if (ret != 0) {
        bck_lock(ctx);
        ctx->paused = QM_TRUE;
        bck_unlock(ctx);
    }
    return ret == 0 ? QM_EOK : -QM_EIO;
}

int qm_audio_device_playback_abort(void *backend_ctx)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    bck_lock(ctx);
    ctx->abort_write = QM_TRUE;
    bck_unlock(ctx);
    (void)os_sem_post(&ctx->data_sem);
    (void)os_sem_post(&ctx->space_sem);
    return QM_EOK;
}

int qm_audio_device_playback_stop(void *backend_ctx)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    bck_lock(ctx);
    ctx->active = QM_FALSE;
    ctx->draining = QM_FALSE;
    ctx->paused = QM_FALSE;
    ctx->abort_write = QM_TRUE;
    cbuf_clear(&ctx->queue);
    bck_unlock(ctx);
    (void)os_sem_post(&ctx->data_sem);
    (void)os_sem_post(&ctx->space_sem);
    /* 唤醒阻塞的 VFS 读后同步停止解码会话, 保证设备 I/O 静默。 */
    dac_server_close(ctx);
    return QM_EOK;
}

int qm_audio_device_playback_get_position(void *backend_ctx,
                                         uint32_t *position_ms)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    uint32_t bytes_per_second;
    uint64_t consumed_bytes;

    if (ctx == NULL || position_ms == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    bytes_per_second = bck_bytes_per_second(ctx);
    if (bytes_per_second == 0U) {
        return -QM_ENOSYS;
    }
    bck_lock(ctx);
    consumed_bytes = ctx->consumed_bytes;
    bck_unlock(ctx);
    /* DAC 已消费字节折算: 计数来自虚拟解码 VFS 的实读量。 */
    {
        uint64_t ms = consumed_bytes / bytes_per_second * 1000U +
                      consumed_bytes % bytes_per_second * 1000U / bytes_per_second;
        *position_ms = ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
    }
    return QM_EOK;
}

int qm_audio_device_capture_start(void *backend_ctx)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    if (ctx->input_format.sample_rate_hz == 0U) {
        return -QM_ENOSYS;
    }
    if (ctx->input_stream_format.codec != QM_AUDIO_CODEC_PCM) {
        return -QM_ENOSYS;
    }
    (void)os_mutex_pend(&s_capture_ingress_lock, 0);
    if (os_mutex_valid(&ctx->cap_lock)) {
        (void)os_mutex_pend(&ctx->cap_lock, 0);
    }
    ctx->cap_started = QM_TRUE;
    ctx->cap_abort = QM_FALSE;
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    cbuf_clear(&ctx->cap_queue);
#endif
    if (os_mutex_valid(&ctx->cap_lock)) {
        (void)os_mutex_post(&ctx->cap_lock);
    }
    (void)os_mutex_post(&s_capture_ingress_lock);
    return QM_EOK;
}

#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
int qm_audio_device_capture_read(void *backend_ctx,
                                 qm_audio_data_t *input,
                                 uint32_t timeout_ms)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    qm_utils_time_t deadline;
    uint32_t cap = CONFIG_QM_AUDIO_DEVICE_HW_CAPTURE_BYTES & ~1U;

    if (ctx == NULL || input == NULL) {
        return -QM_EINVAL;
    }
    /* The adapter contract clears output metadata on every failed read. */
    input->data = NULL;
    input->size = 0U;
    input->timestamp_ms = 0U;
    input->duration_ms = 0U;
    input->sequence = 0U;
    input->flags = 0U;
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    if (ctx->input_format.sample_rate_hz == 0U) {
        return -QM_ENOSYS;
    }
    if (cap > (CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES & ~1U)) {
        cap = CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES & ~1U;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        uint32_t avail;
        uint32_t n;

        if (ctx->cap_abort) {
            return -QM_EINTR;
        }
        if (os_mutex_valid(&ctx->cap_lock)) {
            (void)os_mutex_pend(&ctx->cap_lock, 0);
        }
        avail = cbuf_get_data_size(&ctx->cap_queue);
        n = avail > cap ? cap : avail;
        n &= ~1U;
        if (n != 0U) {
            n = cbuf_read(&ctx->cap_queue, ctx->cap_read_buf, n);
            if (os_mutex_valid(&ctx->cap_lock)) {
                (void)os_mutex_post(&ctx->cap_lock);
            }
            input->data = ctx->cap_read_buf;
            input->size = n;
            input->timestamp_ms = 0U;
            input->duration_ms = 0U;
            input->sequence = 0U;
            input->flags = 0U;
            return QM_EOK;
        }
        if (os_mutex_valid(&ctx->cap_lock)) {
            (void)os_mutex_post(&ctx->cap_lock);
        }
        if (timeout_ms == 0U) {
            return -QM_ETIMEOUT;
        }
        {
            uint32_t left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                            qm_utils_time_left(&deadline);

            if (left == 0U) {
                return -QM_ETIMEOUT;
            }
            /* JL os_sem_pend 以 10ms 为刻度; 短等待循环便于 abort 及时中断。 */
            (void)os_sem_pend(&ctx->cap_sem, 1);
        }
    }
}
#endif

int qm_audio_device_capture_abort(void *backend_ctx)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    /* 无采集能力时保持 no-op(Engine shutdown 恒可收敛)。 */
    if (ctx->input_format.sample_rate_hz == 0U) {
        return QM_EOK;
    }
    (void)os_mutex_pend(&s_capture_ingress_lock, 0);
    ctx->cap_abort = QM_TRUE;
    (void)os_mutex_post(&s_capture_ingress_lock);
    if (os_sem_valid(&ctx->cap_sem)) {
        (void)os_sem_post(&ctx->cap_sem);
    }
    return QM_EOK;
}

int qm_audio_device_capture_stop(void *backend_ctx)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    qm_utils_time_t deadline = {0};
    uint32_t inflight = 0U;

    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    if (ctx->input_format.sample_rate_hz == 0U) {
        return QM_EOK;
    }
    (void)os_mutex_pend(&s_capture_ingress_lock, 0);
    ctx->cap_started = QM_FALSE;
    ctx->cap_abort = QM_TRUE;
    (void)os_mutex_post(&s_capture_ingress_lock);
    qm_utils_time_countdown_ms(&deadline, CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS);
    for (;;) {
        (void)os_mutex_pend(&s_capture_ingress_lock, 0);
        inflight = ctx->cap_inflight;
        (void)os_mutex_post(&s_capture_ingress_lock);
        if (inflight == 0U) {
            break;
        }
        if (qm_utils_time_is_expired(&deadline)) {
            return -QM_ETIMEOUT;
        }
        os_time_dly(1);
    }
    if (os_mutex_valid(&ctx->cap_lock)) {
        (void)os_mutex_pend(&ctx->cap_lock, 0);
    }
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    cbuf_clear(&ctx->cap_queue);
#endif
    if (os_mutex_valid(&ctx->cap_lock)) {
        (void)os_mutex_post(&ctx->cap_lock);
    }
    return QM_EOK;
}

int qm_audio_device_capture_submit(const qm_audio_data_t *input)
{
    backend_ctx_t *ctx = &s_backend;
    qm_audio_data_t chunk = {0};
    uint32_t offset = 0U;
    uint32_t frame_bytes = CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES & ~1U;
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    uint32_t written = 0U;
#endif
    int ret = QM_EOK;

    if (input == NULL || input->data == NULL || input->size == 0U ||
        input->size % sizeof(int16_t) != 0U || input->flags != 0U ||
        input->timestamp_ms != 0U || input->duration_ms != 0U || input->sequence != 0U) {
        return -QM_EINVAL;
    }
    if (!s_capture_ingress_ready) {
        return -QM_EBUSY;
    }
    (void)os_mutex_pend(&s_capture_ingress_lock, 0);
    if (!ctx->opened || !ctx->cap_started || ctx->cap_abort) {
        (void)os_mutex_post(&s_capture_ingress_lock);
        return -QM_EBUSY;
    }
    if (ctx->cap_inflight == UINT32_MAX || frame_bytes == 0U) {
        (void)os_mutex_post(&s_capture_ingress_lock);
        return -QM_EBUSY;
    }
    ctx->cap_inflight++;
    (void)os_mutex_post(&s_capture_ingress_lock);
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
    while (offset < input->size && ret == QM_EOK) {
        chunk = *input;
        chunk.data = (const u8 *)input->data + offset;
        chunk.size = input->size - offset;
        if (chunk.size > frame_bytes) {
            chunk.size = frame_bytes;
        }
        ret = qm_audio_capture_feed(&chunk, QM_EOK);
        offset += chunk.size;
    }
#else
    (void)chunk;
    (void)offset;
    (void)os_mutex_pend(&ctx->cap_lock, 0);
    if (input->size > CONFIG_QM_AUDIO_DEVICE_HW_CAPTURE_BYTES) {
        ret = -QM_EINVAL;
    } else {
        if (cbuf_is_write_able(&ctx->cap_queue, input->size) == 0U) {
            cbuf_clear(&ctx->cap_queue);
        }
        written = cbuf_write(&ctx->cap_queue, (void *)input->data, input->size);
        ret = written == input->size ? QM_EOK : -QM_EIO;
    }
    (void)os_mutex_post(&ctx->cap_lock);
    if (written != 0U) {
        (void)os_sem_post(&ctx->cap_sem);
    }
#endif
    (void)os_mutex_pend(&s_capture_ingress_lock, 0);
    ctx->cap_inflight--;
    (void)os_mutex_post(&s_capture_ingress_lock);
    return ret;
}

int qm_audio_device_set_volume(void *backend_ctx, uint8_t volume)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    union audio_req req;

    if (ctx == NULL || volume > 100U) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    ctx->volume = volume;
    if (ctx->server == NULL) {
        /* 会话未启动: 记录值, 下次 dac_server_start 生效。 */
        return QM_EOK;
    }
    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_SET_VOLUME;
    req.dec.volume = ctx->mute ? 0U : ctx->volume;
    return server_request(ctx->server, AUDIO_REQ_DEC, &req) == 0 ?
           QM_EOK : -QM_EIO;
}

int qm_audio_device_set_mute(void *backend_ctx, qm_bool muted)
{
    backend_ctx_t *ctx = (backend_ctx_t *)backend_ctx;
    union audio_req req;

    if (ctx == NULL || (muted != QM_TRUE && muted != QM_FALSE)) {
        return -QM_EINVAL;
    }
    if (!ctx->opened) {
        return -QM_EINIT;
    }
    ctx->mute = muted ? 1U : 0U;
    if (ctx->server == NULL) {
        return QM_EOK;
    }
    qm_memset(&req, 0, sizeof(req));
    req.dec.cmd = AUDIO_DEC_DIGITAL_MUTE_SET;
    req.dec.attr = ctx->mute ? AUDIO_ATTR_DEC_MUTE_EN : 0U;
    return server_request(ctx->server, AUDIO_REQ_DEC, &req) == 0 ?
           QM_EOK : -QM_EIO;
}
