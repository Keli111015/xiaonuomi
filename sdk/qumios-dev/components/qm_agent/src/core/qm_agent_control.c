/**
 * @file qm_agent_control.c
 * @brief Serialized Agent control command queue and timer worker.
 *
 * The control task is the sole owner of blocking Agent lifecycle work. Audio
 * and video payloads deliberately bypass this queue; only bounded, low-rate
 * commands and normalized control events are admitted here.
 *
 * 控制任务独占可能阻塞的 Agent 生命周期操作。音频和视频数据刻意绕过该队列；仅有界的
 * 低频命令和标准化控制事件可以进入队列，保证状态转换按顺序执行。
 */

#include "qm_agent_internal.h"

#ifndef CONFIG_QM_AGENT_CONTROL_TASK_STACK_SIZE
#define CONFIG_QM_AGENT_CONTROL_TASK_STACK_SIZE 3072
#endif
#ifndef CONFIG_QM_AGENT_CONTROL_TASK_PRIORITY
#define CONFIG_QM_AGENT_CONTROL_TASK_PRIORITY 20
#endif
#ifndef CONFIG_QM_AGENT_CONTROL_QUEUE_LEN
#define CONFIG_QM_AGENT_CONTROL_QUEUE_LEN 16
#endif
#ifndef CONFIG_QM_AGENT_CONTROL_STOP_TIMEOUT_MS
#define CONFIG_QM_AGENT_CONTROL_STOP_TIMEOUT_MS 1000U
#endif

/* Yield before retrying when an indefinite queue wait ends without a message.
 * 无限等待却未取到消息时，让步重试的间隔。 */
#define QM_AGENT_CONTROL_RETRY_YIELD_MS 1U

static uint32_t nearest_deadline_locked(qm_agent_control_runtime_t *control)
{
    uint32_t wait_ms = QM_WAIT_FOREVER;
    uint32_t left;

    /* Reserved audio facts take priority over timers so lifecycle ordering is
     * not delayed by an otherwise long control wait.
     * 保留的音频事实优先于定时器，避免较长的控制等待延迟生命周期顺序。 */
    if (control->audio_started_pending || control->audio_terminal_pending ||
        control->audio_error_pending) return QM_NO_WAIT;
    if (control->audio_retry_pending) {
        left = qm_utils_time_left(&control->audio_retry_deadline);
        if (left < wait_ms) wait_ms = left;
    }
    if (control->reconcile_pending) {
        left = qm_utils_time_left(&control->reconcile_deadline);
        if (left < wait_ms) wait_ms = left;
    }
    if (control->auth_retry_pending) {
        left = qm_utils_time_left(&control->auth_retry_deadline);
        if (left < wait_ms) wait_ms = left;
    }
    if (control->tts_timeout_pending) {
        left = qm_utils_time_left(&control->tts_timeout_deadline);
        if (left < wait_ms) wait_ms = left;
    }
    return wait_ms;
}

/**
 * @brief Move expired timers out of protected control state for execution.
 * 将已过期定时器从受保护的控制状态取出以执行。
 *
 * The lock is held only while consuming deadline flags. Potentially blocking
 * lifecycle, authentication, and audio work runs after the lock is released.
 * 锁只在消费截止标志期间持有；可能阻塞的生命周期、认证和音频工作在释放锁后执行。
 */
static void take_due_actions(qm_agent_handle_t handle,
                             qm_bool *reconcile,
                             qm_bool *auth_retry,
                             qm_bool *tts_timeout,
                             uint32_t *request_id,
                             uint32_t *generation,
                             uint32_t *auth_generation)
{
    qm_agent_control_runtime_t *control = &handle->control;

    *reconcile = QM_FALSE;
    *auth_retry = QM_FALSE;
    *tts_timeout = QM_FALSE;
    *request_id = 0U;
    *generation = 0U;
    *auth_generation = 0U;

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (control->reconcile_pending &&
        qm_utils_time_is_expired(&control->reconcile_deadline)) {
        control->reconcile_pending = QM_FALSE;
        *reconcile = QM_TRUE;
    }
    if (control->auth_retry_pending &&
        qm_utils_time_is_expired(&control->auth_retry_deadline)) {
        control->auth_retry_pending = QM_FALSE;
        *auth_retry = QM_TRUE;
        *auth_generation = control->auth_retry_generation;
    }
    if (control->tts_timeout_pending &&
        qm_utils_time_is_expired(&control->tts_timeout_deadline)) {
        control->tts_timeout_pending = QM_FALSE;
        *request_id = control->tts_timeout_request_id;
        *generation = control->tts_timeout_generation;
        *tts_timeout = QM_TRUE;
    }
    qm_mutex_unlock(&handle->network_lock);
}

static void complete_message(qm_agent_control_message_t *message, int result)
{
    if (message == NULL || message->completion == NULL) return;
    message->completion->result = result;
    (void)qm_sem_signal(&message->completion->done);
}

/**
 * @brief Release heap-owned payloads carried by an asynchronous command.
 * 释放异步命令携带的堆内存载荷。
 *
 * This is used by both normal dequeue and shutdown rejection so every message
 * type has one ownership rule regardless of which control-path consumes it.
 * 正常出队和关闭拒绝都会调用本函数，因此无论哪条控制路径消费消息，各类型只有一套所有权规则。
 */
static void release_owned_message(qm_agent_control_message_t *message)
{
    if (message == NULL) {
        return;
    }

    if (message->type == QM_AGENT_CONTROL_TRANSPORT_EVENT &&
        message->data.event != NULL) {
        qm_agent_event_free(message->data.event);
        qm_free(message->data.event);
        message->data.event = NULL;
    } else if (message->type == QM_AGENT_CONTROL_MODULE_REQUEST &&
               message->data.module_request != NULL) {
        qm_agent_module_cloud_request_free(message->data.module_request);
        message->data.module_request = NULL;
    }
}

static void reject_queued_messages(qm_agent_handle_t handle)
{
    qm_agent_control_runtime_t *control = &handle->control;
    qm_agent_control_message_t message;
    unsigned int size;

    while (control->queue_valid) {
        size = sizeof(message);
        if (qm_queue_recv(&control->queue, &message, &size, QM_NO_WAIT) != QM_EOK) {
            break;
        }
        if (control->slots_sem_valid) (void)qm_sem_signal(&control->slots_sem);
        release_owned_message(&message);
        complete_message(&message, -QM_EBUSY);
    }
}

/**
 * @brief Deliver reserved audio facts and due timers in deterministic order.
 * 按确定顺序交付保留音频事实和到期定时器。
 *
 * Audio start, terminal, and error mailboxes do not rely on queue capacity,
 * preventing high-rate callbacks from losing lifecycle facts under pressure.
 * 音频开始、终态和错误邮箱不依赖队列容量，避免高频回调在压力下丢失生命周期事实。
 */
static void execute_due_actions(qm_agent_handle_t handle)
{
    qm_bool reconcile;
    qm_bool auth_retry;
    qm_bool tts_timeout;
    uint32_t request_id;
    uint32_t generation;
    uint32_t auth_generation;
    qm_agent_control_message_t message;

    /* Drain reserved facts in start/terminal order. Copy one value at a time
     * to keep the control task stack bounded; callbacks never wait for us.
     * 按开始/终态顺序排空保留事实；每次仅复制一个值以限制控制任务栈占用，回调不会等待此任务。 */
    qm_memset(&message, 0, sizeof(message));
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (handle->control.audio_started_pending) {
        message.type = QM_AGENT_CONTROL_AUDIO_EVENT;
        message.data.audio_event = handle->control.audio_started;
        handle->control.audio_started_pending = QM_FALSE;
    }
    qm_mutex_unlock(&handle->network_lock);
    if (message.type == QM_AGENT_CONTROL_AUDIO_EVENT) {
        (void)qm_agent_control_execute(handle, &message);
    }
    message.type = QM_AGENT_CONTROL_WAKE;
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (handle->control.audio_terminal_pending) {
        message.type = QM_AGENT_CONTROL_AUDIO_EVENT;
        message.data.audio_event = handle->control.audio_terminal;
        handle->control.audio_terminal_pending = QM_FALSE;
    }
    qm_mutex_unlock(&handle->network_lock);
    if (message.type == QM_AGENT_CONTROL_AUDIO_EVENT) {
        (void)qm_agent_control_execute(handle, &message);
    }
    message.type = QM_AGENT_CONTROL_WAKE;
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (handle->control.audio_error_pending) {
        message.type = QM_AGENT_CONTROL_AUDIO_INPUT_ERROR;
        message.data.audio_error.code = handle->control.audio_error;
        message.data.audio_error.generation = handle->control.audio_error_generation;
        handle->control.audio_error_pending = QM_FALSE;
    }
    qm_mutex_unlock(&handle->network_lock);
    if (message.type == QM_AGENT_CONTROL_AUDIO_INPUT_ERROR) {
        (void)qm_agent_control_execute(handle, &message);
    }
    message.type = QM_AGENT_CONTROL_WAKE;
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (handle->control.audio_retry_pending &&
        qm_utils_time_is_expired(&handle->control.audio_retry_deadline)) {
        handle->control.audio_retry_pending = QM_FALSE;
        message.type = QM_AGENT_CONTROL_AUDIO_RECONCILE;
    }
    qm_mutex_unlock(&handle->network_lock);
    if (message.type == QM_AGENT_CONTROL_AUDIO_RECONCILE) {
        (void)qm_agent_control_execute(handle, &message);
    }

    take_due_actions(handle, &reconcile, &auth_retry, &tts_timeout,
                     &request_id, &generation, &auth_generation);
    qm_memset(&message, 0, sizeof(message));
    if (reconcile) {
        message.type = QM_AGENT_CONTROL_RECONCILE;
        (void)qm_agent_control_execute(handle, &message);
    }
    if (auth_retry) {
        message.type = QM_AGENT_CONTROL_AUTH_RETRY;
        message.data.asr.timeout_ms = auth_generation;
        (void)qm_agent_control_execute(handle, &message);
    }
    if (tts_timeout) {
        message.type = QM_AGENT_CONTROL_TTS_TIMEOUT;
        message.data.tts_cancel.request_id = request_id;
        message.data.tts_cancel.generation = generation;
        (void)qm_agent_control_execute(handle, &message);
    }
}

/**
 * @brief Run the serialized Agent control loop until teardown completes.
 * 运行串行 Agent 控制循环，直到拆除完成。
 */
static void control_task(void *arg)
{
    qm_agent_handle_t handle = (qm_agent_handle_t)arg;
    qm_agent_control_runtime_t *control;
    qm_agent_control_message_t message;
    unsigned int size;
    int ret;

    if (handle == NULL) return;
    control = &handle->control;

    for (;;) {
        qm_bool shutdown;
        uint32_t wait_ms;

        qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
        shutdown = control->shutdown_requested;
        wait_ms = nearest_deadline_locked(control);
        qm_mutex_unlock(&handle->network_lock);

        if (shutdown) {
            reject_queued_messages(handle);
            break;
        }

        size = sizeof(message);
        ret = qm_queue_recv(&control->queue, &message, &size, wait_ms);
        if (ret == QM_EOK) {
            if (control->slots_sem_valid) (void)qm_sem_signal(&control->slots_sem);
            if (message.type == QM_AGENT_CONTROL_WAKE) {
                qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
                control->wake_posted = QM_FALSE;
                qm_mutex_unlock(&handle->network_lock);
            }

            if ((message.type == QM_AGENT_CONTROL_TRANSPORT_EVENT &&
                 message.data.event == NULL) ||
                (message.type == QM_AGENT_CONTROL_MODULE_REQUEST &&
                 message.data.module_request == NULL)) {
                complete_message(&message, -QM_EINVAL);
            } else {
                int command_ret = qm_agent_control_execute(handle, &message);
                release_owned_message(&message);
                complete_message(&message, command_ret);
            }
        } else if (ret != -QM_ETIMEOUT && ret != -QM_EEMPTY &&
                   wait_ms == QM_WAIT_FOREVER) {
            /* An indefinite wait must only end with a message; ending without
             * one is a backend anomaly, so yield and retry instead of spinning
             * the resident worker. An expired finite wait is the normal timer
             * path and must not sleep here: qm_msleep rounds up to a whole
             * 10 ms tick on MCU targets, where a timeout is also reported as a
             * generic failure and would otherwise stall every expiry by a tick. */
            /* 无限等待只应以收到消息结束；没有消息即为后端异常，让步后重试而不是忙等常驻任务。
             * 有限等待到期属于正常定时器路径，不能在此休眠：MCU 上 qm_msleep 会向上取整为一整个
             * 10ms tick，且超时在那类平台上同样上报为通用失败，否则每次到期都会多耗一个 tick。 */
            (void)qm_msleep(QM_AGENT_CONTROL_RETRY_YIELD_MS);
        }
        execute_due_actions(handle);
    }

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    control->task_exited = QM_TRUE;
    qm_mutex_unlock(&handle->network_lock);
    (void)qm_sem_signal(&control->exit_sem);
}

/* Caller holds network_lock. The slot reservation makes queue_send bounded on
 * host QMOS, whose primitive otherwise waits forever when the queue is full.
 * 调用方持有 network_lock。槽位预留使 host QMOS 的 queue_send 有界；其原语在队列满时会
 * 无限等待。 */
static int post_locked(qm_agent_handle_t handle,
                       const qm_agent_control_message_t *message)
{
    qm_agent_control_runtime_t *control = &handle->control;
    int ret;

    if (!control->queue_valid || !control->slots_sem_valid) return -QM_EBUSY;
    ret = qm_sem_wait(&control->slots_sem, QM_NO_WAIT);
    if (ret != QM_EOK) return -QM_EFULL;
    ret = qm_queue_send(&control->queue, (void *)message, sizeof(*message));
    if (ret != QM_EOK) (void)qm_sem_signal(&control->slots_sem);
    return ret;
}

static int post_wake_locked(qm_agent_handle_t handle)
{
    qm_agent_control_runtime_t *control = &handle->control;
    qm_agent_control_message_t message;
    int ret;

    if (control->wake_posted) return QM_EOK;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_WAKE;
    ret = post_locked(handle, &message);
    if (ret == QM_EOK) control->wake_posted = QM_TRUE;
    return ret;
}

/**
 * @brief Create per-session queue, timer primitives, and control task.
 * 创建每会话队列、定时原语和控制任务。
 */
int qm_agent_control_init(qm_agent_handle_t handle)
{
    qm_agent_control_runtime_t *control;
    int ret;

    if (handle == NULL) return -QM_EINVAL;
    control = &handle->control;
    qm_memset(control, 0, sizeof(*control));

    ret = qm_queue_new(&control->queue, CONFIG_QM_AGENT_CONTROL_QUEUE_LEN,
                       sizeof(qm_agent_control_message_t));
    if (ret != QM_EOK) return ret;
    control->queue_valid = QM_TRUE;
    ret = qm_sem_new(&control->slots_sem, CONFIG_QM_AGENT_CONTROL_QUEUE_LEN);
    if (ret != QM_EOK) {
        (void)qm_queue_free(&control->queue);
        control->queue_valid = QM_FALSE;
        return ret;
    }
    control->slots_sem_valid = QM_TRUE;
    ret = qm_sem_new(&control->exit_sem, 0U);
    if (ret != QM_EOK) {
        (void)qm_sem_free(&control->slots_sem);
        (void)qm_queue_free(&control->queue);
        control->slots_sem_valid = QM_FALSE;
        control->queue_valid = QM_FALSE;
        return ret;
    }
    control->exit_sem_valid = QM_TRUE;
    control->accepting = QM_TRUE;

    ret = qm_task_new(&control->task, "qm_agent_ctl", control_task, handle,
                      CONFIG_QM_AGENT_CONTROL_TASK_STACK_SIZE,
                      CONFIG_QM_AGENT_CONTROL_TASK_PRIORITY);
    if (ret != QM_EOK) {
        (void)qm_sem_free(&control->exit_sem);
        (void)qm_sem_free(&control->slots_sem);
        (void)qm_queue_free(&control->queue);
        control->exit_sem_valid = QM_FALSE;
        control->slots_sem_valid = QM_FALSE;
        control->queue_valid = QM_FALSE;
        return ret;
    }
    control->task_started = QM_TRUE;
    return QM_EOK;
}

static int control_deinit(qm_agent_handle_t handle, uint32_t timeout_ms)
{
    qm_agent_control_runtime_t *control;
    qm_agent_control_message_t message;
    int ret;

    if (handle == NULL) return -QM_EINVAL;
    control = &handle->control;
    if (!control->task_started) {
        if (control->exit_sem_valid) (void)qm_sem_free(&control->exit_sem);
        if (control->slots_sem_valid) (void)qm_sem_free(&control->slots_sem);
        if (control->queue_valid) (void)qm_queue_free(&control->queue);
        qm_memset(control, 0, sizeof(*control));
        return QM_EOK;
    }
    if (qm_task_is_current(&control->task)) return -QM_EBUSY;

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    control->accepting = QM_FALSE;
    control->shutdown_requested = QM_TRUE;
    control->reconcile_pending = QM_FALSE;
    control->auth_retry_pending = QM_FALSE;
    control->tts_timeout_pending = QM_FALSE;
    control->audio_retry_pending = QM_FALSE;
    control->audio_started_pending = QM_FALSE;
    control->audio_terminal_pending = QM_FALSE;
    control->audio_error_pending = QM_FALSE;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_WAKE;
    (void)post_locked(handle, &message);
    qm_mutex_unlock(&handle->network_lock);

    if (!control->task_exited) {
        ret = qm_sem_wait(&control->exit_sem, timeout_ms);
        if (ret != QM_EOK) return ret;
        control->task_exited = QM_TRUE;
    }
    ret = qm_task_exit(&control->task);
    if (ret != QM_EOK) return ret;
    if (control->exit_sem_valid) (void)qm_sem_free(&control->exit_sem);
    if (control->slots_sem_valid) (void)qm_sem_free(&control->slots_sem);
    if (control->queue_valid) (void)qm_queue_free(&control->queue);
    qm_memset(control, 0, sizeof(*control));
    return QM_EOK;
}

/**
 * @brief Request control-task shutdown without blocking the caller indefinitely.
 * 请求控制任务退出，且不会无限期阻塞调用方。
 */
int qm_agent_control_deinit(qm_agent_handle_t handle)
{
    return control_deinit(handle, CONFIG_QM_AGENT_CONTROL_STOP_TIMEOUT_MS);
}

int qm_agent_control_deinit_wait(qm_agent_handle_t handle)
{
    return control_deinit(handle, QM_WAIT_FOREVER);
}

qm_bool qm_agent_control_is_current(qm_agent_handle_t handle)
{
    return (handle != NULL && handle->control.task_started &&
            qm_task_is_current(&handle->control.task)) ? QM_TRUE : QM_FALSE;
}

/**
 * @brief Queue a synchronous low-rate command and wait for its completion.
 * 入队一条同步低频命令并等待其完成。
 *
 * The caller owns completion storage while waiting; pointer payloads are valid
 * only for this synchronous path or when separately cloned for ownership.
 * 调用方在等待期间拥有 completion 存储；指针载荷只用于此同步路径，或必须单独克隆后取得所有权。
 */
int qm_agent_control_call(qm_agent_handle_t handle,
                          const qm_agent_control_message_t *message)
{
    qm_agent_control_message_t queued;
    qm_agent_control_completion_t completion;
    int ret;

    if (handle == NULL || message == NULL) return -QM_EINVAL;
    if (qm_agent_control_is_current(handle)) {
        return qm_agent_control_execute(handle, message);
    }

    qm_memcpy(&queued, message, sizeof(queued));
    qm_memset(&completion, 0, sizeof(completion));
    ret = qm_sem_new(&completion.done, 0U);
    if (ret != QM_EOK) return ret;
    completion.valid = QM_TRUE;
    queued.completion = &completion;

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!handle->control.task_started || !handle->control.accepting ||
        handle->control.shutdown_requested ||
        (handle->destroying && message->type != QM_AGENT_CONTROL_DESTROY)) {
        qm_mutex_unlock(&handle->network_lock);
        (void)qm_sem_free(&completion.done);
        return -QM_EBUSY;
    }
    ret = post_locked(handle, &queued);
    qm_mutex_unlock(&handle->network_lock);
    if (ret != QM_EOK) {
        (void)qm_sem_free(&completion.done);
        return ret;
    }

    ret = qm_sem_wait(&completion.done, QM_WAIT_FOREVER);
    if (ret == QM_EOK) ret = completion.result;
    (void)qm_sem_free(&completion.done);
    return ret;
}

int qm_agent_control_post_event(qm_agent_handle_t handle, qm_agent_event_t *event)
{
    qm_agent_control_message_t message;
    int ret;

    if (handle == NULL || event == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_TRANSPORT_EVENT;
    message.data.event = event;

    if (qm_agent_control_is_current(handle)) {
        ret = qm_agent_control_execute(handle, &message);
        qm_agent_event_free(event);
        qm_free(event);
        return ret;
    }

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!handle->control.task_started || !handle->control.accepting ||
        handle->control.shutdown_requested || handle->destroying) {
        qm_mutex_unlock(&handle->network_lock);
        qm_agent_event_free(event);
        qm_free(event);
        return -QM_EBUSY;
    }
    ret = post_locked(handle, &message);
    qm_mutex_unlock(&handle->network_lock);
    if (ret != QM_EOK) {
        qm_agent_event_free(event);
        qm_free(event);
    }
    return ret;
}

int qm_agent_control_post_module_request(
    qm_agent_handle_t handle,
    qm_agent_module_cloud_request_t *request)
{
    qm_agent_control_message_t message;
    int ret;

    if (request == NULL) {
        return -QM_EINVAL;
    }
    if (handle == NULL) {
        qm_agent_module_cloud_request_free(request);
        return -QM_EINVAL;
    }

    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_MODULE_REQUEST;
    message.data.module_request = request;

    if (qm_agent_control_is_current(handle)) {
        ret = qm_agent_control_execute(handle, &message);
        release_owned_message(&message);
        return ret;
    }

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!handle->control.task_started || !handle->control.accepting ||
        handle->control.shutdown_requested || handle->destroying) {
        qm_mutex_unlock(&handle->network_lock);
        release_owned_message(&message);
        return -QM_EBUSY;
    }
    ret = post_locked(handle, &message);
    qm_mutex_unlock(&handle->network_lock);
    if (ret != QM_EOK) {
        release_owned_message(&message);
    }
    return ret;
}

int qm_agent_control_post_module_event(qm_agent_handle_t handle,
                                       qm_agent_module_event_id_t event)
{
    qm_agent_control_message_t message;
    int ret;

    if (handle == NULL) {
        return -QM_EINVAL;
    }
    if (event != QM_AGENT_MODULE_EVENT_CLOUD_CONNECTED &&
        event != QM_AGENT_MODULE_EVENT_CLOUD_DISCONNECTED) {
        return -QM_EINVAL;
    }

    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_MODULE_EVENT;
    message.data.module_event = event;

    if (qm_agent_control_is_current(handle)) {
        return qm_agent_control_execute(handle, &message);
    }

    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!handle->control.task_started || !handle->control.accepting ||
        handle->control.shutdown_requested || handle->destroying) {
        qm_mutex_unlock(&handle->network_lock);
        return -QM_EBUSY;
    }
    ret = post_locked(handle, &message);
    qm_mutex_unlock(&handle->network_lock);
    return ret;
}

int qm_agent_control_post_audio_event(qm_agent_handle_t handle,
                                      const qm_audio_event_t *event)
{
    qm_agent_control_runtime_t *control;
    int ret;

    if (handle == NULL || event == NULL) return -QM_EINVAL;
    if (event->type != QM_AUDIO_EVENT_STARTED &&
        event->type != QM_AUDIO_EVENT_FINISHED &&
        event->type != QM_AUDIO_EVENT_CANCELLED &&
        event->type != QM_AUDIO_EVENT_PREEMPTED &&
        event->type != QM_AUDIO_EVENT_ERROR) return QM_EOK;

    control = &handle->control;
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!control->task_started || !control->accepting ||
        control->shutdown_requested || handle->destroying) {
        qm_mutex_unlock(&handle->network_lock);
        return -QM_EBUSY;
    }
    /* One Agent owns one operation. Its start and terminal each have a
     * reserved slot, including while play() has not returned its ID yet.
     * 一个 Agent 只拥有一个操作，其开始和终态各有一个保留槽位，即使 play() 尚未返回 ID 也不丢失。 */
    if (event->type == QM_AUDIO_EVENT_STARTED) {
        control->audio_started = *event;
        control->audio_started_pending = QM_TRUE;
    } else {
        control->audio_terminal = *event;
        control->audio_terminal_pending = QM_TRUE;
    }
    ret = post_wake_locked(handle);
    qm_mutex_unlock(&handle->network_lock);
    /* A full queue already wakes the worker, which drains facts each turn.
     * 队列满意味着工作任务已被唤醒，它会在每轮处理时排空这些事实。 */
    return ret == -QM_EFULL ? QM_EOK : ret;
}

int qm_agent_control_post_audio_begin(qm_agent_handle_t handle,
                                      const qm_agent_audio_format_t *format,
                                      uint32_t generation)
{
    qm_agent_control_message_t message;
    int ret;

    if (handle == NULL || format == NULL) return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    message.type = QM_AGENT_CONTROL_AUDIO_BEGIN;
    message.data.audio_begin.format = *format;
    message.data.audio_begin.generation = generation;

    /* A spontaneous transport stream (for example, RTC downlink) has no
     * control-plane TTS_BEGIN.  Queue only its small admission request; the
     * media callback itself remains nonblocking and drops frames until the
     * control task has installed the PUSH operation.
     * 自发传输流（例如 RTC 下行）没有控制平面的 TTS_BEGIN；仅入队小型准入请求，媒体回调本身
     * 保持非阻塞，在控制任务创建 PUSH 操作前丢弃帧。 */
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!handle->control.task_started || !handle->control.accepting ||
        handle->control.shutdown_requested || handle->destroying) {
        qm_mutex_unlock(&handle->network_lock);
        return -QM_EBUSY;
    }
    ret = post_locked(handle, &message);
    qm_mutex_unlock(&handle->network_lock);
    return ret;
}

int qm_agent_control_post_audio_error(qm_agent_handle_t handle,
                                      int code,
                                      uint32_t generation)
{
    int ret;

    if (handle == NULL) return -QM_EINVAL;
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!handle->control.task_started || !handle->control.accepting ||
        handle->control.shutdown_requested || handle->destroying) {
        qm_mutex_unlock(&handle->network_lock);
        return -QM_EBUSY;
    }
    handle->control.audio_error_pending = QM_TRUE;
    handle->control.audio_error = code;
    handle->control.audio_error_generation = generation;
    ret = post_wake_locked(handle);
    qm_mutex_unlock(&handle->network_lock);
    return ret == -QM_EFULL ? QM_EOK : ret;
}

int qm_agent_control_schedule(qm_agent_handle_t handle,
                              qm_agent_control_action_t type,
                              uint32_t delay_ms,
                              uint32_t request_id,
                              uint32_t generation)
{
    qm_agent_control_runtime_t *control;
    int ret;

    if (handle == NULL || delay_ms >= UINT32_MAX / 2U) return -QM_EINVAL;
    control = &handle->control;
    qm_mutex_lock(&handle->network_lock, QM_WAIT_FOREVER);
    if (!control->task_started || !control->queue_valid || !control->accepting ||
        control->shutdown_requested || handle->destroying) {
        qm_mutex_unlock(&handle->network_lock);
        return -QM_EBUSY;
    }

    switch (type) {
        case QM_AGENT_CONTROL_AUDIO_RECONCILE:
            if (!control->audio_retry_pending ||
                qm_utils_time_left(&control->audio_retry_deadline) > delay_ms) {
                qm_utils_time_countdown_ms(&control->audio_retry_deadline, delay_ms);
            }
            control->audio_retry_pending = QM_TRUE;
            break;
        case QM_AGENT_CONTROL_RECONCILE:
            if (!control->reconcile_pending ||
                qm_utils_time_left(&control->reconcile_deadline) > delay_ms) {
                qm_utils_time_countdown_ms(&control->reconcile_deadline, delay_ms);
            }
            control->reconcile_pending = QM_TRUE;
            break;
        case QM_AGENT_CONTROL_AUTH_RETRY:
            control->auth_retry_pending = QM_TRUE;
            control->auth_retry_generation = generation;
            qm_utils_time_countdown_ms(&control->auth_retry_deadline, delay_ms);
            break;
        case QM_AGENT_CONTROL_TTS_TIMEOUT:
            control->tts_timeout_pending = QM_TRUE;
            control->tts_timeout_request_id = request_id;
            control->tts_timeout_generation = generation;
            qm_utils_time_countdown_ms(&control->tts_timeout_deadline, delay_ms);
            break;
        default:
            qm_mutex_unlock(&handle->network_lock);
            return -QM_EINVAL;
    }
    ret = post_wake_locked(handle);
    qm_mutex_unlock(&handle->network_lock);
    return type == QM_AGENT_CONTROL_AUDIO_RECONCILE && ret == -QM_EFULL ?
           QM_EOK : ret;
}
