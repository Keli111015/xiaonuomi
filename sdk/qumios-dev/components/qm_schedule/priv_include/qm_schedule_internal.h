#ifndef QM_SCHEDULE_INTERNAL_H
#define QM_SCHEDULE_INTERNAL_H

#include "qm_schedule.h"

#include "qm_kernel.h"

#define QM_SCHEDULE_BINDING_VERSION_MAX 64u
#define QM_SCHEDULE_CAPABILITY_VERSION_MAX 64u
#define QM_SCHEDULE_STORE_MAGIC 0x51534348u
#define QM_SCHEDULE_STORE_VERSION 2u
#define QM_SCHEDULE_MAX_TYPE_LEN 32u
#define QM_SCHEDULE_MAX_SOURCE_LEN 32u
#define QM_SCHEDULE_MAX_ACTION_LEN QM_SCHEDULE_ACTION_MAX_DEFAULT
#define QM_SCHEDULE_MAX_ITEMS_INTERNAL 100u
#define QM_SCHEDULE_MAX_REMINDERS_INTERNAL 3u
#define QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL 128u
#define QM_SCHEDULE_MAX_OUTBOX_INTERNAL 132u
#define QM_SCHEDULE_MAX_TOMBSTONES_INTERNAL 64u
#ifndef CONFIG_QM_SCHEDULE_SYNC_MAX_CHUNKS
#define CONFIG_QM_SCHEDULE_SYNC_MAX_CHUNKS 32
#endif
#define QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL CONFIG_QM_SCHEDULE_SYNC_MAX_CHUNKS
#ifndef CONFIG_QM_SCHEDULE_SYNC_MAX_ITEMS_PER_CHUNK
#define CONFIG_QM_SCHEDULE_SYNC_MAX_ITEMS_PER_CHUNK 20
#endif
#define QM_SCHEDULE_MAX_SYNC_ITEMS_PER_CHUNK_INTERNAL \
    CONFIG_QM_SCHEDULE_SYNC_MAX_ITEMS_PER_CHUNK
#define QM_SCHEDULE_SYNC_ID_MAX 64u
#define QM_SCHEDULE_DIGEST_MAX 72u
#define QM_SCHEDULE_MAX_GROUPS_INTERNAL 32u
#define QM_SCHEDULE_MIN_TIMER_MS 1000u
#define QM_SCHEDULE_MAX_TIMER_MS 2147483647u
#define QM_SCHEDULE_SEC_PER_DAY 86400
#define QM_SCHEDULE_SEC_PER_WEEK 604800

#ifndef CONFIG_QM_SCHEDULE_DEFAULT_MAX_ITEMS
#define CONFIG_QM_SCHEDULE_DEFAULT_MAX_ITEMS 100
#endif

#ifndef CONFIG_QM_SCHEDULE_MAX_REMINDERS_PER_ITEM
#define CONFIG_QM_SCHEDULE_MAX_REMINDERS_PER_ITEM 3
#endif

#ifndef CONFIG_QM_SCHEDULE_LOCAL_REPORT_OUTBOX_SIZE
#define CONFIG_QM_SCHEDULE_LOCAL_REPORT_OUTBOX_SIZE 100
#endif

#ifndef CONFIG_QM_SCHEDULE_IOT_EVENT_OUTBOX_SIZE
#define CONFIG_QM_SCHEDULE_IOT_EVENT_OUTBOX_SIZE 32
#endif

#ifndef CONFIG_QM_SCHEDULE_SNOOZE_MAX_COUNT
#define CONFIG_QM_SCHEDULE_SNOOZE_MAX_COUNT 3
#endif

#ifndef CONFIG_QM_SCHEDULE_MISSED_WINDOW_SEC
#define CONFIG_QM_SCHEDULE_MISSED_WINDOW_SEC 1800
#endif

#ifndef CONFIG_QM_SCHEDULE_IOT_MAX_PAYLOAD_LEN
#define CONFIG_QM_SCHEDULE_IOT_MAX_PAYLOAD_LEN 8192
#endif

#ifndef CONFIG_QM_SCHEDULE_TOMBSTONE_RETENTION_SEC
#define CONFIG_QM_SCHEDULE_TOMBSTONE_RETENTION_SEC 604800
#endif

#ifndef CONFIG_QM_SCHEDULE_SYNC_STAGING_TIMEOUT_SEC
#define CONFIG_QM_SCHEDULE_SYNC_STAGING_TIMEOUT_SEC 600
#endif

#ifndef CONFIG_QM_SCHEDULE_SNOOZE_RECOVERY_WINDOW_SEC
#define CONFIG_QM_SCHEDULE_SNOOZE_RECOVERY_WINDOW_SEC 900
#endif

#ifndef CONFIG_QM_SCHEDULE_OCCURRENCE_RETENTION_SEC
#define CONFIG_QM_SCHEDULE_OCCURRENCE_RETENTION_SEC 2592000
#endif

#ifndef CONFIG_QM_SCHEDULE_MAX_QUERY_PAGE
#define CONFIG_QM_SCHEDULE_MAX_QUERY_PAGE 100
#endif

#ifndef CONFIG_QM_SCHEDULE_OUTBOX_MAX_RETRIES
#define CONFIG_QM_SCHEDULE_OUTBOX_MAX_RETRIES 10
#endif

#ifndef CONFIG_QM_SCHEDULE_OUTBOX_ACK_TIMEOUT_SEC
#define CONFIG_QM_SCHEDULE_OUTBOX_ACK_TIMEOUT_SEC 30
#endif

#ifndef CONFIG_QM_SCHEDULE_BINDING_LEASE_SEC
#define CONFIG_QM_SCHEDULE_BINDING_LEASE_SEC 86400
#endif

typedef enum {
    QM_SCHEDULE_OUTBOX_USER_ACTION = 0,
    QM_SCHEDULE_OUTBOX_SYSTEM_TRIGGERED,
    QM_SCHEDULE_OUTBOX_SYSTEM_SYNC_REQUEST,
    QM_SCHEDULE_OUTBOX_SYSTEM_BINDING_INVALIDATED,
    QM_SCHEDULE_OUTBOX_SYSTEM_CAPACITY_EXCEEDED,
} qm_schedule_outbox_type_t;

typedef struct {
    uint32_t format_version;
    uint32_t active_generation;
    uint32_t previous_generation;
    uint32_t desired_revision;
    uint32_t confirmed_revision;
    qm_schedule_sync_state_t sync_state;
    char binding_version[QM_SCHEDULE_BINDING_VERSION_MAX];
    char capability_version[QM_SCHEDULE_CAPABILITY_VERSION_MAX];
    char last_operation_id[QM_SCHEDULE_OPERATION_ID_MAX_DEFAULT];
    int64_t lease_expires_at;
    bool recovery_pending;
} qm_schedule_store_meta_t;

typedef struct {
    char id[QM_SCHEDULE_REMINDER_ID_MAX_DEFAULT];
    int32_t offset_min;
    char action[QM_SCHEDULE_MAX_ACTION_LEN];
    char channels_json[QM_SCHEDULE_CHANNELS_MAX_DEFAULT];
    bool requires_ack;
    bool snooze_enabled;
    uint16_t snooze_interval_min;
    uint8_t snooze_max_count;
} qm_schedule_reminder_record_t;

typedef struct {
    bool used;
    char id[QM_SCHEDULE_ID_MAX_DEFAULT];
    uint32_t revision;
    char type[QM_SCHEDULE_MAX_TYPE_LEN];
    char source[QM_SCHEDULE_MAX_SOURCE_LEN];
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
    qm_schedule_reminder_record_t reminders[QM_SCHEDULE_MAX_REMINDERS_INTERNAL];
    uint8_t reminder_count;
    bool enabled;
} qm_schedule_item_record_t;

typedef struct {
    bool used;
    char schedule_id[QM_SCHEDULE_ID_MAX_DEFAULT];
    int64_t occurrence_at;
    char reminder_id[QM_SCHEDULE_REMINDER_ID_MAX_DEFAULT];
    qm_schedule_occurrence_state_t occurrence_state;
    qm_schedule_reminder_state_t reminder_state;
    qm_schedule_close_reason_t close_reason;
    uint8_t snooze_count;
    int64_t snooze_until;
    int64_t action_at;
    char last_operation_id[QM_SCHEDULE_OPERATION_ID_MAX_DEFAULT];
    qm_schedule_action_type_t last_action_type;
} qm_schedule_occurrence_record_t;

typedef struct {
    bool used;
    qm_schedule_outbox_type_t event_type;
    char event_id[QM_SCHEDULE_EVENT_ID_MAX_DEFAULT];
    char binding_version[QM_SCHEDULE_BINDING_VERSION_MAX];
    char schedule_id[QM_SCHEDULE_ID_MAX_DEFAULT];
    uint32_t revision;
    int64_t occurrence_at;
    char reminder_id[QM_SCHEDULE_REMINDER_ID_MAX_DEFAULT];
    qm_schedule_action_type_t action_type;
    int64_t action_at;
    int64_t event_at;
    int64_t snooze_until;
    uint8_t snooze_count;
    qm_schedule_close_reason_t close_reason;
    char reason[QM_SCHEDULE_LABEL_MAX_DEFAULT];
    uint32_t confirmed_revision;
    uint8_t retry_count;
    bool awaiting_ack;
    int64_t next_attempt_at;
} qm_schedule_outbox_record_t;

typedef struct {
    bool used;
    char group_id[QM_SCHEDULE_GROUP_ID_MAX_DEFAULT];
    uint32_t revision;
} qm_schedule_group_record_t;

typedef struct {
    bool used;
    char schedule_id[QM_SCHEDULE_ID_MAX_DEFAULT];
    uint32_t revision;
    int64_t deleted_at;
} qm_schedule_tombstone_record_t;

typedef struct {
    qm_schedule_store_meta_t meta;
    qm_schedule_item_record_t items[QM_SCHEDULE_MAX_ITEMS_INTERNAL];
    qm_schedule_item_record_t staged_items[QM_SCHEDULE_MAX_ITEMS_INTERNAL];
    qm_schedule_occurrence_record_t occurrences[QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL];
    qm_schedule_outbox_record_t outbox[QM_SCHEDULE_MAX_OUTBOX_INTERNAL];
    qm_schedule_tombstone_record_t tombstones[QM_SCHEDULE_MAX_TOMBSTONES_INTERNAL];
    qm_schedule_group_record_t groups[QM_SCHEDULE_MAX_GROUPS_INTERNAL];
    bool staging_active;
    bool staging_replace;
    bool staging_full_projection;
    char staging_sync_id[QM_SCHEDULE_SYNC_ID_MAX];
    char staging_group_id[QM_SCHEDULE_GROUP_ID_MAX_DEFAULT];
    uint32_t staging_group_revision;
    qm_schedule_group_record_t staging_groups[QM_SCHEDULE_MAX_GROUPS_INTERNAL];
    uint16_t staging_group_count;
    uint16_t staging_total_chunks;
    uint16_t staging_total_items;
    uint32_t staging_received_mask;
    char staging_chunk_digests[QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL]
                              [QM_SCHEDULE_DIGEST_MAX];
    uint16_t staging_chunk_item_counts[QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL];
    uint16_t staging_item_count;
    int64_t staging_started_at;
    char last_sync_id[QM_SCHEDULE_SYNC_ID_MAX];
    uint16_t last_sync_chunk_count;
    uint32_t last_sync_received_mask;
    char last_sync_chunk_digests[QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL]
                                [QM_SCHEDULE_DIGEST_MAX];
} qm_schedule_store_t;

struct qm_schedule {
    qm_schedule_config_t config;
    qm_mutex_t lock;
    qm_timer_t timer;
    bool lock_valid;
    bool timer_valid;
    bool timer_armed;
    bool running;
    bool schedulable;
    bool next_is_snooze;
    bool destroying;
    int64_t next_trigger_at;
    uint16_t next_item_index;
    uint8_t next_reminder_index;
    uint16_t next_occurrence_index;
    int64_t last_trigger_at;
    uint8_t active_bank;
    uint32_t store_sequence;
    qm_schedule_store_t store;
};

int qm_schedule_store_init(qm_schedule_t schedule);
void qm_schedule_store_deinit(qm_schedule_t schedule);
int qm_schedule_store_recover(qm_schedule_t schedule);
int qm_schedule_store_load_meta(qm_schedule_t schedule,
                                qm_schedule_store_meta_t *meta);
int qm_schedule_store_save_meta(qm_schedule_t schedule,
                                const qm_schedule_store_meta_t *meta);
int qm_schedule_store_save_all(qm_schedule_t schedule);

bool qm_schedule_binding_valid(qm_schedule_t schedule, int64_t now_utc);
int qm_schedule_expire_binding_if_needed(qm_schedule_t schedule,
                                         int64_t now_utc);

int qm_schedule_scheduler_start(qm_schedule_t schedule);
int qm_schedule_scheduler_stop(qm_schedule_t schedule);
int qm_schedule_scheduler_rebuild(qm_schedule_t schedule);
void qm_schedule_scheduler_fire(qm_schedule_t schedule);
void qm_schedule_scheduler_timer_cb(qm_timer_t *timer, void *arg);

int qm_schedule_recurrence_next(const qm_schedule_item_t *item,
                                int64_t after_utc,
                                int64_t *next_utc);
int qm_schedule_recurrence_next_record(const qm_schedule_item_record_t *item,
                                       int64_t after_utc,
                                       int64_t *next_utc);
bool qm_schedule_timezone_supported(const char *timezone);
int qm_schedule_recurrence_date_to_utc(const char *date,
                                       const char *timezone,
                                       bool end_of_day,
                                       int64_t *utc);

int qm_schedule_sync_stage_begin(qm_schedule_t schedule,
                                 const char *sync_id,
                                 uint16_t total_chunks,
                                 bool replace,
                                 const qm_schedule_apply_ctx_t *ctx);
int qm_schedule_sync_stage_items(qm_schedule_t schedule,
                                 const char *sync_id,
                                 uint16_t chunk_index,
                                 const qm_schedule_item_t *items,
                                 uint16_t item_count);
int qm_schedule_sync_stage_commit(qm_schedule_t schedule,
                                  const char *sync_id,
                                  const qm_schedule_apply_ctx_t *ctx);
int qm_schedule_sync_stage_abort(qm_schedule_t schedule,
                                 const char *sync_id);

typedef struct {
    const char *sync_id;
    bool full_projection;
    const char *group_id;
    uint32_t group_revision;
    const qm_schedule_group_record_t *groups;
    uint16_t group_count;
    bool replace;
    uint16_t chunk_index;
    uint16_t chunk_count;
    uint16_t total_items;
    const char *content_digest;
} qm_schedule_sync_chunk_t;

int qm_schedule_sync_apply_chunk(qm_schedule_t schedule,
                                 const qm_schedule_sync_chunk_t *chunk,
                                 const qm_schedule_item_t *items,
                                 uint16_t item_count,
                                 const qm_schedule_apply_ctx_t *ctx,
                                 bool *complete,
                                 bool *idempotent);

#endif /* QM_SCHEDULE_INTERNAL_H */
