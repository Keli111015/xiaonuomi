#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Real protocol and adapter against a fragmented, asynchronous fake UART. */
#include "../src/nc_audio.c"
#include "../../../platform/mcu/esp32s3/qm_audio/qm_audio_device_gx8006.c"

#define TEST_UART_CAPACITY 8192U
static pthread_mutex_t wire_lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t wire_rx[TEST_UART_CAPACITY];
static uint32_t wire_rx_size;
static uint8_t wire_tx[TEST_UART_CAPACITY];
static uint32_t wire_tx_size;
static bool installed;
static unsigned starts, packets, stops, mic_offs;
static unsigned partial_limit = 7U;
static bool auto_complete = true;
static int fail_after = -1;
static unsigned mic_codec, spk_codec;
static uint8_t gpio_level = 1U;
static bool uart_read_failure;

#define TEST_FEED_CAPACITY 16U
static pthread_mutex_t feed_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t feed_changed = PTHREAD_COND_INITIALIZER;
static qm_audio_data_t feed_inputs[TEST_FEED_CAPACITY];
static uint8_t feed_bytes[TEST_FEED_CAPACITY][GX_PACKET_BYTES];
static unsigned feed_count, feed_read, feed_errors;
static int feed_error;
static bool feed_blocked, feed_entered, feed_release;
static qm_sem_t feed_ready;

int qm_audio_capture_feed(const qm_audio_data_t *input, int result)
{
    pthread_mutex_lock(&feed_lock);
    feed_entered = true;
    pthread_cond_broadcast(&feed_changed);
    while (feed_blocked && !feed_release) pthread_cond_wait(&feed_changed, &feed_lock);
    if (result == QM_EOK) {
        assert(input != NULL && input->size == GX_PACKET_BYTES);
        assert(feed_count < TEST_FEED_CAPACITY);
        feed_inputs[feed_count] = *input;
        memcpy(feed_bytes[feed_count], input->data, input->size);
        feed_inputs[feed_count].data = feed_bytes[feed_count];
        feed_count++;
    } else {
        assert(input == NULL && result < 0);
        feed_error = result;
        feed_errors++;
    }
    pthread_mutex_unlock(&feed_lock);
    (void)qm_sem_signal(&feed_ready);
    return QM_EOK;
}

static void reset_feed(void)
{
    pthread_mutex_lock(&feed_lock);
    feed_count = feed_read = feed_errors = 0U;
    feed_error = QM_EOK;
    feed_blocked = feed_entered = feed_release = false;
    pthread_mutex_unlock(&feed_lock);
}

static int capture_receive(qm_audio_data_t *input, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        uint32_t left;
        pthread_mutex_lock(&feed_lock);
        if (feed_read < feed_count) {
            *input = feed_inputs[feed_read++];
            pthread_mutex_unlock(&feed_lock);
            return QM_EOK;
        }
        if (feed_error != QM_EOK) {
            int result = feed_error;
            pthread_mutex_unlock(&feed_lock);
            return result;
        }
        pthread_mutex_unlock(&feed_lock);
        left = qm_utils_time_left(&deadline);
        if (left == 0U) return -QM_ETIMEOUT;
        (void)qm_sem_wait(&feed_ready, left);
    }
}

void *qm_malloc(unsigned int size) { return malloc(size); }
void *qm_calloc(unsigned int n, unsigned int size) { return calloc(n, size); }
void qm_free(void *p) { free(p); }
void *qm_memcpy(void *dst, const void *src, uint32_t n) { return memcpy(dst, src, n); }
void *qm_memset(void *dst, int32_t c, uint32_t n) { return memset(dst, c, n); }

static uint32_t make_frame(uint8_t *out, uint8_t cmd, const uint8_t *data, uint32_t size)
{
    out[0] = 0x55U; out[1] = 0xAAU; out[2] = 3U; out[3] = 0x92U;
    out[4] = (uint8_t)((size + 1U) >> 8); out[5] = (uint8_t)(size + 1U);
    out[6] = cmd;
    if (size != 0U) memcpy(out + 7U, data, size);
    out[size + 7U] = checksum(out, size + 7U);
    return size + 8U;
}

static void queue_reply(uint8_t cmd, const uint8_t *data, uint32_t size)
{
    assert(wire_rx_size + size + 8U <= sizeof(wire_rx));
    wire_rx_size += make_frame(wire_rx + wire_rx_size, cmd, data, size);
}

static void accept_frame(const uint8_t *frame, uint32_t size)
{
    const uint8_t ok = 0U;
    const uint8_t version[] = {1U, 38U, 1U, 't', 'e', 's', 't'};
    const uint8_t empty[] = {0U, 0U, 0U, 2U};
    const uint8_t complete[] = {0U, 0U, 0U, 3U};
    assert(checksum(frame, size - 1U) == frame[size - 1U]);
    if (frame[6] == NC_AUDIO_SUB_CMD_GET_MCU_VER) {
        queue_reply(frame[6], version, sizeof(version));
    } else if (frame[6] == NC_AUDIO_SUB_CMD_MIC_CFG) {
        mic_codec = frame[14];
        queue_reply(frame[6], &ok, 1U);
    } else if (frame[6] == NC_AUDIO_SUB_CMD_SPK_CFG) {
        spk_codec = frame[13];
        queue_reply(frame[6], &ok, 1U);
    } else if (frame[6] == NC_AUDIO_SUB_CMD_SPK_PLAY) {
        if (frame[7] == NC_AUDIO_SPK_PLAY_TYPE_START) starts++;
        if (frame[7] == NC_AUDIO_SPK_PLAY_TYPE_DATA) {
            assert(size == GX_PACKET_BYTES + 13U);
            packets++;
        }
        if (frame[7] == NC_AUDIO_SPK_PLAY_TYPE_STOP) {
            stops++;
            queue_reply(NC_AUDIO_SUB_CMD_SKP_REPLY, empty, sizeof(empty));
            if (auto_complete) queue_reply(NC_AUDIO_SUB_CMD_SKP_REPLY, complete, sizeof(complete));
        }
    } else if (frame[6] == NC_AUDIO_SUB_CMD_AUDIO_CFG &&
               frame[7] == NC_AUDIO_CFG_TYPE_MIC_ONOFF && frame[8] == 0U) {
        mic_offs++;
    }
}

int uart_tx_chars(int port, const char *data, uint32_t size)
{
    uint32_t accepted = size > partial_limit ? partial_limit : size;
    (void)port;
    pthread_mutex_lock(&wire_lock);
    if (fail_after == 0) accepted = 0U;
    if (fail_after > 0) {
        if (accepted > (unsigned)fail_after) accepted = (unsigned)fail_after;
        fail_after -= (int)accepted;
    }
    assert(wire_tx_size + accepted <= sizeof(wire_tx));
    memcpy(wire_tx + wire_tx_size, data, accepted);
    wire_tx_size += accepted;
    while (wire_tx_size >= 6U) {
        uint32_t frame_size = (((uint32_t)wire_tx[4] << 8) | wire_tx[5]) + 7U;
        if (wire_tx_size < frame_size) break;
        accept_frame(wire_tx, frame_size);
        memmove(wire_tx, wire_tx + frame_size, wire_tx_size - frame_size);
        wire_tx_size -= frame_size;
    }
    pthread_mutex_unlock(&wire_lock);
    return (int)accepted;
}

int uart_param_config(int p, const uart_config_t *c) { (void)p; (void)c; return 0; }
int uart_set_pin(int p, int t, int r, int rt, int ct)
{ (void)p; (void)t; (void)r; (void)rt; (void)ct; return 0; }
int uart_driver_install(int p, int r, int t, int q, void *h, int f)
{ (void)p; (void)r; (void)t; (void)q; (void)h; (void)f; installed = true; return 0; }
bool uart_is_driver_installed(int port) { (void)port; return installed; }
int32_t qm_uart_deinit(qm_uart_dev_t *u) { (void)u; installed = false; return 0; }
int32_t qm_uart_read(qm_uart_dev_t *u, void *out, uint32_t cap, uint32_t ms)
{
    uint32_t count;
    (void)u;
    pthread_mutex_lock(&wire_lock);
    if (uart_read_failure) {
        pthread_mutex_unlock(&wire_lock);
        return -QM_ETIMEOUT;
    }
    count = wire_rx_size > cap ? cap : wire_rx_size;
    /* Split any protocol field, including its two-byte length. */
    if (count > 11U) count = 11U;
    memcpy(out, wire_rx, count);
    memmove(wire_rx, wire_rx + count, wire_rx_size - count);
    wire_rx_size -= count;
    pthread_mutex_unlock(&wire_lock);
    if (count == 0U && ms != 0U) qm_msleep(ms);
    return (int)count;
}
int32_t qm_gpio_init(qm_gpio_dev_t *g) { (void)g; return 0; }
int32_t qm_gpio_deinit(qm_gpio_dev_t *g) { (void)g; return 0; }
uint8_t qm_gpio_get_level(qm_gpio_dev_t *g) { (void)g; return gpio_level; }

static qm_audio_device_config_t config(void)
{
    qm_audio_device_config_t cfg = {0};
    cfg.input_format.codec = QM_AUDIO_CODEC_OPUS;
    cfg.input_format.pcm = (qm_audio_pcm_format_t){16000U, 1U, QM_AUDIO_PCM_S16};
    cfg.input_format.packet_duration_ms = GX_PACKET_DURATION_MS;
    cfg.input_format.packet_bytes = cfg.input_format.max_packet_bytes = GX_PACKET_BYTES;
    cfg.output_format = cfg.input_format;
    return cfg;
}

static qm_audio_data_t test_packet(void)
{
    static const uint8_t data[GX_PACKET_BYTES] = {[0] = 0x99U};
    qm_audio_data_t packet = {0};
    packet.data = data; packet.size = sizeof(data);
    packet.duration_ms = GX_PACKET_DURATION_MS;
    packet.flags = QM_AUDIO_DATA_DURATION_VALID;
    return packet;
}

static void test_playback_and_capture(void)
{
    qm_audio_device_config_t cfg = config();
    qm_audio_data_t packet = test_packet();
    qm_audio_data_t captured;
    qm_audio_device_caps_t caps;
    void *ctx = NULL;
    uint8_t mic[GX_PACKET_BYTES * 3U + 1U];
    uint8_t wrong_duration[GX_PACKET_BYTES] = {[0] = 0x91U};
    assert(qm_audio_device_open(&ctx, &cfg) == QM_EOK);
    assert(mic_codec == NC_AUDIO_FMT_TYPE_OPUS && spk_codec == NC_AUDIO_FMT_TYPE_OPUS);
    assert(qm_audio_device_get_caps(ctx, &caps) == QM_EOK);
    assert(caps.flags & QM_AUDIO_DEVICE_CAP_STOP_DRAINS);
    assert(caps.flags & QM_AUDIO_DEVICE_CAP_CAPTURE);
    assert(caps.flags & QM_AUDIO_DEVICE_CAP_ABORT_READ);
    assert(qm_audio_device_capture_start(ctx) == QM_EOK);
    mic[0] = NC_AUDIO_VAD_DATA;
    memset(mic + 1U, 0xA5, sizeof(mic) - 1U);
    mic[1U] = mic[1U + GX_PACKET_BYTES] =
        mic[1U + GX_PACKET_BYTES * 2U] = 0x99U;
    pthread_mutex_lock(&wire_lock);
    queue_reply(NC_AUDIO_SUB_CMD_MIC_DATA, mic, sizeof(mic));
    pthread_mutex_unlock(&wire_lock);
    qm_msleep(30U);
    assert(capture_receive(&captured, 200U) == QM_EOK);
    assert(captured.data != NULL && captured.size == GX_PACKET_BYTES);
    assert(captured.sequence == 0U);
    assert(captured.flags & QM_AUDIO_DATA_DISCONTINUITY);
    assert(!(captured.flags & QM_AUDIO_DATA_TIMESTAMP_VALID));
    assert(qm_audio_device_playback_start(ctx, QM_AUDIO_DELIVERY_RELIABLE) == QM_EOK);
    packet.data = wrong_duration;
    packet.flags = 0U;
    assert(qm_audio_device_playback_write(ctx, &packet, 200U) == -QM_EINVAL);
    assert(qm_audio_device_playback_stop(ctx) == QM_EOK);
    assert(qm_audio_device_playback_start(ctx, QM_AUDIO_DELIVERY_RELIABLE) == QM_EOK);
    packet = test_packet();
    packet.flags = 0U; /* TOC validation also protects metadata-free callers. */
    assert(qm_audio_device_playback_write(ctx, &packet, 200U) == QM_EOK);
    assert(qm_audio_device_playback_drain(ctx, 200U) == QM_EOK);
    assert(starts == 1U && packets == 1U && stops == 1U);
    assert(qm_audio_device_playback_stop(ctx) == QM_EOK);
    assert(((gx_ctx_t *)ctx)->capture_started);
    assert(qm_audio_device_capture_stop(ctx) == QM_EOK);
    assert(qm_audio_device_close(ctx) == QM_EOK);
    assert(s_gx8006 == NULL && g_comm_ctx == NULL);
}

static void test_empty_is_not_complete(void)
{
    qm_audio_device_config_t cfg = config();
    qm_audio_data_t packet = test_packet();
    const uint8_t complete[] = {0U, 0U, 0U, 3U};
    void *ctx = NULL;
    auto_complete = false;
    assert(qm_audio_device_open(&ctx, &cfg) == QM_EOK);
    assert(qm_audio_device_playback_start(ctx, QM_AUDIO_DELIVERY_RELIABLE) == QM_EOK);
    assert(qm_audio_device_playback_write(ctx, &packet, 200U) == QM_EOK);
    assert(qm_audio_device_playback_drain(ctx, 30U) == -QM_ETIMEOUT);
    assert(!((gx_ctx_t *)ctx)->poisoned);
    assert(qm_audio_device_playback_start(ctx, QM_AUDIO_DELIVERY_RELIABLE) == -QM_EBUSY);
    pthread_mutex_lock(&wire_lock);
    queue_reply(NC_AUDIO_SUB_CMD_SKP_REPLY, complete, sizeof(complete));
    pthread_mutex_unlock(&wire_lock);
    assert(qm_audio_device_playback_drain(ctx, 200U) == QM_EOK);
    assert(qm_audio_device_playback_stop(ctx) == QM_EOK);
    assert(qm_audio_device_close(ctx) == QM_EOK);
    auto_complete = true;
}

static void test_partial_transport_poison(void)
{
    qm_audio_device_config_t cfg = config();
    qm_audio_data_t packet = test_packet();
    void *ctx = NULL;
    assert(qm_audio_device_open(&ctx, &cfg) == QM_EOK);
    assert(qm_audio_device_playback_start(ctx, QM_AUDIO_DELIVERY_RELIABLE) == QM_EOK);
    fail_after = 5;
    assert(qm_audio_device_playback_write(ctx, &packet, 5U) == -QM_EIO);
    assert(((gx_ctx_t *)ctx)->poisoned);
    assert(qm_audio_device_close(ctx) == -QM_EIO);
    assert(s_gx8006 == ctx); /* Failed close retains all live resources. */
    fail_after = -1;
    /* Test-only hardware reset permits cleanup. Product recovery requires reset. */
    (void)qm_mutex_lock(&((gx_ctx_t *)ctx)->lock, QM_WAIT_FOREVER);
    ((gx_ctx_t *)ctx)->poisoned = QM_FALSE;
    ((gx_ctx_t *)ctx)->render_started = QM_FALSE;
    (void)qm_mutex_unlock(&((gx_ctx_t *)ctx)->lock);
    wire_tx_size = 0U;
    assert(qm_audio_device_close(ctx) == QM_EOK);
}

static unsigned parsed_mic, parsed_states;
static uint32_t parsed_msg_id;
static int protocol_sink(nc_audio_event_type_t type, nc_audio_event_param_t *event)
{
    if (type == NC_AUDIO_EVENT_TYPE_MIC_DATA_GET) {
        assert(event->mic_data.payload_len == GX_PACKET_BYTES);
        parsed_mic++;
    }
    if (type == NC_AUDIO_EVENT_TYPE_SPK_REPLY) parsed_states++;
    if (type == NC_AUDIO_EVENT_TYPE_SPK_PLAY_ACK) parsed_msg_id = event->spk_play_ack.msg_id;
    return QM_EOK;
}
static int protocol_send(uint8_t *data, int len, uint32_t timeout_ms)
{
    (void)timeout_ms;
    assert(len > 7 && checksum(data, (uint32_t)len - 1U) == data[len - 1]);
    if (data[6] == NC_AUDIO_SUB_CMD_MIC_CFG) mic_codec = data[14];
    if (data[6] == NC_AUDIO_SUB_CMD_SPK_CFG) spk_codec = data[13];
    return QM_EOK;
}

static void test_protocol_bounds_and_independent_formats(void)
{
    nc_audio_param_t cfg = {0};
    uint8_t frame[512], second[512], mic[GX_PACKET_BYTES + 1U] = {NC_AUDIO_VAD_DATA};
    uint8_t version[] = {1U, 38U, 1U};
    uint8_t ack[] = {0U, 0xFEU, 0xDCU, 0xBAU, 0x98U};
    uint8_t bad_length[] = {0x55U, 0xAAU, 3U, 0x92U, 0xFFU, 0xFFU};
    uint32_t len;
    cfg.send = protocol_send; cfg.notify = protocol_sink;
    cfg.mic = (nc_audio_format_t){NC_AUDIO_FMT_TYPE_OPUS, 16000U, 512U, 16U, 1U};
    cfg.spk = (nc_audio_format_t){NC_AUDIO_FMT_TYPE_PCM, 24000U, 512U, 16U, 1U};
    assert(nc_audio_init(&cfg) == QM_EOK);
    len = make_frame(frame, NC_AUDIO_SUB_CMD_GET_MCU_VER, version, sizeof(version));
    for (uint32_t i = 0; i < len; ++i) assert(nc_audio_recv_push(frame + i, 1) == QM_EOK);
    assert(mic_codec == NC_AUDIO_FMT_TYPE_OPUS && spk_codec == NC_AUDIO_FMT_TYPE_PCM);
    assert(nc_audio_recv_push(bad_length, sizeof(bad_length)) == -QM_EINVAL);
    len = make_frame(frame, NC_AUDIO_SUB_CMD_MIC_DATA, mic, sizeof(mic));
    assert(nc_audio_recv_push(frame, (int)len) == QM_EOK && parsed_mic == 1U);
    frame[len - 1U] ^= 1U;
    assert(nc_audio_recv_push(frame, (int)len) == -QM_EIO && parsed_mic == 1U);
    len = make_frame(frame, NC_AUDIO_SUB_CMD_MIC_DATA, NULL, 0U);
    assert(nc_audio_recv_push(frame, (int)len) == -QM_EINVAL);
    len = make_frame(frame, NC_AUDIO_SUB_CMD_SPK_PLAY, ack, sizeof(ack));
    assert(nc_audio_recv_push(frame, (int)len) == QM_EOK && parsed_msg_id == 0xFEDCBA98U);
    len = make_frame(frame, NC_AUDIO_SUB_CMD_SKP_REPLY, ack, 3U);
    assert(nc_audio_recv_push(frame, (int)len) == -QM_EINVAL && parsed_states == 0U);
    len = make_frame(frame, NC_AUDIO_SUB_CMD_MIC_DATA, mic, sizeof(mic));
    memcpy(second, frame, len);
    memcpy(second + len, frame, len);
    assert(nc_audio_recv_push(second, (int)(len * 2U)) == QM_EOK && parsed_mic == 3U);
    assert(nc_audio_deinit() == QM_EOK);
    assert(nc_audio_init(&cfg) == QM_EOK); /* No retained parser registrations. */
    assert(nc_audio_deinit() == QM_EOK);
}

static void test_stop_timeout_blocks_reuse(void)
{
    qm_audio_device_config_t cfg = config();
    qm_audio_data_t packet = test_packet();
    void *ctx = NULL;
    auto_complete = false;
    assert(qm_audio_device_open(&ctx, &cfg) == QM_EOK);
    assert(qm_audio_device_playback_start(ctx, QM_AUDIO_DELIVERY_RELIABLE) == QM_EOK);
    assert(qm_audio_device_playback_write(ctx, &packet, 200U) == QM_EOK);
    assert(qm_audio_device_playback_stop(ctx) == -QM_ETIMEOUT);
    assert(qm_audio_device_playback_start(ctx, QM_AUDIO_DELIVERY_RELIABLE) == -QM_EIO);
    assert(qm_audio_device_close(ctx) == -QM_EIO && s_gx8006 == ctx);
    /* Test-only externally confirmed reset. */
    (void)qm_mutex_lock(&((gx_ctx_t *)ctx)->lock, QM_WAIT_FOREVER);
    ((gx_ctx_t *)ctx)->poisoned = QM_FALSE;
    ((gx_ctx_t *)ctx)->render_started = QM_FALSE;
    (void)qm_mutex_unlock(&((gx_ctx_t *)ctx)->lock);
    assert(qm_audio_device_close(ctx) == QM_EOK);
    auto_complete = true;
}

static void test_capture_start_failure_requires_stop(void)
{
    qm_audio_device_config_t cfg = config();
    gx_ctx_t *ctx = NULL;
    void *backend = NULL;
    unsigned before = 0U;

    assert(qm_audio_device_open(&backend, &cfg) == QM_EOK);
    ctx = (gx_ctx_t *)backend;
    before = mic_offs;
    fail_after = 0;
    assert(qm_audio_device_capture_start(ctx) == -QM_ETIMEOUT);
    assert(ctx->capture_started && ctx->capture_abort);
    assert(qm_audio_device_capture_start(ctx) == -QM_EBUSY);
    assert(qm_audio_device_capture_stop(ctx) == -QM_ETIMEOUT);
    assert(ctx->capture_started && ctx->capture_abort);
    assert(mic_offs == before);

    fail_after = -1;
    assert(qm_audio_device_capture_stop(ctx) == QM_EOK);
    assert(!ctx->capture_started && mic_offs == before + 1U);
    assert(qm_audio_device_capture_start(ctx) == QM_EOK);
    assert(qm_audio_device_capture_stop(ctx) == QM_EOK);
    assert(qm_audio_device_close(ctx) == QM_EOK);
}

static void test_capture_stop_retries_after_send_failure(void)
{
    qm_audio_device_config_t cfg = config();
    gx_ctx_t *ctx;
    void *backend = NULL;
    unsigned before;

    assert(qm_audio_device_open(&backend, &cfg) == QM_EOK);
    ctx = (gx_ctx_t *)backend;
    assert(qm_audio_device_capture_start(ctx) == QM_EOK);
    before = mic_offs;
    fail_after = 0;
    assert(qm_audio_device_capture_stop(ctx) == -QM_ETIMEOUT);
    assert(ctx->capture_started && ctx->capture_abort);
    assert(mic_offs == before);
    fail_after = -1;
    assert(qm_audio_device_capture_stop(ctx) == QM_EOK);
    assert(!ctx->capture_started && mic_offs == before + 1U);
    assert(qm_audio_device_close(ctx) == QM_EOK);
}

static void test_capture_rejects_wrong_opus_profile(void)
{
    qm_audio_device_config_t cfg = config();
    void *ctx = NULL;
    uint8_t invalid[GX_PACKET_BYTES + 1U] = {NC_AUDIO_VAD_DATA};
    uint8_t valid[GX_PACKET_BYTES + 1U] = {NC_AUDIO_VAD_DATA};
    qm_audio_data_t captured;

    invalid[1U] = 0x91U; /* 20 ms, not the negotiated 40 ms profile. */
    valid[1U] = 0x99U;
    assert(qm_audio_device_open(&ctx, &cfg) == QM_EOK);
    assert(qm_audio_device_capture_start(ctx) == QM_EOK);
    pthread_mutex_lock(&wire_lock);
    queue_reply(NC_AUDIO_SUB_CMD_MIC_DATA, invalid, sizeof(invalid));
    pthread_mutex_unlock(&wire_lock);
    qm_msleep(30U);
    assert(capture_receive(&captured, 20U) ==
           -QM_ETIMEOUT);
    pthread_mutex_lock(&wire_lock);
    queue_reply(NC_AUDIO_SUB_CMD_MIC_DATA, valid, sizeof(valid));
    pthread_mutex_unlock(&wire_lock);
    qm_msleep(30U);
    assert(capture_receive(&captured, 200U) ==
           QM_EOK);
    assert((captured.flags & QM_AUDIO_DATA_DISCONTINUITY) != 0U);
    assert(qm_audio_device_capture_stop(ctx) == QM_EOK);
    assert(qm_audio_device_close(ctx) == QM_EOK);
}

static void test_capture_uart_failure(void)
{
    qm_audio_device_config_t cfg = config();
    qm_audio_data_t captured;
    void *backend = NULL;
    assert(qm_audio_device_open(&backend, &cfg) == QM_EOK);
    assert(qm_audio_device_capture_start(backend) == QM_EOK);
    pthread_mutex_lock(&wire_lock);
    uart_read_failure = true;
    pthread_mutex_unlock(&wire_lock);
    assert(capture_receive(&captured, 200U) == -QM_EIO);
    pthread_mutex_lock(&feed_lock);
    assert(feed_errors == 1U && feed_count == 0U);
    pthread_mutex_unlock(&feed_lock);
    pthread_mutex_lock(&wire_lock);
    uart_read_failure = false;
    pthread_mutex_unlock(&wire_lock);
    /* Test-only confirmed reset allows cleanup of the poisoned transport. */
    gx_ctx_t *ctx = backend;
    (void)qm_mutex_lock(&ctx->lock, QM_WAIT_FOREVER);
    ctx->poisoned = QM_FALSE;
    (void)qm_mutex_unlock(&ctx->lock);
    assert(qm_audio_device_close(ctx) == QM_EOK);
}

static void test_capture_stop_waits_for_feed(void)
{
    qm_audio_device_config_t cfg = config();
    qm_audio_data_t input;
    uint8_t mic[GX_PACKET_BYTES * 3U + 1U] = {NC_AUDIO_VAD_DATA};
    uint8_t restarted[GX_PACKET_BYTES + 1U] = {NC_AUDIO_VAD_DATA, 0x99U, 0x5AU};
    void *backend = NULL;
    mic[1U] = mic[1U + GX_PACKET_BYTES] = mic[1U + GX_PACKET_BYTES * 2U] = 0x99U;
    assert(qm_audio_device_open(&backend, &cfg) == QM_EOK);
    assert(qm_audio_device_capture_start(backend) == QM_EOK);
    pthread_mutex_lock(&feed_lock);
    feed_blocked = true;
    pthread_mutex_unlock(&feed_lock);
    pthread_mutex_lock(&wire_lock);
    queue_reply(NC_AUDIO_SUB_CMD_MIC_DATA, mic, sizeof(mic));
    pthread_mutex_unlock(&wire_lock);
    pthread_mutex_lock(&feed_lock);
    while (!feed_entered) pthread_cond_wait(&feed_changed, &feed_lock);
    pthread_mutex_unlock(&feed_lock);
    assert(qm_audio_device_capture_stop(backend) == -QM_ETIMEOUT);
    assert(((gx_ctx_t *)backend)->capture_started);
    pthread_mutex_lock(&feed_lock);
    feed_release = true;
    pthread_cond_broadcast(&feed_changed);
    pthread_mutex_unlock(&feed_lock);
    assert(qm_audio_device_capture_stop(backend) == QM_EOK);
    /* Restart immediately: the old aggregate must not enter the new run.
     * 立即重启：旧聚合包剩余数据不得进入新一轮采集。 */
    assert(qm_audio_device_capture_start(backend) == QM_EOK);
    pthread_mutex_lock(&wire_lock);
    queue_reply(NC_AUDIO_SUB_CMD_MIC_DATA, restarted, sizeof(restarted));
    pthread_mutex_unlock(&wire_lock);
    assert(capture_receive(&input, 200U) == QM_EOK);
    assert(input.sequence == 0U && ((const uint8_t *)input.data)[1U] == 0U);
    assert(capture_receive(&input, 200U) == QM_EOK);
    assert(input.sequence == 0U && ((const uint8_t *)input.data)[1U] == 0x5AU);
    assert((input.flags & QM_AUDIO_DATA_DISCONTINUITY) != 0U);
    assert(capture_receive(&input, 20U) == -QM_ETIMEOUT);
    assert(qm_audio_device_capture_stop(backend) == QM_EOK);
    assert(qm_audio_device_close(backend) == QM_EOK);
    pthread_mutex_lock(&feed_lock);
    assert(feed_count == 2U && feed_errors == 0U);
    pthread_mutex_unlock(&feed_lock);
}

int main(void)
{
    assert(qm_sem_new(&feed_ready, 0U) == QM_EOK);
    reset_feed();
    test_protocol_bounds_and_independent_formats();
    test_playback_and_capture();
    test_empty_is_not_complete();
    test_partial_transport_poison();
    test_stop_timeout_blocks_reuse();
    test_capture_start_failure_requires_stop();
    test_capture_stop_retries_after_send_failure();
    reset_feed();
    test_capture_rejects_wrong_opus_profile();
    reset_feed();
    test_capture_uart_failure();
    reset_feed();
    test_capture_stop_waits_for_feed();
    assert(qm_sem_free(&feed_ready) == QM_EOK);
    puts("PASS GX8006: fragmented UART, packet capture, drain barrier, partial-send poison");
    return 0;
}
