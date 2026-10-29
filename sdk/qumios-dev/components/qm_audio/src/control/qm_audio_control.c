/**
 * @file qm_audio_control.c
 * @brief Service/Pipeline orchestration and shared output controls. / Service/Pipeline 编排与共享输出控制。
 *
 * Service/Pipeline orchestration and shared output controls.
 */
#include "qm_audio_priv.h"
#include "qm_utils_timer.h"

#define LOG_TAG "audio_ctl"

static qm_audio_control_operation_t *find_operation(qm_audio_control_t *control, qm_audio_operation_id_t id)
{
    uint32_t i;

    if (control == NULL) {
        return NULL;
    }
    for (i = 0U; i < control->operation_count; ++i) {
        if (control->operations[i] != NULL && control->operations[i]->id == id) {
            return control->operations[i];
        }
    }
    return NULL;
}

/**
 * @brief Acquire a stable operation reference before releasing Control lock. / 释放 Control 锁前获取稳定的操作引用。
 *
 * The operation table is protected by Control::lock. Callers retain a
 * reference before dropping that lock and release it after touching handles
 * or state, so release cannot free an operation underneath them. */
static int acquire_operation_for_event(qm_audio_control_t *control, qm_audio_operation_id_t id, qm_bool allow_initializing, qm_audio_control_operation_t **out_operation)
{
    qm_audio_control_operation_t *operation;

    if (control == NULL || out_operation == NULL) {
        return -QM_EINVAL;
    }
    *out_operation = NULL;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation = find_operation(control, id);
    if (operation == NULL) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EINVAL;
    }
    if (operation->releasing || (!allow_initializing && (operation->initializing || operation->repositioning)) ||
        operation->api_users == UINT32_MAX) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EBUSY;
    }
    operation->api_users++;
    *out_operation = operation;
    (void)qm_mutex_unlock(&control->lock);
    return QM_EOK;
}

static int acquire_operation(qm_audio_control_t *control, qm_audio_operation_id_t id, qm_audio_control_operation_t **out_operation)
{
    return acquire_operation_for_event(control, id, QM_FALSE, out_operation);
}

static void release_operation_ref(qm_audio_control_operation_t *operation)
{
    qm_audio_control_t *control;

    if (operation == NULL || operation->control == NULL) {
        return;
    }
    control = operation->control;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (operation->api_users > 0U) {
        operation->api_users--;
    }
    (void)qm_mutex_unlock(&control->lock);
}

/**
 * @brief Detect an event belonging to an older output-ownership generation. / 检测属于旧输出所有权代次的事件。
 *
 * Service generations increase for a new ownership run.  Signed subtraction
 * keeps the comparison correct when the 32-bit counter wraps. */
static qm_bool stale_generation(uint32_t current, uint32_t incoming)
{
    if (current == QM_AUDIO_INVALID_GENERATION ||
        incoming == QM_AUDIO_INVALID_GENERATION) {
        return QM_FALSE;
    }
    return ((int32_t)(incoming - current) < 0) ? QM_TRUE : QM_FALSE;
}

static void destroy_push_source(qm_audio_control_operation_t *operation)
{
    if (operation == NULL || operation->push_source == NULL) return;
    qm_audio_push_source_destroy(operation->push_source);
    operation->push_source = NULL;
}

static int source_submit(qm_audio_control_t *control, qm_audio_pipeline_t *pipeline)
{
    return qm_audio_executor_submit(control->audio, pipeline);
}

/** @brief Bind only while the producer is new or acknowledged paused. / 仅在生产者未启动或已确认暂停时绑定。 */
static int bind_output(qm_audio_control_operation_t *operation)
{
    qm_audio_render_stream_t *output = NULL;
    int ret = qm_audio_service_session_bind(operation->session, &output);
    if (ret != QM_EOK) return ret;
    qm_audio_engine_render_release(operation->output);
    operation->output = output;
    return QM_EOK;
}

/** @brief Allocate an operation ID only within this Control's assigned range. / 仅在本 Control 已分配区间内分配操作 ID。 */
static int alloc_operation_id_locked(qm_audio_control_t *control, qm_audio_operation_id_t *operation_id)
{
    uint32_t attempts;

    if (control == NULL || operation_id == NULL) {
        return -QM_EINVAL;
    }
    for (attempts = 0U; attempts <= control->max_operations; ++attempts) {
        qm_audio_operation_id_t candidate = control->next_operation_id++;

        if ((control->next_operation_id & UINT16_MAX) == 0U) {
            control->next_operation_id = control->request_id_base + 1U;
        }
        if (candidate != QM_AUDIO_INVALID_REQUEST_ID &&
            find_operation(control, candidate) == NULL) {
            *operation_id = candidate;
            return QM_EOK;
        }
    }
    return -QM_EFULL;
}

static void signal_operation(qm_audio_control_operation_t *operation)
{
    if (operation != NULL && qm_sem_is_valid(&operation->state_sem)) {
        (void)qm_sem_signal(&operation->state_sem);
    }
}

/**
 * @brief Remove only a fully quiesced operation from Control ownership. / 仅从 Control 所有权中移除完全静止的操作。
 *
 * Control owns the externally visible operation state.  Service and Pipeline
 * states remain diagnostic inputs only: neither side can independently report
 * a completed product operation before both its input and device boundaries
 * have been reached.
 */
static qm_audio_control_state_t project_state_locked(const qm_audio_control_operation_t *operation)
{
    /* A stop acknowledgement includes both workers. / 停止确认包含两端工作任务。 */
    if (operation->transition == QM_AUDIO_TRANSITION_STOP)
        return operation->state;
    if (operation->control_failed ||
        operation->session_state == QM_AUDIO_SESSION_ERROR ||
        operation->pipeline_state == QM_AUDIO_PIPELINE_ERROR) {
        return QM_AUDIO_CONTROL_ERROR;
    }
    if (operation->session_state == QM_AUDIO_SESSION_PREEMPTED) {
        return QM_AUDIO_CONTROL_PREEMPTED;
    }
    if (operation->session_state == QM_AUDIO_SESSION_CANCELLED ||
        operation->pipeline_state == QM_AUDIO_PIPELINE_CANCELLED) {
        return QM_AUDIO_CONTROL_CANCELLED;
    }
    if (operation->session_state == QM_AUDIO_SESSION_FINISHED) {
        return QM_AUDIO_CONTROL_FINISHED;
    }
    if (operation->restart_pending) return QM_AUDIO_CONTROL_PAUSED;
    if (operation->pipeline_finished) {
        return QM_AUDIO_CONTROL_DRAINING;
    }
    /* Resume is not observable until both preserved stages are running. */
    if (operation->resume_in_progress ||
        (operation->session_state == QM_AUDIO_SESSION_PAUSED &&
         operation->pipeline_state == QM_AUDIO_PIPELINE_PAUSED)) {
        return QM_AUDIO_CONTROL_PAUSED;
    }
    if (!operation->service_ready ||
        operation->pipeline_state == QM_AUDIO_PIPELINE_NEW) {
        return QM_AUDIO_CONTROL_WAITING;
    }
    return QM_AUDIO_CONTROL_PLAYING;
}

static qm_audio_event_type_t event_for_state_locked(qm_audio_control_operation_t *operation, qm_audio_control_state_t previous_state)
{
    if (!operation->accepted_event_sent &&
        operation->state != QM_AUDIO_CONTROL_NEW) {
        operation->accepted_event_sent = QM_TRUE;
        return QM_AUDIO_EVENT_ACCEPTED;
    }
    if (qm_audio_control_terminal(operation->state)) {
        if (operation->terminal_event_sent) {
            return QM_AUDIO_EVENT_NONE;
        }
        operation->terminal_event_sent = QM_TRUE;
        switch (operation->state) {
            case QM_AUDIO_CONTROL_FINISHED:
                return QM_AUDIO_EVENT_FINISHED;
            case QM_AUDIO_CONTROL_CANCELLED:
                return QM_AUDIO_EVENT_CANCELLED;
            case QM_AUDIO_CONTROL_PREEMPTED:
                return QM_AUDIO_EVENT_PREEMPTED;
            case QM_AUDIO_CONTROL_ERROR:
            default:
                return QM_AUDIO_EVENT_ERROR;
        }
    }
    if (operation->state == QM_AUDIO_CONTROL_PAUSED &&
        previous_state != QM_AUDIO_CONTROL_PAUSED) {
        return QM_AUDIO_EVENT_PAUSED;
    }
    if (operation->state == QM_AUDIO_CONTROL_PLAYING) {
        if (previous_state == QM_AUDIO_CONTROL_PAUSED) {
            return QM_AUDIO_EVENT_RESUMED;
        }
        if (!operation->started_event_sent) {
            operation->started_event_sent = QM_TRUE;
            return QM_AUDIO_EVENT_STARTED;
        }
    }
    return QM_AUDIO_EVENT_NONE;
}

static void emit_control_event(qm_audio_control_operation_t *operation, const qm_audio_event_t *event)
{
    qm_audio_control_t *control;
    qm_audio_event_msg_t msg = {0};

    if (operation == NULL || event == NULL || operation->control == NULL) {
        return;
    }
    control = operation->control;
    msg.type = QM_AUDIO_EVENT_MSG_DISPATCH;
    msg.control = control;
    msg.u.event = *event;
    (void)qm_audio_event_post(control->audio, &msg);
}

/**
 * @brief Project lower-layer facts into one business-visible Control state. / 将底层事实投影为面向业务的 Control 状态。
 *
 * Recompute the projected Control state under the lock, derive the event for
 * the transition, then signal and emit outside the lock.  Setup suppresses
 * delivery until play() has published its operation id; the caller can flush
 * the resulting state transitions afterwards. */
static qm_bool project_and_notify(qm_audio_control_operation_t *operation)
{
    qm_audio_control_t *control;
    qm_audio_control_state_t previous_state;
    qm_audio_event_t event;
    qm_audio_event_type_t type;

    if (operation == NULL || operation->control == NULL) {
        return QM_FALSE;
    }
    control = operation->control;
    qm_memset(&event, 0, sizeof(event));
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    previous_state = operation->state;
    operation->state = project_state_locked(operation);
    if (operation->state == QM_AUDIO_CONTROL_FINISHED) {
        uint32_t duration, flags;
        qm_audio_pipeline_get_progress(operation->pipeline, &duration, &flags);
        if ((flags & QM_AUDIO_PROGRESS_DURATION_VALID) != 0U &&
            (flags & QM_AUDIO_PROGRESS_DURATION_ESTIMATED) == 0U) {
            /* Whole-media frame counting avoids adding two rounded ms values
             * after a seek at rates such as 44.1 kHz. Drain is confirmed here. */
            operation->position_ms = duration;
        }
    }
    if (qm_audio_control_terminal(operation->state)) {
        operation->seeking = operation->restart_pending = operation->rewind_on_resume = QM_FALSE;
        operation->transition = QM_AUDIO_TRANSITION_NONE;
    }
    type = operation->initializing ? QM_AUDIO_EVENT_NONE :
           event_for_state_locked(operation, previous_state);
    if (type != QM_AUDIO_EVENT_NONE) {
        event.type = type;
        event.request_id = operation->id;
        event.operation_id = operation->id;
        event.generation = operation->generation;
        event.result = operation->result;
        event.position_ms = operation->position_ms;
        event.reason = operation->reason;
        event.timestamp_ms = qm_now_ms();
    }
    (void)qm_mutex_unlock(&control->lock);

    signal_operation(operation);
    if (type != QM_AUDIO_EVENT_NONE) {
        emit_control_event(operation, &event);
    }
    return type != QM_AUDIO_EVENT_NONE ? QM_TRUE : QM_FALSE;
}

/**
 * @brief Preserve accepted-before-started lifecycle ordering. / 保证接受事件先于启动或终止事件。
 *
 * Accepted is intentionally ordered before STARTED/terminal.  A state can
 * advance several times while setup is suppressed, so drain the small
 * projection state machine until it reports no additional public transition. */
static void flush_projected_events(qm_audio_control_operation_t *operation)
{
    uint32_t passes = 0U;

    while (operation != NULL && passes++ < 4U && project_and_notify(operation)) {
        /* Each pass consumes at most one pending lifecycle transition. */
    }
}

static void update_service_state(qm_audio_control_operation_t *operation, const qm_audio_event_t *event)
{
    qm_audio_control_t *control;

    if (operation == NULL || event == NULL || operation->control == NULL) {
        return;
    }
    control = operation->control;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (stale_generation(operation->generation, event->generation)) {
        (void)qm_mutex_unlock(&control->lock);
        return;
    }
    if (event->generation != QM_AUDIO_INVALID_GENERATION) {
        operation->generation = event->generation;
    }
    if (event->position_ms != 0U) {
        operation->position_ms = event->position_ms;
    }
    switch (event->type) {
        case QM_AUDIO_EVENT_ACCEPTED:
            operation->session_state = QM_AUDIO_SESSION_QUEUED;
            break;
        case QM_AUDIO_EVENT_STARTED:
        case QM_AUDIO_EVENT_RESUMED:
            operation->service_ready = QM_TRUE;
            operation->session_state = QM_AUDIO_SESSION_ACTIVE;
            break;
        case QM_AUDIO_EVENT_PAUSED:
            operation->session_state = QM_AUDIO_SESSION_PAUSED;
            break;
        case QM_AUDIO_EVENT_FINISHED:
            operation->session_state = QM_AUDIO_SESSION_FINISHED;
            operation->result = QM_EOK;
            break;
        case QM_AUDIO_EVENT_PREEMPTED:
            operation->session_state = QM_AUDIO_SESSION_PREEMPTED;
            operation->result = event->result;
            operation->reason = event->reason;
            break;
        case QM_AUDIO_EVENT_CANCELLED:
            operation->session_state = QM_AUDIO_SESSION_CANCELLED;
            if (operation->pipeline_state != QM_AUDIO_PIPELINE_ERROR && !operation->control_failed)
                operation->result = event->result;
            operation->reason = event->reason;
            break;
        case QM_AUDIO_EVENT_ERROR:
            operation->session_state = QM_AUDIO_SESSION_ERROR;
            operation->result = event->result;
            operation->reason = event->reason;
            break;
        default:
            break;
    }
    (void)qm_mutex_unlock(&control->lock);
    project_and_notify(operation);
}

/** @brief Start an operation Pipeline exactly once after Service route admission. / Service 通道就绪后仅启动一次操作 Pipeline。 */
static void maybe_start_pipeline(qm_audio_control_operation_t *operation)
{
    qm_audio_pipeline_t *pipeline = NULL;
    qm_audio_control_t *control;
    int ret;

    if (operation == NULL || operation->control == NULL) {
        return;
    }
    control = operation->control;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (operation->service_ready && operation->session != NULL &&
        operation->pipeline != NULL &&
        !operation->pipeline_start_requested &&
        !qm_audio_control_terminal(operation->state)) {
        operation->pipeline_start_requested = QM_TRUE;
        pipeline = operation->pipeline;
    }
    (void)qm_mutex_unlock(&control->lock);
    if (pipeline == NULL) {
        return;
    }
    ret = bind_output(operation);
    if (ret == QM_EOK) ret = qm_audio_pipeline_prepare(pipeline);
    if (ret == QM_EOK) {
        ret = source_submit(control, pipeline);
    }
    if (ret != QM_EOK) {
        qm_audio_pipeline_fail(pipeline, ret);
    }
}

/**
 * @brief Finish an operation only after the physical output boundary. / 仅在物理输出边界后完成操作。
 *
 * The source worker owns completion through the physical output boundary.
 * Keeping this wait out of the event task leaves stop and release commands
 * able to interrupt a slow hardware drain. */
static int complete_output(qm_audio_control_operation_t *operation)
{
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    int ret = QM_EOK;

    if (operation == NULL || operation->control == NULL) {
        return -QM_EINVAL;
    }
    /* One operation owns one total drain budget across retryable drain calls.
     * / 一次 operation 在可重试的 drain 调用间共享同一个总预算。 */
    qm_utils_time_countdown_ms(&deadline, operation->control->operation_timeout_ms);
    for (;;) {
        left = operation->control->operation_timeout_ms == QM_WAIT_FOREVER ?
            QM_WAIT_FOREVER : qm_utils_time_left(&deadline);
        if (operation->control->operation_timeout_ms != QM_WAIT_FOREVER && left == 0U) {
            return -QM_ETIMEOUT;
        }
        ret = operation->output == NULL ? -QM_EINTR :
            qm_audio_engine_render_drain(operation->output,
                                         left);
        if (ret != -QM_ETIMEOUT) {
            return ret;
        }
    }
}

void qm_audio_control_service_event_impl(qm_audio_control_t *control, const qm_audio_event_t *event)
{
    qm_audio_control_operation_t *operation;

    if (control == NULL || event == NULL) {
        return;
    }
    /* Service can synchronously report acceptance while play_impl is still
     * constructing its Pipeline. Keep that event in the projection rather
     * than losing the public ACCEPTED/STARTED sequence. External APIs remain
     * blocked by the regular acquire_operation() path until setup finishes. */
    if (acquire_operation_for_event(control, event->request_id, QM_TRUE,
                                    &operation) != QM_EOK) {
        return;
    }

    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (stale_generation(operation->generation, event->generation)) {
        (void)qm_mutex_unlock(&control->lock);
        release_operation_ref(operation);
        return;
    }
    (void)qm_mutex_unlock(&control->lock);
    update_service_state(operation, event);
    /* Revoking the output route also revokes its source-worker execution.
     * In particular, a paused pipeline must be woken before the next play. */
    if (operation->pipeline != NULL &&
        (event->type == QM_AUDIO_EVENT_PREEMPTED ||
         event->type == QM_AUDIO_EVENT_CANCELLED ||
         event->type == QM_AUDIO_EVENT_ERROR)) {
        (void)qm_audio_pipeline_cancel_for_stop(operation->pipeline);
    }
    if (event->type == QM_AUDIO_EVENT_STARTED) {
        maybe_start_pipeline(operation);
    }
    release_operation_ref(operation);
}

static void service_event(const qm_audio_event_t *event, void *user_ctx)
{
    qm_audio_control_t *control = user_ctx;
    qm_audio_event_msg_t msg = {0};

    if (control == NULL || event == NULL) return;
    msg.type = QM_AUDIO_EVENT_MSG_LOWER;
    msg.control = control;
    msg.u.lower.source = QM_AUDIO_LOWER_SERVICE;
    msg.u.lower.event = *event;
    (void)qm_audio_event_post(control->audio, &msg);
}

/**
 * @brief Submit a packet through the run's stable output lease. / 通过本轮稳定输出句柄提交完整包。
 */
static int write_packet(const qm_audio_data_t *packet, void *user_ctx)
{
    qm_audio_control_operation_t *operation = user_ctx;
    if (operation == NULL || packet == NULL) return -QM_EINVAL;
    return operation->output == NULL ? -QM_EINTR :
        qm_audio_engine_render_write_packet(operation->output, packet, QM_WAIT_FOREVER);
}

static int write_pcm(const qm_audio_data_t *buffer, void *user_ctx)
{
    qm_audio_control_operation_t *operation =
        (qm_audio_control_operation_t *)user_ctx;
    qm_audio_data_t suffix = {0};
    uint32_t offset = 0U;
    uint32_t written = 0U;
    int ret = QM_EOK;

    if (operation == NULL || operation->control == NULL || buffer == NULL) {
        return -QM_EINVAL;
    }
    if (operation->output == NULL) {
        return -QM_EINTR;
    }
    while (offset < buffer->size ||
           (buffer->size == 0U && offset == 0U)) {
        suffix = *buffer;
        written = 0U;
        suffix.data = buffer->size == 0U ? NULL :
                      (const uint8_t *)buffer->data + offset;
        suffix.size = buffer->size - offset;
        if (offset != 0U) {
            suffix.flags &= ~QM_AUDIO_DATA_DISCONTINUITY;
        }
        ret = qm_audio_engine_render_write(operation->output, &suffix,
                                             QM_WAIT_FOREVER, &written);
        if (ret != QM_EOK) {
            return ret;
        }
        if (buffer->size == 0U) {
            return QM_EOK;
        }
        if (written == 0U || written > suffix.size) {
            return -QM_EIO;
        }
        offset += written;
    }
    return QM_EOK;
}

/**
 * @brief Project one retained Pipeline fact. / 投影一条已保活的 Pipeline 事实。
 *
 * The caller owns one operation reference for this entire call. / 调用方在整个调用
 * 期间持有一个操作引用。
 */
static void apply_pipeline_fact(qm_audio_control_operation_t *operation, const qm_audio_event_t *event)
{
    qm_audio_control_t *control;

    if (operation == NULL || event == NULL || operation->control == NULL) {
        return;
    }
    control = operation->control;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (event->generation != operation->pipeline_generation) {
        (void)qm_mutex_unlock(&control->lock);
        return;
    }
    if (qm_audio_control_terminal(operation->state)) {
        (void)qm_mutex_unlock(&control->lock);
        return;
    }
    if (event->position_ms != 0U) {
        operation->position_ms = event->position_ms;
    }
    switch (event->type) {
        case QM_AUDIO_EVENT_SEEKED:
            operation->position_ms = event->position_ms;
            operation->seeking = QM_FALSE;
            break;
        case QM_AUDIO_EVENT_STARTED:
            operation->pipeline_state = QM_AUDIO_PIPELINE_RUNNING;
            break;
        case QM_AUDIO_EVENT_PAUSED:
            operation->pipeline_state = QM_AUDIO_PIPELINE_PAUSED;
            break;
        case QM_AUDIO_EVENT_RESUMED:
            operation->pipeline_state = QM_AUDIO_PIPELINE_RUNNING;
            break;
        case QM_AUDIO_EVENT_FINISHED:
            operation->pipeline_state = QM_AUDIO_PIPELINE_FINISHED;
            operation->pipeline_finished = QM_TRUE;
            break;
        case QM_AUDIO_EVENT_CANCELLED:
            operation->pipeline_state = QM_AUDIO_PIPELINE_CANCELLED;
            break;
        case QM_AUDIO_EVENT_ERROR:
            operation->pipeline_state = QM_AUDIO_PIPELINE_ERROR;
            operation->result = event->result;
            operation->reason = event->reason;
            break;
        default:
            break;
    }
    (void)qm_mutex_unlock(&control->lock);
    if (event->type == QM_AUDIO_EVENT_NONE) {
        if (event->result == QM_EOK) {
            /* The producer has already completed the physical drain. */
            (void)qm_audio_service_session_request(operation->session,
                QM_AUDIO_SERVICE_ACTION_FINISH, 0U);
        } else if (event->result != -QM_EINTR && event->result != -QM_EBUSY) {
            (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
            operation->pipeline_state = QM_AUDIO_PIPELINE_ERROR;
            operation->result = event->result;
            (void)qm_mutex_unlock(&control->lock);
            (void)qm_audio_control_stop_impl(control, operation->id, 0U);
        }
    }
    project_and_notify(operation);
    if (event->type == QM_AUDIO_EVENT_SEEKED && !qm_audio_control_terminal(operation->state)) {
        qm_audio_event_t copy = *event;
        copy.generation = operation->generation;
        emit_control_event(operation, &copy);
    }
}

/**
 * @brief Ingest one Pipeline event and retain its ordering constraints. / 接收一条 Pipeline 事件并保持其顺序约束。
 *
 * The source worker starts the physical drain;
 * this event-task half only projects the result and publishes notifications. */
void qm_audio_control_pipeline_event_impl(qm_audio_control_t *control, const qm_audio_event_t *event)
{
    qm_audio_control_operation_t *operation;

    if (control == NULL || event == NULL) {
        return;
    }
    /* A Pipeline can also finish during play setup for an empty memory source.
     * Its terminal state is an internal dependency and must not be discarded. */
    if (acquire_operation_for_event(control, event->request_id, QM_TRUE,
                                    &operation) != QM_EOK) {
        return;
    }
    apply_pipeline_fact(operation, event);
    release_operation_ref(operation);
}

static int write_bytes(const qm_audio_data_t *data, void *user_ctx)
{
    return write_pcm(data, user_ctx);
}

static void store_pipeline_fact(qm_audio_control_operation_t *operation, const qm_audio_event_t *event)
{
    qm_audio_control_t *control;
    qm_audio_t *audio;

    if (operation == NULL || event == NULL || operation->control == NULL ||
        operation->control->audio == NULL) {
        return;
    }
    control = operation->control;
    audio = control->audio;
    /* Source is the normal sole producer, but the instance lock also makes
     * direct producers share the same fact sequence. */
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (event->generation != operation->pipeline_generation) {
        (void)qm_mutex_unlock(&control->lock);
        (void)qm_mutex_unlock(&audio->lock);
        return;
    }
    if (++audio->pipeline_fact_sequence == 0U) {
        audio->pipeline_fact_sequence++;
    }
    operation->facts[event->type].sequence = audio->pipeline_fact_sequence;
    operation->facts[event->type].generation = event->generation;
    operation->facts[event->type].timestamp_ms = event->timestamp_ms;
    operation->facts[event->type].position_ms = event->position_ms;
    operation->facts[event->type].reason = event->reason;
    operation->facts[event->type].result = event->result;
    (void)qm_mutex_unlock(&control->lock);
    (void)qm_mutex_unlock(&audio->lock);
    qm_audio_event_wake(audio);
}

static qm_bool fact_precedes(uint32_t candidate, uint32_t current)
{
    return (int32_t)(candidate - current) < 0 ? QM_TRUE : QM_FALSE;
}

static qm_audio_control_operation_t *retain_oldest_fact_locked(qm_audio_control_t *control, uint32_t *out_type, uint32_t *out_sequence)
{
    qm_audio_control_operation_t *candidate = NULL;
    uint32_t candidate_type = 0U;
    uint32_t candidate_sequence = 0U;
    uint32_t i;

    for (i = 0U; i < control->operation_count; ++i) {
        qm_audio_control_operation_t *operation = control->operations[i];
        uint32_t type;

        if (operation == NULL || operation->releasing ||
            operation->api_users == UINT32_MAX) {
            continue;
        }
        for (type = 0U; type <= QM_AUDIO_EVENT_SEEKED; ++type) {
            uint32_t sequence = operation->facts[type].sequence;

            if (sequence != 0U && (candidate == NULL ||
                fact_precedes(sequence, candidate_sequence))) {
                candidate = operation;
                candidate_type = type;
                candidate_sequence = sequence;
            }
        }
    }
    if (candidate != NULL) {
        candidate->api_users++;
        *out_type = candidate_type;
        *out_sequence = candidate_sequence;
    }
    return candidate;
}

/**
 * @brief Take the oldest durable Pipeline fact. / 取走最早的持久 Pipeline 事实。
 *
 * The small, fixed fact table avoids a second queue and its separate lifetime.
 * `audio->lock` blocks concurrent publication while the winner is selected.
 * / 小型固定事实表避免引入第二个队列及其独立生命周期；选择期间由 `audio->lock`
 * 阻止并发发布。
 */
static qm_bool take_oldest_pipeline_fact(qm_audio_t *audio, qm_audio_control_operation_t **out_operation, qm_audio_event_t *out_event)
{
    qm_audio_control_t *control;
    qm_audio_control_operation_t *selected = NULL;
    uint32_t selected_type = 0U;
    uint32_t selected_sequence = 0U;
    qm_bool taken = QM_FALSE;

    if (audio == NULL || out_operation == NULL ||
        out_event == NULL || !qm_mutex_is_valid(&audio->lock)) {
        return QM_FALSE;
    }
    *out_operation = NULL;
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    for (control = audio->controls; control != NULL;
         control = control->event_next) {
        qm_audio_control_operation_t *candidate;
        uint32_t candidate_type = 0U;
        uint32_t candidate_sequence = 0U;

        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        candidate = retain_oldest_fact_locked(control, &candidate_type,
                                              &candidate_sequence);
        (void)qm_mutex_unlock(&control->lock);
        if (candidate == NULL) {
            continue;
        }
        if (selected == NULL || fact_precedes(candidate_sequence,
                                              selected_sequence)) {
            if (selected != NULL) {
                release_operation_ref(selected);
            }
            selected = candidate;
            selected_type = candidate_type;
            selected_sequence = candidate_sequence;
        } else {
            release_operation_ref(candidate);
        }
    }
    if (selected != NULL) {
        (void)qm_mutex_lock(&selected->control->lock, QM_WAIT_FOREVER);
        if (!selected->releasing &&
            selected->facts[selected_type].sequence == selected_sequence) {
            out_event->type = (qm_audio_event_type_t)selected_type;
            out_event->request_id = out_event->operation_id = selected->id;
            out_event->generation = selected->facts[selected_type].generation;
            out_event->timestamp_ms = selected->facts[selected_type].timestamp_ms;
            out_event->position_ms = selected->facts[selected_type].position_ms;
            out_event->reason = selected->facts[selected_type].reason;
            out_event->result = selected->facts[selected_type].result;
            selected->facts[selected_type].sequence = 0U;
            *out_operation = selected;
            taken = QM_TRUE;
        }
        (void)qm_mutex_unlock(&selected->control->lock);
        if (!taken) {
            release_operation_ref(selected);
        }
    }
    (void)qm_mutex_unlock(&audio->lock);
    return taken;
}

static void dispatch_pipeline_facts(qm_audio_t *audio)
{
    qm_audio_control_operation_t *operation;
    qm_audio_event_t event = {0};

    while (take_oldest_pipeline_fact(audio, &operation, &event)) {
        apply_pipeline_fact(operation, &event);
        release_operation_ref(operation);
    }
}

void qm_audio_dispatch_pipeline_facts(qm_audio_t *audio)
{
    dispatch_pipeline_facts(audio);
}

static qm_bool set_seek_origin(qm_audio_control_operation_t *operation, const qm_audio_event_t *event)
{
    qm_audio_control_t *control;
    int ret;

    if (operation == NULL || event == NULL || operation->control == NULL) {
        return QM_FALSE;
    }
    control = operation->control;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (operation->releasing || event->generation != operation->pipeline_generation ||
        operation->session == NULL) {
        (void)qm_mutex_unlock(&control->lock);
        return QM_FALSE;
    }
    /* The next source write follows this callback, so the media clock must be
     * ready before returning to Pipeline. / 下一次源写入紧随回调，返回 Pipeline 前
     * 必须先准备好媒体时钟。 */
    ret = qm_audio_service_session_set_origin(operation->session,
                                              event->position_ms);
    if (ret == QM_EOK) {
        operation->position_ms = event->position_ms;
        operation->seeking = QM_FALSE;
    }
    (void)qm_mutex_unlock(&control->lock);
    return ret == QM_EOK ? QM_TRUE : QM_FALSE;
}

static void pipeline_event(const qm_audio_event_t *event, void *user_ctx)
{
    qm_audio_control_operation_t *operation = user_ctx;
    qm_audio_session_t *session = NULL;

    if (operation == NULL || event == NULL) return;
    if (event->type == QM_AUDIO_EVENT_SEEKED &&
        !set_seek_origin(operation, event)) {
        return;
    }
    store_pipeline_fact(operation, event);
    if (event->type == QM_AUDIO_EVENT_FINISHED) {
        qm_audio_event_t completion = *event;
        completion.type = QM_AUDIO_EVENT_NONE;
        completion.result = complete_output(operation);
        store_pipeline_fact(operation, &completion);
    } else if (event->type == QM_AUDIO_EVENT_ERROR) {
        (void)qm_mutex_lock(&operation->control->lock, QM_WAIT_FOREVER);
        if (!operation->releasing &&
            event->generation == operation->pipeline_generation) {
            session = operation->session;
        }
        (void)qm_mutex_unlock(&operation->control->lock);
        if (session != NULL) {
            (void)qm_audio_service_session_request(session, QM_AUDIO_SERVICE_ACTION_STOP, 0U);
        }
    }
}

int qm_audio_control_create(qm_audio_control_t **out_control)
{
    qm_audio_t *audio;
    qm_audio_control_t *control;
    uint32_t capacity;
    int ret;

    if (out_control == NULL) {
        return -QM_EINVAL;
    }
    *out_control = NULL;
    capacity = CONFIG_QM_AUDIO_CONTROL_MAX_OPERATIONS;
    if (capacity > UINT16_MAX) return -QM_EINVAL;
    audio = NULL;
    ret = qm_audio_acquire(&audio);
    if (ret != QM_EOK) return ret;
    control = (qm_audio_control_t *)qm_calloc(1U, sizeof(*control));
    if (control == NULL) {
        qm_audio_release(audio);
        return -QM_ENOMEM;
    }
    control->audio = audio;
    control->operations = (qm_audio_control_operation_t **)qm_calloc(
        capacity, sizeof(*control->operations));
    if (control->operations == NULL) {
        ret = -QM_ENOMEM;
        goto fail;
    }
    ret = qm_mutex_new(&control->lock);
    if (ret != QM_EOK) {
        ret = -QM_EIO;
        goto fail;
    }
    control->service = audio->service;
    control->engine = audio->engine;
    control->max_operations = capacity;
    control->operation_timeout_ms = CONFIG_QM_AUDIO_CONTROL_OPERATION_TIMEOUT_MS;
    control->subscriptions = (qm_audio_subscription_t *)qm_calloc(
        CONFIG_QM_AUDIO_MAX_SUBSCRIBERS, sizeof(*control->subscriptions));
    if (control->subscriptions == NULL) {
        ret = -QM_ENOMEM;
        goto fail;
    }
    ret = qm_audio_engine_get_output_stream_format(control->engine, &control->output_format);
    if (ret != QM_EOK) {
        goto fail;
    }
    ret = qm_audio_service_alloc_request_id_base(control->service,
                                                 &control->request_id_base);
    if (ret != QM_EOK) {
        goto fail;
    }
    control->next_operation_id = control->request_id_base + 1U;
    ret = qm_audio_event_runtime_init(control);
    if (ret != QM_EOK) {
        goto fail;
    }
    *out_control = control;
    return QM_EOK;

fail:
    if (control->event_dispatch_active) {
        (void)qm_audio_event_runtime_shutdown(control, QM_WAIT_FOREVER);
    }
    if (qm_mutex_is_valid(&control->lock)) {
        (void)qm_audio_event_runtime_destroy(control);
        (void)qm_mutex_free(&control->lock);
    }
    qm_free(control->subscriptions);
    qm_free(control->operations);
    qm_free(control);
    qm_audio_release(audio);
    return ret;
}

int qm_audio_control_shutdown(qm_audio_control_t *control, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;
    uint32_t i;
    int ret = QM_EOK;

    if (control == NULL || !qm_mutex_is_valid(&control->lock)) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (control->shutdown_active) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EBUSY;
    }
    /* Keep the table stable until this attempt returns. New submissions are
     * rejected, and a concurrent release cannot remove a traversed entry. */
    for (i = 0U; i < control->operation_count; ++i) {
        if (control->operations[i]->releasing ||
            control->operations[i]->initializing) {
            (void)qm_mutex_unlock(&control->lock);
            return -QM_EBUSY;
        }
    }
    control->shutdown_active = QM_TRUE;
    control->shutting_down = QM_TRUE;
    (void)qm_mutex_unlock(&control->lock);
    for (i = 0U; i < control->operation_count; ++i) {
        qm_audio_control_operation_t *operation = control->operations[i];
        qm_bool terminal;

        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        terminal = qm_audio_control_terminal(operation->state);
        (void)qm_mutex_unlock(&control->lock);
        if (!terminal) {
            int current = qm_audio_control_stop(control, operation->id, 0U);
            if (ret == QM_EOK && current != QM_EOK) {
                ret = current;
            }
        }
    }
    for (i = 0U; i < control->operation_count; ++i) {
        qm_audio_control_operation_t *operation = control->operations[i];
        uint32_t left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                        qm_utils_time_left(&deadline);
        int current = qm_audio_control_wait(control, operation->id, left);
        if (current == -QM_ETIMEOUT && ret == QM_EOK) {
            qm_bool terminal;

            /* wait() also returns the operation's terminal error. A media or
             * admission timeout is already complete and must not prevent the
             * remaining shutdown barriers from running. */
            (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
            terminal = operation != NULL &&
                qm_audio_control_terminal(operation->state);
            (void)qm_mutex_unlock(&control->lock);
            if (!terminal) {
                ret = current;
            }
        }
        if (operation != NULL && operation->pipeline != NULL) {
            left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                   qm_utils_time_left(&deadline);
            current = qm_audio_pipeline_wait(operation->pipeline, left);
            if (current == -QM_ETIMEOUT) {
                if (ret == QM_EOK) ret = current;
            }
        }
    }
    if (ret == QM_EOK) {
        uint32_t left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
                        qm_utils_time_left(&deadline);
        int current = qm_audio_event_runtime_shutdown(control, left);
        if (current != QM_EOK) {
            ret = current;
        }
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    control->shutdown_active = QM_FALSE;
    (void)qm_mutex_unlock(&control->lock);
    return ret;
}

int qm_audio_control_destroy(qm_audio_control_t *control)
{
    qm_audio_t *audio;
    int ret;

    if (control == NULL || !qm_mutex_is_valid(&control->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (!control->shutting_down || control->shutdown_active ||
        control->operation_count != 0U ||
        control->subscription_count != 0U) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EBUSY;
    }
    (void)qm_mutex_unlock(&control->lock);
    /* Every session is already released; wait for any in-flight notice that
     * borrowed this Control as its sink context before freeing the handle. */
    ret = qm_audio_service_wait_event_idle(control->service, QM_WAIT_FOREVER);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_audio_event_runtime_destroy(control);
    if (ret != QM_EOK) {
        return ret;
    }
    qm_free(control->subscriptions);
    (void)qm_mutex_free(&control->lock);
    qm_free(control->operations);
    audio = control->audio;
    qm_free(control);
    qm_audio_release(audio);
    return QM_EOK;
}

int qm_audio_control_play_impl(qm_audio_control_t *control, const qm_audio_control_play_request_t *request, qm_audio_operation_id_t *operation_id)
{
    qm_audio_control_operation_t *operation;
    qm_audio_session_request_t session_request;
    qm_audio_pipeline_config_t pipeline_config;
    qm_audio_session_state_t state;
    qm_audio_control_play_request_t normalized_request;
    qm_audio_operation_id_t allocated_id;
    qm_bool start = QM_FALSE;
    int ret;

    if (operation_id != NULL) {
        *operation_id = QM_AUDIO_INVALID_REQUEST_ID;
    }
    if (control == NULL || request == NULL || operation_id == NULL ||
        !qm_audio_stream_format_valid(&control->output_format)) {
        return -QM_EINVAL;
    }
    if (request->codec.framing == QM_AUDIO_CODEC_FRAMING_PACKET &&
        request->source.type != QM_AUDIO_SOURCE_PUSH) return -QM_EINVAL;
    normalized_request = *request;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (control->shutting_down || control->operation_count >= control->max_operations) {
        (void)qm_mutex_unlock(&control->lock);
        return control->shutting_down ? -QM_EBUSY : -QM_EFULL;
    }
    ret = alloc_operation_id_locked(control, &allocated_id);
    if (ret != QM_EOK) {
        (void)qm_mutex_unlock(&control->lock);
        return ret;
    }
    operation = (qm_audio_control_operation_t *)qm_calloc(1U, sizeof(*operation));
    if (operation == NULL) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_ENOMEM;
    }
    operation->control = control;
    operation->initializing = QM_TRUE;
    operation->api_users = 1U;
    operation->id = allocated_id;
    operation->state = QM_AUDIO_CONTROL_NEW;
    operation->session_state = QM_AUDIO_SESSION_NEW;
    operation->pipeline_state = QM_AUDIO_PIPELINE_NEW;
    operation->generation = QM_AUDIO_INVALID_GENERATION;
    operation->pipeline_generation = 1U;
    if (normalized_request.source.type == QM_AUDIO_SOURCE_PUSH) {
        ret = qm_audio_push_source_create(
            normalized_request.source.value.push.buffer_bytes,
            &operation->push_source);
        if (ret != QM_EOK) {
            qm_free(operation);
            (void)qm_mutex_unlock(&control->lock);
            return ret;
        }
        normalized_request.source.value.push.context = operation->push_source;
    }
    ret = qm_sem_new(&operation->state_sem, 0);
    if (ret != QM_EOK) {
        destroy_push_source(operation);
        qm_free(operation);
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EIO;
    }
    control->operations[control->operation_count++] = operation;
    (void)qm_mutex_unlock(&control->lock);

    /* Validate and allocate the source before admission can preempt a route.
     * No source or device I/O runs during Pipeline creation. */
    qm_memset(&pipeline_config, 0, sizeof(pipeline_config));
    pipeline_config.output_format = control->output_format;
    pipeline_config.input_format = normalized_request.codec;
    pipeline_config.source = normalized_request.source;
    pipeline_config.resume = normalized_request.resume;
    if (control->output_format.codec == QM_AUDIO_CODEC_PCM)
        pipeline_config.write_pcm = write_pcm;
    else if (qm_audio_encoded_bytes(&control->output_format))
        pipeline_config.write_bytes = write_bytes;
    else
        pipeline_config.write_packet = write_packet;
    pipeline_config.user_ctx = operation;
    ret = qm_audio_pipeline_create(&pipeline_config, &operation->pipeline);
    if (ret != QM_EOK) {
        goto fail_operation;
    }
    ret = qm_audio_pipeline_set_event_sink(operation->pipeline,
                                           pipeline_event, operation);
    if (ret != QM_EOK) {
        (void)qm_audio_pipeline_destroy(operation->pipeline);
        operation->pipeline = NULL;
        goto fail_operation;
    }
    session_request = (qm_audio_session_request_t){
        .request_id = operation->id,
        .priority = normalized_request.priority,
        .policy = normalized_request.admission,
        .delivery = QM_AUDIO_DELIVERY_RELIABLE,
        .acquire_timeout_ms = normalized_request.admission_timeout_ms,
    };
    ret = qm_audio_service_session_acquire_async(control->service, &session_request,
                                          service_event, control,
                                          &operation->session);
    if (ret != QM_EOK) {
        (void)qm_audio_pipeline_destroy(operation->pipeline);
        operation->pipeline = NULL;
        goto fail_operation;
    }
    (void)qm_audio_service_session_get_state(operation->session, &state);
    /* 断点续播: acquire 后立即锚定 position base(不限是否已 ACTIVE), 使排队/等待
     * 阶段读秒即为断点, 而非等到首次解码输出或 STARTED 后才从 0 跳回。set_origin
     * 内部会拒绝 terminal, 对刚创建的会话必成功。 */
    if (normalized_request.resume != NULL &&
        normalized_request.resume->position_ms != 0U) {
        (void)qm_audio_service_session_set_origin(operation->session,
                                                  normalized_request.resume->position_ms);
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation->session_state = state;
    operation->service_ready = (state == QM_AUDIO_SESSION_ACTIVE) ? QM_TRUE : QM_FALSE;
    if (operation->service_ready) {
        start = QM_TRUE;
    }
    (void)qm_mutex_unlock(&control->lock);
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation->initializing = QM_FALSE;
    (void)qm_mutex_unlock(&control->lock);
    *operation_id = operation->id;
    if (start) {
        maybe_start_pipeline(operation);
    }
    flush_projected_events(operation);
    release_operation_ref(operation);
    return QM_EOK;

fail_operation:
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    {
        uint32_t i;
        for (i = 0U; i < control->operation_count; ++i) {
            if (control->operations[i] == operation) {
                control->operations[i] = control->operations[control->operation_count - 1U];
                control->operations[--control->operation_count] = NULL;
                break;
            }
        }
    }
    (void)qm_mutex_unlock(&control->lock);
    if (qm_sem_is_valid(&operation->state_sem)) {
        (void)qm_sem_free(&operation->state_sem);
    }
    destroy_push_source(operation);
    qm_free(operation);
    return ret;
}

static int pause_operation(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, qm_bool yield_output)
{
    qm_audio_control_operation_t *operation;
    qm_audio_pipeline_t *pipeline;
    qm_audio_session_t *session;
    qm_audio_pipeline_state_t pipeline_state;
    int ret;

    if (control == NULL || !qm_mutex_is_valid(&control->lock)) return -QM_EINVAL;
    ret = acquire_operation(control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (operation->transition == QM_AUDIO_TRANSITION_NONE &&
        operation->pipeline != NULL && operation->session != NULL &&
        (operation->state == QM_AUDIO_CONTROL_PLAYING ||
         (yield_output && operation->state == QM_AUDIO_CONTROL_PAUSED))) {
        pipeline = operation->pipeline;
        session = operation->session;
    } else {
        pipeline = NULL;
        session = NULL;
    }
    (void)qm_mutex_unlock(&control->lock);
    if (pipeline == NULL || session == NULL) {
        release_operation_ref(operation);
        return -QM_EBUSY;
    }
    (void)qm_audio_pipeline_get_state(pipeline, &pipeline_state, NULL);
    ret = yield_output && (operation->restart_pending || pipeline_state == QM_AUDIO_PIPELINE_PAUSED) ? QM_EOK :
          qm_audio_pipeline_pause(pipeline);
    if (ret != QM_EOK) {
        release_operation_ref(operation);
        return ret;
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation->transition = yield_output ? QM_AUDIO_TRANSITION_SUSPEND :
                                           QM_AUDIO_TRANSITION_PAUSE;
    operation->transition_service_started = QM_FALSE;
    operation->transition_output_closed = QM_FALSE;
    (void)qm_mutex_unlock(&control->lock);
    release_operation_ref(operation);
    return QM_EOK;
}

int qm_audio_control_pause_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id)
{
    return pause_operation(control, operation_id, QM_FALSE);
}

int qm_audio_control_suspend_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id)
{
    return pause_operation(control, operation_id, QM_TRUE);
}

/**
 * @brief Replace a processing run while retaining its business operation. / 替换处理运行，但保留其业务操作。
 *
 * A seek replaces a processing run, never the business operation. Invalidate
 * its generation before cancellation, then acknowledge exit before resetting any cursor.
 * Scanning compressed preroll happens on the source worker after this command.
 */
static int reposition(qm_audio_control_operation_t *operation, uint32_t position_ms, qm_bool paused)
{
    qm_audio_control_t *control = operation->control;
    qm_audio_pipeline_t *pipeline = operation->pipeline;

    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (operation->transition != QM_AUDIO_TRANSITION_NONE &&
        operation->transition != QM_AUDIO_TRANSITION_SEEK) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EBUSY;
    }
    operation->seeking = QM_TRUE;
    operation->rewind_on_resume = QM_FALSE;
    operation->position_ms = position_ms;
    operation->transition_position_ms = position_ms;
    operation->transition_paused = paused;
    if (operation->transition != QM_AUDIO_TRANSITION_SEEK) {
        if (++operation->pipeline_generation == 0U) operation->pipeline_generation++;
        operation->transition_output_closed = QM_FALSE;
        operation->transition_service_started = QM_FALSE;
        operation->transition = QM_AUDIO_TRANSITION_SEEK;
    }
    (void)qm_mutex_unlock(&control->lock);
    (void)qm_audio_pipeline_cancel(pipeline);
    return QM_EOK;
}

static int finish_seek(qm_audio_control_operation_t *operation)
{
    qm_audio_control_t *control = operation->control;
    qm_audio_pipeline_t *pipeline = operation->pipeline;
    qm_audio_session_t *session = operation->session;
    uint32_t position_ms = operation->transition_position_ms;
    qm_bool paused = operation->transition_paused;
    int status;
    int ret;

    ret = qm_audio_pipeline_rewind_request(control->audio, pipeline, position_ms,
                                           operation->pipeline_generation);
    if (ret != QM_EOK) return ret;
    status = qm_audio_pipeline_rewind_result(pipeline, &ret);
    if (status != QM_EOK) return status;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    (void)qm_mutex_lock(&session->service->lock, QM_WAIT_FOREVER);
    operation->generation = session->generation;
    operation->session_state = session->state;
    (void)qm_mutex_unlock(&session->service->lock);
    if (ret == QM_EOK) {
        operation->position_ms = position_ms;
        operation->pipeline_finished = QM_FALSE;
        operation->pipeline_state = paused ? QM_AUDIO_PIPELINE_PAUSED : QM_AUDIO_PIPELINE_RUNNING;
        operation->pipeline_start_requested = QM_TRUE;
        operation->restart_pending = QM_TRUE;
    } else {
        operation->control_failed = QM_TRUE;
        operation->seeking = QM_FALSE;
        operation->result = ret;
    }
    (void)qm_mutex_unlock(&control->lock);
    if (ret == QM_EOK && !paused) {
        ret = qm_audio_service_session_request(session, QM_AUDIO_SERVICE_ACTION_RESUME, 0U);
        if (ret == QM_EOK) {
            (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
            operation->transition = QM_AUDIO_TRANSITION_RESUME;
            operation->transition_service_started = QM_TRUE;
            operation->transition_output_closed = QM_FALSE;
            (void)qm_mutex_unlock(&control->lock);
        }
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation->resume_in_progress = ret == QM_EOK && !paused;
    (void)qm_mutex_unlock(&control->lock);
    return ret;
}

static int finish_resume(qm_audio_control_operation_t *operation)
{
    qm_audio_control_t *control = operation->control;
    int ret = bind_output(operation);
    if (ret == QM_EOK && operation->restart_pending) {
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        operation->restart_pending = QM_FALSE;
        operation->pipeline_state = QM_AUDIO_PIPELINE_RUNNING;
        (void)qm_mutex_unlock(&control->lock);
        ret = qm_audio_pipeline_prepare(operation->pipeline);
        if (ret == QM_EOK) ret = source_submit(control, operation->pipeline);
    } else if (ret == QM_EOK) {
        ret = qm_audio_pipeline_resume(operation->pipeline);
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation->resume_in_progress = QM_FALSE;
    (void)qm_mutex_unlock(&control->lock);
    return ret;
}

/** Replaying discarded output re-obtains the whole stream from a URI source.
 * A URI rewind therefore costs a full remote re-download while recovering at
 * most the output ring depth of already decoded PCM. Resume in place instead
 * and let the media clock advance past those samples, exactly as a live source
 * does. Local sources reopen for free, so they keep replaying the tail.
 * URI 源重放已丢弃的输出等于把整段流重新下载一遍, 而能补回的只是输出环里那
 * 一层"已解码未播出"的 PCM; 改为就地在 `continuation` 处续播, 让媒体时钟跨过
 * 这些采样, 与直播源一致。本地源重开无代价, 仍保留回退以补回尾巴。 */
static qm_bool replay_requires_refetch(const qm_audio_pipeline_t *pipeline)
{
    return pipeline->config.source.type == QM_AUDIO_SOURCE_URI;
}

static int finish_pause(qm_audio_control_operation_t *operation, qm_bool yield_output)
{
    qm_audio_control_t *control = operation->control;
    qm_audio_session_t *session = operation->session;
    int ret = QM_EOK;
    if (ret == QM_EOK) {
        uint32_t position, duration, flags;
        qm_audio_pipeline_get_progress(operation->pipeline, &duration, &flags);
        if (qm_audio_service_session_get_position(session, &position) == QM_EOK) {
            (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
            if (!operation->seeking) operation->position_ms = position;
            (void)qm_mutex_lock(&session->service->lock, QM_WAIT_FOREVER);
            operation->rewind_on_resume = yield_output && !operation->seeking &&
                (flags & QM_AUDIO_PROGRESS_SEEKABLE) != 0U &&
                session->continuation_position_ms > position &&
                !replay_requires_refetch(operation->pipeline);
            (void)qm_mutex_unlock(&session->service->lock);
            (void)qm_mutex_unlock(&control->lock);
        }
    }
    return ret;
}

/** Source completion is durable; checking it never waits on audio_event. / Source 完成状态持久保存，事件任务仅检查而不等待。 */
static uint32_t advance_operation(qm_audio_control_operation_t *operation)
{
    qm_audio_control_t *control = operation->control;
    qm_audio_pipeline_t *pipeline = operation->pipeline;
    qm_audio_transition_t transition = operation->transition;
    qm_audio_service_action_t action = transition == QM_AUDIO_TRANSITION_SEEK ?
        QM_AUDIO_SERVICE_ACTION_REPOSITION : transition == QM_AUDIO_TRANSITION_STOP ?
        QM_AUDIO_SERVICE_ACTION_STOP : transition == QM_AUDIO_TRANSITION_RESUME ?
        QM_AUDIO_SERVICE_ACTION_RESUME : transition == QM_AUDIO_TRANSITION_SUSPEND ?
        QM_AUDIO_SERVICE_ACTION_SUSPEND : QM_AUDIO_SERVICE_ACTION_PAUSE;
    qm_bool ready;
    int ret = QM_EOK;

    /* 操作一旦进入终态(完成/取消/被抢占/错误)，其上的迁移都不可能再推进：
     * 已没有需要停止或定位的活动输出，advance 的 ready 条件与 Service 应答也可能
     * 永远不满足。若不清除迁移标记，advance 会永久返回 QM_WAIT_FOREVER，
     * qm_audio_control_release 也会永久因 transition != NONE 返回 -QM_EBUSY，
     * 最终泄漏该操作及其 Service 会话，使后续播放因会话表满被直接拒绝。
     * project_and_notify 会在终态下清除迁移标记并唤醒等待者；真正的拆除由 release
     * 自身的有界 cancel/wait/destroy/session_release 完成。 */
    if (qm_audio_control_terminal(operation->state)) {
        project_and_notify(operation);
        return QM_WAIT_FOREVER;
    }
    (void)qm_mutex_lock(&pipeline->lock, QM_WAIT_FOREVER);
    ready = transition == QM_AUDIO_TRANSITION_RESUME ? QM_TRUE :
        transition == QM_AUDIO_TRANSITION_SEEK || transition == QM_AUDIO_TRANSITION_STOP ?
        !pipeline->task_started && !pipeline->execution_active :
        (operation->restart_pending || (pipeline->state == QM_AUDIO_PIPELINE_PAUSED && pipeline->pause_notified)) &&
        pipeline->emit_inflight == 0U && !pipeline->execution_active;
    if (!ready && transition != QM_AUDIO_TRANSITION_SEEK && transition != QM_AUDIO_TRANSITION_STOP &&
        (pipeline->state == QM_AUDIO_PIPELINE_ERROR ||
         pipeline->state == QM_AUDIO_PIPELINE_CANCELLED))
        ret = pipeline->result != QM_EOK ? pipeline->result : -QM_EINTR;
    (void)qm_mutex_unlock(&pipeline->lock);
    /* Stop/reposition revoke output immediately so a Source drain can exit.
     * Pause preserves output until the Source checkpoint. / 停止和定位立即撤销
     * 输出以便 Source drain 退出；暂停先等待 Source 到达检查点。 */
    if (ret == QM_EOK && !operation->transition_service_started &&
        (ready || transition == QM_AUDIO_TRANSITION_SEEK || transition == QM_AUDIO_TRANSITION_STOP)) {
        ret = qm_audio_service_session_request(operation->session, action,
            transition == QM_AUDIO_TRANSITION_STOP ? operation->reason : operation->transition_position_ms);
        if (ret == QM_EOK) operation->transition_service_started = QM_TRUE;
    }
    if (ret == QM_EOK && operation->transition_service_started && !operation->transition_output_closed) {
        int status = qm_audio_service_session_result(operation->session, action, &ret);
        if (status == -QM_EBUSY) return QM_WAIT_FOREVER;
        if (status != QM_EOK) ret = status;
        if (ret == QM_EOK) operation->transition_output_closed = QM_TRUE;
    }
    if (ret == QM_EOK && !ready) return QM_WAIT_FOREVER;
    if (ret == QM_EOK && transition != QM_AUDIO_TRANSITION_STOP)
        ret = transition == QM_AUDIO_TRANSITION_RESUME ? finish_resume(operation) :
            transition == QM_AUDIO_TRANSITION_SEEK ? finish_seek(operation) :
            finish_pause(operation, transition == QM_AUDIO_TRANSITION_SUSPEND);
    if (ret == -QM_EBUSY && transition == QM_AUDIO_TRANSITION_SEEK) {
        return QM_WAIT_FOREVER;
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (operation->transition == transition) operation->transition = QM_AUDIO_TRANSITION_NONE;
    if (ret != QM_EOK) {
        operation->control_failed = QM_TRUE;
        operation->seeking = operation->resume_in_progress = QM_FALSE;
        operation->result = ret;
    }
    (void)qm_mutex_unlock(&control->lock);
    if (ret != QM_EOK) {
        (void)qm_audio_pipeline_cancel_for_stop(pipeline);
        if (transition != QM_AUDIO_TRANSITION_STOP) {
            (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
            operation->transition = QM_AUDIO_TRANSITION_STOP;
            operation->transition_service_started = QM_FALSE;
            operation->transition_output_closed = QM_FALSE;
            (void)qm_mutex_unlock(&control->lock);
            qm_audio_event_wake(control->audio);
        }
    }
    project_and_notify(operation);
    return QM_WAIT_FOREVER;
}

uint32_t qm_audio_control_advance(qm_audio_control_t *control)
{
    uint32_t next = QM_WAIT_FOREVER;
    for (uint32_t i = 0U; ; ++i) {
        qm_audio_control_operation_t *operation;
        uint32_t left;
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        if (i >= control->operation_count) {
            (void)qm_mutex_unlock(&control->lock);
            break;
        }
        operation = control->operations[i];
        if (operation->transition == QM_AUDIO_TRANSITION_NONE || operation->releasing) {
            (void)qm_mutex_unlock(&control->lock);
            continue;
        }
        operation->api_users++;
        (void)qm_mutex_unlock(&control->lock);
        left = advance_operation(operation);
        if (left < next) next = left;
        release_operation_ref(operation);
    }
    return next;
}

int qm_audio_control_seek_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t position_ms)
{
    qm_audio_control_operation_t *operation;
    uint32_t duration, flags;
    qm_audio_control_state_t state;
    int ret = acquire_operation(control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    qm_audio_pipeline_get_progress(operation->pipeline, &duration, &flags);
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    state = operation->state;
    (void)qm_mutex_unlock(&control->lock);
    if ((flags & QM_AUDIO_PROGRESS_SEEKABLE) == 0U) ret = -QM_ENOSYS;
    else if (state != QM_AUDIO_CONTROL_PLAYING && state != QM_AUDIO_CONTROL_PAUSED) ret = -QM_EBUSY;
    else if ((flags & QM_AUDIO_PROGRESS_DURATION_VALID) != 0U &&
             (flags & QM_AUDIO_PROGRESS_DURATION_ESTIMATED) == 0U && position_ms > duration) ret = -QM_EINVAL;
    else ret = reposition(operation, position_ms, state == QM_AUDIO_CONTROL_PAUSED);
    release_operation_ref(operation);
    return ret;
}

int qm_audio_control_resume_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id)
{
    qm_audio_control_operation_t *operation;
    qm_audio_pipeline_t *pipeline;
    qm_audio_session_t *session;
    int ret;

    if (control == NULL || !qm_mutex_is_valid(&control->lock)) return -QM_EINVAL;
    ret = acquire_operation(control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (operation->transition == QM_AUDIO_TRANSITION_SEEK && operation->transition_paused) {
        operation->transition_paused = QM_FALSE;
        operation->resume_in_progress = QM_TRUE;
        (void)qm_mutex_unlock(&control->lock);
        release_operation_ref(operation);
        return QM_EOK;
    }
    if (operation->pipeline != NULL && operation->session != NULL &&
        operation->transition == QM_AUDIO_TRANSITION_NONE &&
        operation->state == QM_AUDIO_CONTROL_PAUSED) {
        operation->resume_in_progress = QM_TRUE;
        pipeline = operation->pipeline;
        session = operation->session;
    } else {
        pipeline = NULL;
        session = NULL;
    }
    (void)qm_mutex_unlock(&control->lock);
    if (pipeline == NULL || session == NULL) {
        release_operation_ref(operation);
        return -QM_EBUSY;
    }
    ret = QM_EOK;
    if (operation->rewind_on_resume) {
        ret = reposition(operation, operation->position_ms, QM_FALSE);
        release_operation_ref(operation);
        return ret;
    }
    if (ret == QM_EOK && !operation->restart_pending && !operation->seeking) {
        uint32_t continuation = 0U;
        qm_bool yielded;
        (void)qm_mutex_lock(&session->service->lock, QM_WAIT_FOREVER);
        yielded = session->stream == NULL;
        continuation = session->continuation_position_ms;
        (void)qm_mutex_unlock(&session->service->lock);
        /* A live source cannot rewind discarded output. Its next PCM advances
         * the media clock past those samples instead of silently losing time. */
        if (yielded) ret = qm_audio_service_session_set_origin(session, continuation);
    }
    if (ret == QM_EOK) ret = qm_audio_service_session_request(session, QM_AUDIO_SERVICE_ACTION_RESUME, 0U);
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (ret == QM_EOK) {
        operation->transition = QM_AUDIO_TRANSITION_RESUME;
        operation->transition_service_started = QM_TRUE;
        operation->transition_output_closed = QM_FALSE;
    } else operation->resume_in_progress = QM_FALSE;
    (void)qm_mutex_unlock(&control->lock);
    project_and_notify(operation);
    release_operation_ref(operation);
    return ret;
}

int qm_audio_control_stop_impl(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t reason)
{
    qm_audio_control_operation_t *operation;
    qm_audio_pipeline_t *pipeline;
    qm_audio_session_t *session;
    int ret = QM_EOK;

    if (control == NULL || !qm_mutex_is_valid(&control->lock)) return -QM_EINVAL;
    ret = acquire_operation(control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    pipeline = operation->pipeline;
    session = operation->session;
    /* Only arm the STOP transition while a Pipeline still exists to acknowledge
     * it, and while the operation has not reached a terminal state yet. A
     * half-released operation (Pipeline torn down, Service still draining) must
     * stay quiescent, and a terminal operation has no output left to stop:
     * arming STOP in either case would leave audio_event with a transition it
     * can never advance (see advance_operation), which then rejects the
     * following release with -QM_EBUSY. / 只有当 Pipeline 仍然存在、且操作尚未
     * 进入终态时才挂起 STOP 迁移；已拆除 Pipeline 仅剩 Service 排空的操作必须保持
     * 静止，终态操作也没有可停止的输出，这两种情况挂迁移都会留下无法推进的迁移
     * (见 advance_operation)，并使随后的 release 返回 -QM_EBUSY。 */
    if (pipeline != NULL && session != NULL &&
        !qm_audio_control_terminal(operation->state)) {
        operation->reason = reason;
        operation->transition = QM_AUDIO_TRANSITION_STOP;
        operation->transition_service_started = QM_FALSE;
        operation->transition_output_closed = QM_FALSE;
        operation->seeking = operation->resume_in_progress = QM_FALSE;
    }
    (void)qm_mutex_unlock(&control->lock);
    if (pipeline != NULL) {
        (void)qm_audio_pipeline_cancel_for_stop(pipeline);
    }
    if (session == NULL || pipeline == NULL) ret = -QM_EINVAL;
    else qm_audio_event_wake(control->audio);
    release_operation_ref(operation);
    return ret;
}

int qm_audio_control_wait(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t timeout_ms)
{
    qm_audio_control_operation_t *operation;
    qm_utils_time_t deadline;
    int ret = QM_EOK;

    if (control == NULL || !qm_mutex_is_valid(&control->lock)) return -QM_EINVAL;
    /* Waiting observes only Control state, including an accepted seek whose
     * source is being rebuilt on audio_event. / 等待只观察 Control 状态，允许
     * 已接纳 seek 正在事件任务重建 Source 时开始等待。 */
    ret = acquire_operation_for_event(control, operation_id, QM_TRUE, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (operation->wait_active) {
        (void)qm_mutex_unlock(&control->lock);
        release_operation_ref(operation);
        return -QM_EBUSY;
    }
    operation->wait_active = QM_TRUE;
    (void)qm_mutex_unlock(&control->lock);
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        qm_audio_control_state_t state;
        uint32_t left;
        int result;

        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        state = operation->state;
        result = operation->result;
        (void)qm_mutex_unlock(&control->lock);
        if (qm_audio_control_terminal(state)) {
            if (state == QM_AUDIO_CONTROL_FINISHED) {
                ret = QM_EOK;
                break;
            }
            if (state == QM_AUDIO_CONTROL_CANCELLED || state == QM_AUDIO_CONTROL_PREEMPTED) {
                ret = result != QM_EOK ? result : -QM_EINTR;
                break;
            }
            ret = result != QM_EOK ? result : -QM_EIO;
            break;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (left == 0U) {
            ret = -QM_ETIMEOUT;
            break;
        }
        /* Admission deadlines are driven by audio_event. A caller wait never
         * changes scheduling or cancels the operation on its own timeout. */
        if (!qm_sem_is_valid(&operation->state_sem) ||
            qm_sem_wait(&operation->state_sem, left) != QM_EOK) {
            ret = -QM_ETIMEOUT;
            break;
        }
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation->wait_active = QM_FALSE;
    (void)qm_mutex_unlock(&control->lock);
    release_operation_ref(operation);
    return ret;
}

int qm_audio_control_get_snapshot(const qm_audio_control_t *control, qm_audio_operation_id_t operation_id, qm_audio_control_snapshot_t *snapshot)
{
    qm_audio_control_operation_t *operation = NULL;
    qm_audio_control_t *mutable_control = (qm_audio_control_t *)control;
    uint32_t position = 0U;
    uint32_t generation = 0U;
    int observed = QM_EOK;
    int ret = QM_EOK;
    if (control == NULL || snapshot == NULL ||
        !qm_mutex_is_valid(&mutable_control->lock)) {
        return -QM_EINVAL;
    }
    ret = acquire_operation(mutable_control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&mutable_control->lock, QM_WAIT_FOREVER);
    generation = operation->pipeline_generation;
    (void)qm_mutex_unlock(&mutable_control->lock);
    qm_audio_pipeline_get_progress(operation->pipeline, &snapshot->duration_ms, &snapshot->flags);
    observed = qm_audio_service_session_get_position(operation->session, &position);
    (void)qm_mutex_lock(&mutable_control->lock, QM_WAIT_FOREVER);
    snapshot->operation_id = operation->id;
    snapshot->state = operation->state;
    snapshot->result = operation->result;
    if (operation->repositioning || generation != operation->pipeline_generation) {
        (void)qm_mutex_unlock(&mutable_control->lock);
        release_operation_ref(operation);
        return -QM_EBUSY;
    }
    if (operation->seeking) {
        snapshot->flags |= QM_AUDIO_PROGRESS_SEEKING;
        snapshot->flags &= ~QM_AUDIO_PROGRESS_POSITION_VALID;
    } else {
        snapshot->flags &= ~QM_AUDIO_PROGRESS_SEEKING;
        if (observed == QM_EOK) {
            operation->position_ms = position;
            snapshot->flags |= QM_AUDIO_PROGRESS_POSITION_VALID;
            if (snapshot->state != QM_AUDIO_CONTROL_FINISHED)
                snapshot->flags |= QM_AUDIO_PROGRESS_POSITION_ESTIMATED;
        }
    }
    if (snapshot->state == QM_AUDIO_CONTROL_FINISHED &&
        (snapshot->flags & QM_AUDIO_PROGRESS_DURATION_VALID) != 0U &&
        (snapshot->flags & QM_AUDIO_PROGRESS_DURATION_ESTIMATED) == 0U) {
        operation->position_ms = snapshot->duration_ms;
        snapshot->flags |= QM_AUDIO_PROGRESS_POSITION_VALID;
    }
    snapshot->position_ms = operation->position_ms;
    (void)qm_mutex_unlock(&mutable_control->lock);
    release_operation_ref(operation);
    return ret;
}

int qm_audio_control_capture_resume_point(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, qm_audio_resume_point_t *resume, uint32_t timeout_ms)
{
    qm_audio_control_operation_t *operation = NULL;
    qm_audio_pipeline_t *pipeline;
    int ret;

    if (control == NULL || resume == NULL || !qm_mutex_is_valid(&control->lock)) {
        return -QM_EINVAL;
    }
    ret = acquire_operation(control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    /* A terminal operation no longer owns a live decoder, so it cannot produce
     * a record; report it as unsupported rather than as an empty success.
     * / 终态操作不再持有活动解码器，无法产生记录，按不支持上报而非空成功。 */
    if (operation->pipeline == NULL ||
        qm_audio_control_terminal(operation->state)) {
        (void)qm_mutex_unlock(&control->lock);
        release_operation_ref(operation);
        return -QM_ENOSYS;
    }
    pipeline = operation->pipeline;
    (void)qm_mutex_unlock(&control->lock);
    /* The operation reference keeps the Pipeline alive for this capture. */
    ret = qm_audio_normalize_result(
        qm_audio_pipeline_capture_resume(pipeline, resume, timeout_ms));
    if (ret == QM_EOK && resume->state_size == 0U) {
        /* 裸 PCM 的断点即字节偏移: 用渲染层(DAC 已消费)位置对齐, 消除输出 buffer 造成
         * 的"已 emit 领先已播"约 1 秒漂移, 使恢复时从"听到的位置"继续而非跳过 buffer
         * 里未播完的内容。bytes_per_second 从记录反推, 为 frame_bytes*rate 的稳定值。 */
        uint32_t played_ms = 0U;
        if (qm_audio_service_session_get_position(operation->session, &played_ms) == QM_EOK &&
            resume->position_ms != 0U) {
            uint64_t bytes_per_second = (uint64_t)resume->offset * 1000U / resume->position_ms;
            uint64_t played_offset = (uint64_t)played_ms * bytes_per_second / 1000U;
            resume->offset = played_offset > UINT32_MAX ? UINT32_MAX : (uint32_t)played_offset;
            resume->position_ms = played_ms;
        }
    }
    release_operation_ref(operation);
    return ret;
}

int qm_audio_control_release(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t timeout_ms)
{
    qm_audio_control_operation_t *operation;
    int ret;
    uint32_t i;

    if (control == NULL || !qm_mutex_is_valid(&control->lock)) return -QM_EINVAL;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation = find_operation(control, operation_id);
    if (control->shutdown_active) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EBUSY;
    }
    if (operation == NULL) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EINVAL;
    }
    if (!qm_audio_control_terminal(operation->state) || operation->releasing ||
        operation->initializing || operation->api_users != 0U ||
        operation->transition != QM_AUDIO_TRANSITION_NONE) {
        qm_audio_control_state_t state = operation->state;
        qm_bool releasing = operation->releasing;
        qm_bool initializing = operation->initializing;
        uint32_t users = operation->api_users;
        qm_audio_transition_t transition = operation->transition;
        (void)qm_mutex_unlock(&control->lock);
        /* 日志宏在关闭日志的构建(CONFIG_QM_LOG_SUPPORT=0)里展开为空, 显式引用这些
         * 仅用于日志的值, 否则会触发 -Werror,-Wunused-variable。 */
        (void)state;
        (void)releasing;
        (void)initializing;
        (void)users;
        (void)transition;
        /* 释放被拒的具体原因：终态/释放中/构建中/引用未归零/迁移未完成。 */
        QM_LOGW(LOG_TAG, "release busy op=%u state=%d releasing=%u init=%u users=%u transition=%d",
                (unsigned)operation_id, (int)state, (unsigned)releasing,
                (unsigned)initializing, (unsigned)users, (int)transition);
        return -QM_EBUSY;
    }
    operation->releasing = QM_TRUE;
    (void)qm_mutex_unlock(&control->lock);

    if (operation->pipeline != NULL) {
        (void)qm_audio_pipeline_cancel_for_stop(operation->pipeline);
        ret = qm_audio_pipeline_wait(operation->pipeline, timeout_ms);
        if (ret == -QM_ETIMEOUT) {
            goto retry;
        }
        ret = qm_audio_pipeline_destroy(operation->pipeline);
        if (ret != QM_EOK) {
            /* Pipeline 拆除失败：本层保留句柄重试，操作仍在表中。 */
            QM_LOGW(LOG_TAG, "release retained op=%u pipeline destroy ret=%d",
                    (unsigned)operation->id, ret);
            goto retry;
        }
        operation->pipeline = NULL;
    }
    qm_audio_engine_render_release(operation->output);
    operation->output = NULL;
    destroy_push_source(operation);
    if (operation->session != NULL) {
        ret = qm_audio_service_session_release(operation->session);
        if (ret != QM_EOK) {
            /* Pipeline 已经拆除并置空，Service 尚未终态：操作以半释放状态留在表中。 */
            QM_LOGW(LOG_TAG, "release retained op=%u session alive ret=%d",
                    (unsigned)operation->id, ret);
            goto retry;
        }
        operation->session = NULL;
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    for (i = 0U; i < control->operation_count; ++i) {
        if (control->operations[i] == operation) {
            control->operations[i] = control->operations[control->operation_count - 1U];
            control->operations[--control->operation_count] = NULL;
            break;
        }
    }
    (void)qm_mutex_unlock(&control->lock);
    if (qm_sem_is_valid(&operation->state_sem)) {
        (void)qm_sem_free(&operation->state_sem);
    }
    qm_free(operation);
    /* A concurrent release can move a pending operation behind the event
     * task's scan cursor. Ensure it rescans after compacting the table. */
    qm_audio_event_wake(control->audio);
    return QM_EOK;

retry:
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    operation->releasing = QM_FALSE;
    (void)qm_mutex_unlock(&control->lock);
    return ret;
}

int qm_audio_control_set_volume_impl(qm_audio_control_t *control, uint8_t volume)
{
    if (control == NULL || control->engine == NULL) {
        return -QM_EINVAL;
    }
    return qm_audio_engine_render_setting_request(control->engine,
        QM_AUDIO_RENDER_SETTING_VOLUME, volume);
}

int qm_audio_control_set_mute_impl(qm_audio_control_t *control, qm_bool muted)
{
    if (control == NULL || control->engine == NULL) {
        return -QM_EINVAL;
    }
    return qm_audio_engine_render_setting_request(control->engine,
        QM_AUDIO_RENDER_SETTING_MUTE, (uint8_t)muted);
}

int qm_audio_control_stream_write(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, const void *data, uint32_t size, uint32_t timeout_ms)
{
    qm_audio_control_operation_t *operation;
    qm_audio_push_source_t *source;
    const qm_audio_codec_config_t *input_format;
    qm_audio_control_state_t state;
    int ret;

    if (control == NULL || data == NULL || size == 0U) return -QM_EINVAL;
    ret = acquire_operation(control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    source = operation->push_source;
    input_format = operation->pipeline != NULL ?
        &operation->pipeline->config.input_format : NULL;
    state = operation->state;
    (void)qm_mutex_unlock(&control->lock);
    if (source == NULL || input_format == NULL || qm_audio_control_terminal(state)) {
        release_operation_ref(operation);
        return -QM_EBUSY;
    }
    if (input_format->framing == QM_AUDIO_CODEC_FRAMING_PACKET) {
        release_operation_ref(operation);
        return -QM_EINVAL;
    }
    ret = qm_audio_push_source_write(source, data, size, timeout_ms);
    release_operation_ref(operation);
    return ret;
}

int qm_audio_control_stream_write_packet(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, const qm_audio_data_t *packet, uint32_t timeout_ms)
{
    qm_audio_control_operation_t *operation;
    qm_audio_push_source_t *source;
    const qm_audio_codec_config_t *input_format;
    qm_audio_control_state_t state;
    int ret;
    if (control == NULL || packet == NULL) return -QM_EINVAL;
    ret = acquire_operation(control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    source = operation->push_source;
    input_format = operation->pipeline != NULL ?
        &operation->pipeline->config.input_format : NULL;
    state = operation->state;
    (void)qm_mutex_unlock(&control->lock);
    if (source == NULL || operation->pipeline == NULL || qm_audio_control_terminal(state)) {
        /* 区分入队被拒的原因：源已释放 / Pipeline 已拆除 / 操作已终态。 */
        QM_LOGW(LOG_TAG, "write packet busy op=%u source=%d pipeline=%d state=%d",
                (unsigned)operation_id, source != NULL, operation->pipeline != NULL, (int)state);
        ret = -QM_EBUSY;
    } else if (input_format->framing != QM_AUDIO_CODEC_FRAMING_PACKET ||
               !qm_audio_packet_valid(&control->output_format, packet) ||
               (input_format->packet_bytes != 0U &&
                packet->size != input_format->packet_bytes) ||
               (input_format->packet_duration_ms != 0U &&
                (packet->flags & QM_AUDIO_DATA_DURATION_VALID) != 0U &&
                packet->duration_ms != input_format->packet_duration_ms)) {
        ret = -QM_EINVAL;
    } else {
        ret = qm_audio_push_source_write_packet(source, packet, timeout_ms);
    }
    release_operation_ref(operation);
    return ret;
}

int qm_audio_control_stream_finish(qm_audio_control_t *control, qm_audio_operation_id_t operation_id)
{
    qm_audio_control_operation_t *operation;
    qm_audio_push_source_t *source;
    int ret;

    if (control == NULL) return -QM_EINVAL;
    ret = acquire_operation(control, operation_id, &operation);
    if (ret != QM_EOK) return ret;
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    source = operation->push_source;
    (void)qm_mutex_unlock(&control->lock);
    if (source == NULL) {
        release_operation_ref(operation);
        return -QM_EINVAL;
    }
    ret = qm_audio_push_source_finish(source);
    release_operation_ref(operation);
    return ret;
}

static int submit_command(qm_audio_control_t *control, qm_audio_command_t *command)
{
    int ret;

    if (control == NULL || command == NULL || !control->event_dispatch_active) {
        return -QM_EBUSY;
    }
    ret = qm_sem_new(&command->done, 0);
    if (ret != QM_EOK) {
        return -QM_EIO;
    }
    ret = qm_audio_command_post(control, command);
    if (qm_sem_is_valid(&command->done)) {
        (void)qm_sem_free(&command->done);
    }
    return ret;
}

int qm_audio_control_play(qm_audio_control_t *control, const qm_audio_control_play_request_t *request, qm_audio_operation_id_t *operation_id)
{
    qm_audio_command_t command = {0};

    if (request == NULL || operation_id == NULL) {
        return -QM_EINVAL;
    }
    command.type = QM_AUDIO_COMMAND_PLAY;
    command.play_request = *request;
    command.operation_id = operation_id;
    return submit_command(control, &command);
}

int qm_audio_control_pause(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, qm_audio_pause_mode_t mode)
{
    qm_audio_command_t command = {0};

    if (mode != QM_AUDIO_PAUSE_HOLD_OUTPUT && mode != QM_AUDIO_PAUSE_YIELD_OUTPUT) {
        return -QM_EINVAL;
    }
    command.type = mode == QM_AUDIO_PAUSE_HOLD_OUTPUT ? QM_AUDIO_COMMAND_PAUSE :
                                                       QM_AUDIO_COMMAND_SUSPEND;
    command.operation = operation_id;
    return submit_command(control, &command);
}

int qm_audio_control_resume(qm_audio_control_t *control, qm_audio_operation_id_t operation_id)
{
    qm_audio_command_t command = {0};

    command.type = QM_AUDIO_COMMAND_RESUME;
    command.operation = operation_id;
    return submit_command(control, &command);
}

int qm_audio_control_seek(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t position_ms)
{
    qm_audio_command_t command = {0};
    command.type = QM_AUDIO_COMMAND_SEEK;
    command.operation = operation_id;
    command.position_ms = position_ms;
    return submit_command(control, &command);
}

int qm_audio_control_stop(qm_audio_control_t *control, qm_audio_operation_id_t operation_id, uint32_t reason)
{
    qm_audio_command_t command = {0};

    command.type = QM_AUDIO_COMMAND_STOP;
    command.operation = operation_id;
    command.reason = reason;
    return submit_command(control, &command);
}

int qm_audio_control_subscribe(qm_audio_control_t *control, qm_audio_event_cb_t callback, void *user_ctx, qm_audio_subscription_t *out_subscription)
{
    qm_audio_subscription_t subscription;
    int ret;

    if (control == NULL || callback == NULL || out_subscription == NULL ||
        !qm_mutex_is_valid(&control->lock)) {
        return -QM_EINVAL;
    }
    *out_subscription = NULL;
    subscription = (qm_audio_subscription_t)qm_calloc(1U,
                                                       sizeof(*subscription));
    if (subscription == NULL) return -QM_ENOMEM;
    ret = qm_sem_new(&subscription->idle_sem, 0U);
    if (ret != QM_EOK) {
        qm_free(subscription);
        return ret;
    }
    subscription->control = control;
    subscription->callback = callback;
    subscription->context = user_ctx;
    subscription->active = QM_TRUE;

    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (control->shutting_down || !control->event_dispatch_active ||
        control->subscription_count >= CONFIG_QM_AUDIO_MAX_SUBSCRIBERS) {
        (void)qm_mutex_unlock(&control->lock);
        (void)qm_sem_free(&subscription->idle_sem);
        qm_free(subscription);
        return control->shutting_down ? -QM_EBUSY : -QM_EFULL;
    }
    control->subscriptions[control->subscription_count++] = subscription;
    (void)qm_mutex_unlock(&control->lock);
    *out_subscription = subscription;
    return QM_EOK;
}

int qm_audio_control_unsubscribe(qm_audio_control_t *control, qm_audio_subscription_t subscription)
{
    uint32_t i;
    qm_bool found = QM_FALSE;

    if (control == NULL || subscription == NULL ||
        !qm_mutex_is_valid(&control->lock)) {
        return -QM_EINVAL;
    }
    if (qm_audio_event_is_current(control)) {
        return -QM_EBUSY;
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    for (i = 0U; i < control->subscription_count; ++i) {
        if (control->subscriptions[i] == subscription) {
            found = QM_TRUE;
            break;
        }
    }
    if (!found || subscription->control != control || !subscription->active) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EINVAL;
    }
    subscription->active = QM_FALSE;
    for (; i + 1U < control->subscription_count; ++i) {
        control->subscriptions[i] = control->subscriptions[i + 1U];
    }
    control->subscriptions[--control->subscription_count] = NULL;
    (void)qm_mutex_unlock(&control->lock);

    for (;;) {
        uint32_t inflight;

        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        inflight = subscription->callback_inflight;
        (void)qm_mutex_unlock(&control->lock);
        if (inflight == 0U) {
            break;
        }
        if (qm_sem_is_valid(&subscription->idle_sem)) {
            while (qm_sem_wait(&subscription->idle_sem, QM_WAIT_FOREVER) != QM_EOK) {}
        }
    }
    if (qm_sem_is_valid(&subscription->idle_sem)) {
        (void)qm_sem_free(&subscription->idle_sem);
    }
    qm_free(subscription);
    return QM_EOK;
}
