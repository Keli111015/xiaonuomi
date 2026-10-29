#include "qm_schedule_iot.h"

#include "qm_schedule_internal.h"

#include "backoff_algorithm.h"
#include "cJSON.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_utils_sha256.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include "qm_time.h"

struct qm_schedule_iot {
    qm_schedule_iot_config_t config;
};

static const char *json_string(cJSON *obj, const char *name);
static const char *close_reason_name(qm_schedule_close_reason_t reason);

typedef struct {
    char *data;
    uint32_t length;
    uint32_t capacity;
} jcs_buffer_t;

typedef struct {
    bool idempotent;
    bool applied;
    bool deleted;
    bool enabled;
    bool complete;
    uint16_t received_chunks;
    uint16_t applied_items;
    uint16_t deleted_items;
} dispatch_result_t;

static int jcs_write(jcs_buffer_t *buffer, const char *text, uint32_t length)
{
    if (!buffer || !text || length > buffer->capacity - buffer->length) {
        return -QM_EFULL;
    }
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    return QM_EOK;
}

static int jcs_char(jcs_buffer_t *buffer, char value)
{
    return jcs_write(buffer, &value, 1);
}

static int utf8_valid(const char *text)
{
    const unsigned char *p = (const unsigned char *)text;

    if (!text) {
        return false;
    }
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
            if ((p[i] & 0xc0u) != 0x80u) {
                return false;
            }
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

static bool object_keys_unique_and_valid(const cJSON *item)
{
    const cJSON *child;

    if (!item) {
        return false;
    }
    if (cJSON_IsString(item)) {
        return utf8_valid(item->valuestring);
    }
    if (cJSON_IsObject(item)) {
        for (child = item->child; child; child = child->next) {
            const cJSON *other;

            if (!child->string || !utf8_valid(child->string) ||
                !object_keys_unique_and_valid(child)) {
                return false;
            }
            for (other = child->next; other; other = other->next) {
                if (other->string && strcmp(child->string, other->string) == 0) {
                    return false;
                }
            }
        }
    } else if (cJSON_IsArray(item)) {
        for (child = item->child; child; child = child->next) {
            if (!object_keys_unique_and_valid(child)) {
                return false;
            }
        }
    }
    return true;
}

static int jcs_append_string(jcs_buffer_t *buffer, const char *text)
{
    const unsigned char *p = (const unsigned char *)(text ? text : "");
    char escaped[7];

    if (jcs_char(buffer, '"') != QM_EOK) {
        return -QM_EFULL;
    }
    while (*p != '\0') {
        switch (*p) {
        case '"':
            if (jcs_write(buffer, "\\\"", 2) != QM_EOK) return -QM_EFULL;
            break;
        case '\\':
            if (jcs_write(buffer, "\\\\", 2) != QM_EOK) return -QM_EFULL;
            break;
        case '\b':
            if (jcs_write(buffer, "\\b", 2) != QM_EOK) return -QM_EFULL;
            break;
        case '\f':
            if (jcs_write(buffer, "\\f", 2) != QM_EOK) return -QM_EFULL;
            break;
        case '\n':
            if (jcs_write(buffer, "\\n", 2) != QM_EOK) return -QM_EFULL;
            break;
        case '\r':
            if (jcs_write(buffer, "\\r", 2) != QM_EOK) return -QM_EFULL;
            break;
        case '\t':
            if (jcs_write(buffer, "\\t", 2) != QM_EOK) return -QM_EFULL;
            break;
        default:
            if (*p < 0x20u) {
                snprintf(escaped, sizeof(escaped), "\\u%04x", *p);
                if (jcs_write(buffer, escaped, 6) != QM_EOK) return -QM_EFULL;
            } else if (jcs_char(buffer, (char)*p) != QM_EOK) {
                return -QM_EFULL;
            }
            break;
        }
        p++;
    }
    return jcs_char(buffer, '"');
}

typedef struct {
    const unsigned char *next;
    uint16_t low_surrogate;
    bool has_low_surrogate;
} utf16_iterator_t;

static int utf16_next(utf16_iterator_t *iterator)
{
    const unsigned char *p;
    uint32_t codepoint;
    uint8_t length;

    if (iterator->has_low_surrogate) {
        iterator->has_low_surrogate = false;
        return iterator->low_surrogate;
    }
    p = iterator->next;
    if (!p || *p == '\0') return -1;
    if (*p < 0x80u) {
        codepoint = *p++;
        length = 1;
    } else if ((*p & 0xe0u) == 0xc0u) {
        codepoint = *p++ & 0x1fu;
        length = 2;
    } else if ((*p & 0xf0u) == 0xe0u) {
        codepoint = *p++ & 0x0fu;
        length = 3;
    } else {
        codepoint = *p++ & 0x07u;
        length = 4;
    }
    for (uint8_t i = 1; i < length; i++) {
        codepoint = (codepoint << 6) | (*p++ & 0x3fu);
    }
    iterator->next = p;
    if (codepoint <= 0xffffu) return (int)codepoint;
    codepoint -= 0x10000u;
    iterator->low_surrogate = (uint16_t)(0xdc00u + (codepoint & 0x3ffu));
    iterator->has_low_surrogate = true;
    return (int)(0xd800u + (codepoint >> 10));
}

static int utf16_compare(const char *left, const char *right)
{
    utf16_iterator_t a = {(const unsigned char *)left, 0, false};
    utf16_iterator_t b = {(const unsigned char *)right, 0, false};

    while (true) {
        int left_unit = utf16_next(&a);
        int right_unit = utf16_next(&b);
        if (left_unit < 0 || right_unit < 0) {
            return left_unit == right_unit ? 0 : (left_unit < 0 ? -1 : 1);
        }
        if (left_unit != right_unit) return left_unit < right_unit ? -1 : 1;
    }
}

static int jcs_key_compare(const void *left, const void *right)
{
    const cJSON *a = *(const cJSON *const *)left;
    const cJSON *b = *(const cJSON *const *)right;

    return utf16_compare(a->string ? a->string : "", b->string ? b->string : "");
}

static int jcs_append_value(jcs_buffer_t *buffer, const cJSON *item)
{
    if (cJSON_IsNull(item)) {
        return jcs_write(buffer, "null", 4);
    }
    if (cJSON_IsTrue(item)) {
        return jcs_write(buffer, "true", 4);
    }
    if (cJSON_IsFalse(item)) {
        return jcs_write(buffer, "false", 5);
    }
    if (cJSON_IsString(item)) {
        return jcs_append_string(buffer, item->valuestring);
    }
    if (cJSON_IsNumber(item)) {
        char number[64];
        double value = item->valuedouble;
        int written;

        if (!isfinite(value)) {
            return -QM_EINVAL;
        }
        if (value == 0.0) {
            return jcs_char(buffer, '0');
        }
        written = snprintf(number, sizeof(number), "%.17g", value);
        if (written < 0 || (uint32_t)written >= sizeof(number)) {
            return -QM_EINVAL;
        }
        return jcs_write(buffer, number, (uint32_t)written);
    }
    if (cJSON_IsArray(item)) {
        const cJSON *child;
        bool first = true;

        if (jcs_char(buffer, '[') != QM_EOK) return -QM_EFULL;
        for (child = item->child; child; child = child->next) {
            if (!first && jcs_char(buffer, ',') != QM_EOK) return -QM_EFULL;
            if (jcs_append_value(buffer, child) != QM_EOK) return -QM_EFULL;
            first = false;
        }
        return jcs_char(buffer, ']');
    }
    if (cJSON_IsObject(item)) {
        const cJSON *child;
        const cJSON **children;
        uint16_t count = 0;
        uint16_t i;

        for (child = item->child; child; child = child->next) count++;
        children = count ? (const cJSON **)qm_malloc(
            (uint32_t)count * sizeof(*children)) : NULL;
        if (count && !children) return -QM_ENOMEM;
        i = 0;
        for (child = item->child; child; child = child->next) children[i++] = child;
        if (count) qsort(children, count, sizeof(*children), jcs_key_compare);
        if (jcs_char(buffer, '{') != QM_EOK) { qm_free(children); return -QM_EFULL; }
        for (i = 0; i < count; i++) {
            if (i && jcs_char(buffer, ',') != QM_EOK) { qm_free(children); return -QM_EFULL; }
            if (jcs_append_string(buffer, children[i]->string) != QM_EOK ||
                jcs_char(buffer, ':') != QM_EOK ||
                jcs_append_value(buffer, children[i]) != QM_EOK) {
                qm_free(children);
                return -QM_EFULL;
            }
        }
        qm_free(children);
        return jcs_char(buffer, '}');
    }
    return -QM_EINVAL;
}

static int verify_sync_digest(cJSON *params)
{
    static const char *const names[] = {
        "desiredRevision", "syncId", "syncScope", "groupId",
        "groupRevision", "groups", "replace", "chunkIndex",
        "chunkCount", "totalItems", "schedules"
    };
    cJSON *semantic = NULL;
    const char *expected;
    char *canonical = NULL;
    char actual[QM_SCHEDULE_DIGEST_MAX];
    uint8_t digest[SHA256_DIGEST_LENGTH];
    qm_sha256_context hash;
    jcs_buffer_t buffer;
    uint8_t i;
    int written;
    int ret = QM_EOK;

    if (!params || !object_keys_unique_and_valid(params)) return -QM_EINVAL;
    expected = json_string(params, "contentDigest");
    if (!expected || strncmp(expected, "sha256:", 7) != 0 ||
        strlen(expected) != 71) return -QM_EINVAL;
    semantic = cJSON_CreateObject();
    if (!semantic) return -QM_ENOMEM;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        cJSON *value = cJSON_GetObjectItemCaseSensitive(params, names[i]);
        if (!value) {
            if (strcmp(names[i], "groupId") == 0 ||
                strcmp(names[i], "groupRevision") == 0 ||
                strcmp(names[i], "groups") == 0) continue;
            ret = -QM_EINVAL;
            break;
        }
        cJSON_AddItemToObject(semantic, names[i], cJSON_Duplicate(value, true));
    }
    if (ret != QM_EOK) { cJSON_Delete(semantic); return ret; }
    canonical = (char *)qm_malloc(CONFIG_QM_SCHEDULE_IOT_MAX_PAYLOAD_LEN);
    if (!canonical) { cJSON_Delete(semantic); return -QM_ENOMEM; }
    buffer.data = canonical;
    buffer.length = 0;
    buffer.capacity = CONFIG_QM_SCHEDULE_IOT_MAX_PAYLOAD_LEN;
    ret = jcs_append_value(&buffer, semantic);
    if (ret == QM_EOK) {
        qm_utils_sha256_init(&hash);
        qm_utils_sha256_starts(&hash);
        qm_utils_sha256_update(&hash, (const unsigned char *)canonical,
                               buffer.length);
        qm_utils_sha256_finish(&hash, digest);
        qm_utils_sha256_free(&hash);
        written = snprintf(actual, sizeof(actual), "sha256:");
        for (i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            snprintf(actual + written + ((uint16_t)i * 2u), 3, "%02x",
                     digest[i]);
        }
        ret = strcmp(actual, expected) == 0 ? QM_EOK : -QM_EINVAL;
    }
    qm_free(canonical);
    cJSON_Delete(semantic);
    return ret;
}

static const char *json_string(cJSON *obj, const char *name)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);

    return cJSON_IsString(item) ? item->valuestring : NULL;
}

static int json_int(cJSON *obj, const char *name, int required, int64_t *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);
    double value;

    if (!item) {
        return required ? -QM_EINVAL : QM_EOK;
    }
    if (!cJSON_IsNumber(item)) {
        return -QM_EINVAL;
    }
    value = item->valuedouble;
    if (!isfinite(value) || floor(value) != value ||
        value < -9223372036854774784.0 ||
        value > 9223372036854774784.0) {
        return -QM_EINVAL;
    }
    *out = (int64_t)value;
    return QM_EOK;
}

static int json_bool(cJSON *obj, const char *name, int required, bool *out)
{
    cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, name);

    if (!item) {
        return required ? -QM_EINVAL : QM_EOK;
    }
    if (cJSON_IsBool(item)) {
        *out = cJSON_IsTrue(item) ? true : false;
        return QM_EOK;
    }
    return -QM_EINVAL;
}

static int json_compact(cJSON *item,
                        cJSON_bool (*type_check)(const cJSON *),
                        const char *default_json,
                        char *out,
                        uint32_t out_len)
{
    if (!out || out_len == 0) {
        return -QM_EINVAL;
    }
    if (!item) {
        if (strlen(default_json) >= out_len) {
            return -QM_EFULL;
        }
        strcpy(out, default_json);
        return QM_EOK;
    }
    if (!type_check(item)) {
        return -QM_EINVAL;
    }
    if (!cJSON_PrintPreallocated(item, out, (int)out_len, false)) {
        return -QM_EFULL;
    }
    return QM_EOK;
}

static bool extensions_valid(const cJSON *item)
{
    const cJSON *child;

    if (!item) return true;
    if (!cJSON_IsObject(item)) return false;
    for (child = item->child; child; child = child->next) {
        cJSON *version;
        if (!cJSON_IsObject(child)) return false;
        version = cJSON_GetObjectItemCaseSensitive(child, "schemaVersion");
        if (!cJSON_IsNumber(version) || !isfinite(version->valuedouble) ||
            version->valuedouble < 1.0 || floor(version->valuedouble) !=
            version->valuedouble) {
            return false;
        }
    }
    return true;
}

static bool channels_valid(const cJSON *item)
{
    const cJSON *child;

    if (!item) return true;
    if (!cJSON_IsArray(item)) return false;
    for (child = item->child; child; child = child->next) {
        if (!cJSON_IsString(child) || !utf8_valid(child->valuestring)) {
            return false;
        }
    }
    return true;
}

static const char *sync_state_name(qm_schedule_sync_state_t state)
{
    switch (state) {
    case QM_SCHEDULE_SYNC_NOT_REQUIRED:
        return "notRequired";
    case QM_SCHEDULE_SYNC_PENDING:
        return "pending";
    case QM_SCHEDULE_SYNC_SYNCING:
        return "syncing";
    case QM_SCHEDULE_SYNC_SYNCED:
        return "synced";
    case QM_SCHEDULE_SYNC_FAILED:
        return "failed";
    default:
        return "unknown";
    }
}

static const char *error_name(int ret)
{
    switch (ret) {
    case QM_EOK:
        return "OK";
    case -QM_EINVAL:
        return "INVALID_REQUEST";
    case -QM_EFULL:
        return "CAPACITY_EXCEEDED";
    case -QM_ENOMEM:
        return "NO_MEMORY";
    case -QM_ENOSYS:
        return "UNSUPPORTED_OPERATION";
    case -QM_EBUSY:
        return "BUSY";
    case -QM_ENOENT:
        return "NOT_FOUND";
    case -QM_SCHEDULE_E_ALREADY_EXISTS:
        return "ALREADY_EXISTS";
    case -QM_SCHEDULE_E_REVISION_CONFLICT:
        return "REVISION_CONFLICT";
    case -QM_SCHEDULE_E_SYNC_CONFLICT:
        return "SYNC_CONFLICT";
    case -QM_SCHEDULE_E_SYNC_INCOMPLETE:
        return "SYNC_INCOMPLETE";
    case -QM_SCHEDULE_E_BINDING_INVALID:
        return "BINDING_INVALID";
    case -QM_SCHEDULE_E_INVALID_TIMEZONE:
        return "INVALID_TIMEZONE";
    case -QM_SCHEDULE_E_OCCURRENCE_NOT_ACTIVE:
        return "OCCURRENCE_NOT_ACTIVE";
    case -QM_SCHEDULE_E_SNOOZE_NOT_ALLOWED:
        return "SNOOZE_NOT_ALLOWED";
    case -QM_SCHEDULE_E_CAPABILITY_UNSUPPORTED:
        return "CAPABILITY_UNSUPPORTED";
    case -QM_SCHEDULE_E_OUTBOX_FULL:
        return "OUTBOX_FULL";
    case -QM_SCHEDULE_E_UNSUPPORTED_SCHEMA:
        return "UNSUPPORTED_SCHEMA";
    default:
        return "ERROR";
    }
}

static int validate_schema(cJSON *params)
{
    int64_t schema = 0;

    if (json_int(params, "schemaVersion", 1, &schema) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (schema != 1) {
        return -QM_SCHEDULE_E_UNSUPPORTED_SCHEMA;
    }
    return QM_EOK;
}

static uint32_t append_json_string(char *reply,
                                   uint32_t reply_len,
                                   uint32_t used,
                                   const char *value);

static int parse_recurrence(cJSON *schedule,
                            qm_schedule_recurrence_t *out)
{
    cJSON *recurrence = cJSON_GetObjectItemCaseSensitive(schedule, "recurrence");
    const char *value = NULL;

    if (!out) {
        return -QM_EINVAL;
    }
    if (!recurrence || cJSON_IsNull(recurrence)) {
        *out = QM_SCHEDULE_RECURRENCE_ONCE;
        return QM_EOK;
    }
    if (cJSON_IsObject(recurrence)) {
        value = json_string(recurrence, "frequency");
    }
    if (!value) {
        return -QM_EINVAL;
    }
    if (strcmp(value, "daily") == 0) {
        *out = QM_SCHEDULE_RECURRENCE_DAILY;
        return QM_EOK;
    }
    if (strcmp(value, "weekly") == 0) {
        *out = QM_SCHEDULE_RECURRENCE_WEEKLY;
        return QM_EOK;
    }
    return -QM_EINVAL;
}

static int parse_weekday_mask(cJSON *schedule, uint8_t *mask)
{
    cJSON *recurrence;
    cJSON *days;
    int i;

    if (!schedule || !mask) {
        return -QM_EINVAL;
    }
    recurrence = cJSON_GetObjectItemCaseSensitive(schedule, "recurrence");
    if (!cJSON_IsObject(recurrence)) {
        return QM_EOK;
    }
    days = cJSON_GetObjectItemCaseSensitive(recurrence, "byWeekday");
    if (!days) {
        return QM_EOK;
    }
    if (!cJSON_IsArray(days)) {
        return -QM_EINVAL;
    }
    *mask = 0;
    for (i = 0; i < cJSON_GetArraySize(days); i++) {
        cJSON *day = cJSON_GetArrayItem(days, i);
        const char *name = cJSON_IsString(day) ? day->valuestring : NULL;
        int weekday = -1;

        if (!name) {
            return -QM_EINVAL;
        }
        if (strcmp(name, "SU") == 0 || strcmp(name, "Sun") == 0) {
            weekday = 0;
        } else if (strcmp(name, "MO") == 0 || strcmp(name, "Mon") == 0) {
            weekday = 1;
        } else if (strcmp(name, "TU") == 0 || strcmp(name, "Tue") == 0) {
            weekday = 2;
        } else if (strcmp(name, "WE") == 0 || strcmp(name, "Wed") == 0) {
            weekday = 3;
        } else if (strcmp(name, "TH") == 0 || strcmp(name, "Thu") == 0) {
            weekday = 4;
        } else if (strcmp(name, "FR") == 0 || strcmp(name, "Fri") == 0) {
            weekday = 5;
        } else if (strcmp(name, "SA") == 0 || strcmp(name, "Sat") == 0) {
            weekday = 6;
        } else {
            return -QM_EINVAL;
        }
        if ((*mask & (uint8_t)(1u << weekday)) != 0) {
            return -QM_EINVAL;
        }
        *mask |= (uint8_t)(1u << weekday);
    }
    return QM_EOK;
}

static int parse_ctx(cJSON *params, qm_schedule_apply_ctx_t *ctx)
{
    int64_t desired = 0;

    memset(ctx, 0, sizeof(*ctx));
    ctx->operation_id = json_string(params, "operationId");
    ctx->binding_version = json_string(params, "bindingVersion");
    if (json_int(params, "desiredRevision", 1, &desired) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (desired < 0 || desired > UINT32_MAX) {
        return -QM_EINVAL;
    }
    ctx->desired_revision = (uint32_t)desired;
    return QM_EOK;
}

static int parse_schedule_item(cJSON *obj,
                               qm_schedule_item_t *item,
                               qm_schedule_reminder_t *reminders,
                               uint8_t reminder_cap,
                               char *extensions_json,
                               uint32_t extensions_len,
                               char *channels_json,
                               uint32_t channels_stride)
{
    int64_t value = 0;
    cJSON *array;
    cJSON *recurrence_json;
    int i;
    int count;

    memset(item, 0, sizeof(*item));
    item->id = json_string(obj, "id");
    item->type = json_string(obj, "type");
    item->source = json_string(obj, "source");
    item->group_id = json_string(obj, "groupId");
    item->title = json_string(obj, "title");
    item->description = json_string(obj, "description");
    item->location = json_string(obj, "location");
    item->priority = json_string(obj, "priority");
    item->category = json_string(obj, "category");
    item->sensitivity = json_string(obj, "sensitivity");
    item->timezone = json_string(obj, "timezone");
    item->status = json_string(obj, "status");
    item->missed_policy = json_string(obj, "missedPolicy");
    item->projection_key = json_string(obj, "projectionKey");
    if (!extensions_valid(cJSON_GetObjectItemCaseSensitive(obj,
                                                           "extensions")) ||
        json_compact(cJSON_GetObjectItemCaseSensitive(obj, "extensions"),
                     cJSON_IsObject, "{}", extensions_json,
                     extensions_len) != QM_EOK) {
        return -QM_EINVAL;
    }
    item->extensions_json = extensions_json;
    recurrence_json = cJSON_GetObjectItemCaseSensitive(obj, "recurrence");
    if (recurrence_json && !cJSON_IsNull(recurrence_json) &&
        !cJSON_IsObject(recurrence_json)) {
        return -QM_EINVAL;
    }
    if (parse_recurrence(obj, &item->recurrence) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (json_int(obj, "revision", 1, &value) != QM_EOK ||
        value <= 0 || value > UINT32_MAX) {
        return -QM_EINVAL;
    }
    item->revision = (uint32_t)value;
    if (json_int(obj, "startAt", 1, &item->start_at) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (json_int(obj, "endAt", 0, &item->end_at) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (json_bool(obj, "allDay", 0, &item->all_day) != QM_EOK ||
        item->all_day) {
        return -QM_EINVAL;
    }
    value = 0;
    if (json_int(obj, "weeklyMask", 0, &value) != QM_EOK ||
        value < 0 || value > UINT8_MAX) {
        return -QM_EINVAL;
    }
    item->weekly_mask = (uint8_t)value;
    if (item->weekly_mask == 0 &&
        parse_weekday_mask(obj, &item->weekly_mask) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (json_int(obj, "recurrenceUntil", 0, &item->recurrence_until) != QM_EOK) {
        return -QM_EINVAL;
    }
    {
        int64_t interval = 1;

        if (cJSON_IsObject(recurrence_json)) {
            int64_t start_date_utc = 0;
            int64_t end_date_utc = 0;
            item->recurrence_start_date = json_string(recurrence_json,
                                                       "startDate");
            item->recurrence_end_date = json_string(recurrence_json, "endDate");
            if (json_int(recurrence_json, "interval", 0, &interval) != QM_EOK ||
                interval != 1) {
                return -QM_EINVAL;
            }
            if (item->recurrence != QM_SCHEDULE_RECURRENCE_ONCE &&
                !item->recurrence_start_date) {
                return -QM_EINVAL;
            }
            if (item->recurrence == QM_SCHEDULE_RECURRENCE_DAILY &&
                cJSON_GetObjectItemCaseSensitive(recurrence_json,
                                                 "byWeekday")) {
                return -QM_EINVAL;
            }
            if (item->recurrence_start_date) {
                if (qm_schedule_recurrence_date_to_utc(
                        item->recurrence_start_date, item->timezone, false,
                        &start_date_utc) != QM_EOK ||
                    item->start_at < start_date_utc) {
                    return -QM_EINVAL;
                }
            }
            if (item->recurrence_end_date) {
                if (qm_schedule_recurrence_date_to_utc(
                        item->recurrence_end_date, item->timezone, true,
                        &end_date_utc) != QM_EOK ||
                    (item->recurrence_start_date &&
                     start_date_utc > end_date_utc) ||
                    item->start_at > end_date_utc) {
                    return -QM_EINVAL;
                }
            }
            if (item->recurrence_end_date &&
                qm_schedule_recurrence_date_to_utc(
                    item->recurrence_end_date, item->timezone, true,
                    &item->recurrence_until) != QM_EOK) {
                return -QM_EINVAL;
            }
        }
        item->recurrence_interval = (uint16_t)interval;
    }
    item->enabled = true;
    if (json_bool(obj, "enabled", 1, &item->enabled) != QM_EOK) {
        return -QM_EINVAL;
    }

    array = cJSON_GetObjectItemCaseSensitive(obj, "reminders");
    if (!array) {
        item->reminders = NULL;
        item->reminder_count = 0;
        return QM_EOK;
    }
    if (!cJSON_IsArray(array)) {
        return -QM_EINVAL;
    }
    count = cJSON_GetArraySize(array);
    if (count > reminder_cap) {
        return -QM_EFULL;
    }
    memset(reminders, 0, sizeof(reminders[0]) * reminder_cap);
    for (i = 0; i < count; i++) {
        cJSON *reminder = cJSON_GetArrayItem(array, i);
        cJSON *snooze;
        int64_t offset = 0;
        int64_t interval = 0;
        int64_t max_count = 0;

        if (!cJSON_IsObject(reminder)) {
            return -QM_EINVAL;
        }
        reminders[i].id = json_string(reminder, "id");
        reminders[i].action = json_string(reminder, "action");
        if (!reminders[i].action) {
            reminders[i].action = "notify";
        }
        if (!channels_valid(cJSON_GetObjectItemCaseSensitive(reminder,
                                                             "channels")) ||
            json_compact(cJSON_GetObjectItemCaseSensitive(reminder,
                                                          "channels"),
                         cJSON_IsArray, "[]",
                         channels_json + ((uint32_t)i * channels_stride),
                         channels_stride) != QM_EOK) {
            return -QM_EINVAL;
        }
        reminders[i].channels_json =
            channels_json + ((uint32_t)i * channels_stride);
        if (json_int(reminder, "offsetMin", 1, &offset) != QM_EOK) {
            return -QM_EINVAL;
        }
        reminders[i].offset_min = (int32_t)offset;
        snooze = cJSON_GetObjectItemCaseSensitive(reminder, "snooze");
        if (snooze && !cJSON_IsObject(snooze)) {
            return -QM_EINVAL;
        }
        if (json_bool(reminder, "requiresAck", 0,
                      &reminders[i].requires_ack) != QM_EOK ||
            json_bool(reminder, "snoozeEnabled", 0,
                      &reminders[i].snooze_enabled) != QM_EOK ||
            json_int(reminder, "snoozeIntervalMin", 0, &interval) != QM_EOK ||
            json_int(reminder, "snoozeMaxCount", 0, &max_count) != QM_EOK ||
            offset < INT32_MIN || offset > INT32_MAX ||
            interval < 0 || interval > UINT16_MAX ||
            max_count < 0 || max_count > UINT8_MAX) {
            return -QM_EINVAL;
        }
        if (snooze) {
            if (json_bool(snooze, "enabled", 0,
                          &reminders[i].snooze_enabled) != QM_EOK ||
                json_int(snooze, "intervalMin", 0, &interval) != QM_EOK ||
                json_int(snooze, "maxCount", 0, &max_count) != QM_EOK ||
                interval < 0 || interval > UINT16_MAX ||
                max_count < 0 || max_count > UINT8_MAX) {
                return -QM_EINVAL;
            }
        }
        reminders[i].snooze_interval_min = (uint16_t)interval;
        reminders[i].snooze_max_count = (uint8_t)max_count;
    }
    item->reminders = reminders;
    item->reminder_count = (uint8_t)count;
    return QM_EOK;
}

static void build_reply(char *reply,
                        uint32_t reply_len,
                        qm_schedule_t schedule,
                        const char *sub_method,
                        cJSON *params,
                        int id,
                        int ret)
{
    const char *ack = "unknownAck";
    int code = (ret == QM_EOK) ? 0 : 1;
    const char *operation_id = params ? json_string(params, "operationId") : "";
    const char *binding_version = params ? json_string(params, "bindingVersion") : "";
    qm_schedule_status_t status;
    uint32_t desired = 0;
    uint32_t confirmed = 0;
    const char *sync_state = "unknown";
    char operation_json[192];
    char binding_json[192];
    char sub_method_json[192];
    char sub_method_ack[192];
    char sub_method_ack_json[192];
    char desired_json[24];
    char confirmed_json[24];
    int written;

    if (!operation_id) {
        operation_id = "";
    }
    if (!binding_version) {
        binding_version = "";
    }
    memset(&status, 0, sizeof(status));
    if (schedule && qm_schedule_get_status(schedule, &status) == QM_EOK) {
        desired = status.desired_revision;
        confirmed = status.confirmed_revision;
        sync_state = sync_state_name(status.sync_state);
        if (binding_version[0] == '\0') {
            binding_version = status.binding_version;
        }
    }

    memset(operation_json, 0, sizeof(operation_json));
    memset(binding_json, 0, sizeof(binding_json));
    memset(sub_method_json, 0, sizeof(sub_method_json));
    memset(sub_method_ack, 0, sizeof(sub_method_ack));
    memset(sub_method_ack_json, 0, sizeof(sub_method_ack_json));
    (void)append_json_string(operation_json, sizeof(operation_json), 0,
                             operation_id);
    (void)append_json_string(binding_json, sizeof(binding_json), 0,
                             binding_version);
    (void)append_json_string(sub_method_json, sizeof(sub_method_json), 0,
                             sub_method ? sub_method : ack);
    snprintf(sub_method_ack, sizeof(sub_method_ack), "%sAck",
             sub_method ? sub_method : ack);
    (void)append_json_string(sub_method_ack_json, sizeof(sub_method_ack_json), 0,
                             sub_method_ack);
    if (desired > 0) {
        snprintf(desired_json, sizeof(desired_json), "%" PRIu32, desired);
    }
    else snprintf(desired_json, sizeof(desired_json), "null");
    if (confirmed > 0) {
        snprintf(confirmed_json, sizeof(confirmed_json), "%" PRIu32, confirmed);
    } else {
        snprintf(confirmed_json, sizeof(confirmed_json), "null");
    }

    if (sub_method && reply && reply_len > 0) {
        written = snprintf(reply, reply_len,
                 "{\"method\":\"scheduleReply\",\"subMethod\":%s,"
                 "\"id\":%d,\"code\":%d,\"params\":{\"schemaVersion\":1,"
                 "\"operationId\":%s,\"bindingVersion\":%s,"
                 "\"desiredRevision\":%s,\"confirmedRevision\":%s,"
                 "\"syncState\":\"%s\",\"errorCode\":%d,"
                 "\"errorName\":\"%s\"}}",
                 sub_method_ack_json, id, code, operation_json, binding_json,
                 desired_json, confirmed_json,
                 sync_state, ret, error_name(ret));
        if (written < 0 || (uint32_t)written >= reply_len) {
            snprintf(reply, reply_len,
                     "{\"method\":\"scheduleReply\",\"code\":1,"
                     "\"errorCode\":%d,\"errorName\":\"REPLY_TOO_SMALL\"}",
                     -QM_EFULL);
        }
        return;
    }
    if (reply && reply_len > 0) {
        written = snprintf(reply, reply_len,
                 "{\"method\":\"scheduleReply\",\"subMethod\":%s,"
                 "\"id\":%d,\"code\":%d,\"params\":{\"schemaVersion\":1,"
                 "\"operationId\":%s,\"bindingVersion\":%s,"
                 "\"desiredRevision\":%s,\"confirmedRevision\":%s,"
                 "\"syncState\":\"%s\",\"errorCode\":%d,"
                 "\"errorName\":\"%s\"}}",
                 sub_method_json, id, code, operation_json, binding_json,
                 desired_json, confirmed_json,
                 sync_state, ret, error_name(ret));
        if (written < 0 || (uint32_t)written >= reply_len) {
            snprintf(reply, reply_len,
                     "{\"method\":\"scheduleReply\",\"code\":1,"
                     "\"errorCode\":%d,\"errorName\":\"REPLY_TOO_SMALL\"}",
                     -QM_EFULL);
        }
    }
}

static const char *recurrence_name(qm_schedule_recurrence_t recurrence)
{
    switch (recurrence) {
    case QM_SCHEDULE_RECURRENCE_ONCE:
        return "once";
    case QM_SCHEDULE_RECURRENCE_DAILY:
        return "daily";
    case QM_SCHEDULE_RECURRENCE_WEEKLY:
        return "weekly";
    default:
        return "unknown";
    }
}

static uint32_t append_text(char *reply,
                            uint32_t reply_len,
                            uint32_t used,
                            const char *fmt,
                            ...)
{
    va_list ap;
    int written;

    if (!reply || used >= reply_len) {
        return used;
    }

    va_start(ap, fmt);
    written = vsnprintf(reply + used, reply_len - used, fmt, ap);
    va_end(ap);
    if (written < 0) {
        return used;
    }
    if ((uint32_t)written >= reply_len - used) {
        return reply_len;
    }
    return used + (uint32_t)written;
}

static uint32_t append_char(char *reply,
                            uint32_t reply_len,
                            uint32_t used,
                            char ch)
{
    if (!reply || used + 1 >= reply_len) {
        return reply_len;
    }
    reply[used++] = ch;
    reply[used] = '\0';
    return used;
}

static uint32_t append_json_string(char *reply,
                                   uint32_t reply_len,
                                   uint32_t used,
                                   const char *value)
{
    const char *p = value ? value : "";

    used = append_char(reply, reply_len, used, '"');
    while (*p != '\0' && used < reply_len) {
        if (*p == '"' || *p == '\\') {
            used = append_char(reply, reply_len, used, '\\');
            used = append_char(reply, reply_len, used, *p);
        } else if ((unsigned char)*p < 0x20) {
            used = append_char(reply, reply_len, used, ' ');
        } else {
            used = append_char(reply, reply_len, used, *p);
        }
        p++;
    }
    return append_char(reply, reply_len, used, '"');
}

static int build_sync_reply(qm_schedule_t schedule,
                            cJSON *params,
                            int id,
                            const dispatch_result_t *result,
                            int ret,
                            char *reply,
                            uint32_t reply_len)
{
    qm_schedule_status_t status;
    char operation[192];
    char binding[192];
    char sync_id[192];
    char scope[64];
    char group_id[192];
    char group_clause[256];
    char desired_json[24];
    char confirmed_json[24];
    const char *operation_id = json_string(params, "operationId");
    const char *binding_version = json_string(params, "bindingVersion");
    const char *sync = json_string(params, "syncId");
    const char *sync_scope = json_string(params, "syncScope");
    const char *group = json_string(params, "groupId");
    int64_t chunk_index = 0;
    int64_t chunk_count = 0;
    int64_t desired_revision = 0;
    int written;

    memset(&status, 0, sizeof(status));
    (void)qm_schedule_get_status(schedule, &status);
    (void)json_int(params, "chunkIndex", 0, &chunk_index);
    (void)json_int(params, "chunkCount", 0, &chunk_count);
    (void)json_int(params, "desiredRevision", 0, &desired_revision);
    (void)append_json_string(operation, sizeof(operation), 0,
                             operation_id ? operation_id : "");
    (void)append_json_string(binding, sizeof(binding), 0,
                             binding_version ? binding_version :
                             status.binding_version);
    (void)append_json_string(sync_id, sizeof(sync_id), 0, sync ? sync : "");
    (void)append_json_string(scope, sizeof(scope), 0,
                             sync_scope ? sync_scope : "");
    (void)append_json_string(group_id, sizeof(group_id), 0, group ? group : "");
    if (group && group[0] != '\0') {
        snprintf(group_clause, sizeof(group_clause), ",\"groupId\":%s,",
                 group_id);
    } else {
        snprintf(group_clause, sizeof(group_clause), ",");
    }
    if (status.confirmed_revision > 0) {
        snprintf(confirmed_json, sizeof(confirmed_json), "%" PRIu32,
                 status.confirmed_revision);
    } else {
        snprintf(confirmed_json, sizeof(confirmed_json), "null");
    }
    if (desired_revision > 0) {
        snprintf(desired_json, sizeof(desired_json), "%" PRId64,
                 desired_revision);
    } else if (status.desired_revision > 0) {
        snprintf(desired_json, sizeof(desired_json), "%" PRIu32,
                 status.desired_revision);
    } else {
        snprintf(desired_json, sizeof(desired_json), "null");
    }
    written = snprintf(reply, reply_len,
        "{\"method\":\"scheduleReply\",\"subMethod\":\"syncAck\","
        "\"id\":%d,\"code\":%d,\"params\":{\"schemaVersion\":1,"
        "\"operationId\":%s,\"bindingVersion\":%s,\"syncId\":%s,"
        "\"syncScope\":%s%s\"chunkIndex\":%lld,\"chunkCount\":%lld,"
        "\"receivedChunks\":%u,\"complete\":%s,\"desiredRevision\":%s,"
        "\"confirmedRevision\":%s,\"syncState\":\"%s\","
        "\"idempotent\":%s",
        id, ret == QM_EOK ? 0 : 1, operation, binding, sync_id, scope,
        group_clause, (long long)chunk_index,
        (long long)chunk_count, result ? result->received_chunks : 0,
        result && result->complete ? "true" : "false",
        desired_json,
        confirmed_json,
        sync_state_name(status.sync_state),
        result && result->idempotent ? "true" : "false");
    if (written > 0 && (uint32_t)written < reply_len) {
        written += snprintf(reply + written, reply_len - (uint32_t)written,
                            ret == QM_EOK ? "}}}" :
                            ",\"error\":{\"name\":\"%s\",\"retryable\":%s}}}",
                            ret == QM_EOK ? "" : error_name(ret),
                            ret == -QM_EBUSY || ret == -QM_EFULL ?
                            "true" : "false");
    }
    if (written < 0 || (uint32_t)written >= reply_len) {
        return -QM_EFULL;
    }
    return QM_EOK;
}

static const char *occurrence_state_name(qm_schedule_occurrence_state_t state)
{
    switch (state) {
    case QM_SCHEDULE_OCCURRENCE_COMPLETED: return "completed";
    case QM_SCHEDULE_OCCURRENCE_CANCELLED: return "cancelled";
    case QM_SCHEDULE_OCCURRENCE_EXPIRED: return "expired";
    default: return "pending";
    }
}

static const char *reminder_state_name(qm_schedule_reminder_state_t state)
{
    switch (state) {
    case QM_SCHEDULE_REMINDER_ACTIVE: return "active";
    case QM_SCHEDULE_REMINDER_ACKNOWLEDGED: return "acknowledged";
    case QM_SCHEDULE_REMINDER_SNOOZED: return "snoozed";
    case QM_SCHEDULE_REMINDER_CLOSED: return "closed";
    case QM_SCHEDULE_REMINDER_CANCELLED: return "cancelled";
    default: return "pending";
    }
}

static int build_dispatch_reply(qm_schedule_t schedule,
                                const char *sub_method,
                                cJSON *params,
                                int id,
                                const dispatch_result_t *result,
                                int ret,
                                char *reply,
                                uint32_t reply_len)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *reply_params = cJSON_CreateObject();
    qm_schedule_status_t status;
    const char *schedule_id = json_string(params, "scheduleId");
    const char *operation_id = json_string(params, "operationId");
    const char *binding_version = json_string(params, "bindingVersion");
    int64_t revision = 0;
    int64_t desired_revision = 0;
    int64_t occurrence_at = 0;
    const char *reminder_id = json_string(params, "reminderId");
    char *text = NULL;
    int result_code = QM_EOK;

    if (!root || !reply_params) {
        if (root) cJSON_Delete(root);
        if (reply_params) cJSON_Delete(reply_params);
        return -QM_ENOMEM;
    }
    memset(&status, 0, sizeof(status));
    (void)qm_schedule_get_status(schedule, &status);
    if (json_int(params, "revision", 0, &revision) != QM_EOK) revision = 0;
    if (json_int(params, "desiredRevision", 0, &desired_revision) != QM_EOK) {
        desired_revision = 0;
    }
    (void)json_int(params, "occurrenceAt", 0, &occurrence_at);
    if (!schedule_id && cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(
            params, "schedule"))) {
        cJSON *object = cJSON_GetObjectItemCaseSensitive(params, "schedule");
        schedule_id = json_string(object, "id");
        (void)json_int(object, "revision", 0, &revision);
    }
    cJSON_AddStringToObject(root, "method", "scheduleReply");
    {
        char ack[64];
        snprintf(ack, sizeof(ack), "%sAck", sub_method ? sub_method : "unknown");
        cJSON_AddStringToObject(root, "subMethod", ack);
    }
    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddNumberToObject(root, "code", ret == QM_EOK ? 0 : 1);
    cJSON_AddItemToObject(root, "params", reply_params);
    cJSON_AddNumberToObject(reply_params, "schemaVersion", 1);
    cJSON_AddStringToObject(reply_params, "operationId", operation_id ? operation_id : "");
    cJSON_AddStringToObject(reply_params, "bindingVersion",
                            binding_version ? binding_version : status.binding_version);
    if (desired_revision > 0 && desired_revision <= UINT32_MAX) {
        cJSON_AddNumberToObject(reply_params, "desiredRevision",
                                (double)desired_revision);
    } else if (status.desired_revision > 0) {
        cJSON_AddNumberToObject(reply_params, "desiredRevision",
                                status.desired_revision);
    } else {
        cJSON_AddNullToObject(reply_params, "desiredRevision");
    }
    if (status.confirmed_revision > 0) {
        cJSON_AddNumberToObject(reply_params, "confirmedRevision",
                                status.confirmed_revision);
    } else {
        cJSON_AddNullToObject(reply_params, "confirmedRevision");
    }
    cJSON_AddStringToObject(reply_params, "syncState", sync_state_name(status.sync_state));
    if (schedule_id) cJSON_AddStringToObject(reply_params, "scheduleId", schedule_id);
    if (revision > 0) cJSON_AddNumberToObject(reply_params, "revision", revision);
    if (strcmp(sub_method, "enable") == 0) {
        bool enabled = false;
        (void)json_bool(params, "enabled", 0, &enabled);
        cJSON_AddBoolToObject(reply_params, "enabled", enabled);
    }
    if (strcmp(sub_method, "create") == 0 || strcmp(sub_method, "modify") == 0) {
        cJSON_AddBoolToObject(reply_params, "applied",
                              result ? result->applied : ret == QM_EOK);
    } else if (strcmp(sub_method, "delete") == 0) {
        cJSON_AddBoolToObject(reply_params, "deleted", result ? result->deleted : false);
    } else if (strcmp(sub_method, "acknowledge") == 0 ||
               strcmp(sub_method, "complete") == 0 ||
               strcmp(sub_method, "snooze") == 0 ||
               strcmp(sub_method, "dismiss") == 0) {
        qm_schedule_occurrence_record_t occurrence;
        bool found = false;
        memset(&occurrence, 0, sizeof(occurrence));
        qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
        for (uint16_t i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
            qm_schedule_occurrence_record_t *candidate =
                &schedule->store.occurrences[i];
            if (candidate->used && schedule_id && reminder_id &&
                candidate->occurrence_at == occurrence_at &&
                strcmp(candidate->schedule_id, schedule_id) == 0 &&
                strcmp(candidate->reminder_id, reminder_id) == 0) {
                occurrence = *candidate;
                found = true;
                break;
            }
        }
        qm_mutex_unlock(&schedule->lock);
        if (found) {
            cJSON_AddStringToObject(reply_params, "occurrenceState",
                                    occurrence_state_name(occurrence.occurrence_state));
            cJSON_AddStringToObject(reply_params, "reminderState",
                                    reminder_state_name(occurrence.reminder_state));
            cJSON_AddNumberToObject(reply_params, "actionAt", occurrence.action_at);
            if (strcmp(sub_method, "snooze") == 0) {
                cJSON_AddNumberToObject(reply_params, "snoozeUntil", occurrence.snooze_until);
                cJSON_AddNumberToObject(reply_params, "snoozeCount", occurrence.snooze_count);
            }
            if (strcmp(sub_method, "complete") == 0 ||
                strcmp(sub_method, "dismiss") == 0) {
                cJSON_AddStringToObject(reply_params, "closeReason",
                                        close_reason_name(occurrence.close_reason));
            }
        }
    }
    cJSON_AddBoolToObject(reply_params, "idempotent",
                          result ? result->idempotent : false);
    if (ret != QM_EOK) {
        cJSON *error = cJSON_CreateObject();
        if (!error) { result_code = -QM_ENOMEM; goto done; }
        cJSON_AddStringToObject(error, "name", error_name(ret));
        cJSON_AddBoolToObject(error, "retryable", ret == -QM_EBUSY ||
                              ret == -QM_EFULL || ret == -QM_SCHEDULE_E_OUTBOX_FULL);
        cJSON_AddItemToObject(reply_params, "error", error);
    }
    text = cJSON_PrintUnformatted(root);
    if (!text || strlen(text) >= reply_len) {
        result_code = -QM_EFULL;
        goto done;
    }
    strcpy(reply, text);
done:
    if (text) qm_free(text);
    cJSON_Delete(root);
    return result_code;
}

static int build_status_reply(qm_schedule_t schedule,
                              const char *sub_method,
                              cJSON *params,
                              int id,
                              char *reply,
                              uint32_t reply_len)
{
    qm_schedule_status_t status;
    const char *operation_id = json_string(params, "operationId");
    char operation_json[192];
    char binding_json[192];
    char desired_json[24];
    char confirmed_json[24];
    int written;
    int ret;

    if (!operation_id) {
        operation_id = "";
    }
    ret = qm_schedule_get_status(schedule, &status);
    if (ret != QM_EOK) {
        build_reply(reply, reply_len, schedule, sub_method, params, id, ret);
        return ret;
    }
    (void)append_json_string(operation_json, sizeof(operation_json), 0,
                             operation_id);
    (void)append_json_string(binding_json, sizeof(binding_json), 0,
                             status.binding_version);
    if (status.desired_revision > 0) {
        snprintf(desired_json, sizeof(desired_json), "%" PRIu32,
                 status.desired_revision);
    } else {
        snprintf(desired_json, sizeof(desired_json), "null");
    }
    if (status.confirmed_revision > 0) {
        snprintf(confirmed_json, sizeof(confirmed_json), "%" PRIu32,
                 status.confirmed_revision);
    } else {
        snprintf(confirmed_json, sizeof(confirmed_json), "null");
    }
    written = snprintf(reply, reply_len,
             "{\"method\":\"scheduleReply\",\"subMethod\":\"%sAck\","
             "\"id\":%d,\"code\":0,\"params\":{\"schemaVersion\":1,"
             "\"operationId\":%s,\"bindingVersion\":%s,"
             "\"desiredRevision\":%s,\"confirmedRevision\":%s,"
             "\"syncState\":\"%s\",\"running\":%s,\"schedulable\":%s,"
             "\"errorCode\":0,\"errorName\":\"OK\"}}",
             sub_method, id, operation_json, binding_json,
             desired_json, confirmed_json,
             sync_state_name(status.sync_state),
             status.running ? "true" : "false",
             status.schedulable ? "true" : "false");
    if (written < 0 || (uint32_t)written >= reply_len) {
        build_reply(reply, reply_len, schedule, sub_method, params, id,
                    -QM_EFULL);
        return -QM_EFULL;
    }
    return QM_EOK;
}

static int build_capability_reply(qm_schedule_t schedule,
                                  const char *sub_method,
                                  cJSON *params,
                                  int id,
                                  char *reply,
                                  uint32_t reply_len)
{
    qm_schedule_capability_t capability;
    const char *operation_id = json_string(params, "operationId");
    char operation_json[192];
    int written;
    int ret;

    if (!operation_id) {
        operation_id = "";
    }
    ret = qm_schedule_get_capability(schedule, &capability);
    if (ret != QM_EOK) {
        build_reply(reply, reply_len, schedule, sub_method, params, id, ret);
        return ret;
    }
    (void)append_json_string(operation_json, sizeof(operation_json), 0,
                             operation_id);
    written = snprintf(reply, reply_len,
             "{\"method\":\"scheduleReply\",\"subMethod\":\"%sAck\","
             "\"id\":%d,\"code\":0,\"params\":{\"schemaVersion\":1,"
             "\"operationId\":%s,\"maxItems\":%u,"
             "\"maxRemindersPerItem\":%u,\"maxOutboxUser\":%u,"
             "\"maxOutboxSystem\":%u,\"maxSyncChunks\":%u,"
             "\"maxSyncItemsPerChunk\":%u,"
             "\"maxPayloadLen\":%lu,\"supportsFixedOffsetTimezone\":%s,"
             "\"supportsIanaTimezone\":%s,\"supportsDoubleBankStore\":%s,"
             "\"supportsPagedStore\":%s,\"errorCode\":0,"
             "\"errorName\":\"OK\"}}",
             sub_method, id, operation_json,
             (unsigned int)capability.max_items,
             (unsigned int)capability.max_reminders_per_item,
             (unsigned int)capability.max_outbox_user,
             (unsigned int)capability.max_outbox_system,
             (unsigned int)capability.max_sync_chunks,
             (unsigned int)capability.max_sync_items_per_chunk,
             (unsigned long)capability.max_payload_len,
             capability.supports_fixed_offset_timezone ? "true" : "false",
             capability.supports_iana_timezone ? "true" : "false",
             capability.supports_double_bank_store ? "true" : "false",
             capability.supports_paged_store ? "true" : "false");
    if (written < 0 || (uint32_t)written >= reply_len) {
        build_reply(reply, reply_len, schedule, sub_method, params, id,
                    -QM_EFULL);
        return -QM_EFULL;
    }
    return QM_EOK;
}

static uint32_t append_item_summary(char *reply,
                                    uint32_t reply_len,
                                    uint32_t used,
                                    const qm_schedule_item_snapshot_t *item)
{
    static const char *weekday_names[] = {
        "SU", "MO", "TU", "WE", "TH", "FR", "SA"
    };
    uint8_t i;
    bool first_day = true;

    used = append_text(reply, reply_len, used, "{\"id\":");
    used = append_json_string(reply, reply_len, used, item->id);
    used = append_text(reply, reply_len, used, ",\"revision\":%u,\"type\":",
                       (unsigned int)item->revision);
    used = append_json_string(reply, reply_len, used, item->type);
    used = append_text(reply, reply_len, used, ",\"source\":");
    used = append_json_string(reply, reply_len, used, item->source);
    if (item->group_id[0] != '\0') {
        used = append_text(reply, reply_len, used, ",\"groupId\":");
        used = append_json_string(reply, reply_len, used, item->group_id);
    }
    used = append_text(reply, reply_len, used, ",\"title\":");
    used = append_json_string(reply, reply_len, used, item->title);
    if (item->description[0] != '\0') {
        used = append_text(reply, reply_len, used, ",\"description\":");
        used = append_json_string(reply, reply_len, used, item->description);
    }
    if (item->location[0] != '\0') {
        used = append_text(reply, reply_len, used, ",\"location\":");
        used = append_json_string(reply, reply_len, used, item->location);
    }
    used = append_text(reply, reply_len, used, ",\"priority\":");
    used = append_json_string(reply, reply_len, used, item->priority);
    if (item->category[0] != '\0') {
        used = append_text(reply, reply_len, used, ",\"category\":");
        used = append_json_string(reply, reply_len, used, item->category);
    }
    if (item->sensitivity[0] != '\0') {
        used = append_text(reply, reply_len, used, ",\"sensitivity\":");
        used = append_json_string(reply, reply_len, used, item->sensitivity);
    }
    used = append_text(reply, reply_len, used,
                       ",\"startAt\":%lld,\"endAt\":%lld,\"timezone\":",
                       (long long)item->start_at, (long long)item->end_at);
    used = append_json_string(reply, reply_len, used, item->timezone);
    used = append_text(reply, reply_len, used, ",\"allDay\":%s,\"recurrence\":",
                       item->all_day ? "true" : "false");
    if (item->recurrence == QM_SCHEDULE_RECURRENCE_ONCE) {
        used = append_text(reply, reply_len, used, "null");
    } else {
        used = append_text(reply, reply_len, used, "{\"frequency\":");
        used = append_json_string(reply, reply_len, used,
                                  recurrence_name(item->recurrence));
        if (item->recurrence == QM_SCHEDULE_RECURRENCE_WEEKLY) {
            used = append_text(reply, reply_len, used, ",\"byWeekday\":[");
            for (i = 0; i < 7; i++) {
                if ((item->weekly_mask & (uint8_t)(1u << i)) == 0) {
                    continue;
                }
                if (!first_day) {
                    used = append_text(reply, reply_len, used, ",");
                }
                used = append_json_string(reply, reply_len, used,
                                          weekday_names[i]);
                first_day = false;
            }
            used = append_text(reply, reply_len, used, "]");
        }
        if (item->recurrence_start_date[0] != '\0') {
            used = append_text(reply, reply_len, used, ",\"startDate\":");
            used = append_json_string(reply, reply_len, used,
                                      item->recurrence_start_date);
        }
        if (item->recurrence_end_date[0] != '\0') {
            used = append_text(reply, reply_len, used, ",\"endDate\":");
            used = append_json_string(reply, reply_len, used,
                                      item->recurrence_end_date);
        }
        used = append_text(reply, reply_len, used, ",\"interval\":%u}",
                           (unsigned int)item->recurrence_interval);
    }
    used = append_text(reply, reply_len, used, ",\"reminders\":[");
    for (i = 0; i < item->reminder_count; i++) {
        const qm_schedule_reminder_snapshot_t *reminder = &item->reminders[i];

        if (i > 0) {
            used = append_text(reply, reply_len, used, ",");
        }
        used = append_text(reply, reply_len, used, "{\"id\":");
        used = append_json_string(reply, reply_len, used, reminder->id);
        used = append_text(reply, reply_len, used,
                           ",\"offsetMin\":%ld,\"action\":",
                           (long)reminder->offset_min);
        used = append_json_string(reply, reply_len, used, reminder->action);
        used = append_text(reply, reply_len, used, ",\"channels\":%s,"
                           "\"requiresAck\":%s,\"snooze\":{"
                           "\"enabled\":%s,\"intervalMin\":%u,"
                           "\"maxCount\":%u}}",
                           reminder->channels_json[0] != '\0' ?
                               reminder->channels_json : "[]",
                           reminder->requires_ack ? "true" : "false",
                           reminder->snooze_enabled ? "true" : "false",
                           (unsigned int)reminder->snooze_interval_min,
                           (unsigned int)reminder->snooze_max_count);
    }
    used = append_text(reply, reply_len, used, "],\"enabled\":%s,\"status\":",
                       item->enabled ? "true" : "false");
    used = append_json_string(reply, reply_len, used, item->status);
    used = append_text(reply, reply_len, used, ",\"missedPolicy\":");
    used = append_json_string(reply, reply_len, used, item->missed_policy);
    if (item->projection_key[0] != '\0') {
        used = append_text(reply, reply_len, used, ",\"projectionKey\":");
        used = append_json_string(reply, reply_len, used,
                                  item->projection_key);
    }
    used = append_text(reply, reply_len, used, ",\"extensions\":%s}",
                       item->extensions_json[0] != '\0' ?
                           item->extensions_json : "{}");
    return used;
}

static int build_get_reply(qm_schedule_t schedule,
                           const char *sub_method,
                           cJSON *params,
                           int id,
                           char *reply,
                           uint32_t reply_len)
{
    const char *operation_id = json_string(params, "operationId");
    const char *schedule_id = json_string(params, "scheduleId");
    char operation_json[192];
    qm_schedule_item_snapshot_t item;
    uint32_t used = 0;
    int ret;

    if (!operation_id) {
        operation_id = "";
    }
    ret = qm_schedule_get_item(schedule, schedule_id, &item);
    if (ret != QM_EOK) {
        build_reply(reply, reply_len, schedule, sub_method, params, id, ret);
        return ret;
    }
    (void)append_json_string(operation_json, sizeof(operation_json), 0,
                             operation_id);
    used = append_text(reply, reply_len, used,
                       "{\"method\":\"scheduleReply\",\"subMethod\":\"%sAck\","
                       "\"id\":%d,\"code\":0,\"params\":{\"schemaVersion\":1,"
                       "\"operationId\":%s,\"schedule\":",
                       sub_method, id, operation_json);
    used = append_item_summary(reply, reply_len, used, &item);
    used = append_text(reply, reply_len, used,
                       ",\"errorCode\":0,\"errorName\":\"OK\"}}");
    if (used >= reply_len) {
        build_reply(reply, reply_len, schedule, sub_method, params, id,
                    -QM_EFULL);
        return -QM_EFULL;
    }
    return QM_EOK;
}

static int build_list_reply(qm_schedule_t schedule,
                            const char *sub_method,
                            cJSON *params,
                            int id,
                            char *reply,
                            uint32_t reply_len)
{
    const char *operation_id = json_string(params, "operationId");
    char operation_json[192];
    qm_schedule_item_snapshot_t *items;
    int64_t limit64 = 8;
    uint16_t total = 0;
    uint16_t limit;
    uint16_t i;
    uint32_t used = 0;
    int ret;

    if (!operation_id) {
        operation_id = "";
    }
    (void)append_json_string(operation_json, sizeof(operation_json), 0,
                             operation_id);
    (void)json_int(params, "limit", 0, &limit64);
    if (limit64 <= 0) {
        limit64 = 8;
    }
    if (limit64 > CONFIG_QM_SCHEDULE_MAX_QUERY_PAGE) {
        limit64 = CONFIG_QM_SCHEDULE_MAX_QUERY_PAGE;
    }
    limit = (uint16_t)limit64;
    items = (qm_schedule_item_snapshot_t *)qm_calloc(limit, sizeof(*items));
    if (!items) {
        build_reply(reply, reply_len, schedule, sub_method, params, id,
                    -QM_ENOMEM);
        return -QM_ENOMEM;
    }
    ret = qm_schedule_list_items(schedule, items, limit, &total);
    if (ret != QM_EOK && ret != -QM_EFULL) {
        qm_free(items);
        build_reply(reply, reply_len, schedule, sub_method, params, id, ret);
        return ret;
    }

    used = append_text(reply, reply_len, used,
                       "{\"method\":\"scheduleReply\",\"subMethod\":\"%sAck\","
                       "\"id\":%d,\"code\":0,\"params\":{\"schemaVersion\":1,"
                       "\"operationId\":%s,\"total\":%u,\"schedules\":[",
                       sub_method, id, operation_json, (unsigned int)total);
    for (i = 0; i < limit && i < total; i++) {
        if (i > 0) {
            used = append_text(reply, reply_len, used, ",");
        }
        used = append_item_summary(reply, reply_len, used, &items[i]);
    }
    used = append_text(reply, reply_len, used,
                       "],\"errorCode\":0,\"errorName\":\"OK\"}}");
    qm_free(items);
    if (used >= reply_len) {
        build_reply(reply, reply_len, schedule, sub_method, params, id,
                    -QM_EFULL);
        return -QM_EFULL;
    }
    return QM_EOK;
}

static bool handle_query(qm_schedule_t schedule,
                         const char *sub_method,
                         cJSON *params,
                         int id,
                         char *reply,
                         uint32_t reply_len,
                         int *ret)
{
    if (!sub_method || !params || !ret) {
        return false;
    }
    if (strcmp(sub_method, "status") == 0) {
        *ret = build_status_reply(schedule, sub_method, params, id, reply,
                                  reply_len);
        return true;
    }
    if (strcmp(sub_method, "capability") == 0) {
        *ret = build_capability_reply(schedule, sub_method, params, id, reply,
                                      reply_len);
        return true;
    }
    if (strcmp(sub_method, "get") == 0) {
        *ret = build_get_reply(schedule, sub_method, params, id, reply,
                               reply_len);
        return true;
    }
    if (strcmp(sub_method, "list") == 0) {
        *ret = build_list_reply(schedule, sub_method, params, id, reply,
                                reply_len);
        return true;
    }
    return false;
}

static int dispatch(qm_schedule_t schedule, const char *sub_method,
                    cJSON *params, dispatch_result_t *result)
{
    qm_schedule_apply_ctx_t ctx;
    int64_t value = 0;

    if (!sub_method || !params || !result) {
        return -QM_EINVAL;
    }
    memset(result, 0, sizeof(*result));
    {
        int schema_ret = validate_schema(params);
        if (schema_ret != QM_EOK) return schema_ret;
    }

    if (strcmp(sub_method, "create") == 0 ||
        strcmp(sub_method, "modify") == 0) {
        qm_schedule_item_t item;
        qm_schedule_reminder_t reminders[QM_SCHEDULE_MAX_REMINDERS_DEFAULT];
        char *extensions_json;
        char *channels_json;
        cJSON *obj = cJSON_GetObjectItemCaseSensitive(params, "schedule");
        qm_schedule_item_snapshot_t existing;
        bool same_revision = false;
        int ret;

        if (!cJSON_IsObject(obj) || parse_ctx(params, &ctx) != QM_EOK) {
            return -QM_EINVAL;
        }
        extensions_json = (char *)qm_calloc(
            1, QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT);
        channels_json = (char *)qm_calloc(
            QM_SCHEDULE_MAX_REMINDERS_DEFAULT,
            QM_SCHEDULE_CHANNELS_MAX_DEFAULT);
        if (!extensions_json || !channels_json) {
            qm_free(extensions_json);
            qm_free(channels_json);
            return -QM_ENOMEM;
        }
        ret = parse_schedule_item(
            obj, &item, reminders, QM_SCHEDULE_MAX_REMINDERS_DEFAULT,
            extensions_json, QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT,
            channels_json, QM_SCHEDULE_CHANNELS_MAX_DEFAULT);
        if (ret != QM_EOK) {
            qm_free(extensions_json);
            qm_free(channels_json);
            return ret;
        }
        same_revision = qm_schedule_get_item(schedule, item.id, &existing) ==
            QM_EOK && existing.revision == item.revision;
        if (strcmp(sub_method, "create") == 0) {
            ret = qm_schedule_create_item(schedule, &item, &ctx);
        } else {
            ret = qm_schedule_modify_item(schedule, &item, &ctx);
        }
        qm_free(extensions_json);
        qm_free(channels_json);
        if (ret == QM_EOK) {
            result->idempotent = same_revision;
            result->applied = !same_revision;
        }
        return ret;
    }

    if (strcmp(sub_method, "delete") == 0) {
        const char *schedule_id = json_string(params, "scheduleId");
        bool existed = false;
        bool tombstone_replay = false;
        if (parse_ctx(params, &ctx) != QM_EOK ||
            json_int(params, "revision", 1, &value) != QM_EOK ||
            value <= 0 || value > UINT32_MAX) {
            return -QM_EINVAL;
        }
        qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
        for (uint16_t i = 0; i < schedule->config.max_items; i++) {
            if (schedule->store.items[i].used && schedule_id &&
                strcmp(schedule->store.items[i].id, schedule_id) == 0) {
                existed = true;
                break;
            }
        }
        for (uint16_t i = 0; i < QM_SCHEDULE_MAX_TOMBSTONES_INTERNAL; i++) {
            if (schedule->store.tombstones[i].used && schedule_id &&
                strcmp(schedule->store.tombstones[i].schedule_id,
                       schedule_id) == 0 &&
                schedule->store.tombstones[i].revision == (uint32_t)value) {
                tombstone_replay = true;
                break;
            }
        }
        qm_mutex_unlock(&schedule->lock);
        {
            int ret = qm_schedule_delete_item(schedule, schedule_id,
                                              (uint32_t)value, &ctx);
            if (ret == QM_EOK) {
                result->idempotent = tombstone_replay;
                result->deleted = existed;
                result->applied = !tombstone_replay;
            }
            return ret;
        }
    }

    if (strcmp(sub_method, "enable") == 0) {
        const char *schedule_id = json_string(params, "scheduleId");
        qm_schedule_item_snapshot_t existing;
        bool same_state = false;
        bool enabled = false;
        if (parse_ctx(params, &ctx) != QM_EOK ||
            json_int(params, "revision", 1, &value) != QM_EOK ||
            value <= 0 || value > UINT32_MAX ||
            json_bool(params, "enabled", 1, &enabled) != QM_EOK) {
            return -QM_EINVAL;
        }
        same_state = qm_schedule_get_item(schedule, schedule_id, &existing) ==
            QM_EOK && existing.revision == (uint32_t)value &&
            existing.enabled == enabled;
        {
            int ret = qm_schedule_enable_item(schedule, schedule_id,
                                              (uint32_t)value, enabled, &ctx);
            if (ret == QM_EOK) {
                result->idempotent = same_state;
                result->applied = !same_state;
                result->enabled = enabled;
            }
            return ret;
        }
    }

    if (strcmp(sub_method, "acknowledge") == 0 ||
        strcmp(sub_method, "complete") == 0 ||
        strcmp(sub_method, "snooze") == 0 ||
        strcmp(sub_method, "dismiss") == 0) {
        qm_schedule_action_t action;
        int64_t duration = 0;
        bool replay = false;
        int ret;

        memset(&action, 0, sizeof(action));
        action.operation_id = json_string(params, "operationId");
        action.event_id = json_string(params, "eventId");
        action.binding_version = json_string(params, "bindingVersion");
        action.schedule_id = json_string(params, "scheduleId");
        action.occurrence_id = json_string(params, "occurrenceId");
        action.reminder_id = json_string(params, "reminderId");
        if (!action.operation_id || !action.binding_version ||
            !action.schedule_id || !action.reminder_id) {
            return -QM_EINVAL;
        }
        if (json_int(params, "occurrenceAt", 1, &action.occurrence_at) != QM_EOK ||
            json_int(params, "actionAt", 1, &action.action_at) != QM_EOK) {
            return -QM_EINVAL;
        }
        qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
        for (uint16_t i = 0; i < QM_SCHEDULE_MAX_OCCURRENCES_INTERNAL; i++) {
            qm_schedule_occurrence_record_t *occ =
                &schedule->store.occurrences[i];
            if (occ->used && action.operation_id &&
                strcmp(occ->schedule_id, action.schedule_id) == 0 &&
                occ->occurrence_at == action.occurrence_at &&
                strcmp(occ->reminder_id, action.reminder_id) == 0 &&
                strcmp(occ->last_operation_id, action.operation_id) == 0) {
                replay = true;
                break;
            }
        }
        qm_mutex_unlock(&schedule->lock);
        if (strcmp(sub_method, "acknowledge") == 0) {
            ret = qm_schedule_acknowledge(schedule, &action);
            goto action_done;
        }
        if (strcmp(sub_method, "complete") == 0) {
            ret = qm_schedule_complete(schedule, &action);
            goto action_done;
        }
        if (strcmp(sub_method, "snooze") == 0) {
            if (json_int(params, "durationMin", 0, &duration) != QM_EOK ||
                duration < 0 || duration > UINT16_MAX) {
                return -QM_EINVAL;
            }
            ret = qm_schedule_snooze(schedule, &action, (uint16_t)duration);
            goto action_done;
        }
        {
            const char *reason = json_string(params, "closeReason");
            qm_schedule_close_reason_t close_reason =
                QM_SCHEDULE_CLOSE_USER_CLOSED;
            if (reason && strcmp(reason, "expired") == 0) {
                close_reason = QM_SCHEDULE_CLOSE_EXPIRED;
            } else if (reason && strcmp(reason, "cancelled") == 0) {
                close_reason = QM_SCHEDULE_CLOSE_CANCELLED;
            } else if (reason && strcmp(reason, "user_closed") != 0) {
                return -QM_EINVAL;
            }
            ret = qm_schedule_dismiss(schedule, &action, close_reason);
        }
action_done:
        if (ret == QM_EOK) {
            result->idempotent = replay;
            result->applied = !replay;
        }
        return ret;
    }

    if (strcmp(sub_method, "syncStart") == 0) {
        const char *sync_id = json_string(params, "syncId");
        bool replace = true;
        int64_t total_chunks = 0;

        if (parse_ctx(params, &ctx) != QM_EOK ||
            json_int(params, "totalChunks", 1, &total_chunks) != QM_EOK ||
            total_chunks <= 0 || total_chunks > UINT16_MAX) {
            return -QM_EINVAL;
        }
        (void)json_bool(params, "replace", 0, &replace);
        return qm_schedule_sync_stage_begin(schedule, sync_id,
                                            (uint16_t)total_chunks, replace,
                                            &ctx);
    }

    if (strcmp(sub_method, "syncChunk") == 0) {
        const char *sync_id = json_string(params, "syncId");
        cJSON *array = cJSON_GetObjectItemCaseSensitive(params, "schedules");
        qm_schedule_item_t *items;
        qm_schedule_reminder_t *reminders;
        char *extensions_json;
        char *channels_json;
        int64_t chunk_index = 0;
        int count;
        int i;
        int ret = QM_EOK;

        if (json_int(params, "chunkIndex", 1, &chunk_index) != QM_EOK ||
            chunk_index < 0 || chunk_index > UINT16_MAX ||
            !cJSON_IsArray(array)) {
            return -QM_EINVAL;
        }
        count = cJSON_GetArraySize(array);
        if (count < 0 || count > (int)QM_SCHEDULE_MAX_ITEMS_INTERNAL) {
            return -QM_EFULL;
        }
        items = (qm_schedule_item_t *)qm_calloc((uint32_t)count,
                                                sizeof(*items));
        reminders = (qm_schedule_reminder_t *)qm_calloc(
            (uint32_t)(count * QM_SCHEDULE_MAX_REMINDERS_DEFAULT),
            sizeof(*reminders));
        extensions_json = (char *)qm_calloc(
            (uint32_t)count, QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT);
        channels_json = (char *)qm_calloc(
            (uint32_t)(count * QM_SCHEDULE_MAX_REMINDERS_DEFAULT),
            QM_SCHEDULE_CHANNELS_MAX_DEFAULT);
        if ((count > 0 && !items) || (count > 0 && !reminders) ||
            (count > 0 && !extensions_json) ||
            (count > 0 && !channels_json)) {
            qm_free(items);
            qm_free(reminders);
            qm_free(extensions_json);
            qm_free(channels_json);
            return -QM_ENOMEM;
        }
        for (i = 0; i < count; i++) {
            ret = parse_schedule_item(
                cJSON_GetArrayItem(array, i), &items[i],
                &reminders[i * QM_SCHEDULE_MAX_REMINDERS_DEFAULT],
                QM_SCHEDULE_MAX_REMINDERS_DEFAULT,
                extensions_json +
                    ((uint32_t)i * QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT),
                QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT,
                channels_json + ((uint32_t)i *
                    QM_SCHEDULE_MAX_REMINDERS_DEFAULT *
                    QM_SCHEDULE_CHANNELS_MAX_DEFAULT),
                QM_SCHEDULE_CHANNELS_MAX_DEFAULT);
            if (ret != QM_EOK) {
                break;
            }
        }
        if (ret == QM_EOK) {
            ret = qm_schedule_sync_stage_items(schedule, sync_id,
                                               (uint16_t)chunk_index,
                                               items, (uint16_t)count);
        }
        qm_free(items);
        qm_free(reminders);
        qm_free(extensions_json);
        qm_free(channels_json);
        return ret;
    }

    if (strcmp(sub_method, "syncCommit") == 0) {
        const char *sync_id = json_string(params, "syncId");

        if (parse_ctx(params, &ctx) != QM_EOK) {
            return -QM_EINVAL;
        }
        return qm_schedule_sync_stage_commit(schedule, sync_id, &ctx);
    }

    if (strcmp(sub_method, "syncAbort") == 0) {
        const char *sync_id = json_string(params, "syncId");

        return qm_schedule_sync_stage_abort(schedule, sync_id);
    }

    if (strcmp(sub_method, "requestSync") == 0) {
        const char *event_id = json_string(params, "eventId");
        int64_t event_at = 0;

        if (json_int(params, "eventAt", 0, &event_at) != QM_EOK) {
            return -QM_EINVAL;
        }
        return qm_schedule_request_sync(schedule, event_id, event_at);
    }

    if (strcmp(sub_method, "cleanup") == 0) {
        int64_t now = 0;

        if (json_int(params, "now", 0, &now) != QM_EOK) {
            return -QM_EINVAL;
        }
        return qm_schedule_cleanup(schedule, now);
    }

    if (strcmp(sub_method, "sync") == 0) {
        cJSON *array = cJSON_GetObjectItemCaseSensitive(params, "schedules");
        cJSON *groups_json = cJSON_GetObjectItemCaseSensitive(params, "groups");
        const char *sync_id = json_string(params, "syncId");
        const char *sync_scope = json_string(params, "syncScope");
        const char *group_id = json_string(params, "groupId");
        const char *content_digest = json_string(params, "contentDigest");
        qm_schedule_sync_chunk_t chunk;
        qm_schedule_group_record_t groups[QM_SCHEDULE_MAX_GROUPS_INTERNAL];
        qm_schedule_item_t *items;
        qm_schedule_reminder_t *reminders;
        char *extensions_json;
        char *channels_json;
        int64_t group_revision = 0;
        int64_t chunk_index = 0;
        int64_t chunk_count = 0;
        int64_t total_items = 0;
        bool replace = false;
        bool full_projection;
        uint16_t group_count = 0;
        int count;
        int i;
        int ret = QM_EOK;

        memset(&chunk, 0, sizeof(chunk));
        memset(groups, 0, sizeof(groups));
        if (parse_ctx(params, &ctx) != QM_EOK || !cJSON_IsArray(array) ||
            !sync_id || !sync_scope || !content_digest ||
            json_bool(params, "replace", 1, &replace) != QM_EOK ||
            json_int(params, "chunkIndex", 1, &chunk_index) != QM_EOK ||
            json_int(params, "chunkCount", 1, &chunk_count) != QM_EOK ||
            json_int(params, "totalItems", 1, &total_items) != QM_EOK ||
            chunk_index < 0 || chunk_count <= 0 ||
            chunk_index >= chunk_count ||
            chunk_count > QM_SCHEDULE_MAX_SYNC_CHUNKS_INTERNAL ||
            total_items < 0 || total_items > schedule->config.max_items) {
            return -QM_EINVAL;
        }
        full_projection = strcmp(sync_scope, "fullProjection") == 0;
        if (!full_projection && strcmp(sync_scope, "group") != 0) {
            return -QM_EINVAL;
        }
        if (full_projection) {
            if (!replace || group_id ||
                cJSON_GetObjectItemCaseSensitive(params, "groupRevision") ||
                !cJSON_IsArray(groups_json) ||
                cJSON_GetArraySize(groups_json) >
                    (int)QM_SCHEDULE_MAX_GROUPS_INTERNAL) {
                return -QM_EINVAL;
            }
            group_count = (uint16_t)cJSON_GetArraySize(groups_json);
            for (i = 0; i < group_count; i++) {
                cJSON *group = cJSON_GetArrayItem(groups_json, i);
                const char *id = json_string(group, "groupId");
                int64_t revision = 0;

                if (!cJSON_IsObject(group) || !id || id[0] == '\0' ||
                    strlen(id) >= QM_SCHEDULE_GROUP_ID_MAX_DEFAULT ||
                    json_int(group, "groupRevision", 1, &revision) != QM_EOK ||
                    revision <= 0 || revision > UINT32_MAX ||
                    (i > 0 && strcmp(groups[i - 1].group_id, id) >= 0)) {
                    return -QM_EINVAL;
                }
                groups[i].used = true;
                snprintf(groups[i].group_id, sizeof(groups[i].group_id), "%s", id);
                groups[i].revision = (uint32_t)revision;
            }
        } else {
            if (!group_id || group_id[0] == '\0' || groups_json ||
                json_int(params, "groupRevision", 1, &group_revision) != QM_EOK ||
                group_revision <= 0 || group_revision > UINT32_MAX) {
                return -QM_EINVAL;
            }
        }
        if (verify_sync_digest(params) != QM_EOK) {
            return -QM_SCHEDULE_E_SYNC_CONFLICT;
        }
        count = cJSON_GetArraySize(array);
        if (count < 0 || count > (int)QM_SCHEDULE_MAX_SYNC_ITEMS_PER_CHUNK_INTERNAL ||
            count > (int)schedule->config.max_items ||
            count > total_items) {
            return -QM_EFULL;
        }
        items = (qm_schedule_item_t *)qm_calloc((uint32_t)count, sizeof(*items));
        reminders = (qm_schedule_reminder_t *)qm_calloc(
            (uint32_t)(count * QM_SCHEDULE_MAX_REMINDERS_DEFAULT),
            sizeof(*reminders));
        extensions_json = (char *)qm_calloc(
            (uint32_t)count, QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT);
        channels_json = (char *)qm_calloc(
            (uint32_t)(count * QM_SCHEDULE_MAX_REMINDERS_DEFAULT),
            QM_SCHEDULE_CHANNELS_MAX_DEFAULT);
        if ((count > 0 && !items) || (count > 0 && !reminders) ||
            (count > 0 && !extensions_json) ||
            (count > 0 && !channels_json)) {
            qm_free(items);
            qm_free(reminders);
            qm_free(extensions_json);
            qm_free(channels_json);
            return -QM_ENOMEM;
        }
        for (i = 0; i < count; i++) {
            ret = parse_schedule_item(
                cJSON_GetArrayItem(array, i), &items[i],
                &reminders[i * QM_SCHEDULE_MAX_REMINDERS_DEFAULT],
                QM_SCHEDULE_MAX_REMINDERS_DEFAULT,
                extensions_json +
                    ((uint32_t)i * QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT),
                QM_SCHEDULE_EXTENSIONS_MAX_DEFAULT,
                channels_json + ((uint32_t)i *
                    QM_SCHEDULE_MAX_REMINDERS_DEFAULT *
                    QM_SCHEDULE_CHANNELS_MAX_DEFAULT),
                QM_SCHEDULE_CHANNELS_MAX_DEFAULT);
            if (ret != QM_EOK) {
                break;
            }
            if (!full_projection) {
                if (!items[i].group_id) {
                    items[i].group_id = group_id;
                } else if (strcmp(items[i].group_id, group_id) != 0) {
                    ret = -QM_SCHEDULE_E_SYNC_CONFLICT;
                    break;
                }
            } else if (items[i].group_id) {
                uint16_t group_index;
                bool found = false;
                for (group_index = 0; group_index < group_count; group_index++) {
                    if (strcmp(groups[group_index].group_id,
                               items[i].group_id) == 0) {
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    ret = -QM_SCHEDULE_E_SYNC_CONFLICT;
                    break;
                }
            }
            if (i > 0) {
                const char *previous_key = items[i - 1].projection_key ?
                    items[i - 1].projection_key : "";
                const char *current_key = items[i].projection_key ?
                    items[i].projection_key : "";
                int order = strcmp(previous_key, current_key);
                if (order > 0 ||
                    (order == 0 && strcmp(items[i - 1].id, items[i].id) >= 0)) {
                    ret = -QM_SCHEDULE_E_SYNC_CONFLICT;
                    break;
                }
            }
        }
        if (ret == QM_EOK) {
            chunk.sync_id = sync_id;
            chunk.full_projection = full_projection;
            chunk.group_id = group_id;
            chunk.group_revision = (uint32_t)group_revision;
            chunk.groups = groups;
            chunk.group_count = group_count;
            chunk.replace = replace;
            chunk.chunk_index = (uint16_t)chunk_index;
            chunk.chunk_count = (uint16_t)chunk_count;
            chunk.total_items = (uint16_t)total_items;
            chunk.content_digest = content_digest;
            ret = qm_schedule_sync_apply_chunk(
                schedule, &chunk, items, (uint16_t)count, &ctx,
                &result->complete, &result->idempotent);
            if (ret == QM_EOK) {
                uint16_t received = 0;
                qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
                for (i = 0; i < (uint16_t)chunk_count; i++) {
                    if (result->complete ||
                        (schedule->store.staging_received_mask & (1u << i))) {
                        received++;
                    }
                }
                result->received_chunks = received;
                result->applied_items = result->complete ?
                    (uint16_t)total_items : 0;
                qm_mutex_unlock(&schedule->lock);
            }
        }
        qm_free(items);
        qm_free(reminders);
        qm_free(extensions_json);
        qm_free(channels_json);
        return ret;
    }

    return -QM_ENOSYS;
}

int qm_schedule_iot_register(qm_schedule_iot_t *out,
                             const qm_schedule_iot_config_t *config)
{
    qm_schedule_iot_t iot;

    if (!out || !config || !config->schedule || !config->iot_handle) {
        return -QM_EINVAL;
    }

    iot = (qm_schedule_iot_t)qm_calloc(1, sizeof(*iot));
    if (!iot) {
        return -QM_ENOMEM;
    }
    iot->config = *config;
    *out = iot;
    return QM_EOK;
}

int qm_schedule_iot_unregister(qm_schedule_iot_t iot)
{
    if (!iot) {
        return -QM_EINVAL;
    }

    qm_free(iot);
    return QM_EOK;
}

static const char *close_reason_name(qm_schedule_close_reason_t reason)
{
    switch (reason) {
    case QM_SCHEDULE_CLOSE_EXPIRED:
        return "expired";
    case QM_SCHEDULE_CLOSE_CANCELLED:
        return "cancelled";
    case QM_SCHEDULE_CLOSE_OCCURRENCE_COMPLETED:
        return "occurrence_completed";
    default:
        return "user_closed";
    }
}

static uint32_t event_transport_id(const char *event_id)
{
    uint32_t hash = 2166136261u;

    while (event_id && *event_id) {
        hash ^= (unsigned char)*event_id++;
        hash *= 16777619u;
    }
    return hash ? hash : 1u;
}

static int build_event_payload(qm_schedule_t schedule,
                               const qm_schedule_outbox_record_t *event,
                               char *payload,
                               uint32_t payload_len)
{
    cJSON *root = NULL;
    cJSON *params = NULL;
    cJSON *groups = NULL;
    const char *sub_method = "syncRequest";
    char *text = NULL;
    uint16_t i;
    int ret = QM_EOK;

    if (!schedule || !event || !payload || payload_len == 0) return -QM_EINVAL;
    root = cJSON_CreateObject();
    params = cJSON_CreateObject();
    if (!root || !params) { ret = -QM_ENOMEM; goto done; }
    cJSON_AddStringToObject(root, "method", "schedule");
    cJSON_AddNumberToObject(root, "id", event_transport_id(event->event_id));
    cJSON_AddItemToObject(root, "params", params);
    cJSON_AddNumberToObject(params, "schemaVersion", 1);
    cJSON_AddStringToObject(params, "operationId", event->event_id);
    cJSON_AddStringToObject(params, "eventId", event->event_id);
    cJSON_AddStringToObject(params, "bindingVersion", event->binding_version);

    if (event->event_type == QM_SCHEDULE_OUTBOX_USER_ACTION) {
        switch (event->action_type) {
        case QM_SCHEDULE_ACTION_ACKNOWLEDGE: sub_method = "acknowledged"; break;
        case QM_SCHEDULE_ACTION_COMPLETE: sub_method = "completed"; break;
        case QM_SCHEDULE_ACTION_SNOOZE: sub_method = "snoozed"; break;
        case QM_SCHEDULE_ACTION_DISMISS: sub_method = "dismissed"; break;
        default: ret = -QM_EINVAL; goto done;
        }
        cJSON_AddStringToObject(params, "scheduleId", event->schedule_id);
        cJSON_AddNumberToObject(params, "revision", event->revision);
        cJSON_AddNumberToObject(params, "occurrenceAt", event->occurrence_at);
        cJSON_AddStringToObject(params, "reminderId", event->reminder_id);
        cJSON_AddNumberToObject(params, "actionAt", event->action_at);
        if (event->action_type == QM_SCHEDULE_ACTION_SNOOZE) {
            cJSON_AddNumberToObject(params, "snoozeUntil", event->snooze_until);
            cJSON_AddNumberToObject(params, "snoozeCount", event->snooze_count);
        }
        if (event->action_type == QM_SCHEDULE_ACTION_COMPLETE ||
            event->action_type == QM_SCHEDULE_ACTION_DISMISS) {
            cJSON_AddStringToObject(params, "closeReason",
                                    close_reason_name(event->close_reason));
        }
        cJSON_AddNumberToObject(params, "eventAt", event->action_at);
    } else if (event->event_type == QM_SCHEDULE_OUTBOX_SYSTEM_TRIGGERED) {
        sub_method = "triggered";
        cJSON_AddStringToObject(params, "scheduleId", event->schedule_id);
        cJSON_AddNumberToObject(params, "revision", event->revision);
        cJSON_AddNumberToObject(params, "occurrenceAt", event->occurrence_at);
        cJSON_AddStringToObject(params, "reminderId", event->reminder_id);
        cJSON_AddNumberToObject(params, "eventAt", event->event_at);
        cJSON_AddNumberToObject(params, "triggeredAt", event->event_at);
    } else {
        cJSON_AddStringToObject(params, "reason",
                                event->reason[0] ? event->reason : "storageReset");
        if (event->confirmed_revision > 0) {
            cJSON_AddNumberToObject(params, "confirmedRevision",
                                    event->confirmed_revision);
        } else {
            cJSON_AddNullToObject(params, "confirmedRevision");
        }
        groups = cJSON_CreateArray();
        if (!groups) { ret = -QM_ENOMEM; goto done; }
        qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
        for (i = 0; i < QM_SCHEDULE_MAX_GROUPS_INTERNAL; i++) {
            if (schedule->store.groups[i].used) {
                cJSON *group = cJSON_CreateObject();
                if (!group) { ret = -QM_ENOMEM; break; }
                cJSON_AddStringToObject(group, "groupId",
                                        schedule->store.groups[i].group_id);
                cJSON_AddNumberToObject(group, "groupRevision",
                                        schedule->store.groups[i].revision);
                cJSON_AddItemToArray(groups, group);
            }
        }
        qm_mutex_unlock(&schedule->lock);
        if (ret != QM_EOK) goto done;
        cJSON_AddItemToObject(params, "groups", groups);
        groups = NULL;
        cJSON_AddNumberToObject(params, "eventAt", event->event_at);
    }
    cJSON_AddStringToObject(root, "subMethod", sub_method);
    text = cJSON_PrintUnformatted(root);
    if (!text || strlen(text) >= payload_len) {
        ret = -QM_EFULL;
        goto done;
    }
    strcpy(payload, text);
done:
    if (groups) cJSON_Delete(groups);
    if (text) qm_free(text);
    if (root) cJSON_Delete(root);
    return ret;
}

static uint16_t retry_delay_sec(uint8_t retry_count)
{
    backoff_algorithm_ctx_t backoff;
    uint16_t delay_ms = 1000;
    uint8_t i;

    backoff_algorithm_init(&backoff, 1000, 30000,
                           CONFIG_QM_SCHEDULE_OUTBOX_MAX_RETRIES);
    for (i = 0; i < retry_count; i++) {
        if (backoff_algorithm_get_next_delay(&backoff, &delay_ms) !=
            BACKOFF_ALGORITHM_SUCCESS) {
            return 30;
        }
    }
    return delay_ms < 1000 ? 1 : (uint16_t)((delay_ms + 999u) / 1000u);
}

int qm_schedule_iot_flush_outbox(qm_schedule_iot_t iot)
{
    qm_schedule_t schedule;
    qm_schedule_outbox_record_t event;
    char *payload;
    int64_t now;
    uint16_t outbox_total;
    uint16_t i;
    int selected = -1;
    int ret;

    if (!iot) return -QM_EINVAL;
    if (!iot->config.send_event) return -QM_ENOSYS;
    schedule = iot->config.schedule;
    now = (int64_t)qm_time(NULL);
    memset(&event, 0, sizeof(event));

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    outbox_total = schedule->config.max_outbox_user +
        schedule->config.max_outbox_system;
    if (outbox_total > QM_SCHEDULE_MAX_OUTBOX_INTERNAL) {
        outbox_total = QM_SCHEDULE_MAX_OUTBOX_INTERNAL;
    }
    for (i = 0; i < outbox_total; i++) {
        qm_schedule_outbox_record_t *candidate = &schedule->store.outbox[i];
        if (!candidate->used) continue;
        if (candidate->awaiting_ack && candidate->next_attempt_at > now) continue;
        if (candidate->retry_count >= CONFIG_QM_SCHEDULE_OUTBOX_MAX_RETRIES) {
            qm_mutex_unlock(&schedule->lock);
            return -QM_EBUSY;
        }
        candidate->awaiting_ack = true;
        candidate->retry_count++;
        candidate->next_attempt_at = now +
            CONFIG_QM_SCHEDULE_OUTBOX_ACK_TIMEOUT_SEC;
        event = *candidate;
        selected = (int)i;
        break;
    }
    qm_mutex_unlock(&schedule->lock);
    if (selected < 0) return QM_EOK;

    ret = qm_schedule_store_save_all(schedule);
    if (ret != QM_EOK) return ret;
    payload = (char *)qm_malloc(CONFIG_QM_SCHEDULE_IOT_MAX_PAYLOAD_LEN);
    if (!payload) return -QM_ENOMEM;
    ret = build_event_payload(schedule, &event, payload,
                              CONFIG_QM_SCHEDULE_IOT_MAX_PAYLOAD_LEN);
    if (ret == QM_EOK) {
        ret = iot->config.send_event(iot->config.iot_handle, payload,
                                     (uint32_t)strlen(payload));
    }
    qm_free(payload);
    if (ret == QM_EOK) return QM_EOK;

    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    if (schedule->store.outbox[selected].used &&
        strcmp(schedule->store.outbox[selected].event_id,
               event.event_id) == 0) {
        schedule->store.outbox[selected].awaiting_ack = false;
        schedule->store.outbox[selected].next_attempt_at = now +
            retry_delay_sec(schedule->store.outbox[selected].retry_count);
    }
    qm_mutex_unlock(&schedule->lock);
    (void)qm_schedule_store_save_all(schedule);
    return ret;
}

int qm_schedule_iot_ack_event(qm_schedule_iot_t iot,
                              const char *event_id)
{
    qm_schedule_t schedule;
    uint16_t outbox_total;
    uint16_t i;
    bool removed = false;

    if (!iot || !event_id || event_id[0] == '\0' ||
        strlen(event_id) >= QM_SCHEDULE_EVENT_ID_MAX_DEFAULT) {
        return -QM_EINVAL;
    }
    schedule = iot->config.schedule;
    qm_mutex_lock(&schedule->lock, QM_WAIT_FOREVER);
    outbox_total = schedule->config.max_outbox_user +
        schedule->config.max_outbox_system;
    if (outbox_total > QM_SCHEDULE_MAX_OUTBOX_INTERNAL) {
        outbox_total = QM_SCHEDULE_MAX_OUTBOX_INTERNAL;
    }
    for (i = 0; i < outbox_total; i++) {
        if (schedule->store.outbox[i].used &&
            strcmp(schedule->store.outbox[i].event_id, event_id) == 0) {
            memset(&schedule->store.outbox[i], 0,
                   sizeof(schedule->store.outbox[i]));
            removed = true;
            break;
        }
    }
    qm_mutex_unlock(&schedule->lock);

    return removed ? qm_schedule_store_save_all(schedule) : QM_EOK;
}

int qm_schedule_iot_handle_downlink(qm_schedule_iot_t iot,
                                    const char *payload,
                                    uint32_t payload_len,
                                    char *reply,
                                    uint32_t reply_len)
{
    cJSON *root;
    cJSON *params;
    const char *method;
    const char *sub_method;
    int64_t id64 = 0;
    int ret;
    dispatch_result_t result;

    memset(&result, 0, sizeof(result));

    if (!iot || !payload || payload_len == 0 || !reply || reply_len == 0) {
        return -QM_EINVAL;
    }
    if (payload_len > CONFIG_QM_SCHEDULE_IOT_MAX_PAYLOAD_LEN) {
        build_reply(reply, reply_len, iot->config.schedule, NULL, NULL, 0,
                    -QM_EFULL);
        return -QM_EFULL;
    }

    root = cJSON_ParseWithLength(payload, payload_len);
    if (!root) {
        build_reply(reply, reply_len, NULL, NULL, NULL, 0, -QM_EINVAL);
        return -QM_EINVAL;
    }

    method = json_string(root, "method");
    sub_method = json_string(root, "subMethod");
    params = cJSON_GetObjectItemCaseSensitive(root, "params");
    (void)json_int(root, "id", 0, &id64);

    if (method && strcmp(method, "scheduleReply") == 0) {
        const char *event_id;
        const char *binding_version;
        int64_t code = 0;
        int64_t retry_after = 0;
        bool accepted = false;
        bool idempotent = false;
        bool found = false;
        bool ack_matches = false;
        uint16_t outbox_total;
        uint16_t i;

        if (!cJSON_IsObject(params) ||
            validate_schema(params) != QM_EOK ||
            json_int(root, "code", 1, &code) != QM_EOK ||
            json_bool(params, "accepted", 1, &accepted) != QM_EOK ||
            json_bool(params, "idempotent", 1, &idempotent) != QM_EOK) {
            cJSON_Delete(root);
            reply[0] = '\0';
            return -QM_EINVAL;
        }
        event_id = json_string(params, "eventId");
        binding_version = json_string(params, "bindingVersion");
        if (!event_id || !binding_version || !sub_method) {
            cJSON_Delete(root);
            reply[0] = '\0';
            return -QM_EINVAL;
        }
        if (strcmp(sub_method, "syncRequestAck") == 0 &&
            !cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(params,
                                                            "groups"))) {
            cJSON_Delete(root);
            reply[0] = '\0';
            return -QM_EINVAL;
        }
        (void)idempotent;
        (void)json_int(params, "retryAfterSec", 0, &retry_after);
        qm_mutex_lock(&iot->config.schedule->lock, QM_WAIT_FOREVER);
        outbox_total = iot->config.schedule->config.max_outbox_user +
            iot->config.schedule->config.max_outbox_system;
        if (outbox_total > QM_SCHEDULE_MAX_OUTBOX_INTERNAL) {
            outbox_total = QM_SCHEDULE_MAX_OUTBOX_INTERNAL;
        }
        for (i = 0; i < outbox_total; i++) {
            qm_schedule_outbox_record_t *event =
                &iot->config.schedule->store.outbox[i];
            const char *expected_ack = "syncRequestAck";
            if (!event->used || strcmp(event->event_id, event_id) != 0) continue;
            found = true;
            if (event->event_type == QM_SCHEDULE_OUTBOX_SYSTEM_TRIGGERED) {
                expected_ack = "triggeredAck";
            } else if (event->event_type == QM_SCHEDULE_OUTBOX_USER_ACTION) {
                switch (event->action_type) {
                case QM_SCHEDULE_ACTION_ACKNOWLEDGE: expected_ack = "acknowledgedAck"; break;
                case QM_SCHEDULE_ACTION_COMPLETE: expected_ack = "completedAck"; break;
                case QM_SCHEDULE_ACTION_SNOOZE: expected_ack = "snoozedAck"; break;
                case QM_SCHEDULE_ACTION_DISMISS: expected_ack = "dismissedAck"; break;
                default: expected_ack = ""; break;
                }
            }
            ack_matches = strcmp(expected_ack, sub_method) == 0 &&
                strcmp(event->binding_version, binding_version) == 0;
            if (ack_matches && (code != 0 || !accepted)) {
                event->awaiting_ack = false;
                event->next_attempt_at = (int64_t)qm_time(NULL) +
                    (retry_after > 0 ? retry_after :
                     retry_delay_sec(event->retry_count));
            }
            break;
        }
        qm_mutex_unlock(&iot->config.schedule->lock);
        if (!found) {
            ret = QM_EOK;
        } else if (!ack_matches) {
            ret = -QM_EINVAL;
        } else if (code != 0 || !accepted) {
            ret = qm_schedule_store_save_all(iot->config.schedule);
            if (ret == QM_EOK) ret = -QM_EBUSY;
        } else {
            ret = qm_schedule_iot_ack_event(iot, event_id);
        }
        cJSON_Delete(root);
        reply[0] = '\0';
        return ret;
    }

    if (!method || strcmp(method, "schedule") != 0 || !cJSON_IsObject(params)) {
        ret = -QM_EINVAL;
    } else if ((ret = validate_schema(params)) != QM_EOK) {
    } else if (handle_query(iot->config.schedule, sub_method, params,
                            (int)id64, reply, reply_len, &ret)) {
        cJSON_Delete(root);
        if (iot->config.send_reply) {
            (void)iot->config.send_reply(iot->config.iot_handle, reply,
                                         (uint32_t)strlen(reply));
        }
        return ret;
    } else {
        ret = dispatch(iot->config.schedule, sub_method, params, &result);
    }
    if (sub_method && strcmp(sub_method, "sync") == 0) {
        if (build_sync_reply(iot->config.schedule, params, (int)id64,
                             &result, ret, reply, reply_len) != QM_EOK) {
            build_reply(reply, reply_len, iot->config.schedule, sub_method,
                        params, (int)id64, -QM_EFULL);
        }
    } else if (sub_method &&
               (strcmp(sub_method, "create") == 0 ||
                strcmp(sub_method, "modify") == 0 ||
                strcmp(sub_method, "delete") == 0 ||
                strcmp(sub_method, "enable") == 0 ||
                strcmp(sub_method, "acknowledge") == 0 ||
                strcmp(sub_method, "complete") == 0 ||
                strcmp(sub_method, "snooze") == 0 ||
                strcmp(sub_method, "dismiss") == 0)) {
        if (build_dispatch_reply(iot->config.schedule, sub_method, params,
                                 (int)id64, &result, ret, reply,
                                 reply_len) != QM_EOK) {
            build_reply(reply, reply_len, iot->config.schedule, sub_method,
                        params, (int)id64, -QM_EFULL);
        }
    } else {
        build_reply(reply, reply_len, iot->config.schedule, sub_method, params,
                    (int)id64, ret);
    }
    cJSON_Delete(root);
    if (iot->config.send_reply) {
        (void)iot->config.send_reply(iot->config.iot_handle, reply,
                                     (uint32_t)strlen(reply));
    }
    return ret;
}
