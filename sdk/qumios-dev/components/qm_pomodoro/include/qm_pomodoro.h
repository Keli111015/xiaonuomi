#ifndef QM_POMODORO_H
#define QM_POMODORO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_errno.h"
#include "qm_types.h"

/**
 * @brief Opaque pomodoro timer handle.
 */
typedef struct qm_pomodoro *qm_pomodoro_t;

/**
 * @brief Special focus target value for never-ending pomodoro sessions.
 */
#define QM_POMODORO_FOCUS_TARGET_FOREVER 0xffffffffu

/**
 * @brief Pomodoro phase state.
 */
typedef enum {
    QM_POMODORO_STATE_IDLE = 0,        /**< Timer is idle. */
    QM_POMODORO_STATE_FOCUS,          /**< Focus phase is running or paused. */
    QM_POMODORO_STATE_SHORT_BREAK,    /**< Short break phase is running or paused. */
    QM_POMODORO_STATE_LONG_BREAK,     /**< Long break phase is running or paused. */
    QM_POMODORO_STATE_FINISHED,       /**< Target focus count has completed. */
} qm_pomodoro_state_t;

/**
 * @brief Pomodoro events delivered from the timer.
 */
typedef enum {
    QM_POMODORO_EVENT_STATE_CHANGED = 0, /**< Phase changed. */
    QM_POMODORO_EVENT_TICK,              /**< Remaining time was updated. */
    QM_POMODORO_EVENT_PHASE_DONE,        /**< Current focus or break phase completed. */
    QM_POMODORO_EVENT_PAUSED,            /**< Timer was paused. */
    QM_POMODORO_EVENT_RESUMED,           /**< Timer resumed from pause. */
    QM_POMODORO_EVENT_STOPPED,           /**< Timer was stopped and reset to idle. */
    QM_POMODORO_EVENT_FINISHED,          /**< Target focus count completed. */
} qm_pomodoro_event_t;

/**
 * @brief Read-only pomodoro status snapshot.
 */
typedef struct {
    qm_pomodoro_state_t state;   /**< Current phase state. */
    qm_bool paused;              /**< QM_TRUE when the current phase is paused. */
    uint32_t remaining_sec;      /**< Seconds remaining in the current phase. */
    uint32_t elapsed_sec;        /**< Seconds elapsed in the current phase. */
    uint32_t duration_sec;       /**< Current phase duration in seconds. */
    uint32_t completed_focus;    /**< Number of completed focus phases. */
    uint32_t focus_target;       /**< Target focus phases, or QM_POMODORO_FOCUS_TARGET_FOREVER. */
} qm_pomodoro_status_t;

/**
 * @brief Pomodoro event payload.
 */
typedef struct {
    qm_pomodoro_event_t event;           /**< Event type. */
    qm_pomodoro_state_t previous_state;  /**< Previous phase for state changes, or current phase for other events. */
    qm_pomodoro_status_t status;         /**< Status snapshot after the event. */
    qm_bool phase_skipped;               /**< Completion was requested by skip, not timeout. */
    void *user_ctx;                      /**< User context from qm_pomodoro_config_t. */
} qm_pomodoro_event_data_t;

/**
 * @brief Pomodoro event callback.
 *
 * The callback is called after the internal lock is released. It should avoid
 * long blocking work because timer callbacks may run from a kernel timer task.
 * qm_pomodoro_destroy() suppresses later callbacks and waits for in-flight
 * operations to finish. Do not destroy the timer from this callback.
 */
typedef void (*qm_pomodoro_event_cb_t)(qm_pomodoro_t pomodoro,
                                       const qm_pomodoro_event_data_t *event);

/**
 * @brief Pomodoro timer configuration.
 *
 * Any duration or interval field set to 0 uses the component Kconfig default.
 */
typedef struct {
    uint32_t focus_sec;             /**< Focus phase duration in seconds. */
    uint32_t short_break_sec;       /**< Short break duration in seconds. */
    uint32_t long_break_sec;        /**< Long break duration in seconds. */
    uint32_t long_break_interval;   /**< Start a long break after this many focus phases. */
    uint32_t focus_target;          /**< Focus phases before finish; 0 uses the default. */
    uint32_t tick_ms;               /**< Kernel timer period in milliseconds; 0 uses the default;
                                         need not divide durations. */
    qm_bool single_round;           /**< QM_TRUE: one focus phase then one short break then finish. */
    qm_pomodoro_event_cb_t event_cb;/**< Optional event callback. */
    void *user_ctx;                 /**< User context passed to events. */
} qm_pomodoro_config_t;

/**
 * @brief Create a pomodoro timer.
 *
 * @param[in] config Timer configuration, or NULL for Kconfig defaults.
 *
 * @return Timer handle on success, NULL on failure.
 */
qm_pomodoro_t qm_pomodoro_create(const qm_pomodoro_config_t *config);

/**
 * @brief Destroy a pomodoro timer.
 *
 * @param[in] pomodoro Timer handle.
 */
void qm_pomodoro_destroy(qm_pomodoro_t pomodoro);

/**
 * @brief Start from the first focus phase.
 *
 * @param[in] pomodoro Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_pomodoro_start(qm_pomodoro_t pomodoro);

/**
 * @brief Pause the current focus or break phase.
 *
 * @param[in] pomodoro Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_pomodoro_pause(qm_pomodoro_t pomodoro);

/**
 * @brief Resume a paused focus or break phase.
 *
 * @param[in] pomodoro Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_pomodoro_resume(qm_pomodoro_t pomodoro);

/**
 * @brief Stop the timer and reset it to idle.
 *
 * @param[in] pomodoro Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_pomodoro_stop(qm_pomodoro_t pomodoro);

/**
 * @brief Complete the current phase immediately.
 *
 * @param[in] pomodoro Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_pomodoro_skip(qm_pomodoro_t pomodoro);

/**
 * @brief Copy current status.
 *
 * @param[in]  pomodoro Timer handle.
 * @param[out] status   Destination status snapshot.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_pomodoro_get_status(qm_pomodoro_t pomodoro,
                           qm_pomodoro_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* QM_POMODORO_H */
