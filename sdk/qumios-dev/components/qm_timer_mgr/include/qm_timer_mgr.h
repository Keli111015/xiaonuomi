#ifndef QM_TIMER_MGR_H
#define QM_TIMER_MGR_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_errno.h"
#include "qm_types.h"

/**
 * @brief Opaque timer manager handle.
 *
 * Named qm_timer_mgr_t to avoid clashing with the kernel timer type
 * qm_timer_t from qm_kernel.h and the SoC timer HAL qm_timer.h.
 */
typedef struct qm_timer_mgr *qm_timer_mgr_t;

/**
 * @brief Timer mode.
 */
typedef enum {
    QM_TIMER_MGR_MODE_COUNTDOWN = 0,  /**< Countdown: counts remaining time down to zero. */
    QM_TIMER_MGR_MODE_STOPWATCH,      /**< Stopwatch: counts elapsed time up. */
} qm_timer_mgr_mode_t;

/**
 * @brief Timer state.
 */
typedef enum {
    QM_TIMER_MGR_STATE_IDLE = 0,      /**< Timer is idle, no task configured. */
    QM_TIMER_MGR_STATE_RUNNING,       /**< Timer is running. */
    QM_TIMER_MGR_STATE_PAUSED,        /**< Timer is paused. */
    QM_TIMER_MGR_STATE_STOPPED,       /**< Timer was stopped; countdown keeps its duration for restart. */
    QM_TIMER_MGR_STATE_FINISHED,      /**< Countdown reached zero. */
} qm_timer_mgr_state_t;

/**
 * @brief Timer events delivered from the manager.
 */
typedef enum {
    QM_TIMER_MGR_EVENT_STARTED = 0,   /**< start() entered RUNNING. */
    QM_TIMER_MGR_EVENT_TICK,          /**< A whole second advanced while running. */
    QM_TIMER_MGR_EVENT_PAUSED,        /**< RUNNING -> PAUSED. */
    QM_TIMER_MGR_EVENT_RESUMED,       /**< PAUSED -> RUNNING. */
    QM_TIMER_MGR_EVENT_RESTARTED,     /**< restart() began a fresh run. */
    QM_TIMER_MGR_EVENT_STOPPED,       /**< Timer stopped -> STOPPED. */
    QM_TIMER_MGR_EVENT_FINISHED,      /**< Countdown reached zero. */
} qm_timer_mgr_event_t;

/**
 * @brief Read-only timer status snapshot.
 */
typedef struct {
    qm_timer_mgr_mode_t mode;     /**< Current mode. */
    qm_timer_mgr_state_t state;   /**< Current state. */
    uint32_t duration_sec;        /**< Countdown: total duration; stopwatch: 0. */
    uint32_t elapsed_sec;         /**< Seconds elapsed in the current run. */
    uint32_t remaining_sec;       /**< Countdown: seconds remaining; stopwatch: 0. */
} qm_timer_mgr_status_t;

/**
 * @brief Timer event payload.
 */
typedef struct {
    qm_timer_mgr_event_t event;          /**< Event type. */
    qm_timer_mgr_state_t previous_state; /**< State before a transition; current state for other events. */
    qm_timer_mgr_status_t status;        /**< Status snapshot after the event. */
    void *user_ctx;                      /**< User context from qm_timer_mgr_config_t. */
} qm_timer_mgr_event_data_t;

/**
 * @brief Timer event callback.
 *
 * The callback is called after the internal lock is released. It should avoid
 * long blocking work because timer callbacks may run from a kernel timer task.
 * qm_timer_mgr_destroy() suppresses later callbacks and waits for in-flight
 * operations to finish. Do not destroy the manager from this callback.
 */
typedef void (*qm_timer_mgr_event_cb_t)(qm_timer_mgr_t timer,
                                        const qm_timer_mgr_event_data_t *event);

/**
 * @brief Cloud commands routed from the qm_iot `timer` method.
 *
 * The cloud submodule parses the frame and hands the command to the product
 * policy callback; the component itself never interprets business fields such
 * as timerType or guide voice.
 */
typedef enum {
    QM_TIMER_MGR_CLOUD_COMMAND_INVALID = 0, /**< Unknown or empty submethod. */
    QM_TIMER_MGR_CLOUD_COMMAND_START,       /**< "start" */
    QM_TIMER_MGR_CLOUD_COMMAND_PAUSE,       /**< "pause" */
    QM_TIMER_MGR_CLOUD_COMMAND_CONTINUE,    /**< "continue" */
    QM_TIMER_MGR_CLOUD_COMMAND_STOP,        /**< "stop" */
    QM_TIMER_MGR_CLOUD_COMMAND_RESTART,     /**< "restart" */
} qm_timer_mgr_cloud_command_type_t;

/**
 * @brief A parsed cloud command passed to the product policy callback.
 */
typedef struct {
    qm_timer_mgr_cloud_command_type_t type; /**< Command type. */
    const char *params_json;                /**< Raw params JSON (cJSON-allocated), or "{}". */
    uint32_t params_json_len;               /**< Length of params_json. */
} qm_timer_mgr_cloud_command_t;

/**
 * @brief Product policy handler for a parsed cloud timer command.
 *
 * @param[in]  timer         Timer handle.
 * @param[in]  command       Parsed command.
 * @param[out] reply_payload Optional reply payload allocated with cJSON_malloc;
 *                           the submodule frees it after replying.
 * @param[in]  user_data     Context from qm_timer_mgr_cloud_config_t.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
typedef int (*qm_timer_mgr_cloud_command_cb_t)(qm_timer_mgr_t timer,
                                               const qm_timer_mgr_cloud_command_t *command,
                                               char **reply_payload,
                                               void *user_data);

/**
 * @brief Optional product status serializer for cloud reply and update payloads.
 *
 * Return a cJSON-allocated JSON string. The cloud submodule frees it after
 * sending. Returning NULL uses the generic countdown/stopwatch status payload.
 */
typedef char *(*qm_timer_mgr_cloud_status_payload_cb_t)(qm_timer_mgr_t timer,
                                                        void *user_data);

/**
 * @brief Optional cloud integration owned by qm_timer_mgr.
 *
 * The submodule handles protocol frames and transport; the product injects
 * business fields and command execution through command_cb.
 */
typedef struct {
    void *iot_handle;                     /**< qm_iot session for down/reply/up traffic. */
    qm_timer_mgr_cloud_command_cb_t command_cb; /**< Product-specific command execution. */
    qm_timer_mgr_cloud_status_payload_cb_t status_payload_cb; /**< Optional product status JSON. */
    void *user_data;                      /**< Context passed to command_cb. */
} qm_timer_mgr_cloud_config_t;

/**
 * @brief Timer manager configuration.
 *
 * A field set to 0 uses the component Kconfig default.
 */
typedef struct {
    qm_timer_mgr_mode_t mode;     /**< Default mode; 0 uses the Kconfig default. */
    uint32_t duration_sec;        /**< Default countdown duration; 0 uses the Kconfig default. */
    uint32_t tick_ms;             /**< Kernel timer wake-up period; 0 uses the default;
                                       need not divide durations. */
    qm_timer_mgr_event_cb_t event_cb; /**< Optional event callback. */
    void *user_ctx;               /**< User context passed to events. */
    /** Optional cloud integration; NULL disables the cloud submodule. The
        configuration is borrowed and must outlive the timer manager. */
    const qm_timer_mgr_cloud_config_t *cloud;
} qm_timer_mgr_config_t;

/**
 * @brief Create a timer manager.
 *
 * @param[in] config Timer configuration, or NULL for Kconfig defaults.
 *
 * @return Timer handle on success, NULL on failure.
 */
qm_timer_mgr_t qm_timer_mgr_create(const qm_timer_mgr_config_t *config);

/**
 * @brief Destroy a timer manager.
 *
 * @param[in] timer Timer handle.
 */
void qm_timer_mgr_destroy(qm_timer_mgr_t timer);

/**
 * @brief Start a fresh run.
 *
 * For countdown, a duration_sec of 0 falls back to the last used or the
 * configured default duration. The duration is ignored for stopwatch.
 *
 * @param[in] timer       Timer handle.
 * @param[in] mode        Timer mode.
 * @param[in] duration_sec Countdown duration, or 0 for the default.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_timer_mgr_start(qm_timer_mgr_t timer, qm_timer_mgr_mode_t mode,
                       uint32_t duration_sec);

/**
 * @brief Pause a running timer.
 *
 * @param[in] timer Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_timer_mgr_pause(qm_timer_mgr_t timer);

/**
 * @brief Resume a paused timer.
 *
 * @param[in] timer Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_timer_mgr_resume(qm_timer_mgr_t timer);

/**
 * @brief Restart with the last used mode and duration.
 *
 * A countdown returns to its full duration; a stopwatch resets to zero.
 *
 * @param[in] timer Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_timer_mgr_restart(qm_timer_mgr_t timer);

/**
 * @brief Stop the timer and enter the STOPPED state.
 *
 * A countdown keeps its configured duration (remaining_sec reset to full) so
 * restart can reuse it; a stopwatch resets to zero.
 *
 * @param[in] timer Timer handle.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_timer_mgr_stop(qm_timer_mgr_t timer);

/**
 * @brief Copy the current status after advancing the time.
 *
 * @param[in]  timer  Timer handle.
 * @param[out] status Destination status snapshot.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_timer_mgr_get_status(qm_timer_mgr_t timer, qm_timer_mgr_status_t *status);

/**
 * @brief Copy the current status without advancing the time.
 *
 * @param[in]  timer  Timer handle.
 * @param[out] status Destination status snapshot.
 *
 * @return QM_EOK on success, or a negative QM_E* error code.
 */
int qm_timer_mgr_peek_status(qm_timer_mgr_t timer, qm_timer_mgr_status_t *status);

/**
 * @brief Check whether the timer is running or paused.
 *
 * @param[in] timer Timer handle.
 *
 * @return 1 when active, 0 otherwise.
 */
int qm_timer_mgr_is_active(qm_timer_mgr_t timer);

/**
 * @brief Attach the optional cloud adapter to an existing timer.
 *
 * The configuration is borrowed and must remain valid until destroy(). This
 * can be called once when the timer was created before the cloud session.
 */
int qm_timer_mgr_cloud_attach(qm_timer_mgr_t timer,
                              const qm_timer_mgr_cloud_config_t *config);

/**
 * @brief Mode name for logging.
 *
 * @param[in] mode Timer mode.
 *
 * @return Static string.
 */
const char *qm_timer_mgr_mode_str(qm_timer_mgr_mode_t mode);

/**
 * @brief State name for logging.
 *
 * @param[in] state Timer state.
 *
 * @return Static string.
 */
const char *qm_timer_mgr_state_str(qm_timer_mgr_state_t state);

/**
 * @brief Return non-zero while cloud commands are queued or being handled.
 *
 * @param[in] timer Timer handle.
 *
 * @return 1 while pending, 0 otherwise.
 */
int qm_timer_mgr_cloud_command_pending(qm_timer_mgr_t timer);

#ifdef __cplusplus
}
#endif

#endif /* QM_TIMER_MGR_H */
