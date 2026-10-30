#include "qm.h"
#include "adapter/qm_audio_device.h"
#include "qm_audio.h"
#include "qm_gpio.h"
#include "qm_log.h"
#include "qm_uart.h"
#include "qm_utils_timer.h"
#include "qm_work.h"
#include "vb701n.h"
#include "opus_pkt.h"
#include "driver/gpio.h"

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <string.h>

#if defined(CONFIG_QM_AUDIO_DEVICE_VB701N) && CONFIG_QM_AUDIO_DEVICE_VB701N

#ifndef CONFIG_QM_VB701N_UART_PORT
#define CONFIG_QM_VB701N_UART_PORT 1
#endif
#ifndef CONFIG_QM_VB701N_UART_BAUDRATE
#define CONFIG_QM_VB701N_UART_BAUDRATE 2000000
#endif
#ifndef CONFIG_QM_VB701N_UART_TX_PIN
#define CONFIG_QM_VB701N_UART_TX_PIN 40
#endif
#ifndef CONFIG_QM_VB701N_UART_RX_PIN
#define CONFIG_QM_VB701N_UART_RX_PIN 39
#endif
#ifndef CONFIG_QM_VB701N_FLOW_GPIO
#define CONFIG_QM_VB701N_FLOW_GPIO 38
#endif
#ifndef CONFIG_QM_VB701N_TX_INTERVAL_MS
#define CONFIG_QM_VB701N_TX_INTERVAL_MS 5
#endif

#define LOG_TAG "vb701n"

/* 16 kHz / 单声道 / S16。PCM 一帧 320 字节 / 10 ms。麦包最大 512。 */
#define VB701N_PCM_FRAME_LEN   320
#define VB701N_PCM_FRAME_MS    10
#define VB701N_TX_BURST_FRAMES 2
#define VB701N_PCM_LEAD_MS     200
#define VB701N_AUDIO_SLOT_MAX  512
#define VB701N_AUDIO_Q_LEN     8
/* 对外是 20 ms 变长 Opus，单包不超过 255。芯片仍是 PCM。 */
#define VB701N_OPUS_FRAME_MS   OPUS_PKT_FRAME_MS
#define VB701N_OPUS_PKT_MAX    OPUS_PKT_MAX
#define VB701N_PCM_DEC_MAX     3840
#define VB701N_ENC_STACK_BYTES (30 * 1024)
#define VB701N_VOLUME_HW_MAX   31
#define VB701N_WAKE_SOURCE     1U
#define VB701N_RX_READ_BYTES   1024
#define VB701N_RX_WAIT_MS      20
#define VB701N_RX_STACK_BYTES  4096
#define VB701N_RX_PRIORITY     12
#define VB701N_RX_STOP_MS      1000
#define VB701N_VER_FIRST_MS    1500
#define VB701N_VER_RETRY_MS    3000
#define VB701N_VER_RETRY_MAX   5
#define VB701N_READY_DELAY_MS  500

typedef struct {
    uint16_t len;
    uint8_t data[VB701N_AUDIO_SLOT_MAX];
} vb701n_audio_slot_t;

static vb701n_audio_slot_t g_q[VB701N_AUDIO_Q_LEN];
static uint8_t g_head;
static uint8_t g_tail;
static uint8_t g_count;
static vb701n_audio_slot_t g_opus_q[VB701N_AUDIO_Q_LEN];
static uint8_t g_opus_head;
static uint8_t g_opus_tail;
static uint8_t g_opus_count;
static qm_mutex_t g_lock;
static qm_sem_t g_sem;
static qm_sem_t g_opus_sem;
static StaticTask_t g_enc_tcb;
static StackType_t *g_enc_stack;
static uint8_t g_enc_ready;
static uint8_t g_enc_drop_logged;
static uint8_t g_ready;
static uint8_t g_open;
static uint8_t g_ctx;
static volatile uint8_t g_abort_read;
static volatile uint8_t g_abort_write;
static uint8_t g_play;
static uint8_t g_cap;
static uint8_t g_mute;
static uint8_t g_mic_logged;
static uint32_t g_last_send_ms;
static uint16_t g_play_frame_len = VB701N_PCM_FRAME_LEN;
static uint16_t g_play_frame_ms = VB701N_PCM_FRAME_MS;
static uint8_t g_hold[VB701N_AUDIO_SLOT_MAX];
static qm_audio_stream_format_t g_in_fmt;
static qm_audio_stream_format_t g_out_fmt;
static qm_uart_dev_t g_uart;
static qm_task_t g_rx_task;
static qm_sem_t g_rx_exited;
static uint8_t g_rx_task_up;
static volatile uint8_t g_rx_stop;
static qm_gpio_dev_t g_flow_gpio;
static uint8_t g_uart_up;
static uint8_t g_flow_up;
static uint8_t g_volume;
static uint8_t g_volume_valid;
static uint8_t g_chip_ready;
static uint8_t g_got_version;
static uint8_t g_ver_retry;
static uint8_t g_mic_on;
static qm_work_t g_ver_work;
static qm_work_t g_ready_work;

static int profile_ok(const qm_audio_stream_format_t *fmt)
{
    if (fmt == NULL) {
        return 0;
    }
    if (fmt->pcm.sample_rate_hz == 0 && fmt->pcm.channels == 0) {
        return 1;
    }
    return fmt->codec == QM_AUDIO_CODEC_OPUS &&
           fmt->pcm.sample_rate_hz == 16000 &&
           fmt->pcm.channels == 1 &&
           fmt->pcm.sample == QM_AUDIO_PCM_S16 &&
           fmt->framing == QM_AUDIO_CODEC_FRAMING_STREAM &&
           fmt->packet_duration_ms == VB701N_OPUS_FRAME_MS &&
           fmt->packet_bytes == 0 &&
           fmt->max_packet_bytes == VB701N_OPUS_PKT_MAX;
}

static int flow_is_busy(void)
{
    int level = qm_gpio_get_level(&g_flow_gpio);
    static uint32_t last_log;

    if (!g_flow_up) {
        return 0;
    }
    if (level != 0) {
        uint32_t now = qm_now_ms();
        if ((now - last_log) > 1000) {
            last_log = now;
            QM_LOGD(LOG_TAG, "tx gate busy, flow pin %u level %d",
                    (unsigned)CONFIG_QM_VB701N_FLOW_GPIO, level);
        }
    }
    return level != 0;
}

static void flush_volume(void)
{
    uint8_t sent;
    uint8_t hw;

    if (!g_open || !g_chip_ready || !g_volume_valid) {
        return;
    }
    do {
        sent = g_volume;
        hw = (uint8_t)(((uint32_t)sent * VB701N_VOLUME_HW_MAX) / 100u);
        QM_LOGD(LOG_TAG, "volume %u hw=%u", (unsigned)sent, (unsigned)hw);
        (void)vb701n_proto_set_volume(hw);
    } while (sent != g_volume);
}

static void on_chip_ready(void *arg)
{
    (void)arg;
    if (!g_open || g_chip_ready) {
        return;
    }
    g_chip_ready = 1;
    flush_volume();
    if (!g_mic_on) {
        g_mic_on = 1;
        (void)vb701n_proto_start_record();
        QM_LOGD(LOG_TAG, "mic record start, pcm 16k");
    }
}

static void ver_retry_handle(void *arg)
{
    (void)arg;
    if (!g_open || g_got_version) {
        return;
    }
    if (g_ver_retry >= VB701N_VER_RETRY_MAX) {
        QM_LOGD(LOG_TAG, "version query timeout, continue anyway");
        on_chip_ready(NULL);
        return;
    }
    g_ver_retry++;
    (void)vb701n_proto_query_version();
    qm_post_delayed_action(&g_ver_work, ver_retry_handle, NULL, VB701N_VER_RETRY_MS);
}

static void on_proto_frame(uint16_t cmd, const uint8_t *data, uint16_t len, void *user)
{
    (void)user;
    if (cmd == VB701N_CMD_DEV_VERSION) {
        g_got_version = 1;
        if (data != NULL && len > 0) {
            char ver[64];
            uint32_t n = len;

            if (n >= sizeof(ver)) {
                n = sizeof(ver) - 1;
            }
            memcpy(ver, data, n);
            ver[n] = '\0';
            QM_LOGD(LOG_TAG, "version: %s", ver);
        }
        qm_post_delayed_action(&g_ready_work, on_chip_ready, NULL, VB701N_READY_DELAY_MS);
        return;
    }
    if (cmd == VB701N_CMD_DEV_ASR) {
        qm_audio_wake_info_t wake;

        if (data == NULL || len == 0) {
            QM_LOGD(LOG_TAG, "wakeup: (empty)");
        } else {
            char text[128];
            uint32_t n = len;

            if (n >= sizeof(text)) {
                n = sizeof(text) - 1;
            }
            memcpy(text, data, n);
            text[n] = '\0';
            QM_LOGD(LOG_TAG, "wakeup: %s", text);
        }
        memset(&wake, 0, sizeof(wake));
        wake.source = VB701N_WAKE_SOURCE;
        (void)qm_audio_report_wake(&wake);
    }
}

static void on_mic(const uint8_t *data, uint16_t len, void *user)
{
    int dropped = 0;

    (void)user;
    if (!g_cap || data == NULL || len == 0 || len > VB701N_AUDIO_SLOT_MAX) {
        return;
    }
    if ((len & 1u) != 0) {
        return;
    }
    if (!g_mic_logged) {
        g_mic_logged = 1;
        QM_LOGD(LOG_TAG, "mic %u bytes", (unsigned)len);
    }

    qm_mutex_lock(&g_lock, QM_WAIT_FOREVER);
    if (g_count == VB701N_AUDIO_Q_LEN) {
        g_tail = (uint8_t)((g_tail + 1) % VB701N_AUDIO_Q_LEN);
        g_count--;
        dropped = 1;
    }
    g_q[g_head].len = len;
    memcpy(g_q[g_head].data, data, len);
    g_head = (uint8_t)((g_head + 1) % VB701N_AUDIO_Q_LEN);
    g_count++;
    qm_mutex_unlock(&g_lock);

    if (!dropped) {
        qm_sem_signal(&g_sem);
    }
}

static void enc_task(void *arg)
{
    (void)arg;
    QM_LOGI(LOG_TAG, "enc loop");
    for (;;) {
        vb701n_audio_slot_t pcm;
        uint8_t opus[VB701N_OPUS_PKT_MAX];
        int n;
        int dropped = 0;

        if (qm_sem_wait(&g_sem, QM_WAIT_FOREVER) != QM_EOK) {
            continue;
        }
        qm_mutex_lock(&g_lock, QM_WAIT_FOREVER);
        if (g_count == 0) {
            qm_mutex_unlock(&g_lock);
            continue;
        }
        pcm = g_q[g_tail];
        g_tail = (uint8_t)((g_tail + 1) % VB701N_AUDIO_Q_LEN);
        g_count--;
        qm_mutex_unlock(&g_lock);

        n = opus_pcm_to_packet(pcm.data, pcm.len, opus, sizeof(opus));
        if (n <= 0) {
            continue;
        }
        if ((uint32_t)n > VB701N_OPUS_PKT_MAX) {
            QM_LOGW(LOG_TAG, "opus len %d exceeds %u", n, (unsigned)VB701N_OPUS_PKT_MAX);
            continue;
        }

        qm_mutex_lock(&g_lock, QM_WAIT_FOREVER);
        if (g_opus_count == VB701N_AUDIO_Q_LEN) {
            g_opus_tail = (uint8_t)((g_opus_tail + 1) % VB701N_AUDIO_Q_LEN);
            g_opus_count--;
            dropped = 1;
        }
        g_opus_q[g_opus_head].len = (uint16_t)n;
        memcpy(g_opus_q[g_opus_head].data, opus, (uint32_t)n);
        g_opus_head = (uint8_t)((g_opus_head + 1) % VB701N_AUDIO_Q_LEN);
        g_opus_count++;
        qm_mutex_unlock(&g_lock);
        if (!dropped) {
            qm_sem_signal(&g_opus_sem);
        } else if (!g_enc_drop_logged) {
            g_enc_drop_logged = 1;
            QM_LOGW(LOG_TAG, "opus queue full, drop");
        }
    }
}

static int enc_start(void)
{
    if (g_enc_ready) {
        return QM_EOK;
    }
    g_enc_stack = (StackType_t *)heap_caps_malloc(VB701N_ENC_STACK_BYTES,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (g_enc_stack == NULL) {
        QM_LOGE(LOG_TAG, "enc stack alloc failed");
        return -QM_ENOMEM;
    }
    if (xTaskCreateStatic(enc_task, "opus_enc",
                          VB701N_ENC_STACK_BYTES / sizeof(StackType_t),
                          NULL, CONFIG_QM_AUDIO_CAPTURE_TASK_PRIORITY,
                          g_enc_stack, &g_enc_tcb) == NULL) {
        QM_LOGE(LOG_TAG, "enc task create failed");
        heap_caps_free(g_enc_stack);
        g_enc_stack = NULL;
        return -QM_ENOMEM;
    }
    g_enc_ready = 1;
    QM_LOGI(LOG_TAG, "enc task stack=%u", (unsigned)VB701N_ENC_STACK_BYTES);
    return QM_EOK;
}

static int wait_gap(uint32_t gap, qm_utils_time_t *deadline, int timed)
{
    for (;;) {
        uint32_t now;
        uint32_t dt;

        if (g_abort_write) {
            return -QM_EINTR;
        }
        if (timed && qm_utils_time_is_expired(deadline)) {
            return -QM_ETIMEOUT;
        }
        if (flow_is_busy()) {
            qm_msleep(1);
            continue;
        }
        now = qm_now_ms();
        if (g_last_send_ms != 0) {
            dt = now - g_last_send_ms;
            if (dt < gap) {
                qm_msleep(gap - dt);
                continue;
            }
        }
        return QM_EOK;
    }
}

/* 整段发完才返回 QM_EOK。中途失败由调用方结束这一轮播放。 */
static int pace_send(const uint8_t *data, uint32_t len, uint32_t timeout_ms)
{
    uint32_t off = 0;
    qm_utils_time_t deadline;
    int timed = (timeout_ms != QM_WAIT_FOREVER);

    if (data == NULL || len == 0 || !g_open) {
        return -QM_EINVAL;
    }
    if (timed) {
        qm_utils_time_countdown_ms(&deadline, timeout_ms);
    }

    while (off < len) {
        uint16_t n;
        uint16_t sent = 0;
        uint32_t burst;
        uint32_t gap;
        int ret;

        if (g_abort_write) {
            g_abort_write = 0;
            return -QM_EINTR;
        }

        burst = (uint32_t)g_play_frame_len * VB701N_TX_BURST_FRAMES;
        if ((len - off) < burst) {
            burst = len - off;
        }
        n = (uint16_t)burst;

        gap = CONFIG_QM_VB701N_TX_INTERVAL_MS;
        ret = wait_gap(gap, &deadline, timed);
        if (ret != QM_EOK) {
            if (ret == -QM_EINTR) {
                g_abort_write = 0;
            }
            return ret;
        }

        /* 间隔从这一帧开始算，串口发送时间算在间隔里面。 */
        g_last_send_ms = qm_now_ms();
        while (sent < n) {
            uint16_t one = g_play_frame_len;

            if ((uint16_t)(n - sent) < one) {
                one = (uint16_t)(n - sent);
            }
            if (g_abort_write) {
                g_abort_write = 0;
                return -QM_EINTR;
            }
            if (!g_mute) {
                if (vb701n_proto_send(VB701N_CMD_HOST_SPK_AUDIO,
                                      data + off + sent, one) != QM_EOK) {
                    return -QM_EIO;
                }
            }
            sent = (uint16_t)(sent + one);
        }
        off += n;
    }
    return QM_EOK;
}

static void send_pcm_lead_silence(void)
{
    static uint8_t zero[VB701N_PCM_FRAME_LEN];
    uint32_t sent_ms = 0;

    while (sent_ms < VB701N_PCM_LEAD_MS) {
        if (pace_send(zero, g_play_frame_len, QM_WAIT_FOREVER) != QM_EOK) {
            break;
        }
        sent_ms += g_play_frame_ms;
    }
    QM_LOGD(LOG_TAG, "pcm lead silence %u ms", (unsigned)sent_ms);
}

static void uart_rx_task(void *arg)
{
    uint8_t buf[VB701N_RX_READ_BYTES];
    uint8_t logged = 0;

    (void)arg;
    while (!g_rx_stop) {
        int len = qm_uart_read(&g_uart, buf, sizeof(buf), VB701N_RX_WAIT_MS);

        if (len > 0) {
            if (!logged) {
                logged = 1;
                QM_LOGI(LOG_TAG, "uart rx first %d", len);
            }
            vb701n_proto_recv_push(buf, (uint32_t)len);
        }
    }
    (void)qm_sem_signal(&g_rx_exited);
    (void)qm_task_exit(NULL);
}

static int open_link(void)
{
    qm_err_t ret;

    memset(&g_uart, 0, sizeof(g_uart));
    g_uart.port = (uint8_t)CONFIG_QM_VB701N_UART_PORT;
    g_uart.config.baud_rate = CONFIG_QM_VB701N_UART_BAUDRATE;
    g_uart.config.data_bits = QM_UART_DATA_8_BITS;
    g_uart.config.parity = QM_UART_PARITY_NONE;
    g_uart.config.stop_bits = QM_UART_STOP_BITS_1;
    g_uart.config.flow_ctrl = QM_UART_FLOW_CTRL_NONE;
    g_uart.config.pins.tx_pin = (uint8_t)CONFIG_QM_VB701N_UART_TX_PIN;
    g_uart.config.pins.rx_pin = (uint8_t)CONFIG_QM_VB701N_UART_RX_PIN;

    QM_LOGD(LOG_TAG, "port %u TX=%u RX=%u baud=%u",
            (unsigned)g_uart.port,
            (unsigned)g_uart.config.pins.tx_pin,
            (unsigned)g_uart.config.pins.rx_pin,
            (unsigned)g_uart.config.baud_rate);
    ret = qm_uart_init(&g_uart);
    if (ret != QM_EOK) {
        QM_LOGD(LOG_TAG, "qm_uart_init fail %d", ret);
        return ret;
    }
    g_uart_up = 1;
    gpio_set_pull_mode(CONFIG_QM_VB701N_UART_RX_PIN, GPIO_PULLUP_ONLY);

    memset(&g_flow_gpio, 0, sizeof(g_flow_gpio));
    g_flow_gpio.port = CONFIG_QM_VB701N_FLOW_GPIO;
    g_flow_gpio.config.mode = QM_GPIO_MODE_INPUT;
    g_flow_gpio.config.pull_en = QM_GPIO_PULLDOWN_ONLY;
    ret = qm_gpio_init(&g_flow_gpio);
    if (ret != QM_EOK) {
        QM_LOGD(LOG_TAG, "flow gpio init fail %d", ret);
        return ret;
    }
    g_flow_up = 1;

    ret = vb701n_proto_init(&g_uart);
    if (ret != QM_EOK) {
        return ret;
    }
    vb701n_proto_set_frame_cb(on_proto_frame, NULL);
    if (qm_sem_new(&g_rx_exited, 0) != QM_EOK) {
        return -QM_ENOMEM;
    }
    g_rx_stop = 0;
    if (qm_task_new(&g_rx_task, "vb701n_uart", uart_rx_task, NULL,
                    VB701N_RX_STACK_BYTES, VB701N_RX_PRIORITY) != QM_EOK) {
        (void)qm_sem_free(&g_rx_exited);
        return -QM_ENOMEM;
    }
    g_rx_task_up = 1;
    return vb701n_proto_stop_audio();
}

static void close_link(void)
{
    (void)qm_cancel_delayed_action(&g_ver_work);
    (void)qm_cancel_delayed_action(&g_ready_work);
    vb701n_proto_set_mic_cb(NULL, NULL);
    vb701n_proto_set_frame_cb(NULL, NULL);
    if (g_rx_task_up) {
        g_rx_stop = 1;
        if (qm_sem_wait(&g_rx_exited, VB701N_RX_STOP_MS) != QM_EOK) {
            QM_LOGW(LOG_TAG, "uart rx task stop timeout");
        }
        g_rx_task_up = 0;
        (void)qm_sem_free(&g_rx_exited);
    }
    if (g_flow_up) {
        (void)qm_gpio_deinit(&g_flow_gpio);
        g_flow_up = 0;
    }
    if (g_uart_up) {
        (void)qm_uart_deinit(&g_uart);
        g_uart_up = 0;
    }
}

int qm_audio_device_open(void **ctx, const qm_audio_device_config_t *config)
{
    int ret;

    if (ctx == NULL || config == NULL) {
        return -QM_EINVAL;
    }
    *ctx = NULL;
    if (g_open) {
        return -QM_EBUSY;
    }
    if (!profile_ok(&config->input_format) || !profile_ok(&config->output_format)) {
        return -QM_ENOSYS;
    }
    if (!g_ready) {
        if (qm_mutex_new(&g_lock) != QM_EOK) {
            return -QM_ENOMEM;
        }
        if (qm_sem_new(&g_sem, 0) != QM_EOK) {
            qm_mutex_free(&g_lock);
            return -QM_ENOMEM;
        }
        if (qm_sem_new(&g_opus_sem, 0) != QM_EOK) {
            qm_sem_free(&g_sem);
            qm_mutex_free(&g_lock);
            return -QM_ENOMEM;
        }
        g_ready = 1;
    }

    g_in_fmt = config->input_format;
    g_out_fmt = config->output_format;
    g_play_frame_len = VB701N_PCM_FRAME_LEN;
    g_play_frame_ms = VB701N_PCM_FRAME_MS;
    g_abort_read = 0;
    g_abort_write = 0;
    g_play = 0;
    g_cap = 0;
    g_mute = 0;
    g_mic_logged = 0;
    g_mic_on = 0;
    g_last_send_ms = 0;
    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_opus_head = 0;
    g_opus_tail = 0;
    g_opus_count = 0;
    g_chip_ready = 0;
    g_got_version = 0;
    g_ver_retry = 0;
    while (qm_sem_wait(&g_sem, 0) == QM_EOK) {
    }
    while (qm_sem_wait(&g_opus_sem, 0) == QM_EOK) {
    }

    ret = open_link();
    if (ret != QM_EOK) {
        close_link();
        return ret;
    }
    ret = enc_start();
    if (ret != QM_EOK) {
        close_link();
        return ret;
    }
    vb701n_proto_set_mic_cb(on_mic, NULL);
    g_open = 1;
    *ctx = &g_ctx;
    qm_post_delayed_action(&g_ver_work, ver_retry_handle, NULL, VB701N_VER_FIRST_MS);
    QM_LOGD(LOG_TAG, "open opus 20ms, chip pcm 16k");
    return QM_EOK;
}

int qm_audio_device_close(void *ctx)
{
    (void)ctx;
    if (!g_open) {
        return QM_EOK;
    }
    if (g_play || g_cap) {
        return -QM_EBUSY;
    }
    g_open = 0;
    close_link();
    QM_LOGD(LOG_TAG, "close");
    return QM_EOK;
}

int qm_audio_device_get_caps(void *ctx, qm_audio_device_caps_t *caps)
{
    (void)ctx;
    if (!g_open || caps == NULL) {
        return -QM_EINVAL;
    }
    memset(caps, 0, sizeof(*caps));
    caps->input_format = g_in_fmt;
    caps->output_format = g_out_fmt;
    caps->flags = QM_AUDIO_DEVICE_CAP_CAPTURE |
                  QM_AUDIO_DEVICE_CAP_PLAYBACK |
                  QM_AUDIO_DEVICE_CAP_VOLUME |
                  QM_AUDIO_DEVICE_CAP_MUTE |
                  QM_AUDIO_DEVICE_CAP_ABORT_READ |
                  QM_AUDIO_DEVICE_CAP_ABORT_WRITE;
    return QM_EOK;
}

int qm_audio_device_playback_start(void *ctx, qm_audio_delivery_t delivery)
{
    (void)ctx;
    (void)delivery;
    if (!g_open) {
        return -QM_EINIT;
    }
    if (g_out_fmt.pcm.sample_rate_hz == 0) {
        return -QM_ENOSYS;
    }
    if (g_play) {
        return -QM_EBUSY;
    }
    g_abort_write = 0;
    g_last_send_ms = 0;
    g_play = 1;
    send_pcm_lead_silence();
    return QM_EOK;
}

int qm_audio_device_playback_write(void *ctx, const qm_audio_data_t *input, uint32_t timeout_ms)
{
    (void)ctx;
    if (!g_open || !g_play || input == NULL || input->data == NULL || input->size == 0) {
        return -QM_EINVAL;
    }
    if ((input->flags & QM_AUDIO_DATA_END_OF_STREAM) != 0 ||
        input->size > VB701N_OPUS_PKT_MAX) {
        return -QM_EINVAL;
    }
    {
        static uint8_t pcm[VB701N_PCM_DEC_MAX];
        static uint8_t s_play_logged;
        int pcm_len;

        if (!s_play_logged) {
            s_play_logged = 1;
            QM_LOGI(LOG_TAG, "play first opus %u", (unsigned)input->size);
        }
        pcm_len = opus_packet_to_pcm((const uint8_t *)input->data, input->size,
                                     pcm, sizeof(pcm));

        if (pcm_len <= 0) {
            return pcm_len < 0 ? pcm_len : -QM_ERROR;
        }
        return pace_send(pcm, (uint32_t)pcm_len, timeout_ms);
    }
}

int qm_audio_device_playback_drain(void *ctx, uint32_t timeout_ms)
{
    (void)ctx;
    (void)timeout_ms;
    /* write 返回时这一帧已经写进串口。芯片没有播放完成回执。 */
    if (!g_open) {
        return -QM_EINIT;
    }
    return QM_EOK;
}

int qm_audio_device_playback_pause(void *ctx)
{
    (void)ctx;
    return -QM_ENOSYS;
}

int qm_audio_device_playback_resume(void *ctx)
{
    (void)ctx;
    return -QM_ENOSYS;
}

int qm_audio_device_playback_abort(void *ctx)
{
    (void)ctx;
    g_abort_write = 1;
    return QM_EOK;
}

int qm_audio_device_playback_stop(void *ctx)
{
    (void)ctx;
    g_abort_write = 1;
    g_last_send_ms = 0;
    g_play = 0;
    return QM_EOK;
}

int qm_audio_device_playback_get_position(void *ctx, uint32_t *position_ms)
{
    (void)ctx;
    (void)position_ms;
    return -QM_ENOSYS;
}

int qm_audio_device_capture_start(void *ctx)
{
    (void)ctx;
    if (!g_open) {
        return -QM_EINIT;
    }
    if (g_in_fmt.pcm.sample_rate_hz == 0) {
        return -QM_ENOSYS;
    }
    if (g_cap) {
        return -QM_EBUSY;
    }
    qm_mutex_lock(&g_lock, QM_WAIT_FOREVER);
    g_head = 0;
    g_tail = 0;
    g_count = 0;
    g_opus_head = 0;
    g_opus_tail = 0;
    g_opus_count = 0;
    qm_mutex_unlock(&g_lock);
    while (qm_sem_wait(&g_sem, 0) == QM_EOK) {
    }
    while (qm_sem_wait(&g_opus_sem, 0) == QM_EOK) {
    }
    g_abort_read = 0;
    g_cap = 1;
    return vb701n_proto_start_record();
}

int qm_audio_device_capture_read(void *ctx, qm_audio_data_t *input, uint32_t timeout_ms)
{
    int ret;
    uint16_t n;

    (void)ctx;
    if (!g_open || !g_cap || input == NULL) {
        return -QM_EINVAL;
    }
    memset(input, 0, sizeof(*input));

    ret = qm_sem_wait(&g_opus_sem, timeout_ms);
    if (g_abort_read) {
        g_abort_read = 0;
        return -QM_EINTR;
    }
    if (ret != QM_EOK) {
        return -QM_ETIMEOUT;
    }

    qm_mutex_lock(&g_lock, QM_WAIT_FOREVER);
    if (g_opus_count == 0) {
        qm_mutex_unlock(&g_lock);
        return -QM_ETIMEOUT;
    }
    n = g_opus_q[g_opus_tail].len;
    memcpy(g_hold, g_opus_q[g_opus_tail].data, n);
    g_opus_tail = (uint8_t)((g_opus_tail + 1) % VB701N_AUDIO_Q_LEN);
    g_opus_count--;
    qm_mutex_unlock(&g_lock);

    input->data = g_hold;
    input->size = n;
    input->duration_ms = VB701N_OPUS_FRAME_MS;
    input->flags = QM_AUDIO_DATA_DURATION_VALID;
    return QM_EOK;
}

int qm_audio_device_capture_abort(void *ctx)
{
    (void)ctx;
    g_abort_read = 1;
    qm_sem_signal(&g_opus_sem);
    return QM_EOK;
}

int qm_audio_device_capture_stop(void *ctx)
{
    (void)ctx;
    /* 不发 0x0201。那条命令会把喇叭一起停掉。 */
    g_abort_read = 1;
    g_cap = 0;
    qm_sem_signal(&g_opus_sem);
    return QM_EOK;
}

int qm_audio_device_set_volume(void *ctx, uint8_t volume)
{
    (void)ctx;
    if (!g_open) {
        return -QM_EINIT;
    }
    if (volume > 100) {
        return -QM_EINVAL;
    }
    g_volume = volume;
    g_volume_valid = 1;
    flush_volume();
    return QM_EOK;
}

int qm_audio_device_set_mute(void *ctx, qm_bool muted)
{
    (void)ctx;
    if (!g_open) {
        return -QM_EINIT;
    }
    g_mute = muted ? 1 : 0;
    QM_LOGD(LOG_TAG, "mute %u", g_mute);
    return QM_EOK;
}

#endif /* CONFIG_QM_AUDIO_DEVICE_VB701N */
