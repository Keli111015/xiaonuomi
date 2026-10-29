#include "adapter/qm_audio_device.h"
#include "qm_audio.h"
#include "qm_audio_capture.h"
#include "nc_audio.h"
#include "qm.h"
#include "qm_gpio.h"
#include "qm_string.h"
#include "qm_uart.h"
#include "qm_utils_timer.h"
#include "driver/uart.h"

#ifndef CONFIG_QM_GX8006_UART_PORT
#define CONFIG_QM_GX8006_UART_PORT 1
#endif
#ifndef CONFIG_QM_GX8006_UART_BAUDRATE
#define CONFIG_QM_GX8006_UART_BAUDRATE 115200
#endif
#ifndef CONFIG_QM_GX8006_UART_TX_PIN
#define CONFIG_QM_GX8006_UART_TX_PIN 40
#endif
#ifndef CONFIG_QM_GX8006_UART_RX_PIN
#define CONFIG_QM_GX8006_UART_RX_PIN 39
#endif
#ifndef CONFIG_QM_GX8006_FLOW_GPIO
#define CONFIG_QM_GX8006_FLOW_GPIO 38
#endif
#ifndef CONFIG_QM_GX8006_UART_RECV_SIZE
#define CONFIG_QM_GX8006_UART_RECV_SIZE 512
#endif
#ifndef CONFIG_QM_GX8006_UART_TASK_STACK_SIZE
#define CONFIG_QM_GX8006_UART_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_QM_GX8006_UART_TASK_PRIORITY
#define CONFIG_QM_GX8006_UART_TASK_PRIORITY 20
#endif
#ifndef CONFIG_QM_GX8006_MIC_GAIN
#define CONFIG_QM_GX8006_MIC_GAIN 30
#endif
#ifndef CONFIG_QM_GX8006_SPK_GAIN
#define CONFIG_QM_GX8006_SPK_GAIN 30
#endif
#ifndef CONFIG_QM_GX8006_VAD_TIMEOUT
#define CONFIG_QM_GX8006_VAD_TIMEOUT 10
#endif
#ifndef CONFIG_QM_GX8006_STOP_TIMEOUT_MS
#define CONFIG_QM_GX8006_STOP_TIMEOUT_MS 5000
#endif

#define LOG_TAG "gx8006"

#define GX_PACKET_BYTES 80U
#define GX_PACKET_DURATION_MS 40U
#define GX_FLOW_WAIT_MS 2U
#define GX_SPK_LEAD_MS 750U
#define GX_SPK_LEAD_MAX_MS 2500U
#define GX_SPK_LEAD_STEP_MS 250U
#define GX_FEED_LOG_PACKETS 125U
#define GX_UART_READ_MS 20U
#define GX_INIT_TIMEOUT_MS 3000U
#define GX_CLOSE_TIMEOUT_MS 1000U
#define GX_UART_SEND_TIMEOUT_MS 100U
#define GX_UART_RX_CAPACITY 2048U
#define GX_FLOW_UNUSED 0xFFU
#define GX_WAKE_SOURCE_MODULE 1U

typedef struct {
    qm_audio_stream_format_t input_format;
    qm_audio_stream_format_t output_format;
    qm_mutex_t lock;
    qm_sem_t capture_gate;
    qm_sem_t render_ready;
    qm_sem_t task_exited;
    qm_uart_dev_t uart;
    qm_gpio_dev_t flow_gpio;
    qm_task_t uart_task;
    uint8_t *rx;
    uint32_t capture_sequence;
    uint32_t spk_msg_id;
    uint32_t play_ack_error;   /* 被模块拒绝的 DATA 帧数 */
    uint32_t play_empty_count; /* 模块上报播放缓冲为空的次数 */
    uint32_t feed_start_ms;    /* 本轮送数起点, 节拍与诊断共用的挂钟基准 */
    uint32_t lead_ms;          /* 允许领先真实时间的音频时长, 播空后自增 */
    uint8_t flow_ready_level;
    uint8_t volume;
    uint8_t configs_ready;
    int init_error;
    qm_bool flow_enabled;
    qm_bool opened;
    qm_bool task_stop;
    qm_bool task_created;
    qm_bool capture_started;
    qm_bool capture_abort;
    qm_bool capture_gap;
    qm_bool render_started;
    qm_bool render_abort;
    qm_bool start_sent;
    qm_bool eos_sent;
    qm_bool render_done;
    qm_bool poisoned;
    qm_bool muted;
} gx_ctx_t;

/* UART/protocol are singletons; Engine is the only device owner. */
static gx_ctx_t *s_gx8006;

static qm_bool format_supported(const qm_audio_stream_format_t *format)
{
    return format != NULL && format->codec == QM_AUDIO_CODEC_OPUS &&
           format->pcm.sample_rate_hz == 16000U && format->pcm.channels == 1U &&
           format->pcm.sample == QM_AUDIO_PCM_S16 &&
           format->framing == QM_AUDIO_CODEC_FRAMING_STREAM &&
           format->packet_duration_ms == GX_PACKET_DURATION_MS &&
           format->packet_bytes == GX_PACKET_BYTES &&
           format->max_packet_bytes == GX_PACKET_BYTES;
}

/* The fixed GX8006 profile cannot trust optional caller metadata alone. The
 * Opus TOC gives enough information to reject a packet with another duration
 * or channel count before it reaches the module. Full entropy decoding remains
 * the module's responsibility. */
static qm_bool opus_packet_matches_profile(const qm_audio_data_t *packet)
{
    const uint8_t *data;
    uint32_t frame_units, count, total;

    if (packet == NULL || packet->data == NULL || packet->size != GX_PACKET_BYTES) {
        return QM_FALSE;
    }
    data = (const uint8_t *)packet->data;
    if ((data[0] & 4U) != 0U) return QM_FALSE;
    if ((data[0] & 0x80U) != 0U) {
        frame_units = 1U << ((data[0] >> 3U) & 3U);
    } else if ((data[0] & 0x60U) == 0x60U) {
        frame_units = (data[0] & 0x08U) != 0U ? 8U : 4U;
    } else {
        uint32_t mode = (data[0] >> 3U) & 3U;
        frame_units = mode == 3U ? 24U : (4U << mode);
    }
    count = data[0] & 3U;
    if (count == 0U) count = 1U;
    else if (count < 3U) count = 2U;
    else {
        if (packet->size < 2U) return QM_FALSE;
        count = data[1] & 0x3fU;
    }
    total = frame_units * count;
    return count != 0U && total == GX_PACKET_DURATION_MS * 2U / 5U ?
        QM_TRUE : QM_FALSE;
}

static void poison(gx_ctx_t *ctx)
{
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    ctx->poisoned = QM_TRUE;
    (void)qm_mutex_unlock(&ctx->lock);
    (void)qm_sem_signal(&ctx->render_ready);
}

static int protocol_event(nc_audio_event_type_t type, nc_audio_event_param_t *event)
{
    gx_ctx_t *ctx = s_gx8006;
    qm_bool wake_seen = QM_FALSE;
    if (ctx == NULL) {
        return -QM_EINVAL;
    }
    if (type == NC_AUDIO_EVENT_TYPE_MIC_DATA_GET && event != NULL) {
        const nc_audio_mic_data_t *mic = &event->mic_data;
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        if (!ctx->capture_started || ctx->capture_abort || ctx->poisoned) {
            (void)qm_mutex_unlock(&ctx->lock);
            return QM_EOK;
        }
        /* Accept under the state lock; stop waits for the whole callback.
         * 状态锁内确认接收；stop 等待整个回调，避免聚包尾部跨越新一轮 start。 */
        if (qm_sem_wait(&ctx->capture_gate, QM_NO_WAIT) != QM_EOK) {
            ctx->capture_gap = QM_TRUE;
            (void)qm_mutex_unlock(&ctx->lock);
            return QM_EOK;
        }
        if (mic->status != NC_AUDIO_VAD_DATA || mic->payload == NULL ||
            mic->payload_len == 0U || mic->payload_len % GX_PACKET_BYTES != 0U) {
            ctx->capture_gap = QM_TRUE;
        } else {
            for (uint32_t offset = 0U; offset < mic->payload_len; offset += GX_PACKET_BYTES) {
                if (!ctx->capture_started || ctx->capture_abort || ctx->poisoned) break;
                qm_audio_data_t packet = {
                    .data = mic->payload + offset,
                    .size = GX_PACKET_BYTES,
                };
                if (!opus_packet_matches_profile(&packet)) {
                    ctx->capture_gap = QM_TRUE;
                    continue;
                }
                qm_audio_data_t input = {
                    .data = packet.data, .size = GX_PACKET_BYTES,
                    .duration_ms = GX_PACKET_DURATION_MS,
                    .sequence = ctx->capture_sequence++,
                    .flags = QM_AUDIO_DATA_DURATION_VALID |
                             (ctx->capture_gap ? QM_AUDIO_DATA_DISCONTINUITY : 0U),
                };
                (void)qm_mutex_unlock(&ctx->lock);
                int ret = qm_audio_capture_feed(&input, QM_EOK);
                (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
                ctx->capture_gap = ret != QM_EOK;
            }
        }
        (void)qm_mutex_unlock(&ctx->lock);
        (void)qm_sem_signal(&ctx->capture_gate);
        return QM_EOK;
    }
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    if (type == NC_AUDIO_EVENT_TYPE_GET_MCU_VER && event != NULL) {
        if (ctx->opened) {
            /* A module reboot invalidates packet identity and active playback. */
            ctx->poisoned = QM_TRUE;
        }
        ctx->flow_enabled = event->mcu_ver.status != GX_FLOW_UNUSED;
        ctx->flow_ready_level = event->mcu_ver.level ? 1U : 0U;
    } else if ((type == NC_AUDIO_EVENT_TYPE_MIC_CFG ||
                type == NC_AUDIO_EVENT_TYPE_SPK_CFG) && event != NULL) {
        if (event->mic_cfg_result.code != 0U) {
            ctx->init_error = -QM_EIO;
            if (ctx->opened) {
                ctx->poisoned = QM_TRUE;
            }
        } else {
            ctx->configs_ready |= type == NC_AUDIO_EVENT_TYPE_MIC_CFG ? 1U : 2U;
        }
    } else if (type == NC_AUDIO_EVENT_TYPE_SPK_REPLY && event != NULL) {
        QM_LOGD(LOG_TAG, "skp reply status=%u", (unsigned int)event->spk_state.status);
        if (event->spk_state.status == NC_AUDIO_SPK_REPLY_TYPE_EMPTY) {
            /* 模块缓冲播空: 供数节拍不足的直接证据, 加大领先量到不再播空为止。 */
            ctx->play_empty_count++;
            if (ctx->lead_ms < GX_SPK_LEAD_MAX_MS) {
                ctx->lead_ms += GX_SPK_LEAD_STEP_MS;
                if (ctx->lead_ms > GX_SPK_LEAD_MAX_MS) {
                    ctx->lead_ms = GX_SPK_LEAD_MAX_MS;
                }
                QM_LOGW(LOG_TAG, "skp empty, raise lead to %u ms", (unsigned int)ctx->lead_ms);
            }
        } else if (event->spk_state.status == NC_AUDIO_SPK_REPLY_TYPE_STOP &&
                   ctx->render_started && ctx->eos_sent) {
            /* EMPTY is explicitly not completion.  No new START before this barrier. */
            ctx->render_done = QM_TRUE;
        }
    } else if (type == NC_AUDIO_EVENT_TYPE_SPK_PLAY_ACK && event != NULL) {
        if (event->spk_play_ack.code != 0U) {
            /* 模块拒绝了该帧, 该帧音频已丢失。 */
            ctx->play_ack_error++;
            QM_LOGW(LOG_TAG, "spk frame rejected msg_id=%u",
                    (unsigned int)event->spk_play_ack.msg_id);
        }
    } else if (type == NC_AUDIO_EVENT_TYPE_WAKEUP) {
        wake_seen = QM_TRUE;
    }
    (void)qm_mutex_unlock(&ctx->lock);
    if (wake_seen) {
        /* 模块级唤醒与播放通道正交，交给引擎级接口分发，避免占用 per-Control 事件通道。 */
        qm_audio_wake_info_t wake = {0};
        wake.source = GX_WAKE_SOURCE_MODULE;
        (void)qm_audio_report_wake(&wake);
    }
    (void)qm_sem_signal(&ctx->render_ready);
    return QM_EOK;
}

static int serial_send(uint8_t *data, int len, uint32_t timeout_ms)
{
    gx_ctx_t *ctx = s_gx8006;
    qm_utils_time_t deadline;
    int offset = 0;

    if (ctx == NULL || data == NULL || len <= 0) {
        return -QM_EINVAL;
    }
    /* A finite transport bound also applies to a caller's infinite wait. */
    if (timeout_ms == QM_WAIT_FOREVER || timeout_ms > GX_UART_SEND_TIMEOUT_MS) {
        timeout_ms = GX_UART_SEND_TIMEOUT_MS;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    while (offset < len) {
        /* TX FIFO writes are nonblocking. The generic UART write ignores its
         * timeout on ESP-IDF, so this adapter deliberately uses the SDK here. */
        int sent = uart_tx_chars(ctx->uart.port, (const char *)data + offset,
                                 (uint32_t)(len - offset));
        if (sent < 0 || sent > len - offset) {
            poison(ctx);
            return -QM_EIO;
        }
        offset += sent;
        if (offset == len) {
            return QM_EOK;
        }
        if (qm_utils_time_is_expired(&deadline)) {
            /* A partial UART frame cannot be retried as an atomic Opus packet. */
            if (offset != 0) poison(ctx);
            return offset != 0 ? -QM_EIO : -QM_ETIMEOUT;
        }
        qm_msleep(GX_FLOW_WAIT_MS);
    }
    return QM_EOK;
}

static void uart_task(void *arg)
{
    gx_ctx_t *ctx = (gx_ctx_t *)arg;
    for (;;) {
        qm_bool stop;
        int len;
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        stop = ctx->task_stop;
        (void)qm_mutex_unlock(&ctx->lock);
        if (stop) {
            break;
        }
        len = qm_uart_read(&ctx->uart, ctx->rx, CONFIG_QM_GX8006_UART_RECV_SIZE,
                           GX_UART_READ_MS);
        if (len > 0) {
            (void)nc_audio_recv_push(ctx->rx, len);
        } else if (len < 0) {
            /* ESP32 UART returns zero on a normal timeout. / ESP32 UART 正常超时返回零。 */
            poison(ctx);
        }
        (void)qm_sem_wait(&ctx->capture_gate, QM_WAIT_FOREVER);
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        qm_bool failed = ctx->capture_started && !ctx->capture_abort && ctx->poisoned;
        if (failed) ctx->capture_abort = QM_TRUE;
        (void)qm_mutex_unlock(&ctx->lock);
        if (failed) (void)qm_audio_capture_feed(NULL, -QM_EIO);
        (void)qm_sem_signal(&ctx->capture_gate);
        if (len < 0) break;
    }
    (void)qm_sem_signal(&ctx->task_exited); /* Last context access. */
    qm_task_exit(NULL);
}

static int open_uart(gx_ctx_t *ctx)
{
    uart_config_t config = {0};
    if (uart_is_driver_installed(ctx->uart.port)) return -QM_EBUSY;
    config.baud_rate = CONFIG_QM_GX8006_UART_BAUDRATE;
    config.data_bits = UART_DATA_8_BITS;
    config.parity = UART_PARITY_DISABLE;
    config.stop_bits = UART_STOP_BITS_1;
    config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
    if (uart_param_config(ctx->uart.port, &config) != ESP_OK ||
        uart_set_pin(ctx->uart.port, CONFIG_QM_GX8006_UART_TX_PIN,
                     CONFIG_QM_GX8006_UART_RX_PIN, UART_PIN_NO_CHANGE,
                     UART_PIN_NO_CHANGE) != ESP_OK) {
        return -QM_EIO;
    }
    /* No SDK TX queue: each protocol frame uses bounded FIFO writes. */
    return uart_driver_install(ctx->uart.port, GX_UART_RX_CAPACITY, 0,
                                0, NULL, 0) == ESP_OK ? QM_EOK : -QM_EIO;
}

static void free_context(gx_ctx_t *ctx)
{
    (void)qm_sem_free(&ctx->task_exited);
    (void)qm_sem_free(&ctx->render_ready);
    (void)qm_sem_free(&ctx->capture_gate);
    (void)qm_mutex_free(&ctx->lock);
    qm_free(ctx->rx);
    qm_free(ctx);
}

int qm_audio_device_open(void **backend_ctx, const qm_audio_device_config_t *config)
{
    gx_ctx_t *ctx;
    nc_audio_param_t params = {0};
    qm_utils_time_t deadline;
    qm_bool uart_opened = QM_FALSE;
    qm_bool gpio_opened = QM_FALSE;
    qm_bool protocol_opened = QM_FALSE;
    uint32_t rx_total = 0U;
    int ret;

    if (backend_ctx == NULL) {
        return -QM_EINVAL;
    }
    *backend_ctx = NULL;
    if (config == NULL || config->flags != 0U) {
        QM_LOGE(LOG_TAG, "open invalid config flags=%u",
                config == NULL ? 0U : config->flags);
        return -QM_EINVAL;
    }
    if (s_gx8006 != NULL) {
        QM_LOGE(LOG_TAG, "open while already opened");
        return -QM_EBUSY;
    }
    if ((config->input_format.pcm.sample_rate_hz != 0U &&
         !format_supported(&config->input_format)) ||
        (config->output_format.pcm.sample_rate_hz != 0U &&
         !format_supported(&config->output_format)) ||
        (config->input_format.pcm.sample_rate_hz == 0U &&
         config->output_format.pcm.sample_rate_hz == 0U)) {
        QM_LOGE(LOG_TAG, "open unsupported format codec=%u rate=%u ch=%u framing=%u",
                (unsigned)config->input_format.codec,
                config->input_format.pcm.sample_rate_hz,
                config->input_format.pcm.channels,
                (unsigned)config->input_format.framing);
        return -QM_ENOSYS;
    }
    ctx = (gx_ctx_t *)qm_calloc(1U, sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    ctx->rx = (uint8_t *)qm_malloc(CONFIG_QM_GX8006_UART_RECV_SIZE);
    if (ctx->rx == NULL) {
        free_context(ctx);
        return -QM_ENOMEM;
    }
    ctx->input_format = config->input_format;
    ctx->output_format = config->output_format;
    ctx->volume = CONFIG_QM_GX8006_SPK_GAIN;
    ctx->uart.port = CONFIG_QM_GX8006_UART_PORT;
    ctx->flow_gpio.port = CONFIG_QM_GX8006_FLOW_GPIO;
    ctx->flow_gpio.config.mode = QM_GPIO_MODE_INPUT;
    ctx->flow_gpio.config.pull_en = QM_GPIO_PULLUP_ONLY;
    ret = qm_mutex_new(&ctx->lock);
    if (ret == QM_EOK) ret = qm_sem_new(&ctx->capture_gate, 1);
    if (ret == QM_EOK) ret = qm_sem_new(&ctx->render_ready, 0);
    if (ret == QM_EOK) ret = qm_sem_new(&ctx->task_exited, 0);
    if (ret == QM_EOK) {
        ret = open_uart(ctx);
        uart_opened = ret == QM_EOK;
    }
    if (ret == QM_EOK) {
        ret = qm_gpio_init(&ctx->flow_gpio);
        gpio_opened = ret == QM_EOK;
    }
    if (ret != QM_EOK) goto fail;
    s_gx8006 = ctx;
    params.send = serial_send;
    params.notify = protocol_event;
    params.mic.codec = params.spk.codec = NC_AUDIO_FMT_TYPE_OPUS;
    params.mic.sample_rate_hz = params.spk.sample_rate_hz = 16000U;
    params.mic.sample_bits = params.spk.sample_bits = 16U;
    params.mic.channels = params.spk.channels = 1U;
    params.mic.max_payload_bytes = CONFIG_NC_AUDIO_RECV_MAX_SIZE;
    params.spk.max_payload_bytes = GX_PACKET_BYTES;
    params.mic_gain = CONFIG_QM_GX8006_MIC_GAIN;
    params.spk_gain = ctx->volume;
    params.vad_timeout = CONFIG_QM_GX8006_VAD_TIMEOUT;
    ret = nc_audio_init(&params);
    if (ret != QM_EOK) goto fail;
    protocol_opened = QM_TRUE;
    /* Handshake is synchronous before any task can retain this context. */
    qm_utils_time_countdown_ms(&deadline, GX_INIT_TIMEOUT_MS);
    while (ctx->configs_ready != 3U && ctx->init_error == QM_EOK) {
        int len = qm_uart_read(&ctx->uart, ctx->rx, CONFIG_QM_GX8006_UART_RECV_SIZE,
                               GX_UART_READ_MS);
        if (len > 0) {
            rx_total += (uint32_t)len;
            (void)nc_audio_recv_push(ctx->rx, len);
        }
        if (qm_utils_time_is_expired(&deadline)) {
            ret = -QM_ETIMEOUT;
            goto fail;
        }
    }
    ret = ctx->init_error;
    if (ret == QM_EOK && ctx->poisoned) ret = -QM_EIO;
    if (ret == QM_EOK) ret = nc_audio_cfg(NC_AUDIO_CFG_TYPE_VAD_ONOFF, 0);
    if (ret == QM_EOK) ret = nc_audio_cfg(NC_AUDIO_CFG_TYPE_MIC_ONOFF, 0);
    /* 打开离线唤醒检测与其上报，模块才会经 UART 0x08 通知唤醒。 */
    if (ret == QM_EOK) ret = nc_audio_cfg(NC_AUDIO_CFG_TYPE_WAKEUP_ONOFF, 1);
    if (ret == QM_EOK) ret = nc_audio_cfg(NC_AUDIO_CFG_TYPE_WAKEUP_ONTIFY_ONOFF, 1);
    if (ret != QM_EOK) goto fail;
    ctx->opened = QM_TRUE;
    ret = qm_task_new(&ctx->uart_task, "gx8006_uart", uart_task, ctx,
                      CONFIG_QM_GX8006_UART_TASK_STACK_SIZE,
                      CONFIG_QM_GX8006_UART_TASK_PRIORITY);
    if (ret != QM_EOK) goto fail;
    ctx->task_created = QM_TRUE;
    *backend_ctx = ctx;
    return QM_EOK;

fail:
    /* rx_bytes=0 表示模块一个字节都没回（接线/供电/波特率）；
     * rx_bytes>0 但 configs_ready 不足则是模块已应答但未完成配置握手。 */
    QM_LOGE(LOG_TAG, "open failed ret=%d configs_ready=%u rx_bytes=%lu flow_enabled=%u",
            ret, ctx->configs_ready, (unsigned long)rx_total, ctx->flow_enabled);
    if (s_gx8006 == ctx) {
        if (protocol_opened) (void)nc_audio_deinit();
        s_gx8006 = NULL;
    }
    if (gpio_opened) (void)qm_gpio_deinit(&ctx->flow_gpio);
    if (uart_opened) (void)qm_uart_deinit(&ctx->uart);
    free_context(ctx);
    return ret;
}

int qm_audio_device_close(void *backend_ctx)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    int ret;
    if (ctx == NULL || ctx != s_gx8006) return -QM_EINVAL;
    ret = qm_audio_device_capture_stop(ctx);
    if (ret != QM_EOK) return ret;
    ret = qm_audio_device_playback_stop(ctx);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    ctx->task_stop = QM_TRUE;
    (void)qm_mutex_unlock(&ctx->lock);
    if (ctx->task_created) {
        ret = qm_sem_wait(&ctx->task_exited, GX_CLOSE_TIMEOUT_MS);
        if (ret != QM_EOK) return ret;
        ctx->task_created = QM_FALSE;
    }
    (void)nc_audio_deinit();
    (void)qm_gpio_deinit(&ctx->flow_gpio);
    (void)qm_uart_deinit(&ctx->uart);
    s_gx8006 = NULL;
    free_context(ctx);
    return QM_EOK;
}

int qm_audio_device_get_caps(void *backend_ctx, qm_audio_device_caps_t *caps)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    if (ctx == NULL || caps == NULL || !ctx->opened) return -QM_EINVAL;
    qm_memset(caps, 0, sizeof(*caps));
    caps->input_format = ctx->input_format;
    caps->output_format = ctx->output_format;
    if (ctx->input_format.pcm.sample_rate_hz != 0U) {
        caps->flags |= QM_AUDIO_DEVICE_CAP_CAPTURE | QM_AUDIO_DEVICE_CAP_ABORT_READ;
    }
    if (ctx->output_format.pcm.sample_rate_hz != 0U) {
        caps->flags |= QM_AUDIO_DEVICE_CAP_PLAYBACK | QM_AUDIO_DEVICE_CAP_DRAIN |
                       QM_AUDIO_DEVICE_CAP_ABORT_WRITE | QM_AUDIO_DEVICE_CAP_STOP_DRAINS |
                       QM_AUDIO_DEVICE_CAP_VOLUME | QM_AUDIO_DEVICE_CAP_MUTE;
    }
    return QM_EOK;
}

int qm_audio_device_playback_start(void *backend_ctx, qm_audio_delivery_t delivery)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    int ret = QM_EOK;
    if (ctx == NULL || !ctx->opened || !format_supported(&ctx->output_format) ||
        ctx->output_format.pcm.sample_rate_hz == 0U ||
        (delivery != QM_AUDIO_DELIVERY_RELIABLE && delivery != QM_AUDIO_DELIVERY_REALTIME)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    if (ctx->poisoned) ret = -QM_EIO;
    else if (ctx->render_started) ret = -QM_EBUSY;
    else {
        ctx->render_started = QM_TRUE;
        ctx->render_abort = QM_FALSE;
        ctx->start_sent = QM_FALSE;
        ctx->eos_sent = QM_FALSE;
        ctx->render_done = QM_FALSE;
        ctx->spk_msg_id = 0U;
        ctx->play_ack_error = 0U;
        ctx->play_empty_count = 0U;
        ctx->feed_start_ms = qm_now_ms();
        ctx->lead_ms = GX_SPK_LEAD_MS;
    }
    (void)qm_mutex_unlock(&ctx->lock);
    return ret;
}

/* 模块既不上报逐帧确认也不声明流控 GPIO, 唯一可靠的节拍是挂钟: 每帧承载固定
 * 40 ms 音频, 允许领先 GX_SPK_LEAD_MS 以吸收上游抖动, 之后与真实时间对齐。
 * 不节拍时送数速率只受 UART 限制(约 5 倍实时), 会灌爆模块播放缓冲导致丢帧。 */
static void pace_render(gx_ctx_t *ctx)
{
    for (;;) {
        uint32_t sent_ms;
        uint32_t elapsed_ms;
        uint32_t lead_ms;
        qm_bool halt;
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        halt = ctx->poisoned || ctx->render_abort || !ctx->render_started || ctx->eos_sent;
        sent_ms = (ctx->spk_msg_id + 1U) * GX_PACKET_DURATION_MS;
        elapsed_ms = qm_now_ms() - ctx->feed_start_ms;
        lead_ms = ctx->lead_ms;
        (void)qm_mutex_unlock(&ctx->lock);
        /* 停止/中断不在这里定性, 交给随后的发送路径给出确切错误。 */
        if (halt || sent_ms <= elapsed_ms + lead_ms) {
            return;
        }
        qm_msleep(GX_FLOW_WAIT_MS);
    }
}

static int wait_flow(gx_ctx_t *ctx, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;
    if (timeout_ms != QM_WAIT_FOREVER) qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        int ret = QM_EOK;
        uint8_t level;
        qm_bool flow_enabled;
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        if (ctx->poisoned) ret = -QM_EIO;
        else if (ctx->render_abort) ret = -QM_EINTR;
        else if (!ctx->render_started || ctx->eos_sent) ret = -QM_EBUSY;
        level = ctx->flow_ready_level;
        flow_enabled = ctx->flow_enabled;
        (void)qm_mutex_unlock(&ctx->lock);
        if (ret != QM_EOK) return ret;
        /* 模块声明不使用流控 GPIO（GET_MCU_VER status=0xFF）时不等待。 */
        if (!flow_enabled) return QM_EOK;
        if (qm_gpio_get_level(&ctx->flow_gpio) == level) return QM_EOK;
        if (timeout_ms != QM_WAIT_FOREVER && qm_utils_time_is_expired(&deadline)) {
            return -QM_ETIMEOUT;
        }
        (void)qm_sem_wait(&ctx->render_ready, GX_FLOW_WAIT_MS);
    }
}

/* A write result never carries a partial-progress count. Once the module has
 * observed a write error, seal this run so the caller cannot ambiguously retry
 * the same encoded access unit. playback_stop() remains responsible for the
 * final protocol stop barrier. */
static int fail_render(gx_ctx_t *ctx, int ret)
{
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    if (ctx->render_started && !ctx->render_done) ctx->render_abort = QM_TRUE;
    (void)qm_mutex_unlock(&ctx->lock);
    (void)qm_sem_signal(&ctx->render_ready);
    return ret;
}

int qm_audio_device_playback_write(void *backend_ctx,
                                   const qm_audio_data_t *input,
                                   uint32_t timeout_ms)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    nc_audio_spk_play_header_t play = {0};
    qm_utils_time_t deadline;
    uint32_t left;
    int ret;
    if (ctx == NULL || ctx != s_gx8006 || !ctx->opened) return -QM_EINVAL;
    if (input == NULL ||
        (input->flags & ~(QM_AUDIO_DATA_TIMESTAMP_VALID |
                          QM_AUDIO_DATA_DURATION_VALID |
                          QM_AUDIO_DATA_DISCONTINUITY)) != 0U ||
        !opus_packet_matches_profile(input) ||
        ((input->flags & QM_AUDIO_DATA_DURATION_VALID) != 0U &&
         input->duration_ms != GX_PACKET_DURATION_MS)) {
        return fail_render(ctx, -QM_EINVAL);
    }
    /* 节拍等待不计入本次发送超时预算。 */
    pace_render(ctx);
    if (timeout_ms != QM_WAIT_FOREVER) qm_utils_time_countdown_ms(&deadline, timeout_ms);
    ret = wait_flow(ctx, timeout_ms);
    if (ret != QM_EOK) return fail_render(ctx, ret);
    left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline);
    if (!ctx->start_sent) {
        /* 首帧才起算节拍基准: 起播瞬间就允许灌满 lead 缓冲, 不再从零开始边灌边播。
         * START 命令与首帧同时发出, 因此起播时刻不变, 只是缓冲立刻建立。 */
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        ctx->feed_start_ms = qm_now_ms();
        (void)qm_mutex_unlock(&ctx->lock);
        play.type = NC_AUDIO_SPK_PLAY_TYPE_START;
        ret = nc_audio_spk_play(&play, NULL, 0, left);
        if (ret != QM_EOK) return fail_render(ctx, ret);
        ctx->start_sent = QM_TRUE;
    }
    left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline);
    play.type = NC_AUDIO_SPK_PLAY_TYPE_DATA;
    play.msg_id = ctx->spk_msg_id + 1U;
    ret = nc_audio_spk_play(&play, (uint8_t *)input->data, (int)input->size, left);
    if (ret != QM_EOK) return fail_render(ctx, ret);
    ctx->spk_msg_id = play.msg_id;
    if ((ctx->spk_msg_id % GX_FEED_LOG_PACKETS) == 0U) {
        uint32_t empty;
        uint32_t ack_error;
        uint32_t lead_ms;
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        empty = ctx->play_empty_count;
        ack_error = ctx->play_ack_error;
        lead_ms = ctx->lead_ms;
        (void)qm_mutex_unlock(&ctx->lock);
        /* audio 应随 wall 同增长: 明显超前即说明节拍失效, empty 非零即供数不足。 */
        QM_LOGI(LOG_TAG, "spk feed sent=%u audio=%u ms wall=%u ms lead=%u ms empty=%u ack_err=%u",
                (unsigned int)ctx->spk_msg_id,
                (unsigned int)(ctx->spk_msg_id * GX_PACKET_DURATION_MS),
                (unsigned int)(qm_now_ms() - ctx->feed_start_ms),
                (unsigned int)lead_ms, (unsigned int)empty, (unsigned int)ack_error);
    }
    return QM_EOK;
}

static int finish_render(gx_ctx_t *ctx, uint32_t timeout_ms, qm_bool interruptible)
{
    nc_audio_spk_play_header_t stop = {0};
    qm_utils_time_t deadline;
    qm_bool send_eos = QM_FALSE;
    int ret;
    if (timeout_ms != QM_WAIT_FOREVER) qm_utils_time_countdown_ms(&deadline, timeout_ms);
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    if (ctx->poisoned) {
        (void)qm_mutex_unlock(&ctx->lock);
        return -QM_EIO;
    }
    if (!ctx->render_started || !ctx->start_sent || ctx->render_done) {
        (void)qm_mutex_unlock(&ctx->lock);
        return QM_EOK;
    }
    if (!ctx->eos_sent) {
        ctx->eos_sent = QM_TRUE; /* Set before send: reply may arrive immediately. */
        send_eos = QM_TRUE;
    }
    (void)qm_mutex_unlock(&ctx->lock);
    if (send_eos) {
        stop.type = NC_AUDIO_SPK_PLAY_TYPE_STOP;
        stop.msg_id = ctx->spk_msg_id;
        uint32_t left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                        qm_utils_time_left(&deadline);
        ret = nc_audio_spk_play(&stop, NULL, 0, left);
        if (ret != QM_EOK) {
            (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
            if (!ctx->poisoned) ctx->eos_sent = QM_FALSE;
            (void)qm_mutex_unlock(&ctx->lock);
            return ret;
        }
    }
    for (;;) {
        uint32_t left = QM_WAIT_FOREVER;
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        if (ctx->poisoned) ret = -QM_EIO;
        else if (ctx->render_done) ret = QM_EOK;
        else if (interruptible && ctx->render_abort) ret = -QM_EINTR;
        else ret = -QM_EBUSY;
        (void)qm_mutex_unlock(&ctx->lock);
        if (ret != -QM_EBUSY) return ret;
        if (timeout_ms != QM_WAIT_FOREVER) {
            left = qm_utils_time_left(&deadline);
            if (left == 0U) {
                /* Keep this run sealed; a later drain may continue waiting.
                 * Only a failed stop barrier poisons route reuse. */
                return -QM_ETIMEOUT;
            }
        }
        (void)qm_sem_wait(&ctx->render_ready, left);
    }
}

int qm_audio_device_playback_drain(void *backend_ctx, uint32_t timeout_ms)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    if (ctx == NULL || !ctx->opened) return -QM_EINVAL;
    return finish_render(ctx, timeout_ms, QM_TRUE);
}

int qm_audio_device_playback_abort(void *backend_ctx)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    if (ctx == NULL || !ctx->opened) return -QM_EINVAL;
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    ctx->render_abort = QM_TRUE;
    (void)qm_mutex_unlock(&ctx->lock);
    (void)qm_sem_signal(&ctx->render_ready);
    return QM_EOK;
}

int qm_audio_device_playback_stop(void *backend_ctx)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    int ret;
    if (ctx == NULL || !ctx->opened) return -QM_EINVAL;
    (void)qm_audio_device_playback_abort(ctx);
    /* Protocol STOP is EOS, not an immediate discard. Preserve MIC resources. */
    ret = finish_render(ctx, CONFIG_QM_GX8006_STOP_TIMEOUT_MS, QM_FALSE);
    if (ret == -QM_ETIMEOUT) poison(ctx);
    if (ret == QM_EOK) {
        uint32_t sent;
        uint32_t ack_error;
        uint32_t empty;
        uint32_t wall_ms;
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        ctx->render_started = QM_FALSE;
        sent = ctx->spk_msg_id;
        ack_error = ctx->play_ack_error;
        empty = ctx->play_empty_count;
        wall_ms = qm_now_ms() - ctx->feed_start_ms;
        (void)qm_mutex_unlock(&ctx->lock);
        QM_LOGI(LOG_TAG, "spk run sent=%u audio=%u ms wall=%u ms err=%u empty=%u",
                (unsigned int)sent, (unsigned int)(sent * GX_PACKET_DURATION_MS),
                (unsigned int)wall_ms, (unsigned int)ack_error, (unsigned int)empty);
    }
    return ret;
}

int qm_audio_device_capture_start(void *backend_ctx)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    int ret = QM_EOK;

    if (ctx == NULL || ctx != s_gx8006 || !ctx->opened ||
        !format_supported(&ctx->input_format) ||
        ctx->input_format.pcm.sample_rate_hz == 0U) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    if (ctx->poisoned) {
        ret = -QM_EIO;
    } else if (ctx->capture_started) {
        ret = -QM_EBUSY;
    } else {
        ctx->capture_abort = QM_FALSE;
        ctx->capture_gap = QM_TRUE;
        ctx->capture_sequence = 0U;
        ctx->capture_started = QM_TRUE;
    }
    (void)qm_mutex_unlock(&ctx->lock);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = nc_audio_cfg(NC_AUDIO_CFG_TYPE_MIC_ONOFF, 1);
    if (ret != QM_EOK) {
        (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
        /* MIC_ON may have partially reached the device. Block delivery, but
         * retain the run until stop sends MIC_OFF successfully. /
         * MIC_ON 可能已部分到达设备；关闭交付，保留运行状态直到 MIC_OFF 成功。 */
        ctx->capture_abort = QM_TRUE;
        (void)qm_mutex_unlock(&ctx->lock);
    }
    return ret;
}

int qm_audio_device_capture_abort(void *backend_ctx)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;

    if (ctx == NULL || ctx != s_gx8006 || !ctx->opened) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    ctx->capture_abort = QM_TRUE;
    (void)qm_mutex_unlock(&ctx->lock);
    return QM_EOK;
}

int qm_audio_device_capture_stop(void *backend_ctx)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    qm_bool started = QM_FALSE;
    int ret = QM_EOK;

    if (ctx == NULL || ctx != s_gx8006 || !ctx->opened) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    started = ctx->capture_started;
    /* Stop delivering immediately, but retain capture_started until MIC_OFF
     * reaches the module. A failed command must remain retryable. */
    ctx->capture_abort = QM_TRUE;
    (void)qm_mutex_unlock(&ctx->lock);
    ret = qm_sem_wait(&ctx->capture_gate, CONFIG_QM_GX8006_STOP_TIMEOUT_MS);
    if (ret != QM_EOK) {
        return -QM_ETIMEOUT;
    }
    (void)qm_sem_signal(&ctx->capture_gate);
    if (!started) {
        return QM_EOK;
    }

    ret = nc_audio_cfg(NC_AUDIO_CFG_TYPE_MIC_ONOFF, 0);
    if (ret != QM_EOK) {
        return ret;
    }

    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    ctx->capture_started = QM_FALSE;
    (void)qm_mutex_unlock(&ctx->lock);
    return QM_EOK;
}

int qm_audio_device_set_volume(void *backend_ctx, uint8_t volume)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    int ret;
    if (ctx == NULL || ctx != s_gx8006 || !ctx->opened || volume > 100U) {
        return -QM_EINVAL;
    }
    ret = nc_audio_set_spk_volume(ctx->muted ? 0U : volume);
    if (ret == QM_EOK) ctx->volume = volume;
    return ret;
}

int qm_audio_device_set_mute(void *backend_ctx, qm_bool muted)
{
    gx_ctx_t *ctx = (gx_ctx_t *)backend_ctx;
    uint8_t volume;
    int ret;
    if (ctx == NULL || ctx != s_gx8006 || !ctx->opened) {
        return -QM_EINVAL;
    }
    if (muted != QM_TRUE && muted != QM_FALSE) return -QM_EINVAL;
    volume = ctx->volume;
    ret = nc_audio_set_spk_volume(muted ? 0U : volume);
    if (ret == QM_EOK) ctx->muted = muted;
    return ret;
}

int qm_audio_device_playback_pause(void *ctx) { QM_UNUSED_ARG(ctx); return -QM_ENOSYS; }
int qm_audio_device_playback_resume(void *ctx) { QM_UNUSED_ARG(ctx); return -QM_ENOSYS; }
/* 模块固件不上报逐帧确认(SKP_PLAY_ACK), 协议里也没有播放偏移字段, 已消费进度
 * 无法观测, 因此由该适配层声明不支持并返回 -QM_ENOSYS, 不伪造进度。 */
int qm_audio_device_playback_get_position(void *backend_ctx, uint32_t *position_ms)
{
    QM_UNUSED_ARG(backend_ctx);
    QM_UNUSED_ARG(position_ms);
    return -QM_ENOSYS;
}
