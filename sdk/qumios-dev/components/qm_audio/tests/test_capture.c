/** @file test_capture.c @brief Capture ownership and delivery regressions. / Capture 所有权与交付回归。 */
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "qm.h"

static int test_mutex_unlock(qm_mutex_t *mutex, const char *function);

/* Include the implementation to inject queue/source faults and observe entry
 * into blocking operations. Assertions exercise the public API.
 * / 包含实现用于队列/源故障注入和观察阻塞操作入口；行为断言使用公开接口。 */
#define qm_mutex_unlock(mutex) test_mutex_unlock(mutex, __func__)
#include "../src/capture/qm_audio_capture.c"
#undef qm_mutex_unlock

#define TEST_WAIT_MS 2000U
#define TEST_FRAME_BYTES 16U
#define TEST_SOURCE_QUEUE_LEN 32U

typedef struct {
    uint8_t bytes[TEST_FRAME_BYTES];
    uint32_t size;
    uint32_t serial;
    int result;
} source_record_t;

typedef struct {
    qm_queue_t records;
    qm_sem_t processed;
    qm_sem_t stop_entered;
    qm_sem_t feed_entered;
    qm_sem_t feed_release;
    qm_sem_t feed_done;
    atomic_int starts;
    atomic_int stops;
    atomic_int aborts;
    atomic_int start_error;
    atomic_int stop_error;
    atomic_int running;
    atomic_int feed_active;
    atomic_uint processed_serial;
    uint32_t next_serial;
    uint32_t read_serial;
    uint8_t read_buffer[TEST_FRAME_BYTES];
} source_t;

typedef enum { FAULT_NONE, FAULT_ENABLE_BEFORE_COMMIT, FAULT_SUBSCRIBE_AFTER_START } fault_kind_t;
static _Thread_local fault_kind_t fault_kind;
static _Thread_local qm_audio_capture_t *fault_capture;
static _Thread_local qm_audio_capture_subscription_t *fault_subscription;
static _Thread_local qm_bool fault_injected;

/* Inject a source failure at one real lock boundary, without production hooks
 * or scheduler sleeps. The final locked statement models the next source error
 * winning the lock before the caller's next critical section.
 * / 在真实解锁边界注入一次源故障，不加生产钩子或延时；模拟源错误先于下一临界区到达。 */
static int test_mutex_unlock(qm_mutex_t *mutex, const char *function)
{
    if (fault_capture != NULL && mutex == &fault_capture->lock) {
        source_t *source = fault_capture->config.device_ctx;
        qm_bool inject =
            (fault_kind == FAULT_ENABLE_BEFORE_COMMIT &&
             strcmp(function, "qm_audio_capture_enable") == 0 &&
             fault_subscription->snapshot.state == QM_AUDIO_CAPTURE_DISABLED) ||
            (fault_kind == FAULT_SUBSCRIBE_AFTER_START &&
             strcmp(function, "start_device") == 0 && atomic_load(&source->starts) != 0);

        if (inject) {
            fault_kind = FAULT_NONE;
            fault_injected = QM_TRUE;
            capture_source_failed_locked(fault_capture, -QM_EIO);
        }
    }
    return qm_mutex_unlock(mutex);
}

typedef struct observer observer_t;
struct observer {
    qm_sem_t state_changed;
    qm_sem_t media;
    qm_sem_t entered;
    qm_sem_t release;
    atomic_int block_first;
    atomic_int media_count;
    atomic_int error_count;
    atomic_int first_peer_count;
    observer_t *peer;
};

typedef enum { OP_UNSUBSCRIBE, OP_SHUTDOWN, OP_RECV } operation_kind_t;
typedef struct {
    operation_kind_t kind;
    qm_audio_capture_t *capture;
    qm_audio_capture_subscription_t *subscription;
    pthread_t thread;
    qm_sem_t done;
    int result;
} operation_t;

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

static void source_init(source_t *source)
{
    qm_memset(source, 0, sizeof(*source));
    assert(qm_queue_new(&source->records, TEST_SOURCE_QUEUE_LEN, sizeof(source_record_t)) == QM_EOK);
    sem_init(&source->processed);
    sem_init(&source->stop_entered);
    sem_init(&source->feed_entered);
    sem_init(&source->feed_release);
    sem_init(&source->feed_done);
}

static void source_free(source_t *source)
{
    assert(atomic_load(&source->running) == 0);
    assert(qm_queue_free(&source->records) == QM_EOK);
    assert(qm_sem_free(&source->processed) == QM_EOK);
    assert(qm_sem_free(&source->stop_entered) == QM_EOK);
    assert(qm_sem_free(&source->feed_entered) == QM_EOK);
    assert(qm_sem_free(&source->feed_release) == QM_EOK);
    assert(qm_sem_free(&source->feed_done) == QM_EOK);
}

int qm_audio_device_capture_start(void *context)
{
    source_t *source = context;

    atomic_fetch_add(&source->starts, 1);
    /* Model a partially started device even when start returns an error.
     * / 启动报错也模拟设备已经部分启动。 */
    atomic_store(&source->running, 1);
    return atomic_load(&source->start_error);
}

int qm_audio_device_capture_read(void *context, qm_audio_data_t *input, uint32_t timeout_ms)
{
    source_t *source = context;
    source_record_t record = {0};
    unsigned int bytes = sizeof(record);
    int ret = QM_EOK;

    if (source->read_serial != atomic_load(&source->processed_serial)) {
        atomic_store(&source->processed_serial, source->read_serial);
        give(&source->processed);
    }
    ret = qm_queue_recv(&source->records, &record, &bytes, timeout_ms);
    if (ret != QM_EOK) {
        return ret;
    }
    if (record.result != QM_EOK) {
        return record.result;
    }
    qm_memcpy(source->read_buffer, record.bytes, record.size);
    input->data = source->read_buffer;
    input->size = record.size;
    input->timestamp_ms = record.serial;
    input->flags = QM_AUDIO_DATA_TIMESTAMP_VALID;
    source->read_serial = record.serial;
    return QM_EOK;
}

int qm_audio_device_capture_abort(void *context)
{
    source_t *source = context;

    atomic_fetch_add(&source->aborts, 1);
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    source_record_t record = {.result = -QM_EINTR};

    assert(qm_queue_send(&source->records, &record, sizeof(record)) == QM_EOK);
#endif
    return QM_EOK;
}

int qm_audio_device_capture_stop(void *context)
{
    source_t *source = context;
    int ret = QM_EOK;

    atomic_fetch_add(&source->stops, 1);
    give(&source->stop_entered);
    if (atomic_load(&source->feed_active)) {
        take(&source->feed_done);
    }
    ret = atomic_load(&source->stop_error);
    if (ret == QM_EOK) {
        source_record_t record = {0};
        unsigned int size = sizeof(record);

        atomic_store(&source->running, 0);
        while (qm_queue_recv(&source->records, &record, &size, QM_NO_WAIT) == QM_EOK) {
            size = sizeof(record);
        }
    }
    return ret;
}

static qm_audio_capture_t *open_capture(source_t *source)
{
    qm_audio_capture_open_config_t config = {
        .device_ctx = source,
        .input_format = {
            .codec = QM_AUDIO_CODEC_PCM,
            .pcm = {.sample_rate_hz = 16000U, .channels = 1U, .sample = QM_AUDIO_PCM_S16},
        },
        .frame_bytes = TEST_FRAME_BYTES,
        .queue_len = 4U,
    };
    qm_audio_capture_t *capture = NULL;

    assert(qm_audio_capture_create(&config, &capture) == QM_EOK);
    assert(capture != NULL);
    return capture;
}

static void observer_init(observer_t *observer)
{
    qm_memset(observer, 0, sizeof(*observer));
    sem_init(&observer->state_changed);
    sem_init(&observer->media);
    sem_init(&observer->entered);
    sem_init(&observer->release);
}

static void observer_free(observer_t *observer)
{
    assert(qm_sem_free(&observer->state_changed) == QM_EOK);
    assert(qm_sem_free(&observer->media) == QM_EOK);
    assert(qm_sem_free(&observer->entered) == QM_EOK);
    assert(qm_sem_free(&observer->release) == QM_EOK);
}

static void state_changed(const qm_audio_capture_snapshot_t *snapshot, void *context)
{
    observer_t *observer = context;

    if (snapshot->state == QM_AUDIO_CAPTURE_ERROR) {
        atomic_fetch_add(&observer->error_count, 1);
    }
    give(&observer->state_changed);
}

static void media_received(const qm_audio_data_t *frame, void *context)
{
    observer_t *observer = context;
    int previous_count = atomic_fetch_add(&observer->media_count, 1);

    assert(frame->size != 0U && frame->size <= TEST_FRAME_BYTES);
    if (previous_count == 0 && observer->peer != NULL) {
        atomic_store(&observer->first_peer_count, atomic_load(&observer->peer->media_count));
    }
    if (atomic_exchange(&observer->block_first, 0)) {
        give(&observer->entered);
        take(&observer->release);
    }
    give(&observer->media);
}

static qm_audio_capture_subscription_t *subscribe(qm_audio_capture_t *capture,
                                                 qm_bool enabled, uint32_t queue_len,
                                                 observer_t *observer, qm_bool callback)
{
    qm_audio_capture_config_t config = {
        .enabled = enabled,
        .queue_len = queue_len,
        .delivery = callback ? QM_AUDIO_CAPTURE_DELIVERY_CALLBACK : QM_AUDIO_CAPTURE_DELIVERY_QUEUE,
        .frame_cb = callback ? media_received : NULL,
        .state_cb = observer != NULL ? state_changed : NULL,
        .user_ctx = observer,
    };
    qm_audio_capture_subscription_t *subscription = NULL;

    assert(qm_audio_capture_subscribe(capture, &config, &subscription) == QM_EOK);
    return subscription;
}

static void send_media(source_t *source, uint8_t value, uint32_t size)
{
    source_record_t record = {.size = size, .serial = ++source->next_serial};

    qm_memset(record.bytes, value, sizeof(record.bytes));
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
    qm_audio_data_t input = {
        .data = record.bytes, .size = size, .timestamp_ms = record.serial,
        .flags = QM_AUDIO_DATA_TIMESTAMP_VALID,
    };

    assert(qm_audio_capture_feed(&input, QM_EOK) == QM_EOK);
#else
    assert(qm_queue_send(&source->records, &record, sizeof(record)) == QM_EOK);
    while (atomic_load(&source->processed_serial) < record.serial) {
        take(&source->processed);
    }
#endif
}

static void fail_source(source_t *source)
{
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
    (void)source;
    assert(qm_audio_capture_feed(NULL, -QM_EIO) == -QM_EIO);
#else
    source_record_t record = {.result = -QM_EIO};

    assert(qm_queue_send(&source->records, &record, sizeof(record)) == QM_EOK);
#endif
}

static void expect_state(qm_audio_capture_subscription_t *subscription,
                         qm_audio_capture_state_t state, int result)
{
    qm_audio_capture_snapshot_t snapshot = {0};

    assert(qm_audio_capture_get_snapshot(subscription, &snapshot) == QM_EOK);
    assert(snapshot.state == state);
    assert(snapshot.result == result);
}

static void await_error(observer_t *observer)
{
    while (atomic_load(&observer->error_count) == 0) {
        take(&observer->state_changed);
    }
}

static void close_capture(qm_audio_capture_t *capture,
                          qm_audio_capture_subscription_t *first,
                          qm_audio_capture_subscription_t *second)
{
    assert(qm_audio_capture_shutdown(capture, TEST_WAIT_MS) == QM_EOK);
    if (first != NULL) {
        assert(qm_audio_capture_unsubscribe(first) == QM_EOK);
    }
    if (second != NULL) {
        assert(qm_audio_capture_unsubscribe(second) == QM_EOK);
    }
    assert(qm_audio_capture_destroy(capture) == QM_EOK);
}

static void *run_operation(void *context)
{
    operation_t *operation = context;

    if (operation->kind == OP_UNSUBSCRIBE) {
        operation->result = qm_audio_capture_unsubscribe(operation->subscription);
    } else if (operation->kind == OP_SHUTDOWN) {
        operation->result = qm_audio_capture_shutdown(operation->capture, TEST_WAIT_MS);
    } else {
        uint8_t data[TEST_FRAME_BYTES] = {0};
        qm_audio_data_t frame = {.data = data, .size = sizeof(data)};

        operation->result = qm_audio_capture_recv(operation->subscription, &frame, TEST_WAIT_MS);
    }
    give(&operation->done);
    return NULL;
}

static void operation_start(operation_t *operation)
{
    sem_init(&operation->done);
    assert(pthread_create(&operation->thread, NULL, run_operation, operation) == 0);
}

static void operation_finish(operation_t *operation, int expected)
{
    take(&operation->done);
    assert(pthread_join(operation->thread, NULL) == 0);
    assert(operation->result == expected);
    assert(qm_sem_free(&operation->done) == QM_EOK);
}

/* Observe entry, never inject timing by sleeping. / 观察操作入口，不以延时猜测时序。 */
static void await_subscription_flag(qm_audio_capture_subscription_t *subscription,
                                    qm_bool removing)
{
    qm_utils_time_t deadline = {0};

    qm_utils_time_countdown_ms(&deadline, TEST_WAIT_MS);
    for (;;) {
        qm_bool ready = QM_FALSE;

        assert(qm_mutex_lock(&subscription->capture->lock, QM_WAIT_FOREVER) == QM_EOK);
        ready = removing ? subscription->removing : subscription->recv_active;
        assert(qm_mutex_unlock(&subscription->capture->lock) == QM_EOK);
        if (ready) {
            return;
        }
        assert(!qm_utils_time_is_expired(&deadline));
        sched_yield();
    }
}

static void test_delivery_and_disable(void)
{
    source_t source = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *fast = NULL;
    qm_audio_capture_subscription_t *slow = NULL;
    uint8_t data[TEST_FRAME_BYTES] = {0};
    qm_audio_data_t frame = {.data = data, .size = sizeof(data)};
    uint32_t size = 99U;
    operation_t receiver = {.kind = OP_RECV};

    source_init(&source);
    capture = open_capture(&source);
    assert(atomic_load(&source.starts) == 0);
    fast = subscribe(capture, QM_FALSE, 4U, NULL, QM_FALSE);
    assert(atomic_load(&source.starts) == 0);
    assert(qm_audio_capture_enable(fast, QM_TRUE) == QM_EOK);
    assert(atomic_load(&source.starts) == 1);
    slow = subscribe(capture, QM_TRUE, 1U, NULL, QM_FALSE);
    assert(atomic_load(&source.starts) == 1);
    for (uint8_t value = 1U; value <= 3U; ++value) {
        send_media(&source, value, (uint32_t)value * 2U);
    }
    assert(qm_audio_capture_peek_size(fast, &size) == QM_EOK && size == 2U);
    frame.size = 1U;
    assert(qm_audio_capture_recv(fast, &frame, QM_NO_WAIT) == -QM_EINVAL && frame.size == 0U);
    assert(qm_audio_capture_peek_size(fast, &size) == QM_EOK && size == 2U);
    for (uint8_t value = 1U; value <= 3U; ++value) {
        frame.size = sizeof(data);
        assert(qm_audio_capture_recv(fast, &frame, QM_NO_WAIT) == QM_EOK);
        assert(frame.size == (uint32_t)value * 2U && data[0] == value);
        assert(frame.sequence == value);
    }
    frame.size = sizeof(data);
    assert(qm_audio_capture_recv(slow, &frame, QM_NO_WAIT) == QM_EOK);
    assert(frame.size == 6U && data[0] == 3U && frame.sequence == 3U);
    assert((frame.flags & QM_AUDIO_DATA_DISCONTINUITY) != 0U);
    assert(qm_audio_capture_peek_size(fast, &size) == -QM_EEMPTY && size == 0U);
    receiver.subscription = fast;
    operation_start(&receiver);
    await_subscription_flag(fast, QM_FALSE);
    assert(qm_audio_capture_enable(fast, QM_FALSE) == QM_EOK);
    operation_finish(&receiver, -QM_EBUSY);
    assert(qm_audio_capture_enable(slow, QM_FALSE) == QM_EOK);
    assert(atomic_load(&source.aborts) == 0 && atomic_load(&source.stops) == 0);
    assert(qm_audio_capture_enable(fast, QM_TRUE) == QM_EOK);
    assert(atomic_load(&source.starts) == 1);
    close_capture(capture, fast, slow);
    source_free(&source);
}

static void test_start_failure_cleanup(qm_bool failed_stop)
{
    source_t source = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *subscription = NULL;
    qm_audio_capture_config_t config = {.enabled = QM_TRUE, .delivery = QM_AUDIO_CAPTURE_DELIVERY_QUEUE};

    source_init(&source);
    capture = open_capture(&source);
    atomic_store(&source.start_error, -QM_EIO);
    atomic_store(&source.stop_error, failed_stop ? -QM_EBUSY : QM_EOK);
    assert(qm_audio_capture_subscribe(capture, &config, &subscription) == -QM_EIO);
    assert(subscription == NULL);
    assert(atomic_load(&source.starts) == 1 && atomic_load(&source.stops) == 1);
    if (!failed_stop) {
        atomic_store(&source.start_error, QM_EOK);
        subscription = subscribe(capture, QM_TRUE, 0U, NULL, QM_FALSE);
        assert(atomic_load(&source.starts) == 2);
        close_capture(capture, subscription, NULL);
        source_free(&source);
        return;
    }
    assert(qm_audio_capture_destroy(capture) == -QM_EBUSY);
    assert(qm_audio_capture_subscribe(capture, &config, &subscription) != QM_EOK);
    assert(subscription == NULL && atomic_load(&source.starts) == 1);
    assert(qm_audio_capture_shutdown(capture, TEST_WAIT_MS) != QM_EOK);
    atomic_store(&source.stop_error, QM_EOK);
    assert(qm_audio_capture_shutdown(capture, TEST_WAIT_MS) == QM_EOK);
    assert(qm_audio_capture_destroy(capture) == QM_EOK);
    source_free(&source);
}

static void test_error_preservation(void)
{
    source_t source = {0};
    observer_t first = {0};
    observer_t second = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *running = NULL;
    qm_audio_capture_subscription_t *disabled = NULL;
    uint32_t size = 99U;

    source_init(&source);
    observer_init(&first);
    observer_init(&second);
    capture = open_capture(&source);
    running = subscribe(capture, QM_TRUE, 0U, &first, QM_FALSE);
    disabled = subscribe(capture, QM_FALSE, 0U, &second, QM_FALSE);
    fail_source(&source);
    await_error(&first);
    await_error(&second);
    expect_state(running, QM_AUDIO_CAPTURE_ERROR, -QM_EIO);
    expect_state(disabled, QM_AUDIO_CAPTURE_ERROR, -QM_EIO);
    assert(qm_audio_capture_enable(disabled, QM_TRUE) == -QM_EBUSY);
    assert(qm_audio_capture_peek_size(disabled, &size) == -QM_EIO && size == 0U);
    assert(qm_audio_capture_shutdown(capture, TEST_WAIT_MS) == QM_EOK);
    expect_state(running, QM_AUDIO_CAPTURE_ERROR, -QM_EIO);
    expect_state(disabled, QM_AUDIO_CAPTURE_ERROR, -QM_EIO);
    close_capture(capture, running, disabled);
    observer_free(&first);
    observer_free(&second);
    source_free(&source);
}

static void test_enable_source_error_before_commit(void)
{
    source_t source = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *running = NULL;
    qm_audio_capture_subscription_t *disabled = NULL;
    int ret = QM_EOK;

    source_init(&source);
    capture = open_capture(&source);
    running = subscribe(capture, QM_TRUE, 0U, NULL, QM_FALSE);
    disabled = subscribe(capture, QM_FALSE, 0U, NULL, QM_FALSE);
    fault_capture = capture;
    fault_subscription = disabled;
    fault_kind = FAULT_ENABLE_BEFORE_COMMIT;
    fault_injected = QM_FALSE;
    ret = qm_audio_capture_enable(disabled, QM_TRUE);
    fault_kind = FAULT_NONE;
    fault_capture = NULL;
    fault_subscription = NULL;
    assert(atomic_load(&source.starts) == 1);
    if (fault_injected) {
        assert(ret != QM_EOK);
        expect_state(disabled, QM_AUDIO_CAPTURE_ERROR, -QM_EIO);
    } else {
        /* A single locked commit removes the vulnerable unlock entirely.
         * / 锁内一次提交使原竞态解锁窗口不再存在。 */
        assert(ret == QM_EOK);
        expect_state(disabled, QM_AUDIO_CAPTURE_RUNNING, QM_EOK);
    }
    close_capture(capture, running, disabled);
    source_free(&source);
}

static void test_subscribe_source_error_before_publication(qm_bool failed_stop)
{
    source_t source = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *subscription = NULL;
    qm_audio_capture_config_t config = {.enabled = QM_TRUE, .delivery = QM_AUDIO_CAPTURE_DELIVERY_QUEUE};

    source_init(&source);
    capture = open_capture(&source);
    atomic_store(&source.stop_error, failed_stop ? -QM_EBUSY : QM_EOK);
    fault_capture = capture;
    fault_kind = FAULT_SUBSCRIBE_AFTER_START;
    fault_injected = QM_FALSE;
    assert(qm_audio_capture_subscribe(capture, &config, &subscription) == -QM_EIO);
    assert(subscription == NULL && fault_injected);
    fault_kind = FAULT_NONE;
    fault_capture = NULL;
    assert(atomic_load(&source.starts) == 1 && atomic_load(&source.stops) >= 1);
    if (failed_stop) {
        assert(atomic_load(&source.running) == 1);
        assert(qm_audio_capture_destroy(capture) == -QM_EBUSY);
        assert(qm_audio_capture_subscribe(capture, &config, &subscription) != QM_EOK);
        assert(subscription == NULL && atomic_load(&source.starts) == 1);
        atomic_store(&source.stop_error, QM_EOK);
        close_capture(capture, NULL, NULL);
    } else {
        assert(atomic_load(&source.running) == 0);
        subscription = subscribe(capture, QM_TRUE, 0U, NULL, QM_FALSE);
        assert(atomic_load(&source.starts) == 2);
        close_capture(capture, subscription, NULL);
    }
    source_free(&source);
}

static void test_subscriber_error_isolation(void)
{
    source_t source = {0};
    observer_t observer = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *broken = NULL;
    qm_audio_capture_subscription_t *healthy = NULL;
    uint8_t data[TEST_FRAME_BYTES] = {0};
    qm_audio_data_t frame = {.data = data, .size = sizeof(data)};

    source_init(&source);
    observer_init(&observer);
    capture = open_capture(&source);
    broken = subscribe(capture, QM_TRUE, 1U, &observer, QM_FALSE);
    healthy = subscribe(capture, QM_TRUE, 4U, NULL, QM_FALSE);
    send_media(&source, 1U, 4U);
    assert(qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER) == QM_EOK);
    broken->ring.used_bytes = 1U;
    assert(qm_mutex_unlock(&capture->lock) == QM_EOK);
    send_media(&source, 2U, 4U);
    await_error(&observer);
    expect_state(broken, QM_AUDIO_CAPTURE_ERROR, -QM_EIO);
    expect_state(healthy, QM_AUDIO_CAPTURE_RUNNING, QM_EOK);
    assert(atomic_load(&observer.error_count) == 1);
    send_media(&source, 3U, 4U);
    assert(atomic_load(&observer.error_count) == 1);
    for (uint8_t value = 1U; value <= 3U; ++value) {
        frame.size = sizeof(data);
        assert(qm_audio_capture_recv(healthy, &frame, QM_NO_WAIT) == QM_EOK);
        assert(data[0] == value);
    }
    expect_state(broken, QM_AUDIO_CAPTURE_ERROR, -QM_EIO);
    close_capture(capture, broken, healthy);
    observer_free(&observer);
    source_free(&source);
}

static void test_callback_fairness(void)
{
    source_t source = {0};
    observer_t first = {0};
    observer_t second = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *a = NULL;
    qm_audio_capture_subscription_t *b = NULL;

    source_init(&source);
    observer_init(&first);
    observer_init(&second);
    atomic_store(&first.block_first, 1);
    second.peer = &first;
    capture = open_capture(&source);
    a = subscribe(capture, QM_TRUE, 8U, &first, QM_TRUE);
    b = subscribe(capture, QM_TRUE, 8U, &second, QM_TRUE);
    send_media(&source, 1U, 4U);
    take(&first.entered);
    for (uint8_t value = 2U; value <= 7U; ++value) {
        send_media(&source, value, 4U);
    }
    give(&first.release);
    take(&second.media);
    assert(atomic_load(&second.first_peer_count) <= 2);
    close_capture(capture, a, b);
    observer_free(&first);
    observer_free(&second);
    source_free(&source);
}

static void test_corrupt_record(qm_bool peek)
{
    source_t source = {0};
    observer_t observer = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *subscription = NULL;
    uint8_t corrupt[sizeof(capture_record_metadata_t) + TEST_FRAME_BYTES + 2U] = {0};
    uint8_t data[TEST_FRAME_BYTES * 2U] = {0};
    qm_audio_data_t frame = {.data = data, .size = sizeof(data)};
    uint32_t size = 99U;

    source_init(&source);
    observer_init(&observer);
    capture = open_capture(&source);
    subscription = subscribe(capture, QM_TRUE, 4U, &observer, QM_FALSE);
    assert(qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER) == QM_EOK);
    assert(qm_ring_queue_push(&subscription->ring, corrupt, sizeof(corrupt)) == QM_EOK);
    subscription->queued_records = 1U;
    assert(qm_mutex_unlock(&capture->lock) == QM_EOK);
    if (peek) {
        assert(qm_audio_capture_peek_size(subscription, &size) == -QM_EIO && size == 0U);
    } else {
        assert(qm_audio_capture_recv(subscription, &frame, QM_NO_WAIT) == -QM_EIO);
        assert(frame.size == 0U);
    }
    await_error(&observer);
    expect_state(subscription, QM_AUDIO_CAPTURE_ERROR, -QM_EIO);
    close_capture(capture, subscription, NULL);
    observer_free(&observer);
    source_free(&source);
}

static void test_unsubscribe_serialization(void)
{
    source_t source = {0};
    observer_t observer = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *a = NULL;
    qm_audio_capture_subscription_t *b = NULL;
    operation_t operation = {.kind = OP_UNSUBSCRIBE};

    source_init(&source);
    observer_init(&observer);
    atomic_store(&observer.block_first, 1);
    capture = open_capture(&source);
    a = subscribe(capture, QM_TRUE, 0U, &observer, QM_TRUE);
    b = subscribe(capture, QM_TRUE, 0U, NULL, QM_FALSE);
    send_media(&source, 1U, 4U);
    take(&observer.entered);
    operation.subscription = a;
    operation_start(&operation);
    await_subscription_flag(a, QM_TRUE);
    assert(qm_audio_capture_unsubscribe(b) == -QM_EBUSY);
    give(&observer.release);
    operation_finish(&operation, QM_EOK);
    close_capture(capture, b, NULL);
    observer_free(&observer);
    source_free(&source);
}

static void test_timeout_retry(qm_bool shutdown)
{
    source_t source = {0};
    observer_t observer = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *subscription = NULL;

    source_init(&source);
    observer_init(&observer);
    atomic_store(&observer.block_first, 1);
    capture = open_capture(&source);
    subscription = subscribe(capture, QM_TRUE, 0U, &observer, QM_TRUE);
    send_media(&source, 1U, 4U);
    take(&observer.entered);
    if (shutdown) {
        assert(qm_audio_capture_shutdown(capture, QM_NO_WAIT) == -QM_ETIMEOUT);
    } else {
        assert(qm_audio_capture_unsubscribe(subscription) == -QM_ETIMEOUT);
    }
    assert(qm_audio_capture_destroy(capture) == -QM_EBUSY);
    give(&observer.release);
    if (shutdown) {
        close_capture(capture, subscription, NULL);
    } else {
        assert(qm_audio_capture_unsubscribe(subscription) == QM_EOK);
        close_capture(capture, NULL, NULL);
    }
    observer_free(&observer);
    source_free(&source);
}

static void test_stop_failure_retry(void)
{
    source_t source = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_subscription_t *subscription = NULL;

    source_init(&source);
    capture = open_capture(&source);
    subscription = subscribe(capture, QM_TRUE, 0U, NULL, QM_FALSE);
    atomic_store(&source.stop_error, -QM_EIO);
    assert(qm_audio_capture_unsubscribe(subscription) == -QM_EIO);
    assert(atomic_load(&source.running) == 1);
    assert(qm_audio_capture_destroy(capture) == -QM_EBUSY);
    atomic_store(&source.stop_error, QM_EOK);
    assert(qm_audio_capture_unsubscribe(subscription) == QM_EOK);
    assert(atomic_load(&source.running) == 0);
    close_capture(capture, NULL, NULL);
    source_free(&source);
}

static void test_capacity_overflow_rejected(void)
{
    source_t source = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_t *overflow_capture = NULL;
    qm_audio_capture_subscription_t *subscription = NULL;
    qm_audio_capture_open_config_t open_config = {0};
    qm_audio_capture_config_t subscription_config = {
        .enabled = QM_FALSE,
        .queue_len = UINT32_MAX,
        .delivery = QM_AUDIO_CAPTURE_DELIVERY_QUEUE,
    };

    source_init(&source);
    open_config.device_ctx = &source;
    open_config.input_format.codec = QM_AUDIO_CODEC_PCM;
    open_config.input_format.pcm.sample_rate_hz = 16000U;
    open_config.input_format.pcm.channels = 1U;
    open_config.input_format.pcm.sample = QM_AUDIO_PCM_S16;
    open_config.frame_bytes = UINT32_MAX - 3U;
    open_config.queue_len = 1U;
    assert(qm_audio_capture_create(&open_config, &overflow_capture) == -QM_EINVAL);
    assert(overflow_capture == NULL);
    open_config.frame_bytes = TEST_FRAME_BYTES;
    open_config.queue_len = UINT32_MAX;
    assert(qm_audio_capture_create(&open_config, &overflow_capture) == -QM_EINVAL);
    assert(overflow_capture == NULL);

    capture = open_capture(&source);
    assert(qm_audio_capture_subscribe(capture, &subscription_config, &subscription) ==
           -QM_EINVAL);
    assert(subscription == NULL);
    close_capture(capture, NULL, NULL);
    source_free(&source);
}

#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
static void *held_feed(void *context)
{
    source_t *source = context;
    uint8_t data[4] = {0};
    qm_audio_data_t input = {.data = data, .size = sizeof(data)};

    atomic_store(&source->feed_active, 1);
    give(&source->feed_entered);
    take(&source->feed_release);
    assert(qm_audio_capture_feed(&input, QM_EOK) == -QM_EBUSY);
    atomic_store(&source->feed_active, 0);
    give(&source->feed_done);
    return NULL;
}

static void test_push_stop_barrier_and_slot(void)
{
    source_t source = {0};
    qm_audio_capture_t *capture = NULL;
    qm_audio_capture_t *other = NULL;
    qm_audio_capture_subscription_t *subscription = NULL;
    pthread_t producer = {0};
    operation_t operation = {.kind = OP_SHUTDOWN};
    qm_audio_capture_open_config_t config = {0};

    source_init(&source);
    capture = open_capture(&source);
    config = capture->config;
    subscription = subscribe(capture, QM_TRUE, 0U, NULL, QM_FALSE);
    assert(pthread_create(&producer, NULL, held_feed, &source) == 0);
    take(&source.feed_entered);
    operation.capture = capture;
    operation_start(&operation);
    take(&source.stop_entered);
    assert(qm_sem_wait(&operation.done, QM_NO_WAIT) == -QM_ETIMEOUT);
    assert(qm_audio_capture_create(&config, &other) == -QM_EBUSY && other == NULL);
    give(&source.feed_release);
    assert(pthread_join(producer, NULL) == 0);
    operation_finish(&operation, QM_EOK);
    assert(qm_audio_capture_create(&config, &other) == -QM_EBUSY && other == NULL);
    close_capture(capture, subscription, NULL);
    other = open_capture(&source);
    close_capture(other, NULL, NULL);
    source_free(&source);
}
#endif

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    puts("delivery, size, overflow and disable");
    test_delivery_and_disable();
    puts("start failure cleanup and retry");
    test_start_failure_cleanup(QM_FALSE);
    test_start_failure_cleanup(QM_TRUE);
    puts("source errors at enable and subscribe commit boundaries");
    test_enable_source_error_before_commit();
    test_subscribe_source_error_before_publication(QM_FALSE);
    test_subscribe_source_error_before_publication(QM_TRUE);
    puts("source error preservation and subscriber error isolation");
    test_error_preservation();
    test_subscriber_error_isolation();
    puts("corrupt record detection and state callbacks");
    test_corrupt_record(QM_FALSE);
    test_corrupt_record(QM_TRUE);
    puts("callback fairness and lifecycle serialization");
    test_callback_fairness();
    test_unsubscribe_serialization();
    puts("timeout and stop failure retry");
    test_timeout_retry(QM_FALSE);
    test_timeout_retry(QM_TRUE);
    test_stop_failure_retry();
    puts("capacity overflow rejection");
    test_capacity_overflow_rejected();
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
    puts("push producer barrier and slot lifetime");
    test_push_stop_barrier_and_slot();
#endif
    puts("capture regression tests passed");
    return 0;
}
