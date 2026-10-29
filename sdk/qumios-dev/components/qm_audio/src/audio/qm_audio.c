/**
 * @file qm_audio.c
 * @brief Audio instance ownership and shared source execution. / 音频实例所有权与共享输入源执行。
 */
#include "qm_audio_priv.h"

/**
 * @brief Default-instance registry owned by lifecycle APIs. / 由生命周期 API 独占的默认实例注册表。
 *
 * Only init/deinit mutate this registry. The lifecycle owner excludes default
 * lookup calls during those transitions; no lazy global lock/device startup. */
static qm_audio_t *default_audio;
static qm_audio_config_t default_config;
static qm_bool default_ready;

/**
 * @brief Select one runnable Pipeline for a shared source worker. / 为共享输入源任务选择一个可运行的 Pipeline。
 *
 * A pause request must be scheduled once so the worker can checkpoint and
 * acknowledge it. Only an acknowledged pause yields the shared source worker. */
static qm_audio_pipeline_t *take_pipeline(qm_audio_t *audio, uint32_t *index)
{
    uint32_t i = 0U;
    uint32_t n = 0U;
    qm_audio_pipeline_t *pipeline = NULL;

    for (i = 0U; i < audio->pipeline_capacity; ++i) {
        n = (audio->next_pipeline + i) % audio->pipeline_capacity;
        pipeline = audio->pipelines[n];
        if (pipeline == NULL) {
            continue;
        }
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        if (!pipeline->execution_active &&
            (!pipeline->paused || !pipeline->pause_notified || pipeline->cancel_requested)) {
            pipeline->execution_active = QM_TRUE;
            pipeline->execution_task = &audio->source_task;
            (void)qm_mutex_unlock(&pipeline->lock);
            audio->next_pipeline = (n + 1U) % audio->pipeline_capacity;
            *index = n;
            return pipeline;
        }
        (void)qm_mutex_unlock(&pipeline->lock);
    }
    return NULL;
}

/**
 * @brief Run the instance's source worker. / 运行实例唯一的输入源任务。
 *
 * The worker clears Pipeline execution ownership before releasing waiters, so
 * a destroy operation cannot race its final task transition. 任务在释放等待者前
 * 清除 Pipeline 执行所有权，避免销毁流程与最终任务状态转换竞态。
 */
static void source_task(void *context)
{
    qm_audio_t *audio = context;
    qm_audio_pipeline_t *pipeline = NULL;
    uint32_t index = 0U;
    qm_bool complete = QM_FALSE;

    for (;;) {
        pipeline = NULL;
        index = 0U;
        complete = QM_FALSE;
        (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
        if (audio->source_closing) {
            audio->source_started = QM_FALSE;
            (void)qm_sem_signal(&audio->source_stopped);
            (void)qm_mutex_unlock(&audio->lock);
            break;
        }
        pipeline = take_pipeline(audio, &index);
        (void)qm_mutex_unlock(&audio->lock);
        if (pipeline == NULL) {
            (void)qm_sem_wait(&audio->source_wake, QM_WAIT_FOREVER);
            continue;
        }
        complete = qm_audio_pipeline_execute(pipeline);
        /** Clear execution ownership before waking waiters. / 唤醒等待者前清除执行所有权。
         *
         * A terminal pipeline
         * also leaves the registry here, so a later destroy cannot race this task. */
        (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
        if (complete) {
            audio->pipelines[index] = NULL;
        }
        (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
        pipeline->execution_task = NULL;
        pipeline->execution_active = QM_FALSE;
        if (complete) {
            pipeline->task_started = QM_FALSE;
        }
        (void)qm_sem_signal(&pipeline->state_sem);
        (void)qm_mutex_unlock(&pipeline->lock);
        (void)qm_mutex_unlock(&audio->lock);
        /* Lifecycle events may precede this ownership release. Wake the event
         * task once the durable quiescence flags are observable. 生命周期事件
         * 可能先于所有权释放；静止标志可见后再唤醒事件任务继续处理。 */
        qm_audio_event_wake(audio);
    }
    qm_task_exit(NULL);
}

static int source_init(qm_audio_t *audio, uint32_t capacity)
{
    int ret = QM_EOK;

    if (audio == NULL || capacity == 0U ||
        capacity > UINT32_MAX / sizeof(*audio->pipelines)) {
        return -QM_EINVAL;
    }
    audio->pipelines = qm_calloc(capacity, sizeof(*audio->pipelines));
    if (audio->pipelines == NULL) {
        return -QM_ENOMEM;
    }
    audio->pipeline_capacity = capacity;
    ret = qm_sem_new(&audio->source_wake, 0U);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_sem_new(&audio->source_stopped, 0U);
    if (ret != QM_EOK) {
        return ret;
    }
    audio->source_started = QM_TRUE;
    ret = qm_task_new(&audio->source_task, "audio_source", source_task, audio,
                      CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_STACK_SIZE,
                      CONFIG_QM_AUDIO_CONTROL_SOURCE_TASK_PRIORITY);
    if (ret != QM_EOK) {
        audio->source_started = QM_FALSE;
    }
    return ret;
}

int qm_audio_executor_submit(qm_audio_t *audio, qm_audio_pipeline_t *pipeline)
{
    uint32_t i = 0U;

    if (audio == NULL || pipeline == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    if (audio->source_closing || !qm_sem_is_valid(&audio->source_wake)) {
        (void)qm_mutex_unlock(&audio->lock);
        return -QM_EBUSY;
    }
    for (i = 0U; i < audio->pipeline_capacity; ++i) {
        if (audio->pipelines[i] == NULL) {
            break;
        }
    }
    if (i == audio->pipeline_capacity) {
        (void)qm_mutex_unlock(&audio->lock);
        return -QM_EFULL;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    if (pipeline->executor != NULL || !pipeline->task_started ||
        pipeline->state != QM_AUDIO_PIPELINE_RUNNING) {
        (void)qm_mutex_unlock(&pipeline->lock);
        (void)qm_mutex_unlock(&audio->lock);
        return -QM_EBUSY;
    }
    pipeline->executor = audio;
    (void)qm_mutex_unlock(&pipeline->lock);
    audio->pipelines[i] = pipeline;
    (void)qm_sem_signal(&audio->source_wake);
    (void)qm_mutex_unlock(&audio->lock);
    return QM_EOK;
}

void qm_audio_executor_wake(qm_audio_pipeline_t *pipeline)
{
    if (pipeline->executor != NULL &&
        qm_sem_is_valid(&pipeline->executor->source_wake)) {
        (void)qm_sem_signal(&pipeline->executor->source_wake);
    }
}

/**
 * @brief Stop the source worker after every Pipeline leaves the registry. / 在所有 Pipeline 离开注册表后停止输入源任务。
 */
static int source_shutdown(qm_audio_t *audio, uint32_t timeout_ms)
{
    qm_utils_time_t deadline = {0};
    uint32_t i = 0U;
    qm_bool running = QM_FALSE;
    uint32_t left = 0U;

    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    for (i = 0U; i < audio->pipeline_capacity; ++i) {
        if (audio->pipelines[i] != NULL) {
            (void)qm_mutex_unlock(&audio->lock);
            return -QM_EBUSY;
        }
    }
    audio->source_closing = QM_TRUE;
    if (audio->source_started) {
        (void)qm_sem_signal(&audio->source_wake);
    }
    (void)qm_mutex_unlock(&audio->lock);
    for (;;) {
        running = QM_FALSE;
        left = 0U;
        (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
        running = audio->source_started;
        (void)qm_mutex_unlock(&audio->lock);
        if (!running) {
            return QM_EOK;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER : qm_utils_time_left(&deadline);
        if (left == 0U || qm_sem_wait(&audio->source_stopped, left) != QM_EOK) {
            return -QM_ETIMEOUT;
        }
    }
}

static int source_destroy(qm_audio_t *audio)
{
    if (audio->source_started) {
        return -QM_EBUSY;
    }
    if (qm_sem_is_valid(&audio->source_stopped)) {
        (void)qm_sem_free(&audio->source_stopped);
    }
    if (qm_sem_is_valid(&audio->source_wake)) {
        (void)qm_sem_free(&audio->source_wake);
    }
    qm_free(audio->pipelines);
    audio->pipelines = NULL;
    audio->pipeline_capacity = 0U;
    return QM_EOK;
}

static qm_bool same_config(const qm_audio_config_t *a, const qm_audio_config_t *b)
{
    return qm_audio_stream_format_equal(&a->input_format, &b->input_format) &&
           qm_audio_stream_format_equal(&a->output_format, &b->output_format) &&
           a->capture_frame_bytes == b->capture_frame_bytes &&
           a->capture_queue_len == b->capture_queue_len;
}

int qm_audio_init(const qm_audio_config_t *config)
{
    int ret = QM_EOK;

    if (config == NULL) {
        return -QM_EINVAL;
    }
    if (default_ready) {
        return same_config(config, &default_config) ? QM_EOK : -QM_EBUSY;
    }
    if (default_audio != NULL) {
        ret = qm_audio_deinit(CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    ret = qm_audio_create(config, &default_audio);
    if (ret != QM_EOK) {
        return ret;
    }
    default_config = *config;
    default_ready = QM_TRUE;
    return QM_EOK;
}

int qm_audio_deinit(uint32_t timeout_ms)
{
    int ret = QM_EOK;

    if (default_audio == NULL) {
        return QM_EOK;
    }
    ret = qm_audio_shutdown(default_audio, timeout_ms);
    if (ret != QM_EOK) {
        /** @brief Preserve usability only when children are still active. / 仅在子对象仍活跃时保持默认实例可用。 */
        if (default_audio->closing) {
            default_ready = QM_FALSE;
        }
        return ret;
    }
    default_ready = QM_FALSE;
    ret = qm_audio_destroy(default_audio);
    if (ret == QM_EOK) {
        default_audio = NULL;
    }
    return ret;
}

int qm_audio_acquire(qm_audio_t **audio)
{
    if (*audio == NULL) {
        if (!default_ready) {
            return default_audio == NULL ? -QM_EINIT : -QM_EBUSY;
        }
        *audio = default_audio;
    }
    return qm_audio_retain(*audio);
}

int qm_audio_retain(qm_audio_t *audio)
{
    int ret = QM_EOK;

    if (audio == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    if (audio->closing) {
        ret = -QM_EBUSY;
    } else if (audio->users == UINT32_MAX) {
        ret = -QM_EFULL;
    } else {
        audio->users++;
    }
    (void)qm_mutex_unlock(&audio->lock);
    return ret;
}

void qm_audio_release(qm_audio_t *audio)
{
    if (audio == NULL) {
        return;
    }
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    audio->users--;
    (void)qm_mutex_unlock(&audio->lock);
}

int qm_audio_create(const qm_audio_config_t *config, qm_audio_t **out_audio)
{
    qm_audio_t *audio = NULL;
    qm_audio_engine_config_t engine = {0};
    qm_audio_service_config_t service = {0};
    uint32_t capacity = 0U;
    int ret = QM_EOK;

    if (out_audio == NULL) {
        return -QM_EINVAL;
    }
    *out_audio = NULL;
    if (config == NULL) {
        return -QM_EINVAL;
    }
    capacity = CONFIG_QM_AUDIO_SERVICE_MAX_SESSIONS;
    if (capacity == 0U) {
        return -QM_EINVAL;
    }
    audio = (qm_audio_t *)qm_calloc(1U, sizeof(*audio));
    if (audio == NULL) {
        return -QM_ENOMEM;
    }
    ret = qm_mutex_new(&audio->lock);
    if (ret != QM_EOK) {
        qm_free(audio);
        return ret;
    }
    ret = qm_sem_new(&audio->settings_serial_sem, 1U);
    if (ret != QM_EOK) {
        (void)qm_mutex_free(&audio->lock);
        qm_free(audio);
        return -QM_EIO;
    }
    engine = (qm_audio_engine_config_t){
        .input_format = config->input_format,
        .output_format = config->output_format,
        .capture_frame_bytes = config->capture_frame_bytes,
        .capture_queue_len = config->capture_queue_len,
    };
    /* Bring up dependencies in execution order; the failure path tears them
     * down through the same public shutdown state machine. */
    ret = qm_audio_engine_create(&engine, &audio->engine);
    if (ret == QM_EOK) {
        audio->engine->audio = audio;
        service = (qm_audio_service_config_t){
            .engine = audio->engine, .max_sessions = capacity,
        };
        ret = qm_audio_service_create(&service, &audio->service);
    }
    if (ret == QM_EOK) {
        ret = qm_audio_event_shared_init(audio);
    }
    if (ret == QM_EOK) {
        ret = source_init(audio, capacity);
    }
    if (ret != QM_EOK) {
        if (qm_audio_shutdown(audio, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS) == QM_EOK &&
            qm_audio_destroy(audio) == QM_EOK) {
            return ret;
        }
        /* A failed backend close must remain reachable for cleanup retry. */
        *out_audio = audio;
        return ret;
    }
    *out_audio = audio;
    return QM_EOK;
}

int qm_audio_shutdown(qm_audio_t *audio, uint32_t timeout_ms)
{
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    int ret = QM_EOK;

    if (audio == NULL) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    if (audio->stopped) {
        (void)qm_mutex_unlock(&audio->lock);
        return QM_EOK;
    }
    if (audio->users != 0U || audio->shutdown_active) {
        (void)qm_mutex_unlock(&audio->lock);
        return -QM_EBUSY;
    }
    audio->closing = QM_TRUE;
    audio->shutdown_active = QM_TRUE;
    (void)qm_mutex_unlock(&audio->lock);

    /* Stop producers first. The event task may still observe their completion;
     * only after both are quiescent may Service and Engine be released. */
    ret = source_shutdown(audio, timeout_ms == QM_WAIT_FOREVER ?
                                     QM_WAIT_FOREVER : qm_utils_time_left(&deadline));
    if (ret != QM_EOK) {
        goto done;
    }
    ret = qm_audio_event_shared_shutdown(audio, timeout_ms == QM_WAIT_FOREVER ?
                                         QM_WAIT_FOREVER : qm_utils_time_left(&deadline));
    if (ret != QM_EOK) {
        goto done;
    }
    if (audio->service != NULL) {
        ret = qm_audio_service_shutdown(audio->service);
        if (ret == QM_EOK) {
            ret = qm_audio_service_destroy(audio->service);
        }
        if (ret != QM_EOK) {
            goto done;
        }
        audio->service = NULL;
    }
    if (audio->engine != NULL) {
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        ret = qm_audio_engine_shutdown(audio->engine, left);
    }
done:
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    if (ret == QM_EOK) {
        audio->stopped = QM_TRUE;
    }
    audio->shutdown_active = QM_FALSE;
    (void)qm_mutex_unlock(&audio->lock);
    return ret;
}

int qm_audio_destroy(qm_audio_t *audio)
{
    int ret = QM_EOK;

    if (audio == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    if (!audio->stopped || audio->shutdown_active || audio->users != 0U) {
        (void)qm_mutex_unlock(&audio->lock);
        return -QM_EBUSY;
    }
    (void)qm_mutex_unlock(&audio->lock);
    if (audio->engine != NULL) {
        ret = qm_audio_engine_destroy(audio->engine);
        if (ret != QM_EOK) {
            return ret;
        }
        audio->engine = NULL;
    }
    ret = source_destroy(audio);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_audio_event_shared_destroy(audio);
    if (ret != QM_EOK) {
        return ret;
    }
    if (qm_sem_is_valid(&audio->settings_serial_sem)) {
        (void)qm_sem_free(&audio->settings_serial_sem);
    }
    (void)qm_mutex_free(&audio->lock);
    qm_free(audio);
    return QM_EOK;
}

int qm_audio_get_input_format(qm_audio_pcm_format_t *format)
{
    qm_audio_stream_format_t stream_format = {0};
    qm_audio_t *audio = NULL;
    int ret = qm_audio_acquire(&audio);

    if (ret != QM_EOK) {
        return ret;
    }
    if (format == NULL) {
        ret = -QM_EINVAL;
    } else {
        ret = qm_audio_engine_get_input_stream_format(audio->engine, &stream_format);
        if (ret == QM_EOK) {
            if (stream_format.codec == QM_AUDIO_CODEC_PCM) {
                *format = stream_format.pcm;
            } else {
                ret = -QM_ENOSYS;
            }
        }
    }
    qm_audio_release(audio);
    return ret;
}

int qm_audio_get_input_stream_format(qm_audio_stream_format_t *format)
{
    qm_audio_t *audio = NULL;
    int ret = qm_audio_acquire(&audio);

    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_audio_engine_get_input_stream_format(audio->engine, format);
    qm_audio_release(audio);
    return ret;
}

int qm_audio_get_output_stream_format(qm_audio_stream_format_t *format)
{
    qm_audio_t *audio = NULL;
    int ret = qm_audio_acquire(&audio);

    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_audio_engine_get_output_stream_format(audio->engine, format);
    qm_audio_release(audio);
    return ret;
}

int qm_audio_get_caps(qm_audio_caps_t *caps)
{
    qm_audio_device_caps_t device_caps = {0};
    qm_audio_t *audio = NULL;
    int ret = QM_EOK;

    if (caps == NULL) {
        return -QM_EINVAL;
    }
    ret = qm_audio_acquire(&audio);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_audio_engine_get_caps(audio->engine, &device_caps);
    if (ret == QM_EOK) {
        caps->input_format = device_caps.input_format;
        caps->output_format = device_caps.output_format;
        caps->flags = 0U;
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_CAPTURE) {
            caps->flags |= QM_AUDIO_CAP_CAPTURE;
        }
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_PLAYBACK) {
            caps->flags |= QM_AUDIO_CAP_PLAYBACK;
        }
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_VOLUME) {
            caps->flags |= QM_AUDIO_CAP_VOLUME;
        }
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_MUTE) {
            caps->flags |= QM_AUDIO_CAP_MUTE;
        }
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_DRAIN) {
            caps->flags |= QM_AUDIO_CAP_DRAIN;
        }
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_ABORT_READ) {
            caps->flags |= QM_AUDIO_CAP_ABORT_CAPTURE;
        }
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_ABORT_WRITE) {
            caps->flags |= QM_AUDIO_CAP_ABORT_PLAYBACK;
        }
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_POSITION) {
            caps->flags |= QM_AUDIO_CAP_POSITION;
        }
        if (device_caps.flags & QM_AUDIO_DEVICE_CAP_STOP_DRAINS) {
            caps->flags |= QM_AUDIO_CAP_STOP_DRAINS;
        }
    }
    qm_audio_release(audio);
    return ret;
}

qm_audio_capture_t *qm_audio_get_capture(void)
{
    if (!default_ready || default_audio == NULL || default_audio->engine == NULL) {
        return NULL;
    }
    return qm_audio_engine_get_capture(default_audio->engine);
}

int qm_audio_get_capture_frame_bytes(uint32_t *bytes)
{
    qm_audio_t *audio = NULL;
    int ret = QM_EOK;

    if (bytes == NULL) {
        return -QM_EINVAL;
    }
    ret = qm_audio_acquire(&audio);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = audio->engine->capture == NULL ? -QM_ENOSYS :
          qm_audio_capture_get_frame_bytes(audio->engine->capture, bytes);
    qm_audio_release(audio);
    return ret;
}

int qm_audio_get_output_format(qm_audio_pcm_format_t *format)
{
    qm_audio_t *audio = NULL;

    return qm_audio_get_output_format_instance(audio, format);
}

int qm_audio_get_output_format_instance(qm_audio_t *audio, qm_audio_pcm_format_t *format)
{
    qm_audio_stream_format_t stream_format = {0};
    int ret = qm_audio_acquire(&audio);

    if (ret != QM_EOK) {
        return ret;
    }
    if (format == NULL) {
        ret = -QM_EINVAL;
    } else {
        ret = qm_audio_engine_get_output_stream_format(audio->engine, &stream_format);
        if (ret == QM_EOK) {
            if (stream_format.codec == QM_AUDIO_CODEC_PCM) {
                *format = stream_format.pcm;
            } else {
                ret = -QM_ENOSYS;
            }
        }
    }
    qm_audio_release(audio);
    return ret;
}

/* The command turn only accepts the request.  This caller-side wait preserves
 * the public API result contract without ever waiting on audio_event. */
static int submit_shared_setting(qm_audio_t *audio, qm_audio_render_setting_t setting, uint8_t value)
{
    qm_audio_command_t command = {0};
    int ret = QM_EOK;

    if (audio == NULL || !qm_sem_is_valid(&audio->settings_serial_sem)) {
        return -QM_EBUSY;
    }
    if (qm_task_is_current(&audio->event_task)) {
        /* Keep a completed result reserved for its external waiter without
         * blocking audio_event. / 保留外部等待者尚未读取的结果，不阻塞事件任务。 */
        if (qm_sem_wait(&audio->settings_serial_sem, QM_NO_WAIT) != QM_EOK) {
            return -QM_EBUSY;
        }
        ret = qm_audio_engine_render_setting_request(audio->engine, setting, value);
        (void)qm_sem_signal(&audio->settings_serial_sem);
        return ret;
    }
    if (qm_sem_wait(&audio->settings_serial_sem, QM_WAIT_FOREVER) != QM_EOK) {
        return -QM_EBUSY;
    }
    command.type = setting == QM_AUDIO_RENDER_SETTING_VOLUME ?
        QM_AUDIO_COMMAND_SET_VOLUME : QM_AUDIO_COMMAND_SET_MUTE;
    command.volume = value;
    command.muted = (qm_bool)value;
    ret = qm_sem_new(&command.done, 0U);
    if (ret == QM_EOK) {
        ret = qm_audio_engine_command_post(audio, &command);
        if (ret == QM_EOK) {
            ret = qm_audio_engine_render_setting_wait(audio->engine, setting, value);
        }
        qm_sem_free(&command.done);
    } else {
        ret = -QM_EIO;
    }
    (void)qm_sem_signal(&audio->settings_serial_sem);
    return ret;
}

int qm_audio_set_volume(uint8_t volume)
{
    qm_audio_t *audio = NULL;
    int ret = qm_audio_acquire(&audio);

    if (ret != QM_EOK) {
        return ret;
    }
    ret = submit_shared_setting(audio, QM_AUDIO_RENDER_SETTING_VOLUME, volume);
    qm_audio_release(audio);
    return ret;
}

int qm_audio_set_mute(qm_bool muted)
{
    qm_audio_t *audio = NULL;
    int ret = qm_audio_acquire(&audio);

    if (ret != QM_EOK) {
        return ret;
    }
    ret = submit_shared_setting(audio, QM_AUDIO_RENDER_SETTING_MUTE, (uint8_t)muted);
    qm_audio_release(audio);
    return ret;
}

int qm_audio_report_wake(const qm_audio_wake_info_t *wake)
{
    qm_audio_event_msg_t msg = {0};
    qm_audio_t *audio = NULL;
    int ret = QM_EOK;

    if (wake == NULL || wake->confidence > 100U) {
        return -QM_EINVAL;
    }
    ret = qm_audio_acquire(&audio);
    if (ret != QM_EOK) {
        return ret;
    }
    msg.type = QM_AUDIO_EVENT_MSG_FRONTEND_WAKE;
    msg.u.event.type = QM_AUDIO_EVENT_WAKE;
    msg.u.event.request_id = QM_AUDIO_INVALID_REQUEST_ID;
    msg.u.event.operation_id = QM_AUDIO_INVALID_REQUEST_ID;
    msg.u.event.generation = QM_AUDIO_INVALID_GENERATION;
    msg.u.event.result = QM_EOK;
    msg.u.event.timestamp_ms = qm_now_ms();
    msg.u.event.wake = *wake;
    ret = qm_audio_event_post(audio, &msg);
    qm_audio_release(audio);
    return ret;
}

int qm_audio_wake_subscribe(qm_audio_event_cb_t callback, void *user_ctx,
                            qm_audio_subscription_t *out_subscription)
{
    qm_audio_subscription_t subscription = NULL;
    qm_audio_t *audio = NULL;
    qm_bool closing = QM_FALSE;
    int ret = QM_EOK;

    if (callback == NULL || out_subscription == NULL) {
        return -QM_EINVAL;
    }
    *out_subscription = NULL;
    ret = qm_audio_acquire(&audio);
    if (ret != QM_EOK) {
        return ret;
    }
    subscription = (qm_audio_subscription_t)qm_calloc(1U, sizeof(*subscription));
    if (subscription == NULL) {
        qm_audio_release(audio);
        return -QM_ENOMEM;
    }
    ret = qm_sem_new(&subscription->idle_sem, 0U);
    if (ret != QM_EOK) {
        qm_free(subscription);
        qm_audio_release(audio);
        return ret;
    }
    subscription->audio = audio;
    subscription->callback = callback;
    subscription->context = user_ctx;
    subscription->active = QM_TRUE;
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    if (audio->closing || !audio->event_started ||
        audio->wake_subscription_count >= CONFIG_QM_AUDIO_MAX_SUBSCRIBERS) {
        closing = audio->closing;
        (void)qm_mutex_unlock(&audio->lock);
        qm_sem_free(&subscription->idle_sem);
        qm_free(subscription);
        qm_audio_release(audio);
        return closing ? -QM_EBUSY : -QM_EFULL;
    }
    audio->wake_subscriptions[audio->wake_subscription_count++] = subscription;
    (void)qm_mutex_unlock(&audio->lock);
    *out_subscription = subscription;
    return QM_EOK;
}

int qm_audio_wake_unsubscribe(qm_audio_subscription_t subscription)
{
    qm_audio_t *audio = NULL;
    uint32_t i = 0U;
    qm_bool found = QM_FALSE;

    if (subscription == NULL || subscription->audio == NULL ||
        subscription->control != NULL) {
        return -QM_EINVAL;
    }
    audio = subscription->audio;
    if (qm_task_is_current(&audio->event_task)) {
        return -QM_EBUSY;
    }
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    for (i = 0U; i < audio->wake_subscription_count; ++i) {
        if (audio->wake_subscriptions[i] == subscription) {
            found = QM_TRUE;
            break;
        }
    }
    if (!found || !subscription->active) {
        (void)qm_mutex_unlock(&audio->lock);
        return -QM_EINVAL;
    }
    subscription->active = QM_FALSE;
    for (; i + 1U < audio->wake_subscription_count; ++i) {
        audio->wake_subscriptions[i] = audio->wake_subscriptions[i + 1U];
    }
    audio->wake_subscriptions[--audio->wake_subscription_count] = NULL;
    (void)qm_mutex_unlock(&audio->lock);
    for (;;) {
        uint32_t inflight;

        (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
        inflight = subscription->callback_inflight;
        (void)qm_mutex_unlock(&audio->lock);
        if (inflight == 0U) {
            break;
        }
        if (qm_sem_is_valid(&subscription->idle_sem)) {
            while (qm_sem_wait(&subscription->idle_sem, QM_WAIT_FOREVER) != QM_EOK) {}
        }
    }
    qm_sem_free(&subscription->idle_sem);
    qm_free(subscription);
    qm_audio_release(audio);
    return QM_EOK;
}
