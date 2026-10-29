#include "qm_schedule.h"

#include "qm_schedule_internal.h"

#include "cJSON.h"
#include "qm_time.h"

static uint16_t default_u16(uint16_t value, uint16_t fallback)
{
    return value ? value : fallback;
}

static uint8_t default_u8(uint8_t value, uint8_t fallback)
{
    return value ? value : fallback;
}

static bool str_empty(const char *value)
{
    return (!value || value[0] == '\0') ? true : false;
}

static bool str_too_long(const char *value, uint32_t max_len)
{
    return value && strlen(value) >= max_len;
}

static bool utf8_valid(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!text) return false;
    while (*p != '\0') {
        uint32_t codepoint;
        uint8_t length;

        if (*p < 0x80u) {
            p++;
            continue;
        }
        if ((*p & 0xe0u) == 0xc0u) {
            codepoint = *p & 0x1fu;
            length = 2;
        } else if ((*p & 0xf0u) == 0xe0u) {
            codepoint = *p & 0x0fu;
            length = 3;
        } else if ((*p & 0xf8u) == 0xf0u) {
            codepoint = *p & 0x07u;
            length = 4;
        } else {
            return false;
        }
        for (uint8_t i = 1; i < length; i++) {
            if ((p[i] & 0xc0u) != 0x80u) return false;
            codepoint = (codepoint << 6) | (p[i] & 0x3fu);
        }
        if ((length == 2 && codepoint < 0x80u) ||
            (length == 3 && codepoint < 0x800u) ||
            (length == 4 && codepoint < 0x10000u) ||
            codepoint > 0x10ffffu ||
            (codepoint >= 0xd800u && codepoint <= 0xdfffu)) {
            return false;
        }
        p += length;
    }
    return true;
}

static bool json_utf8_valid(const cJSON *item)
{
    const cJSON *child;

    if (!item) return true;
    if (cJSON_IsString(item)) return utf8_valid(item->valuestring);
    if (cJSON_IsObject(item)) {
        for (child = item->child; child; child = child->next) {
            if (!child->string || !utf8_valid(child->string) ||
                !json_utf8_valid(child)) {
                return false;
            }
        }
    } else if (cJSON_IsArray(item)) {
        for (child = item->child; child; child = child->next) {
            if (!json_utf8_valid(child)) return false;
        }
    }
    return true;
}

static bool optional_utf8(const char *value)
{
    return !value || utf8_valid(value);
}

static void copy_string(char *dst, uint32_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, (size_t)dst_len, "%s", src);
}

static bool string_in_set(const char *value,
                          const char *const *values,
                          uint8_t value_count)
{
    uint8_t i;

    if (str_empty(value)) {
        return true;
    }
    for (i = 0; i < value_count; i++) {
        if (strcmp(value, values[i]) == 0) {
            return true;
        }
    }
    return false;
}

static bool valid_json_type(const char *json, bool expect_object)
{
    cJSON *root;
    bool valid;

    if (str_empty(json)) {
        return true;
    }
    root = cJSON_ParseWithLength(json, strlen(json) + 1u);
    if (!root) {
        return false;
    }
    valid = (expect_object ? cJSON_IsObject(root) : cJSON_IsArray(root)) &&
        json_utf8_valid(root);
    cJSON_Delete(root);
    return valid;
}

static bool extensions_schema_valid(const char *json)
{
    cJSON *root;
    cJSON *child;

    if (str_empty(json)) return true;
    root = cJSON_ParseWithLength(json, strlen(json) + 1u);
    if (!root || !cJSON_IsObject(root) || !json_utf8_valid(root)) {
        if (root) cJSON_Delete(root);
        return false;
    }
    for (child = root->child; child; child = child->next) {
        cJSON *version = cJSON_GetObjectItemCaseSensitive(child,
                                                            "schemaVersion");
        if (!cJSON_IsObject(child) || !cJSON_IsNumber(version) ||
            !isfinite(version->valuedouble) || version->valuedouble < 1.0 ||
            floor(version->valuedouble) != version->valuedouble) {
            cJSON_Delete(root);
            return false;
        }
    }
    cJSON_Delete(root);
    return true;
}

static bool channels_schema_valid(const char *json)
{
    cJSON *root;
    cJSON *child;

    if (str_empty(json)) return true;
    root = cJSON_ParseWithLength(json, strlen(json) + 1u);
    if (!root || !cJSON_IsArray(root) || !json_utf8_valid(root)) {
        if (root) cJSON_Delete(root);
        return false;
    }
    for (child = root->child; child; child = child->next) {
        if (!cJSON_IsString(child)) {
            cJSON_Delete(root);
            return false;
        }
    }
    cJSON_Delete(root);
    return true;
}

static int64_t current_utc(void)
{
    return (int64_t)qm_time(NULL);
}

static int append_system_outbox_locked(qm_schedule_t schedule,
                                       qm_schedule_outbox_type_t event_type,
                                       const char *event_id,
                                       const char *binding_version,
                                       const char *schedule_id,
                                       int64_t event_at);

static void clear_binding_replica_locked(qm_schedule_t schedule,
                                         const char *binding_version,
                                         int64_t event_at)
{
    uint16_t i;

    for (i = 0; i < schedule->config.max_items; i++) {
        memset(&schedule->store.items[i], 0,
               sizeof(schedule->store.items[i]));
    }
    for (i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
        memset(&schedule->store.occurrences[i], 0,
               sizeof(schedule->store.occurrences[i]));
    }
    memset(schedule->store.staged_items, 0,
           sizeof(schedule->store.staged_items));
    memset(schedule->store.groups, 0, sizeof(schedule->store.groups));
    memset(schedule->store.staging_groups, 0,
           sizeof(schedule->store.staging_groups));
    memset(schedule->store.staging_chunk_digests, 0,
           sizeof(schedule->store.staging_chunk_digests));
    memset(schedule->store.staging_chunk_item_counts, 0,
           sizeof(schedule->store.staging_chunk_item_counts));
    schedule->store.staging_full_projection = false;
    schedule->store.staging_active = false;
    schedule->store.staging_replace = false;
    schedule->store.staging_sync_id[0] = '\0';
    schedule->store.staging_group_id[0] = '\0';
    schedule->store.staging_group_revision = 0;
    schedule->store.staging_group_count = 0;
    schedule->store.staging_total_chunks = 0;
    schedule->store.staging_total_items = 0;
    schedule->store.staging_received_mask = 0;
    schedule->store.staging_item_count = 0;
    schedule->store.staging_started_at = 0;
    schedule->store.last_sync_id[0] = '\0';
    schedule->store.last_sync_chunk_count = 0;
    schedule->store.last_sync_received_mask = 0;
    memset(schedule->store.last_sync_chunk_digests, 0,
           sizeof(schedule->store.last_sync_chunk_digests));
    memset(schedule->store.outbox, 0, sizeof(schedule->store.outbox));
    (void)append_system_outbox_locked(
        schedule, QM_SCHEDULE_OUTBOX_SYSTEM_BINDING_INVALIDATED, NULL,
        binding_version, NULL, event_at);
    schedule->store.meta.binding_version[0] = '\0';
    schedule->store.meta.capability_version[0] = '\0';
    schedule->store.meta.lease_expires_at = 0;
    schedule->store.meta.sync_state = QM_SCHEDULE_SYNC_PENDING;
    schedule->schedulable = false;
    schedule->next_is_snooze = false;
    schedule->next_trigger_at = 0;
}

bool qm_schedule_binding_valid(qm_schedule_t schedule, int64_t now_utc)
{
    bool valid;

    if (!schedule) {
        return false;
    }
    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    valid = schedule->store.meta.binding_version[0] != '\0' &&
        schedule->store.meta.lease_expires_at > now_utc;
    qm_mutex_unlock(&schedule->lock);
    return valid;
}

int qm_schedule_expire_binding_if_needed(qm_schedule_t schedule,
                                         int64_t now_utc)
{
    char binding_version[QM_SCHEDULE_BINDING_VERSION_MAX];
    bool expired = false;

    if (!schedule) {
        return -QM_EINVAL;
    }
    if (now_utc <= 0) {
        now_utc = current_utc();
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (schedule->store.meta.binding_version[0] != '\0' &&
        schedule->store.meta.lease_expires_at > 0 &&
        schedule->store.meta.lease_expires_at <= now_utc) {
        copy_string(binding_version, sizeof(binding_version),
                    schedule->store.meta.binding_version);
        clear_binding_replica_locked(schedule, binding_version, now_utc);
        expired = true;
    }
    qm_mutex_unlock(&schedule->lock);

    if (!expired) {
        return QM_EOK;
    }
    return qm_schedule_store_save_all(schedule);
}

static void normalize_config(qm_schedule_config_t *dst,
                             const qm_schedule_config_t *src)
{
    if (src) {
        *dst = *src;
    } else {
        memset(dst, 0, sizeof(*dst));
    }

    dst->max_items = default_u16(dst->max_items,
                                 (uint16_t)CONFIG_QM_SCHEDULE_DEFAULT_MAX_ITEMS);
    if (dst->max_items > QM_SCHEDULE_MAX_ITEMS_INTERNAL) {
        dst->max_items = QM_SCHEDULE_MAX_ITEMS_INTERNAL;
    }
    dst->max_reminders_per_item = default_u8(
        dst->max_reminders_per_item,
        (uint8_t)CONFIG_QM_SCHEDULE_MAX_REMINDERS_PER_ITEM);
    if (dst->max_reminders_per_item > QM_SCHEDULE_MAX_REMINDERS_INTERNAL) {
        dst->max_reminders_per_item = QM_SCHEDULE_MAX_REMINDERS_INTERNAL;
    }
    dst->max_outbox_user = default_u16(
        dst->max_outbox_user,
        (uint16_t)CONFIG_QM_SCHEDULE_LOCAL_REPORT_OUTBOX_SIZE);
    if (dst->max_outbox_user > QM_SCHEDULE_MAX_OUTBOX_INTERNAL) {
        dst->max_outbox_user = QM_SCHEDULE_MAX_OUTBOX_INTERNAL;
    }
    dst->max_outbox_system = default_u16(
        dst->max_outbox_system,
        (uint16_t)CONFIG_QM_SCHEDULE_IOT_EVENT_OUTBOX_SIZE);
    if (dst->max_outbox_system >
        (QM_SCHEDULE_MAX_OUTBOX_INTERNAL - dst->max_outbox_user)) {
        dst->max_outbox_system =
            QM_SCHEDULE_MAX_OUTBOX_INTERNAL - dst->max_outbox_user;
    }
}

static int enter(qm_schedule_t schedule)
{
    if (!schedule) {
        return -QM_EINVAL;
    }
    if (schedule->destroying) {
        return -QM_EBUSY;
    }
    return QM_EOK;
}

static int find_item_locked(qm_schedule_t schedule, const char *id)
{
    uint16_t i;

    for (i = 0; i < schedule->config.max_items; i++) {
        if (schedule->store.items[i].used &&
            strcmp(schedule->store.items[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int find_free_item_locked(qm_schedule_t schedule)
{
    uint16_t i;

    for (i = 0; i < schedule->config.max_items; i++) {
        if (!schedule->store.items[i].used) {
            return (int)i;
        }
    }
    return -1;
}

static const qm_schedule_reminder_record_t *find_reminder_locked(
    const qm_schedule_item_record_t *item,
    const char *reminder_id)
{
    uint8_t i;

    if (!item || !reminder_id) {
        return NULL;
    }
    for (i = 0; i < item->reminder_count; i++) {
        if (strcmp(item->reminders[i].id, reminder_id) == 0) {
            return &item->reminders[i];
        }
    }
    return NULL;
}

static int find_tombstone_locked(qm_schedule_t schedule, const char *id)
{
    uint16_t i;

    for (i = 0; i < QM_SCHEDULE_MAX_TOMBSTONES_INTERNAL; i++) {
        if (schedule->store.tombstones[i].used &&
            strcmp(schedule->store.tombstones[i].schedule_id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

static int put_tombstone_locked(qm_schedule_t schedule,
                                const char *id,
                                uint32_t revision)
{
    int idx;
    uint16_t i;

    idx = find_tombstone_locked(schedule, id);
    if (idx >= 0) {
        if (schedule->store.tombstones[idx].revision > revision) {
            return -QM_SCHEDULE_E_REVISION_CONFLICT;
        }
        if (schedule->store.tombstones[idx].revision == revision) {
            return QM_EOK;
        }
        schedule->store.tombstones[idx].revision = revision;
        schedule->store.tombstones[idx].deleted_at = current_utc();
        return QM_EOK;
    }

    for (i = 0; i < QM_SCHEDULE_MAX_TOMBSTONES_INTERNAL; i++) {
        if (!schedule->store.tombstones[i].used) {
            schedule->store.tombstones[i].used = true;
            copy_string(schedule->store.tombstones[i].schedule_id,
                        sizeof(schedule->store.tombstones[i].schedule_id), id);
            schedule->store.tombstones[i].revision = revision;
            schedule->store.tombstones[i].deleted_at = current_utc();
            return QM_EOK;
        }
    }
    return -QM_EFULL;
}

static int validate_binding_ctx(qm_schedule_t schedule,
                                const qm_schedule_apply_ctx_t *ctx)
{
    if (!ctx || str_empty(ctx->operation_id) || str_empty(ctx->binding_version)) {
        return -QM_EINVAL;
    }
    if (str_too_long(ctx->operation_id,
                     QM_SCHEDULE_OPERATION_ID_MAX_DEFAULT) ||
        str_too_long(ctx->binding_version,
                     QM_SCHEDULE_BINDING_VERSION_MAX_DEFAULT)) {
        return -QM_EINVAL;
    }
    if (strcmp(schedule->store.meta.binding_version, ctx->binding_version) != 0) {
        return -QM_SCHEDULE_E_BINDING_INVALID;
    }
    if (schedule->store.meta.lease_expires_at <= current_utc()) {
        return -QM_SCHEDULE_E_BINDING_INVALID;
    }
    return QM_EOK;
}

static int validate_apply_ctx(qm_schedule_t schedule,
                              const qm_schedule_apply_ctx_t *ctx)
{
    int ret = validate_binding_ctx(schedule, ctx);

    if (ret != QM_EOK) return ret;
    if (ctx->desired_revision < schedule->store.meta.confirmed_revision ||
        ctx->desired_revision > schedule->store.meta.confirmed_revision + 1u) {
        return -QM_SCHEDULE_E_REVISION_CONFLICT;
    }
    if (ctx->desired_revision == schedule->store.meta.confirmed_revision &&
        strcmp(schedule->store.meta.last_operation_id,
               ctx->operation_id) != 0) {
        return -QM_SCHEDULE_E_REVISION_CONFLICT;
    }
    return QM_EOK;
}

static int validate_item(qm_schedule_t schedule, const qm_schedule_item_t *item)
{
    static const char *const types[] = {
        "event", "meeting", "task", "birthday", "medication",
        "maintenance", "other"
    };
    static const char *const priorities[] = {
        "low", "normal", "high", "critical"
    };
    static const char *const categories[] = {
        "general", "work", "health", "education", "home", "other"
    };
    static const char *const sensitivities[] = {"normal", "sensitive"};
    static const char *const statuses[] = {"scheduled", "ended", "cancelled"};
    int64_t recurrence_start_utc = 0;
    int64_t recurrence_end_utc = 0;
    uint8_t i;

    if (!schedule || !item || str_empty(item->id) || item->revision == 0 ||
        str_empty(item->type) || str_empty(item->source) ||
        str_empty(item->title) || str_empty(item->timezone) ||
        item->start_at <= 0 ||
        item->reminder_count > schedule->config.max_reminders_per_item ||
        item->recurrence > QM_SCHEDULE_RECURRENCE_WEEKLY) {
        return -QM_EINVAL;
    }
    if (!utf8_valid(item->id) || !utf8_valid(item->type) ||
        !utf8_valid(item->source) || !utf8_valid(item->title) ||
        !utf8_valid(item->timezone) ||
        !optional_utf8(item->group_id) || !optional_utf8(item->description) ||
        !optional_utf8(item->location) || !optional_utf8(item->priority) ||
        !optional_utf8(item->category) || !optional_utf8(item->sensitivity) ||
        !optional_utf8(item->recurrence_start_date) ||
        !optional_utf8(item->recurrence_end_date) ||
        !optional_utf8(item->status) || !optional_utf8(item->missed_policy) ||
        !optional_utf8(item->projection_key)) {
        return -QM_EINVAL;
    }
    if (!string_in_set(item->type, types,
                       (uint8_t)(sizeof(types) / sizeof(types[0]))) ||
        !string_in_set(item->priority, priorities,
                       (uint8_t)(sizeof(priorities) / sizeof(priorities[0]))) ||
        !string_in_set(item->category, categories,
                       (uint8_t)(sizeof(categories) / sizeof(categories[0]))) ||
        !string_in_set(item->sensitivity, sensitivities,
                       (uint8_t)(sizeof(sensitivities) /
                                 sizeof(sensitivities[0]))) ||
        !string_in_set(item->status, statuses,
                       (uint8_t)(sizeof(statuses) / sizeof(statuses[0]))) ||
        (!str_empty(item->missed_policy) &&
         strcmp(item->missed_policy, "windowed") != 0) ||
        !valid_json_type(item->extensions_json, true) ||
        !extensions_schema_valid(item->extensions_json)) {
        return -QM_EINVAL;
    }
    if (!qm_schedule_timezone_supported(item->timezone)) {
        return -QM_SCHEDULE_E_INVALID_TIMEZONE;
    }
    if (item->recurrence == QM_SCHEDULE_RECURRENCE_ONCE) {
        if (!str_empty(item->recurrence_start_date) ||
            !str_empty(item->recurrence_end_date) || item->weekly_mask != 0) {
            return -QM_EINVAL;
        }
    } else {
        if (str_empty(item->recurrence_start_date) ||
            qm_schedule_recurrence_date_to_utc(
                item->recurrence_start_date, item->timezone, false,
                &recurrence_start_utc) != QM_EOK ||
            item->start_at < recurrence_start_utc ||
            (item->recurrence == QM_SCHEDULE_RECURRENCE_DAILY &&
             item->weekly_mask != 0)) {
            return -QM_EINVAL;
        }
        if (!str_empty(item->recurrence_end_date) &&
            (qm_schedule_recurrence_date_to_utc(
                 item->recurrence_end_date, item->timezone, true,
                 &recurrence_end_utc) != QM_EOK ||
             recurrence_start_utc > recurrence_end_utc ||
             item->start_at > recurrence_end_utc ||
             (item->recurrence_until != 0 &&
              item->recurrence_until != recurrence_end_utc))) {
            return -QM_EINVAL;
        }
    }
    if (str_too_long(item->id, QM_SCHEDULE_ID_MAX_DEFAULT) ||
        str_too_long(item->type, QM_SCHEDULE_TYPE_MAX_DEFAULT) ||
        str_too_long(item->source, QM_SCHEDULE_SOURCE_MAX_DEFAULT) ||
        str_too_long(item->group_id, QM_SCHEDULE_GROUP_ID_MAX_DEFAULT) ||
        str_too_long(item->title, QM_SCHEDULE_TITLE_MAX_DEFAULT) ||
        str_too_long(item->description,
                     QM_SCHEDULE_DESCRIPTION_MAX_DEFAULT) ||
        str_too_long(item->location, QM_SCHEDULE_LOCATION_MAX_DEFAULT) ||
        str_too_long(item->priority, QM_SCHEDULE_PRIORITY_MAX_DEFAULT) ||
        str_too_long(item->category, QM_SCHEDULE_CATEGORY_MAX_DEFAULT) ||
        str_too_long(item->sensitivity,
                     QM_SCHEDULE_SENSITIVITY_MAX_DEFAULT) ||
        str_too_long(item->timezone, QM_SCHEDULE_TIMEZONE_MAX_DEFAULT)) {
        return -QM_EINVAL;
    }
    if (item->all_day ||
        str_too_long(item->recurrence_start_date,
                     QM_SCHEDULE_DATE_MAX_DEFAULT) ||
        str_too_long(item->recurrence_end_date,
                     QM_SCHEDULE_DATE_MAX_DEFAULT) ||
        (item->recurrence_interval != 0 && item->recurrence_interval != 1) ||
        str_too_long(item->status, QM_SCHEDULE_STATUS_MAX_DEFAULT) ||
        str_too_long(item->missed_policy,
                     QM_SCHEDULE_MISSED_POLICY_MAX_DEFAULT) ||
        str_too_long(item->projection_key,
                     QM_SCHEDULE_PROJECTION_KEY_MAX_DEFAULT) ||
        str_too_long(item->extensions_json,
                     QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT)) {
        return -QM_EINVAL;
    }
    if (item->end_at != 0 && item->end_at < item->start_at) {
        return -QM_EINVAL;
    }
    if (item->recurrence == QM_SCHEDULE_RECURRENCE_WEEKLY &&
        item->weekly_mask == 0) {
        return -QM_EINVAL;
    }
    if (item->reminder_count > 0 && !item->reminders) {
        return -QM_EINVAL;
    }
    for (i = 0; i < item->reminder_count; i++) {
        const qm_schedule_reminder_t *reminder = &item->reminders[i];
        uint8_t j;
        if (str_empty(reminder->id)) {
            return -QM_EINVAL;
        }
        if (!utf8_valid(reminder->id) || !optional_utf8(reminder->action)) {
            return -QM_EINVAL;
        }
        if ((!str_empty(reminder->action) &&
             strcmp(reminder->action, "notify") != 0) ||
            !valid_json_type(reminder->channels_json, false) ||
            !channels_schema_valid(reminder->channels_json) ||
            reminder->offset_min < 0) {
            return -QM_EINVAL;
        }
        if (str_too_long(reminder->id,
                         QM_SCHEDULE_REMINDER_ID_MAX_DEFAULT) ||
            str_too_long(reminder->action, QM_SCHEDULE_MAX_ACTION_LEN) ||
            str_too_long(reminder->channels_json,
                         QM_SCHEDULE_CHANNELS_MAX_DEFAULT)) {
            return -QM_EINVAL;
        }
        if (reminder->snooze_interval_min != 0 &&
            reminder->snooze_interval_min != 5 &&
            reminder->snooze_interval_min != 10 &&
            reminder->snooze_interval_min != 30) {
            return -QM_EINVAL;
        }
        if (reminder->snooze_max_count > CONFIG_QM_SCHEDULE_SNOOZE_MAX_COUNT) {
            return -QM_EINVAL;
        }
        for (j = (uint8_t)(i + 1u); j < item->reminder_count; j++) {
            if (!str_empty(item->reminders[j].id) &&
                strcmp(reminder->id, item->reminders[j].id) == 0) {
                return -QM_EINVAL;
            }
        }
    }
    return QM_EOK;
}

static void fill_record(qm_schedule_item_record_t *dst,
                        const qm_schedule_item_t *src)
{
    uint8_t i;

    memset(dst, 0, sizeof(*dst));
    dst->used = true;
    copy_string(dst->id, sizeof(dst->id), src->id);
    dst->revision = src->revision;
    copy_string(dst->type, sizeof(dst->type), src->type);
    copy_string(dst->source, sizeof(dst->source), src->source);
    copy_string(dst->group_id, sizeof(dst->group_id), src->group_id);
    copy_string(dst->title, sizeof(dst->title), src->title);
    copy_string(dst->description, sizeof(dst->description), src->description);
    copy_string(dst->location, sizeof(dst->location), src->location);
    copy_string(dst->priority, sizeof(dst->priority),
                str_empty(src->priority) ? "normal" : src->priority);
    copy_string(dst->category, sizeof(dst->category), src->category);
    copy_string(dst->sensitivity, sizeof(dst->sensitivity), src->sensitivity);
    dst->start_at = src->start_at;
    dst->end_at = src->end_at;
    copy_string(dst->timezone, sizeof(dst->timezone), src->timezone);
    dst->all_day = src->all_day;
    dst->recurrence = src->recurrence;
    dst->weekly_mask = src->weekly_mask;
    copy_string(dst->recurrence_start_date,
                sizeof(dst->recurrence_start_date),
                src->recurrence_start_date);
    copy_string(dst->recurrence_end_date,
                sizeof(dst->recurrence_end_date), src->recurrence_end_date);
    dst->recurrence_interval = src->recurrence_interval == 0 ?
        1 : src->recurrence_interval;
    dst->recurrence_until = src->recurrence_until;
    if (dst->recurrence_until == 0 && src->recurrence_end_date) {
        (void)qm_schedule_recurrence_date_to_utc(
            src->recurrence_end_date, src->timezone, true,
            &dst->recurrence_until);
    }
    copy_string(dst->status, sizeof(dst->status),
                str_empty(src->status) ? "scheduled" : src->status);
    copy_string(dst->missed_policy, sizeof(dst->missed_policy),
                str_empty(src->missed_policy) ? "windowed" :
                src->missed_policy);
    copy_string(dst->projection_key, sizeof(dst->projection_key),
                src->projection_key);
    copy_string(dst->extensions_json, sizeof(dst->extensions_json),
                str_empty(src->extensions_json) ? "{}" :
                src->extensions_json);
    dst->enabled = src->enabled;
    dst->reminder_count = src->reminder_count;
    for (i = 0; i < src->reminder_count; i++) {
        copy_string(dst->reminders[i].id, sizeof(dst->reminders[i].id),
                    src->reminders[i].id);
        dst->reminders[i].offset_min = src->reminders[i].offset_min;
        copy_string(dst->reminders[i].action,
                    sizeof(dst->reminders[i].action),
                    str_empty(src->reminders[i].action) ? "notify" :
                    src->reminders[i].action);
        copy_string(dst->reminders[i].channels_json,
                    sizeof(dst->reminders[i].channels_json),
                    str_empty(src->reminders[i].channels_json) ? "[]" :
                    src->reminders[i].channels_json);
        dst->reminders[i].requires_ack = src->reminders[i].requires_ack;
        dst->reminders[i].snooze_enabled = src->reminders[i].snooze_enabled;
        dst->reminders[i].snooze_interval_min = src->reminders[i].snooze_interval_min;
        dst->reminders[i].snooze_max_count = src->reminders[i].snooze_max_count;
    }
}

static bool item_matches_record(const qm_schedule_item_record_t *record,
                                const qm_schedule_item_t *item)
{
    qm_schedule_item_record_t normalized;

    fill_record(&normalized, item);
    return memcmp(record, &normalized, sizeof(normalized)) == 0;
}

static void fill_snapshot(qm_schedule_item_snapshot_t *dst,
                          const qm_schedule_item_record_t *src)
{
    uint8_t i;

    memset(dst, 0, sizeof(*dst));
    copy_string(dst->id, sizeof(dst->id), src->id);
    dst->revision = src->revision;
    copy_string(dst->type, sizeof(dst->type), src->type);
    copy_string(dst->source, sizeof(dst->source), src->source);
    copy_string(dst->group_id, sizeof(dst->group_id), src->group_id);
    copy_string(dst->title, sizeof(dst->title), src->title);
    copy_string(dst->description, sizeof(dst->description), src->description);
    copy_string(dst->location, sizeof(dst->location), src->location);
    copy_string(dst->priority, sizeof(dst->priority), src->priority);
    copy_string(dst->category, sizeof(dst->category), src->category);
    copy_string(dst->sensitivity, sizeof(dst->sensitivity), src->sensitivity);
    dst->start_at = src->start_at;
    dst->end_at = src->end_at;
    copy_string(dst->timezone, sizeof(dst->timezone), src->timezone);
    dst->all_day = src->all_day;
    dst->recurrence = src->recurrence;
    dst->weekly_mask = src->weekly_mask;
    copy_string(dst->recurrence_start_date,
                sizeof(dst->recurrence_start_date),
                src->recurrence_start_date);
    copy_string(dst->recurrence_end_date,
                sizeof(dst->recurrence_end_date), src->recurrence_end_date);
    dst->recurrence_interval = src->recurrence_interval;
    dst->recurrence_until = src->recurrence_until;
    copy_string(dst->status, sizeof(dst->status), src->status);
    copy_string(dst->missed_policy, sizeof(dst->missed_policy),
                src->missed_policy);
    copy_string(dst->projection_key, sizeof(dst->projection_key),
                src->projection_key);
    copy_string(dst->extensions_json, sizeof(dst->extensions_json),
                src->extensions_json);
    dst->enabled = src->enabled;
    dst->reminder_count = src->reminder_count;
    for (i = 0; i < src->reminder_count; i++) {
        copy_string(dst->reminders[i].id, sizeof(dst->reminders[i].id),
                    src->reminders[i].id);
        dst->reminders[i].offset_min = src->reminders[i].offset_min;
        copy_string(dst->reminders[i].action,
                    sizeof(dst->reminders[i].action),
                    src->reminders[i].action);
        copy_string(dst->reminders[i].channels_json,
                    sizeof(dst->reminders[i].channels_json),
                    src->reminders[i].channels_json);
        dst->reminders[i].requires_ack = src->reminders[i].requires_ack;
        dst->reminders[i].snooze_enabled = src->reminders[i].snooze_enabled;
        dst->reminders[i].snooze_interval_min = src->reminders[i].snooze_interval_min;
        dst->reminders[i].snooze_max_count = src->reminders[i].snooze_max_count;
    }
}

static int apply_generation_success(qm_schedule_t schedule,
                                    const qm_schedule_apply_ctx_t *ctx)
{
    if (ctx) {
        schedule->store.meta.desired_revision = ctx->desired_revision;
        schedule->store.meta.confirmed_revision = ctx->desired_revision;
        schedule->store.meta.sync_state = QM_SCHEDULE_SYNC_SYNCED;
        schedule->store.meta.recovery_pending = false;
        copy_string(schedule->store.meta.last_operation_id,
                    sizeof(schedule->store.meta.last_operation_id),
                    ctx->operation_id);
    }
    schedule->store.meta.active_generation++;
    return QM_EOK;
}

static int persist_and_rebuild(qm_schedule_t schedule)
{
    int ret;

    ret = qm_schedule_store_save_all(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    return qm_schedule_scheduler_rebuild(schedule);
}

static qm_schedule_occurrence_record_t *find_occurrence_locked(
    qm_schedule_t schedule,
    const qm_schedule_action_t *action)
{
    uint16_t i;

    for (i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
        qm_schedule_occurrence_record_t *occ = &schedule->store.occurrences[i];
        if (occ->used &&
            strcmp(occ->schedule_id, action->schedule_id) == 0 &&
            occ->occurrence_at == action->occurrence_at &&
            strcmp(occ->reminder_id, action->reminder_id) == 0) {
            return occ;
        }
    }
    return NULL;
}

static qm_schedule_occurrence_record_t *alloc_occurrence_locked(
    qm_schedule_t schedule,
    const qm_schedule_action_t *action)
{
    uint16_t i;

    for (i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
        qm_schedule_occurrence_record_t *occ = &schedule->store.occurrences[i];
        if (!occ->used) {
            memset(occ, 0, sizeof(*occ));
            occ->used = true;
            copy_string(occ->schedule_id, sizeof(occ->schedule_id),
                        action->schedule_id);
            occ->occurrence_at = action->occurrence_at;
            copy_string(occ->reminder_id, sizeof(occ->reminder_id),
                        action->reminder_id);
            occ->occurrence_state = QM_SCHEDULE_OCCURRENCE_PENDING;
            occ->reminder_state = QM_SCHEDULE_REMINDER_ACTIVE;
            return occ;
        }
    }
    return NULL;
}

static int append_outbox_locked(qm_schedule_t schedule,
                                const qm_schedule_action_t *action,
                                qm_schedule_action_type_t action_type,
                                bool *duplicate)
{
    uint16_t i;

    if (duplicate) {
        *duplicate = false;
    }
    if (str_empty(action->event_id)) {
        return QM_EOK;
    }

    for (i = 0; i < schedule->config.max_outbox_user; i++) {
        if (schedule->store.outbox[i].used &&
            strcmp(schedule->store.outbox[i].event_id, action->event_id) == 0) {
            if (duplicate) {
                *duplicate = true;
            }
            return QM_EOK;
        }
    }
    for (i = 0; i < schedule->config.max_outbox_user; i++) {
        qm_schedule_outbox_record_t *out = &schedule->store.outbox[i];
        if (!out->used) {
            memset(out, 0, sizeof(*out));
            out->used = true;
            out->event_type = QM_SCHEDULE_OUTBOX_USER_ACTION;
            copy_string(out->event_id, sizeof(out->event_id), action->event_id);
            copy_string(out->binding_version, sizeof(out->binding_version),
                        action->binding_version);
            copy_string(out->schedule_id, sizeof(out->schedule_id),
                        action->schedule_id);
            {
                int item_index = find_item_locked(schedule, action->schedule_id);
                if (item_index >= 0) {
                    out->revision = schedule->store.items[item_index].revision;
                }
            }
            out->occurrence_at = action->occurrence_at;
            copy_string(out->reminder_id, sizeof(out->reminder_id),
                        action->reminder_id);
            out->action_type = action_type;
            out->action_at = action->action_at;
            return QM_EOK;
        }
    }
    return -QM_SCHEDULE_E_OUTBOX_FULL;
}

static uint16_t system_outbox_start(const qm_schedule_t schedule)
{
    return schedule->config.max_outbox_user;
}

static uint16_t system_outbox_end(const qm_schedule_t schedule)
{
    uint16_t end = schedule->config.max_outbox_user +
        schedule->config.max_outbox_system;

    return end > QM_SCHEDULE_MAX_OUTBOX_INTERNAL ?
        QM_SCHEDULE_MAX_OUTBOX_INTERNAL : end;
}

static int append_system_outbox_locked(qm_schedule_t schedule,
                                       qm_schedule_outbox_type_t event_type,
                                       const char *event_id,
                                       const char *binding_version,
                                       const char *schedule_id,
                                       int64_t event_at)
{
    uint16_t start = system_outbox_start(schedule);
    uint16_t end = system_outbox_end(schedule);
    uint16_t i;

    if (start >= end) {
        return -QM_EFULL;
    }

    for (i = start; i < end; i++) {
        qm_schedule_outbox_record_t *out = &schedule->store.outbox[i];
        if (out->used && !str_empty(event_id) &&
            strcmp(out->event_id, event_id) == 0) {
            return QM_EOK;
        }
    }

    for (i = start; i < end; i++) {
        qm_schedule_outbox_record_t *out = &schedule->store.outbox[i];
        if (!out->used) {
            memset(out, 0, sizeof(*out));
            out->used = true;
            out->event_type = event_type;
            if (!str_empty(event_id)) {
                copy_string(out->event_id, sizeof(out->event_id), event_id);
            } else {
                snprintf(out->event_id, sizeof(out->event_id), "sys:%d:%lld",
                         (int)event_type, (long long)event_at);
            }
            copy_string(out->binding_version, sizeof(out->binding_version),
                        binding_version ? binding_version :
                        schedule->store.meta.binding_version);
            copy_string(out->schedule_id, sizeof(out->schedule_id), schedule_id);
            if (!str_empty(schedule_id)) {
                int item_index = find_item_locked(schedule, schedule_id);
                if (item_index >= 0) {
                    out->revision = schedule->store.items[item_index].revision;
                }
            }
            out->event_at = event_at;
            out->confirmed_revision = schedule->store.meta.confirmed_revision;
            if (event_type == QM_SCHEDULE_OUTBOX_SYSTEM_SYNC_REQUEST) {
                copy_string(out->reason, sizeof(out->reason), "manual");
            } else if (event_type == QM_SCHEDULE_OUTBOX_SYSTEM_BINDING_INVALIDATED) {
                copy_string(out->reason, sizeof(out->reason),
                            "bindingInvalidated");
            } else if (event_type == QM_SCHEDULE_OUTBOX_SYSTEM_CAPACITY_EXCEEDED) {
                copy_string(out->reason, sizeof(out->reason),
                            "capacityExceeded");
            }
            return QM_EOK;
        }
    }

    return -QM_SCHEDULE_E_OUTBOX_FULL;
}

static void note_capacity_full_locked(qm_schedule_t schedule,
                                      const char *schedule_id)
{
    (void)append_system_outbox_locked(
        schedule, QM_SCHEDULE_OUTBOX_SYSTEM_CAPACITY_EXCEEDED, NULL,
        schedule->store.meta.binding_version, schedule_id, current_utc());
}

static int apply_action(qm_schedule_t schedule,
                        const qm_schedule_action_t *action,
                        qm_schedule_action_type_t action_type,
                        uint16_t duration_min,
                        qm_schedule_close_reason_t reason)
{
    qm_schedule_occurrence_record_t *occ;
    const qm_schedule_item_record_t *item;
    const qm_schedule_reminder_record_t *reminder;
    uint8_t snooze_max_count = CONFIG_QM_SCHEDULE_SNOOZE_MAX_COUNT;
    bool duplicate = false;
    bool created_occurrence = false;
    int item_idx;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (!action || str_empty(action->schedule_id) || str_empty(action->reminder_id) ||
        str_empty(action->binding_version) ||
        action->occurrence_at <= 0 || action->action_at <= 0) {
        return -QM_EINVAL;
    }
    if (str_too_long(action->schedule_id, QM_SCHEDULE_ID_MAX_DEFAULT) ||
        str_too_long(action->reminder_id,
                     QM_SCHEDULE_REMINDER_ID_MAX_DEFAULT) ||
        str_too_long(action->operation_id,
                     QM_SCHEDULE_OPERATION_ID_MAX_DEFAULT) ||
        str_too_long(action->event_id, QM_SCHEDULE_EVENT_ID_MAX_DEFAULT) ||
        str_too_long(action->binding_version,
                     QM_SCHEDULE_BINDING_VERSION_MAX_DEFAULT)) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (schedule->store.meta.lease_expires_at <= current_utc()) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_SCHEDULE_E_BINDING_INVALID;
    }
    if (schedule->store.meta.binding_version[0] != '\0' &&
        (str_empty(action->binding_version) ||
         strcmp(action->binding_version,
                schedule->store.meta.binding_version) != 0)) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_SCHEDULE_E_BINDING_INVALID;
    }
    item_idx = find_item_locked(schedule, action->schedule_id);
    if (item_idx < 0) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_ENOENT;
    }
    item = &schedule->store.items[item_idx];
    reminder = find_reminder_locked(item, action->reminder_id);
    if (!reminder) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_ENOENT;
    }
    if (action_type == QM_SCHEDULE_ACTION_SNOOZE) {
        if (!reminder->snooze_enabled) {
            qm_mutex_unlock(&schedule->lock);
            return -QM_SCHEDULE_E_SNOOZE_NOT_ALLOWED;
        }
        if (duration_min == 0) {
            duration_min = reminder->snooze_interval_min;
        }
        if (duration_min == 0) {
            duration_min = 5;
        }
        if (reminder->snooze_max_count > 0) {
            snooze_max_count = reminder->snooze_max_count;
        }
        if (duration_min != 5 && duration_min != 10 && duration_min != 30) {
            qm_mutex_unlock(&schedule->lock);
            return -QM_SCHEDULE_E_SNOOZE_NOT_ALLOWED;
        }
    }
    occ = find_occurrence_locked(schedule, action);
    if (!occ) {
        occ = alloc_occurrence_locked(schedule, action);
        created_occurrence = true;
    }
    if (!occ) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_EFULL;
    }
    if (!str_empty(action->operation_id) &&
        strcmp(occ->last_operation_id, action->operation_id) == 0 &&
        occ->last_action_type == action_type) {
        qm_mutex_unlock(&schedule->lock);
        return QM_EOK;
    }
    if (occ->reminder_state == QM_SCHEDULE_REMINDER_CLOSED ||
        occ->reminder_state == QM_SCHEDULE_REMINDER_CANCELLED ||
        occ->occurrence_state == QM_SCHEDULE_OCCURRENCE_COMPLETED ||
        occ->occurrence_state == QM_SCHEDULE_OCCURRENCE_EXPIRED) {
        if (created_occurrence) memset(occ, 0, sizeof(*occ));
        qm_mutex_unlock(&schedule->lock);
        return -QM_SCHEDULE_E_OCCURRENCE_NOT_ACTIVE;
    }
    if (action_type == QM_SCHEDULE_ACTION_SNOOZE &&
        occ->snooze_count >= snooze_max_count) {
        if (created_occurrence) {
            memset(occ, 0, sizeof(*occ));
        }
        qm_mutex_unlock(&schedule->lock);
        return -QM_SCHEDULE_E_SNOOZE_NOT_ALLOWED;
    }

    ret = append_outbox_locked(schedule, action, action_type, &duplicate);
    if (ret != QM_EOK) {
        if (created_occurrence) {
            memset(occ, 0, sizeof(*occ));
        }
        qm_mutex_unlock(&schedule->lock);
        return ret;
    }
    if (duplicate) {
        if (created_occurrence) {
            memset(occ, 0, sizeof(*occ));
        }
        qm_mutex_unlock(&schedule->lock);
        return QM_EOK;
    }

    occ->action_at = action->action_at;
    switch (action_type) {
    case QM_SCHEDULE_ACTION_ACKNOWLEDGE:
        occ->reminder_state = QM_SCHEDULE_REMINDER_ACKNOWLEDGED;
        break;
    case QM_SCHEDULE_ACTION_COMPLETE:
        occ->occurrence_state = QM_SCHEDULE_OCCURRENCE_COMPLETED;
        occ->reminder_state = QM_SCHEDULE_REMINDER_CLOSED;
        occ->close_reason = QM_SCHEDULE_CLOSE_OCCURRENCE_COMPLETED;
        for (uint16_t i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
            qm_schedule_occurrence_record_t *other =
                &schedule->store.occurrences[i];
            if (other->used && other != occ &&
                other->occurrence_at == occ->occurrence_at &&
                strcmp(other->schedule_id, occ->schedule_id) == 0 &&
                (other->reminder_state == QM_SCHEDULE_REMINDER_PENDING ||
                 other->reminder_state == QM_SCHEDULE_REMINDER_ACTIVE ||
                 other->reminder_state == QM_SCHEDULE_REMINDER_SNOOZED)) {
                other->reminder_state = QM_SCHEDULE_REMINDER_CANCELLED;
                other->close_reason = QM_SCHEDULE_CLOSE_OCCURRENCE_COMPLETED;
            }
        }
        break;
    case QM_SCHEDULE_ACTION_SNOOZE:
        occ->snooze_count++;
        occ->snooze_until = action->action_at + ((int64_t)duration_min * 60);
        occ->reminder_state = QM_SCHEDULE_REMINDER_SNOOZED;
        break;
    case QM_SCHEDULE_ACTION_DISMISS:
        occ->reminder_state = QM_SCHEDULE_REMINDER_CLOSED;
        occ->close_reason = reason;
        break;
    default:
        qm_mutex_unlock(&schedule->lock);
        return -QM_EINVAL;
    }
    copy_string(occ->last_operation_id, sizeof(occ->last_operation_id),
                action->operation_id);
    occ->last_action_type = action_type;
    if (!str_empty(action->event_id)) {
        for (uint16_t i = 0; i < schedule->config.max_outbox_user; i++) {
            qm_schedule_outbox_record_t *out = &schedule->store.outbox[i];
            if (out->used && strcmp(out->event_id, action->event_id) == 0) {
                out->snooze_until = occ->snooze_until;
                out->snooze_count = occ->snooze_count;
                out->close_reason = occ->close_reason;
                break;
            }
        }
    }
    qm_mutex_unlock(&schedule->lock);

    return persist_and_rebuild(schedule);
}

qm_schedule_t qm_schedule_create(const qm_schedule_config_t *config)
{
    qm_schedule_t schedule;

    schedule = (qm_schedule_t)qm_calloc(1, sizeof(*schedule));
    if (!schedule) {
        return NULL;
    }

    normalize_config(&schedule->config, config);
    if (qm_mutex_new(&schedule->lock) != QM_EOK) {
        qm_free(schedule);
        return NULL;
    }
    schedule->lock_valid = true;

    if (qm_timer_new(&schedule->timer, qm_schedule_scheduler_timer_cb,
                     schedule, QM_WAIT_FOREVER, 0) == QM_EOK) {
        schedule->timer_valid = true;
    }

    if (qm_schedule_store_init(schedule) != QM_EOK) {
        if (schedule->timer_valid) {
            qm_timer_free(&schedule->timer);
        }
        qm_mutex_free(&schedule->lock);
        qm_free(schedule);
        return NULL;
    }

    return schedule;
}

void qm_schedule_destroy(qm_schedule_t schedule)
{
    if (!schedule) {
        return;
    }

    if (schedule->lock_valid) {
        qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
        schedule->destroying = true;
        schedule->config.notify_cb = NULL;
        qm_mutex_unlock(&schedule->lock);
    }

    (void)qm_schedule_scheduler_stop(schedule);
    if (schedule->timer_valid) {
        qm_timer_free(&schedule->timer);
        schedule->timer_valid = false;
    }
    qm_schedule_store_deinit(schedule);

    if (schedule->lock_valid) {
        qm_mutex_free(&schedule->lock);
        schedule->lock_valid = false;
    }
    qm_free(schedule);
}

int qm_schedule_start(qm_schedule_t schedule)
{
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (schedule->running) {
        qm_mutex_unlock(&schedule->lock);
        return QM_EOK;
    }
    schedule->running = true;
    qm_mutex_unlock(&schedule->lock);

    ret = qm_schedule_store_recover(schedule);
    if (ret != QM_EOK) {
        (void)qm_schedule_stop(schedule);
        return ret;
    }

    return qm_schedule_scheduler_start(schedule);
}

int qm_schedule_stop(qm_schedule_t schedule)
{
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }

    (void)qm_schedule_scheduler_stop(schedule);
    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    schedule->running = false;
    schedule->schedulable = false;
    qm_mutex_unlock(&schedule->lock);
    return QM_EOK;
}

int qm_schedule_apply_binding(qm_schedule_t schedule,
                              const qm_schedule_binding_t *binding)
{
    char previous_binding[QM_SCHEDULE_BINDING_VERSION_MAX];
    bool binding_changed = false;
    int64_t now;
    int64_t lease_expires_at;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (!binding || str_empty(binding->binding_version) ||
        str_too_long(binding->binding_version,
                     QM_SCHEDULE_BINDING_VERSION_MAX_DEFAULT) ||
        str_too_long(binding->capability_version,
                     QM_SCHEDULE_CAPABILITY_VERSION_MAX)) {
        return -QM_EINVAL;
    }
    now = current_utc();
    lease_expires_at = binding->lease_expires_at > 0 ?
        binding->lease_expires_at :
        now + CONFIG_QM_SCHEDULE_BINDING_LEASE_SEC;
    if (lease_expires_at <= now) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    copy_string(previous_binding, sizeof(previous_binding),
                schedule->store.meta.binding_version);
    binding_changed = previous_binding[0] != '\0' &&
        strcmp(previous_binding, binding->binding_version) != 0;
    if (binding_changed) {
        clear_binding_replica_locked(schedule, previous_binding, now);
    }
    copy_string(schedule->store.meta.binding_version,
                sizeof(schedule->store.meta.binding_version),
                binding->binding_version);
    copy_string(schedule->store.meta.capability_version,
                sizeof(schedule->store.meta.capability_version),
                binding->capability_version);
    schedule->store.meta.lease_expires_at = lease_expires_at;
    schedule->store.meta.sync_state = binding_changed ?
        QM_SCHEDULE_SYNC_PENDING : schedule->store.meta.sync_state;
    if (schedule->store.meta.recovery_pending) {
        uint16_t start = system_outbox_start(schedule);
        uint16_t end = system_outbox_end(schedule);
        (void)append_system_outbox_locked(
            schedule, QM_SCHEDULE_OUTBOX_SYSTEM_SYNC_REQUEST, NULL,
            binding->binding_version, NULL, now);
        for (uint16_t i = start; i < end; i++) {
            qm_schedule_outbox_record_t *event = &schedule->store.outbox[i];
            if (event->used &&
                event->event_type == QM_SCHEDULE_OUTBOX_SYSTEM_SYNC_REQUEST &&
                event->event_at == now) {
                copy_string(event->reason, sizeof(event->reason),
                            "storageReset");
                break;
            }
        }
        schedule->store.meta.sync_state = QM_SCHEDULE_SYNC_PENDING;
    }
    qm_mutex_unlock(&schedule->lock);

    ret = qm_schedule_store_save_all(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    return schedule->running ? qm_schedule_scheduler_rebuild(schedule) : QM_EOK;
}

int qm_schedule_invalidate_binding(qm_schedule_t schedule,
                                   const char *binding_version)
{
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (str_empty(binding_version) ||
        str_too_long(binding_version, QM_SCHEDULE_BINDING_VERSION_MAX_DEFAULT)) {
        return -QM_EINVAL;
    }

    (void)qm_schedule_scheduler_stop(schedule);
    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (strcmp(schedule->store.meta.binding_version, binding_version) == 0) {
        clear_binding_replica_locked(schedule, binding_version, current_utc());
    }
    qm_mutex_unlock(&schedule->lock);

    return qm_schedule_store_save_all(schedule);
}

int qm_schedule_create_item(qm_schedule_t schedule,
                            const qm_schedule_item_t *item,
                            const qm_schedule_apply_ctx_t *ctx)
{
    int idx;
    int tomb_idx;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = validate_item(schedule, item);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    ret = validate_apply_ctx(schedule, ctx);
    if (ret != QM_EOK) {
        qm_mutex_unlock(&schedule->lock);
        return ret;
    }
    tomb_idx = find_tombstone_locked(schedule, item->id);
    if (tomb_idx >= 0 && item->revision <= schedule->store.tombstones[tomb_idx].revision) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_SCHEDULE_E_REVISION_CONFLICT;
    }
    idx = find_item_locked(schedule, item->id);
    if (idx >= 0) {
        if (schedule->store.items[idx].revision == item->revision) {
            bool identical = item_matches_record(&schedule->store.items[idx],
                                                 item);
            qm_mutex_unlock(&schedule->lock);
            return identical ? QM_EOK :
                -QM_SCHEDULE_E_REVISION_CONFLICT;
        }
        {
            uint32_t current_revision = schedule->store.items[idx].revision;
            qm_mutex_unlock(&schedule->lock);
            return current_revision > item->revision ?
                -QM_SCHEDULE_E_REVISION_CONFLICT :
                -QM_SCHEDULE_E_ALREADY_EXISTS;
        }
    } else {
        idx = find_free_item_locked(schedule);
        if (idx < 0) {
            note_capacity_full_locked(schedule, item->id);
            qm_mutex_unlock(&schedule->lock);
            (void)qm_schedule_store_save_all(schedule);
            return -QM_EFULL;
        }
    }
    fill_record(&schedule->store.items[idx], item);
    (void)apply_generation_success(schedule, ctx);
    qm_mutex_unlock(&schedule->lock);
    return persist_and_rebuild(schedule);
}

int qm_schedule_modify_item(qm_schedule_t schedule,
                            const qm_schedule_item_t *item,
                            const qm_schedule_apply_ctx_t *ctx)
{
    int idx;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = validate_item(schedule, item);
    if (ret != QM_EOK) {
        return ret;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    ret = validate_apply_ctx(schedule, ctx);
    if (ret != QM_EOK) {
        qm_mutex_unlock(&schedule->lock);
        return ret;
    }
    idx = find_item_locked(schedule, item->id);
    if (idx < 0) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_ENOENT;
    }
    if (schedule->store.items[idx].revision == item->revision) {
        bool identical = item_matches_record(&schedule->store.items[idx], item);
        qm_mutex_unlock(&schedule->lock);
        return identical ? QM_EOK : -QM_SCHEDULE_E_REVISION_CONFLICT;
    }
    if (schedule->store.items[idx].revision > item->revision) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_SCHEDULE_E_REVISION_CONFLICT;
    }
    fill_record(&schedule->store.items[idx], item);
    (void)apply_generation_success(schedule, ctx);
    qm_mutex_unlock(&schedule->lock);
    return persist_and_rebuild(schedule);
}

int qm_schedule_delete_item(qm_schedule_t schedule,
                            const char *schedule_id,
                            uint32_t revision,
                            const qm_schedule_apply_ctx_t *ctx)
{
    int idx;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (str_empty(schedule_id) || revision == 0) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    ret = validate_apply_ctx(schedule, ctx);
    if (ret != QM_EOK) {
        qm_mutex_unlock(&schedule->lock);
        return ret;
    }
    idx = find_item_locked(schedule, schedule_id);
    if (idx >= 0) {
        if (schedule->store.items[idx].revision >= revision) {
            qm_mutex_unlock(&schedule->lock);
            return -QM_SCHEDULE_E_REVISION_CONFLICT;
        }
        memset(&schedule->store.items[idx], 0, sizeof(schedule->store.items[idx]));
    }
    ret = put_tombstone_locked(schedule, schedule_id, revision);
    if (ret == QM_EOK) {
        (void)apply_generation_success(schedule, ctx);
    }
    qm_mutex_unlock(&schedule->lock);
    if (ret != QM_EOK) {
        return ret;
    }
    return persist_and_rebuild(schedule);
}

int qm_schedule_enable_item(qm_schedule_t schedule,
                            const char *schedule_id,
                            uint32_t revision,
                            bool enabled,
                            const qm_schedule_apply_ctx_t *ctx)
{
    int idx;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (str_empty(schedule_id) || revision == 0) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    ret = validate_apply_ctx(schedule, ctx);
    if (ret != QM_EOK) {
        qm_mutex_unlock(&schedule->lock);
        return ret;
    }
    idx = find_item_locked(schedule, schedule_id);
    if (idx < 0) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_ENOENT;
    }
    if (schedule->store.items[idx].revision > revision) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_SCHEDULE_E_REVISION_CONFLICT;
    }
    if (schedule->store.items[idx].revision == revision) {
        bool identical = schedule->store.items[idx].enabled == enabled;
        qm_mutex_unlock(&schedule->lock);
        return identical ? QM_EOK : -QM_SCHEDULE_E_REVISION_CONFLICT;
    }
    schedule->store.items[idx].revision = revision;
    schedule->store.items[idx].enabled = enabled;
    (void)apply_generation_success(schedule, ctx);
    qm_mutex_unlock(&schedule->lock);
    return persist_and_rebuild(schedule);
}

int qm_schedule_apply_sync(qm_schedule_t schedule,
                           const qm_schedule_item_t *items,
                           uint16_t item_count,
                           bool replace,
                           const qm_schedule_apply_ctx_t *ctx)
{
    qm_schedule_item_record_t *next_items;
    int16_t *merge_slots = NULL;
    uint16_t i;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (item_count > schedule->config.max_items ||
        (item_count > 0 && !items)) {
        return -QM_EINVAL;
    }

    next_items = (qm_schedule_item_record_t *)qm_calloc(
        QM_SCHEDULE_MAX_ITEMS_INTERNAL, sizeof(*next_items));
    if (!next_items) {
        return -QM_ENOMEM;
    }

    for (i = 0; i < item_count; i++) {
        ret = validate_item(schedule, &items[i]);
        if (ret != QM_EOK) {
            qm_free(next_items);
            return ret;
        }
        fill_record(&next_items[i], &items[i]);
    }
    for (i = 0; i < item_count; i++) {
        uint16_t j;

        for (j = i + 1; j < item_count; j++) {
            if (strcmp(next_items[i].id, next_items[j].id) == 0) {
                qm_free(next_items);
                return -QM_EINVAL;
            }
        }
    }

    if (!replace) {
        merge_slots = (int16_t *)qm_calloc(QM_SCHEDULE_MAX_ITEMS_INTERNAL,
                                           sizeof(*merge_slots));
        if (!merge_slots) {
            qm_free(next_items);
            return -QM_ENOMEM;
        }
        for (i = 0; i < QM_SCHEDULE_MAX_ITEMS_INTERNAL; i++) {
            merge_slots[i] = -1;
        }
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    ret = validate_apply_ctx(schedule, ctx);
    if (ret != QM_EOK) {
        qm_mutex_unlock(&schedule->lock);
        qm_free(merge_slots);
        qm_free(next_items);
        return ret;
    }
    if (replace) {
        memset(schedule->store.items, 0, sizeof(schedule->store.items));
        memset(schedule->store.occurrences, 0, sizeof(schedule->store.occurrences));
        memset(schedule->store.tombstones, 0, sizeof(schedule->store.tombstones));
        for (i = 0; i < item_count; i++) {
            schedule->store.items[i] = next_items[i];
        }
    } else {
        uint8_t reserved[QM_SCHEDULE_MAX_ITEMS_INTERNAL] = {0};

        for (i = 0; i < item_count; i++) {
            int idx = find_item_locked(schedule, next_items[i].id);
            if (idx < 0) {
                uint16_t j;

                for (j = 0; j < schedule->config.max_items; j++) {
                    if (!schedule->store.items[j].used && !reserved[j]) {
                        idx = (int)j;
                        reserved[j] = 1;
                        break;
                    }
                }
            }
            if (idx < 0) {
                note_capacity_full_locked(schedule, next_items[i].id);
                qm_mutex_unlock(&schedule->lock);
                qm_free(merge_slots);
                qm_free(next_items);
                (void)qm_schedule_store_save_all(schedule);
                return -QM_EFULL;
            }
            if (schedule->store.items[idx].used &&
                schedule->store.items[idx].revision > next_items[i].revision) {
                qm_mutex_unlock(&schedule->lock);
                qm_free(merge_slots);
                qm_free(next_items);
                return -QM_EINVAL;
            }
            merge_slots[i] = (int16_t)idx;
        }
        for (i = 0; i < item_count; i++) {
            schedule->store.items[merge_slots[i]] = next_items[i];
        }
    }
    (void)apply_generation_success(schedule, ctx);
    qm_mutex_unlock(&schedule->lock);
    qm_free(merge_slots);
    qm_free(next_items);
    return persist_and_rebuild(schedule);
}

static uint32_t expected_chunk_mask(uint16_t total_chunks)
{
    if (total_chunks >= QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL) {
        return 0xffffffffu;
    }
    return (uint32_t)((1u << total_chunks) - 1u);
}

static bool sync_id_matches_locked(qm_schedule_t schedule, const char *sync_id)
{
    return schedule->store.staging_active &&
        !str_empty(sync_id) &&
        strcmp(schedule->store.staging_sync_id, sync_id) == 0;
}

static bool staged_has_id_locked(qm_schedule_t schedule,
                                 const char *schedule_id)
{
    uint16_t i;

    for (i = 0; i < schedule->store.staging_item_count; i++) {
        if (schedule->store.staged_items[i].used &&
            strcmp(schedule->store.staged_items[i].id, schedule_id) == 0) {
            return true;
        }
    }
    return false;
}

int qm_schedule_sync_stage_begin(qm_schedule_t schedule,
                                 const char *sync_id,
                                 uint16_t total_chunks,
                                 bool replace,
                                 const qm_schedule_apply_ctx_t *ctx)
{
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (str_empty(sync_id) || total_chunks == 0 ||
        total_chunks > QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    ret = validate_binding_ctx(schedule, ctx);
    if (ret != QM_EOK) {
        qm_mutex_unlock(&schedule->lock);
        return ret;
    }

    memset(schedule->store.staged_items, 0,
           sizeof(schedule->store.staged_items));
    memset(schedule->store.staging_chunk_digests, 0,
           sizeof(schedule->store.staging_chunk_digests));
    memset(schedule->store.staging_chunk_item_counts, 0,
           sizeof(schedule->store.staging_chunk_item_counts));
    memset(schedule->store.staging_groups, 0,
           sizeof(schedule->store.staging_groups));
    schedule->store.staging_active = true;
    schedule->store.staging_replace = replace;
    schedule->store.staging_full_projection = false;
    copy_string(schedule->store.staging_sync_id,
                sizeof(schedule->store.staging_sync_id), sync_id);
    schedule->store.staging_group_id[0] = '\0';
    schedule->store.staging_group_revision = 0;
    schedule->store.staging_group_count = 0;
    schedule->store.staging_total_chunks = total_chunks;
    schedule->store.staging_total_items = 0;
    schedule->store.staging_received_mask = 0;
    schedule->store.staging_item_count = 0;
    schedule->store.staging_started_at = current_utc();
    schedule->store.meta.desired_revision = ctx->desired_revision;
    schedule->store.meta.sync_state = QM_SCHEDULE_SYNC_SYNCING;
    qm_mutex_unlock(&schedule->lock);

    return qm_schedule_store_save_all(schedule);
}

int qm_schedule_sync_stage_items(qm_schedule_t schedule,
                                 const char *sync_id,
                                 uint16_t chunk_index,
                                 const qm_schedule_item_t *items,
                                 uint16_t item_count)
{
    qm_schedule_item_record_t *next_items;
    uint16_t i;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (str_empty(sync_id) || item_count > schedule->config.max_items ||
        (item_count > 0 && !items)) {
        return -QM_EINVAL;
    }

    next_items = (qm_schedule_item_record_t *)qm_calloc(
        QM_SCHEDULE_MAX_ITEMS_INTERNAL, sizeof(*next_items));
    if (!next_items) {
        return -QM_ENOMEM;
    }

    for (i = 0; i < item_count; i++) {
        uint16_t j;

        ret = validate_item(schedule, &items[i]);
        if (ret != QM_EOK) {
            qm_free(next_items);
            return ret;
        }
        for (j = i + 1; j < item_count; j++) {
            if (!str_empty(items[j].id) &&
                strcmp(items[i].id, items[j].id) == 0) {
                qm_free(next_items);
                return -QM_EINVAL;
            }
            if (!str_empty(items[i].projection_key) &&
                !str_empty(items[j].projection_key) &&
                strcmp(items[i].group_id ? items[i].group_id : "",
                       items[j].group_id ? items[j].group_id : "") == 0 &&
                strcmp(items[i].projection_key,
                       items[j].projection_key) == 0) {
                qm_free(next_items);
                return -QM_EINVAL;
            }
        }
        fill_record(&next_items[i], &items[i]);
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (!sync_id_matches_locked(schedule, sync_id) ||
        chunk_index >= schedule->store.staging_total_chunks) {
        qm_mutex_unlock(&schedule->lock);
        qm_free(next_items);
        return -QM_EINVAL;
    }
    if ((schedule->store.staging_received_mask & (1u << chunk_index)) != 0) {
        qm_mutex_unlock(&schedule->lock);
        qm_free(next_items);
        return QM_EOK;
    }
    if ((uint32_t)schedule->store.staging_item_count + item_count >
        schedule->config.max_items) {
        note_capacity_full_locked(schedule, NULL);
        qm_mutex_unlock(&schedule->lock);
        qm_free(next_items);
        (void)qm_schedule_store_save_all(schedule);
        return -QM_EFULL;
    }
    for (i = 0; i < item_count; i++) {
        if (staged_has_id_locked(schedule, next_items[i].id)) {
            qm_mutex_unlock(&schedule->lock);
            qm_free(next_items);
            return -QM_EINVAL;
        }
        if (next_items[i].projection_key[0] != '\0') {
            uint16_t j;
            for (j = 0; j < schedule->store.staging_item_count; j++) {
                const qm_schedule_item_record_t *staged =
                    &schedule->store.staged_items[j];
                if (strcmp(staged->group_id, next_items[i].group_id) == 0 &&
                    strcmp(staged->projection_key,
                           next_items[i].projection_key) == 0) {
                    qm_mutex_unlock(&schedule->lock);
                    qm_free(next_items);
                    return -QM_EINVAL;
                }
            }
        }
    }
    for (i = 0; i < item_count; i++) {
        schedule->store.staged_items[schedule->store.staging_item_count++] =
            next_items[i];
    }
    schedule->store.staging_received_mask |= (1u << chunk_index);
    qm_mutex_unlock(&schedule->lock);
    qm_free(next_items);

    return qm_schedule_store_save_all(schedule);
}

int qm_schedule_sync_stage_commit(qm_schedule_t schedule,
                                  const char *sync_id,
                                  const qm_schedule_apply_ctx_t *ctx)
{
    int16_t *merge_slots = NULL;
    uint16_t item_count;
    uint16_t i;
    bool replace;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }

    merge_slots = (int16_t *)qm_calloc(QM_SCHEDULE_MAX_ITEMS_INTERNAL,
                                       sizeof(*merge_slots));
    if (!merge_slots) {
        return -QM_ENOMEM;
    }
    for (i = 0; i < QM_SCHEDULE_MAX_ITEMS_INTERNAL; i++) {
        merge_slots[i] = -1;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (!sync_id_matches_locked(schedule, sync_id) ||
        schedule->store.staging_received_mask !=
            expected_chunk_mask(schedule->store.staging_total_chunks)) {
        qm_mutex_unlock(&schedule->lock);
        qm_free(merge_slots);
        return -QM_EINVAL;
    }
    ret = validate_binding_ctx(schedule, ctx);
    if (ret != QM_EOK) {
        qm_mutex_unlock(&schedule->lock);
        qm_free(merge_slots);
        return ret;
    }

    item_count = schedule->store.staging_item_count;
    replace = schedule->store.staging_replace;
    if (replace && (schedule->store.staging_full_projection ||
                    schedule->store.staging_group_id[0] == '\0')) {
        memset(schedule->store.items, 0, sizeof(schedule->store.items));
        memset(schedule->store.occurrences, 0,
               sizeof(schedule->store.occurrences));
        memset(schedule->store.tombstones, 0,
               sizeof(schedule->store.tombstones));
        for (i = 0; i < item_count; i++) {
            schedule->store.items[i] = schedule->store.staged_items[i];
        }
    } else {
        uint8_t reserved[QM_SCHEDULE_MAX_ITEMS_INTERNAL] = {0};

        if (replace && schedule->store.staging_group_id[0] != '\0') {
            for (i = 0; i < schedule->config.max_items; i++) {
                if (schedule->store.items[i].used &&
                    strcmp(schedule->store.items[i].group_id,
                           schedule->store.staging_group_id) == 0) {
                    memset(&schedule->store.items[i], 0,
                           sizeof(schedule->store.items[i]));
                }
            }
        }

        for (i = 0; i < item_count; i++) {
            int idx = find_item_locked(schedule,
                                       schedule->store.staged_items[i].id);
            if (idx < 0) {
                uint16_t j;

                for (j = 0; j < schedule->config.max_items; j++) {
                    if (!schedule->store.items[j].used && !reserved[j]) {
                        idx = (int)j;
                        reserved[j] = 1;
                        break;
                    }
                }
            }
            if (idx < 0) {
                note_capacity_full_locked(schedule,
                                          schedule->store.staged_items[i].id);
                qm_mutex_unlock(&schedule->lock);
                qm_free(merge_slots);
                (void)qm_schedule_store_save_all(schedule);
                return -QM_EFULL;
            }
            if (schedule->store.items[idx].used &&
                schedule->store.items[idx].revision >
                    schedule->store.staged_items[i].revision) {
                qm_mutex_unlock(&schedule->lock);
                qm_free(merge_slots);
                return -QM_SCHEDULE_E_REVISION_CONFLICT;
            }
            merge_slots[i] = (int16_t)idx;
        }
        for (i = 0; i < item_count; i++) {
            schedule->store.items[merge_slots[i]] =
                schedule->store.staged_items[i];
        }
    }

    if (schedule->store.staging_full_projection) {
        memset(schedule->store.groups, 0, sizeof(schedule->store.groups));
        if (schedule->store.staging_group_count > 0) {
            memcpy(schedule->store.groups, schedule->store.staging_groups,
                   schedule->store.staging_group_count *
                   sizeof(schedule->store.groups[0]));
        }
    } else if (schedule->store.staging_group_id[0] != '\0') {
        int group_index = -1;
        for (i = 0; i < QM_SCHEDULE_MAX_GROUPS_INTERNAL; i++) {
            if (schedule->store.groups[i].used &&
                strcmp(schedule->store.groups[i].group_id,
                       schedule->store.staging_group_id) == 0) {
                group_index = (int)i;
                break;
            }
        }
        if (group_index < 0) {
            for (i = 0; i < QM_SCHEDULE_MAX_GROUPS_INTERNAL; i++) {
                if (!schedule->store.groups[i].used) {
                    group_index = (int)i;
                    break;
                }
            }
        }
        if (group_index < 0) {
            qm_mutex_unlock(&schedule->lock);
            qm_free(merge_slots);
            return -QM_EFULL;
        }
        schedule->store.groups[group_index].used = true;
        copy_string(schedule->store.groups[group_index].group_id,
                    sizeof(schedule->store.groups[group_index].group_id),
                    schedule->store.staging_group_id);
        schedule->store.groups[group_index].revision =
            schedule->store.staging_group_revision;
    }

    copy_string(schedule->store.last_sync_id,
                sizeof(schedule->store.last_sync_id),
                schedule->store.staging_sync_id);
    schedule->store.last_sync_chunk_count =
        schedule->store.staging_total_chunks;
    schedule->store.last_sync_received_mask =
        schedule->store.staging_received_mask;
    memcpy(schedule->store.last_sync_chunk_digests,
           schedule->store.staging_chunk_digests,
           sizeof(schedule->store.last_sync_chunk_digests));
    memset(schedule->store.staged_items, 0,
           sizeof(schedule->store.staged_items));
    schedule->store.staging_active = false;
    schedule->store.staging_replace = false;
    schedule->store.staging_full_projection = false;
    schedule->store.staging_sync_id[0] = '\0';
    schedule->store.staging_group_id[0] = '\0';
    schedule->store.staging_group_revision = 0;
    schedule->store.staging_group_count = 0;
    schedule->store.staging_total_chunks = 0;
    schedule->store.staging_total_items = 0;
    schedule->store.staging_received_mask = 0;
    memset(schedule->store.staging_chunk_digests, 0,
           sizeof(schedule->store.staging_chunk_digests));
    memset(schedule->store.staging_chunk_item_counts, 0,
           sizeof(schedule->store.staging_chunk_item_counts));
    schedule->store.staging_item_count = 0;
    schedule->store.staging_started_at = 0;
    (void)apply_generation_success(schedule, ctx);
    qm_mutex_unlock(&schedule->lock);
    qm_free(merge_slots);

    return persist_and_rebuild(schedule);
}

int qm_schedule_sync_stage_abort(qm_schedule_t schedule,
                                 const char *sync_id)
{
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (str_empty(sync_id)) {
        return -QM_EINVAL;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (!sync_id_matches_locked(schedule, sync_id)) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_EINVAL;
    }
    memset(schedule->store.staged_items, 0,
           sizeof(schedule->store.staged_items));
    memset(schedule->store.staging_chunk_digests, 0,
           sizeof(schedule->store.staging_chunk_digests));
    memset(schedule->store.staging_chunk_item_counts, 0,
           sizeof(schedule->store.staging_chunk_item_counts));
    schedule->store.staging_active = false;
    schedule->store.staging_replace = false;
    schedule->store.staging_full_projection = false;
    schedule->store.staging_sync_id[0] = '\0';
    schedule->store.staging_group_id[0] = '\0';
    schedule->store.staging_group_revision = 0;
    schedule->store.staging_group_count = 0;
    schedule->store.staging_total_chunks = 0;
    schedule->store.staging_total_items = 0;
    schedule->store.staging_received_mask = 0;
    schedule->store.staging_item_count = 0;
    schedule->store.staging_started_at = 0;
    schedule->store.meta.sync_state = QM_SCHEDULE_SYNC_NOT_REQUIRED;
    qm_mutex_unlock(&schedule->lock);

    return qm_schedule_store_save_all(schedule);
}

int qm_schedule_sync_apply_chunk(qm_schedule_t schedule,
                                 const qm_schedule_sync_chunk_t *chunk,
                                 const qm_schedule_item_t *items,
                                 uint16_t item_count,
                                 const qm_schedule_apply_ctx_t *ctx,
                                 bool *complete,
                                 bool *idempotent)
{
    uint32_t expected_mask;
    uint32_t received_mask;
    uint32_t item_total;
    bool active;
    bool replay = false;
    int ret;

    if (!schedule || !chunk || !ctx || !complete || !idempotent ||
        str_empty(chunk->sync_id) || str_empty(chunk->content_digest) ||
        chunk->chunk_count == 0 ||
        chunk->chunk_count > QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL ||
        chunk->chunk_index >= chunk->chunk_count ||
        item_count > schedule->config.max_items ||
        chunk->total_items > schedule->config.max_items ||
        (item_count > 0 && !items)) {
        return -QM_EINVAL;
    }
    *complete = false;
    *idempotent = false;
    expected_mask = expected_chunk_mask(chunk->chunk_count);

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    active = schedule->store.staging_active;
    if (!active && strcmp(schedule->store.last_sync_id, chunk->sync_id) == 0 &&
        chunk->chunk_count == schedule->store.last_sync_chunk_count &&
        (schedule->store.last_sync_received_mask &
         (1u << chunk->chunk_index)) != 0) {
        replay = strcmp(schedule->store.last_sync_chunk_digests[
                            chunk->chunk_index], chunk->content_digest) == 0;
        if (replay) {
            *complete = schedule->store.last_sync_received_mask == expected_mask;
            *idempotent = true;
        }
    }
    if (!active && !replay) {
        bool generation_allowed = chunk->full_projection ?
            (schedule->store.meta.confirmed_revision == 0 ||
             schedule->store.meta.sync_state == QM_SCHEDULE_SYNC_PENDING ||
             schedule->store.meta.recovery_pending) :
            (ctx->desired_revision ==
             schedule->store.meta.confirmed_revision + 1u);
        if (!generation_allowed) {
            qm_mutex_unlock(&schedule->lock);
            return -QM_SCHEDULE_E_REVISION_CONFLICT;
        }
        if (!chunk->full_projection && chunk->group_id) {
            for (uint16_t i = 0; i < QM_SCHEDULE_MAX_GROUPS_INTERNAL; i++) {
                if (schedule->store.groups[i].used &&
                    strcmp(schedule->store.groups[i].group_id,
                           chunk->group_id) == 0 &&
                    schedule->store.groups[i].revision >= chunk->group_revision) {
                    qm_mutex_unlock(&schedule->lock);
                    return -QM_SCHEDULE_E_REVISION_CONFLICT;
                }
            }
        }
    }
    if (active && strcmp(schedule->store.staging_sync_id, chunk->sync_id) != 0) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_EBUSY;
    }
    if (active && (schedule->store.staging_total_chunks != chunk->chunk_count ||
                   schedule->store.staging_total_items != chunk->total_items ||
                   schedule->store.staging_replace != chunk->replace ||
                   schedule->store.staging_full_projection !=
                       chunk->full_projection ||
                   schedule->store.staging_group_revision !=
                       chunk->group_revision ||
                   schedule->store.staging_group_count != chunk->group_count ||
                   (chunk->full_projection && chunk->groups &&
                    memcmp(schedule->store.staging_groups, chunk->groups,
                           chunk->group_count *
                           sizeof(schedule->store.staging_groups[0])) != 0) ||
                   strcmp(schedule->store.staging_group_id,
                          chunk->group_id ? chunk->group_id : "") != 0 ||
                   schedule->store.meta.desired_revision !=
                       ctx->desired_revision)) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_SCHEDULE_E_SYNC_CONFLICT;
    }
    if (active && (schedule->store.staging_received_mask &
                   (1u << chunk->chunk_index)) != 0) {
        replay = strcmp(schedule->store.staging_chunk_digests[
                            chunk->chunk_index], chunk->content_digest) == 0;
        if (replay) {
            received_mask = schedule->store.staging_received_mask;
            *complete = received_mask == expected_mask;
            *idempotent = true;
        }
        qm_mutex_unlock(&schedule->lock);
        return replay ? QM_EOK : -QM_SCHEDULE_E_SYNC_CONFLICT;
    }
    qm_mutex_unlock(&schedule->lock);
    if (replay) {
        return QM_EOK;
    }

    if (!active) {
        ret = qm_schedule_sync_stage_begin(schedule, chunk->sync_id,
                                            chunk->chunk_count, chunk->replace,
                                            ctx);
        if (ret != QM_EOK) {
            return ret;
        }
        qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
        schedule->store.staging_full_projection = chunk->full_projection;
        copy_string(schedule->store.staging_group_id,
                    sizeof(schedule->store.staging_group_id),
                    chunk->group_id);
        schedule->store.staging_group_revision = chunk->group_revision;
        schedule->store.staging_total_items = chunk->total_items;
        schedule->store.staging_group_count = chunk->group_count >
            QM_SCHEDULE_MAX_GROUPS_INTERNAL ?
            QM_SCHEDULE_MAX_GROUPS_INTERNAL : chunk->group_count;
        if (chunk->groups && schedule->store.staging_group_count > 0) {
            memcpy(schedule->store.staging_groups, chunk->groups,
                   schedule->store.staging_group_count *
                   sizeof(schedule->store.staging_groups[0]));
        }
        qm_mutex_unlock(&schedule->lock);
    }

    ret = qm_schedule_sync_stage_items(schedule, chunk->sync_id,
                                       chunk->chunk_index, items, item_count);
    if (ret != QM_EOK) {
        return ret;
    }
    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    copy_string(schedule->store.staging_chunk_digests[chunk->chunk_index],
                sizeof(schedule->store.staging_chunk_digests[0]),
                chunk->content_digest);
    schedule->store.staging_chunk_item_counts[chunk->chunk_index] = item_count;
    received_mask = schedule->store.staging_received_mask;
    item_total = 0;
    for (uint16_t i = 0; i < chunk->chunk_count; i++) {
        item_total += schedule->store.staging_chunk_item_counts[i];
    }
    if (item_total > chunk->total_items ||
        (received_mask == expected_mask && item_total != chunk->total_items)) {
        qm_mutex_unlock(&schedule->lock);
        (void)qm_schedule_store_save_all(schedule);
        return -QM_SCHEDULE_E_SYNC_CONFLICT;
    }
    *complete = received_mask == expected_mask;
    qm_mutex_unlock(&schedule->lock);
    ret = qm_schedule_store_save_all(schedule);
    if (ret != QM_EOK || !*complete) {
        return ret;
    }
    return qm_schedule_sync_stage_commit(schedule, chunk->sync_id, ctx);
}

int qm_schedule_acknowledge(qm_schedule_t schedule,
                            const qm_schedule_action_t *action)
{
    return apply_action(schedule, action, QM_SCHEDULE_ACTION_ACKNOWLEDGE, 0,
                        QM_SCHEDULE_CLOSE_USER_CLOSED);
}

int qm_schedule_complete(qm_schedule_t schedule,
                         const qm_schedule_action_t *action)
{
    return apply_action(schedule, action, QM_SCHEDULE_ACTION_COMPLETE, 0,
                        QM_SCHEDULE_CLOSE_OCCURRENCE_COMPLETED);
}

int qm_schedule_snooze(qm_schedule_t schedule,
                       const qm_schedule_action_t *action,
                       uint16_t duration_min)
{
    if (duration_min != 0 && duration_min != 5 &&
        duration_min != 10 && duration_min != 30) {
        return -QM_EINVAL;
    }
    return apply_action(schedule, action, QM_SCHEDULE_ACTION_SNOOZE,
                        duration_min, QM_SCHEDULE_CLOSE_USER_CLOSED);
}

int qm_schedule_dismiss(qm_schedule_t schedule,
                        const qm_schedule_action_t *action,
                        qm_schedule_close_reason_t reason)
{
    if (reason == QM_SCHEDULE_CLOSE_OCCURRENCE_COMPLETED) {
        return -QM_EINVAL;
    }
    return apply_action(schedule, action, QM_SCHEDULE_ACTION_DISMISS, 0, reason);
}

int qm_schedule_get_status(qm_schedule_t schedule,
                           qm_schedule_status_t *status)
{
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (!status) {
        return -QM_EINVAL;
    }
    (void)qm_schedule_expire_binding_if_needed(schedule, current_utc());

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    status->sync_state = schedule->store.meta.sync_state;
    status->desired_revision = schedule->store.meta.desired_revision;
    status->confirmed_revision = schedule->store.meta.confirmed_revision;
    snprintf(status->binding_version, sizeof(status->binding_version), "%s",
             schedule->store.meta.binding_version);
    status->running = schedule->running;
    status->schedulable = schedule->schedulable;
    qm_mutex_unlock(&schedule->lock);
    return QM_EOK;
}

int qm_schedule_get_item(qm_schedule_t schedule,
                         const char *schedule_id,
                         qm_schedule_item_snapshot_t *item)
{
    int idx;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (str_empty(schedule_id) || !item) {
        return -QM_EINVAL;
    }
    (void)qm_schedule_expire_binding_if_needed(schedule, current_utc());

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    idx = find_item_locked(schedule, schedule_id);
    if (idx < 0) {
        qm_mutex_unlock(&schedule->lock);
        return -QM_ENOENT;
    }
    fill_snapshot(item, &schedule->store.items[idx]);
    qm_mutex_unlock(&schedule->lock);
    return QM_EOK;
}

int qm_schedule_list_items(qm_schedule_t schedule,
                           qm_schedule_item_snapshot_t *items,
                           uint16_t capacity,
                           uint16_t *count)
{
    uint16_t written = 0;
    uint16_t total = 0;
    uint16_t i;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (!count || (capacity > 0 && !items)) {
        return -QM_EINVAL;
    }
    (void)qm_schedule_expire_binding_if_needed(schedule, current_utc());

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    for (i = 0; i < schedule->config.max_items; i++) {
        if (!schedule->store.items[i].used) {
            continue;
        }
        total++;
        if (written < capacity) {
            fill_snapshot(&items[written], &schedule->store.items[i]);
            written++;
        }
    }
    qm_mutex_unlock(&schedule->lock);
    *count = total;
    return (total > capacity) ? -QM_EFULL : QM_EOK;
}

int qm_schedule_get_capability(qm_schedule_t schedule,
                               qm_schedule_capability_t *capability)
{
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (!capability) {
        return -QM_EINVAL;
    }

    memset(capability, 0, sizeof(*capability));
    capability->max_items = schedule->config.max_items;
    capability->max_reminders_per_item = schedule->config.max_reminders_per_item;
    capability->max_outbox_user = schedule->config.max_outbox_user;
    capability->max_outbox_system = schedule->config.max_outbox_system;
    capability->max_sync_chunks = QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL;
    capability->max_sync_items_per_chunk =
        QM_SCHEDULE_MAX_SYNC_ITEMS_PER_CHUNK_INTERNAL;
    capability->max_payload_len = CONFIG_QM_SCHEDULE_IOT_MAX_PAYLOAD_LEN;
    capability->supports_fixed_offset_timezone = true;
    capability->supports_iana_timezone = false;
    capability->supports_double_bank_store = true;
    capability->supports_paged_store = false;
    return QM_EOK;
}

int qm_schedule_request_sync(qm_schedule_t schedule,
                             const char *event_id,
                             int64_t event_at)
{
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (event_at <= 0) {
        event_at = current_utc();
    }
    if (str_too_long(event_id, QM_SCHEDULE_EVENT_ID_MAX_DEFAULT)) {
        return -QM_EINVAL;
    }
    if (!qm_schedule_binding_valid(schedule, current_utc())) {
        return -QM_EBUSY;
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    ret = append_system_outbox_locked(
        schedule, QM_SCHEDULE_OUTBOX_SYSTEM_SYNC_REQUEST, event_id,
        schedule->store.meta.binding_version, NULL, event_at);
    if (ret == QM_EOK) {
        schedule->store.meta.sync_state = QM_SCHEDULE_SYNC_PENDING;
    }
    qm_mutex_unlock(&schedule->lock);
    if (ret != QM_EOK) {
        return ret;
    }
    return qm_schedule_store_save_all(schedule);
}

int qm_schedule_cleanup(qm_schedule_t schedule,
                        int64_t now_utc)
{
    uint16_t i;
    int changed = 0;
    int ret;

    ret = enter(schedule);
    if (ret != QM_EOK) {
        return ret;
    }
    if (now_utc <= 0) {
        now_utc = current_utc();
    }

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (schedule->store.staging_active &&
        schedule->store.staging_started_at > 0 &&
        schedule->store.staging_started_at +
            CONFIG_QM_SCHEDULE_SYNC_STAGING_TIMEOUT_SEC < now_utc) {
        memset(schedule->store.staged_items, 0,
               sizeof(schedule->store.staged_items));
        memset(schedule->store.staging_chunk_digests, 0,
               sizeof(schedule->store.staging_chunk_digests));
        memset(schedule->store.staging_chunk_item_counts, 0,
               sizeof(schedule->store.staging_chunk_item_counts));
        schedule->store.staging_active = false;
        schedule->store.staging_replace = false;
        schedule->store.staging_full_projection = false;
        schedule->store.staging_sync_id[0] = '\0';
        schedule->store.staging_group_id[0] = '\0';
        schedule->store.staging_group_revision = 0;
        schedule->store.staging_group_count = 0;
        schedule->store.staging_total_chunks = 0;
        schedule->store.staging_total_items = 0;
        schedule->store.staging_received_mask = 0;
        schedule->store.staging_item_count = 0;
        schedule->store.staging_started_at = 0;
        schedule->store.meta.sync_state = QM_SCHEDULE_SYNC_FAILED;
        changed = 1;
    }

    for (i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
        qm_schedule_occurrence_record_t *occ = &schedule->store.occurrences[i];
        if (!occ->used) {
            continue;
        }
        if (occ->reminder_state == QM_SCHEDULE_REMINDER_PENDING ||
            occ->reminder_state == QM_SCHEDULE_REMINDER_ACTIVE ||
            occ->reminder_state == QM_SCHEDULE_REMINDER_SNOOZED) {
            int64_t expiry = occ->snooze_until > 0 ?
                occ->snooze_until : occ->occurrence_at;
            int64_t recovery_window = occ->snooze_until > 0 ?
                CONFIG_QM_SCHEDULE_SNOOZE_RECOVERY_WINDOW_SEC :
                CONFIG_QM_SCHEDULE_MISSED_WINDOW_SEC;
            if (expiry + recovery_window < now_utc) {
                qm_schedule_action_t expired_action;
                char expired_event_id[QM_SCHEDULE_EVENT_ID_MAX_DEFAULT];
                memset(&expired_action, 0, sizeof(expired_action));
                snprintf(expired_event_id, sizeof(expired_event_id),
                         "dismiss:%lld:%u:expired",
                         (long long)occ->occurrence_at,
                         (unsigned int)i);
                expired_action.schedule_id = occ->schedule_id;
                expired_action.occurrence_at = occ->occurrence_at;
                expired_action.reminder_id = occ->reminder_id;
                expired_action.action_at = now_utc;
                expired_action.operation_id = expired_event_id;
                expired_action.event_id = expired_event_id;
                expired_action.binding_version = schedule->store.meta.binding_version;
                (void)append_outbox_locked(schedule, &expired_action,
                                           QM_SCHEDULE_ACTION_DISMISS, NULL);
                occ->reminder_state = QM_SCHEDULE_REMINDER_CLOSED;
                occ->occurrence_state = QM_SCHEDULE_OCCURRENCE_EXPIRED;
                occ->close_reason = QM_SCHEDULE_CLOSE_EXPIRED;
                for (uint16_t outbox_index = 0;
                     outbox_index < schedule->config.max_outbox_user;
                     outbox_index++) {
                    qm_schedule_outbox_record_t *out =
                        &schedule->store.outbox[outbox_index];
                    if (out->used && strcmp(out->event_id,
                                           expired_event_id) == 0) {
                        out->close_reason = QM_SCHEDULE_CLOSE_EXPIRED;
                        break;
                    }
                }
                changed = 1;
            }
        } else {
            int64_t retained_at = occ->action_at > 0 ?
                occ->action_at : occ->occurrence_at;
            if (retained_at > 0 &&
                retained_at + CONFIG_QM_SCHEDULE_OCCURRENCE_RETENTION_SEC <
                    now_utc) {
                memset(occ, 0, sizeof(*occ));
                changed = 1;
            }
        }
    }

    for (i = 0; i < QM_SCHEDULE_MAX_TOMBSTONES_INTERNAL; i++) {
        qm_schedule_tombstone_record_t *tomb = &schedule->store.tombstones[i];
        if (!tomb->used) {
            continue;
        }
        if (tomb->deleted_at > 0 &&
            tomb->deleted_at + CONFIG_QM_SCHEDULE_TOMBSTONE_RETENTION_SEC <
                now_utc) {
            memset(tomb, 0, sizeof(*tomb));
            changed = 1;
        }
    }
    qm_mutex_unlock(&schedule->lock);

    if (changed) {
        return persist_and_rebuild(schedule);
    }
    return QM_EOK;
}
