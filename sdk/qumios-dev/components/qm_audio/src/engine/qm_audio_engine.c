/**
 * @file qm_audio_engine.c
 * @brief Native device ownership and serialized rendering. / 原生设备所有权与串行播放。
 */
#include "qm_audio_priv.h"
#include "qm_utils_timer.h"

#ifndef CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_STACK_SIZE
#define CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_STACK_SIZE 4096
#endif
#ifndef CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_PRIORITY
#define CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_PRIORITY 5
#endif

struct qm_audio_render_packet {
    qm_audio_render_stream_t *stream;
    qm_audio_data_t input;
    uint8_t data[];
};

/* Coalesce progress notifications; lock-protected durable state is authoritative.
 * / 合并进度通知，完成条件以相应锁保护的持久状态为准。 */
static void notify(qm_sem_t *sem)
{
    (void)qm_sem_wait(sem, QM_NO_WAIT);
    (void)qm_sem_signal(sem);
}

static void wake_render(qm_audio_engine_t *engine)
{
    notify(&engine->render_wake_sem);
}

static void signal_render_idle(qm_audio_engine_t *engine)
{
    notify(&engine->render_idle_sem);
    if (engine->render != NULL && engine->render->service_progress_sem != NULL) {
        notify(engine->render->service_progress_sem);
    }
    wake_render(engine);
}

/* All ordinary device calls reserve I/O; only abort may overlap them.
 * / 普通设备调用均登记在途 I/O，只有 abort 允许与其并发。 */
static void render_process_action(qm_audio_engine_t *engine)
{
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_render_action_t action = QM_AUDIO_RENDER_ACTION_NONE;
    uint32_t position_ms = 0U;
    qm_bool position_valid = QM_FALSE;
    int ret = QM_EOK;

    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    stream = engine->render;
    if (stream == NULL || !stream->action_pending || engine->render_inflight != 0U ||
        (stream->action == QM_AUDIO_RENDER_ACTION_STOP && stream->pending_packets != 0U)) {
        (void)qm_mutex_unlock(&engine->lock);
        return;
    }
    action = stream->action;
    engine->render_inflight++;
    (void)qm_mutex_unlock(&engine->lock);

    switch (action) {
        case QM_AUDIO_RENDER_ACTION_START:
            ret = qm_audio_device_playback_start(engine->backend_ctx, stream->delivery);
            break;
        case QM_AUDIO_RENDER_ACTION_RESUME:
            ret = qm_audio_device_playback_resume(engine->backend_ctx);
            break;
        case QM_AUDIO_RENDER_ACTION_PAUSE:
            ret = qm_audio_device_playback_pause(engine->backend_ctx);
            break;
        case QM_AUDIO_RENDER_ACTION_STOP:
            /* START may have reset an earlier abort. / START 可能重置先前的中断。 */
            (void)qm_audio_device_playback_abort(engine->backend_ctx);
            ret = qm_audio_device_playback_stop(engine->backend_ctx);
            break;
        default:
            ret = -QM_EINVAL;
            break;
    }
    ret = qm_audio_normalize_result(ret);
    if (ret == QM_EOK &&
        (action == QM_AUDIO_RENDER_ACTION_STOP || action == QM_AUDIO_RENDER_ACTION_PAUSE) &&
        (engine->caps.flags & QM_AUDIO_DEVICE_CAP_POSITION) != 0U) {
        position_valid = qm_audio_device_playback_get_position(engine->backend_ctx,
                                                              &position_ms) == QM_EOK;
    }
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    engine->render_inflight--;
    /* A completed START/PAUSE must not overwrite a newer STOP request.
     * / 较晚完成的 START/PAUSE 不得覆盖已经提交的 STOP。 */
    if (stream->action == action && stream->action_pending) {
        stream->action_result = ret;
        stream->action_pending = QM_FALSE;
        if (position_valid) {
            stream->cached_position_valid = QM_TRUE;
            stream->cached_position_ms = position_ms;
        }
        if (action == QM_AUDIO_RENDER_ACTION_STOP && ret == QM_EOK) {
            stream->state = QM_AUDIO_RENDER_STATE_STOPPED;
        } else if (action == QM_AUDIO_RENDER_ACTION_START) {
            stream->state = ret == QM_EOK ? QM_AUDIO_RENDER_STATE_ACTIVE :
                                           QM_AUDIO_RENDER_STATE_STOPPING;
        } else if (action == QM_AUDIO_RENDER_ACTION_RESUME && ret == QM_EOK) {
            stream->state = QM_AUDIO_RENDER_STATE_ACTIVE;
        } else if (action == QM_AUDIO_RENDER_ACTION_PAUSE && ret != QM_EOK) {
            stream->state = QM_AUDIO_RENDER_STATE_ACTIVE;
        }
    }
    notify(&engine->render_action_sem);
    signal_render_idle(engine);
    (void)qm_mutex_unlock(&engine->lock);
    qm_audio_event_wake(engine->audio);
}

/* Queries keep their own result so Service can still observe a completed action.
 * / 查询单独保存结果，Service 仍可读取先前完成的控制动作。 */
static void render_process_position(qm_audio_engine_t *engine)
{
    qm_audio_render_stream_t *stream = NULL;
    uint32_t position_ms = 0U;
    int ret = -QM_EINTR;

    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    stream = engine->render;
    if (stream == NULL || !stream->position_pending) {
        (void)qm_mutex_unlock(&engine->lock);
        return;
    }
    if (engine->state == QM_AUDIO_ENGINE_STATE_OPEN &&
        stream->state != QM_AUDIO_RENDER_STATE_STOPPING && stream->state != QM_AUDIO_RENDER_STATE_STOPPED) {
        if (engine->render_inflight != 0U || stream->action_pending) {
            (void)qm_mutex_unlock(&engine->lock);
            return;
        }
        engine->render_inflight++;
        (void)qm_mutex_unlock(&engine->lock);
        ret = qm_audio_normalize_result(qm_audio_device_playback_get_position(engine->backend_ctx,
                                                                            &position_ms));
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        engine->render_inflight--;
        if (engine->state != QM_AUDIO_ENGINE_STATE_OPEN || stream->state == QM_AUDIO_RENDER_STATE_STOPPING) {
            ret = -QM_EINTR;
        }
    }
    stream->position_result = ret;
    stream->position_pending = QM_FALSE;
    if (ret == QM_EOK) {
        stream->cached_position_ms = position_ms;
        stream->cached_position_valid = QM_TRUE;
    }
    notify(&engine->render_position_sem);
    signal_render_idle(engine);
    (void)qm_mutex_unlock(&engine->lock);
    qm_audio_event_wake(engine->audio);
}

static void render_process_setting(qm_audio_engine_t *engine)
{
    qm_audio_render_setting_t setting = QM_AUDIO_RENDER_SETTING_NONE;
    uint8_t value = 0U;
    int ret = QM_EOK;

    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (!engine->setting_pending) {
        (void)qm_mutex_unlock(&engine->lock);
        return;
    }
    if (engine->state != QM_AUDIO_ENGINE_STATE_OPEN) {
        engine->setting_result = -QM_EINTR;
        engine->setting_pending = QM_FALSE;
        notify(&engine->render_setting_sem);
        (void)qm_mutex_unlock(&engine->lock);
        qm_audio_event_wake(engine->audio);
        return;
    }
    if (engine->render_inflight != 0U ||
        (engine->render != NULL &&
         (engine->render->drain_active || engine->render->action_pending))) {
        (void)qm_mutex_unlock(&engine->lock);
        return;
    }
    setting = engine->setting;
    value = engine->setting_value;
    engine->render_inflight++;
    (void)qm_mutex_unlock(&engine->lock);

    ret = setting == QM_AUDIO_RENDER_SETTING_VOLUME ?
        qm_audio_device_set_volume(engine->backend_ctx, value) :
        qm_audio_device_set_mute(engine->backend_ctx, (qm_bool)value);

    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    engine->render_inflight--;
    engine->setting_result = qm_audio_normalize_result(ret);
    engine->setting_pending = QM_FALSE;
    notify(&engine->render_setting_sem);
    signal_render_idle(engine);
    (void)qm_mutex_unlock(&engine->lock);
    qm_audio_event_wake(engine->audio);
}

int qm_audio_engine_render_setting_request(qm_audio_engine_t *engine,
                                          qm_audio_render_setting_t setting, uint8_t value)
{
    uint32_t capability = 0U;

    if (engine == NULL ||
        (setting != QM_AUDIO_RENDER_SETTING_VOLUME && setting != QM_AUDIO_RENDER_SETTING_MUTE) ||
        (setting == QM_AUDIO_RENDER_SETTING_VOLUME && value > 100U) ||
        (setting == QM_AUDIO_RENDER_SETTING_MUTE && value > (uint8_t)QM_TRUE)) {
        return -QM_EINVAL;
    }
    capability = setting == QM_AUDIO_RENDER_SETTING_VOLUME ?
        QM_AUDIO_DEVICE_CAP_VOLUME : QM_AUDIO_DEVICE_CAP_MUTE;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->state != QM_AUDIO_ENGINE_STATE_OPEN || engine->setting_pending) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    if ((engine->caps.flags & capability) == 0U || !engine->render_task_started) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_ENOSYS;
    }
    engine->setting = setting;
    engine->setting_value = value;
    engine->setting_result = QM_EOK;
    engine->setting_pending = QM_TRUE;
    wake_render(engine);
    (void)qm_mutex_unlock(&engine->lock);
    return QM_EOK;
}

int qm_audio_engine_render_setting_wait(qm_audio_engine_t *engine,
                                       qm_audio_render_setting_t setting, uint8_t value)
{
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    qm_bool expired = QM_FALSE;
    int ret = QM_EOK;

    if (engine == NULL) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
    for (;;) {
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        if (engine->setting != setting || engine->setting_value != value) {
            (void)qm_mutex_unlock(&engine->lock);
            return -QM_EINTR;
        }
        if (!engine->setting_pending) {
            ret = engine->setting_result;
            (void)qm_mutex_unlock(&engine->lock);
            return ret;
        }
        (void)qm_mutex_unlock(&engine->lock);
        left = qm_utils_time_left(&deadline);
        if (expired || left == 0U) {
            return -QM_ETIMEOUT;
        }
        expired = qm_sem_wait(&engine->render_setting_sem, left) != QM_EOK;
    }
}

int qm_audio_engine_render_request(qm_audio_render_stream_t *stream, qm_audio_render_action_t action)
{
    qm_audio_engine_t *engine = NULL;
    qm_bool abort = QM_FALSE;

    if (stream == NULL || stream->engine == NULL ||
        (action != QM_AUDIO_RENDER_ACTION_PAUSE && action != QM_AUDIO_RENDER_ACTION_STOP &&
         action != QM_AUDIO_RENDER_ACTION_RESUME)) {
        return -QM_EINVAL;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->render != stream) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EINTR;
    }
    if (action == QM_AUDIO_RENDER_ACTION_STOP && stream->state == QM_AUDIO_RENDER_STATE_STOPPED) {
        stream->action = action;
        stream->action_result = QM_EOK;
        (void)qm_mutex_unlock(&engine->lock);
        return QM_EOK;
    }
    if (!engine->render_task_started) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    if (stream->action_pending && stream->action == action) {
        (void)qm_mutex_unlock(&engine->lock);
        return QM_EOK;
    }
    if (action != QM_AUDIO_RENDER_ACTION_STOP) {
        if (engine->state != QM_AUDIO_ENGINE_STATE_OPEN || stream->action_pending ||
            stream->drain_active || stream->state == QM_AUDIO_RENDER_STATE_STOPPING ||
            stream->state == QM_AUDIO_RENDER_STATE_STOPPED ||
            (action == QM_AUDIO_RENDER_ACTION_PAUSE && stream->state != QM_AUDIO_RENDER_STATE_ACTIVE) ||
            (action == QM_AUDIO_RENDER_ACTION_RESUME && stream->state != QM_AUDIO_RENDER_STATE_PAUSED)) {
            (void)qm_mutex_unlock(&engine->lock);
            return -QM_EBUSY;
        }
    }
    stream->action = action;
    stream->action_result = QM_EOK;
    stream->action_pending = QM_TRUE;
    if (action == QM_AUDIO_RENDER_ACTION_STOP) {
        stream->state = QM_AUDIO_RENDER_STATE_STOPPING;
        /* Keep the backend alive until this caller finishes aborting I/O.
         * / 调用方完成 abort 前保活后端，worker 不得提前 stop/close。 */
        engine->render_inflight++;
        abort = QM_TRUE;
        notify(&engine->render_space_sem);
        notify(&engine->render_action_sem);
        notify(&engine->render_idle_sem);
    } else if (action == QM_AUDIO_RENDER_ACTION_PAUSE) {
        stream->state = QM_AUDIO_RENDER_STATE_PAUSED;
    }
    wake_render(engine);
    (void)qm_mutex_unlock(&engine->lock);

    if (abort) {
        (void)qm_audio_device_playback_abort(engine->backend_ctx);
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        engine->render_inflight--;
        signal_render_idle(engine);
        (void)qm_mutex_unlock(&engine->lock);
    }
    return QM_EOK;
}

int qm_audio_engine_render_result(qm_audio_render_stream_t *stream,
                                 qm_audio_render_action_t action, int *result)
{
    qm_audio_engine_t *engine = NULL;
    int ret = QM_EOK;

    if (stream == NULL || stream->engine == NULL || result == NULL) {
        return -QM_EINVAL;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->render != stream || stream->action != action) {
        ret = -QM_EINTR;
    } else if (stream->action_pending) {
        ret = -QM_EBUSY;
    } else {
        *result = stream->action_result;
    }
    (void)qm_mutex_unlock(&engine->lock);
    return ret;
}

int qm_audio_engine_render_cached_position(qm_audio_render_stream_t *stream, uint32_t *position_ms)
{
    qm_audio_engine_t *engine = NULL;
    int ret = -QM_ENOSYS;

    if (stream == NULL || stream->engine == NULL || position_ms == NULL) {
        return -QM_EINVAL;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (stream->cached_position_valid && !stream->action_pending) {
        *position_ms = stream->cached_position_ms;
        ret = QM_EOK;
    }
    (void)qm_mutex_unlock(&engine->lock);
    return ret;
}

static int wait_action(qm_audio_render_stream_t *stream, qm_audio_render_action_t action,
                       uint32_t timeout_ms)
{
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    qm_bool expired = QM_FALSE;
    int result = QM_EOK;
    int ret = QM_EOK;

    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        ret = qm_audio_engine_render_result(stream, action, &result);
        if (ret == QM_EOK) {
            return result;
        }
        if (ret != -QM_EBUSY) {
            return ret;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline);
        if (expired || left == 0U) {
            return -QM_ETIMEOUT;
        }
        expired = qm_sem_wait(&stream->engine->render_action_sem, left) != QM_EOK;
    }
}

static int render_control(qm_audio_render_stream_t *stream, qm_audio_render_action_t action,
                          uint32_t timeout_ms)
{
    qm_utils_time_t deadline = {0};
    int ret = QM_EOK;

    if (stream == NULL || stream->engine == NULL) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    ret = qm_audio_engine_render_request(stream, action);
    if (ret != QM_EOK) {
        return ret;
    }
    return wait_action(stream, action, timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline));
}

static qm_bool render_wait_until_writable(qm_audio_engine_t *engine, qm_audio_render_stream_t *stream)
{
    for (;;) {
        render_process_action(engine);
        render_process_position(engine);
        render_process_setting(engine);
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        if (engine->render != stream || engine->state != QM_AUDIO_ENGINE_STATE_OPEN ||
            stream->state == QM_AUDIO_RENDER_STATE_STOPPING ||
            stream->state == QM_AUDIO_RENDER_STATE_STOPPED || stream->result != QM_EOK) {
            (void)qm_mutex_unlock(&engine->lock);
            return QM_FALSE;
        }
        if (engine->render_inflight == 0U && !stream->action_pending &&
            (stream->state == QM_AUDIO_RENDER_STATE_ACTIVE || stream->state == QM_AUDIO_RENDER_STATE_SEALED)) {
            engine->render_inflight++;
            (void)qm_mutex_unlock(&engine->lock);
            return QM_TRUE;
        }
        (void)qm_mutex_unlock(&engine->lock);
        (void)qm_sem_wait(&engine->render_wake_sem, QM_WAIT_FOREVER);
    }
}

static void render_task(void *arg)
{
    qm_audio_engine_t *engine = arg;
    qm_audio_render_packet_t *packet = NULL;
    qm_task_t self = {0};
    unsigned int size = sizeof(packet);
    qm_bool write_packet = QM_FALSE;
    int ret = QM_EOK;

    for (;;) {
        render_process_action(engine);
        render_process_position(engine);
        render_process_setting(engine);
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        if (engine->render_exit_requested) {
            self = engine->render_task;
            engine->render_task = (qm_task_t){0};
            engine->render_task_started = QM_FALSE;
            notify(&engine->render_stopped_sem);
            (void)qm_mutex_unlock(&engine->lock);
            /* No Engine access after publishing completion. / 发布退出后不再访问 Engine。 */
            qm_task_exit(&self);
            return;
        }
        (void)qm_mutex_unlock(&engine->lock);
        packet = NULL;
        size = sizeof(packet);
        if (qm_queue_recv(&engine->render_queue, &packet, &size, QM_NO_WAIT) != QM_EOK) {
            (void)qm_sem_wait(&engine->render_wake_sem, QM_WAIT_FOREVER);
            continue;
        }
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        engine->render_queued--;
        notify(&engine->render_space_sem);
        (void)qm_mutex_unlock(&engine->lock);
        write_packet = render_wait_until_writable(engine, packet->stream);
        ret = QM_EOK;
        if (write_packet) {
            ret = qm_audio_normalize_result(qm_audio_device_playback_write(
                engine->backend_ctx, &packet->input, CONFIG_QM_AUDIO_ENGINE_RENDER_WRITE_TIMEOUT_MS));
        }
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        if (write_packet) {
            engine->render_inflight--;
            if (ret != QM_EOK && packet->stream->state != QM_AUDIO_RENDER_STATE_STOPPING) {
                packet->stream->result = ret;
                notify(&engine->render_space_sem);
            }
        }
        packet->stream->pending_packets--;
        signal_render_idle(engine);
        (void)qm_mutex_unlock(&engine->lock);
        qm_free(packet);
    }
}

static void render_runtime_destroy(qm_audio_engine_t *engine)
{
    qm_sem_free(&engine->render_idle_sem);
    qm_sem_free(&engine->render_action_sem);
    qm_sem_free(&engine->render_setting_sem);
    qm_sem_free(&engine->render_position_sem);
    qm_sem_free(&engine->render_space_sem);
    qm_sem_free(&engine->render_wake_sem);
    qm_sem_free(&engine->render_stopped_sem);
    if (qm_queue_is_valid(&engine->render_queue)) {
        qm_queue_free(&engine->render_queue);
    }
}

static int render_runtime_init(qm_audio_engine_t *engine)
{
    int ret = QM_EOK;

    if (!qm_audio_stream_format_valid(&engine->output_format)) {
        return QM_EOK;
    }
    ret = qm_queue_new(&engine->render_queue, CONFIG_QM_AUDIO_ENGINE_RENDER_QUEUE_LEN,
                       sizeof(qm_audio_render_packet_t *));
    if (ret != QM_EOK ||
        qm_sem_new(&engine->render_idle_sem, 0) != QM_EOK ||
        qm_sem_new(&engine->render_action_sem, 0) != QM_EOK ||
        qm_sem_new(&engine->render_setting_sem, 0) != QM_EOK ||
        qm_sem_new(&engine->render_position_sem, 0) != QM_EOK ||
        qm_sem_new(&engine->render_space_sem, 0) != QM_EOK ||
        qm_sem_new(&engine->render_wake_sem, 0) != QM_EOK ||
        qm_sem_new(&engine->render_stopped_sem, 0) != QM_EOK) {
        render_runtime_destroy(engine);
        return -QM_EIO;
    }
    engine->render_task_started = QM_TRUE;
    ret = qm_task_new(&engine->render_task, "audio_render", render_task, engine,
                      CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_STACK_SIZE,
                      CONFIG_QM_AUDIO_ENGINE_RENDER_TASK_PRIORITY);
    if (ret != QM_EOK) {
        engine->render_task_started = QM_FALSE;
        render_runtime_destroy(engine);
        return -QM_EIO;
    }
    return QM_EOK;
}

static int render_runtime_shutdown(qm_audio_engine_t *engine, uint32_t timeout_ms)
{
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (!engine->render_task_started) {
        (void)qm_mutex_unlock(&engine->lock);
        return QM_EOK;
    }
    engine->render_exit_requested = QM_TRUE;
    wake_render(engine);
    (void)qm_mutex_unlock(&engine->lock);
    return qm_sem_wait(&engine->render_stopped_sem, timeout_ms) == QM_EOK ? QM_EOK : -QM_ETIMEOUT;
}

static int render_reserve_slot(qm_audio_render_stream_t *stream, uint32_t timeout_ms)
{
    qm_audio_engine_t *engine = stream->engine;
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    qm_bool expired = QM_FALSE;
    int ret = QM_EOK;

    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        if (engine->render != stream || engine->state != QM_AUDIO_ENGINE_STATE_OPEN ||
            stream->state == QM_AUDIO_RENDER_STATE_STOPPING || stream->state == QM_AUDIO_RENDER_STATE_STOPPED) {
            ret = -QM_EINTR;
        } else if (stream->result != QM_EOK) {
            ret = stream->result;
        } else if (stream->state != QM_AUDIO_RENDER_STATE_ACTIVE) {
            ret = -QM_EBUSY;
        } else if (engine->render_queued < CONFIG_QM_AUDIO_ENGINE_RENDER_QUEUE_LEN) {
            engine->render_queued++;
            (void)qm_mutex_unlock(&engine->lock);
            return QM_EOK;
        }
        (void)qm_mutex_unlock(&engine->lock);
        if (ret != QM_EOK) {
            return ret;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline);
        if (expired || left == 0U) {
            return -QM_ETIMEOUT;
        }
        expired = qm_sem_wait(&engine->render_space_sem, left) != QM_EOK;
    }
}

static void render_release_slot(qm_audio_engine_t *engine)
{
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    engine->render_queued--;
    notify(&engine->render_space_sem);
    (void)qm_mutex_unlock(&engine->lock);
}

int qm_audio_engine_claim_owner(qm_audio_engine_t *engine, qm_audio_service_t *owner)
{
    if (engine == NULL || owner == NULL || !qm_mutex_is_valid(&engine->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->state != QM_AUDIO_ENGINE_STATE_OPEN ||
        engine->service_owner != NULL || engine->render != NULL) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    engine->service_owner = owner;
    (void)qm_mutex_unlock(&engine->lock);
    return QM_EOK;
}

void qm_audio_engine_release_owner(qm_audio_engine_t *engine, qm_audio_service_t *owner)
{
    if (engine == NULL || !qm_mutex_is_valid(&engine->lock)) {
        return;
    }
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->service_owner == owner) {
        engine->service_owner = NULL;
    }
    (void)qm_mutex_unlock(&engine->lock);
}

int qm_audio_engine_create(const qm_audio_engine_config_t *config, qm_audio_engine_t **out_engine)
{
    qm_audio_engine_t *engine = NULL;
    uint32_t frame_bytes = 0U;
    uint32_t capture_frame_bytes = 0U;
    uint32_t capture_queue_len = 0U;
    qm_bool backend_open = QM_FALSE;
    int ret = QM_EOK;

    if (out_engine == NULL) {
        return -QM_EINVAL;
    }
    *out_engine = NULL;
    if (config == NULL) {
        return -QM_EINVAL;
    }
    if ((!qm_audio_stream_format_valid(&config->input_format) &&
         (config->input_format.pcm.sample_rate_hz != 0U || config->input_format.codec != QM_AUDIO_CODEC_PCM)) ||
        (!qm_audio_stream_format_valid(&config->output_format) &&
         (config->output_format.pcm.sample_rate_hz != 0U || config->output_format.codec != QM_AUDIO_CODEC_PCM)) ||
        (!qm_audio_stream_format_valid(&config->input_format) &&
         !qm_audio_stream_format_valid(&config->output_format))) {
        return -QM_EINVAL;
    }
#if !defined(CONFIG_QM_AUDIO_CAPTURE_SUPPORT) || !CONFIG_QM_AUDIO_CAPTURE_SUPPORT
    if (qm_audio_stream_format_valid(&config->input_format)) {
        return -QM_ENOSYS;
    }
#endif
    if (qm_audio_stream_format_valid(&config->input_format) &&
        config->input_format.codec == QM_AUDIO_CODEC_PCM &&
        config->capture_frame_bytes != 0U &&
        config->capture_frame_bytes % qm_audio_pcm_frame_bytes(&config->input_format.pcm) != 0U) {
        return -QM_EINVAL;
    }
    if (config->input_format.codec != QM_AUDIO_CODEC_PCM &&
        config->capture_frame_bytes != 0U &&
        config->capture_frame_bytes < config->input_format.max_packet_bytes) {
        return -QM_EINVAL;
    }

    engine = (qm_audio_engine_t *)qm_calloc(1U, sizeof(*engine));
    if (engine == NULL) {
        return -QM_ENOMEM;
    }
    engine->input_format = config->input_format;
    engine->output_format = config->output_format;
    capture_frame_bytes = config->capture_frame_bytes;
    if (capture_frame_bytes == 0U) {
        capture_frame_bytes = engine->input_format.codec != QM_AUDIO_CODEC_PCM ?
            engine->input_format.max_packet_bytes : CONFIG_QM_AUDIO_CAPTURE_FRAME_BYTES;
        if (engine->input_format.codec == QM_AUDIO_CODEC_PCM &&
            qm_audio_stream_format_valid(&engine->input_format)) {
            frame_bytes = qm_audio_pcm_frame_bytes(&engine->input_format.pcm);
            capture_frame_bytes -= capture_frame_bytes % frame_bytes;
            if (capture_frame_bytes == 0U) {
                capture_frame_bytes = frame_bytes;
            }
        }
    }
    capture_queue_len = config->capture_queue_len;
    if (capture_queue_len == 0U) {
        capture_queue_len = CONFIG_QM_AUDIO_CAPTURE_QUEUE_LEN;
    }
    if (!qm_audio_stream_format_valid(&engine->input_format)) {
        capture_frame_bytes = 0U;
        capture_queue_len = 0U;
    }

    ret = qm_mutex_new(&engine->lock);
    if (ret != QM_EOK) {
        ret = -QM_EIO;
        goto fail;
    }
    ret = qm_audio_normalize_result(qm_audio_device_open(
        &engine->backend_ctx,
        &(qm_audio_device_config_t){
            .input_format = engine->input_format,
            .output_format = engine->output_format,
            .flags = 0U,
        }));
    backend_open = engine->backend_ctx != NULL;
    if (ret != QM_EOK || !backend_open) {
        if (ret == QM_EOK) {
            ret = -QM_EIO;
        }
        goto fail;
    }
    ret = qm_audio_normalize_result(qm_audio_device_get_caps(engine->backend_ctx,
                                                              &engine->caps));
    if (ret != QM_EOK) {
        goto fail;
    }
    if (qm_audio_stream_format_valid(&engine->input_format) &&
        ((engine->caps.flags & QM_AUDIO_DEVICE_CAP_CAPTURE) == 0U ||
         !qm_audio_stream_format_equal(&engine->input_format, &engine->caps.input_format))) {
        ret = -QM_ENOSYS;
        goto fail;
    }
    if (qm_audio_stream_format_valid(&engine->output_format) &&
        ((engine->caps.flags & QM_AUDIO_DEVICE_CAP_PLAYBACK) == 0U ||
         !qm_audio_stream_format_equal(&engine->output_format, &engine->caps.output_format))) {
        ret = -QM_ENOSYS;
        goto fail;
    }
    if (qm_audio_stream_format_valid(&engine->input_format)) {
        ret = qm_audio_capture_create(&(qm_audio_capture_open_config_t){
                                          .device_ctx = engine->backend_ctx,
                                          .input_format = engine->input_format,
                                          .frame_bytes = capture_frame_bytes,
                                          .queue_len = capture_queue_len,
                                      }, &engine->capture);
        if (ret != QM_EOK) {
            goto fail;
        }
    }
    engine->state = QM_AUDIO_ENGINE_STATE_OPEN;
    ret = render_runtime_init(engine);
    if (ret != QM_EOK) {
        goto fail;
    }
    *out_engine = engine;
    return QM_EOK;

fail:
    if (backend_open) {
        if (qm_audio_engine_shutdown(engine, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS) == QM_EOK &&
            qm_audio_engine_destroy(engine) == QM_EOK) {
            return ret;
        }
        /* Preserve failed cleanup for its owner. / 保留清理失败的实例供所有者重试。 */
        *out_engine = engine;
        return ret;
    }
    qm_mutex_free(&engine->lock);
    qm_free(engine);
    return ret;
}

qm_audio_capture_t *qm_audio_engine_get_capture(qm_audio_engine_t *engine)
{
    if (engine == NULL || engine->state != QM_AUDIO_ENGINE_STATE_OPEN) {
        return NULL;
    }
    return engine->capture;
}

int qm_audio_engine_shutdown(qm_audio_engine_t *engine, uint32_t timeout_ms)
{
    qm_audio_render_stream_t *stream = NULL;
    qm_utils_time_t deadline = {0};
    int ret = QM_EOK;

    if (engine == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->state == QM_AUDIO_ENGINE_STATE_SHUTDOWN) {
        (void)qm_mutex_unlock(&engine->lock);
        return QM_EOK;
    }
    if (engine->shutdown_active || engine->service_owner != NULL) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    engine->shutdown_active = QM_TRUE;
    engine->state = QM_AUDIO_ENGINE_STATE_SHUTTING_DOWN;
    stream = engine->render;
    notify(&engine->render_space_sem);
    notify(&engine->render_idle_sem);
    (void)qm_mutex_unlock(&engine->lock);
    qm_utils_time_countdown_ms(&deadline, timeout_ms);

    ret = engine->capture == NULL ? QM_EOK :
        qm_audio_capture_shutdown(engine->capture, timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline));
    if (ret != QM_EOK) {
        goto done;
    }
    if (stream != NULL) {
        ret = render_control(stream, QM_AUDIO_RENDER_ACTION_STOP, timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline));
        if (ret != QM_EOK) {
            goto done;
        }
    }
    ret = render_runtime_shutdown(engine, timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline));
    if (ret != QM_EOK) {
        goto done;
    }
    ret = qm_audio_normalize_result(qm_audio_device_close(engine->backend_ctx));
    if (ret == QM_EOK) {
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        engine->backend_ctx = NULL;
        engine->state = QM_AUDIO_ENGINE_STATE_SHUTDOWN;
        (void)qm_mutex_unlock(&engine->lock);
    }

done:
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    engine->shutdown_active = QM_FALSE;
    (void)qm_mutex_unlock(&engine->lock);
    return ret;
}

int qm_audio_engine_destroy(qm_audio_engine_t *engine)
{
    int ret = QM_EOK;

    if (engine == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->state != QM_AUDIO_ENGINE_STATE_SHUTDOWN ||
        engine->render_leases != 0U || engine->service_owner != NULL) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    (void)qm_mutex_unlock(&engine->lock);
    if (engine->capture != NULL) {
        ret = qm_audio_capture_destroy(engine->capture);
        if (ret != QM_EOK) {
            return ret;
        }
        engine->capture = NULL;
    }
    render_runtime_destroy(engine);
    qm_mutex_free(&engine->lock);
    qm_free(engine);
    return QM_EOK;
}

int qm_audio_engine_get_caps(const qm_audio_engine_t *engine, qm_audio_device_caps_t *caps)
{
    if (engine == NULL || caps == NULL) {
        return -QM_EINVAL;
    }
    *caps = engine->caps;
    return QM_EOK;
}

int qm_audio_engine_get_input_stream_format(const qm_audio_engine_t *engine, qm_audio_stream_format_t *format)
{
    if (engine == NULL || format == NULL) {
        return -QM_EINVAL;
    }
    if (!qm_audio_stream_format_valid(&engine->input_format)) {
        return -QM_ENOSYS;
    }
    *format = engine->input_format;
    return QM_EOK;
}

int qm_audio_engine_get_output_stream_format(const qm_audio_engine_t *engine, qm_audio_stream_format_t *format)
{
    if (engine == NULL || format == NULL) {
        return -QM_EINVAL;
    }
    if (!qm_audio_stream_format_valid(&engine->output_format)) {
        return -QM_ENOSYS;
    }
    *format = engine->output_format;
    return QM_EOK;
}

int qm_audio_engine_render_open(qm_audio_engine_t *engine,
                                const qm_audio_render_config_t *config,
                                qm_audio_render_stream_t **out_stream)
{
    return qm_audio_engine_render_open_owned(engine, NULL, config, out_stream);
}

int qm_audio_engine_render_open_owned(qm_audio_engine_t *engine, qm_audio_service_t *owner,
                                      const qm_audio_render_config_t *config,
                                      qm_audio_render_stream_t **out_stream)
{
    int ret = QM_EOK;

    if (out_stream == NULL) {
        return -QM_EINVAL;
    }
    *out_stream = NULL;
    ret = qm_audio_engine_render_open_request(engine, owner, config, out_stream);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = wait_action(*out_stream, QM_AUDIO_RENDER_ACTION_START,
                      CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
    if (ret != QM_EOK) {
        /* The caller retains the lease even if START completes after timeout.
         * / 即使 START 在超时后才完成，调用方也保留租约以重试清理。 */
        (void)qm_audio_engine_render_request(*out_stream, QM_AUDIO_RENDER_ACTION_STOP);
    }
    return ret;
}

int qm_audio_engine_render_open_request(qm_audio_engine_t *engine, qm_audio_service_t *owner,
                                        const qm_audio_render_config_t *config,
                                        qm_audio_render_stream_t **out_stream)
{
    qm_audio_render_stream_t *stream = NULL;

    if (out_stream == NULL) {
        return -QM_EINVAL;
    }
    *out_stream = NULL;
    if (engine == NULL || config == NULL ||
        (config->delivery != QM_AUDIO_DELIVERY_RELIABLE && config->delivery != QM_AUDIO_DELIVERY_REALTIME)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->state != QM_AUDIO_ENGINE_STATE_OPEN || engine->render != NULL ||
        engine->service_owner != owner) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    if ((engine->caps.flags & QM_AUDIO_DEVICE_CAP_PLAYBACK) == 0U || !engine->render_task_started) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_ENOSYS;
    }
    stream = qm_calloc(1U, sizeof(*stream));
    if (stream == NULL) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_ENOMEM;
    }
    stream->engine = engine;
    stream->delivery = config->delivery;
    stream->state = QM_AUDIO_RENDER_STATE_PAUSED;
    stream->references = 1U;
    stream->submitted_duration_known = QM_TRUE;
    stream->action = QM_AUDIO_RENDER_ACTION_START;
    stream->action_pending = QM_TRUE;
    engine->render_leases++;
    engine->render = stream;
    *out_stream = stream;
    wake_render(engine);
    (void)qm_mutex_unlock(&engine->lock);
    return QM_EOK;
}

/* A reserved queue slot is released exactly once: by failure or dequeue.
 * / 预留槽位仅由失败回滚或出队释放一次。 */
static int render_submit(qm_audio_render_stream_t *stream, const qm_audio_data_t *input,
                         uint32_t timeout_ms, uint32_t *written)
{
    qm_audio_engine_t *engine = stream->engine;
    qm_audio_render_packet_t *packet = NULL;
    uint32_t packet_size = 0U;
    int ret = QM_EOK;

    if (input->size == 0U) {
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        if (engine->render != stream || engine->state != QM_AUDIO_ENGINE_STATE_OPEN ||
            stream->state == QM_AUDIO_RENDER_STATE_STOPPING || stream->state == QM_AUDIO_RENDER_STATE_STOPPED) {
            ret = -QM_EINTR;
        } else if (stream->state != QM_AUDIO_RENDER_STATE_ACTIVE) {
            ret = -QM_EBUSY;
        } else if (stream->result != QM_EOK) {
            ret = stream->result;
        } else if ((input->flags & QM_AUDIO_DATA_END_OF_STREAM) != 0U) {
            stream->state = QM_AUDIO_RENDER_STATE_SEALED;
        }
        (void)qm_mutex_unlock(&engine->lock);
        return ret;
    }
    if (input->size > UINT32_MAX - sizeof(*packet)) {
        return -QM_EINVAL;
    }
    ret = render_reserve_slot(stream, timeout_ms);
    if (ret != QM_EOK) {
        return ret;
    }
    packet_size = (uint32_t)sizeof(*packet) + input->size;
    packet = qm_malloc(packet_size);
    if (packet == NULL) {
        render_release_slot(engine);
        return -QM_ENOMEM;
    }
    packet->stream = stream;
    packet->input = *input;
    packet->input.data = packet->data;
    packet->input.flags &= ~QM_AUDIO_DATA_END_OF_STREAM;
    qm_memcpy(packet->data, input->data, input->size);

    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->render != stream || engine->state != QM_AUDIO_ENGINE_STATE_OPEN ||
        stream->state != QM_AUDIO_RENDER_STATE_ACTIVE) {
        ret = -QM_EINTR;
    } else if (stream->result != QM_EOK) {
        ret = stream->result;
    } else if (qm_queue_send(&engine->render_queue, &packet, sizeof(packet)) != QM_EOK) {
        ret = -QM_EIO;
    } else {
        stream->pending_packets++;
        if ((input->flags & QM_AUDIO_DATA_END_OF_STREAM) != 0U) {
            stream->state = QM_AUDIO_RENDER_STATE_SEALED;
        }
        if (engine->output_format.codec == QM_AUDIO_CODEC_PCM) {
            stream->submitted_frames += input->size / qm_audio_pcm_frame_bytes(&engine->output_format.pcm);
        } else {
            stream->submitted_packets++;
            if ((input->flags & QM_AUDIO_DATA_DURATION_VALID) != 0U) {
                stream->submitted_duration_ms += input->duration_ms;
            } else if (engine->output_format.packet_duration_ms != 0U) {
                stream->submitted_duration_ms += engine->output_format.packet_duration_ms;
            } else {
                stream->submitted_duration_known = QM_FALSE;
            }
        }
        *written = input->size;
        wake_render(engine);
    }
    (void)qm_mutex_unlock(&engine->lock);
    if (ret != QM_EOK) {
        qm_free(packet);
        render_release_slot(engine);
    }
    return ret;
}

int qm_audio_engine_render_write(qm_audio_render_stream_t *stream, const qm_audio_data_t *buffer,
                                 uint32_t timeout_ms, uint32_t *written)
{
    uint32_t frame_bytes = 0U;
    uint32_t limit = 0U;
    qm_audio_data_t block = {0};

    if (written != NULL) {
        *written = 0U;
    }
    if (stream == NULL || stream->engine == NULL || buffer == NULL || written == NULL) {
        return -QM_EINVAL;
    }
    if (qm_audio_encoded_packets(&stream->engine->output_format)) {
        return -QM_ENOSYS;
    }
    if ((buffer->flags & ~(QM_AUDIO_DATA_DISCONTINUITY | QM_AUDIO_DATA_END_OF_STREAM)) != 0U ||
        buffer->timestamp_ms != 0U || buffer->duration_ms != 0U || buffer->sequence != 0U ||
        (buffer->size == 0U && (buffer->flags & QM_AUDIO_DATA_DISCONTINUITY) != 0U)) {
        return -QM_EINVAL;
    }
    frame_bytes = qm_audio_encoded_bytes(&stream->engine->output_format) ? 1U :
        qm_audio_pcm_frame_bytes(&stream->engine->output_format.pcm);
    if (frame_bytes == 0U || (buffer->size != 0U &&
        (buffer->data == NULL || buffer->size % frame_bytes != 0U))) {
        return -QM_EINVAL;
    }
    limit = CONFIG_QM_AUDIO_ENGINE_RENDER_BLOCK_BYTES / frame_bytes * frame_bytes;
    if (limit == 0U) {
        return -QM_EINVAL;
    }
    block = *buffer;
    if (block.size > limit) {
        block.size = limit;
        block.flags &= ~QM_AUDIO_DATA_END_OF_STREAM;
    }
    return render_submit(stream, &block, timeout_ms, written);
}

int qm_audio_engine_render_write_packet(qm_audio_render_stream_t *stream,
                                        const qm_audio_data_t *packet, uint32_t timeout_ms)
{
    uint32_t written = 0U;

    if (stream == NULL || stream->engine == NULL || packet == NULL) {
        return -QM_EINVAL;
    }
    if (!qm_audio_encoded_packets(&stream->engine->output_format)) {
        return -QM_ENOSYS;
    }
    if (!qm_audio_packet_valid(&stream->engine->output_format, packet)) {
        return -QM_EINVAL;
    }
    return render_submit(stream, packet, timeout_ms, &written);
}

int qm_audio_engine_render_drain(qm_audio_render_stream_t *stream, uint32_t timeout_ms)
{
    qm_audio_engine_t *engine = NULL;
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    qm_bool expired = QM_FALSE;
    qm_bool io_reserved = QM_FALSE;
    int ret = QM_EOK;

    if (stream == NULL || stream->engine == NULL) {
        return -QM_EINVAL;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->render != stream || engine->state != QM_AUDIO_ENGINE_STATE_OPEN ||
        stream->state == QM_AUDIO_RENDER_STATE_STOPPING || stream->state == QM_AUDIO_RENDER_STATE_STOPPED) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EINTR;
    }
    if (stream->drain_active || stream->action_pending) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    if (stream->state == QM_AUDIO_RENDER_STATE_DRAINED) {
        (void)qm_mutex_unlock(&engine->lock);
        return QM_EOK;
    }
    if ((engine->caps.flags & QM_AUDIO_DEVICE_CAP_DRAIN) == 0U) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_ENOSYS;
    }
    if (stream->state != QM_AUDIO_RENDER_STATE_ACTIVE && stream->state != QM_AUDIO_RENDER_STATE_SEALED) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    stream->state = QM_AUDIO_RENDER_STATE_SEALED;
    stream->drain_active = QM_TRUE;
    (void)qm_mutex_unlock(&engine->lock);
    qm_utils_time_countdown_ms(&deadline, timeout_ms);

    for (;;) {
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        if (engine->state != QM_AUDIO_ENGINE_STATE_OPEN || stream->state != QM_AUDIO_RENDER_STATE_SEALED) {
            ret = -QM_EINTR;
        } else if (stream->result != QM_EOK) {
            ret = stream->result;
        } else if (stream->pending_packets == 0U && !stream->position_pending &&
                   engine->render_inflight == 0U) {
            engine->render_inflight++;
            io_reserved = QM_TRUE;
        }
        (void)qm_mutex_unlock(&engine->lock);
        if (ret != QM_EOK || io_reserved) {
            break;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline);
        if (expired || left == 0U) {
            ret = -QM_ETIMEOUT;
            break;
        }
        expired = qm_sem_wait(&engine->render_idle_sem, left) != QM_EOK;
    }
    if (io_reserved) {
        ret = qm_audio_normalize_result(qm_audio_device_playback_drain(
            engine->backend_ctx, timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline)));
    }
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (io_reserved) {
        engine->render_inflight--;
    }
    stream->drain_active = QM_FALSE;
    if (stream->state != QM_AUDIO_RENDER_STATE_SEALED) {
        ret = -QM_EINTR;
    } else if (ret == QM_EOK) {
        stream->state = QM_AUDIO_RENDER_STATE_DRAINED;
    }
    signal_render_idle(engine);
    (void)qm_mutex_unlock(&engine->lock);
    qm_audio_event_wake(engine->audio);
    return ret;
}

int qm_audio_engine_render_pause(qm_audio_render_stream_t *stream)
{
    return render_control(stream, QM_AUDIO_RENDER_ACTION_PAUSE, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
}

int qm_audio_engine_render_resume(qm_audio_render_stream_t *stream)
{
    return render_control(stream, QM_AUDIO_RENDER_ACTION_RESUME, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
}

int qm_audio_engine_render_stop(qm_audio_render_stream_t *stream)
{
    return render_control(stream, QM_AUDIO_RENDER_ACTION_STOP, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
}

int qm_audio_engine_render_set_service_progress(qm_audio_render_stream_t *stream,
                                               qm_sem_t *sem)
{
    qm_audio_engine_t *engine = NULL;

    if (stream == NULL || stream->engine == NULL ||
        (sem != NULL && !qm_sem_is_valid(sem))) {
        return -QM_EINVAL;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->render != stream) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EINTR;
    }
    stream->service_progress_sem = sem;
    if (sem != NULL) {
        notify(sem);
    }
    (void)qm_mutex_unlock(&engine->lock);
    return QM_EOK;
}

int qm_audio_engine_render_close(qm_audio_render_stream_t *stream)
{
    qm_audio_engine_t *engine = NULL;

    if (stream == NULL || stream->engine == NULL) {
        return -QM_EINVAL;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->render != stream) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EINVAL;
    }
    if (stream->state != QM_AUDIO_RENDER_STATE_STOPPED || stream->action_pending ||
        stream->position_pending || stream->position_waiting || stream->drain_active ||
        stream->pending_packets != 0U || engine->render_inflight != 0U) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    stream->service_progress_sem = NULL;
    engine->render = NULL;
    (void)qm_mutex_unlock(&engine->lock);
    qm_audio_engine_render_release(stream);
    return QM_EOK;
}

int qm_audio_engine_render_retain(qm_audio_render_stream_t *stream)
{
    if (stream == NULL || stream->engine == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&stream->engine->lock, QM_WAIT_FOREVER);
    if (stream->engine->render != stream || stream->references == UINT32_MAX) {
        (void)qm_mutex_unlock(&stream->engine->lock);
        return -QM_EBUSY;
    }
    stream->references++;
    (void)qm_mutex_unlock(&stream->engine->lock);
    return QM_EOK;
}

void qm_audio_engine_render_release(qm_audio_render_stream_t *stream)
{
    qm_bool reclaim = QM_FALSE;
    qm_audio_engine_t *engine = NULL;

    if (stream == NULL) {
        return;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    reclaim = --stream->references == 0U;
    if (reclaim) {
        engine->render_leases--;
    }
    (void)qm_mutex_unlock(&engine->lock);
    if (reclaim) {
        qm_free(stream);
    }
}

int qm_audio_engine_render_submitted(qm_audio_render_stream_t *stream, uint32_t *duration_ms)
{
    qm_audio_engine_t *engine = NULL;
    uint64_t duration = 0U;
    int ret = QM_EOK;

    if (stream == NULL || duration_ms == NULL) {
        return -QM_EINVAL;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if (engine->output_format.codec == QM_AUDIO_CODEC_PCM) {
        duration = qm_audio_frames_to_ms(stream->submitted_frames,
                                          engine->output_format.pcm.sample_rate_hz);
    } else {
        duration = stream->submitted_duration_ms;
        if (!stream->submitted_duration_known ||
            (stream->submitted_packets == 0U && engine->output_format.packet_duration_ms == 0U)) {
            ret = -QM_ENOSYS;
        }
    }
    *duration_ms = duration > UINT32_MAX ? UINT32_MAX : (uint32_t)duration;
    (void)qm_mutex_unlock(&engine->lock);
    return ret;
}

int qm_audio_engine_render_get_position(const qm_audio_render_stream_t *stream, uint32_t *position_ms)
{
    qm_audio_engine_t *engine = NULL;
    qm_audio_render_stream_t *lease = (qm_audio_render_stream_t *)stream;
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    qm_bool expired = QM_FALSE;
    int ret = QM_EOK;

    if (stream == NULL || position_ms == NULL || stream->engine == NULL) {
        return -QM_EINVAL;
    }
    engine = stream->engine;
    (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
    if ((engine->caps.flags & QM_AUDIO_DEVICE_CAP_POSITION) == 0U) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_ENOSYS;
    }
    if (engine->render != stream) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EINTR;
    }
    if (stream->state == QM_AUDIO_RENDER_STATE_STOPPED) {
        ret = stream->cached_position_valid ? QM_EOK : -QM_ENOSYS;
        if (ret == QM_EOK) {
            *position_ms = stream->cached_position_ms;
        }
        (void)qm_mutex_unlock(&engine->lock);
        return ret;
    }
    if (engine->state != QM_AUDIO_ENGINE_STATE_OPEN || stream->drain_active ||
        stream->action_pending || stream->position_pending || stream->position_waiting ||
        stream->state == QM_AUDIO_RENDER_STATE_STOPPING) {
        (void)qm_mutex_unlock(&engine->lock);
        return -QM_EBUSY;
    }
    lease->position_pending = QM_TRUE;
    lease->position_waiting = QM_TRUE;
    wake_render(engine);
    (void)qm_mutex_unlock(&engine->lock);
    qm_utils_time_countdown_ms(&deadline, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);

    for (;;) {
        (void)qm_mutex_lock(&engine->lock, QM_WAIT_FOREVER);
        if (!stream->position_pending) {
            ret = stream->position_result;
            if (ret == QM_EOK) {
                *position_ms = stream->cached_position_ms;
            }
            lease->position_waiting = QM_FALSE;
            signal_render_idle(engine);
            (void)qm_mutex_unlock(&engine->lock);
            qm_audio_event_wake(engine->audio);
            return ret;
        }
        left = qm_utils_time_left(&deadline);
        if (expired || left == 0U) {
            lease->position_waiting = QM_FALSE;
            signal_render_idle(engine);
            (void)qm_mutex_unlock(&engine->lock);
            qm_audio_event_wake(engine->audio);
            return -QM_ETIMEOUT;
        }
        (void)qm_mutex_unlock(&engine->lock);
        expired = qm_sem_wait(&engine->render_position_sem, left) != QM_EOK;
    }
}

int qm_audio_engine_set_volume(qm_audio_engine_t *engine, uint8_t volume)
{
    int ret = qm_audio_engine_render_setting_request(engine,
        QM_AUDIO_RENDER_SETTING_VOLUME, volume);

    if (ret != QM_EOK) {
        return ret;
    }
    return qm_audio_engine_render_setting_wait(engine,
        QM_AUDIO_RENDER_SETTING_VOLUME, volume);
}

int qm_audio_engine_set_mute(qm_audio_engine_t *engine, qm_bool muted)
{
    int ret = qm_audio_engine_render_setting_request(engine,
        QM_AUDIO_RENDER_SETTING_MUTE, (uint8_t)muted);

    if (ret != QM_EOK) {
        return ret;
    }
    return qm_audio_engine_render_setting_wait(engine,
        QM_AUDIO_RENDER_SETTING_MUTE, (uint8_t)muted);
}
