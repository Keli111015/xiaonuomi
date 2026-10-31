#include "qm_pomodoro.h"

#include "qm_kernel.h"

#ifndef CONFIG_QM_POMODORO_FOCUS_SEC
#define CONFIG_QM_POMODORO_FOCUS_SEC 1500
#endif

#ifndef CONFIG_QM_POMODORO_SHORT_BREAK_SEC
#define CONFIG_QM_POMODORO_SHORT_BREAK_SEC 300
#endif

#ifndef CONFIG_QM_POMODORO_LONG_BREAK_SEC
#define CONFIG_QM_POMODORO_LONG_BREAK_SEC 900
#endif

#ifndef CONFIG_QM_POMODORO_LONG_BREAK_INTERVAL
#define CONFIG_QM_POMODORO_LONG_BREAK_INTERVAL 4
#endif

#ifndef CONFIG_QM_POMODORO_FOCUS_TARGET
#define CONFIG_QM_POMODORO_FOCUS_TARGET 4
#endif

#ifndef CONFIG_QM_POMODORO_TICK_MS
#define CONFIG_QM_POMODORO_TICK_MS 1000
#endif

#ifndef CONFIG_QM_POMODORO_SINGLE_ROUND
#define CONFIG_QM_POMODORO_SINGLE_ROUND 0
#endif

#define QM_POMODORO_MAX_EVENTS_PER_STEP 3
#define QM_POMODORO_MIN_TIMER_MS       100u
#define QM_POMODORO_MAX_TIMER_MS       0x7fffffffu
#define QM_POMODORO_MS_PER_SEC         1000u
#define QM_POMODORO_DESTROY_WAIT_MS    10u
#define QM_POMODORO_MAX_DURATION_SEC   (0xffffffffu / QM_POMODORO_MS_PER_SEC)

struct qm_pomodoro {
    qm_pomodoro_config_t config;
    qm_timer_t timer;
    qm_mutex_t lock;
    qm_bool timer_valid;
    qm_bool lock_valid;
    qm_bool paused;
    /* destroy marks the handle invalid for new work; active_ops keeps in-flight
     * API calls and timer callbacks alive until they exit. */
    qm_bool destroying;
    int active_ops;
    qm_pomodoro_state_t state;
    uint32_t remaining_ms;
    uint32_t elapsed_ms;
    uint32_t duration_ms;
    uint32_t completed_focus;
};

static qm_bool qm_pomodoro_is_phase(qm_pomodoro_state_t state)
{
    return (state == QM_POMODORO_STATE_FOCUS ||
            state == QM_POMODORO_STATE_SHORT_BREAK ||
            state == QM_POMODORO_STATE_LONG_BREAK) ? QM_TRUE : QM_FALSE;
}

static uint32_t qm_pomodoro_default_u32(uint32_t value, uint32_t fallback)
{
    return value ? value : fallback;
}

static void qm_pomodoro_normalize_config(qm_pomodoro_config_t *dst,
                                         const qm_pomodoro_config_t *src)
{
    if (src) {
        *dst = *src;
    } else {
        dst->focus_sec = 0;
        dst->short_break_sec = 0;
        dst->long_break_sec = 0;
        dst->long_break_interval = 0;
        dst->focus_target = 0;
        dst->tick_ms = 0;
        dst->single_round = 0;
        dst->event_cb = NULL;
        dst->user_ctx = NULL;
    }

    dst->single_round = dst->single_round ? QM_TRUE : CONFIG_QM_POMODORO_SINGLE_ROUND;

    dst->focus_sec = qm_pomodoro_default_u32(dst->focus_sec,
                                            CONFIG_QM_POMODORO_FOCUS_SEC);
    dst->short_break_sec = qm_pomodoro_default_u32(dst->short_break_sec,
                                                  CONFIG_QM_POMODORO_SHORT_BREAK_SEC);
    dst->long_break_sec = qm_pomodoro_default_u32(dst->long_break_sec,
                                                 CONFIG_QM_POMODORO_LONG_BREAK_SEC);
    dst->long_break_interval = qm_pomodoro_default_u32(dst->long_break_interval,
                                                      CONFIG_QM_POMODORO_LONG_BREAK_INTERVAL);
    dst->tick_ms = qm_pomodoro_default_u32(dst->tick_ms,
                                          CONFIG_QM_POMODORO_TICK_MS);
    if (dst->focus_target == 0) {
        dst->focus_target = CONFIG_QM_POMODORO_FOCUS_TARGET;
    }
}

static qm_bool qm_pomodoro_config_valid(const qm_pomodoro_config_t *config)
{
    if (!config || config->focus_sec == 0 || config->short_break_sec == 0 ||
        config->long_break_sec == 0 || config->long_break_interval == 0 ||
        config->focus_target == 0 || config->tick_ms < QM_POMODORO_MIN_TIMER_MS ||
        config->tick_ms > QM_POMODORO_MAX_TIMER_MS ||
        config->focus_sec > QM_POMODORO_MAX_DURATION_SEC ||
        config->short_break_sec > QM_POMODORO_MAX_DURATION_SEC ||
        config->long_break_sec > QM_POMODORO_MAX_DURATION_SEC) {
        return QM_FALSE;
    }

    return QM_TRUE;
}

static int qm_pomodoro_enter(qm_pomodoro_t pomodoro)
{
    if (!pomodoro) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (pomodoro->destroying) {
        qm_mutex_unlock(&pomodoro->lock);
        return -QM_EBUSY;
    }
    pomodoro->active_ops++;
    qm_mutex_unlock(&pomodoro->lock);
    return QM_EOK;
}

static void qm_pomodoro_leave(qm_pomodoro_t pomodoro)
{
    if (!pomodoro) {
        return;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (pomodoro->active_ops > 0) {
        pomodoro->active_ops--;
    }
    qm_mutex_unlock(&pomodoro->lock);
}

static qm_bool qm_pomodoro_has_active_ops(qm_pomodoro_t pomodoro)
{
    qm_bool active;

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    active = pomodoro->active_ops > 0 ? QM_TRUE : QM_FALSE;
    qm_mutex_unlock(&pomodoro->lock);
    return active;
}

static uint32_t qm_pomodoro_ms_to_remaining_sec(uint32_t ms)
{
    if (ms == 0) {
        return 0;
    }

    return ((ms - 1u) / QM_POMODORO_MS_PER_SEC) + 1u;
}

static void qm_pomodoro_fill_status_locked(qm_pomodoro_t pomodoro,
                                           qm_pomodoro_status_t *status)
{
    status->state = pomodoro->state;
    status->paused = pomodoro->paused;
    status->remaining_sec = qm_pomodoro_ms_to_remaining_sec(pomodoro->remaining_ms);
    status->elapsed_sec = pomodoro->elapsed_ms / QM_POMODORO_MS_PER_SEC;
    status->duration_sec = pomodoro->duration_ms / QM_POMODORO_MS_PER_SEC;
    status->completed_focus = pomodoro->completed_focus;
    status->focus_target = pomodoro->config.focus_target;
}

static void qm_pomodoro_append_event_locked(qm_pomodoro_t pomodoro,
                                            qm_pomodoro_event_data_t *events,
                                            int *event_count,
                                            qm_pomodoro_event_t event,
                                            qm_pomodoro_state_t previous_state)
{
    qm_pomodoro_event_data_t *item = NULL;

    if (*event_count >= QM_POMODORO_MAX_EVENTS_PER_STEP) {
        return;
    }

    item = &events[*event_count];
    item->phase_skipped = QM_FALSE;
    item->event = event;
    item->previous_state = previous_state;
    item->user_ctx = pomodoro->config.user_ctx;
    qm_pomodoro_fill_status_locked(pomodoro, &item->status);
    *event_count = *event_count + 1;
}

static void qm_pomodoro_dispatch_events(qm_pomodoro_t pomodoro,
                                        const qm_pomodoro_event_data_t *events,
                                        int event_count)
{
    int i = 0;

    for (i = 0; i < event_count; i++) {
        qm_pomodoro_event_cb_t event_cb;

        qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
        event_cb = pomodoro->destroying ? NULL : pomodoro->config.event_cb;
        qm_mutex_unlock(&pomodoro->lock);
        if (!event_cb) {
            return;
        }
        event_cb(pomodoro, &events[i]);
    }
}

static void qm_pomodoro_set_phase_locked(qm_pomodoro_t pomodoro,
                                         qm_pomodoro_state_t state,
                                         uint32_t duration_sec)
{
    pomodoro->state = state;
    pomodoro->paused = QM_FALSE;
    pomodoro->duration_ms = duration_sec * QM_POMODORO_MS_PER_SEC;
    pomodoro->remaining_ms = pomodoro->duration_ms;
    pomodoro->elapsed_ms = 0;
}

static qm_pomodoro_state_t qm_pomodoro_next_break_state_locked(qm_pomodoro_t pomodoro)
{
    if ((pomodoro->completed_focus % pomodoro->config.long_break_interval) == 0) {
        return QM_POMODORO_STATE_LONG_BREAK;
    }

    return QM_POMODORO_STATE_SHORT_BREAK;
}

static uint32_t qm_pomodoro_phase_duration_locked(qm_pomodoro_t pomodoro,
                                                  qm_pomodoro_state_t state)
{
    if (state == QM_POMODORO_STATE_FOCUS) {
        return pomodoro->config.focus_sec;
    }

    if (state == QM_POMODORO_STATE_LONG_BREAK) {
        return pomodoro->config.long_break_sec;
    }

    return pomodoro->config.short_break_sec;
}

static void qm_pomodoro_enter_finished_locked(qm_pomodoro_t pomodoro,
                                              qm_pomodoro_event_data_t *events,
                                              int *event_count,
                                              qm_pomodoro_state_t previous_state,
                                              qm_bool *stop_timer)
{
    pomodoro->state = QM_POMODORO_STATE_FINISHED;
    pomodoro->paused = QM_FALSE;
    pomodoro->duration_ms = 0;
    pomodoro->remaining_ms = 0;
    pomodoro->elapsed_ms = 0;
    *stop_timer = QM_TRUE;
    qm_pomodoro_append_event_locked(pomodoro, events, event_count,
                                    QM_POMODORO_EVENT_FINISHED,
                                    previous_state);
}

static void qm_pomodoro_complete_phase_locked(qm_pomodoro_t pomodoro,
                                              qm_pomodoro_event_data_t *events,
                                              int *event_count,
                                              qm_bool *stop_timer)
{
    qm_pomodoro_state_t previous_state = pomodoro->state;
    qm_pomodoro_state_t next_state = QM_POMODORO_STATE_IDLE;
    uint32_t next_duration_sec = 0;

    if (!qm_pomodoro_is_phase(previous_state)) {
        return;
    }

    pomodoro->remaining_ms = 0;
    if (pomodoro->elapsed_ms < pomodoro->duration_ms) {
        pomodoro->elapsed_ms = pomodoro->duration_ms;
    }

    if (previous_state == QM_POMODORO_STATE_FOCUS) {
        pomodoro->completed_focus++;
    }
    qm_pomodoro_append_event_locked(pomodoro, events, event_count,
                                    QM_POMODORO_EVENT_PHASE_DONE,
                                    previous_state);

    if (previous_state == QM_POMODORO_STATE_FOCUS) {
        if (pomodoro->config.single_round) {
            next_state = QM_POMODORO_STATE_SHORT_BREAK;
        } else if (pomodoro->config.focus_target != QM_POMODORO_FOCUS_TARGET_FOREVER &&
                   pomodoro->completed_focus >= pomodoro->config.focus_target) {
            qm_pomodoro_enter_finished_locked(pomodoro, events, event_count,
                                              previous_state, stop_timer);
            return;
        } else {
            next_state = qm_pomodoro_next_break_state_locked(pomodoro);
        }
    } else {
        if (pomodoro->config.single_round) {
            qm_pomodoro_enter_finished_locked(pomodoro, events, event_count,
                                              previous_state, stop_timer);
            return;
        }
        next_state = QM_POMODORO_STATE_FOCUS;
    }

    next_duration_sec = qm_pomodoro_phase_duration_locked(pomodoro, next_state);
    qm_pomodoro_set_phase_locked(pomodoro, next_state, next_duration_sec);
    qm_pomodoro_append_event_locked(pomodoro, events, event_count,
                                    QM_POMODORO_EVENT_STATE_CHANGED,
                                    previous_state);
}

static void qm_pomodoro_timer_cb(qm_timer_t *timer, void *arg)
{
    qm_pomodoro_t pomodoro = (qm_pomodoro_t)arg;
    qm_pomodoro_event_data_t events[QM_POMODORO_MAX_EVENTS_PER_STEP];
    int event_count = 0;
    qm_bool stop_timer = QM_FALSE;

    (void)timer;

    if (!pomodoro) {
        return;
    }

    if (qm_pomodoro_enter(pomodoro) != QM_EOK) {
        return;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (pomodoro->destroying || !qm_pomodoro_is_phase(pomodoro->state) || pomodoro->paused) {
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return;
    }

    if (pomodoro->remaining_ms > 0) {
        /* Tick granularity does not need to divide the phase duration evenly.
         * The final tick is capped to the remaining time. */
        if (pomodoro->remaining_ms > pomodoro->config.tick_ms) {
            pomodoro->remaining_ms -= pomodoro->config.tick_ms;
            pomodoro->elapsed_ms += pomodoro->config.tick_ms;
        } else {
            pomodoro->elapsed_ms += pomodoro->remaining_ms;
            pomodoro->remaining_ms = 0;
        }
        qm_pomodoro_append_event_locked(pomodoro, events, &event_count,
                                        QM_POMODORO_EVENT_TICK,
                                        pomodoro->state);
    }

    if (pomodoro->remaining_ms == 0) {
        qm_pomodoro_complete_phase_locked(pomodoro, events, &event_count,
                                          &stop_timer);
    }
    qm_mutex_unlock(&pomodoro->lock);

    if (stop_timer) {
        (void)qm_timer_stop(&pomodoro->timer);
    }
    qm_pomodoro_dispatch_events(pomodoro, events, event_count);
    qm_pomodoro_leave(pomodoro);
}

qm_pomodoro_t qm_pomodoro_create(const qm_pomodoro_config_t *config)
{
    qm_pomodoro_t pomodoro = NULL;
    qm_pomodoro_config_t normalized_config;

    qm_pomodoro_normalize_config(&normalized_config, config);
    if (!qm_pomodoro_config_valid(&normalized_config)) {
        return NULL;
    }

    pomodoro = (qm_pomodoro_t)qm_calloc(1, sizeof(*pomodoro));
    if (!pomodoro) {
        return NULL;
    }

    pomodoro->config = normalized_config;
    pomodoro->state = QM_POMODORO_STATE_IDLE;
    if (qm_mutex_new(&pomodoro->lock) != QM_EOK) {
        qm_free(pomodoro);
        return NULL;
    }
    pomodoro->lock_valid = QM_TRUE;

    if (qm_timer_new(&pomodoro->timer, qm_pomodoro_timer_cb, pomodoro,
                     (int)pomodoro->config.tick_ms, 1) != QM_EOK) {
        qm_mutex_free(&pomodoro->lock);
        qm_free(pomodoro);
        return NULL;
    }
    pomodoro->timer_valid = QM_TRUE;

    return pomodoro;
}

void qm_pomodoro_destroy(qm_pomodoro_t pomodoro)
{
    if (!pomodoro) {
        return;
    }

    /* Phase out new work first, then stop the timer and wait for any in-flight
     * API/timer callback that already entered the critical section. */
    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (pomodoro->destroying) {
        qm_mutex_unlock(&pomodoro->lock);
        return;
    }
    pomodoro->destroying = QM_TRUE;
    pomodoro->config.event_cb = NULL;
    qm_mutex_unlock(&pomodoro->lock);

    if (pomodoro->timer_valid) {
        (void)qm_timer_stop(&pomodoro->timer);
    }

    while (qm_pomodoro_has_active_ops(pomodoro)) {
        (void)qm_msleep((int)QM_POMODORO_DESTROY_WAIT_MS);
    }

    if (pomodoro->timer_valid) {
        (void)qm_timer_free(&pomodoro->timer);
        pomodoro->timer_valid = QM_FALSE;
    }

    if (pomodoro->lock_valid) {
        qm_mutex_free(&pomodoro->lock);
        pomodoro->lock_valid = QM_FALSE;
    }

    qm_free(pomodoro);
}

int qm_pomodoro_start(qm_pomodoro_t pomodoro)
{
    qm_pomodoro_event_data_t event;
    int event_count = 0;
    int ret = QM_EOK;
    qm_pomodoro_state_t previous_state = QM_POMODORO_STATE_IDLE;

    ret = qm_pomodoro_enter(pomodoro);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (qm_pomodoro_is_phase(pomodoro->state) || pomodoro->paused) {
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return -QM_EBUSY;
    }

    previous_state = pomodoro->state;
    pomodoro->completed_focus = 0;
    qm_pomodoro_set_phase_locked(pomodoro, QM_POMODORO_STATE_FOCUS,
                                 pomodoro->config.focus_sec);
    qm_pomodoro_append_event_locked(pomodoro, &event, &event_count,
                                    QM_POMODORO_EVENT_STATE_CHANGED,
                                    previous_state);
    ret = qm_timer_start(&pomodoro->timer);
    if (ret != QM_EOK) {
        pomodoro->state = previous_state;
        pomodoro->paused = QM_FALSE;
        pomodoro->remaining_ms = 0;
        pomodoro->elapsed_ms = 0;
        pomodoro->duration_ms = 0;
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return -QM_EIO;
    }
    qm_mutex_unlock(&pomodoro->lock);

    qm_pomodoro_dispatch_events(pomodoro, &event, event_count);
    qm_pomodoro_leave(pomodoro);
    return QM_EOK;
}

int qm_pomodoro_pause(qm_pomodoro_t pomodoro)
{
    qm_pomodoro_event_data_t event;
    int event_count = 0;
    int ret;

    ret = qm_pomodoro_enter(pomodoro);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (!qm_pomodoro_is_phase(pomodoro->state)) {
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return -QM_EINVAL;
    }
    if (pomodoro->paused) {
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return -QM_EBUSY;
    }

    pomodoro->paused = QM_TRUE;
    qm_pomodoro_append_event_locked(pomodoro, &event, &event_count,
                                    QM_POMODORO_EVENT_PAUSED,
                                    pomodoro->state);
    (void)qm_timer_stop(&pomodoro->timer);
    qm_mutex_unlock(&pomodoro->lock);

    qm_pomodoro_dispatch_events(pomodoro, &event, event_count);
    qm_pomodoro_leave(pomodoro);
    return QM_EOK;
}

int qm_pomodoro_resume(qm_pomodoro_t pomodoro)
{
    qm_pomodoro_event_data_t event;
    int event_count = 0;
    int ret = QM_EOK;

    ret = qm_pomodoro_enter(pomodoro);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (!qm_pomodoro_is_phase(pomodoro->state) || !pomodoro->paused) {
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return -QM_EINVAL;
    }

    pomodoro->paused = QM_FALSE;
    qm_pomodoro_append_event_locked(pomodoro, &event, &event_count,
                                    QM_POMODORO_EVENT_RESUMED,
                                    pomodoro->state);
    ret = qm_timer_start(&pomodoro->timer);
    if (ret != QM_EOK) {
        pomodoro->paused = QM_TRUE;
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return -QM_EIO;
    }
    qm_mutex_unlock(&pomodoro->lock);

    qm_pomodoro_dispatch_events(pomodoro, &event, event_count);
    qm_pomodoro_leave(pomodoro);
    return QM_EOK;
}

int qm_pomodoro_stop(qm_pomodoro_t pomodoro)
{
    qm_pomodoro_event_data_t event;
    int event_count = 0;
    qm_pomodoro_state_t previous_state = QM_POMODORO_STATE_IDLE;
    int ret;

    ret = qm_pomodoro_enter(pomodoro);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (pomodoro->state == QM_POMODORO_STATE_IDLE && !pomodoro->paused) {
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return QM_EOK;
    }

    previous_state = pomodoro->state;
    pomodoro->state = QM_POMODORO_STATE_IDLE;
    pomodoro->paused = QM_FALSE;
    pomodoro->remaining_ms = 0;
    pomodoro->elapsed_ms = 0;
    pomodoro->duration_ms = 0;
    pomodoro->completed_focus = 0;
    qm_pomodoro_append_event_locked(pomodoro, &event, &event_count,
                                    QM_POMODORO_EVENT_STOPPED,
                                    previous_state);
    (void)qm_timer_stop(&pomodoro->timer);
    qm_mutex_unlock(&pomodoro->lock);

    qm_pomodoro_dispatch_events(pomodoro, &event, event_count);
    qm_pomodoro_leave(pomodoro);
    return QM_EOK;
}

int qm_pomodoro_skip(qm_pomodoro_t pomodoro)
{
    qm_pomodoro_event_data_t events[QM_POMODORO_MAX_EVENTS_PER_STEP];
    int event_count = 0;
    qm_bool stop_timer = QM_FALSE;
    int ret;

    ret = qm_pomodoro_enter(pomodoro);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    if (!qm_pomodoro_is_phase(pomodoro->state)) {
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return -QM_EINVAL;
    }
    if (pomodoro->paused) {
        qm_mutex_unlock(&pomodoro->lock);
        qm_pomodoro_leave(pomodoro);
        return -QM_EBUSY;
    }

    qm_pomodoro_complete_phase_locked(pomodoro, events, &event_count,
                                      &stop_timer);
    for (int i = 0; i < event_count; ++i) {
        events[i].phase_skipped = QM_TRUE;
    }
    if (stop_timer) {
        (void)qm_timer_stop(&pomodoro->timer);
    }
    qm_mutex_unlock(&pomodoro->lock);

    qm_pomodoro_dispatch_events(pomodoro, events, event_count);
    qm_pomodoro_leave(pomodoro);
    return QM_EOK;
}

int qm_pomodoro_get_status(qm_pomodoro_t pomodoro,
                           qm_pomodoro_status_t *status)
{
    int ret;

    if (!pomodoro || !status) {
        return -QM_EINVAL;
    }

    ret = qm_pomodoro_enter(pomodoro);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&pomodoro->lock, QM_WAIT_FOREVER);
    qm_pomodoro_fill_status_locked(pomodoro, status);
    qm_mutex_unlock(&pomodoro->lock);
    qm_pomodoro_leave(pomodoro);

    return QM_EOK;
}
