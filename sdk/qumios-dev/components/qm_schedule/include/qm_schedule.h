#ifndef QM_SCHEDULE_H
#define QM_SCHEDULE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_errno.h"
#include "qm_types.h"

typedef struct qm_schedule *qm_schedule_t;

#ifndef CONFIG_QM_SCHEDULE_ID_MAX_LEN
#define CONFIG_QM_SCHEDULE_ID_MAX_LEN       64u
#endif

#ifndef CONFIG_QM_SCHEDULE_TITLE_MAX_LEN
#define CONFIG_QM_SCHEDULE_TITLE_MAX_LEN    64u
#endif

#define QM_SCHEDULE_ID_MAX_DEFAULT          CONFIG_QM_SCHEDULE_ID_MAX_LEN
#define QM_SCHEDULE_TITLE_MAX_DEFAULT       CONFIG_QM_SCHEDULE_TITLE_MAX_LEN
#define QM_SCHEDULE_TIMEZONE_MAX_DEFAULT    64u
#define QM_SCHEDULE_REMINDER_ID_MAX_DEFAULT 32u
#define QM_SCHEDULE_OPERATION_ID_MAX_DEFAULT 64u
#define QM_SCHEDULE_EVENT_ID_MAX_DEFAULT    64u
#define QM_SCHEDULE_BINDING_VERSION_MAX_DEFAULT 64u
#define QM_SCHEDULE_TYPE_MAX_DEFAULT            32u
#define QM_SCHEDULE_SOURCE_MAX_DEFAULT          32u
#define QM_SCHEDULE_GROUP_ID_MAX_DEFAULT        64u
#define QM_SCHEDULE_DESCRIPTION_MAX_DEFAULT    512u
#define QM_SCHEDULE_LOCATION_MAX_DEFAULT       128u
#define QM_SCHEDULE_PRIORITY_MAX_DEFAULT        16u
#define QM_SCHEDULE_ACTION_MAX_DEFAULT          16u
#define QM_SCHEDULE_CATEGORY_MAX_DEFAULT        16u
#define QM_SCHEDULE_SENSITIVITY_MAX_DEFAULT     16u
#define QM_SCHEDULE_STATUS_MAX_DEFAULT           16u
#define QM_SCHEDULE_MISSED_POLICY_MAX_DEFAULT   16u
#define QM_SCHEDULE_PROJECTION_KEY_MAX_DEFAULT  64u
#define QM_SCHEDULE_DATE_MAX_DEFAULT             11u
#define QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT     1024u
#define QM_SCHEDULE_CHANNELS_MAX_DEFAULT       128u
#define QM_SCHEDULE_LABEL_MAX_DEFAULT          QM_SCHEDULE_PRIORITY_MAX_DEFAULT
#define QM_SCHEDULE_MAX_REMINDERS_DEFAULT       3u

/** Component-specific errors are returned as negative values. */
typedef enum {
    QM_SCHEDULE_E_ALREADY_EXISTS = 1001,
    QM_SCHEDULE_E_REVISION_CONFLICT,
    QM_SCHEDULE_E_SYNC_CONFLICT,
    QM_SCHEDULE_E_SYNC_INCOMPLETE,
    QM_SCHEDULE_E_BINDING_INVALID,
    QM_SCHEDULE_E_INVALID_TIMEZONE,
    QM_SCHEDULE_E_OCCURRENCE_NOT_ACTIVE,
    QM_SCHEDULE_E_SNOOZE_NOT_ALLOWED,
    QM_SCHEDULE_E_CAPABILITY_UNSUPPORTED,
    QM_SCHEDULE_E_OUTBOX_FULL,
    QM_SCHEDULE_E_UNSUPPORTED_SCHEMA,
} qm_schedule_error_t;

typedef enum {
    QM_SCHEDULE_RECURRENCE_ONCE = 0,
    QM_SCHEDULE_RECURRENCE_DAILY,
    QM_SCHEDULE_RECURRENCE_WEEKLY,
} qm_schedule_recurrence_t;

typedef enum {
    QM_SCHEDULE_ACTION_ACKNOWLEDGE = 0,
    QM_SCHEDULE_ACTION_COMPLETE,
    QM_SCHEDULE_ACTION_SNOOZE,
    QM_SCHEDULE_ACTION_DISMISS,
} qm_schedule_action_type_t;

typedef enum {
    QM_SCHEDULE_SYNC_NOT_REQUIRED = 0,
    QM_SCHEDULE_SYNC_PENDING,
    QM_SCHEDULE_SYNC_SYNCING,
    QM_SCHEDULE_SYNC_SYNCED,
    QM_SCHEDULE_SYNC_FAILED,
} qm_schedule_sync_state_t;

typedef enum {
    QM_SCHEDULE_CLOSE_USER_CLOSED = 0,
    QM_SCHEDULE_CLOSE_EXPIRED,
    QM_SCHEDULE_CLOSE_CANCELLED,
    QM_SCHEDULE_CLOSE_OCCURRENCE_COMPLETED,
} qm_schedule_close_reason_t;

typedef enum {
    QM_SCHEDULE_REMINDER_PENDING = 0,
    QM_SCHEDULE_REMINDER_ACTIVE,
    QM_SCHEDULE_REMINDER_ACKNOWLEDGED,
    QM_SCHEDULE_REMINDER_SNOOZED,
    QM_SCHEDULE_REMINDER_CLOSED,
    QM_SCHEDULE_REMINDER_CANCELLED,
} qm_schedule_reminder_state_t;

typedef enum {
    QM_SCHEDULE_OCCURRENCE_PENDING = 0,
    QM_SCHEDULE_OCCURRENCE_COMPLETED,
    QM_SCHEDULE_OCCURRENCE_CANCELLED,
    QM_SCHEDULE_OCCURRENCE_EXPIRED,
} qm_schedule_occurrence_state_t;

typedef struct {
    const char *binding_version;
    int64_t lease_expires_at;
    const char *capability_version;
} qm_schedule_binding_t;

typedef struct {
    const char *operation_id;
    const char *binding_version;
    uint32_t desired_revision;
} qm_schedule_apply_ctx_t;

typedef struct {
    const char *schedule_id;
    int64_t occurrence_at;
    const char *occurrence_id;
    const char *reminder_id;
    int64_t action_at;
    const char *operation_id;
    const char *event_id;
    const char *binding_version;
} qm_schedule_action_t;

typedef struct {
    const char *id;
    int32_t offset_min;
    const char *action;
    const char *channels_json;
    bool requires_ack;
    bool snooze_enabled;
    uint16_t snooze_interval_min;
    uint8_t snooze_max_count;
} qm_schedule_reminder_t;

typedef struct {
    const char *id;
    uint32_t revision;
    const char *type;
    const char *source;
    const char *group_id;
    const char *title;
    const char *description;
    const char *location;
    const char *priority;
    const char *category;
    const char *sensitivity;
    int64_t start_at;
    int64_t end_at;
    const char *timezone;
    bool all_day;
    qm_schedule_recurrence_t recurrence;
    uint8_t weekly_mask;
    const char *recurrence_start_date;
    const char *recurrence_end_date;
    uint16_t recurrence_interval;
    int64_t recurrence_until;
    const char *status;
    const char *missed_policy;
    const char *projection_key;
    const char *extensions_json;
    const qm_schedule_reminder_t *reminders;
    uint8_t reminder_count;
    bool enabled;
} qm_schedule_item_t;

typedef struct {
    char id[QM_SCHEDULE_ID_MAX_DEFAULT];
    int32_t offset_min;
    char action[QM_SCHEDULE_ACTION_MAX_DEFAULT];
    char channels_json[QM_SCHEDULE_CHANNELS_MAX_DEFAULT];
    bool requires_ack;
    bool snooze_enabled;
    uint16_t snooze_interval_min;
    uint8_t snooze_max_count;
} qm_schedule_reminder_snapshot_t;

typedef struct {
    char id[QM_SCHEDULE_ID_MAX_DEFAULT];
    uint32_t revision;
    char type[QM_SCHEDULE_TYPE_MAX_DEFAULT];
    char source[QM_SCHEDULE_SOURCE_MAX_DEFAULT];
    char group_id[QM_SCHEDULE_GROUP_ID_MAX_DEFAULT];
    char title[QM_SCHEDULE_TITLE_MAX_DEFAULT];
    char description[QM_SCHEDULE_DESCRIPTION_MAX_DEFAULT];
    char location[QM_SCHEDULE_LOCATION_MAX_DEFAULT];
    char priority[QM_SCHEDULE_PRIORITY_MAX_DEFAULT];
    char category[QM_SCHEDULE_CATEGORY_MAX_DEFAULT];
    char sensitivity[QM_SCHEDULE_SENSITIVITY_MAX_DEFAULT];
    int64_t start_at;
    int64_t end_at;
    char timezone[QM_SCHEDULE_TIMEZONE_MAX_DEFAULT];
    bool all_day;
    qm_schedule_recurrence_t recurrence;
    uint8_t weekly_mask;
    char recurrence_start_date[QM_SCHEDULE_DATE_MAX_DEFAULT];
    char recurrence_end_date[QM_SCHEDULE_DATE_MAX_DEFAULT];
    uint16_t recurrence_interval;
    int64_t recurrence_until;
    char status[QM_SCHEDULE_STATUS_MAX_DEFAULT];
    char missed_policy[QM_SCHEDULE_MISSED_POLICY_MAX_DEFAULT];
    char projection_key[QM_SCHEDULE_PROJECTION_KEY_MAX_DEFAULT];
    char extensions_json[QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT];
    qm_schedule_reminder_snapshot_t reminders[QM_SCHEDULE_MAX_REMINDERS_DEFAULT];
    uint8_t reminder_count;
    bool enabled;
} qm_schedule_item_snapshot_t;

typedef struct {
    const char *schedule_id;
    int64_t occurrence_at;
    const char *reminder_id;
    qm_schedule_occurrence_state_t occurrence_state;
    qm_schedule_reminder_state_t reminder_state;
    qm_schedule_close_reason_t close_reason;
    int64_t event_at;
} qm_schedule_notify_t;

typedef void (*qm_schedule_notify_cb_t)(qm_schedule_t schedule,
                                        const qm_schedule_notify_t *event,
                                        void *user_ctx);

typedef struct {
    uint16_t max_items;
    uint8_t max_reminders_per_item;
    uint16_t max_outbox_user;
    uint16_t max_outbox_system;
    qm_schedule_notify_cb_t notify_cb;
    void *user_ctx;
} qm_schedule_config_t;

typedef struct {
    qm_schedule_sync_state_t sync_state;
    uint32_t desired_revision;
    uint32_t confirmed_revision;
    char binding_version[QM_SCHEDULE_BINDING_VERSION_MAX_DEFAULT];
    bool running;
    bool schedulable;
} qm_schedule_status_t;

typedef struct {
    uint16_t max_items;
    uint8_t max_reminders_per_item;
    uint16_t max_outbox_user;
    uint16_t max_outbox_system;
    uint16_t max_sync_chunks;
    uint16_t max_sync_items_per_chunk;
    uint32_t max_payload_len;
    bool supports_fixed_offset_timezone;
    bool supports_iana_timezone;
    bool supports_double_bank_store;
    bool supports_paged_store;
} qm_schedule_capability_t;

qm_schedule_t qm_schedule_create(const qm_schedule_config_t *config);
void qm_schedule_destroy(qm_schedule_t schedule);

int qm_schedule_start(qm_schedule_t schedule);
int qm_schedule_stop(qm_schedule_t schedule);

int qm_schedule_apply_binding(qm_schedule_t schedule,
                              const qm_schedule_binding_t *binding);
int qm_schedule_invalidate_binding(qm_schedule_t schedule,
                                   const char *binding_version);

int qm_schedule_create_item(qm_schedule_t schedule,
                            const qm_schedule_item_t *item,
                            const qm_schedule_apply_ctx_t *ctx);
int qm_schedule_modify_item(qm_schedule_t schedule,
                            const qm_schedule_item_t *item,
                            const qm_schedule_apply_ctx_t *ctx);
int qm_schedule_delete_item(qm_schedule_t schedule,
                            const char *schedule_id,
                            uint32_t revision,
                            const qm_schedule_apply_ctx_t *ctx);
int qm_schedule_enable_item(qm_schedule_t schedule,
                            const char *schedule_id,
                            uint32_t revision,
                            bool enabled,
                            const qm_schedule_apply_ctx_t *ctx);
int qm_schedule_apply_sync(qm_schedule_t schedule,
                           const qm_schedule_item_t *items,
                           uint16_t item_count,
                           bool replace,
                           const qm_schedule_apply_ctx_t *ctx);

int qm_schedule_acknowledge(qm_schedule_t schedule,
                            const qm_schedule_action_t *action);
int qm_schedule_complete(qm_schedule_t schedule,
                         const qm_schedule_action_t *action);
int qm_schedule_snooze(qm_schedule_t schedule,
                       const qm_schedule_action_t *action,
                       uint16_t duration_min);
int qm_schedule_dismiss(qm_schedule_t schedule,
                        const qm_schedule_action_t *action,
                        qm_schedule_close_reason_t reason);

int qm_schedule_get_status(qm_schedule_t schedule,
                           qm_schedule_status_t *status);
int qm_schedule_get_item(qm_schedule_t schedule,
                         const char *schedule_id,
                         qm_schedule_item_snapshot_t *item);
int qm_schedule_list_items(qm_schedule_t schedule,
                           qm_schedule_item_snapshot_t *items,
                           uint16_t capacity,
                           uint16_t *count);
int qm_schedule_get_capability(qm_schedule_t schedule,
                               qm_schedule_capability_t *capability);
int qm_schedule_request_sync(qm_schedule_t schedule,
                             const char *event_id,
                             int64_t event_at);
int qm_schedule_cleanup(qm_schedule_t schedule,
                        int64_t now_utc);

#ifdef __cplusplus
}
#endif

#endif /* QM_SCHEDULE_H */
