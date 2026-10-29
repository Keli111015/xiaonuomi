/**
 * @file qm_audio_capture.c
 * @brief Native capture subscription storage and ingress. / 原生采集订阅存储与输入。
 */
#include "qm.h"
#include "qm_string.h"
#include "qm_utils_timer.h"
#include "qm_ring_queue.h"
#include "qm_audio_capture.h"
#include "qm_audio_format.h"
#include "adapter/qm_audio_device.h"

#ifndef CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN
#define CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN 4U
#endif
#ifndef CONFIG_QM_AUDIO_CAPTURE_MAX_SUBSCRIBERS
#define CONFIG_QM_AUDIO_CAPTURE_MAX_SUBSCRIBERS 4U
#endif
#ifndef CONFIG_QM_AUDIO_CAPTURE_TASK_STACK_SIZE
#define CONFIG_QM_AUDIO_CAPTURE_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_QM_AUDIO_CAPTURE_TASK_PRIORITY
#define CONFIG_QM_AUDIO_CAPTURE_TASK_PRIORITY 20
#endif
#ifndef CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS
#define CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS 1000U
#endif
#ifndef CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_STACK_SIZE
#define CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_STACK_SIZE 3072
#endif
#ifndef CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_PRIORITY
#define CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_PRIORITY 20
#endif
#define CAPTURE_READ_TIMEOUT_MS 100U

typedef struct {
    uint32_t timestamp_ms;
    uint32_t duration_ms;
    uint32_t sequence;
    uint32_t flags;
} capture_record_metadata_t;

struct qm_audio_capture {
    qm_audio_capture_open_config_t config;
    qm_mutex_t lock;
    qm_sem_t quiesced;
    qm_sem_t dispatch_wake;
    qm_task_t dispatch_task;
    uint8_t *callback_buffer;
    qm_bool closing;
    qm_bool running;
    qm_bool dispatch_active;
    qm_bool lifecycle_busy;
    qm_bool device_stop_pending;
    qm_bool stopped;
    uint32_t dispatch_next;
    int stop_result;
    int source_error;
    uint32_t sequence;
    uint32_t subscription_count;
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    qm_task_t task;
    qm_bool task_active;
#endif
    qm_audio_capture_subscription_t *subscriptions[CONFIG_QM_AUDIO_CAPTURE_MAX_SUBSCRIBERS];
};

struct qm_audio_capture_subscription {
    qm_audio_capture_t *capture;
    qm_audio_capture_config_t config;
    qm_audio_capture_snapshot_t snapshot;
    qm_ring_queue_t ring;
    uint8_t *storage;
    uint32_t queued_records;
    qm_sem_t data_ready;
    qm_bool removing;
    qm_bool discontinuity_pending;
    qm_bool recv_active;
    qm_bool callback_active;
    qm_bool state_pending;
};

#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
/* create() must publish the fully initialized instance here. / create() 必须在实例
 * 完整初始化后把实例发布到这里。 */
static qm_audio_capture_t *push_capture;
#endif

static qm_bool capture_input_valid(const qm_audio_capture_t *capture,
                                   const qm_audio_data_t *input)
{
    uint32_t sample_frame_bytes = 0U;

    if (capture == NULL || input == NULL || input->data == NULL || input->size == 0U ||
        input->size > capture->config.frame_bytes ||
        (input->flags & ~(QM_AUDIO_DATA_TIMESTAMP_VALID |
                          QM_AUDIO_DATA_DURATION_VALID |
                          QM_AUDIO_DATA_DISCONTINUITY)) != 0U) {
        return QM_FALSE;
    }
    if (capture->config.input_format.codec == QM_AUDIO_CODEC_PCM) {
        sample_frame_bytes = qm_audio_pcm_frame_bytes(&capture->config.input_format.pcm);
        return sample_frame_bytes != 0U && (input->size % sample_frame_bytes) == 0U;
    }
    return qm_audio_packet_valid(&capture->config.input_format, input);
}

static int capture_subscription_storage_bytes(uint32_t frame_bytes,
                                              uint32_t queue_len,
                                              uint32_t *storage_bytes)
{
    uint64_t record_bytes = 0U;
    uint64_t total_bytes = 0U;

    if (storage_bytes == NULL || queue_len == 0U) {
        return -QM_EINVAL;
    }

    record_bytes = (uint64_t)sizeof(capture_record_metadata_t) +
                   frame_bytes;
    if (record_bytes > UINT32_MAX - QM_RING_QUEUE_RECORD_HEADER_BYTES) {
        return -QM_EINVAL;
    }
    total_bytes = (uint64_t)queue_len *
                  ((uint64_t)QM_RING_QUEUE_RECORD_HEADER_BYTES + record_bytes);
    if (total_bytes > UINT32_MAX) {
        return -QM_EINVAL;
    }

    *storage_bytes = (uint32_t)total_bytes;
    return QM_EOK;
}

static void capture_subscription_free(qm_audio_capture_subscription_t *subscription)
{
    if (subscription == NULL) {
        return;
    }
    if (qm_sem_is_valid(&subscription->data_ready)) {
        (void)qm_sem_free(&subscription->data_ready);
    }
    qm_free(subscription->storage);
    qm_free(subscription);
}

static void capture_drain_notification(qm_sem_t *sem)
{
    while (qm_sem_wait(sem, QM_NO_WAIT) == QM_EOK) {
    }
}

static void capture_signal_dispatch(qm_audio_capture_t *capture)
{
    if (qm_sem_is_valid(&capture->dispatch_wake)) {
        (void)qm_sem_signal(&capture->dispatch_wake);
    }
}

static void capture_dispatch_task(void *arg);

/* The callback resources are lazy: QUEUE-only captures do not need a task or
 * staging buffer. The caller holds capture->lock. */
static int capture_start_dispatcher_locked(qm_audio_capture_t *capture,
                                           qm_bool need_buffer)
{
    uint64_t callback_bytes64 = 0U;
    uint32_t callback_bytes = 0U;
    qm_bool allocated_buffer = QM_FALSE;
    int ret = QM_EOK;

    if (need_buffer && capture->callback_buffer == NULL) {
        callback_bytes64 = (uint64_t)sizeof(capture_record_metadata_t) +
                           capture->config.frame_bytes;
        if (callback_bytes64 > UINT32_MAX) {
            return -QM_EINVAL;
        }
        callback_bytes = (uint32_t)callback_bytes64;
        capture->callback_buffer = qm_malloc(callback_bytes);
        if (capture->callback_buffer == NULL) {
            return -QM_ENOMEM;
        }
        allocated_buffer = QM_TRUE;
    }
    if (capture->dispatch_active) {
        return QM_EOK;
    }
    ret = qm_sem_new(&capture->dispatch_wake, 0U);
    if (ret != QM_EOK) {
        if (allocated_buffer) {
            qm_free(capture->callback_buffer);
            capture->callback_buffer = NULL;
        }
        return ret;
    }

    capture->dispatch_active = QM_TRUE;
    ret = qm_task_new(&capture->dispatch_task, "audio_cap_cb",
                      capture_dispatch_task, capture,
                      CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_STACK_SIZE,
                      CONFIG_QM_AUDIO_CAPTURE_CALLBACK_TASK_PRIORITY);
    if (ret != QM_EOK) {
        capture->dispatch_active = QM_FALSE;
        (void)qm_sem_free(&capture->dispatch_wake);
        if (allocated_buffer) {
            qm_free(capture->callback_buffer);
            capture->callback_buffer = NULL;
        }
    }
    return ret;
}

/* State and queue helpers run under capture->lock. / 状态与队列操作均持有实例锁。 */
static void capture_set_state_locked(qm_audio_capture_t *capture,
                                     qm_audio_capture_subscription_t *subscription,
                                     qm_audio_capture_state_t state, int result)
{
    if (subscription->snapshot.state == QM_AUDIO_CAPTURE_ERROR ||
        subscription->snapshot.state == QM_AUDIO_CAPTURE_STOPPED ||
        (subscription->snapshot.state == state && subscription->snapshot.result == result)) {
        return;
    }
    subscription->snapshot.state = state;
    subscription->snapshot.result = result;
    subscription->state_pending = subscription->config.state_cb != NULL &&
                                  !subscription->removing;
    (void)qm_sem_signal(&subscription->data_ready);
    capture_signal_dispatch(capture);
}

static void clear_queue_locked(qm_audio_capture_subscription_t *subscription)
{
    (void)qm_ring_queue_reset(&subscription->ring);
    subscription->queued_records = 0U;
    subscription->discontinuity_pending = QM_FALSE;
    capture_drain_notification(&subscription->data_ready);
}

static void fail_subscription_locked(qm_audio_capture_subscription_t *subscription, int result)
{
    clear_queue_locked(subscription);
    capture_set_state_locked(subscription->capture, subscription,
                             QM_AUDIO_CAPTURE_ERROR, result);
}

static void capture_source_failed_locked(qm_audio_capture_t *capture, int result)
{
    uint32_t index = 0U;
    qm_audio_capture_subscription_t *subscription = NULL;

    if (capture->source_error == QM_EOK) {
        capture->source_error = result;
    }
    capture->running = QM_FALSE;
    for (index = 0U; index < capture->subscription_count; ++index) {
        subscription = capture->subscriptions[index];

        if (!subscription->removing &&
            (subscription->snapshot.state == QM_AUDIO_CAPTURE_RUNNING ||
             subscription->snapshot.state == QM_AUDIO_CAPTURE_DISABLED)) {
            fail_subscription_locked(subscription, capture->source_error);
        }
    }
}

/* One lifecycle owner may wait; feed/recv never hold this ownership.
 * / 同时仅允许一个生命周期等待者；feed/recv 不占用此所有权。 */
static int enter_lifecycle(qm_audio_capture_t *capture)
{
    int ret = qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);

    if (ret != QM_EOK) {
        return ret;
    }
    if (capture->lifecycle_busy) {
        ret = -QM_EBUSY;
    } else {
        capture->lifecycle_busy = QM_TRUE;
    }
    (void)qm_mutex_unlock(&capture->lock);
    return ret;
}

static void leave_lifecycle(qm_audio_capture_t *capture)
{
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    capture->lifecycle_busy = QM_FALSE;
    (void)qm_mutex_unlock(&capture->lock);
}

/* active belongs to a live Capture/subscription and is protected by its lock.
 * The lifecycle owner keeps that storage alive throughout the shared deadline.
 * / active 属于有效实例/订阅并受实例锁保护；生命周期持有者保证等待期间存储有效。
 * 各阶段共用同一截止时间。 */
static int wait_idle(qm_audio_capture_t *capture, const qm_bool *active,
                     qm_utils_time_t *deadline, uint32_t timeout_ms)
{
    int wait_ret = QM_EOK;
    qm_bool busy = QM_FALSE;

    for (;;) {
        busy = QM_FALSE;

        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
        busy = *active;
        (void)qm_mutex_unlock(&capture->lock);
        if (!busy) {
            return QM_EOK;
        }
        if (wait_ret != QM_EOK ||
            (timeout_ms != QM_WAIT_FOREVER && qm_utils_time_left(deadline) == 0U)) {
            return -QM_ETIMEOUT;
        }
        wait_ret = qm_sem_wait(&capture->quiesced,
                                timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                                qm_utils_time_left(deadline));
    }
}

static int peek_record_locked(qm_audio_capture_subscription_t *subscription, uint32_t *bytes)
{
    int ret = qm_ring_queue_peek_size(&subscription->ring, bytes);
    uint32_t payload_bytes = 0U;

    if (ret != QM_EOK) {
        fail_subscription_locked(subscription, -QM_EIO);
        return -QM_EIO;
    }
    if (*bytes < sizeof(capture_record_metadata_t)) {
        fail_subscription_locked(subscription, -QM_EIO);
        return -QM_EIO;
    }
    payload_bytes = *bytes - (uint32_t)sizeof(capture_record_metadata_t);
    if (payload_bytes > subscription->capture->config.frame_bytes) {
        fail_subscription_locked(subscription, -QM_EIO);
        return -QM_EIO;
    }
    return ret;
}

static int pop_record_locked(qm_audio_capture_subscription_t *subscription,
                             uint8_t *record, uint32_t bytes, qm_audio_data_t *frame)
{
    capture_record_metadata_t metadata = {0};
    uint32_t popped_bytes = 0U;
    int ret = qm_ring_queue_pop(&subscription->ring, record, bytes, &popped_bytes);

    if (ret != QM_EOK) {
        fail_subscription_locked(subscription, -QM_EIO);
        return -QM_EIO;
    }
    qm_memcpy(&metadata, record, sizeof(metadata));
    --subscription->queued_records;
    frame->data = record + sizeof(metadata);
    frame->size = bytes - (uint32_t)sizeof(metadata);
    frame->timestamp_ms = metadata.timestamp_ms;
    frame->duration_ms = metadata.duration_ms;
    frame->sequence = metadata.sequence;
    frame->flags = metadata.flags;
    if (subscription->discontinuity_pending) {
        frame->flags |= QM_AUDIO_DATA_DISCONTINUITY;
        subscription->discontinuity_pending = QM_FALSE;
    }
    return QM_EOK;
}

/* The caller validated the source record and holds capture->lock. Per-subscriber
 * queue failures must not stop other consumers. / 调用方已校验源记录且持有实例锁；
 * 单个订阅的队列故障不影响其他消费者。 */
static void enqueue_locked(qm_audio_capture_t *capture, const qm_audio_data_t *input)
{
    capture_record_metadata_t metadata = {
        .timestamp_ms = input->timestamp_ms,
        .duration_ms = input->duration_ms,
        .sequence = input->sequence,
        .flags = input->flags,
    };
    const qm_ring_queue_segment_t segments[] = {
        { &metadata, sizeof(metadata) },
        { input->data, input->size },
    };
    uint32_t index = 0U;
    qm_audio_capture_subscription_t *subscription = NULL;
    qm_bool was_empty = QM_FALSE;
    int queue_ret = QM_EOK;

    if (capture->config.input_format.codec == QM_AUDIO_CODEC_PCM) {
        metadata.sequence = ++capture->sequence;
    }
    for (index = 0U; index < capture->subscription_count; ++index) {
        subscription = capture->subscriptions[index];
        was_empty = subscription->queued_records == 0U;

        if (subscription->removing || subscription->snapshot.state != QM_AUDIO_CAPTURE_RUNNING) {
            continue;
        }
        if (subscription->queued_records >= subscription->snapshot.queue_len) {
            queue_ret = qm_ring_queue_discard(&subscription->ring);
            if (queue_ret != QM_EOK) {
                fail_subscription_locked(subscription, -QM_EIO);
                continue;
            }
            --subscription->queued_records;
            subscription->discontinuity_pending = QM_TRUE;
        }
        queue_ret = qm_ring_queue_push_segments(&subscription->ring, segments,
                                                sizeof(segments) / sizeof(segments[0]));
        if (queue_ret != QM_EOK) {
            fail_subscription_locked(subscription, -QM_EIO);
            continue;
        }
        ++subscription->queued_records;
        if (was_empty && subscription->config.delivery == QM_AUDIO_CAPTURE_DELIVERY_QUEUE) {
            (void)qm_sem_signal(&subscription->data_ready);
        }
    }
    capture_signal_dispatch(capture);
}

static void capture_dispatch_task(void *arg)
{
    qm_audio_capture_t *capture = (qm_audio_capture_t *)arg;
    qm_task_t self = {0};
    qm_audio_capture_subscription_t *selected = NULL;
    qm_audio_capture_snapshot_t snapshot = {0};
    qm_audio_data_t frame = {0};
    qm_audio_capture_subscription_t *subscription = NULL;
    uint32_t scanned = 0U;
    uint32_t index = 0U;
    uint32_t record_bytes = 0U;
    qm_bool deliver_state = QM_FALSE;

    for (;;) {
        (void)qm_sem_wait(&capture->dispatch_wake, QM_WAIT_FOREVER);
        for (;;) {
            selected = NULL;
            snapshot = (qm_audio_capture_snapshot_t){0};
            frame = (qm_audio_data_t){0};
            subscription = NULL;
            scanned = 0U;
            index = 0U;
            record_bytes = 0U;
            deliver_state = QM_FALSE;

            (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
            if (capture->closing) {
                /* Publish completion before the last access to Capture.
                 * / 在最后一次访问实例前发布完成状态。 */
                self = capture->dispatch_task;
                capture->dispatch_task = (qm_task_t){0};
                capture->dispatch_active = QM_FALSE;
                (void)qm_sem_signal(&capture->quiesced);
                (void)qm_mutex_unlock(&capture->lock);
                qm_task_exit(&self);
                return;
            }
            for (scanned = 0U; scanned < capture->subscription_count; ++scanned) {
                index = capture->dispatch_next;
                record_bytes = 0U;
                subscription = capture->subscriptions[index];

                capture->dispatch_next = (index + 1U) % capture->subscription_count;
                if (subscription->removing) {
                    continue;
                }
                if (subscription->state_pending) {
                    snapshot = subscription->snapshot;
                    subscription->state_pending = QM_FALSE;
                    deliver_state = QM_TRUE;
                } else {
                    if (subscription->config.delivery != QM_AUDIO_CAPTURE_DELIVERY_CALLBACK ||
                        subscription->snapshot.state != QM_AUDIO_CAPTURE_RUNNING ||
                        subscription->queued_records == 0U) {
                        continue;
                    }
                    if (peek_record_locked(subscription, &record_bytes) != QM_EOK ||
                        pop_record_locked(subscription, capture->callback_buffer,
                                           record_bytes, &frame) != QM_EOK) {
                        continue;
                    }
                }
                selected = subscription;
                selected->callback_active = QM_TRUE;
                break;
            }
            (void)qm_mutex_unlock(&capture->lock);
            if (selected == NULL) {
                break;
            }
            if (deliver_state) {
                selected->config.state_cb(&snapshot, selected->config.user_ctx);
            } else if (capture->config.input_format.codec == QM_AUDIO_CODEC_PCM) {
                selected->config.frame_cb(&frame, selected->config.user_ctx);
            } else {
                selected->config.packet_cb(&frame, selected->config.user_ctx);
            }
            (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
            selected->callback_active = QM_FALSE;
            (void)qm_sem_signal(&capture->quiesced);
            (void)qm_mutex_unlock(&capture->lock);
        }
    }
}

#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
static void capture_task(void *arg)
{
    qm_audio_capture_t *capture = (qm_audio_capture_t *)arg;
    qm_task_t self = {0};
    int stop_ret = QM_EOK;
    qm_audio_data_t input = {0};
    int read_ret = QM_EOK;

    for (;;) {
        input = (qm_audio_data_t){0};
        read_ret = QM_EOK;

        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
        if (!capture->running) {
            (void)qm_mutex_unlock(&capture->lock);
            break;
        }
        (void)qm_mutex_unlock(&capture->lock);
        read_ret = qm_audio_normalize_result(qm_audio_device_capture_read(
            capture->config.device_ctx, &input, CAPTURE_READ_TIMEOUT_MS));
        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
        if (capture->running) {
            if (read_ret == QM_EOK) {
                if (capture_input_valid(capture, &input)) {
                    enqueue_locked(capture, &input);
                } else {
                    capture_source_failed_locked(capture, -QM_EIO);
                }
            } else if (read_ret != -QM_ETIMEOUT) {
                capture_source_failed_locked(capture, read_ret);
            }
        }
        (void)qm_mutex_unlock(&capture->lock);
    }

    /* The reader owns stop until it has finished copying borrowed data.
     * / 读取任务完成借用数据复制后负责停止；生命周期调用方先等它退出。 */
    (void)qm_audio_device_capture_abort(capture->config.device_ctx);
    stop_ret = qm_audio_normalize_result(qm_audio_device_capture_stop(capture->config.device_ctx));
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    capture->stop_result = stop_ret;
    if (stop_ret == QM_EOK) {
        capture->device_stop_pending = QM_FALSE;
    } else {
        capture_source_failed_locked(capture, stop_ret);
    }
    self = capture->task;
    capture->task = (qm_task_t){0};
    capture->task_active = QM_FALSE;
    (void)qm_sem_signal(&capture->quiesced);
    (void)qm_mutex_unlock(&capture->lock);
    qm_task_exit(&self);
}
#endif

/* Lifecycle ownership excludes a concurrent start/stop. Device calls never hold
 * capture->lock: PUSH stop must be able to wait for feed using that lock.
 * / 生命周期所有权排除并发启停；设备调用不持实例锁，PUSH stop 才能等待 feed 返回。 */
static int stop_device(qm_audio_capture_t *capture, qm_utils_time_t *deadline, uint32_t timeout_ms)
{
    qm_bool pending = QM_FALSE;
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    qm_bool reader_active = QM_FALSE;
#endif
    int ret = QM_EOK;

    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    capture->running = QM_FALSE;
    pending = capture->device_stop_pending;
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    reader_active = capture->task_active;
#endif
    (void)qm_mutex_unlock(&capture->lock);
    if (!pending) {
        return QM_EOK;
    }
    (void)qm_audio_device_capture_abort(capture->config.device_ctx);
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    if (reader_active) {
        ret = wait_idle(capture, &capture->task_active, deadline, timeout_ms);
        if (ret != QM_EOK) {
            return ret;
        }
        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
        ret = capture->stop_result;
        (void)qm_mutex_unlock(&capture->lock);
        return ret;
    }
#else
    (void)deadline;
    (void)timeout_ms;
#endif
    ret = qm_audio_normalize_result(qm_audio_device_capture_stop(capture->config.device_ctx));
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    capture->stop_result = ret;
    if (ret == QM_EOK) {
        capture->device_stop_pending = QM_FALSE;
    }
    (void)qm_sem_signal(&capture->quiesced);
    (void)qm_mutex_unlock(&capture->lock);
    return ret;
}

static int start_device(qm_audio_capture_t *capture)
{
    qm_utils_time_t deadline = {0};
    qm_bool running = QM_FALSE;
    int ret = QM_EOK;

    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    running = capture->running;
    (void)qm_mutex_unlock(&capture->lock);
    if (running) {
        return QM_EOK;
    }
    qm_utils_time_countdown_ms(&deadline, CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS);
    ret = stop_device(capture, &deadline, CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS);
    if (ret != QM_EOK) {
        return ret;
    }
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    capture->source_error = QM_EOK;
    capture->device_stop_pending = QM_TRUE;
    capture->running = QM_TRUE;
    (void)qm_mutex_unlock(&capture->lock);
    ret = qm_audio_normalize_result(qm_audio_device_capture_start(capture->config.device_ctx));
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (ret == QM_EOK) {
        ret = capture->source_error;
    }
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
    if (ret == QM_EOK) {
        capture->task_active = QM_TRUE;
        ret = qm_task_new(&capture->task, "audio_capture", capture_task, capture,
                          CONFIG_QM_AUDIO_CAPTURE_TASK_STACK_SIZE,
                          CONFIG_QM_AUDIO_CAPTURE_TASK_PRIORITY);
        if (ret != QM_EOK) {
            capture->task_active = QM_FALSE;
        }
    }
#endif
    if (ret != QM_EOK) {
        capture_source_failed_locked(capture, ret);
    }
    (void)qm_mutex_unlock(&capture->lock);
    if (ret != QM_EOK) {
        /* Failed start still owns cleanup; preserve pending on stop failure.
         * / 启动失败仍须清理，停止失败时保留待清理状态。 */
        (void)stop_device(capture, &deadline, CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS);
    }
    return ret;
}

int qm_audio_capture_get_frame_bytes(const qm_audio_capture_t *capture, uint32_t *bytes)
{
    if (capture == NULL || bytes == NULL) {
        return -QM_EINVAL;
    }
    *bytes = capture->config.frame_bytes;
    return QM_EOK;
}

int qm_audio_capture_create(const qm_audio_capture_open_config_t *config, qm_audio_capture_t **out_capture)
{
    int ret = QM_EOK;
    uint32_t queue_len = 0U;
    uint32_t storage_bytes = 0U;
    qm_audio_capture_t *capture = NULL;

    if (out_capture == NULL) {
        return -QM_EINVAL;
    }
    *out_capture = NULL;
#if !CONFIG_QM_AUDIO_CAPTURE_SUPPORT
    (void)config;
    return -QM_ENOSYS;
#endif
    if (config == NULL || config->device_ctx == NULL || config->frame_bytes == 0U ||
        !qm_audio_stream_format_valid(&config->input_format) ||
        qm_audio_encoded_bytes(&config->input_format) ||
        (config->input_format.codec == QM_AUDIO_CODEC_PCM &&
         config->frame_bytes % qm_audio_pcm_frame_bytes(&config->input_format.pcm) != 0U) ||
        (config->input_format.codec != QM_AUDIO_CODEC_PCM &&
         qm_audio_encoded_packets(&config->input_format) &&
         config->frame_bytes < config->input_format.max_packet_bytes)) {
        return -QM_EINVAL;
    }
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
    if (push_capture != NULL) {
        return -QM_EBUSY;
    }
#endif

    queue_len = config->queue_len == 0U ? CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN : config->queue_len;
    ret = capture_subscription_storage_bytes(config->frame_bytes, queue_len, &storage_bytes);
    if (ret != QM_EOK) {
        return ret;
    }

    capture = qm_malloc(sizeof(*capture));
    if (capture == NULL) {
        return -QM_ENOMEM;
    }
    qm_memset(capture, 0, sizeof(*capture));
    capture->config = *config;
    capture->config.queue_len = queue_len;
    ret = qm_mutex_new(&capture->lock);
    if (ret != QM_EOK) {
        goto fail;
    }
    ret = qm_sem_new(&capture->quiesced, 0U);
    if (ret != QM_EOK) {
        goto fail;
    }
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
    push_capture = capture;
#endif
    *out_capture = capture;
    return QM_EOK;

fail:
    if (qm_sem_is_valid(&capture->quiesced)) {
        (void)qm_sem_free(&capture->quiesced);
    }
    if (qm_mutex_is_valid(&capture->lock)) {
        (void)qm_mutex_free(&capture->lock);
    }
    qm_free(capture);
    return ret;
}

int qm_audio_capture_shutdown(qm_audio_capture_t *capture, uint32_t timeout_ms)
{
    qm_utils_time_t deadline = {0};
    uint32_t index = 0U;
    qm_audio_capture_subscription_t *subscription = NULL;
    int ret = QM_EOK;

    if (capture == NULL) {
        return QM_EOK;
    }
    if (timeout_ms != QM_WAIT_FOREVER) {
        qm_utils_time_countdown_ms(&deadline, timeout_ms);
    }
    ret = enter_lifecycle(capture);
    if (ret != QM_EOK) {
        return ret;
    }
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    capture->closing = QM_TRUE;
    capture->running = QM_FALSE;
    for (index = 0U; index < capture->subscription_count; ++index) {
        subscription = capture->subscriptions[index];

        clear_queue_locked(subscription);
        capture_set_state_locked(capture, subscription, QM_AUDIO_CAPTURE_STOPPED, QM_EOK);
        (void)qm_sem_signal(&subscription->data_ready);
    }
    capture_signal_dispatch(capture);
    (void)qm_mutex_unlock(&capture->lock);

    ret = stop_device(capture, &deadline, timeout_ms);
    if (ret == QM_EOK) {
        ret = wait_idle(capture, &capture->dispatch_active, &deadline, timeout_ms);
    }
    if (ret == QM_EOK) {
        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
        capture->stopped = QM_TRUE;
        (void)qm_mutex_unlock(&capture->lock);
    }
    leave_lifecycle(capture);
    return ret;
}

int qm_audio_capture_destroy(qm_audio_capture_t *capture)
{
    int ret = QM_EOK;

    if (capture == NULL) {
        return QM_EOK;
    }
    ret = enter_lifecycle(capture);
    if (ret != QM_EOK) {
        return ret;
    }
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (!capture->stopped || capture->subscription_count != 0U ||
        capture->device_stop_pending || capture->dispatch_active
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PULL
        || capture->task_active
#endif
        ) {
        ret = -QM_EBUSY;
    }
    (void)qm_mutex_unlock(&capture->lock);
    if (ret != QM_EOK) {
        leave_lifecycle(capture);
        return ret;
    }
#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
    push_capture = NULL;
#endif
    if (qm_sem_is_valid(&capture->dispatch_wake)) {
        (void)qm_sem_free(&capture->dispatch_wake);
    }
    (void)qm_sem_free(&capture->quiesced);
    (void)qm_mutex_free(&capture->lock);
    qm_free(capture->callback_buffer);
    qm_free(capture);
    return QM_EOK;
}

int qm_audio_capture_enable(qm_audio_capture_subscription_t *subscription, qm_bool enabled)
{
    qm_audio_capture_t *capture = NULL;
    qm_bool need_start = QM_FALSE;
    int ret = QM_EOK;

    if (subscription == NULL || subscription->capture == NULL ||
        (enabled != QM_TRUE && enabled != QM_FALSE)) {
        return -QM_EINVAL;
    }
    capture = subscription->capture;
    ret = enter_lifecycle(capture);
    if (ret != QM_EOK) {
        return ret;
    }
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (capture->closing || subscription->removing ||
        subscription->snapshot.state == QM_AUDIO_CAPTURE_STOPPED ||
        subscription->snapshot.state == QM_AUDIO_CAPTURE_ERROR) {
        ret = -QM_EBUSY;
    } else if (!enabled) {
        clear_queue_locked(subscription);
        capture_set_state_locked(capture, subscription, QM_AUDIO_CAPTURE_DISABLED, QM_EOK);
        (void)qm_sem_signal(&subscription->data_ready);
    } else if (capture->running) {
        capture_set_state_locked(capture, subscription, QM_AUDIO_CAPTURE_RUNNING, QM_EOK);
    } else {
        need_start = QM_TRUE;
    }
    (void)qm_mutex_unlock(&capture->lock);
    if (need_start) {
        ret = start_device(capture);
        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
        if (ret == QM_EOK) {
            ret = subscription->snapshot.state == QM_AUDIO_CAPTURE_ERROR ?
                      subscription->snapshot.result : capture->source_error;
        }
        if (ret == QM_EOK) {
            capture_set_state_locked(capture, subscription, QM_AUDIO_CAPTURE_RUNNING, QM_EOK);
        }
        (void)qm_mutex_unlock(&capture->lock);
    }
    leave_lifecycle(capture);
    return ret;
}

int qm_audio_capture_flush(qm_audio_capture_subscription_t *subscription)
{
    qm_audio_capture_t *capture = NULL;
    int ret = QM_EOK;

    if (subscription == NULL || subscription->capture == NULL) {
        return -QM_EINVAL;
    }
    capture = subscription->capture;
    ret = qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (ret != QM_EOK) {
        return ret;
    }
    if (capture->closing || subscription->removing ||
        subscription->snapshot.state == QM_AUDIO_CAPTURE_STOPPED ||
        subscription->snapshot.state == QM_AUDIO_CAPTURE_ERROR) {
        (void)qm_mutex_unlock(&capture->lock);
        return -QM_EBUSY;
    }
    clear_queue_locked(subscription);
    (void)qm_mutex_unlock(&capture->lock);
    return ret;
}

int qm_audio_capture_get_snapshot(qm_audio_capture_subscription_t *subscription, qm_audio_capture_snapshot_t *snapshot)
{
    qm_audio_capture_t *capture = NULL;
    int ret = QM_EOK;

    if (subscription == NULL || subscription->capture == NULL || snapshot == NULL) {
        return -QM_EINVAL;
    }
    capture = subscription->capture;
    ret = qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (ret != QM_EOK) {
        return ret;
    }
    *snapshot = subscription->snapshot;
    (void)qm_mutex_unlock(&capture->lock);
    return QM_EOK;
}

int qm_audio_capture_peek_size(qm_audio_capture_subscription_t *subscription,
                               uint32_t *size)
{
    qm_audio_capture_t *capture = NULL;
    uint32_t record_bytes = 0U;
    int ret = QM_EOK;

    if (size == NULL) {
        return -QM_EINVAL;
    }
    *size = 0U;
    if (subscription == NULL || subscription->capture == NULL ||
        subscription->config.delivery != QM_AUDIO_CAPTURE_DELIVERY_QUEUE) {
        return -QM_EINVAL;
    }

    capture = subscription->capture;
    ret = qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (ret != QM_EOK) {
        return ret;
    }

    if (subscription->removing ||
        subscription->snapshot.state == QM_AUDIO_CAPTURE_STOPPED) {
        ret = -QM_EINTR;
    } else if (subscription->snapshot.state == QM_AUDIO_CAPTURE_ERROR) {
        ret = subscription->snapshot.result;
    } else if (subscription->snapshot.state == QM_AUDIO_CAPTURE_DISABLED) {
        ret = -QM_EBUSY;
    } else if (subscription->queued_records == 0U) {
        ret = -QM_EEMPTY;
    } else {
        ret = peek_record_locked(subscription, &record_bytes);
        if (ret == QM_EOK) {
            *size = record_bytes - (uint32_t)sizeof(capture_record_metadata_t);
        }
    }

    (void)qm_mutex_unlock(&capture->lock);
    return ret;
}

int qm_audio_capture_recv(qm_audio_capture_subscription_t *subscription,
                          qm_audio_data_t *frame, uint32_t timeout_ms)
{
    qm_audio_capture_t *capture = NULL;
    qm_utils_time_t deadline = {0};
    uint8_t *record = NULL;
    void *output_data = NULL;
    uint32_t capacity_bytes = 0U;
    uint32_t record_bytes = 0U;
    int wait_ret = QM_EOK;
    int ret = QM_EOK;

    if (frame == NULL) {
        return -QM_EINVAL;
    }
    output_data = (void *)frame->data;
    capacity_bytes = frame->size;
    frame->size = 0U;
    if (subscription == NULL || subscription->capture == NULL ||
        subscription->config.delivery != QM_AUDIO_CAPTURE_DELIVERY_QUEUE ||
        output_data == NULL || capacity_bytes == 0U) {
        return -QM_EINVAL;
    }
    capture = subscription->capture;
    if (timeout_ms != QM_WAIT_FOREVER) {
        qm_utils_time_countdown_ms(&deadline, timeout_ms);
    }
    ret = qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (ret != QM_EOK) {
        return ret;
    }
    if (subscription->recv_active) {
        (void)qm_mutex_unlock(&capture->lock);
        return -QM_EBUSY;
    }
    subscription->recv_active = QM_TRUE;
    for (;;) {
        if (subscription->removing || subscription->snapshot.state == QM_AUDIO_CAPTURE_STOPPED) {
            ret = -QM_EINTR;
            break;
        }
        if (subscription->snapshot.state == QM_AUDIO_CAPTURE_ERROR) {
            ret = subscription->snapshot.result;
            break;
        }
        if (capture->closing) {
            ret = -QM_EINTR;
            break;
        }
        if (subscription->snapshot.state == QM_AUDIO_CAPTURE_DISABLED) {
            ret = -QM_EBUSY;
            break;
        }
        if (subscription->queued_records != 0U) {
            ret = peek_record_locked(subscription, &record_bytes);
            if (ret != QM_EOK) {
                break;
            }
            if (record_bytes - (uint32_t)sizeof(capture_record_metadata_t) > capacity_bytes) {
                if (record_bytes - (uint32_t)sizeof(capture_record_metadata_t) >
                    capture->config.frame_bytes) {
                    fail_subscription_locked(subscription, -QM_EIO);
                    ret = -QM_EIO;
                } else {
                    ret = -QM_EINVAL;
                }
                break;
            }
            record = qm_malloc(record_bytes);
            if (record == NULL) {
                ret = -QM_ENOMEM;
                break;
            }
            ret = pop_record_locked(subscription, record, record_bytes, frame);
            if (ret == QM_EOK) {
                qm_memcpy(output_data, frame->data, frame->size);
            }
            frame->data = output_data;
            qm_free(record);
            break;
        }
        if (wait_ret != QM_EOK ||
            (timeout_ms != QM_WAIT_FOREVER && qm_utils_time_left(&deadline) == 0U)) {
            ret = -QM_ETIMEOUT;
            break;
        }
        /* Notification is a hint, not a record count. Clear it with the queue
         * locked so a producer cannot race the transition into wait.
         * / 通知不代表记录数；持队列锁清除旧通知，避免与生产者进入等待的时序竞争。 */
        capture_drain_notification(&subscription->data_ready);
        (void)qm_mutex_unlock(&capture->lock);
        wait_ret = qm_sem_wait(&subscription->data_ready,
                               timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                               qm_utils_time_left(&deadline));
        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    }
    subscription->recv_active = QM_FALSE;
    (void)qm_sem_signal(&capture->quiesced);
    (void)qm_mutex_unlock(&capture->lock);
    return ret;
}

#if CONFIG_QM_AUDIO_CAPTURE_MODE_PUSH
int qm_audio_capture_feed(const qm_audio_data_t *input, int result)
{
    qm_audio_capture_t *capture = push_capture;
    int ret = QM_EOK;

    if (capture == NULL) {
        return -QM_EBUSY;
    }
    if (result > QM_EOK || (result != QM_EOK && input != NULL)) {
        return -QM_EINVAL;
    }
    ret = qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (ret != QM_EOK) {
        return ret;
    }
    if (capture->closing || !capture->running) {
        ret = -QM_EBUSY;
    } else if (result != QM_EOK) {
        capture_source_failed_locked(capture, result);
        ret = result;
    } else if (!capture_input_valid(capture, input)) {
        ret = -QM_EINVAL;
    } else {
        enqueue_locked(capture, input);
    }
    (void)qm_mutex_unlock(&capture->lock);
    return ret;
}
#endif

int qm_audio_capture_subscribe(qm_audio_capture_t *capture, const qm_audio_capture_config_t *config,
                               qm_audio_capture_subscription_t **out_subscription)
{
    qm_utils_time_t deadline = {0};
    int ret = QM_EOK;
    uint32_t queue_len = 0U;
    uint32_t storage_bytes = 0U;
    uint32_t index = 0U;
    qm_bool needs_dispatcher = QM_FALSE;
    qm_audio_capture_subscription_t *subscription = NULL;

    if (out_subscription == NULL) {
        return -QM_EINVAL;
    }
    *out_subscription = NULL;
    if (capture == NULL) {
        return -QM_ENOSYS;
    }
    if (config == NULL ||
        (config->enabled != QM_TRUE && config->enabled != QM_FALSE)) {
        return -QM_EINVAL;
    }
    if (config->delivery == QM_AUDIO_CAPTURE_DELIVERY_QUEUE) {
        if (config->frame_cb != NULL || config->packet_cb != NULL) {
            return -QM_EINVAL;
        }
    } else if (config->delivery == QM_AUDIO_CAPTURE_DELIVERY_CALLBACK) {
        if (capture->config.input_format.codec == QM_AUDIO_CODEC_PCM) {
            if (config->frame_cb == NULL || config->packet_cb != NULL) {
                return -QM_EINVAL;
            }
        } else if (config->packet_cb == NULL || config->frame_cb != NULL) {
            return -QM_EINVAL;
        }
    } else {
        return -QM_EINVAL;
    }
    needs_dispatcher = config->delivery == QM_AUDIO_CAPTURE_DELIVERY_CALLBACK ||
                       config->state_cb != NULL;

    queue_len = config->queue_len == 0U ? capture->config.queue_len : config->queue_len;
    ret = capture_subscription_storage_bytes(capture->config.frame_bytes, queue_len, &storage_bytes);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = enter_lifecycle(capture);
    if (ret != QM_EOK) {
        return ret;
    }
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (capture->closing) {
        ret = -QM_EBUSY;
    } else if (capture->subscription_count >= CONFIG_QM_AUDIO_CAPTURE_MAX_SUBSCRIBERS) {
        ret = -QM_EFULL;
    }
    (void)qm_mutex_unlock(&capture->lock);
    if (ret != QM_EOK) {
        goto done;
    }
    subscription = qm_malloc(sizeof(*subscription));
    if (subscription == NULL) {
        ret = -QM_ENOMEM;
        goto done;
    }
    qm_memset(subscription, 0, sizeof(*subscription));
    subscription->storage = qm_malloc(storage_bytes);
    if (subscription->storage == NULL) {
        ret = -QM_ENOMEM;
        goto done;
    }
    ret = qm_ring_queue_init(&subscription->ring, subscription->storage, storage_bytes);
    if (ret != QM_EOK) {
        goto done;
    }
    ret = qm_sem_new(&subscription->data_ready, 0U);
    if (ret != QM_EOK) {
        goto done;
    }
    subscription->capture = capture;
    subscription->config = *config;
    subscription->snapshot.state = config->enabled ? QM_AUDIO_CAPTURE_RUNNING :
                                                     QM_AUDIO_CAPTURE_DISABLED;
    subscription->snapshot.queue_len = queue_len;
    subscription->state_pending = config->state_cb != NULL;
    if (needs_dispatcher) {
        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
        ret = capture_start_dispatcher_locked(capture,
                                              config->delivery == QM_AUDIO_CAPTURE_DELIVERY_CALLBACK);
        (void)qm_mutex_unlock(&capture->lock);
        if (ret != QM_EOK) {
            goto done;
        }
    }
    if (config->enabled) {
        ret = start_device(capture);
        if (ret != QM_EOK) {
            goto done;
        }
    }
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    if (config->enabled && capture->source_error != QM_EOK) {
        ret = capture->source_error;
    } else {
        /* Publication commits ownership only after start succeeded. A failed
         * subscription never invokes its callbacks. / 启动成功后才发布所有权；
         * 订阅失败不会调用该订阅的回调。 */
        index = capture->subscription_count;
        capture->subscriptions[index] = subscription;
        ++capture->subscription_count;
        *out_subscription = subscription;
        capture_signal_dispatch(capture);
    }
    (void)qm_mutex_unlock(&capture->lock);
    if (ret != QM_EOK) {
        /* A source failure before publication still belongs to this start.
         * / 发布前的源故障仍须清理本次启动，保留原始错误。 */
        qm_utils_time_countdown_ms(&deadline, CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS);
        (void)stop_device(capture, &deadline, CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS);
    }

done:
    if (ret != QM_EOK) {
        capture_subscription_free(subscription);
    }
    leave_lifecycle(capture);
    return ret;
}

int qm_audio_capture_unsubscribe(qm_audio_capture_subscription_t *subscription)
{
    qm_audio_capture_t *capture = NULL;
    qm_utils_time_t deadline = {0};
    qm_bool last_subscription = QM_FALSE;
    uint32_t timeout_ms = CONFIG_QM_AUDIO_CAPTURE_STOP_TIMEOUT_MS;
    uint32_t index = 0U;
    uint32_t move_index = 0U;
    int ret = QM_EOK;

    if (subscription == NULL || subscription->capture == NULL) {
        return -QM_EINVAL;
    }
    capture = subscription->capture;
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    ret = enter_lifecycle(capture);
    if (ret != QM_EOK) {
        return ret;
    }
    (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
    for (index = 0U; index < capture->subscription_count; ++index) {
        if (capture->subscriptions[index] == subscription) {
            break;
        }
    }
    if (index == capture->subscription_count) {
        (void)qm_mutex_unlock(&capture->lock);
        leave_lifecycle(capture);
        return -QM_EINVAL;
    }
    subscription->removing = QM_TRUE;
    subscription->state_pending = QM_FALSE;
    clear_queue_locked(subscription);
    capture_set_state_locked(capture, subscription, QM_AUDIO_CAPTURE_STOPPED, QM_EOK);
    (void)qm_sem_signal(&subscription->data_ready);
    last_subscription = capture->subscription_count == 1U;
    if (last_subscription) {
        capture->running = QM_FALSE;
    }
    (void)qm_mutex_unlock(&capture->lock);

    if (last_subscription) {
        ret = stop_device(capture, &deadline, timeout_ms);
    }
    if (ret == QM_EOK) {
        ret = wait_idle(capture, &subscription->recv_active, &deadline, timeout_ms);
    }
    if (ret == QM_EOK) {
        ret = wait_idle(capture, &subscription->callback_active, &deadline, timeout_ms);
    }
    if (ret == QM_EOK) {
        (void)qm_mutex_lock(&capture->lock, QM_WAIT_FOREVER);
        for (move_index = index; move_index + 1U < capture->subscription_count; ++move_index) {
            capture->subscriptions[move_index] = capture->subscriptions[move_index + 1U];
        }
        --capture->subscription_count;
        capture->subscriptions[capture->subscription_count] = NULL;
        if (capture->dispatch_next > index) {
            --capture->dispatch_next;
        }
        if (capture->dispatch_next >= capture->subscription_count) {
            capture->dispatch_next = 0U;
        }
        (void)qm_mutex_unlock(&capture->lock);
        capture_subscription_free(subscription);
    }
    leave_lifecycle(capture);
    return ret;
}
