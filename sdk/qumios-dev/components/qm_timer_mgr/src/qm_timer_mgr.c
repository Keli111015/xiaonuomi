#include "qm_timer_mgr.h"

#include "qm_kernel.h"
#include "qm_timer_mgr_cloud.h"

#ifndef CONFIG_QM_TIMER_MGR_DEFAULT_MODE
#define CONFIG_QM_TIMER_MGR_DEFAULT_MODE 0
#endif

#ifndef CONFIG_QM_TIMER_MGR_DEFAULT_DURATION_SEC
#define CONFIG_QM_TIMER_MGR_DEFAULT_DURATION_SEC 600
#endif

#ifndef CONFIG_QM_TIMER_MGR_TICK_MS
#define CONFIG_QM_TIMER_MGR_TICK_MS 1000
#endif

#define QM_TIMER_MGR_MAX_EVENTS_PER_STEP 3
#define QM_TIMER_MGR_MIN_TIMER_MS       100u
#define QM_TIMER_MGR_MAX_TIMER_MS       0x7fffffffu
#define QM_TIMER_MGR_MS_PER_SEC         1000u
#define QM_TIMER_MGR_DESTROY_WAIT_MS    10u
#define QM_TIMER_MGR_MAX_DURATION_SEC   (0xffffffffu / QM_TIMER_MGR_MS_PER_SEC)

struct qm_timer_mgr {
    qm_timer_mgr_config_t config;
    qm_timer_t timer;              /* kernel timer used as the wake-up source */
    qm_mutex_t lock;
    qm_bool timer_valid;
    qm_bool lock_valid;
    /* destroy marks the handle invalid for new work; active_ops keeps in-flight
     * API calls and timer callbacks alive until they exit. */
    qm_bool destroying;
    int active_ops;
    qm_timer_mgr_mode_t mode;
    qm_timer_mgr_state_t state;
    uint32_t duration_sec;
    uint32_t remaining_sec;
    uint32_t elapsed_sec;
    uint32_t last_used_duration_sec;
    uint32_t last_tick_ms;
    void *cloud_ctx;
};

/* enter/leave bracket every public API call and timer callback: enter() bumps
 * active_ops while refusing new work once destroying is set; destroy() then
 * waits for active_ops to drain before freeing the handle. */
static int qm_timer_mgr_enter(qm_timer_mgr_t timer)
{
    if (!timer) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    if (timer->destroying) {
        qm_mutex_unlock(&timer->lock);
        return -QM_EBUSY;
    }
    timer->active_ops++;
    qm_mutex_unlock(&timer->lock);
    return QM_EOK;
}

static void qm_timer_mgr_leave(qm_timer_mgr_t timer)
{
    if (!timer) {
        return;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    if (timer->active_ops > 0) {
        timer->active_ops--;
    }
    qm_mutex_unlock(&timer->lock);
}

static qm_bool qm_timer_mgr_has_active_ops(qm_timer_mgr_t timer)
{
    qm_bool active;

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    active = timer->active_ops > 0 ? QM_TRUE : QM_FALSE;
    qm_mutex_unlock(&timer->lock);
    return active;
}

static uint32_t qm_timer_mgr_default_u32(uint32_t value, uint32_t fallback)
{
    return value ? value : fallback;
}

static void qm_timer_mgr_normalize_config(qm_timer_mgr_config_t *dst,
                                          const qm_timer_mgr_config_t *src)
{
    if (src) {
        *dst = *src;
    } else {
        dst->mode = 0;
        dst->duration_sec = 0;
        dst->tick_ms = 0;
        dst->event_cb = NULL;
        dst->user_ctx = NULL;
        dst->cloud = NULL;
    }

    if (dst->mode != QM_TIMER_MGR_MODE_COUNTDOWN &&
        dst->mode != QM_TIMER_MGR_MODE_STOPWATCH) {
        dst->mode = (qm_timer_mgr_mode_t)CONFIG_QM_TIMER_MGR_DEFAULT_MODE;
    }
    dst->duration_sec = qm_timer_mgr_default_u32(
        dst->duration_sec, CONFIG_QM_TIMER_MGR_DEFAULT_DURATION_SEC);
    dst->tick_ms = qm_timer_mgr_default_u32(dst->tick_ms,
                                            CONFIG_QM_TIMER_MGR_TICK_MS);
}

static qm_bool qm_timer_mgr_config_valid(const qm_timer_mgr_config_t *config)
{
    if (!config ||
        (config->mode != QM_TIMER_MGR_MODE_COUNTDOWN &&
         config->mode != QM_TIMER_MGR_MODE_STOPWATCH) ||
        config->duration_sec == 0 ||
        config->duration_sec > QM_TIMER_MGR_MAX_DURATION_SEC ||
        config->tick_ms < QM_TIMER_MGR_MIN_TIMER_MS ||
        config->tick_ms > QM_TIMER_MGR_MAX_TIMER_MS) {
        return QM_FALSE;
    }

    return QM_TRUE;
}

static void qm_timer_mgr_fill_status_locked(qm_timer_mgr_t timer,
                                            qm_timer_mgr_status_t *status)
{
    status->mode = timer->mode;
    status->state = timer->state;
    status->duration_sec = timer->duration_sec;
    status->elapsed_sec = timer->elapsed_sec;
    status->remaining_sec = timer->remaining_sec;
}

static void qm_timer_mgr_append_event_locked(qm_timer_mgr_t timer,
                                             qm_timer_mgr_event_data_t *events,
                                             int *event_count,
                                             qm_timer_mgr_event_t event,
                                             qm_timer_mgr_state_t previous_state)
{
    qm_timer_mgr_event_data_t *item = NULL;

    if (*event_count >= QM_TIMER_MGR_MAX_EVENTS_PER_STEP) {
        return;
    }

    item = &events[*event_count];
    item->event = event;
    item->previous_state = previous_state;
    item->user_ctx = timer->config.user_ctx;
    qm_timer_mgr_fill_status_locked(timer, &item->status);
    *event_count = *event_count + 1;
}

/* Dispatch a pre-filled event batch after the lock is released. The product
 * callback updates product state first; the optional cloud module reports the
 * resulting state afterwards. If destroy() started mid-batch the callback is
 * suppressed and dispatching stops early. */
static void qm_timer_mgr_dispatch_events(qm_timer_mgr_t timer,
                                         const qm_timer_mgr_event_data_t *events,
                                         int event_count)
{
    int i = 0;

    for (i = 0; i < event_count; i++) {
        qm_timer_mgr_event_cb_t event_cb;
        qm_bool has_cloud;

        qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
        event_cb = timer->destroying ? NULL : timer->config.event_cb;
        has_cloud = timer->config.cloud != NULL;
        qm_mutex_unlock(&timer->lock);
        if (!event_cb && !has_cloud) {
            return;
        }
        if (event_cb) {
            event_cb(timer, &events[i]);
        }
        qm_timer_mgr_cloud_event(timer, &events[i]);
    }
}

void qm_timer_mgr_cloud_set_context(qm_timer_mgr_t timer, void *context)
{
    if (timer == NULL || !timer->lock_valid) {
        return;
    }
    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    timer->cloud_ctx = context;
    qm_mutex_unlock(&timer->lock);
}

void *qm_timer_mgr_cloud_get_context(qm_timer_mgr_t timer)
{
    void *context;

    if (timer == NULL || !timer->lock_valid) {
        return NULL;
    }
    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    context = timer->cloud_ctx;
    qm_mutex_unlock(&timer->lock);
    return context;
}

int qm_timer_mgr_cloud_set_config(qm_timer_mgr_t timer,
                                  const qm_timer_mgr_cloud_config_t *config)
{
    if (timer == NULL || config == NULL || !timer->lock_valid) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    if (timer->destroying || timer->config.cloud != NULL) {
        qm_mutex_unlock(&timer->lock);
        return -QM_EBUSY;
    }
    timer->config.cloud = config;
    qm_mutex_unlock(&timer->lock);
    return QM_EOK;
}

/* Configure a fresh run. Emits STARTED or RESTARTED depending on the caller.
 * Countdown duration falls back to the last used, then the configured default;
 * a stopwatch ignores the duration. */
static void qm_timer_mgr_begin_run_locked(qm_timer_mgr_t timer,
                                          qm_timer_mgr_event_data_t *events,
                                          int *event_count,
                                          qm_timer_mgr_event_t run_event,
                                          qm_timer_mgr_mode_t mode,
                                          uint32_t duration_sec)
{
    qm_timer_mgr_state_t previous_state = timer->state;
    uint32_t duration = duration_sec;

    timer->mode = mode;
    if (mode == QM_TIMER_MGR_MODE_COUNTDOWN) {
        if (duration == 0) {
            duration = timer->last_used_duration_sec;
        }
        if (duration == 0) {
            duration = timer->config.duration_sec;
        }
        timer->last_used_duration_sec = duration;
        timer->duration_sec = duration;
        timer->remaining_sec = duration;
        timer->elapsed_sec = 0;
    } else {
        timer->duration_sec = 0;
        timer->remaining_sec = 0;
        timer->elapsed_sec = 0;
    }
    timer->state = QM_TIMER_MGR_STATE_RUNNING;
    timer->last_tick_ms = qm_now_ms();
    qm_timer_mgr_append_event_locked(timer, events, event_count,
                                     run_event, previous_state);
}

/* Advance the state by whole wall-clock seconds. A single TICK is appended with
 * the latest status so a long suspension does not produce an event storm. */
static void qm_timer_mgr_advance_locked(qm_timer_mgr_t timer,
                                        qm_timer_mgr_event_data_t *events,
                                        int *event_count,
                                        qm_bool *stop_timer)
{
    uint32_t now = qm_now_ms();
    uint32_t elapsed_ms;
    uint32_t seconds;
    qm_timer_mgr_state_t previous_state = timer->state;

    if (timer->state != QM_TIMER_MGR_STATE_RUNNING) {
        return;
    }
    if (timer->last_tick_ms == 0) {
        timer->last_tick_ms = now;
        return;
    }

    elapsed_ms = now - timer->last_tick_ms;
    seconds = elapsed_ms / QM_TIMER_MGR_MS_PER_SEC;
    if (seconds == 0) {
        return;
    }
    timer->last_tick_ms += seconds * QM_TIMER_MGR_MS_PER_SEC;

    if (timer->mode == QM_TIMER_MGR_MODE_STOPWATCH) {
        timer->elapsed_sec += seconds;
        qm_timer_mgr_append_event_locked(timer, events, event_count,
                                         QM_TIMER_MGR_EVENT_TICK,
                                         previous_state);
        return;
    }

    if (seconds >= timer->remaining_sec) {
        timer->elapsed_sec += timer->remaining_sec;
        timer->remaining_sec = 0;
    } else {
        timer->remaining_sec -= seconds;
        timer->elapsed_sec += seconds;
    }

    if (timer->remaining_sec == 0) {
        timer->state = QM_TIMER_MGR_STATE_FINISHED;
        timer->last_tick_ms = 0;
        qm_timer_mgr_append_event_locked(timer, events, event_count,
                                         QM_TIMER_MGR_EVENT_TICK,
                                         previous_state);
        qm_timer_mgr_append_event_locked(timer, events, event_count,
                                         QM_TIMER_MGR_EVENT_FINISHED,
                                         previous_state);
        *stop_timer = QM_TRUE;
    } else {
        qm_timer_mgr_append_event_locked(timer, events, event_count,
                                         QM_TIMER_MGR_EVENT_TICK,
                                         previous_state);
    }
}

/* Kernel timer wake-up: enter the critical section, advance by whole wall-clock
 * seconds, stop the kernel timer on FINISHED, then dispatch events outside the
 * lock. */
static void qm_timer_mgr_timer_cb(qm_timer_t *timer, void *arg)
{
    qm_timer_mgr_t self = (qm_timer_mgr_t)arg;
    qm_timer_mgr_event_data_t events[QM_TIMER_MGR_MAX_EVENTS_PER_STEP];
    int event_count = 0;
    qm_bool stop_timer = QM_FALSE;

    (void)timer;

    if (!self) {
        return;
    }
    if (qm_timer_mgr_enter(self) != QM_EOK) {
        return;
    }

    qm_mutex_lock(&self->lock, QM_WAIT_FOREVER);
    if (self->destroying || self->state != QM_TIMER_MGR_STATE_RUNNING) {
        qm_mutex_unlock(&self->lock);
        qm_timer_mgr_leave(self);
        return;
    }
    qm_timer_mgr_advance_locked(self, events, &event_count, &stop_timer);
    qm_mutex_unlock(&self->lock);

    if (stop_timer) {
        (void)qm_timer_stop(&self->timer);
    }
    qm_timer_mgr_dispatch_events(self, events, event_count);
    qm_timer_mgr_leave(self);
}

qm_timer_mgr_t qm_timer_mgr_create(const qm_timer_mgr_config_t *config)
{
    qm_timer_mgr_t timer = NULL;
    qm_timer_mgr_config_t normalized_config;

    qm_timer_mgr_normalize_config(&normalized_config, config);
    if (!qm_timer_mgr_config_valid(&normalized_config)) {
        return NULL;
    }

    timer = (qm_timer_mgr_t)qm_calloc(1, sizeof(*timer));
    if (!timer) {
        return NULL;
    }

    timer->config = normalized_config;
    timer->mode = normalized_config.mode;
    timer->state = QM_TIMER_MGR_STATE_IDLE;
    if (qm_mutex_new(&timer->lock) != QM_EOK) {
        qm_free(timer);
        return NULL;
    }
    timer->lock_valid = QM_TRUE;

    if (qm_timer_new(&timer->timer, qm_timer_mgr_timer_cb, timer,
                     (int)timer->config.tick_ms, 1) != QM_EOK) {
        qm_mutex_free(&timer->lock);
        qm_free(timer);
        return NULL;
    }
    timer->timer_valid = QM_TRUE;

    /* Tear down in reverse creation order if the optional cloud submodule
     * fails to start, so no partially-initialized resources leak. */
    if (timer->config.cloud != NULL &&
        qm_timer_mgr_cloud_init(timer, timer->config.cloud) != QM_EOK) {
        qm_timer_mgr_cloud_deinit(timer);
        (void)qm_timer_free(&timer->timer);
        qm_mutex_free(&timer->lock);
        qm_free(timer);
        return NULL;
    }

    return timer;
}

void qm_timer_mgr_destroy(qm_timer_mgr_t timer)
{
    if (!timer) {
        return;
    }

    /* Phase out new work first, then stop the timer and wait for any in-flight
     * API/timer callback that may still dispatch through the cloud adapter. */
    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    if (timer->destroying) {
        qm_mutex_unlock(&timer->lock);
        return;
    }
    timer->destroying = QM_TRUE;
    timer->config.event_cb = NULL;
    qm_mutex_unlock(&timer->lock);

    if (timer->timer_valid) {
        (void)qm_timer_stop(&timer->timer);
    }

    while (qm_timer_mgr_has_active_ops(timer)) {
        (void)qm_msleep((int)QM_TIMER_MGR_DESTROY_WAIT_MS);
    }

    /* No event callback or public operation can now touch the cloud context. */
    qm_timer_mgr_cloud_quiesce(timer);
    qm_timer_mgr_cloud_deinit(timer);

    if (timer->timer_valid) {
        (void)qm_timer_free(&timer->timer);
        timer->timer_valid = QM_FALSE;
    }

    if (timer->lock_valid) {
        qm_mutex_free(&timer->lock);
        timer->lock_valid = QM_FALSE;
    }

    qm_free(timer);
}

int qm_timer_mgr_start(qm_timer_mgr_t timer, qm_timer_mgr_mode_t mode,
                       uint32_t duration_sec)
{
    qm_timer_mgr_event_data_t events[QM_TIMER_MGR_MAX_EVENTS_PER_STEP];
    int event_count = 0;
    int ret = QM_EOK;

    if (!timer) {
        return -QM_EINVAL;
    }
    if (mode != QM_TIMER_MGR_MODE_COUNTDOWN &&
        mode != QM_TIMER_MGR_MODE_STOPWATCH) {
        return -QM_EINVAL;
    }

    ret = qm_timer_mgr_enter(timer);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    (void)qm_timer_stop(&timer->timer);
    qm_timer_mgr_begin_run_locked(timer, events, &event_count,
                                  QM_TIMER_MGR_EVENT_STARTED, mode,
                                  duration_sec);

    ret = qm_timer_start(&timer->timer);
    if (ret != QM_EOK) {
        /* Roll back the optimistic state change so a failed start leaves the
         * manager idle rather than RUNNING. */
        timer->state = QM_TIMER_MGR_STATE_IDLE;
        timer->remaining_sec = 0;
        timer->elapsed_sec = 0;
        timer->duration_sec = 0;
        qm_mutex_unlock(&timer->lock);
        qm_timer_mgr_leave(timer);
        return -QM_EIO;
    }
    qm_mutex_unlock(&timer->lock);

    qm_timer_mgr_dispatch_events(timer, events, event_count);
    qm_timer_mgr_leave(timer);
    return QM_EOK;
}

int qm_timer_mgr_pause(qm_timer_mgr_t timer)
{
    qm_timer_mgr_event_data_t event;
    int event_count = 0;
    int ret;

    if (!timer) {
        return -QM_EINVAL;
    }

    ret = qm_timer_mgr_enter(timer);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    if (timer->state != QM_TIMER_MGR_STATE_RUNNING) {
        qm_mutex_unlock(&timer->lock);
        qm_timer_mgr_leave(timer);
        return -QM_EINVAL;
    }

    timer->state = QM_TIMER_MGR_STATE_PAUSED;
    qm_timer_mgr_append_event_locked(timer, &event, &event_count,
                                     QM_TIMER_MGR_EVENT_PAUSED,
                                     QM_TIMER_MGR_STATE_RUNNING);
    (void)qm_timer_stop(&timer->timer);
    qm_mutex_unlock(&timer->lock);

    qm_timer_mgr_dispatch_events(timer, &event, event_count);
    qm_timer_mgr_leave(timer);
    return QM_EOK;
}

int qm_timer_mgr_resume(qm_timer_mgr_t timer)
{
    qm_timer_mgr_event_data_t event;
    int event_count = 0;
    int ret = QM_EOK;

    if (!timer) {
        return -QM_EINVAL;
    }

    ret = qm_timer_mgr_enter(timer);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    if (timer->state != QM_TIMER_MGR_STATE_PAUSED) {
        qm_mutex_unlock(&timer->lock);
        qm_timer_mgr_leave(timer);
        return -QM_EINVAL;
    }

    timer->state = QM_TIMER_MGR_STATE_RUNNING;
    timer->last_tick_ms = qm_now_ms();
    qm_timer_mgr_append_event_locked(timer, &event, &event_count,
                                     QM_TIMER_MGR_EVENT_RESUMED,
                                     QM_TIMER_MGR_STATE_PAUSED);
    ret = qm_timer_start(&timer->timer);
    if (ret != QM_EOK) {
        timer->state = QM_TIMER_MGR_STATE_PAUSED;
        qm_mutex_unlock(&timer->lock);
        qm_timer_mgr_leave(timer);
        return -QM_EIO;
    }
    qm_mutex_unlock(&timer->lock);

    qm_timer_mgr_dispatch_events(timer, &event, event_count);
    qm_timer_mgr_leave(timer);
    return QM_EOK;
}

int qm_timer_mgr_restart(qm_timer_mgr_t timer)
{
    qm_timer_mgr_event_data_t events[QM_TIMER_MGR_MAX_EVENTS_PER_STEP];
    int event_count = 0;
    int ret = QM_EOK;

    if (!timer) {
        return -QM_EINVAL;
    }

    ret = qm_timer_mgr_enter(timer);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    (void)qm_timer_stop(&timer->timer);
    qm_timer_mgr_begin_run_locked(timer, events, &event_count,
                                  QM_TIMER_MGR_EVENT_RESTARTED, timer->mode,
                                  timer->last_used_duration_sec);

    ret = qm_timer_start(&timer->timer);
    if (ret != QM_EOK) {
        timer->state = QM_TIMER_MGR_STATE_IDLE;
        timer->remaining_sec = 0;
        timer->elapsed_sec = 0;
        timer->duration_sec = 0;
        qm_mutex_unlock(&timer->lock);
        qm_timer_mgr_leave(timer);
        return -QM_EIO;
    }
    qm_mutex_unlock(&timer->lock);

    qm_timer_mgr_dispatch_events(timer, events, event_count);
    qm_timer_mgr_leave(timer);
    return QM_EOK;
}

int qm_timer_mgr_stop(qm_timer_mgr_t timer)
{
    qm_timer_mgr_event_data_t event;
    int event_count = 0;
    qm_timer_mgr_state_t previous_state;
    int ret;

    if (!timer) {
        return -QM_EINVAL;
    }

    ret = qm_timer_mgr_enter(timer);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    if (timer->state == QM_TIMER_MGR_STATE_IDLE) {
        qm_mutex_unlock(&timer->lock);
        qm_timer_mgr_leave(timer);
        return QM_EOK;
    }

    previous_state = timer->state;
    timer->state = QM_TIMER_MGR_STATE_STOPPED;
    timer->last_tick_ms = 0;
    if (timer->mode == QM_TIMER_MGR_MODE_COUNTDOWN) {
        timer->remaining_sec = timer->duration_sec;
        timer->elapsed_sec = 0;
    } else {
        timer->duration_sec = 0;
        timer->remaining_sec = 0;
        timer->elapsed_sec = 0;
    }
    qm_timer_mgr_append_event_locked(timer, &event, &event_count,
                                     QM_TIMER_MGR_EVENT_STOPPED,
                                     previous_state);
    (void)qm_timer_stop(&timer->timer);
    qm_mutex_unlock(&timer->lock);

    qm_timer_mgr_dispatch_events(timer, &event, event_count);
    qm_timer_mgr_leave(timer);
    return QM_EOK;
}

int qm_timer_mgr_get_status(qm_timer_mgr_t timer, qm_timer_mgr_status_t *status)
{
    qm_timer_mgr_event_data_t events[QM_TIMER_MGR_MAX_EVENTS_PER_STEP];
    int event_count = 0;
    qm_bool stop_timer = QM_FALSE;
    int ret;

    if (!timer || !status) {
        return -QM_EINVAL;
    }

    ret = qm_timer_mgr_enter(timer);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    if (!timer->destroying) {
        qm_timer_mgr_advance_locked(timer, events, &event_count, &stop_timer);
    }
    qm_timer_mgr_fill_status_locked(timer, status);
    qm_mutex_unlock(&timer->lock);

    if (stop_timer) {
        (void)qm_timer_stop(&timer->timer);
    }
    qm_timer_mgr_dispatch_events(timer, events, event_count);
    qm_timer_mgr_leave(timer);

    return QM_EOK;
}

int qm_timer_mgr_peek_status(qm_timer_mgr_t timer, qm_timer_mgr_status_t *status)
{
    int ret;

    if (!timer || !status) {
        return -QM_EINVAL;
    }

    ret = qm_timer_mgr_enter(timer);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    qm_timer_mgr_fill_status_locked(timer, status);
    qm_mutex_unlock(&timer->lock);
    qm_timer_mgr_leave(timer);

    return QM_EOK;
}

int qm_timer_mgr_is_active(qm_timer_mgr_t timer)
{
    int active = 0;
    int ret;

    if (!timer) {
        return 0;
    }

    ret = qm_timer_mgr_enter(timer);
    if (ret != QM_EOK) {
        return 0;
    }

    qm_mutex_lock(&timer->lock, QM_WAIT_FOREVER);
    active = (timer->state == QM_TIMER_MGR_STATE_RUNNING ||
              timer->state == QM_TIMER_MGR_STATE_PAUSED) ? 1 : 0;
    qm_mutex_unlock(&timer->lock);
    qm_timer_mgr_leave(timer);

    return active;
}

const char *qm_timer_mgr_mode_str(qm_timer_mgr_mode_t mode)
{
    switch (mode) {
    case QM_TIMER_MGR_MODE_COUNTDOWN:
        return "countdown";
    case QM_TIMER_MGR_MODE_STOPWATCH:
        return "stopwatch";
    default:
        return "unknown";
    }
}

const char *qm_timer_mgr_state_str(qm_timer_mgr_state_t state)
{
    switch (state) {
    case QM_TIMER_MGR_STATE_IDLE:
        return "idle";
    case QM_TIMER_MGR_STATE_RUNNING:
        return "running";
    case QM_TIMER_MGR_STATE_PAUSED:
        return "paused";
    case QM_TIMER_MGR_STATE_STOPPED:
        return "stopped";
    case QM_TIMER_MGR_STATE_FINISHED:
        return "finished";
    default:
        return "idle";
    }
}
