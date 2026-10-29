/**
 * @file qm_audio_service.c
 * @brief Single-route admission, scheduling, and session lifecycle. / 单路输出的准入、调度与会话生命周期。
 *
 * Single-route playback admission and session lifecycle.
 *
 * Service owns policy; Engine owns the physical route.  The implementation
 * keeps the public state machine small and uses a bounded session table so an
 * application cannot create an unbounded number of waiting playback jobs.
 */
#include "qm_audio_priv.h"
#include "qm_utils_timer.h"

/** @brief Copied Service lifecycle notification awaiting delivery. / 等待交付的已复制 Service 生命周期通知。 */
typedef struct {
    qm_audio_event_cb_t callback;
    void *context;
    qm_audio_event_t event;
} service_notice_t;

static int request_action_locked(qm_audio_session_t *session, qm_audio_service_action_t action, uint32_t value);
static int advance_action(qm_audio_session_t *session, qm_audio_service_action_t action, int *result);
static int wait_action(qm_audio_session_t *session, qm_audio_service_action_t action, uint32_t timeout_ms);
static int run_action(qm_audio_session_t *session, qm_audio_service_action_t action, uint32_t value);
static void advance_actions(qm_audio_service_t *service);
static void activate_queued(qm_audio_service_t *service, qm_bool wait);

/**
 * @brief Calculate position from the persistent media base. / 从持久媒体基准计算当前位置。
 *
 * Caller holds Service::lock. Media base survives route replacement; the
 * counter covers media submitted to this particular output run. */
static uint32_t submitted_position(qm_audio_session_t *session)
{
    uint32_t value = session->position_base_ms;
    if (session->stream != NULL) {
        session->submitted_valid = qm_audio_engine_render_submitted(
            session->stream, &session->submitted_ms) == QM_EOK;
    }
    if (session->submitted_ms > UINT32_MAX - value) return UINT32_MAX;
    return value + session->submitted_ms;
}

static void cache_position(qm_audio_session_t *session, uint32_t relative_ms)
{
    uint32_t position;
    uint32_t end;
    if (session->origin_pending) return;
    position = session->position_base_ms;
    if (relative_ms > UINT32_MAX - position) {
        position = UINT32_MAX;
    } else {
        position += relative_ms;
    }
    end = submitted_position(session);
    session->cached_position_ms = qm_audio_encoded_bytes(&session->service->engine->output_format) ?
        position :
        (position > end ? end : position);
    session->position_valid = QM_TRUE;
}

/**
 * @brief Capture a session event while its callback context is still owned. / 在会话仍拥有回调上下文时捕获事件。
 *
 * Caller holds Service::lock.  Capture the event while the session is still
 * table-owned, then retain the session's owning-Control sink context until
 * delivery returns. */
static void prepare_notice_locked(qm_audio_service_t *service, qm_audio_session_t *session, qm_audio_event_type_t type, int result, uint32_t reason, service_notice_t *notice)
{
    if (session != NULL) session->result = result;
    if (notice == NULL) {
        return;
    }
    qm_memset(notice, 0, sizeof(*notice));
    if (service == NULL || session == NULL || session->event_callback == NULL) {
        return;
    }

    notice->callback = session->event_callback;
    notice->context = session->event_context;
    notice->event.type = type;
    notice->event.request_id = session->request.request_id;
    notice->event.generation = session->generation;
    notice->event.result = (qm_err_t)result;
    notice->event.position_ms = session->cached_position_ms;
    notice->event.reason = reason;
    notice->event.operation_id = session->request.request_id;
    notice->event.timestamp_ms = qm_now_ms();
    qm_memset(&notice->event.wake, 0, sizeof(notice->event.wake));
    service->event_inflight++;
}

/**
 * @brief Deliver a Service event without holding the Service lock. / 不持有 Service 锁交付 Service 事件。
 *
 * The callback runs without Service::lock.  Clearing the sink waits for this
 * counter, so Control cannot free its callback context while delivery runs. */
static void deliver_notice(qm_audio_service_t *service, service_notice_t *notice)
{
    if (service == NULL || notice == NULL || notice->callback == NULL) {
        return;
    }

    notice->callback(&notice->event, notice->context);

    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (service->event_inflight != 0U) {
        service->event_inflight--;
    }
    if (service->event_inflight == 0U &&
        qm_sem_is_valid(&service->event_idle_sem)) {
        (void)qm_sem_signal(&service->event_idle_sem);
    }
    (void)qm_mutex_unlock(&service->lock);
}

/**
 * @brief Allocate one Control's disjoint request-ID range. / 为一个 Control 分配互不重叠的请求 ID 区间。
 *
 * Hand out the next disjoint request-id base for one Control.  Operations
 * allocate IDs inside their own 16-bit-shifted range, so sessions from
 * different Controls never collide in the shared session table. */
int qm_audio_service_alloc_request_id_base(qm_audio_service_t *service, qm_audio_request_id_t *base)
{
    if (service == NULL || !qm_mutex_is_valid(&service->lock) || base == NULL) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (service->next_request_id_base > UINT16_MAX) {
        (void)qm_mutex_unlock(&service->lock);
        return -QM_EFULL;
    }
    *base = service->next_request_id_base++ << 16;
    (void)qm_mutex_unlock(&service->lock);
    return QM_EOK;
}

/**
 * @brief Wait until all Service-to-Control callbacks are quiescent. / 等待全部 Service 到 Control 的回调静止。
 *
 * Control calls
 * this on destroy after releasing every session, so a notice that borrowed
 * the Control as its sink context cannot outlive the freed handle. */
int qm_audio_service_wait_event_idle(qm_audio_service_t *service, uint32_t timeout_ms)
{
    qm_utils_time_t deadline;
    uint32_t left;

    if (service == NULL || !qm_mutex_is_valid(&service->lock)) {
        return -QM_EINVAL;
    }
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        int ret;

        (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
        if (service->event_inflight == 0U) {
            (void)qm_mutex_unlock(&service->lock);
            return QM_EOK;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        (void)qm_mutex_unlock(&service->lock);
        if (left == 0U || !qm_sem_is_valid(&service->event_idle_sem)) {
            return -QM_ETIMEOUT;
        }
        ret = qm_sem_wait(&service->event_idle_sem, left);
        if (ret != QM_EOK) {
            return -QM_ETIMEOUT;
        }
    }
}

static qm_bool can_wait(qm_audio_service_t *service)
{
    qm_audio_engine_t *engine = service->engine;

    return !qm_task_is_current(&engine->render_task) &&
           (engine->audio == NULL || !qm_task_is_current(&engine->audio->event_task));
}

/* Service state changes wake every registered driver; Engine wakes one driver
 * to advance the shared slot, whose commit then broadcasts completion. */
static void signal_action_locked(qm_audio_session_t *session)
{
    uint32_t i = 0U;

    for (i = 0U; i < session->action_waiters; ++i) {
        (void)qm_sem_signal(&session->action_sem);
    }
}

static void signal_state(qm_audio_session_t *session)
{
    if (session != NULL && qm_sem_is_valid(&session->state_sem)) {
        (void)qm_sem_signal(&session->state_sem);
    }
}

/* Service lock arbitrates expiry against admission and explicit cancellation.
 * The copied notice retains its sink; callbacks always run after unlocking. */
static qm_bool expire_locked(qm_audio_service_t *service, qm_audio_session_t *session, service_notice_t *notice)
{
    if (session->state != QM_AUDIO_SESSION_QUEUED ||
        !session->acquire_deadline_valid ||
        !qm_utils_time_is_expired(&session->acquire_deadline)) return QM_FALSE;
    session->state = QM_AUDIO_SESSION_CANCELLED;
    prepare_notice_locked(service, session, QM_AUDIO_EVENT_CANCELLED,
                          -QM_ETIMEOUT, 0U, notice);
    signal_state(session);
    return QM_TRUE;
}

/* Expire queued requests even while an active owner holds the route. Return
 * the next wake budget; no finite deadlines means an indefinite queue wait. */
uint32_t qm_audio_service_tick(qm_audio_service_t *service)
{
    if (service == NULL || !qm_mutex_is_valid(&service->lock)) return QM_WAIT_FOREVER;
    advance_actions(service);
    for (;;) {
        service_notice_t notice;
        qm_bool expired = QM_FALSE;
        uint32_t next = QM_WAIT_FOREVER;
        (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
        for (uint32_t i = 0U; i < service->session_count; ++i) {
            qm_audio_session_t *session = service->sessions[i];
            if (session == NULL || session->state != QM_AUDIO_SESSION_QUEUED ||
                !session->acquire_deadline_valid) continue;
            if (expire_locked(service, session, &notice)) {
                expired = QM_TRUE;
                break;
            }
            uint32_t left = qm_utils_time_left(&session->acquire_deadline);
            if (left < next) next = left;
        }
        (void)qm_mutex_unlock(&service->lock);
        if (!expired) {
            activate_queued(service, QM_FALSE);
            return next;
        }
        deliver_notice(service, &notice);
    }
}

static int session_index(qm_audio_service_t *service, qm_audio_session_t *session)
{
    uint32_t i;

    for (i = 0U; i < service->session_count; ++i) {
        if (service->sessions[i] == session) {
            return (int)i;
        }
    }
    return -1;
}

/**
 * @brief Pin a session across an unlocked Engine operation. / 在解锁 Engine 操作期间固定会话。
 *
 * Service scheduling can retain a table entry across an unlocked Engine call.
 * Public callers still serialize use of the same session handle; this pin only
 * protects Service-owned cross-session transitions from concurrent release. */
static qm_bool session_pin_locked(qm_audio_service_t *service, qm_audio_session_t *session)
{
    if (service == NULL || session == NULL || session->service != service ||
        session->release_pending || session_index(service, session) < 0) {
        return QM_FALSE;
    }
    session->internal_refs++;
    return QM_TRUE;
}

static void session_unpin_locked(qm_audio_session_t *session)
{
    if (session == NULL || session->internal_refs == 0U) {
        return;
    }
    session->internal_refs--;
    if (session->internal_refs == 0U && session->release_pending &&
        qm_sem_is_valid(&session->internal_ref_sem)) {
        (void)qm_sem_signal(&session->internal_ref_sem);
    }
}

static void session_unpin(qm_audio_session_t *session)
{
    qm_audio_service_t *service;

    if (session == NULL || session->service == NULL) {
        return;
    }
    service = session->service;
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    session_unpin_locked(session);
    (void)qm_mutex_unlock(&service->lock);
}

/**
 * @brief Select the next eligible session in admission order. / 按准入顺序选择下一个符合条件的会话。
 *
 * The table preserves admission order. Return expired entries first so their
 * terminal events can be emitted outside the Service lock. */
static qm_audio_session_t *find_queued(qm_audio_service_t *service)
{
    qm_audio_session_t *best = NULL;
    uint32_t i;

    for (i = 0U; i < service->session_count; ++i) {
        qm_audio_session_t *session = service->sessions[i];
        if (session == NULL || session->state != QM_AUDIO_SESSION_QUEUED) {
            continue;
        }
        if (session->acquire_deadline_valid &&
            qm_utils_time_is_expired(&session->acquire_deadline)) {
            return session;
        }
        if (best == NULL || session->request.priority > best->request.priority) {
            best = session;
        }
    }
    return best;
}

static void stream_put(qm_audio_session_t *session)
{
    qm_audio_service_t *service = session->service;

    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (session->stream_users != 0U) {
        session->stream_users--;
    }
    if (session->stream_users == 0U) {
        signal_action_locked(session);
    }
    (void)qm_mutex_unlock(&service->lock);
    qm_audio_event_wake(service->engine->audio);
}

/* Both admission shells register the same START; only the caller's wait differs. */
static void activate_queued(qm_audio_service_t *service, qm_bool wait)
{
    qm_audio_session_t *session = NULL;
    service_notice_t notice = {0};
    int ret = QM_EOK;

    for (;;) {
        (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
        if (service->active != NULL || service->shutting_down) {
            (void)qm_mutex_unlock(&service->lock);
            return;
        }
        session = find_queued(service);
        if (session == NULL) {
            (void)qm_mutex_unlock(&service->lock);
            return;
        }
        if (expire_locked(service, session, &notice)) {
            (void)qm_mutex_unlock(&service->lock);
            deliver_notice(service, &notice);
            continue;
        }
        if (!session_pin_locked(service, session)) {
            (void)qm_mutex_unlock(&service->lock);
            return;
        }
        ret = request_action_locked(session, QM_AUDIO_SERVICE_ACTION_START, 0U);
        (void)qm_mutex_unlock(&service->lock);
        if (ret == QM_EOK && wait) {
            ret = wait_action(session, QM_AUDIO_SERVICE_ACTION_START,
                              CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
        }
        session_unpin(session);
        qm_audio_event_wake(service->engine->audio);
        if (!wait || ret == QM_EOK || ret == -QM_ETIMEOUT || ret == -QM_EBUSY) {
            return;
        }
    }
}

int qm_audio_service_create(const qm_audio_service_config_t *config, qm_audio_service_t **out_service)
{
    qm_audio_service_t *service = NULL;
    uint32_t capacity = 0U;
    int ret = QM_EOK;

    if (out_service == NULL) {
        return -QM_EINVAL;
    }
    *out_service = NULL;
    if (config == NULL || config->engine == NULL) {
        return -QM_EINVAL;
    }
    capacity = config->max_sessions != 0U ? config->max_sessions :
               CONFIG_QM_AUDIO_SERVICE_MAX_SESSIONS;
    service = (qm_audio_service_t *)qm_calloc(1U, sizeof(*service));
    if (service == NULL) {
        return -QM_ENOMEM;
    }
    service->sessions = (qm_audio_session_t **)qm_calloc(capacity,
                                                          sizeof(*service->sessions));
    if (service->sessions == NULL) {
        ret = -QM_ENOMEM;
        goto fail;
    }
    ret = qm_mutex_new(&service->lock);
    if (ret != QM_EOK) {
        ret = -QM_EIO;
        goto fail;
    }
    ret = qm_sem_new(&service->event_idle_sem, 0U);
    if (ret != QM_EOK) {
        ret = -QM_EIO;
        goto fail;
    }
    service->engine = config->engine;
    service->max_sessions = capacity;
    /* First Control gets base 1 << 16; 0 is the invalid request-id sentinel. */
    service->next_request_id_base = 1U;
    ret = qm_audio_engine_claim_owner(config->engine, service);
    if (ret != QM_EOK) {
        goto fail;
    }
    *out_service = service;
    return QM_EOK;

fail:
    if (qm_sem_is_valid(&service->event_idle_sem)) {
        (void)qm_sem_free(&service->event_idle_sem);
    }
    if (qm_mutex_is_valid(&service->lock)) {
        (void)qm_mutex_free(&service->lock);
    }
    qm_free(service->sessions);
    qm_free(service);
    return ret;
}

int qm_audio_service_shutdown(qm_audio_service_t *service)
{
    int ret = QM_EOK;

    if (service == NULL || !qm_mutex_is_valid(&service->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    service->shutting_down = QM_TRUE;
    (void)qm_mutex_unlock(&service->lock);
    for (;;) {
        qm_audio_session_t *session = NULL;
        uint32_t i;

        (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
        for (i = 0U; i < service->session_count; ++i) {
            qm_audio_session_t *candidate = service->sessions[i];

            /* Terminal state does not prove that a failed stop released the
             * Engine route.  Keep shutdown as the quiescence barrier by
             * retrying a close for any session that still owns, opens, or
             * closes a stream. */
            if (candidate != NULL &&
                (!qm_audio_session_terminal(candidate->state) || service->active == candidate ||
                 candidate->stream != NULL || candidate->stream_closing ||
                 candidate->action_pending)) {
                if (!session_pin_locked(service, candidate)) {
                    ret = -QM_EBUSY;
                } else {
                    session = candidate;
                }
                break;
            }
        }
        (void)qm_mutex_unlock(&service->lock);
        if (ret != QM_EOK) {
            return ret;
        }
        if (session == NULL) {
            break;
        }
        ret = qm_audio_service_session_stop(session, 0U);
        session_unpin(session);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    return QM_EOK;
}

int qm_audio_service_destroy(qm_audio_service_t *service)
{
    if (service == NULL || !qm_mutex_is_valid(&service->lock)) {
        return -QM_EINVAL;
    }
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (!service->shutting_down || service->session_count != 0U ||
        service->event_inflight != 0U) {
        (void)qm_mutex_unlock(&service->lock);
        return -QM_EBUSY;
    }
    (void)qm_mutex_unlock(&service->lock);
    qm_audio_engine_release_owner(service->engine, service);
    if (qm_sem_is_valid(&service->event_idle_sem)) {
        (void)qm_sem_free(&service->event_idle_sem);
    }
    (void)qm_mutex_free(&service->lock);
    qm_free(service->sessions);
    qm_free(service);
    return QM_EOK;
}

/**
 * @brief Admit, queue, reject, or preempt a playback request. / 准入、排队、拒绝或抢占一个播放请求。
 *
 * A free route runs the request immediately;
 * otherwise the request is rejected, queued, or used to preempt the current
 * owner according to its admission policy, and the winning session's Engine
 * stream is opened before returning.
 */
static int acquire_session(qm_audio_service_t *service, const qm_audio_session_request_t *request, qm_audio_event_cb_t event_callback, void *event_context, qm_audio_session_t **out_session, qm_bool asynchronous)
{
    qm_audio_session_t *session = NULL;
    qm_audio_session_t *victim = NULL;
    service_notice_t notice = {0};
    service_notice_t session_notice = {0};
    qm_bool activate = QM_FALSE;
    int ret = QM_EOK;
    uint32_t i = 0U;

    if (out_session == NULL) {
        return -QM_EINVAL;
    }
    *out_session = NULL;
    if (service == NULL || request == NULL || request->request_id == 0U ||
        request->priority > QM_AUDIO_PRIORITY_CRITICAL ||
        request->policy > QM_AUDIO_ADMISSION_PREEMPT_CURRENT ||
        request->delivery > QM_AUDIO_DELIVERY_REALTIME ||
        (request->acquire_timeout_ms != QM_WAIT_FOREVER &&
         request->acquire_timeout_ms >= UINT32_MAX / 2U)) {
        return -QM_EINVAL;
    }
    if (!asynchronous && !can_wait(service)) {
        return -QM_EBUSY;
    }
    session = (qm_audio_session_t *)qm_calloc(1U, sizeof(*session));
    if (session == NULL) {
        return -QM_ENOMEM;
    }
    session->service = service;
    session->request = *request;
    /* Every notice this session raises is routed to its owning Control. */
    session->event_callback = event_callback;
    session->event_context = event_context;
    session->state = QM_AUDIO_SESSION_NEW;
    session->generation = 1U;
    session->action_cookie = 1U;
    if (request->acquire_timeout_ms != 0U &&
        request->acquire_timeout_ms != QM_WAIT_FOREVER) {
        session->acquire_deadline_valid = QM_TRUE;
        qm_utils_time_countdown_ms(&session->acquire_deadline,
                                   request->acquire_timeout_ms);
    }
    ret = qm_sem_new(&session->state_sem, 0);
    if (ret != QM_EOK) {
        qm_free(session);
        return -QM_EIO;
    }
    ret = qm_sem_new(&session->action_sem, 0);
    if (ret != QM_EOK) {
        qm_sem_free(&session->state_sem);
        qm_free(session);
        return -QM_EIO;
    }
    ret = qm_sem_new(&session->internal_ref_sem, 0);
    if (ret != QM_EOK) {
        qm_sem_free(&session->action_sem);
        qm_sem_free(&session->state_sem);
        qm_free(session);
        return -QM_EIO;
    }
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (service->shutting_down || service->session_count >= service->max_sessions) {
        (void)qm_mutex_unlock(&service->lock);
        qm_sem_free(&session->state_sem);
        qm_sem_free(&session->action_sem);
        qm_sem_free(&session->internal_ref_sem);
        qm_free(session);
        return service->shutting_down ? -QM_EBUSY : -QM_EFULL;
    }
    for (i = 0U; i < service->session_count; ++i) {
        if (service->sessions[i]->request.request_id == request->request_id) {
            (void)qm_mutex_unlock(&service->lock);
            qm_sem_free(&session->state_sem);
            qm_sem_free(&session->action_sem);
            qm_sem_free(&session->internal_ref_sem);
            qm_free(session);
            return -QM_EINVAL;
        }
    }
    service->sessions[service->session_count++] = session;
    session->state = QM_AUDIO_SESSION_QUEUED;
    if (service->active == NULL) {
        activate = QM_TRUE;
    } else if (request->policy == QM_AUDIO_ADMISSION_REJECT_IF_BUSY ||
               (request->policy == QM_AUDIO_ADMISSION_QUEUE &&
                request->acquire_timeout_ms == 0U)) {
        service->sessions[--service->session_count] = NULL;
        (void)qm_mutex_unlock(&service->lock);
        qm_sem_free(&session->state_sem);
        qm_sem_free(&session->action_sem);
        qm_sem_free(&session->internal_ref_sem);
        qm_free(session);
        return -QM_EBUSY;
    } else if ((request->policy == QM_AUDIO_ADMISSION_PREEMPT_CURRENT) ||
               (request->policy == QM_AUDIO_ADMISSION_PREEMPT_LOWER &&
                request->priority > service->active->request.priority)) {
        victim = service->active;
        if (!session_pin_locked(service, victim)) {
            victim = NULL;
        }
    }
    prepare_notice_locked(service, session, QM_AUDIO_EVENT_ACCEPTED,
                          QM_EOK, 0U, &notice);
    (void)qm_mutex_unlock(&service->lock);
    deliver_notice(service, &notice);

    if (victim != NULL) {
        (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
        session->preempting = victim;
        ret = victim->release_pending ? -QM_EBUSY :
              request_action_locked(victim, QM_AUDIO_SERVICE_ACTION_PREEMPT, 0U);
        if (ret == QM_EOK) {
            session->preempt_cookie = victim->action_cookie;
        }
        if (ret != QM_EOK) {
            session->preempting = NULL;
            session->preempt_cookie = 0U;
            session->state = QM_AUDIO_SESSION_ERROR;
            prepare_notice_locked(service, session, QM_AUDIO_EVENT_ERROR,
                                  ret, 0U, &session_notice);
            signal_state(session);
        }
        (void)qm_mutex_unlock(&service->lock);
        deliver_notice(service, &session_notice);
        if (ret == QM_EOK && !asynchronous) {
            ret = wait_action(victim, QM_AUDIO_SERVICE_ACTION_PREEMPT,
                              CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
            activate = ret == QM_EOK;
        }
        session_unpin(victim);
    }
    if (activate) {
        activate_queued(service, !asynchronous);
    }
    *out_session = session;
    qm_audio_event_wake(service->engine->audio);
    return QM_EOK;
}

int qm_audio_service_session_acquire(qm_audio_service_t *service, const qm_audio_session_request_t *request, qm_audio_event_cb_t callback, void *context, qm_audio_session_t **out_session)
{
    return acquire_session(service, request, callback, context, out_session, QM_FALSE);
}

int qm_audio_service_session_acquire_async(qm_audio_service_t *service, const qm_audio_session_request_t *request, qm_audio_event_cb_t callback, void *context, qm_audio_session_t **out_session)
{
    return acquire_session(service, request, callback, context, out_session, QM_TRUE);
}

int qm_audio_service_session_wait(qm_audio_session_t *session, uint32_t timeout_ms)
{
    qm_utils_time_t deadline = {0};
    qm_audio_service_t *service = NULL;
    qm_audio_session_t *pending = NULL;
    qm_audio_service_action_t action = QM_AUDIO_SERVICE_ACTION_NONE;
    qm_audio_session_state_t state = QM_AUDIO_SESSION_NEW;
    service_notice_t notice = {0};
    uint32_t left = 0U;
    uint32_t admission_left = 0U;
    qm_bool expired = QM_FALSE;
    int result = QM_EOK;
    int ret = QM_EOK;

    if (session == NULL || !qm_sem_is_valid(&session->state_sem)) {
        return -QM_EINVAL;
    }
    service = session->service;
    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    for (;;) {
        activate_queued(service, QM_FALSE);
        pending = NULL;
        (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
        expired = expire_locked(service, session, &notice);
        state = session->state;
        result = session->result;
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (state == QM_AUDIO_SESSION_QUEUED && session->acquire_deadline_valid) {
            admission_left = qm_utils_time_left(&session->acquire_deadline);
            if (admission_left < left) {
                left = admission_left;
            }
        }
        if (state == QM_AUDIO_SESSION_QUEUED) {
            pending = session->preempting != NULL ? session->preempting : session;
            if (!pending->action_pending || !session_pin_locked(service, pending)) {
                pending = NULL;
            } else {
                action = pending->action;
            }
        }
        (void)qm_mutex_unlock(&service->lock);
        if (expired) {
            deliver_notice(service, &notice);
        }
        if (pending != NULL) {
            ret = wait_action(pending, action, left);
            session_unpin(pending);
            if (ret == -QM_ETIMEOUT) {
                (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
                expired = expire_locked(service, session, &notice);
                (void)qm_mutex_unlock(&service->lock);
                if (expired) {
                    deliver_notice(service, &notice);
                }
                return ret;
            }
            if (ret == -QM_EBUSY) {
                return ret;
            }
            continue;
        }
        if (state == QM_AUDIO_SESSION_CANCELLED || state == QM_AUDIO_SESSION_PREEMPTED) {
            return result != QM_EOK ? result : -QM_EINTR;
        }
        if (state == QM_AUDIO_SESSION_ERROR) {
            return result != QM_EOK ? result : -QM_EIO;
        }
        if (state != QM_AUDIO_SESSION_QUEUED && state != QM_AUDIO_SESSION_NEW) {
            return QM_EOK;
        }
        if (left == 0U) {
            return -QM_ETIMEOUT;
        }
        if (!can_wait(service)) {
            return -QM_EBUSY;
        }
        ret = qm_sem_wait(&session->state_sem, left);
        if (ret != QM_EOK && ret != -QM_ETIMEOUT) {
            return ret;
        }
    }
}

int qm_audio_service_session_cancel(qm_audio_session_t *session)
{
    if (session == NULL || session->service == NULL) {
        return -QM_EINVAL;
    }
    return qm_audio_service_session_stop(session, 0U);
}

int qm_audio_service_session_pause(qm_audio_session_t *session)
{
    return run_action(session, QM_AUDIO_SERVICE_ACTION_PAUSE, 0U);
}

int qm_audio_service_session_suspend(qm_audio_session_t *session)
{
    return run_action(session, QM_AUDIO_SERVICE_ACTION_SUSPEND, 0U);
}

int qm_audio_service_session_resume(qm_audio_session_t *session)
{
    return run_action(session, QM_AUDIO_SERVICE_ACTION_RESUME, 0U);
}

int qm_audio_service_session_bind(qm_audio_session_t *session, qm_audio_render_stream_t **stream)
{
    int ret = -QM_EBUSY;
    if (session == NULL || stream == NULL) return -QM_EINVAL;
    *stream = NULL;
    (void)qm_mutex_lock(&session->service->lock, QM_WAIT_FOREVER);
    if (session->state == QM_AUDIO_SESSION_ACTIVE && !session->stream_closing &&
        session->stream != NULL) {
        ret = qm_audio_engine_render_retain(session->stream);
        if (ret == QM_EOK) *stream = session->stream;
    }
    (void)qm_mutex_unlock(&session->service->lock);
    return ret;
}

int qm_audio_service_session_finish(qm_audio_session_t *session, uint32_t timeout_ms)
{
    qm_audio_service_t *service = NULL;
    qm_audio_render_stream_t *stream = NULL;
    int ret = QM_EOK;
    int drain_result = QM_EOK;

    if (session == NULL || session->service == NULL) {
        return -QM_EINVAL;
    }
    service = session->service;
    if (!can_wait(service)) {
        return -QM_EBUSY;
    }
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (session->action_pending && session->action == QM_AUDIO_SERVICE_ACTION_FINISH) {
        (void)qm_mutex_unlock(&service->lock);
        return run_action(session, QM_AUDIO_SERVICE_ACTION_FINISH, 0U);
    }
    if (session->release_pending || session->action_pending ||
        (session->state != QM_AUDIO_SESSION_ACTIVE &&
         session->state != QM_AUDIO_SESSION_DRAINING) ||
        session->stream == NULL || session->stream_closing) {
        ret = session->stream_closing ? -QM_EINTR : -QM_EBUSY;
        (void)qm_mutex_unlock(&service->lock);
        return ret;
    }
    stream = session->stream;
    session->stream_users++;
    session->state = QM_AUDIO_SESSION_DRAINING;
    (void)qm_mutex_unlock(&service->lock);

    drain_result = qm_audio_engine_render_drain(stream, timeout_ms);

    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (session->stream_closing || session->state != QM_AUDIO_SESSION_DRAINING) {
        ret = -QM_EINTR;
    } else if (drain_result == -QM_ETIMEOUT) {
        ret = drain_result;
    } else {
        ret = request_action_locked(session, QM_AUDIO_SERVICE_ACTION_FINISH, 0U);
        if (ret == QM_EOK) {
            session->finish_result = drain_result;
        }
    }
    (void)qm_mutex_unlock(&service->lock);
    stream_put(session);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = wait_action(session, QM_AUDIO_SERVICE_ACTION_FINISH,
                      CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
    activate_queued(service, QM_TRUE);
    return ret;
}

int qm_audio_service_session_stop(qm_audio_session_t *session, uint32_t reason)
{
    return run_action(session, QM_AUDIO_SERVICE_ACTION_STOP, reason);
}

int qm_audio_service_session_get_state(const qm_audio_session_t *session, qm_audio_session_state_t *state)
{
    qm_audio_service_t *service;

    if (session == NULL || session->service == NULL || state == NULL) {
        return -QM_EINVAL;
    }
    service = session->service;
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    *state = session->state;
    (void)qm_mutex_unlock(&service->lock);
    return QM_EOK;
}

int qm_audio_service_session_get_position(qm_audio_session_t *session, uint32_t *position_ms)
{
    qm_audio_session_t *mutable_session = (qm_audio_session_t *)session;
    qm_audio_service_t *service = NULL;
    qm_audio_render_stream_t *stream = NULL;
    uint32_t relative = 0U;
    int ret = QM_EOK;

    if (session == NULL || session->service == NULL || position_ms == NULL) {
        return -QM_EINVAL;
    }
    service = session->service;
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    /* A paused/terminal session must never observe the next owner's clock. */
    if (session->origin_pending || session->state == QM_AUDIO_SESSION_PAUSED || qm_audio_session_terminal(session->state) ||
        session->stream_closing || session->stream == NULL) {
        *position_ms = session->cached_position_ms;
        ret = session->position_valid ? QM_EOK : -QM_ENOSYS;
        (void)qm_mutex_unlock(&service->lock);
        return ret;
    }
    stream = session->stream;
    mutable_session->stream_users++;
    (void)qm_mutex_unlock(&service->lock);
    ret = qm_audio_engine_render_get_position(stream, &relative);
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (ret == QM_EOK) {
        cache_position(mutable_session, relative);
        *position_ms = session->cached_position_ms;
    }
    (void)qm_mutex_unlock(&service->lock);
    stream_put(mutable_session);
    return ret;
}

int qm_audio_service_session_set_origin(qm_audio_session_t *session, uint32_t position_ms)
{
    qm_audio_service_t *service = session->service;
    int ret = QM_EOK;
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (qm_audio_session_terminal(session->state) ||
        session->stream_closing) ret = -QM_EBUSY;
    else {
        session->origin_pending = QM_FALSE;
        session->position_base_ms = position_ms;
        session->cached_position_ms = position_ms;
        session->position_valid = QM_TRUE;
    }
    (void)qm_mutex_unlock(&service->lock);
    return ret;
}

int qm_audio_service_session_reposition(qm_audio_session_t *session, uint32_t position_ms)
{
    return run_action(session, QM_AUDIO_SERVICE_ACTION_REPOSITION, position_ms);
}

static int request_action_locked(qm_audio_session_t *session, qm_audio_service_action_t action, uint32_t value)
{
    qm_audio_service_t *service = NULL;
    qm_bool valid = QM_FALSE;

    service = session->service;
    if (session->action_pending) {
        if (session->action == action) {
            session->action_value = value;
            return QM_EOK;
        }
        if (action != QM_AUDIO_SERVICE_ACTION_STOP && action != QM_AUDIO_SERVICE_ACTION_PREEMPT) {
            return -QM_EBUSY;
        }
    }
    valid = action == QM_AUDIO_SERVICE_ACTION_STOP || action == QM_AUDIO_SERVICE_ACTION_PREEMPT;
    if (action == QM_AUDIO_SERVICE_ACTION_START) {
        valid = !service->shutting_down && service->active == NULL &&
                session->state == QM_AUDIO_SESSION_QUEUED;
    } else if (action == QM_AUDIO_SERVICE_ACTION_RESUME) {
        valid = !service->shutting_down &&
                (service->active == NULL || service->active == session) &&
                session->state == QM_AUDIO_SESSION_PAUSED && !session->stream_closing;
    } else if (action == QM_AUDIO_SERVICE_ACTION_PAUSE) {
        valid = service->active == session && session->state == QM_AUDIO_SESSION_ACTIVE &&
                session->stream != NULL && !session->stream_closing;
    } else if (action == QM_AUDIO_SERVICE_ACTION_SUSPEND) {
        valid = (service->active == session &&
                 (session->state == QM_AUDIO_SESSION_ACTIVE || session->state == QM_AUDIO_SESSION_PAUSED)) ||
                (session->state == QM_AUDIO_SESSION_PAUSED && session->stream == NULL);
    } else if (action == QM_AUDIO_SERVICE_ACTION_REPOSITION) {
        valid = !qm_audio_session_terminal(session->state) &&
                (service->active == session ||
                 (session->state == QM_AUDIO_SESSION_PAUSED && session->stream == NULL));
    } else if (action == QM_AUDIO_SERVICE_ACTION_FINISH) {
        valid = service->active == session && !session->stream_closing &&
                (session->state == QM_AUDIO_SESSION_ACTIVE || session->state == QM_AUDIO_SESSION_DRAINING);
    }
    if (!valid) {
        return -QM_EBUSY;
    }
    if (!session->action_pending && action == QM_AUDIO_SERVICE_ACTION_SUSPEND &&
        session->state == QM_AUDIO_SESSION_PAUSED && session->stream == NULL &&
        service->active != session) {
        session->action = action;
        session->action_result = QM_EOK;
        return QM_EOK;
    }
    if (!session->action_pending) {
        session->internal_refs++;
    }
    session->action = action;
    session->action_pending = QM_TRUE;
    session->action_submitted = QM_FALSE;
    session->action_open = (action == QM_AUDIO_SERVICE_ACTION_START ||
                           action == QM_AUDIO_SERVICE_ACTION_RESUME) && session->stream == NULL;
    session->action_value = value;
    session->action_result = QM_EOK;
    if (action == QM_AUDIO_SERVICE_ACTION_START || action == QM_AUDIO_SERVICE_ACTION_RESUME) {
        service->active = session;
        if (action == QM_AUDIO_SERVICE_ACTION_START) {
            session->acquire_deadline_valid = QM_FALSE;
            if (++session->generation == QM_AUDIO_INVALID_GENERATION) {
                session->generation++;
            }
        }
    } else if (action != QM_AUDIO_SERVICE_ACTION_PAUSE) {
        session->stream_closing = QM_TRUE;
    }
    if (action == QM_AUDIO_SERVICE_ACTION_FINISH) {
        session->finish_result = QM_EOK;
    }
    signal_action_locked(session);
    return QM_EOK;
}

int qm_audio_service_session_request(qm_audio_session_t *session, qm_audio_service_action_t action, uint32_t value)
{
    qm_audio_service_t *service = NULL;
    int ret = QM_EOK;

    if (session == NULL || session->service == NULL ||
        action <= QM_AUDIO_SERVICE_ACTION_NONE || action > QM_AUDIO_SERVICE_ACTION_FINISH) {
        return -QM_EINVAL;
    }
    service = session->service;
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    ret = session->release_pending ? -QM_EBUSY :
          request_action_locked(session, action, value);
    (void)qm_mutex_unlock(&service->lock);
    qm_audio_event_wake(service->engine->audio);
    return ret;
}

static int advance_action(qm_audio_session_t *session, qm_audio_service_action_t action, int *result)
{
    qm_audio_service_t *service = NULL;
    qm_audio_render_stream_t *stream = NULL;
    qm_audio_render_action_t render_action = QM_AUDIO_RENDER_ACTION_START;
    qm_audio_render_config_t config = {0};
    service_notice_t notice = {0};
    service_notice_t dependent = {0};
    qm_audio_session_t *queued = NULL;
    qm_bool closing = QM_FALSE;
    qm_bool wake = QM_FALSE;
    uint32_t position = 0U;
    uint32_t completed_cookie = 0U;
    uint32_t i = 0U;
    int status = QM_EOK;
    int ret = QM_EOK;

    if (session == NULL || session->service == NULL || result == NULL) {
        return -QM_EINVAL;
    }
    service = session->service;
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    if (session->action != action || action == QM_AUDIO_SERVICE_ACTION_NONE) {
        (void)qm_mutex_unlock(&service->lock);
        return -QM_EINTR;
    }
    if (!session->action_pending) {
        *result = session->action_result;
        (void)qm_mutex_unlock(&service->lock);
        return QM_EOK;
    }
    if (session->action_submitting) {
        (void)qm_mutex_unlock(&service->lock);
        return -QM_EBUSY;
    }
    stream = session->stream;
    closing = action == QM_AUDIO_SERVICE_ACTION_SUSPEND || action == QM_AUDIO_SERVICE_ACTION_REPOSITION ||
              action == QM_AUDIO_SERVICE_ACTION_STOP || action == QM_AUDIO_SERVICE_ACTION_PREEMPT ||
              action == QM_AUDIO_SERVICE_ACTION_FINISH;
    render_action = closing ? QM_AUDIO_RENDER_ACTION_STOP :
        action == QM_AUDIO_SERVICE_ACTION_PAUSE ? QM_AUDIO_RENDER_ACTION_PAUSE :
        session->action_open ? QM_AUDIO_RENDER_ACTION_START : QM_AUDIO_RENDER_ACTION_RESUME;
    /* The action pin owns the session until all copied notices return.
     * Engine calls here submit or observe work; none waits for the worker. */
    if (!session->action_submitted && session->action_open) {
        config.delivery = session->request.delivery;
        ret = qm_audio_engine_render_open_request(service->engine, service, &config, &stream);
        if (ret == QM_EOK) {
            session->stream = stream;
            session->action_submitted = QM_TRUE;
            (void)qm_audio_engine_render_set_service_progress(stream, &session->action_sem);
        }
    } else if (stream != NULL && !session->action_submitted) {
        if (render_action == QM_AUDIO_RENDER_ACTION_STOP) {
            /* The action pin and submitting flag retain the stream while abort
             * runs without blocking unrelated Service operations. */
            session->action_submitting = QM_TRUE;
            (void)qm_mutex_unlock(&service->lock);
            ret = qm_audio_engine_render_request(stream, render_action);
            (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
            session->action_submitting = QM_FALSE;
            signal_action_locked(session);
            wake = QM_TRUE;
            if (session->action != action) {
                (void)qm_mutex_unlock(&service->lock);
                qm_audio_event_wake(service->engine->audio);
                return -QM_EINTR;
            }
        } else {
            ret = qm_audio_engine_render_request(stream, render_action);
        }
        if (ret == QM_EOK) {
            session->action_submitted = QM_TRUE;
        }
    }
    if (ret == QM_EOK && stream != NULL) {
        status = qm_audio_engine_render_result(stream, render_action, &ret);
        if (status == -QM_EBUSY) {
            (void)qm_mutex_unlock(&service->lock);
            if (wake) {
                qm_audio_event_wake(service->engine->audio);
            }
            return -QM_EBUSY;
        }
        if (status != QM_EOK) {
            ret = status;
        }
    }
    if (ret == QM_EOK && closing && session->stream_users != 0U) {
        (void)qm_mutex_unlock(&service->lock);
        if (wake) {
            qm_audio_event_wake(service->engine->audio);
        }
        return -QM_EBUSY;
    }
    if (ret == QM_EOK && stream != NULL) {
        (void)submitted_position(session);
        if (qm_audio_engine_render_cached_position(stream, &position) == QM_EOK &&
            session->state != QM_AUDIO_SESSION_FINISHED) {
            cache_position(session, position);
        }
        if (action == QM_AUDIO_SERVICE_ACTION_FINISH &&
            !qm_audio_encoded_bytes(&service->engine->output_format)) {
            session->cached_position_ms = submitted_position(session);
            session->position_valid = session->submitted_valid;
        }
        if (closing) {
            ret = qm_audio_engine_render_close(stream);
            if (ret == -QM_EBUSY) {
                (void)qm_mutex_unlock(&service->lock);
                if (wake) {
                    qm_audio_event_wake(service->engine->audio);
                }
                return -QM_EBUSY;
            }
            if (ret == QM_EOK) {
                session->stream = NULL;
            }
        }
    }
    if (ret == QM_EOK && closing) {
        session->stream_closing = QM_FALSE;
        if (action == QM_AUDIO_SERVICE_ACTION_FINISH) {
            ret = session->finish_result;
        }
    }
    if (ret == QM_EOK) {
        if (action == QM_AUDIO_SERVICE_ACTION_START || action == QM_AUDIO_SERVICE_ACTION_RESUME) {
            session->state = QM_AUDIO_SESSION_ACTIVE;
            prepare_notice_locked(service, session, action == QM_AUDIO_SERVICE_ACTION_START ?
                QM_AUDIO_EVENT_STARTED : QM_AUDIO_EVENT_RESUMED, QM_EOK, 0U, &notice);
        } else if (action == QM_AUDIO_SERVICE_ACTION_REPOSITION) {
            session->position_base_ms = session->cached_position_ms = session->action_value;
            session->origin_pending = QM_TRUE;
            session->position_valid = QM_FALSE;
            session->submitted_ms = 0U;
            session->submitted_valid = QM_FALSE;
            if (++session->generation == QM_AUDIO_INVALID_GENERATION) {
                session->generation++;
            }
            session->state = QM_AUDIO_SESSION_PAUSED;
        } else if (action == QM_AUDIO_SERVICE_ACTION_STOP || action == QM_AUDIO_SERVICE_ACTION_PREEMPT ||
                   action == QM_AUDIO_SERVICE_ACTION_FINISH) {
            if (service->active == session) {
                service->active = NULL;
            }
            if (!qm_audio_session_terminal(session->state)) {
                session->state = action == QM_AUDIO_SERVICE_ACTION_PREEMPT ?
                    QM_AUDIO_SESSION_PREEMPTED : action == QM_AUDIO_SERVICE_ACTION_FINISH ?
                    QM_AUDIO_SESSION_FINISHED : QM_AUDIO_SESSION_CANCELLED;
                if (action == QM_AUDIO_SERVICE_ACTION_PREEMPT &&
                    ++session->generation == QM_AUDIO_INVALID_GENERATION) {
                    session->generation++;
                }
                prepare_notice_locked(service, session, action == QM_AUDIO_SERVICE_ACTION_PREEMPT ?
                    QM_AUDIO_EVENT_PREEMPTED : action == QM_AUDIO_SERVICE_ACTION_FINISH ?
                    QM_AUDIO_EVENT_FINISHED : QM_AUDIO_EVENT_CANCELLED,
                                      QM_EOK, session->action_value, &notice);
            }
        } else {
            if (action == QM_AUDIO_SERVICE_ACTION_SUSPEND) {
                if (service->active == session) {
                    service->active = NULL;
                }
                session->continuation_position_ms = submitted_position(session);
                session->position_base_ms = session->cached_position_ms;
                session->submitted_ms = 0U;
                session->submitted_valid = QM_FALSE;
            }
            session->state = QM_AUDIO_SESSION_PAUSED;
            prepare_notice_locked(service, session, QM_AUDIO_EVENT_PAUSED,
                                  QM_EOK, 0U, &notice);
        }
    } else if ((action == QM_AUDIO_SERVICE_ACTION_STOP || action == QM_AUDIO_SERVICE_ACTION_PREEMPT ||
                action == QM_AUDIO_SERVICE_ACTION_FINISH ||
                action == QM_AUDIO_SERVICE_ACTION_START || action == QM_AUDIO_SERVICE_ACTION_RESUME) &&
               !qm_audio_session_terminal(session->state)) {
        session->state = QM_AUDIO_SESSION_ERROR;
        prepare_notice_locked(service, session, QM_AUDIO_EVENT_ERROR,
                              ret, session->action_value, &notice);
    }
    if (ret != QM_EOK && session->stream == NULL && service->active == session) {
        service->active = NULL;
    }
    session->action_result = ret;
    session->action_pending = QM_FALSE;
    if (closing) {
        completed_cookie = session->action_cookie;
        if (++session->action_cookie == 0U) {
            session->action_cookie++;
        }
        /* Resolve the old dependencies before notices permit reentrant admission.
         * Failed dependents stay pinned until their copied notice is delivered.
         * / 通知允许重入准入前先结算旧依赖；失败依赖保活到复制通知交付完成。 */
        for (i = 0U; i < service->session_count; ++i) {
            queued = service->sessions[i];
            if (queued == NULL || queued->preempting != session ||
                queued->preempt_cookie != completed_cookie) {
                continue;
            }
            if (ret == QM_EOK) {
                queued->preempting = NULL;
                queued->preempt_cookie = 0U;
            } else if (session_pin_locked(service, queued)) {
                if (queued->state == QM_AUDIO_SESSION_QUEUED) {
                    queued->state = QM_AUDIO_SESSION_ERROR;
                    queued->result = ret;
                    signal_state(queued);
                }
            } else {
                queued->preempting = NULL;
                queued->preempt_cookie = 0U;
            }
        }
    }
    signal_state(session);
    signal_action_locked(session);
    *result = ret;
    (void)qm_mutex_unlock(&service->lock);
    deliver_notice(service, &notice);
    /* The cookie excludes admissions made during another notice's callback.
     * / cookie 排除其他通知回调期间新建立的准入依赖。 */
    if (closing) {
        for (;;) {
            dependent = (service_notice_t){0};
            queued = NULL;

            (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
            for (i = 0U; i < service->session_count; ++i) {
                if (service->sessions[i]->preempting == session &&
                    service->sessions[i]->preempt_cookie == completed_cookie) {
                    queued = service->sessions[i];
                    break;
                }
            }
            if (queued != NULL) {
                queued->preempting = NULL;
                queued->preempt_cookie = 0U;
                if (ret != QM_EOK && queued->state == QM_AUDIO_SESSION_ERROR) {
                    prepare_notice_locked(service, queued, QM_AUDIO_EVENT_ERROR,
                                          ret, 0U, &dependent);
                }
            }
            (void)qm_mutex_unlock(&service->lock);
            if (queued == NULL) {
                break;
            }
            deliver_notice(service, &dependent);
            session_unpin(queued);
        }
    }
    session_unpin(session);
    qm_audio_event_wake(service->engine->audio);
    return QM_EOK;
}

int qm_audio_service_session_result(qm_audio_session_t *session, qm_audio_service_action_t action, int *result)
{
    return advance_action(session, action, result);
}

static int wait_action(qm_audio_session_t *session, qm_audio_service_action_t action, uint32_t timeout_ms)
{
    qm_audio_service_t *service = session->service;
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    qm_bool expired = QM_FALSE;
    int result = QM_EOK;
    int ret = QM_EOK;

    qm_utils_time_countdown_ms(&deadline, timeout_ms);
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    session->action_waiters++;
    session->internal_refs++;
    (void)qm_mutex_unlock(&service->lock);
    for (;;) {
        ret = advance_action(session, action, &result);
        if (ret == QM_EOK) {
            ret = result;
            break;
        }
        if (ret != -QM_EBUSY) {
            break;
        }
        left = timeout_ms == QM_WAIT_FOREVER ? QM_WAIT_FOREVER :
               qm_utils_time_left(&deadline);
        if (expired || left == 0U) {
            ret = -QM_ETIMEOUT;
            break;
        }
        if (!can_wait(service)) {
            ret = -QM_EBUSY;
            break;
        }
        expired = qm_sem_wait(&session->action_sem, left) != QM_EOK;
        /* Recheck durable state even when the wait reaches its deadline. */
    }
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    session->action_waiters--;
    if (session->action_pending && session->action_waiters != 0U) {
        (void)qm_sem_signal(&session->action_sem);
    }
    session_unpin_locked(session);
    (void)qm_mutex_unlock(&service->lock);
    return ret;
}

static int run_action(qm_audio_session_t *session, qm_audio_service_action_t action, uint32_t value)
{
    int ret = QM_EOK;

    if (session == NULL || session->service == NULL) {
        return -QM_EINVAL;
    }
    if (!can_wait(session->service)) {
        return -QM_EBUSY;
    }
    ret = qm_audio_service_session_request(session, action, value);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = wait_action(session, action, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
    activate_queued(session->service, QM_TRUE);
    return ret;
}

static void advance_actions(qm_audio_service_t *service)
{
    uint32_t i = 0U;
    qm_audio_session_t *session = NULL;
    qm_audio_service_action_t action = QM_AUDIO_SERVICE_ACTION_NONE;
    int result = QM_EOK;

    for (i = 0U; ; ++i) {
        session = NULL;
        action = QM_AUDIO_SERVICE_ACTION_NONE;
        result = QM_EOK;
        (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
        if (i >= service->session_count) {
            (void)qm_mutex_unlock(&service->lock);
            break;
        }
        session = service->sessions[i];
        action = session->action_pending ? session->action : QM_AUDIO_SERVICE_ACTION_NONE;
        if (action != QM_AUDIO_SERVICE_ACTION_NONE &&
            !session_pin_locked(service, session)) {
            action = QM_AUDIO_SERVICE_ACTION_NONE;
        }
        (void)qm_mutex_unlock(&service->lock);
        if (action != QM_AUDIO_SERVICE_ACTION_NONE) {
            (void)qm_audio_service_session_result(session, action, &result);
            session_unpin(session);
        }
    }
}

int qm_audio_service_session_release(qm_audio_session_t *session)
{
    qm_audio_service_t *service = NULL;
    qm_utils_time_t deadline = {0};
    uint32_t left = 0U;
    uint32_t i = 0U;
    int index = -1;
    int ret = QM_EOK;

    if (session == NULL || session->service == NULL) {
        return -QM_EINVAL;
    }
    service = session->service;
    qm_utils_time_countdown_ms(&deadline, CONFIG_QM_AUDIO_ENGINE_STOP_TIMEOUT_MS);
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    index = session_index(service, session);
    if (index < 0 || !qm_audio_session_terminal(session->state) ||
        session->release_pending) {
        (void)qm_mutex_unlock(&service->lock);
        return -QM_EBUSY;
    }
    session->release_pending = QM_TRUE;
    /* Release owns the gate; its private STOP uses the same core and pin. */
    ret = request_action_locked(session, QM_AUDIO_SERVICE_ACTION_STOP, 0U);
    (void)qm_mutex_unlock(&service->lock);
    if (ret == QM_EOK) {
        ret = wait_action(session, QM_AUDIO_SERVICE_ACTION_STOP,
                          qm_utils_time_left(&deadline));
    }
    if (ret != QM_EOK) {
        goto retain;
    }
    for (;;) {
        (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
        if (session->internal_refs == 0U) {
            break;
        }
        left = qm_utils_time_left(&deadline);
        (void)qm_mutex_unlock(&service->lock);
        if (left == 0U || !can_wait(service)) {
            ret = -QM_ETIMEOUT;
            goto retain;
        }
        if (qm_sem_wait(&session->internal_ref_sem, left) != QM_EOK) {
            (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
            ret = session->internal_refs == 0U ? QM_EOK : -QM_ETIMEOUT;
            (void)qm_mutex_unlock(&service->lock);
            if (ret != QM_EOK) {
                goto retain;
            }
        }
    }
    index = session_index(service, session);
    for (i = (uint32_t)index + 1U; i < service->session_count; ++i) {
        service->sessions[i - 1U] = service->sessions[i];
    }
    service->sessions[--service->session_count] = NULL;
    (void)qm_mutex_unlock(&service->lock);
    (void)qm_sem_free(&session->state_sem);
    (void)qm_sem_free(&session->action_sem);
    (void)qm_sem_free(&session->internal_ref_sem);
    qm_free(session);
    activate_queued(service, can_wait(service));
    qm_audio_event_wake(service->engine->audio);
    return QM_EOK;

retain:
    (void)qm_mutex_lock(&service->lock, QM_WAIT_FOREVER);
    session->release_pending = QM_FALSE;
    (void)qm_mutex_unlock(&service->lock);
    qm_audio_event_wake(service->engine->audio);
    return ret;
}
