/**
 * @file qm_audio_event.c
 * @brief Serialized Control command and event dispatch. / 串行化的 Control 命令与事件分发。
 *
 * One serialized command/event task per audio instance. Control shutdown drains
 * its admitted producers before posting a targeted EXIT barrier. The shared task
 * remains available to other Controls until the instance exits.
 */

#include "qm_audio_priv.h"

static int event_queue_send(qm_audio_t *audio, const qm_audio_event_msg_t *msg)
{
    if (audio == NULL || msg == NULL ||
        !qm_queue_is_valid(&audio->event_queue)) {
        return -QM_EINVAL;
    }
    return qm_queue_send(&audio->event_queue, (void *)msg, sizeof(*msg));
}

/**
 * @brief Reserve a producer's right to post an event. / 保留生产者投递事件的权限。
 *
 * A reservation covers only the interval in which a producer may still add
 * an event that borrows Control.  Shutdown closes admission first, so once
 * every earlier reservation has completed no new Control-referencing item
 * can be queued behind the EXIT marker. */
static int reserve_event_post(qm_audio_control_t *control)
{
    int ret = -QM_EBUSY;

    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (control->event_dispatch_active && !control->event_exit_requested &&
        control->event_post_inflight != UINT32_MAX) {
        control->event_post_inflight++;
        ret = QM_EOK;
    }
    (void)qm_mutex_unlock(&control->lock);
    return ret;
}

static void release_event_post(qm_audio_control_t *control)
{
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (control->event_post_inflight != 0U) {
        control->event_post_inflight--;
    }
    if (control->event_post_inflight == 0U && control->event_exit_requested &&
        qm_sem_is_valid(&control->event_post_idle_sem)) {
        (void)qm_sem_signal(&control->event_post_idle_sem);
    }
    (void)qm_mutex_unlock(&control->lock);
}

static uint32_t shutdown_time_left(qm_utils_time_t *deadline, uint32_t timeout_ms)
{
    return timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
           qm_utils_time_left(deadline);
}

static int wait_for_event_posts(qm_audio_control_t *control, qm_utils_time_t *deadline, uint32_t timeout_ms)
{
    int ret = QM_EOK;

    for (;;) {
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        if (control->event_post_inflight == 0U) {
            (void)qm_mutex_unlock(&control->lock);
            return QM_EOK;
        }
        if (!qm_sem_is_valid(&control->event_post_idle_sem)) {
            (void)qm_mutex_unlock(&control->lock);
            return -QM_EIO;
        }
        (void)qm_mutex_unlock(&control->lock);
        ret = qm_sem_wait(&control->event_post_idle_sem,
                          shutdown_time_left(deadline, timeout_ms));
        if (ret != QM_EOK) {
            return ret;
        }
    }
}

static void dispatch_event(qm_audio_control_t *control, const qm_audio_event_t *event)
{
    qm_audio_subscription_t subscribers[CONFIG_QM_AUDIO_MAX_SUBSCRIBERS] = {0};
    uint32_t count = 0U;
    uint32_t i = 0U;
    qm_audio_subscription_t subscription = NULL;

    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    for (i = 0U; i < control->subscription_count &&
                  count < CONFIG_QM_AUDIO_MAX_SUBSCRIBERS; ++i) {
        subscription = control->subscriptions[i];

        if (subscription != NULL && subscription->active &&
            subscription->callback != NULL) {
            subscription->callback_inflight++;
            subscribers[count++] = subscription;
        }
    }
    (void)qm_mutex_unlock(&control->lock);

    for (i = 0U; i < count; ++i) {
        subscribers[i]->callback(event, subscribers[i]->context);
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        if (subscribers[i]->callback_inflight != 0U) {
            subscribers[i]->callback_inflight--;
        }
        if (subscribers[i]->callback_inflight == 0U &&
            !subscribers[i]->active &&
            qm_sem_is_valid(&subscribers[i]->idle_sem)) {
            (void)qm_sem_signal(&subscribers[i]->idle_sem);
        }
        (void)qm_mutex_unlock(&control->lock);
    }
}

static void dispatch_wake_event(qm_audio_t *audio, const qm_audio_event_t *event)
{
    qm_audio_subscription_t subscribers[CONFIG_QM_AUDIO_MAX_SUBSCRIBERS] = {0};
    uint32_t count = 0U;
    uint32_t i = 0U;
    qm_audio_subscription_t subscription = NULL;

    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    for (i = 0U; i < audio->wake_subscription_count &&
                  count < CONFIG_QM_AUDIO_MAX_SUBSCRIBERS; ++i) {
        subscription = audio->wake_subscriptions[i];

        if (subscription != NULL && subscription->active &&
            subscription->callback != NULL) {
            subscription->callback_inflight++;
            subscribers[count++] = subscription;
        }
    }
    (void)qm_mutex_unlock(&audio->lock);

    for (i = 0U; i < count; ++i) {
        subscribers[i]->callback(event, subscribers[i]->context);
        (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
        if (subscribers[i]->callback_inflight != 0U) {
            subscribers[i]->callback_inflight--;
        }
        if (subscribers[i]->callback_inflight == 0U &&
            !subscribers[i]->active &&
            qm_sem_is_valid(&subscribers[i]->idle_sem)) {
            (void)qm_sem_signal(&subscribers[i]->idle_sem);
        }
        (void)qm_mutex_unlock(&audio->lock);
    }
}

/**
 * @brief Project a lower-layer event on the serialized task. / 在串行任务上投影底层事件。
 *
 * Called only by the event task, or while that same task synchronously
 * invokes a lower-layer callback during command execution. */
static void dispatch_lower_event(qm_audio_control_t *control, const qm_audio_lower_event_t *lower)
{
    if (lower->source == QM_AUDIO_LOWER_SERVICE) {
        qm_audio_control_service_event_impl(control, &lower->event);
    } else if (lower->source == QM_AUDIO_LOWER_PIPELINE) {
        qm_audio_control_pipeline_event_impl(control, &lower->event);
    } else if (lower->source == QM_AUDIO_LOWER_PUBLIC) {
        qm_audio_event_dispatch(control, &lower->event);
    }
}

static void dispatch_command(qm_audio_control_t *control, qm_audio_command_t *command)
{
    if (command == NULL) {
        return;
    }
    switch (command->type) {
        case QM_AUDIO_COMMAND_PLAY:
            command->result = qm_audio_control_play_impl(
                control, &command->play_request, command->operation_id);
            break;
        case QM_AUDIO_COMMAND_PAUSE:
            command->result = qm_audio_control_pause_impl(control,
                                                          command->operation);
            break;
        case QM_AUDIO_COMMAND_SUSPEND:
            command->result = qm_audio_control_suspend_impl(control,
                                                            command->operation);
            break;
        case QM_AUDIO_COMMAND_RESUME:
            command->result = qm_audio_control_resume_impl(control,
                                                           command->operation);
            break;
        case QM_AUDIO_COMMAND_SEEK:
            command->result = qm_audio_control_seek_impl(control,
                command->operation, command->position_ms);
            break;
        case QM_AUDIO_COMMAND_STOP:
            command->result = qm_audio_control_stop_impl(control,
                                                         command->operation,
                                                         command->reason);
            break;
        case QM_AUDIO_COMMAND_SET_VOLUME:
            command->result = qm_audio_control_set_volume_impl(control,
                                                               command->volume);
            break;
        case QM_AUDIO_COMMAND_SET_MUTE:
            command->result = qm_audio_control_set_mute_impl(control,
                                                             command->muted);
            break;
        default:
            command->result = -QM_EINVAL;
            break;
    }
}

static void dispatch_engine_command(qm_audio_t *audio, qm_audio_command_t *command)
{
    if (audio == NULL || audio->engine == NULL || command == NULL) {
        if (command != NULL) {
            command->result = -QM_EINVAL;
        }
        return;
    }
    switch (command->type) {
        case QM_AUDIO_COMMAND_SET_VOLUME:
            command->result = qm_audio_engine_render_setting_request(audio->engine,
                QM_AUDIO_RENDER_SETTING_VOLUME, command->volume);
            break;
        case QM_AUDIO_COMMAND_SET_MUTE:
            command->result = qm_audio_engine_render_setting_request(audio->engine,
                QM_AUDIO_RENDER_SETTING_MUTE, (uint8_t)command->muted);
            break;
        default:
            command->result = -QM_EINVAL;
            break;
    }
}

/**
 * @brief Remove one pending lower-layer fact in FIFO order. / 以 FIFO 顺序移除一条待处理底层事件。
 *
 * Only the event task removes facts. Pop one at a time so facts produced
 * during projection cannot overtake the remainder of an earlier batch. */
static qm_bool dispatch_pending(qm_audio_control_t *control, qm_bool end_turn)
{
    qm_audio_lower_event_t lower = {0};

    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (control->event_pending_count == 0U) {
        if (end_turn) {
            control->event_turn_active = QM_FALSE;
        }
        (void)qm_mutex_unlock(&control->lock);
        return QM_FALSE;
    }
    lower = control->event_pending[0];
    control->event_pending_count--;
    qm_memmove(control->event_pending, control->event_pending + 1,
                control->event_pending_count * sizeof(lower));
    if (control->event_pending_waiters != 0U) {
        (void)qm_sem_signal(&control->event_pending_space_sem);
    }
    (void)qm_mutex_unlock(&control->lock);
    dispatch_lower_event(control, &lower);
    return QM_TRUE;
}

static void flush_pending(qm_audio_control_t *control, qm_bool end_turn)
{
    while (dispatch_pending(control, end_turn)) {}
}

static int stage_lower_event(qm_audio_control_t *control, qm_audio_lower_event_source_t source, const qm_audio_event_t *event)
{
    qm_audio_event_msg_t wake_msg = {0};
    qm_bool wake = QM_FALSE;
    qm_bool inline_turn = QM_FALSE;
    int ret = QM_EOK;

    for (;;) {
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        if (control->event_pending_count < CONFIG_QM_AUDIO_EVENT_PENDING_MAX) {
            control->event_pending[control->event_pending_count].source = source;
            control->event_pending[control->event_pending_count].event = *event;
            control->event_pending_count++;
            if (qm_audio_event_is_current(control) && !control->event_turn_active) {
                control->event_turn_active = QM_TRUE;
                inline_turn = QM_TRUE;
            }
            wake = !control->event_turn_active && !control->event_lower_wake_pending;
            if (wake) {
                control->event_lower_wake_pending = QM_TRUE;
            }
            (void)qm_mutex_unlock(&control->lock);
            break;
        }
        if (qm_audio_event_is_current(control)) {
            (void)qm_mutex_unlock(&control->lock);
            /* The consumer cannot wait for itself. Project one fact inline. */
            (void)dispatch_pending(control, QM_FALSE);
        } else {
            control->event_pending_waiters++;
            (void)qm_mutex_unlock(&control->lock);
            ret = qm_sem_wait(&control->event_pending_space_sem, QM_WAIT_FOREVER);
            (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
            control->event_pending_waiters--;
            (void)qm_mutex_unlock(&control->lock);
            if (ret != QM_EOK) {
                return ret;
            }
        }
    }
    if (inline_turn) {
        flush_pending(control, QM_TRUE);
    }
    if (!wake) {
        return QM_EOK;
    }

    wake_msg.type = QM_AUDIO_EVENT_MSG_LOWER;
    wake_msg.control = control;
    ret = event_queue_send(control->audio, &wake_msg);
    if (ret != QM_EOK) {
        /* The durable fact remains in the FIFO; clear the coalescing bit so a
         * later producer can retry the wake after the queue drains. */
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        control->event_lower_wake_pending = QM_FALSE;
        (void)qm_mutex_unlock(&control->lock);
    }
    /* The fact has already been accepted into the durable FIFO. A full or
     * temporarily unavailable wake queue must not make the producer retry
     * the same public event and create a duplicate notification. */
    return QM_EOK;
}

int qm_audio_event_post(qm_audio_t *audio, const qm_audio_event_msg_t *msg)
{
    qm_audio_control_t *control = NULL;
    qm_bool reserved = QM_FALSE;
    int ret = QM_EOK;

    if (audio == NULL || msg == NULL ||
        !qm_queue_is_valid(&audio->event_queue)) {
        return -QM_EINVAL;
    }
    control = msg->control;
    if (control != NULL && control->audio != audio) {
        return -QM_EINVAL;
    }
    switch (msg->type) {
        case QM_AUDIO_EVENT_MSG_COMMAND:
            if (msg->u.command == NULL) {
                return -QM_EINVAL;
            }
            break;
        case QM_AUDIO_EVENT_MSG_DISPATCH:
            if (control == NULL) {
                return -QM_EINVAL;
            }
            break;
        case QM_AUDIO_EVENT_MSG_FRONTEND_WAKE:
            if (control != NULL || msg->u.event.type != QM_AUDIO_EVENT_WAKE) {
                return -QM_EINVAL;
            }
            break;
        case QM_AUDIO_EVENT_MSG_LOWER:
            if (control == NULL ||
                (msg->u.lower.source != QM_AUDIO_LOWER_SERVICE &&
                 msg->u.lower.source != QM_AUDIO_LOWER_PIPELINE)) {
                return -QM_EINVAL;
            }
            break;
        case QM_AUDIO_EVENT_MSG_EXIT:
        case QM_AUDIO_EVENT_MSG_WAKE:
            break;
        default:
            return -QM_EINVAL;
    }
    if (msg->type != QM_AUDIO_EVENT_MSG_EXIT && control != NULL) {
        ret = reserve_event_post(control);
        if (ret != QM_EOK) {
            return ret;
        }
        reserved = QM_TRUE;
    }
    if (msg->type == QM_AUDIO_EVENT_MSG_LOWER) {
        ret = stage_lower_event(control, msg->u.lower.source, &msg->u.lower.event);
    } else if (msg->type == QM_AUDIO_EVENT_MSG_DISPATCH) {
        if (qm_audio_event_is_current(control)) {
            /* Projection already runs on the consumer; dispatch in order
             * without queueing behind its own pending commands. */
            qm_audio_event_dispatch(control, &msg->u.event);
        } else {
            ret = stage_lower_event(control, QM_AUDIO_LOWER_PUBLIC, &msg->u.event);
        }
    } else {
        ret = event_queue_send(audio, msg);
    }
    if (reserved) {
        release_event_post(control);
    }
    return ret;
}

static void dispatch_msg(qm_audio_control_t *control, const qm_audio_event_msg_t *msg)
{
    switch (msg->type) {
        case QM_AUDIO_EVENT_MSG_COMMAND:
            /** The event task wraps this whole message in one command turn. / 事件任务将整条消息包裹在同一个命令轮次中。
             *
             * Service callbacks raised on this stack are staged and projected
             * by flush_pending() before the completion semaphore is signaled. */
            dispatch_command(control, msg->u.command);
            break;
        case QM_AUDIO_EVENT_MSG_DISPATCH:
            dispatch_event(control, &msg->u.event);
            break;
        case QM_AUDIO_EVENT_MSG_LOWER:
            (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
            control->event_lower_wake_pending = QM_FALSE;
            (void)qm_mutex_unlock(&control->lock);
            break;
        default:
            break;
    }
}

static void event_task(void *arg)
{
    qm_audio_t *audio = arg;
    qm_audio_event_msg_t msg = {0};
    qm_audio_control_t *control = NULL;
    qm_audio_control_t **link = NULL;
    unsigned int size = sizeof(msg);
    uint32_t wait_ms = QM_NO_WAIT;

    for (;;) {
        msg = (qm_audio_event_msg_t){0};
        control = NULL;
        link = NULL;
        size = sizeof(msg);
        if (qm_queue_recv(&audio->event_queue, &msg, &size, wait_ms) != QM_EOK ||
            size != sizeof(msg)) {
            goto advance;
        }
        if (msg.type == QM_AUDIO_EVENT_MSG_WAKE) {
            goto advance;
        }
        if (msg.type == QM_AUDIO_EVENT_MSG_FRONTEND_WAKE) {
            dispatch_wake_event(audio, &msg.u.event);
            goto advance;
        }
        control = msg.control;
        if (msg.type == QM_AUDIO_EVENT_MSG_EXIT) {
            if (control == NULL) {
                break;
            }
            qm_audio_dispatch_pipeline_facts(audio);
            flush_pending(control, QM_TRUE);
            (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
            link = &audio->controls;
            while (*link != NULL && *link != control) {
                link = &(*link)->event_next;
            }
            if (*link == control) {
                *link = control->event_next;
            }
            (void)qm_mutex_unlock(&audio->lock);
            /** EXIT follows all posts admitted before shutdown. / EXIT 位于关闭前已接纳的全部投递之后。
             *
             * This is the
             * ownership barrier after which Control state can be destroyed. */
            (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
            control->event_attached = QM_FALSE;
            (void)qm_sem_signal(&control->event_stopped_sem);
            (void)qm_mutex_unlock(&control->lock);
            goto advance;
        }
        if (msg.type == QM_AUDIO_EVENT_MSG_COMMAND && control == NULL) {
            dispatch_engine_command(audio, msg.u.command);
            if (qm_sem_is_valid(&msg.u.command->done)) {
                (void)qm_sem_signal(&msg.u.command->done);
            }
            goto advance;
        }
        if (control == NULL) {
            goto advance;
        }
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        control->event_turn_active = QM_TRUE;
        (void)qm_mutex_unlock(&control->lock);
        flush_pending(control, QM_FALSE);
        qm_audio_dispatch_pipeline_facts(audio);
        dispatch_msg(control, &msg);
        qm_audio_dispatch_pipeline_facts(audio);
        flush_pending(control, QM_TRUE);
        if (msg.type == QM_AUDIO_EVENT_MSG_COMMAND &&
            qm_sem_is_valid(&msg.u.command->done)) {
            (void)qm_sem_signal(&msg.u.command->done);
        }
advance:
        /* Consume one queued command before advancing durable worker facts.
         * 每次先消费一条队列命令，再推进工作任务持久状态。 */
        wait_ms = QM_WAIT_FOREVER;
        (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
        control = audio->controls;
        (void)qm_mutex_unlock(&audio->lock);
        qm_audio_dispatch_pipeline_facts(audio);
        for (; control != NULL; control = control->event_next) {
            (void)qm_audio_control_advance(control);
            flush_pending(control, QM_FALSE);
        }
        wait_ms = qm_audio_service_tick(audio->service);
    }
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    audio->event_started = QM_FALSE;
    (void)qm_sem_signal(&audio->event_stopped);
    (void)qm_mutex_unlock(&audio->lock);
    qm_task_exit(NULL);
}

void qm_audio_event_wake(qm_audio_t *audio)
{
    qm_audio_event_msg_t msg = {0};

    if (audio == NULL || !qm_queue_is_valid(&audio->event_queue)) {
        return;
    }
    msg.type = QM_AUDIO_EVENT_MSG_WAKE;
    (void)qm_audio_event_post(audio, &msg);
}

int qm_audio_event_shared_init(qm_audio_t *audio)
{
    int ret = QM_EOK;

    if (audio == NULL) {
        return -QM_EINVAL;
    }
    ret = qm_queue_new(&audio->event_queue, CONFIG_QM_AUDIO_CONTROL_EVENT_QUEUE_LEN,
                       sizeof(qm_audio_event_msg_t));
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_sem_new(&audio->event_stopped, 0U);
    if (ret != QM_EOK) {
        (void)qm_queue_free(&audio->event_queue);
        return ret;
    }
    audio->event_exit_sent = QM_FALSE;
    audio->event_started = QM_TRUE;
    ret = qm_task_new(&audio->event_task, "audio_event", event_task, audio,
                      CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_STACK_SIZE,
                      CONFIG_QM_AUDIO_CONTROL_EVENT_TASK_PRIORITY);
    if (ret != QM_EOK) {
        audio->event_started = QM_FALSE;
        (void)qm_sem_free(&audio->event_stopped);
        (void)qm_queue_free(&audio->event_queue);
    }
    return ret;
}

int qm_audio_event_shared_shutdown(qm_audio_t *audio, uint32_t timeout_ms)
{
    qm_audio_event_msg_t msg = {0};
    qm_utils_time_t deadline = {0};
    qm_bool running = QM_FALSE;
    qm_bool send_exit = QM_FALSE;

    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    if (audio == NULL || !qm_mutex_is_valid(&audio->lock)) {
        return -QM_EINVAL;
    }
    for (;;) {
        running = QM_FALSE;
        send_exit = QM_FALSE;
        (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
        running = audio->event_started;
        if (running && !audio->event_exit_sent) {
            audio->event_exit_sent = QM_TRUE;
            send_exit = QM_TRUE;
        }
        (void)qm_mutex_unlock(&audio->lock);
        if (!running) {
            return QM_EOK;
        }
        if (send_exit) {
            msg.type = QM_AUDIO_EVENT_MSG_EXIT;
            if (qm_audio_event_post(audio, &msg) != QM_EOK) {
                (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
                audio->event_exit_sent = QM_FALSE;
                (void)qm_mutex_unlock(&audio->lock);
                return -QM_EFULL;
            }
        }
        if (qm_sem_wait(&audio->event_stopped,
                         shutdown_time_left(&deadline, timeout_ms)) != QM_EOK) {
            return -QM_ETIMEOUT;
        }
    }
}

int qm_audio_event_shared_destroy(qm_audio_t *audio)
{
    qm_bool started = QM_FALSE;

    if (audio == NULL || !qm_mutex_is_valid(&audio->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    started = audio->event_started;
    (void)qm_mutex_unlock(&audio->lock);
    if (started) {
        return -QM_EBUSY;
    }
    if (qm_sem_is_valid(&audio->event_stopped)) {
        (void)qm_sem_free(&audio->event_stopped);
    }
    if (qm_queue_is_valid(&audio->event_queue)) {
        (void)qm_queue_free(&audio->event_queue);
    }
    return QM_EOK;
}

int qm_audio_event_runtime_init(qm_audio_control_t *control)
{
    int ret = QM_EOK;

    if (control == NULL || !qm_mutex_is_valid(&control->lock) || control->audio == NULL ||
        !control->audio->event_started) {
        return -QM_EINVAL;
    }
    ret = qm_sem_new(&control->event_post_idle_sem, 0U);
    if (ret != QM_EOK) {
        ret = -QM_EIO;
        goto fail;
    }
    ret = qm_sem_new(&control->event_pending_space_sem, 0U);
    if (ret != QM_EOK) {
        ret = -QM_EIO;
        goto fail;
    }
    ret = qm_sem_new(&control->event_stopped_sem, 0U);
    if (ret != QM_EOK) {
        ret = -QM_EIO;
        goto fail;
    }
    control->event_dispatch_active = QM_TRUE;
    control->event_exit_requested = QM_FALSE;
    control->event_shutdown_active = QM_FALSE;
    control->event_post_inflight = 0U;
    control->event_attached = QM_TRUE;
    (void)qm_mutex_lock(&control->audio->lock, QM_WAIT_FOREVER);
    control->event_next = control->audio->controls;
    control->audio->controls = control;
    (void)qm_mutex_unlock(&control->audio->lock);
    return QM_EOK;

fail:
    (void)qm_audio_event_runtime_destroy(control);
    return ret;
}

static int post_exit_marker(qm_audio_control_t *control)
{
    qm_audio_event_msg_t msg = {0};

    msg.control = control;
    msg.type = QM_AUDIO_EVENT_MSG_EXIT;
    msg.u.command = NULL;
    return qm_audio_event_post(control->audio, &msg);
}

int qm_audio_event_runtime_shutdown(qm_audio_control_t *control, uint32_t timeout_ms)
{
    qm_utils_time_t deadline = {0};
    qm_bool committed = QM_FALSE;
    int ret = QM_EOK;

    if (control == NULL || !qm_mutex_is_valid(&control->lock)) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);

    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (!control->event_attached) {
        (void)qm_mutex_unlock(&control->lock);
        return QM_EOK;
    }
    if (control->event_shutdown_active) {
        /* Another shutdown call is still in flight; reject instead of racing
         * the producer drain or the EXIT marker. */
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EBUSY;
    }
    if (control->event_exit_requested) {
        /* An earlier attempt already accepted the EXIT marker; the task will
         * finish exiting on its own. Only observe the completion. */
        committed = QM_TRUE;
    } else {
        control->event_shutdown_active = QM_TRUE;
        control->event_dispatch_active = QM_FALSE;
        control->event_exit_requested = QM_TRUE;
    }
    (void)qm_mutex_unlock(&control->lock);

    if (!committed) {
        /* Close producer admission, drain reservations, then append EXIT.
         * This preserves FIFO ordering between borrowed Control events and
         * the marker that permits Control destruction. */
        ret = wait_for_event_posts(control, &deadline, timeout_ms);
        if (ret != QM_EOK) {
            goto reopen;
        }
        ret = post_exit_marker(control);
        if (ret != QM_EOK) {
            goto reopen;
        }
    }

    if (qm_sem_is_valid(&control->event_stopped_sem)) {
        ret = qm_sem_wait(&control->event_stopped_sem,
                          shutdown_time_left(&deadline, timeout_ms));
    }
    if (ret == -QM_ETIMEOUT) {
        /* The marker was accepted; the task is still draining.  Keep the
         * gates closed and let the caller retry to observe completion. */
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        control->event_shutdown_active = QM_FALSE;
        (void)qm_mutex_unlock(&control->lock);
        return -QM_ETIMEOUT;
    }
    if (ret == QM_EOK) {
        (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
        control->event_shutdown_active = QM_FALSE;
        (void)qm_mutex_unlock(&control->lock);
    }
    return ret;

reopen:
    /* The EXIT marker never reached the queue, so no handler can complete the
     * shutdown later.  Reopen admission and let the caller retry. */
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    control->event_shutdown_active = QM_FALSE;
    control->event_exit_requested = QM_FALSE;
    control->event_dispatch_active = QM_TRUE;
    (void)qm_mutex_unlock(&control->lock);
    return ret;
}

int qm_audio_event_runtime_destroy(qm_audio_control_t *control)
{
    if (control == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&control->lock, QM_WAIT_FOREVER);
    if (control->event_attached || control->event_post_inflight != 0U) {
        (void)qm_mutex_unlock(&control->lock);
        return -QM_EBUSY;
    }
    control->event_dispatch_active = QM_FALSE;
    control->event_exit_requested = QM_FALSE;
    control->event_shutdown_active = QM_FALSE;
    (void)qm_mutex_unlock(&control->lock);
    if (qm_sem_is_valid(&control->event_stopped_sem)) {
        (void)qm_sem_free(&control->event_stopped_sem);
    }
    if (qm_sem_is_valid(&control->event_pending_space_sem)) {
        (void)qm_sem_free(&control->event_pending_space_sem);
    }
    if (qm_sem_is_valid(&control->event_post_idle_sem)) {
        (void)qm_sem_free(&control->event_post_idle_sem);
    }
    return QM_EOK;
}

qm_bool qm_audio_event_is_current(const qm_audio_control_t *control)
{
    qm_audio_control_t *mutable_control = (qm_audio_control_t *)control;

    if (control == NULL || !qm_mutex_is_valid(&mutable_control->lock) ||
        control->audio == NULL) {
        return QM_FALSE;
    }
    return qm_task_is_current(&control->audio->event_task) ? QM_TRUE : QM_FALSE;
}

void qm_audio_event_dispatch(qm_audio_control_t *control, const qm_audio_event_t *event)
{
    if (control == NULL || event == NULL || !qm_mutex_is_valid(&control->lock)) {
        return;
    }
    dispatch_event(control, event);
}

int qm_audio_command_post(qm_audio_control_t *control, qm_audio_command_t *command)
{
    qm_audio_event_msg_t msg = {0};
    int ret = QM_EOK;

    if (control == NULL || command == NULL ||
        !qm_sem_is_valid(&command->done)) {
        return -QM_EINVAL;
    }
    msg.control = control;
    msg.type = QM_AUDIO_EVENT_MSG_COMMAND;
    msg.u.command = command;
    ret = qm_audio_event_post(control->audio, &msg);
    if (ret != QM_EOK) {
        return -QM_EFULL;
    }
    while (qm_sem_wait(&command->done, QM_WAIT_FOREVER) != QM_EOK) {}
    return command->result;
}

int qm_audio_engine_command_post(qm_audio_t *audio, qm_audio_command_t *command)
{
    qm_audio_event_msg_t msg = {0};
    qm_bool active = QM_FALSE;
    int ret = QM_EOK;

    if (audio == NULL || command == NULL || !qm_sem_is_valid(&command->done)) {
        return -QM_EINVAL;
    }
    if (command->type != QM_AUDIO_COMMAND_SET_VOLUME &&
        command->type != QM_AUDIO_COMMAND_SET_MUTE) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&audio->lock, QM_WAIT_FOREVER);
    active = audio->event_started && !audio->closing;
    (void)qm_mutex_unlock(&audio->lock);
    if (!active) {
        return -QM_EBUSY;
    }
    msg.type = QM_AUDIO_EVENT_MSG_COMMAND;
    msg.u.command = command;
    ret = qm_audio_event_post(audio, &msg);
    if (ret != QM_EOK) {
        return -QM_EFULL;
    }
    while (qm_sem_wait(&command->done, QM_WAIT_FOREVER) != QM_EOK) {}
    return command->result;
}
