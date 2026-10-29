/** @file test_engine.c @brief Engine I/O ownership and cleanup regressions. / Engine I/O 所有权和清理回归。 */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include "qm_audio_priv.h"

#define TEST_WAIT_MS 2000U
#define TEST_PACKET_BYTES 8U
#define TEST_RECORD_COUNT 8U
#define TEST_PCM_BYTES (CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES + 32U)

typedef struct {
    qm_audio_data_t input;
    uint8_t bytes[CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES];
} record_t;

typedef struct {
    qm_audio_engine_t *engine;
    qm_audio_device_config_t config;
    qm_queue_t records;
    qm_sem_t start_entered;
    qm_sem_t start_release;
    qm_sem_t write_entered;
    qm_sem_t write_release;
    qm_sem_t drain_entered;
    qm_sem_t drain_release;
    qm_sem_t stop_entered;
    qm_sem_t stop_release;
    qm_sem_t pause_entered;
    qm_sem_t pause_release;
    qm_sem_t abort_entered;
    qm_sem_t abort_release;
    qm_sem_t position_entered;
    qm_sem_t position_release;
    atomic_int block_start;
    atomic_int block_write;
    atomic_int block_drain;
    atomic_int block_stop;
    atomic_int block_pause;
    atomic_int block_position;
    atomic_int block_abort;
    atomic_int start_error;
    atomic_int stop_error;
    atomic_int resume_error;
    atomic_int close_error;
    atomic_int capture_shutdown_error;
    atomic_int capture_shutdowns;
    atomic_int capture_destroys;
    atomic_int aborted;
    atomic_int active;
    atomic_int normal_inflight;
    atomic_int drain_active;
    atomic_int writes;
    atomic_int drains;
    atomic_int stops;
    atomic_int aborts;
    atomic_int settings;
    atomic_int closes;
} device_t;

typedef struct {
    qm_audio_render_stream_t *stream;
    pthread_t thread;
    int result;
    uint32_t position_ms;
} stream_call_t;

typedef struct {
    atomic_uint counts[QM_AUDIO_EVENT_SEEKED + 1U];
} service_events_t;

typedef struct {
    qm_audio_session_t *session;
    pthread_t thread;
    int result;
} service_call_t;

typedef struct {
    qm_audio_service_t *service;
    qm_audio_session_t *new_session;
    service_events_t new_events;
    qm_bool injected;
    qm_bool complete_preempt;
} preempt_notice_t;

static device_t device;

static void sem_init(qm_sem_t *sem)
{
    assert(qm_sem_new(sem, 0U) == QM_EOK);
}

static void take(qm_sem_t *sem)
{
    assert(qm_sem_wait(sem, TEST_WAIT_MS) == QM_EOK);
}

static void give(qm_sem_t *sem)
{
    assert(qm_sem_signal(sem) == QM_EOK);
}

/* Playback operations are serialized on the real resident worker; drain is
 * the one exclusive caller-owned operation. / 普通播放操作检查真实常驻任务，
 * drain 是唯一由调用任务执行的独占操作。 */
static void normal_enter(device_t *ctx)
{
    assert(ctx == &device);
    assert(qm_task_is_current(&ctx->engine->render_task));
    assert(atomic_fetch_add(&ctx->normal_inflight, 1) == 0);
    assert(atomic_load(&ctx->drain_active) == 0);
}

static void normal_leave(device_t *ctx)
{
    assert(atomic_fetch_sub(&ctx->normal_inflight, 1) == 1);
}

int qm_audio_device_open(void **out, const qm_audio_device_config_t *config)
{
    assert(out != NULL && config != NULL);
    device.config = *config;
    *out = &device;
    return QM_EOK;
}

int qm_audio_device_get_caps(void *context, qm_audio_device_caps_t *caps)
{
    device_t *ctx = context;

    assert(ctx == &device && caps != NULL);
    *caps = (qm_audio_device_caps_t){
        .input_format = ctx->config.input_format,
        .output_format = ctx->config.output_format,
        .flags = QM_AUDIO_DEVICE_CAP_PLAYBACK | QM_AUDIO_DEVICE_CAP_DRAIN |
                 QM_AUDIO_DEVICE_CAP_ABORT_WRITE | QM_AUDIO_DEVICE_CAP_POSITION |
                 QM_AUDIO_DEVICE_CAP_VOLUME | QM_AUDIO_DEVICE_CAP_MUTE,
    };
    if (qm_audio_stream_format_valid(&ctx->config.input_format)) {
        caps->flags |= QM_AUDIO_DEVICE_CAP_CAPTURE;
    }
    return QM_EOK;
}

int qm_audio_device_close(void *context)
{
    device_t *ctx = context;

    assert(ctx == &device);
    assert(atomic_load(&ctx->active) == 0);
    assert(atomic_load(&ctx->normal_inflight) == 0);
    assert(atomic_load(&ctx->drain_active) == 0);
    atomic_fetch_add(&ctx->closes, 1);
    return atomic_load(&ctx->close_error);
}

int qm_audio_device_playback_start(void *context, qm_audio_delivery_t delivery)
{
    device_t *ctx = context;
    int ret = QM_EOK;

    normal_enter(ctx);
    assert(delivery == QM_AUDIO_DELIVERY_RELIABLE);
    give(&ctx->start_entered);
    if (atomic_load(&ctx->block_start)) {
        take(&ctx->start_release);
    }
    /* Model platforms that clear abort near the end of START, even if STOP
     * already requested cancellation. / 模拟 START 尾部清除先到达的 abort。 */
    atomic_store(&ctx->aborted, 0);
    atomic_store(&ctx->active, 1);
    ret = atomic_load(&ctx->start_error);
    normal_leave(ctx);
    return ret;
}

int qm_audio_device_playback_write(void *context, const qm_audio_data_t *input,
                                   uint32_t timeout_ms)
{
    device_t *ctx = context;
    record_t record = {0};
    int ret = QM_EOK;

    normal_enter(ctx);
    assert(input != NULL && input->size <= sizeof(record.bytes));
    assert(timeout_ms == CONFIG_QM_AUDIO_ENGINE_RENDER_WRITE_TIMEOUT_MS);
    record.input = *input;
    record.input.data = NULL;
    qm_memcpy(record.bytes, input->data, input->size);
    assert(qm_queue_send(&ctx->records, &record, sizeof(record)) == QM_EOK);
    atomic_fetch_add(&ctx->writes, 1);
    give(&ctx->write_entered);
    if (atomic_load(&ctx->block_write)) {
        take(&ctx->write_release);
    }
    if (atomic_load(&ctx->aborted)) {
        ret = -QM_EINTR;
    }
    normal_leave(ctx);
    return ret;
}

int qm_audio_device_playback_drain(void *context, uint32_t timeout_ms)
{
    device_t *ctx = context;
    int ret = QM_EOK;

    assert(ctx == &device);
    assert(!qm_task_is_current(&ctx->engine->render_task));
    assert(atomic_fetch_add(&ctx->drain_active, 1) == 0);
    assert(atomic_load(&ctx->normal_inflight) == 0);
    atomic_fetch_add(&ctx->drains, 1);
    give(&ctx->drain_entered);
    if (atomic_load(&ctx->block_drain)) {
        assert(timeout_ms == QM_WAIT_FOREVER);
        take(&ctx->drain_release);
    }
    if (atomic_load(&ctx->aborted)) {
        ret = -QM_EINTR;
    }
    assert(atomic_fetch_sub(&ctx->drain_active, 1) == 1);
    return ret;
}

int qm_audio_device_playback_abort(void *context)
{
    device_t *ctx = context;

    assert(ctx == &device);
    atomic_store(&ctx->aborted, 1);
    atomic_fetch_add(&ctx->aborts, 1);
    give(&ctx->abort_entered);
    give(&ctx->write_release);
    give(&ctx->drain_release);
    if (atomic_exchange(&ctx->block_abort, 0)) {
        take(&ctx->abort_release);
    }
    return QM_EOK;
}

int qm_audio_device_playback_stop(void *context)
{
    device_t *ctx = context;
    int ret = QM_EOK;

    normal_enter(ctx);
    assert(atomic_load(&ctx->aborted));
    atomic_fetch_add(&ctx->stops, 1);
    give(&ctx->stop_entered);
    if (atomic_load(&ctx->block_stop)) {
        take(&ctx->stop_release);
    }
    ret = atomic_load(&ctx->stop_error);
    if (ret == QM_EOK) {
        atomic_store(&ctx->active, 0);
    }
    normal_leave(ctx);
    return ret;
}

int qm_audio_device_playback_pause(void *context)
{
    device_t *ctx = context;

    normal_enter(ctx);
    give(&ctx->pause_entered);
    if (atomic_load(&ctx->block_pause)) {
        take(&ctx->pause_release);
    }
    normal_leave(ctx);
    return QM_EOK;
}

int qm_audio_device_playback_resume(void *context)
{
    device_t *ctx = context;
    int ret = QM_EOK;

    normal_enter(ctx);
    ret = atomic_load(&ctx->resume_error);
    normal_leave(ctx);
    return ret;
}

int qm_audio_device_playback_get_position(void *context, uint32_t *position_ms)
{
    device_t *ctx = context;

    normal_enter(ctx);
    give(&ctx->position_entered);
    if (atomic_load(&ctx->block_position)) {
        take(&ctx->position_release);
    }
    *position_ms = 37U;
    normal_leave(ctx);
    return QM_EOK;
}

int qm_audio_device_set_volume(void *context, uint8_t volume)
{
    device_t *ctx = context;

    normal_enter(ctx);
    assert(volume <= 100U);
    atomic_fetch_add(&ctx->settings, 1);
    normal_leave(ctx);
    return QM_EOK;
}

int qm_audio_device_set_mute(void *context, qm_bool muted)
{
    device_t *ctx = context;

    normal_enter(ctx);
    assert(muted == QM_TRUE || muted == QM_FALSE);
    atomic_fetch_add(&ctx->settings, 1);
    normal_leave(ctx);
    return QM_EOK;
}

/* Only Capture lifecycle failure is faked here; media delivery has its own
 * push/pull suite. / 此处只模拟 Capture 生命周期故障，媒体交付有独立回归。 */
int qm_audio_capture_create(const qm_audio_capture_open_config_t *config,
                            qm_audio_capture_t **out_capture)
{
    assert(config != NULL && config->device_ctx == &device);
    assert(qm_audio_stream_format_valid(&config->input_format));
    assert(out_capture != NULL);
    *out_capture = (qm_audio_capture_t *)&device;
    return QM_EOK;
}

int qm_audio_capture_shutdown(qm_audio_capture_t *capture, uint32_t timeout_ms)
{
    QM_UNUSED_ARG(timeout_ms);
    assert((void *)capture == &device);
    atomic_fetch_add(&device.capture_shutdowns, 1);
    return atomic_load(&device.capture_shutdown_error);
}

int qm_audio_capture_destroy(qm_audio_capture_t *capture)
{
    assert((void *)capture == &device);
    assert(atomic_load(&device.capture_shutdown_error) == QM_EOK);
    atomic_fetch_add(&device.capture_destroys, 1);
    return QM_EOK;
}

void qm_audio_event_wake(qm_audio_t *audio)
{
    assert(audio == NULL);
}

static qm_audio_engine_t *create_engine_profile(qm_bool packets, qm_bool capture)
{
    qm_audio_engine_t *engine = NULL;
    qm_audio_engine_config_t config = {
        .output_format = {
            .codec = QM_AUDIO_CODEC_PCM,
            .pcm = {.sample_rate_hz = 16000U, .channels = 1U, .sample = QM_AUDIO_PCM_S16},
        },
    };

    qm_memset(&device, 0, sizeof(device));
    sem_init(&device.start_entered);
    sem_init(&device.start_release);
    sem_init(&device.write_entered);
    sem_init(&device.write_release);
    sem_init(&device.drain_entered);
    sem_init(&device.drain_release);
    sem_init(&device.stop_entered);
    sem_init(&device.stop_release);
    sem_init(&device.pause_entered);
    sem_init(&device.pause_release);
    sem_init(&device.abort_entered);
    sem_init(&device.abort_release);
    sem_init(&device.position_entered);
    sem_init(&device.position_release);
    assert(qm_queue_new(&device.records, TEST_RECORD_COUNT, sizeof(record_t)) == QM_EOK);
    if (packets) {
        config.output_format.codec = QM_AUDIO_CODEC_OPUS;
        config.output_format.packet_bytes = TEST_PACKET_BYTES;
        config.output_format.max_packet_bytes = TEST_PACKET_BYTES;
        config.output_format.packet_duration_ms = 20U;
    }
    if (capture) {
        config.input_format = config.output_format;
    }
    assert(qm_audio_engine_create(&config, &engine) == QM_EOK);
    device.engine = engine;
    return engine;
}

static qm_audio_engine_t *create_engine(qm_bool packets)
{
    return create_engine_profile(packets, QM_FALSE);
}

static qm_audio_render_stream_t *open_stream(qm_audio_engine_t *engine)
{
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_render_config_t config = {.delivery = QM_AUDIO_DELIVERY_RELIABLE};

    assert(qm_audio_engine_render_open(engine, &config, &stream) == QM_EOK);
    assert(stream != NULL);
    return stream;
}

static void destroy_engine(qm_audio_engine_t *engine)
{
    assert(qm_audio_engine_shutdown(engine, TEST_WAIT_MS) == QM_EOK);
    assert(qm_audio_engine_destroy(engine) == QM_EOK);
    assert(qm_queue_free(&device.records) == QM_EOK);
    assert(qm_sem_free(&device.start_entered) == QM_EOK);
    assert(qm_sem_free(&device.start_release) == QM_EOK);
    assert(qm_sem_free(&device.write_entered) == QM_EOK);
    assert(qm_sem_free(&device.write_release) == QM_EOK);
    assert(qm_sem_free(&device.drain_entered) == QM_EOK);
    assert(qm_sem_free(&device.drain_release) == QM_EOK);
    assert(qm_sem_free(&device.stop_entered) == QM_EOK);
    assert(qm_sem_free(&device.stop_release) == QM_EOK);
    assert(qm_sem_free(&device.pause_entered) == QM_EOK);
    assert(qm_sem_free(&device.pause_release) == QM_EOK);
    assert(qm_sem_free(&device.abort_entered) == QM_EOK);
    assert(qm_sem_free(&device.abort_release) == QM_EOK);
    assert(qm_sem_free(&device.position_entered) == QM_EOK);
    assert(qm_sem_free(&device.position_release) == QM_EOK);
}

static void read_record(record_t *record)
{
    unsigned int size = sizeof(*record);

    assert(qm_queue_recv(&device.records, record, &size, TEST_WAIT_MS) == QM_EOK);
    assert(size == sizeof(*record));
}

static void *drain_thread(void *context)
{
    stream_call_t *call = context;

    call->result = qm_audio_engine_render_drain(call->stream, QM_WAIT_FOREVER);
    return NULL;
}

static void *stop_thread(void *context)
{
    stream_call_t *call = context;

    call->result = qm_audio_engine_render_stop(call->stream);
    return NULL;
}

static void *position_thread(void *context)
{
    stream_call_t *call = context;

    call->result = qm_audio_engine_render_get_position(call->stream, &call->position_ms);
    return NULL;
}

static void test_worker_and_retry(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    uint32_t position_ms = 0U;

    assert(qm_audio_engine_render_close(stream) == -QM_EBUSY);
    assert(qm_audio_engine_render_pause(stream) == QM_EOK);
    assert(qm_audio_engine_render_resume(stream) == QM_EOK);
    assert(qm_audio_engine_set_volume(engine, 42U) == QM_EOK);
    assert(qm_audio_engine_set_mute(engine, QM_TRUE) == QM_EOK);
    assert(qm_audio_engine_render_get_position(stream, &position_ms) == QM_EOK);
    assert(position_ms == 37U);
    assert(atomic_load(&device.settings) == 2);
    atomic_store(&device.stop_error, -QM_EIO);
    assert(qm_audio_engine_render_stop(stream) == -QM_EIO);
    assert(qm_audio_engine_render_close(stream) == -QM_EBUSY);
    atomic_store(&device.stop_error, QM_EOK);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    atomic_store(&device.close_error, -QM_EIO);
    assert(qm_audio_engine_shutdown(engine, TEST_WAIT_MS) == -QM_EIO);
    assert(qm_audio_engine_destroy(engine) == -QM_EBUSY);
    atomic_store(&device.close_error, QM_EOK);
    destroy_engine(engine);
    assert(atomic_load(&device.closes) == 2);
}

static void test_blocked_write_stop(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    uint8_t bytes[TEST_PACKET_BYTES] = {1U};
    qm_audio_data_t input = {.data = bytes, .size = sizeof(bytes)};
    uint32_t written = 0U;

    atomic_store(&device.block_write, 1);
    assert(qm_audio_engine_render_write(stream, &input, TEST_WAIT_MS, &written) == QM_EOK);
    assert(written == sizeof(bytes));
    take(&device.write_entered);
    assert(qm_audio_engine_render_write(stream, &input, QM_NO_WAIT, &written) == QM_EOK);
    assert(qm_audio_engine_render_write(stream, &input, QM_NO_WAIT, &written) == QM_EOK);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(atomic_load(&device.aborts) != 0);
    assert(atomic_load(&device.writes) == 1);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_drain_stop(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    stream_call_t call = {.stream = stream, .result = -QM_ERROR};

    atomic_store(&device.block_drain, 1);
    assert(pthread_create(&call.thread, NULL, drain_thread, &call) == 0);
    take(&device.drain_entered);
    assert(qm_audio_engine_render_setting_request(engine, QM_AUDIO_RENDER_SETTING_VOLUME, 50U) == QM_EOK);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(pthread_join(call.thread, NULL) == 0);
    assert(call.result == -QM_EINTR);
    assert(qm_audio_engine_render_setting_wait(engine, QM_AUDIO_RENDER_SETTING_VOLUME, 50U) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_start_stop_race(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_render_config_t config = {.delivery = QM_AUDIO_DELIVERY_RELIABLE};

    atomic_store(&device.block_start, 1);
    assert(qm_audio_engine_render_open_request(engine, NULL, &config, &stream) == QM_EOK);
    take(&device.start_entered);
    assert(qm_audio_engine_render_request(stream, QM_AUDIO_RENDER_ACTION_STOP) == QM_EOK);
    take(&device.abort_entered);
    assert(qm_audio_engine_render_close(stream) == -QM_EBUSY);
    give(&device.start_release);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(atomic_load(&device.writes) == 0);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_stop_timeout_late_completion(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);

    atomic_store(&device.block_stop, 1);
    assert(qm_audio_engine_render_stop(stream) == -QM_ETIMEOUT);
    take(&device.stop_entered);
    assert(qm_audio_engine_render_close(stream) == -QM_EBUSY);
    assert(qm_audio_engine_destroy(engine) == -QM_EBUSY);
    give(&device.stop_release);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(atomic_load(&device.stops) == 1);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_failed_start_handle(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_render_config_t config = {.delivery = QM_AUDIO_DELIVERY_RELIABLE};

    atomic_store(&device.start_error, -QM_EIO);
    atomic_store(&device.block_stop, 1);
    assert(qm_audio_engine_render_open(engine, &config, &stream) == -QM_EIO);
    assert(stream != NULL);
    take(&device.stop_entered);
    assert(qm_audio_engine_render_close(stream) == -QM_EBUSY);
    atomic_store(&device.block_stop, 0);
    give(&device.stop_release);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_open_timeout_late_start(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_render_config_t config = {.delivery = QM_AUDIO_DELIVERY_RELIABLE};

    atomic_store(&device.block_start, 1);
    assert(qm_audio_engine_render_open(engine, &config, &stream) == -QM_ETIMEOUT);
    assert(stream != NULL);
    take(&device.start_entered);
    assert(qm_audio_engine_render_close(stream) == -QM_EBUSY);
    assert(qm_audio_engine_render_request(stream, QM_AUDIO_RENDER_ACTION_STOP) == QM_EOK);
    take(&device.abort_entered);
    give(&device.start_release);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_position_timeout_output_lifetime(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    uint32_t position_ms = 0U;

    atomic_store(&device.block_position, 1);
    assert(qm_audio_engine_render_get_position(stream, &position_ms) == -QM_ETIMEOUT);
    take(&device.position_entered);
    position_ms = 999U;
    atomic_store(&device.block_position, 0);
    give(&device.position_release);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(position_ms == 999U);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_partial_eos(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    uint8_t bytes[TEST_PCM_BYTES] = {9U};
    qm_audio_data_t input = {
        .data = bytes, .size = sizeof(bytes),
        .flags = QM_AUDIO_DATA_END_OF_STREAM | QM_AUDIO_DATA_DISCONTINUITY,
    };
    uint32_t written = 0U;
    record_t record = {0};

    input.timestamp_ms = 1U;
    assert(qm_audio_engine_render_write(stream, &input, TEST_WAIT_MS, &written) == -QM_EINVAL);
    assert(written == 0U);
    input.timestamp_ms = 0U;
    assert(qm_audio_engine_render_write(stream, &input, TEST_WAIT_MS, &written) == QM_EOK);
    assert(written == CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES);
    input.data = bytes + written;
    input.size -= written;
    input.flags = QM_AUDIO_DATA_END_OF_STREAM;
    assert(qm_audio_engine_render_write(stream, &input, TEST_WAIT_MS, &written) == QM_EOK);
    assert(written == 32U);
    assert(qm_audio_engine_render_write(stream, &input, TEST_WAIT_MS, &written) != QM_EOK);
    assert(written == 0U);
    assert(qm_audio_engine_render_drain(stream, TEST_WAIT_MS) == QM_EOK);
    read_record(&record);
    assert(record.input.size == CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES);
    assert(record.input.flags == QM_AUDIO_DATA_DISCONTINUITY);
    read_record(&record);
    assert(record.input.size == 32U);
    assert(record.input.flags == QM_AUDIO_DATA_NONE);
    assert(qm_audio_engine_render_close(stream) == -QM_EBUSY);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_packet_copy_and_backpressure(void)
{
    qm_audio_engine_t *engine = create_engine(QM_TRUE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    uint8_t bytes[TEST_PACKET_BYTES] = {3U};
    qm_audio_data_t packet = {
        .data = bytes, .size = sizeof(bytes), .timestamp_ms = 123U,
        .duration_ms = 20U, .sequence = 7U,
        .flags = QM_AUDIO_DATA_TIMESTAMP_VALID | QM_AUDIO_DATA_DURATION_VALID |
                 QM_AUDIO_DATA_DISCONTINUITY,
    };
    record_t record = {0};

    packet.size = TEST_PACKET_BYTES + 1U;
    assert(qm_audio_engine_render_write_packet(stream, &packet, QM_NO_WAIT) == -QM_EINVAL);
    packet.size = TEST_PACKET_BYTES;
    packet.flags |= QM_AUDIO_DATA_END_OF_STREAM;
    assert(qm_audio_engine_render_write_packet(stream, &packet, QM_NO_WAIT) == -QM_EINVAL);
    packet.flags &= ~QM_AUDIO_DATA_END_OF_STREAM;
    atomic_store(&device.block_write, 1);
    assert(qm_audio_engine_render_write_packet(stream, &packet, TEST_WAIT_MS) == QM_EOK);
    take(&device.write_entered);
    assert(qm_audio_engine_render_write_packet(stream, &packet, QM_NO_WAIT) == QM_EOK);
    assert(qm_audio_engine_render_write_packet(stream, &packet, QM_NO_WAIT) == QM_EOK);
    assert(qm_audio_engine_render_write_packet(stream, &packet, QM_NO_WAIT) == -QM_ETIMEOUT);
    bytes[0] = 99U;
    atomic_store(&device.block_write, 0);
    give(&device.write_release);
    assert(qm_audio_engine_render_drain(stream, TEST_WAIT_MS) == QM_EOK);
    assert(atomic_load(&device.writes) == 3);
    read_record(&record);
    read_record(&record);
    assert(record.bytes[0] == 3U);
    assert(record.input.size == TEST_PACKET_BYTES);
    assert(record.input.timestamp_ms == 123U);
    assert(record.input.duration_ms == 20U);
    assert(record.input.sequence == 7U);
    assert(record.input.flags == packet.flags);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_owner(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_render_config_t config = {.delivery = QM_AUDIO_DELIVERY_RELIABLE};
    qm_audio_service_t owner = {0};
    qm_audio_service_t wrong_owner = {0};

    assert(qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER) == QM_EOK);
    engine->service_owner = &owner;
    assert(qm_mutex_unlock(&engine->lock) == QM_EOK);
    assert(qm_audio_engine_render_open(engine, &config, &stream) == -QM_EBUSY);
    assert(stream == NULL);
    assert(qm_audio_engine_render_open_owned(engine, &wrong_owner, &config, &stream) == -QM_EBUSY);
    assert(stream == NULL);
    assert(qm_audio_engine_render_open_owned(engine, &owner, &config, &stream) == QM_EOK);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    assert(qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER) == QM_EOK);
    engine->service_owner = NULL;
    assert(qm_mutex_unlock(&engine->lock) == QM_EOK);
    destroy_engine(engine);
}

static void test_retained_producer_after_close(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    uint8_t bytes[TEST_PACKET_BYTES] = {1U};
    qm_audio_data_t input = {.data = bytes, .size = sizeof(bytes)};
    uint32_t written = 0U;

    assert(qm_audio_engine_render_retain(stream) == QM_EOK);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    assert(qm_audio_engine_shutdown(engine, TEST_WAIT_MS) == QM_EOK);
    assert(qm_audio_engine_destroy(engine) == -QM_EBUSY);
    assert(qm_audio_engine_render_write(stream, &input, QM_NO_WAIT, &written) == -QM_EINTR);
    assert(written == 0U);
    qm_audio_engine_render_release(stream);
    destroy_engine(engine);
}

static void test_abort_pins_route(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    qm_audio_render_stream_t *other = NULL;
    qm_audio_render_config_t config = {.delivery = QM_AUDIO_DELIVERY_RELIABLE};
    stream_call_t call = {.stream = stream, .result = -QM_ERROR};

    atomic_store(&device.block_abort, 1);
    assert(pthread_create(&call.thread, NULL, stop_thread, &call) == 0);
    take(&device.abort_entered);
    assert(atomic_load(&device.stops) == 0);
    assert(qm_audio_engine_render_close(stream) == -QM_EBUSY);
    assert(qm_audio_engine_render_open(engine, &config, &other) == -QM_EBUSY);
    assert(other == NULL);
    give(&device.abort_release);
    assert(pthread_join(call.thread, NULL) == 0);
    assert(call.result == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    other = open_stream(engine);
    assert(qm_audio_engine_render_stop(other) == QM_EOK);
    assert(qm_audio_engine_render_close(other) == QM_EOK);
    destroy_engine(engine);
}

static void test_capture_shutdown_failure_stays_closed(void)
{
    qm_audio_engine_t *engine = create_engine_profile(QM_FALSE, QM_TRUE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    qm_audio_render_stream_t *other = NULL;
    qm_audio_render_config_t config = {.delivery = QM_AUDIO_DELIVERY_RELIABLE};
    uint8_t bytes[TEST_PACKET_BYTES] = {1U};
    qm_audio_data_t input = {.data = bytes, .size = sizeof(bytes)};
    uint32_t written = 0U;

    atomic_store(&device.capture_shutdown_error, -QM_EBUSY);
    assert(qm_audio_engine_shutdown(engine, TEST_WAIT_MS) == -QM_EBUSY);
    assert(qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(engine->state == QM_AUDIO_ENGINE_STATE_SHUTTING_DOWN);
    assert(qm_mutex_unlock(&engine->lock) == QM_EOK);
    assert(qm_audio_engine_get_capture(engine) == NULL);
    assert(qm_audio_engine_render_write(stream, &input, QM_NO_WAIT, &written) == -QM_EINTR);
    assert(written == 0U);
    assert(qm_audio_engine_destroy(engine) == -QM_EBUSY);
    assert(atomic_load(&device.closes) == 0);
    /* Release the old route so admission must be rejected by shutdown state,
     * not merely by an occupied slot. / 先释放旧路由，确认拒绝来自关闭状态。 */
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    assert(qm_audio_engine_render_open(engine, &config, &other) == -QM_EBUSY);
    assert(other == NULL);
    atomic_store(&device.capture_shutdown_error, QM_EOK);
    destroy_engine(engine);
    assert(atomic_load(&device.capture_shutdowns) == 2);
    assert(atomic_load(&device.capture_destroys) == 1);
    assert(atomic_load(&device.closes) == 1);
}

/* Observe completion without consuming the Service-facing result query.
 * / 仅观察动作完成，不提前读取面向 Service 的结果接口。 */
static void wait_control_complete(qm_audio_render_stream_t *stream)
{
    qm_bool pending = QM_TRUE;

    while (pending) {
        assert(qm_mutex_lock(&stream->engine->lock, QM_WAIT_FOREVER) == QM_EOK);
        pending = stream->action_pending;
        assert(qm_mutex_unlock(&stream->engine->lock) == QM_EOK);
        if (pending) {
            take(&stream->engine->render_action_sem);
        }
    }
}

static void service_event(const qm_audio_event_t *event, void *context)
{
    service_events_t *events = context;

    assert(event != NULL && events != NULL);
    assert(event->type > QM_AUDIO_EVENT_NONE && event->type <= QM_AUDIO_EVENT_SEEKED);
    atomic_fetch_add(&events->counts[event->type], 1U);
}

/* No event task is installed: advance the same action only after its real
 * Engine acknowledgement. / 不创建事件任务，根据真实 Engine 确认推进动作。 */
static int complete_service_action(qm_audio_session_t *session,
                                   qm_audio_service_action_t action)
{
    qm_audio_render_stream_t *stream = NULL;
    int result = -QM_ERROR;
    int status = QM_EOK;

    for (;;) {
        status = qm_audio_service_session_result(session, action, &result);
        if (status != -QM_EBUSY) {
            return status == QM_EOK ? result : status;
        }
        assert(qm_mutex_lock(&session->service->lock, QM_WAIT_FOREVER) == QM_EOK);
        stream = session->stream;
        assert(stream != NULL);
        assert(qm_mutex_unlock(&session->service->lock) == QM_EOK);
        wait_control_complete(stream);
    }
}

static qm_audio_session_t *create_service_session(qm_audio_engine_t *engine,
                                                 qm_audio_service_t **service,
                                                 service_events_t *events,
                                                 qm_bool asynchronous)
{
    qm_audio_service_config_t config = {.engine = engine, .max_sessions = 2U};
    qm_audio_session_request_t request = {
        .request_id = 1U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_REJECT_IF_BUSY,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
    };
    qm_audio_session_t *session = NULL;

    assert(qm_audio_service_create(&config, service) == QM_EOK);
    if (asynchronous) {
        assert(qm_audio_service_session_acquire_async(*service, &request,
                                                     service_event, events, &session) == QM_EOK);
        (void)qm_audio_service_tick(*service);
        assert(complete_service_action(session, QM_AUDIO_SERVICE_ACTION_START) == QM_EOK);
    } else {
        assert(qm_audio_service_session_acquire(*service, &request,
                                               service_event, events, &session) == QM_EOK);
    }
    assert(session != NULL);
    assert(atomic_load(&events->counts[QM_AUDIO_EVENT_ACCEPTED]) == 1U);
    assert(atomic_load(&events->counts[QM_AUDIO_EVENT_STARTED]) == 1U);
    return session;
}

static int service_action(qm_audio_session_t *session,
                           qm_audio_service_action_t action, uint32_t value,
                           qm_bool asynchronous)
{
    int ret = QM_EOK;

    if (asynchronous) {
        ret = qm_audio_service_session_request(session, action, value);
        return ret == QM_EOK ? complete_service_action(session, action) : ret;
    }
    switch (action) {
        case QM_AUDIO_SERVICE_ACTION_PAUSE:
            return qm_audio_service_session_pause(session);
        case QM_AUDIO_SERVICE_ACTION_RESUME:
            return qm_audio_service_session_resume(session);
        case QM_AUDIO_SERVICE_ACTION_SUSPEND:
            return qm_audio_service_session_suspend(session);
        case QM_AUDIO_SERVICE_ACTION_REPOSITION:
            return qm_audio_service_session_reposition(session, value);
        default:
            assert(0);
            return -QM_EINVAL;
    }
}

static void assert_service_state(qm_audio_session_t *session,
                                 qm_audio_session_state_t expected,
                                 qm_bool owns_route, qm_bool has_stream)
{
    qm_audio_session_state_t state = QM_AUDIO_SESSION_NEW;

    assert(qm_audio_service_session_get_state(session, &state) == QM_EOK);
    assert(state == expected);
    assert(qm_mutex_lock(&session->service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert((session->service->active == session) == owns_route);
    assert((session->stream != NULL) == has_stream);
    assert(session->internal_refs == 0U);
    assert(!session->action_pending);
    assert(qm_mutex_unlock(&session->service->lock) == QM_EOK);
}

static void test_service_action_contract(qm_bool asynchronous)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    service_events_t events = {0};
    qm_audio_session_t *session = create_service_session(engine, &service, &events, asynchronous);
    uint32_t generation = session->generation;
    uint32_t position = 0U;

    assert(service_action(session, QM_AUDIO_SERVICE_ACTION_PAUSE, 0U, asynchronous) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_PAUSED, QM_TRUE, QM_TRUE);
    assert(session->generation == generation);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_PAUSED]) == 1U);
    assert(service_action(session, QM_AUDIO_SERVICE_ACTION_RESUME, 0U, asynchronous) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_ACTIVE, QM_TRUE, QM_TRUE);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_RESUMED]) == 1U);
    assert(service_action(session, QM_AUDIO_SERVICE_ACTION_SUSPEND, 0U, asynchronous) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_PAUSED, QM_FALSE, QM_FALSE);
    assert(session->generation == generation);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_PAUSED]) == 2U);
    assert(service_action(session, QM_AUDIO_SERVICE_ACTION_SUSPEND, 0U, asynchronous) == QM_EOK);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_PAUSED]) == 2U);
    assert(service_action(session, QM_AUDIO_SERVICE_ACTION_REPOSITION, 120U, asynchronous) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_PAUSED, QM_FALSE, QM_FALSE);
    assert(session->generation == generation + 1U);
    assert(qm_audio_service_session_get_position(session, &position) == -QM_ENOSYS);
    assert(position == 120U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_PAUSED]) == 2U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_SEEKED]) == 0U);
    assert(service_action(session, QM_AUDIO_SERVICE_ACTION_RESUME, 0U, asynchronous) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_ACTIVE, QM_TRUE, QM_TRUE);
    assert(session->generation == generation + 1U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_RESUMED]) == 2U);

    assert(service_action(session, QM_AUDIO_SERVICE_ACTION_PAUSE, 0U, asynchronous) == QM_EOK);
    atomic_store(&device.resume_error, -QM_EIO);
    assert(service_action(session, QM_AUDIO_SERVICE_ACTION_RESUME, 0U, asynchronous) == -QM_EIO);
    assert_service_state(session, QM_AUDIO_SESSION_ERROR, QM_TRUE, QM_TRUE);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_ERROR]) == 1U);
    atomic_store(&device.stop_error, -QM_ENOSYS);
    assert(qm_audio_service_session_release(session) == -QM_ENOSYS);
    assert_service_state(session, QM_AUDIO_SESSION_ERROR, QM_TRUE, QM_TRUE);
    assert(!session->release_pending);
    assert(session->result == -QM_EIO);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_ERROR]) == 1U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 0U);
    atomic_store(&device.stop_error, QM_EOK);
    assert(qm_audio_service_session_release(session) == QM_EOK);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_ERROR]) == 1U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 0U);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_stop_supersedes(qm_bool starting)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    qm_audio_service_config_t config = {.engine = engine, .max_sessions = 2U};
    qm_audio_session_request_t request = {
        .request_id = 1U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_REJECT_IF_BUSY,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
    };
    service_events_t events = {0};
    qm_audio_session_t *session = NULL;
    qm_audio_service_action_t interrupted = starting ? QM_AUDIO_SERVICE_ACTION_START :
                                                     QM_AUDIO_SERVICE_ACTION_PAUSE;
    int result = -QM_ERROR;

    if (starting) {
        assert(qm_audio_service_create(&config, &service) == QM_EOK);
        atomic_store(&device.block_start, 1);
        assert(qm_audio_service_session_acquire_async(service, &request,
                                                     service_event, &events, &session) == QM_EOK);
        (void)qm_audio_service_tick(service);
    } else {
        session = create_service_session(engine, &service, &events, QM_FALSE);
        atomic_store(&device.block_pause, 1);
        assert(qm_audio_service_session_request(session, interrupted, 0U) == QM_EOK);
    }
    assert(qm_audio_service_session_result(session, interrupted, &result) == -QM_EBUSY);
    take(starting ? &device.start_entered : &device.pause_entered);
    assert(qm_audio_service_session_request(session, QM_AUDIO_SERVICE_ACTION_STOP, 17U) == QM_EOK);
    assert(qm_audio_service_session_result(session, interrupted, &result) == -QM_EINTR);
    assert(qm_audio_service_session_result(session, QM_AUDIO_SERVICE_ACTION_STOP, &result) == -QM_EBUSY);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(session->internal_refs == 1U);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);
    give(starting ? &device.start_release : &device.pause_release);
    assert(complete_service_action(session, QM_AUDIO_SERVICE_ACTION_STOP) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    assert(atomic_load(&device.stops) == 1);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 1U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_PAUSED]) == 0U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_STARTED]) == (starting ? 0U : 1U));
    assert(qm_audio_service_session_release(session) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_stop_timeout_retry(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    service_events_t events = {0};
    qm_audio_session_t *session = create_service_session(engine, &service, &events, QM_FALSE);

    atomic_store(&device.block_stop, 1);
    assert(qm_audio_service_session_stop(session, 23U) == -QM_ETIMEOUT);
    take(&device.stop_entered);
    assert(qm_audio_service_session_stop(session, 23U) == -QM_ETIMEOUT);
    assert(atomic_load(&device.stops) == 1);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(session->action_pending && session->internal_refs == 1U);
    assert(session->stream != NULL && service->active == session);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 0U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_ERROR]) == 0U);
    give(&device.stop_release);
    assert(qm_audio_service_session_stop(session, 23U) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    assert(qm_audio_service_session_stop(session, 23U) == QM_EOK);
    assert(atomic_load(&device.stops) == 1);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 1U);
    assert(qm_audio_service_session_release(session) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void *service_finish_thread(void *context)
{
    service_call_t *call = context;

    call->result = qm_audio_service_session_finish(call->session, QM_WAIT_FOREVER);
    return NULL;
}

static void *service_stop_thread(void *context)
{
    service_call_t *call = context;

    call->result = qm_audio_service_session_stop(call->session, 0U);
    return NULL;
}

static void *service_release_thread(void *context)
{
    service_call_t *call = context;

    call->result = qm_audio_service_session_release(call->session);
    return NULL;
}

static void test_service_shutdown_during_release(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    service_events_t events = {0};
    qm_audio_session_t *session = create_service_session(engine, &service, &events, QM_FALSE);
    service_call_t release = {.session = session, .result = -QM_ERROR};

    assert(qm_audio_service_session_pause(session) == QM_EOK);
    atomic_store(&device.resume_error, -QM_EIO);
    assert(qm_audio_service_session_resume(session) == -QM_EIO);
    assert_service_state(session, QM_AUDIO_SESSION_ERROR, QM_TRUE, QM_TRUE);
    atomic_store(&device.block_abort, 1);
    assert(pthread_create(&release.thread, NULL, service_release_thread, &release) == 0);
    take(&device.abort_entered);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(session->release_pending && session->action_submitting && session->action_pending);
    assert(session->stream != NULL && service->active == session);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == -QM_EBUSY);
    assert(atomic_load(&device.stops) == 0);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(service->shutting_down && session->release_pending && session->stream != NULL);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);
    give(&device.abort_release);
    assert(pthread_join(release.thread, NULL) == 0);
    assert(release.result == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void preempt_during_error_notice(const qm_audio_event_t *event, void *context)
{
    preempt_notice_t *test = context;
    qm_audio_session_request_t request = {
        .request_id = 3U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_PREEMPT_CURRENT,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
        .acquire_timeout_ms = QM_WAIT_FOREVER,
    };
    qm_audio_session_t *victim = NULL;

    if (event->type != QM_AUDIO_EVENT_ERROR || test->injected) {
        return;
    }
    test->injected = QM_TRUE;
    atomic_store(&device.stop_error, QM_EOK);
    assert(qm_audio_service_session_acquire_async(test->service, &request,
                                                 service_event, &test->new_events,
                                                 &test->new_session) == QM_EOK);
    assert(qm_mutex_lock(&test->service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(test->new_session->state == QM_AUDIO_SESSION_QUEUED);
    victim = test->new_session->preempting;
    assert(victim != NULL && victim->action_pending);
    assert(qm_mutex_unlock(&test->service->lock) == QM_EOK);
    if (test->complete_preempt) {
        assert(complete_service_action(victim, QM_AUDIO_SERVICE_ACTION_PREEMPT) == QM_EOK);
    }
}

static void test_service_preempt_notice_dependency(qm_bool complete_preempt)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    qm_audio_service_config_t config = {.engine = engine, .max_sessions = 3U};
    qm_audio_session_request_t request = {
        .request_id = 1U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_REJECT_IF_BUSY,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
        .acquire_timeout_ms = QM_WAIT_FOREVER,
    };
    qm_audio_session_t *victim = NULL;
    qm_audio_session_t *first = NULL;
    service_events_t first_events = {0};
    preempt_notice_t test = {.complete_preempt = complete_preempt};

    assert(qm_audio_service_create(&config, &service) == QM_EOK);
    test.service = service;
    assert(qm_audio_service_session_acquire(service, &request,
                                            preempt_during_error_notice, &test, &victim) == QM_EOK);
    atomic_store(&device.stop_error, -QM_EIO);
    request.request_id = 2U;
    request.policy = QM_AUDIO_ADMISSION_PREEMPT_CURRENT;
    assert(qm_audio_service_session_acquire_async(service, &request,
                                                 service_event, &first_events, &first) == QM_EOK);
    assert(complete_service_action(victim, QM_AUDIO_SERVICE_ACTION_PREEMPT) == -QM_EIO);
    assert(test.injected);
    assert_service_state(first, QM_AUDIO_SESSION_ERROR, QM_FALSE, QM_FALSE);
    assert(first->preempting == NULL && first->result == -QM_EIO);
    assert(atomic_load(&first_events.counts[QM_AUDIO_EVENT_ERROR]) == 1U);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(test.new_session->state == QM_AUDIO_SESSION_QUEUED);
    assert(test.new_session->result == QM_EOK);
    assert((test.new_session->preempting != NULL) == !complete_preempt);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);
    assert(atomic_load(&test.new_events.counts[QM_AUDIO_EVENT_ERROR]) == 0U);
    if (!complete_preempt) {
        assert(complete_service_action(victim, QM_AUDIO_SERVICE_ACTION_PREEMPT) == QM_EOK);
    }
    assert(test.new_session->preempting == NULL);
    assert(qm_audio_service_session_wait(test.new_session, TEST_WAIT_MS) == QM_EOK);
    assert_service_state(test.new_session, QM_AUDIO_SESSION_ACTIVE, QM_TRUE, QM_TRUE);
    assert(atomic_load(&test.new_events.counts[QM_AUDIO_EVENT_STARTED]) == 1U);
    assert(atomic_load(&device.stops) == 2);
    assert(qm_audio_service_session_release(first) == QM_EOK);
    assert(qm_audio_service_session_release(victim) == QM_EOK);
    assert(qm_audio_service_session_stop(test.new_session, 0U) == QM_EOK);
    assert(qm_audio_service_session_release(test.new_session) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_slow_abort_does_not_lock_service(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    service_events_t events = {0};
    service_events_t queued_events = {0};
    qm_audio_session_t *session = create_service_session(engine, &service, &events, QM_FALSE);
    qm_audio_session_t *queued = NULL;
    service_call_t stop = {.session = session, .result = -QM_ERROR};
    qm_audio_session_state_t state = QM_AUDIO_SESSION_NEW;
    qm_audio_session_request_t request = {
        .request_id = 2U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_QUEUE,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
        .acquire_timeout_ms = QM_WAIT_FOREVER,
    };
    int result = -QM_ERROR;

    assert(qm_audio_service_session_acquire_async(service, &request,
                                                 service_event, &queued_events, &queued) == QM_EOK);
    atomic_store(&device.block_abort, 1);
    assert(pthread_create(&stop.thread, NULL, service_stop_thread, &stop) == 0);
    take(&device.abort_entered);
    /* The backend cancellation gate remains closed throughout these calls.
     * / 以下调用均在底层取消门闩保持关闭时完成。 */
    assert(qm_audio_service_session_get_state(session, &state) == QM_EOK);
    assert(state == QM_AUDIO_SESSION_ACTIVE);
    assert(qm_audio_service_session_stop(queued, 0U) == QM_EOK);
    assert_service_state(queued, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    assert(qm_audio_service_session_request(session, QM_AUDIO_SERVICE_ACTION_STOP, 0U) == QM_EOK);
    assert(qm_audio_service_session_result(session, QM_AUDIO_SERVICE_ACTION_STOP, &result) == -QM_EBUSY);
    assert(atomic_load(&device.aborts) == 1);
    assert(atomic_load(&device.stops) == 0);
    assert(atomic_load(&queued_events.counts[QM_AUDIO_EVENT_CANCELLED]) == 1U);
    assert(atomic_load(&queued_events.counts[QM_AUDIO_EVENT_STARTED]) == 0U);
    give(&device.abort_release);
    assert(pthread_join(stop.thread, NULL) == 0);
    assert(stop.result == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    /* Engine reasserts abort once on its worker immediately before STOP.
     * / Engine 在 worker 执行 STOP 前会再确认一次 abort。 */
    assert(atomic_load(&device.aborts) == 2);
    assert(atomic_load(&device.stops) == 1);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 1U);
    assert(qm_audio_service_session_release(queued) == QM_EOK);
    assert(qm_audio_service_session_release(session) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_session_progress_handoff(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    service_events_t first_events = {0};
    service_events_t next_events = {0};
    qm_audio_session_t *first = create_service_session(engine, &service, &first_events, QM_FALSE);
    qm_audio_session_t *next = NULL;
    service_call_t stop = {.session = first, .result = -QM_ERROR};
    qm_audio_session_request_t request = {
        .request_id = 2U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_QUEUE,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
        .acquire_timeout_ms = QM_WAIT_FOREVER,
    };
    int ret = QM_EOK;

    take(&device.start_entered);
    assert(qm_audio_service_session_acquire_async(service, &request,
                                                 service_event, &next_events, &next) == QM_EOK);
    atomic_store(&device.block_start, 1);
    assert(pthread_create(&stop.thread, NULL, service_stop_thread, &stop) == 0);
    take(&device.start_entered);
    assert_service_state(first, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(service->active == next && next->action_pending);
    assert(next->action_waiters == 1U);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);

    /* While the handoff caller waits for the new START, an independent old
     * session cleanup must neither steal nor receive its progress signal.
     * / 路由切换调用者等待新 START 时，旧会话清理不得消费或接收新会话进度。 */
    assert(qm_audio_service_session_stop(first, 0U) == QM_EOK);
    do {
        ret = qm_sem_wait(&first->action_sem, QM_NO_WAIT);
    } while (ret == QM_EOK);
    assert(ret == -QM_ETIMEOUT);
    give(&device.start_release);
    assert(pthread_join(stop.thread, NULL) == 0);
    assert(stop.result == QM_EOK);
    assert_service_state(next, QM_AUDIO_SESSION_ACTIVE, QM_TRUE, QM_TRUE);
    assert(qm_sem_wait(&first->action_sem, QM_NO_WAIT) == -QM_ETIMEOUT);
    assert(atomic_load(&first_events.counts[QM_AUDIO_EVENT_CANCELLED]) == 1U);
    assert(atomic_load(&next_events.counts[QM_AUDIO_EVENT_STARTED]) == 1U);
    assert(atomic_load(&device.stops) == 1);
    assert(qm_audio_service_session_stop(next, 0U) == QM_EOK);
    assert(qm_audio_service_session_release(first) == QM_EOK);
    assert(qm_audio_service_session_release(next) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_acquire_timeout_wait(qm_bool preempting)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    qm_audio_service_config_t config = {.engine = engine, .max_sessions = 2U};
    service_events_t victim_events = {0};
    service_events_t events = {0};
    qm_audio_session_t *victim = NULL;
    qm_audio_session_t *session = NULL;
    qm_audio_session_t *pending = NULL;
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_session_request_t request = {
        .request_id = 2U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_PREEMPT_CURRENT,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
        .acquire_timeout_ms = QM_WAIT_FOREVER,
    };

    if (preempting) {
        victim = create_service_session(engine, &service, &victim_events, QM_FALSE);
        atomic_store(&device.block_stop, 1);
    } else {
        assert(qm_audio_service_create(&config, &service) == QM_EOK);
        atomic_store(&device.block_start, 1);
    }
    assert(qm_audio_service_session_acquire(service, &request,
                                           service_event, &events, &session) == QM_EOK);
    take(preempting ? &device.stop_entered : &device.start_entered);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_ACCEPTED]) == 1U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_STARTED]) == 0U);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(session->state == QM_AUDIO_SESSION_QUEUED);
    assert(session->preempting == victim);
    pending = preempting ? victim : session;
    assert(pending->action_pending && pending->internal_refs == 1U);
    stream = pending->stream;
    assert(stream != NULL);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);
    atomic_store(preempting ? &device.block_stop : &device.block_start, 0);
    give(preempting ? &device.stop_release : &device.start_release);
    wait_control_complete(stream);
    /* Admission remains locally driveable after acquire's wait budget expires.
     * / acquire 内部等待超时后，调用者无需事件任务即可继续完成准入。 */
    assert(qm_audio_service_session_wait(session, TEST_WAIT_MS) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_ACTIVE, QM_TRUE, QM_TRUE);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_STARTED]) == 1U);
    if (preempting) {
        assert_service_state(victim, QM_AUDIO_SESSION_PREEMPTED, QM_FALSE, QM_FALSE);
        assert(atomic_load(&victim_events.counts[QM_AUDIO_EVENT_PREEMPTED]) == 1U);
        assert(atomic_load(&device.stops) == 1);
        assert(qm_audio_service_session_release(victim) == QM_EOK);
    }
    assert(qm_audio_service_session_stop(session, 0U) == QM_EOK);
    assert(qm_audio_service_session_release(session) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_preempt_admission_timeout(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    service_events_t victim_events = {0};
    service_events_t events = {0};
    qm_audio_session_t *victim = create_service_session(engine, &service, &victim_events, QM_FALSE);
    qm_audio_session_t *session = NULL;
    qm_audio_session_request_t request = {
        .request_id = 2U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_PREEMPT_CURRENT,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
        .acquire_timeout_ms = CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS,
    };
    uint32_t generation = 0U;
    int result = -QM_ERROR;

    atomic_store(&device.block_stop, 1);
    assert(qm_audio_service_session_acquire_async(service, &request,
                                                 service_event, &events, &session) == QM_EOK);
    generation = session->generation;
    assert(qm_audio_service_session_result(victim, QM_AUDIO_SERVICE_ACTION_PREEMPT, &result) == -QM_EBUSY);
    take(&device.stop_entered);
    assert(qm_audio_service_session_wait(session, TEST_WAIT_MS) == -QM_ETIMEOUT);
    assert_service_state(session, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    assert(session->result == -QM_ETIMEOUT && session->generation == generation);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 1U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_STARTED]) == 0U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_ERROR]) == 0U);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(victim->action_pending && victim->internal_refs == 1U);
    assert(session->preempting == victim && service->active == victim);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);
    atomic_store(&device.block_stop, 0);
    give(&device.stop_release);
    assert(complete_service_action(victim, QM_AUDIO_SERVICE_ACTION_PREEMPT) == QM_EOK);
    assert_service_state(victim, QM_AUDIO_SESSION_PREEMPTED, QM_FALSE, QM_FALSE);
    assert_service_state(session, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    assert(session->preempting == NULL);
    assert(session->result == -QM_ETIMEOUT && session->generation == generation);
    assert(qm_audio_service_session_wait(session, QM_NO_WAIT) == -QM_ETIMEOUT);
    assert(qm_audio_service_session_stop(session, 0U) == QM_EOK);
    assert(session->result == -QM_ETIMEOUT && session->generation == generation);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 1U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_STARTED]) == 0U);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_ERROR]) == 0U);
    assert(atomic_load(&victim_events.counts[QM_AUDIO_EVENT_PREEMPTED]) == 1U);
    assert(atomic_load(&device.stops) == 1);
    assert(qm_audio_service_session_release(session) == QM_EOK);
    assert(qm_audio_service_session_release(victim) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_finish_and_shutdown(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_service_t *service = NULL;
    service_events_t events = {0};
    qm_audio_session_t *session = create_service_session(engine, &service, &events, QM_FALSE);
    service_call_t finish = {.session = session, .result = -QM_ERROR};
    qm_audio_session_state_t state = QM_AUDIO_SESSION_NEW;
    qm_audio_session_t *queued = NULL;
    qm_audio_session_request_t request = {
        .request_id = 2U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_QUEUE,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
        .acquire_timeout_ms = QM_WAIT_FOREVER,
    };

    atomic_store(&device.block_drain, 1);
    assert(pthread_create(&finish.thread, NULL, service_finish_thread, &finish) == 0);
    take(&device.drain_entered);
    assert(qm_audio_service_session_get_state(session, &state) == QM_EOK);
    assert(state == QM_AUDIO_SESSION_DRAINING);
    assert(atomic_load(&device.stops) == 0);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_FINISHED]) == 0U);
    give(&device.drain_release);
    assert(pthread_join(finish.thread, NULL) == 0);
    assert(finish.result == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_FINISHED, QM_FALSE, QM_FALSE);
    assert(atomic_load(&device.drains) == 1);
    assert(atomic_load(&device.stops) == 1);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_FINISHED]) == 1U);
    assert(qm_audio_service_session_stop(session, 0U) == QM_EOK);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 0U);
    assert(qm_audio_service_session_release(session) == QM_EOK);

    assert(qm_audio_service_session_acquire(service, &request,
                                           service_event, &events, &session) == QM_EOK);
    request.request_id++;
    assert(qm_audio_service_session_acquire_async(service, &request,
                                                 service_event, &events, &queued) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert_service_state(session, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    assert_service_state(queued, QM_AUDIO_SESSION_CANCELLED, QM_FALSE, QM_FALSE);
    assert(atomic_load(&events.counts[QM_AUDIO_EVENT_CANCELLED]) == 2U);
    assert(atomic_load(&device.stops) == 2);
    assert(qm_audio_service_session_release(session) == QM_EOK);
    assert(qm_audio_service_session_release(queued) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_position_preserves_control_result(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_render_config_t config = {.delivery = QM_AUDIO_DELIVERY_RELIABLE};
    uint32_t position_ms = 0U;
    int result = -QM_ERROR;

    assert(qm_audio_engine_render_open_request(engine, NULL, &config, &stream) == QM_EOK);
    wait_control_complete(stream);
    assert(qm_audio_engine_render_get_position(stream, &position_ms) == QM_EOK);
    assert(position_ms == 37U);
    assert(qm_audio_engine_render_result(stream, QM_AUDIO_RENDER_ACTION_START, &result) == QM_EOK);
    assert(result == QM_EOK);
    assert(qm_audio_engine_render_request(stream, QM_AUDIO_RENDER_ACTION_PAUSE) == QM_EOK);
    wait_control_complete(stream);
    assert(qm_audio_engine_render_get_position(stream, &position_ms) == QM_EOK);
    assert(qm_audio_engine_render_result(stream, QM_AUDIO_RENDER_ACTION_PAUSE, &result) == QM_EOK);
    assert(result == QM_EOK);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

static void test_service_failed_start_cleanup(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    qm_audio_service_t *service = NULL;
    qm_audio_service_config_t config = {.engine = engine, .max_sessions = 2U};
    qm_audio_session_t *session = NULL;
    qm_audio_session_t *other = NULL;
    qm_audio_session_state_t state = QM_AUDIO_SESSION_NEW;
    qm_audio_session_request_t request = {
        .request_id = 1U, .priority = QM_AUDIO_PRIORITY_NORMAL,
        .policy = QM_AUDIO_ADMISSION_REJECT_IF_BUSY,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
    };

    assert(qm_audio_service_create(&config, &service) == -QM_EBUSY);
    assert(service == NULL);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    assert(qm_audio_service_create(&config, &service) == QM_EOK);
    /* Discard the previous route's notification before gating this STOP.
     * / 清除上一条路由的通知，使 gate 对应本次失败 START 的清理。 */
    take(&device.stop_entered);
    atomic_store(&device.start_error, -QM_EIO);
    atomic_store(&device.block_stop, 1);
    assert(qm_audio_service_session_acquire(service, &request, NULL, NULL, &session) == QM_EOK);
    assert(session != NULL);
    assert(qm_audio_service_session_get_state(session, &state) == QM_EOK);
    assert(state == QM_AUDIO_SESSION_ERROR);
    assert(qm_audio_service_session_wait(session, QM_NO_WAIT) == -QM_EIO);
    assert(qm_audio_service_session_release(session) == -QM_ETIMEOUT);
    take(&device.stop_entered);
    assert(qm_mutex_lock(&service->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(!session->release_pending);
    assert(session->action_pending && session->internal_refs == 1U);
    assert(qm_mutex_unlock(&service->lock) == QM_EOK);
    assert(qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(engine->render != NULL);
    assert(qm_mutex_unlock(&engine->lock) == QM_EOK);
    request.request_id = 2U;
    assert(qm_audio_service_session_acquire(service, &request, NULL, NULL, &other) == -QM_EBUSY);
    assert(other == NULL);
    atomic_store(&device.start_error, QM_EOK);
    atomic_store(&device.block_stop, 0);
    give(&device.stop_release);
    assert(qm_audio_service_session_release(session) == QM_EOK);
    assert(qm_audio_service_session_acquire(service, &request, NULL, NULL, &other) == QM_EOK);
    assert(qm_audio_service_session_get_state(other, &state) == QM_EOK);
    assert(state == QM_AUDIO_SESSION_ACTIVE);
    assert(qm_audio_service_session_stop(other, 0U) == QM_EOK);
    assert(qm_audio_service_session_release(other) == QM_EOK);
    assert(qm_audio_service_shutdown(service) == QM_EOK);
    assert(qm_audio_service_destroy(service) == QM_EOK);
    destroy_engine(engine);
}

static void test_position_then_drain(void)
{
    qm_audio_engine_t *engine = create_engine(QM_FALSE);
    qm_audio_render_stream_t *stream = open_stream(engine);
    stream_call_t query = {.stream = stream, .result = -QM_ERROR};
    stream_call_t drain = {.stream = stream, .result = -QM_ERROR};
    qm_utils_time_t deadline = {0};
    qm_bool admitted = QM_FALSE;
    uint32_t other_position = 0U;

    atomic_store(&device.block_position, 1);
    assert(pthread_create(&query.thread, NULL, position_thread, &query) == 0);
    take(&device.position_entered);
    assert(qm_audio_engine_render_get_position(stream, &other_position) == -QM_EBUSY);
    assert(pthread_create(&drain.thread, NULL, drain_thread, &drain) == 0);
    /* Hold the adapter gate until drain is actually admitted. Yielding only
     * lets that thread acquire the mutex; elapsed time never implies readiness.
     * / 根据真实准入状态放行，yield 仅让其他任务取锁，不以延时时长猜测就绪。 */
    qm_utils_time_countdown_ms(&deadline, TEST_WAIT_MS);
    while (!admitted) {
        assert(qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER) == QM_EOK);
        admitted = stream->drain_active;
        assert(qm_mutex_unlock(&engine->lock) == QM_EOK);
        assert(!qm_utils_time_is_expired(&deadline));
        sched_yield();
    }
    atomic_store(&device.block_position, 0);
    give(&device.position_release);
    assert(pthread_join(query.thread, NULL) == 0);
    assert(pthread_join(drain.thread, NULL) == 0);
    assert(query.result == QM_EOK);
    assert(query.position_ms == 37U);
    assert(drain.result == QM_EOK);
    assert(qm_audio_engine_render_stop(stream) == QM_EOK);
    assert(qm_audio_engine_render_close(stream) == QM_EOK);
    destroy_engine(engine);
}

int main(void)
{
    test_worker_and_retry();
    test_blocked_write_stop();
    test_drain_stop();
    test_start_stop_race();
    test_stop_timeout_late_completion();
    test_failed_start_handle();
    test_open_timeout_late_start();
    test_position_timeout_output_lifetime();
    test_partial_eos();
    test_packet_copy_and_backpressure();
    test_service_owner();
    test_retained_producer_after_close();
    test_abort_pins_route();
    test_capture_shutdown_failure_stays_closed();
    test_position_preserves_control_result();
    test_service_action_contract(QM_FALSE);
    test_service_action_contract(QM_TRUE);
    test_service_stop_supersedes(QM_TRUE);
    test_service_stop_supersedes(QM_FALSE);
    test_service_stop_timeout_retry();
    test_service_shutdown_during_release();
    test_service_preempt_notice_dependency(QM_FALSE);
    test_service_preempt_notice_dependency(QM_TRUE);
    test_service_slow_abort_does_not_lock_service();
    test_service_session_progress_handoff();
    test_service_acquire_timeout_wait(QM_FALSE);
    test_service_acquire_timeout_wait(QM_TRUE);
    test_service_preempt_admission_timeout();
    test_service_finish_and_shutdown();
    test_service_failed_start_cleanup();
    test_position_then_drain();
    puts("engine ownership and cleanup regressions passed");
    return 0;
}
