#ifndef QM_ALARM_H
#define QM_ALARM_H

#include "qm_errno.h"
#include "qm_types.h"
#include "qm_time.h"
#include "qm_alarm_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Cloud method used by alarm commands and full-list reports. */
#define QM_ALARM_METHOD "alarm"

/** @brief Cloud method used by alarm command acknowledgements. */
#define QM_ALARM_REPLY_METHOD "alarmReply"

/** @brief MQTT subMethod used to create one alarm. */
#define QM_ALARM_SUBMETHOD_CREATE "create"

/** @brief MQTT subMethod used to modify one alarm. */
#define QM_ALARM_SUBMETHOD_MODIFY "modify"

/** @brief MQTT subMethod used to delete one alarm. */
#define QM_ALARM_SUBMETHOD_DELETE "delete"

/** @brief MQTT subMethod used to enable or disable one alarm. */
#define QM_ALARM_SUBMETHOD_ENABLE "enable"

/** @brief MQTT subMethod used to report the full alarm list. */
#define QM_ALARM_SUBMETHOD_UPDATE "update"

/** @brief MQTT subMethod used to close the currently active alarm. */
#define QM_ALARM_SUBMETHOD_CLOSE "close"

/** @brief MQTT subMethod used to snooze the currently active alarm. */
#define QM_ALARM_SUBMETHOD_SNOOZE "snooze"

/** @brief Device-to-cloud runtime state report submethod. */
#define QM_ALARM_SUBMETHOD_STATE_CHANGE "stateChange"

/** @brief Opaque standalone alarm handle. */
typedef struct qm_alarm *qm_alarm_t;

/** @brief Runtime state of the current alarm flow. */
typedef enum {
    /** @brief No alarm is currently ringing or snoozing. */
    QM_ALARM_STATE_IDLE = 0,
    /** @brief The alarm is currently ringing. */
    QM_ALARM_STATE_RINGING,
    /** @brief The alarm is waiting for its snooze deadline. */
    QM_ALARM_STATE_SNOOZING,
} qm_alarm_state_t;

/** @brief Maximum alarm display name length including terminator. */
#define QM_ALARM_NAME_MAX_LEN 32U

/** @brief Maximum alarm source string length including terminator. */
#define QM_ALARM_SOURCE_MAX_LEN 8U

/** @brief Maximum online tone id string length including terminator. */
#define QM_ALARM_TONE_ONLINE_ID_MAX_LEN 32U

/** @brief Auto-assign one local device alarm id during create. */
#define QM_ALARM_LOCAL_ID_AUTO 0U

/** @brief Alarm source name for device-created alarms. */
#define QM_ALARM_SOURCE_DEVICE "device"

/** @brief Alarm source name for app-created alarms. */
#define QM_ALARM_SOURCE_APP "app"

/** @brief Repeat mask bit for Monday. */
#define QM_ALARM_REPEAT_MON 0x01U

/** @brief Repeat mask bit for Tuesday. */
#define QM_ALARM_REPEAT_TUE 0x02U

/** @brief Repeat mask bit for Wednesday. */
#define QM_ALARM_REPEAT_WED 0x04U

/** @brief Repeat mask bit for Thursday. */
#define QM_ALARM_REPEAT_THU 0x08U

/** @brief Repeat mask bit for Friday. */
#define QM_ALARM_REPEAT_FRI 0x10U

/** @brief Repeat mask bit for Saturday. */
#define QM_ALARM_REPEAT_SAT 0x20U

/** @brief Repeat mask bit for Sunday. */
#define QM_ALARM_REPEAT_SUN 0x40U

/** @brief Repeat mask for workdays from Monday to Friday. */
#define QM_ALARM_REPEAT_WORKDAY 0x1FU

/** @brief Repeat mask for weekend days. */
#define QM_ALARM_REPEAT_WEEKEND 0x60U

/** @brief Repeat mask for every day. */
#define QM_ALARM_REPEAT_EVERYDAY 0x7FU

/** @brief Alarm notification type emitted to the application. */
typedef enum {
    /** @brief Alarm is firing and should start ringing/actions. */
    QM_ALARM_NOTIFY_RING = 0,
    /** @brief Active alarm was dismissed. */
    QM_ALARM_NOTIFY_DISMISS,
    /** @brief Active alarm entered snooze. */
    QM_ALARM_NOTIFY_SNOOZE,
    /** @brief Alarm content changed and the new state should be observed. */
    QM_ALARM_NOTIFY_CHANGED,
} qm_alarm_notify_t;

/** @brief Alarm tone source type. */
typedef enum {
    /** @brief Play one local built-in tone selected by numeric id. */
    QM_ALARM_TONE_TYPE_LOCAL = 0,
    /** @brief Play one cloud-provided online tone selected by string id. */
    QM_ALARM_TONE_TYPE_ONLINE,
} qm_alarm_tone_type_t;

/** @brief Snooze configuration for one alarm. */
typedef struct {
    /** @brief Whether snooze is enabled for this alarm. */
    qm_bool enabled;
    /** @brief Snooze interval in minutes. */
    uint8_t interval_min;
    /** @brief Maximum number of allowed snooze operations. */
    uint8_t max_count;
} qm_alarm_snooze_t;

/** @brief Tone configuration for one alarm. */
typedef struct {
    /** @brief Tone source type. */
    qm_alarm_tone_type_t type;
    /** @brief Local tone id, valid when type is QM_ALARM_TONE_TYPE_LOCAL. */
    uint8_t local_id;
    /**
     * @brief Online tone id string, valid when type is QM_ALARM_TONE_TYPE_ONLINE.
     *
     * The value is reported to and accepted from the cloud as-is.
     */
    char online_id[QM_ALARM_TONE_ONLINE_ID_MAX_LEN];
} qm_alarm_tone_t;

/** @brief One alarm item snapshot. */
typedef struct {
    /**
     * @brief Alarm id in range 1 to 255.
     *
     * For local device create requests, set this to QM_ALARM_LOCAL_ID_AUTO to
     * allocate the first free device-side id automatically.
     */
    uint8_t alarm_id;
    /** @brief Alarm source, for example `device` or `app`. */
    char source[QM_ALARM_SOURCE_MAX_LEN];
    /** @brief Alarm display name. */
    char name[QM_ALARM_NAME_MAX_LEN];
    /** @brief Hour in 24-hour format, range 0 to 23. */
    uint8_t hour;
    /** @brief Minute, range 0 to 59. */
    uint8_t minute;
    /** @brief Whether the alarm is enabled. */
    qm_bool enabled;
    /** @brief Repeat mask using QM_ALARM_REPEAT_* bits. */
    uint8_t repeat_mask;
    /** @brief Tone selection reported to the action callback. */
    qm_alarm_tone_t tone;
    /** @brief Whether vibration is enabled. */
    qm_bool vibrate;
    /** @brief Snooze behavior for this alarm. */
    qm_alarm_snooze_t snooze;
    /**
     * @brief Optional application-defined extensions object JSON.
     *
     * The value must point to a JSON object string, for example "{}".
     * The alarm module stores and reports it without parsing scenario-specific
     * fields. For create and modify APIs this pointer is borrowed and copied.
     */
    const char *extensions;
} qm_alarm_item_t;

/**
 * @brief Alarm notify callback.
 *
 * The callback runs on the alarm owner task. It must not synchronously call
 * another qm_alarm API; queue application work and return instead.
 *
 * @param alarm Alarm runtime handle.
 * @param notify Notification type.
 * @param item Alarm item related to the notification. For delete, this is the
 *             removed alarm snapshot before deletion. `extensions` is read-only
 *             borrowed data and valid only during the callback.
 * @param user_ctx User context from qm_alarm_config_t.
 */
typedef void (*qm_alarm_notify_cb_t)(qm_alarm_t alarm,
                                     qm_alarm_notify_t notify,
                                     const qm_alarm_item_t *item,
                                     void *user_ctx);

/** @brief Standalone alarm runtime configuration. */
typedef struct {
    /** @brief Existing `qm_iot` session handle. NULL disables cloud sync. */
    void *iot_handle;
    /**
     * @brief Maximum number of alarms kept in memory.
     *
     * Zero uses `CONFIG_QM_ALARM_MAX_ALARMS`. Non-zero values must not exceed
     * that compile-time limit.
     */
    uint16_t max_alarms;
    /** @brief Optional notify callback for ring, dismiss, snooze, and changed. */
    qm_alarm_notify_cb_t notify_cb;
    /** @brief User context passed to the notify callback. */
    void *user_ctx;
} qm_alarm_config_t;

/**
 * @brief Create an alarm runtime.
 *
 * @param[in] config Alarm configuration. NULL uses component defaults.
 * @return Alarm handle on success, NULL on failure.
 */
qm_alarm_t qm_alarm_create(const qm_alarm_config_t *config);

/** @brief Get the component-wide qm_alarm log mask. */

/**
 * @brief Set the component-wide qm_alarm log mask.
 *
 * @param log_flags Bitmask of qm_alarm_log_flags_t.
 * @return QM_EOK on success, or -QM_EINVAL for unsupported bits.
 */

/**
 * @brief Destroy an alarm runtime and all owned resources.
 *
 * @param[in] alarm Alarm runtime handle.
 */
void qm_alarm_destroy(qm_alarm_t alarm);

/**
 * @brief Start alarm scheduling and optional cloud registration.
 *
 * @param[in] alarm Alarm runtime handle.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_start(qm_alarm_t alarm);

/**
 * @brief Stop alarm scheduling and unregister cloud callbacks.
 *
 * @param[in] alarm Alarm runtime handle.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_stop(qm_alarm_t alarm);

/**
 * @brief Create one local alarm and report the full list when cloud is available.
 *
 * @param[in] alarm Alarm runtime handle.
 * @param[in] item Alarm item to create. The module copies borrowed fields.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_create_item(qm_alarm_t alarm, const qm_alarm_item_t *item);

/**
 * @brief Modify one existing local alarm.
 *
 * @param[in] alarm Alarm runtime handle.
 * @param[in] item Full alarm item carrying an existing `(source, alarm_id)`.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_modify_item(qm_alarm_t alarm, const qm_alarm_item_t *item);

/**
 * @brief Delete one local alarm.
 *
 * @param[in] alarm Alarm runtime handle.
 * @param[in] source Alarm source used together with `alarm_id` as the key.
 * @param[in] alarm_id Alarm id to delete.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_delete_item(qm_alarm_t alarm,
                         const char *source,
                         uint8_t alarm_id);

/**
 * @brief Enable or disable one local alarm.
 *
 * @param[in] alarm Alarm runtime handle.
 * @param[in] source Alarm source used together with `alarm_id` as the key.
 * @param[in] alarm_id Alarm id to update.
 * @param[in] enabled New enabled state.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_set_enabled(qm_alarm_t alarm,
                         const char *source,
                         uint8_t alarm_id,
                         qm_bool enabled);

/**
 * @brief Snooze the currently ringing alarm if allowed.
 *
 * @param[in] alarm Alarm runtime handle.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_snooze(qm_alarm_t alarm);

/**
 * @brief Cancel the currently pending snooze and return the alarm to idle.
 *
 * @param[in] alarm Alarm runtime handle.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_cancel_snooze(qm_alarm_t alarm);

/**
 * @brief Dismiss the currently ringing alarm.
 *
 * @param[in] alarm Alarm runtime handle.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_dismiss(qm_alarm_t alarm);

/**
 * @brief Get the latest runtime state and associated alarm id.
 *
 * @param[in] alarm Alarm runtime handle.
 * @param[out] state Latest runtime state.
 * @param[out] alarm_id Alarm id associated with the latest state, or 0 when
 *                      no alarm flow has started yet.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_get_state(qm_alarm_t alarm,
                       qm_alarm_state_t *state,
                       uint8_t *alarm_id);

/**
 * @brief Report the complete alarm list through the `alarm/update` method.
 *
 * @param[in] alarm Alarm runtime handle.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_report(qm_alarm_t alarm);

/**
 * @brief Copy the current alarm list.
 *
 * @param[in] alarm Alarm runtime handle.
 * @param[out] out Output array. `extensions` remains borrowed module-owned data.
 * @param[in] capacity Number of entries available in `out`.
 * @param[out] count Number of copied alarms.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_list(qm_alarm_t alarm,
                  qm_alarm_item_t *out,
                  uint16_t capacity,
                  uint16_t *count);

/**
 * @brief Get the next scheduled alarm and its trigger time.
 *
 * @param[in] alarm Alarm runtime handle.
 * @param[out] out Output alarm snapshot.
 * @param[out] trigger_time Absolute local trigger timestamp in seconds.
 * @return QM_EOK on success, negative `qm_errno` on failure.
 */
int qm_alarm_get_upcoming(qm_alarm_t alarm,
                          qm_alarm_item_t *out,
                          qm_time_t *trigger_time);

#ifdef __cplusplus
}
#endif

#endif /* QM_ALARM_H */
