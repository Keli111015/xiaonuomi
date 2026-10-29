/**
 * @file qm_agent_agora_stream.c
 * @brief Asynchronous Agora business-stream framing and message normalization.
 *
 * Agora 业务流的异步分帧和消息标准化。输入回调只将字节复制到受限队列；解析任务负责
 * 重组帧、解析 JSON/Base64 数据，并在回调期间交付借用的 qm_agent_event_t。
 */

#include "qm_agent_agora_stream.h"
#include "qm_agent_internal.h"

#include "cJSON.h"
#include "qm.h"
#include "qm_log.h"
#include "qm_string.h"
#include "qm_utils_base64.h"
#include "qm_utils_timer.h"

#define LOG_TAG "qm_agent_agora_stream"

#ifndef CONFIG_QM_AGENT_AGORA_STREAM_QUEUE_LEN
#define CONFIG_QM_AGENT_AGORA_STREAM_QUEUE_LEN 16
#endif

#ifndef CONFIG_QM_AGENT_AGORA_STREAM_TASK_STACK_SIZE
#define CONFIG_QM_AGENT_AGORA_STREAM_TASK_STACK_SIZE 4096
#endif

#ifndef CONFIG_QM_AGENT_AGORA_STREAM_TASK_PRIORITY
#define CONFIG_QM_AGENT_AGORA_STREAM_TASK_PRIORITY 5
#endif

#define QM_AGENT_AGORA_STREAM_CACHE_MAX 10U
#define QM_AGENT_AGORA_STREAM_MSG_ID_MAX 64U
#define QM_AGENT_AGORA_STREAM_CACHE_TIMEOUT_MS 8000U

#define AGORA_STREAM_OBJECT "object"
#define AGORA_STREAM_TEXT "text"
#define AGORA_STREAM_WORDS "words"
#define AGORA_STREAM_START_MS "start_ms"
#define AGORA_STREAM_DURATION_MS "duration_ms"
#define AGORA_STREAM_TURN_STATUS "turn_status"
#define AGORA_STREAM_FINAL "final"
#define AGORA_STREAM_STATE "state"
#define AGORA_STREAM_TURN_ID "turn_id"

/*
 * Agora stream messages arrive as text packets:
 *     msg_id|index|total|base64(json)
 *
 * A JSON body may be split across several SDK packets. The parser keeps a
 * small per-message cache until all fragments are present, then decodes the
 * assembled base64 payload and emits normalized qm_agent events.
 */
typedef struct {
    uint32_t index;
    uint32_t total;
    uint32_t payload_len;
    char *payload;
} agora_stream_packet_t;

typedef struct {
    qm_bool used;
    uint32_t updated_ms;
    qm_utils_time_t expire_timer;
    char msg_id[QM_AGENT_AGORA_STREAM_MSG_ID_MAX];
    uint32_t total;
    uint32_t count;
    agora_stream_packet_t **packets;
} agora_stream_cache_t;

typedef enum {
    AGORA_STREAM_MSG_DATA = 0,
    AGORA_STREAM_MSG_STOP,
} agora_stream_msg_type_t;

typedef struct {
    agora_stream_msg_type_t type;
    uint32_t generation;
    uint32_t len;
    char *data;
} agora_stream_msg_t;

struct qm_agent_agora_stream {
    qm_agent_agora_stream_event_cb_t event_cb;
    void *user_ctx;
    agora_stream_cache_t cache[QM_AGENT_AGORA_STREAM_CACHE_MAX];
    qm_task_t task;
    qm_queue_t queue;
    qm_mutex_t lock;
    qm_sem_t exit_sem;
    qm_bool queue_valid;
    qm_bool lock_valid;
    qm_bool exit_sem_valid;
    qm_bool task_running;
    qm_bool stop_requested;
    uint32_t generation;

    /*
     * assistant.transcription text is cumulative within one turn. Keep the
     * last emitted byte offset so qm_agent only forwards the new delta text.
     */
    int32_t assistant_turn_id;
    uint32_t assistant_last_text_len;
};

static void emit_event(qm_agent_agora_stream_t *stream, const qm_agent_event_t *event)
{
    if (stream != NULL && stream->event_cb != NULL && event != NULL) {
        stream->event_cb(event, stream->user_ctx);
    }
}

static void cache_clear(agora_stream_cache_t *cache)
{
    uint32_t i;

    if (cache == NULL) {
        return;
    }

    if (cache->packets != NULL) {
        for (i = 0; i < cache->total; i++) {
            if (cache->packets[i] != NULL) {
                qm_free(cache->packets[i]->payload);
                cache->packets[i]->payload = NULL;
                qm_free(cache->packets[i]);
                cache->packets[i] = NULL;
            }
        }
        qm_free(cache->packets);
        cache->packets = NULL;
    }

    qm_memset(cache, 0, sizeof(*cache));
}

static void stream_msg_free(agora_stream_msg_t *msg)
{
    if (msg == NULL) {
        return;
    }

    if (msg->data != NULL) {
        qm_free(msg->data);
        msg->data = NULL;
    }
    msg->len = 0U;
}

static void stream_queue_drain(qm_agent_agora_stream_t *stream)
{
    agora_stream_msg_t msg;
    unsigned int size;

    if (stream == NULL || !stream->queue_valid) {
        return;
    }

    while (1) {
        size = sizeof(msg);
        qm_memset(&msg, 0, sizeof(msg));
        if (qm_queue_recv(&stream->queue, &msg, &size, QM_NO_WAIT) != QM_EOK) {
            break;
        }
        stream_msg_free(&msg);
    }
}

static void clear_expired_caches(qm_agent_agora_stream_t *stream)
{
    uint32_t i;

    if (stream == NULL) {
        return;
    }

    for (i = 0; i < QM_AGENT_AGORA_STREAM_CACHE_MAX; i++) {
        if (!stream->cache[i].used) {
            continue;
        }
        /* Drop incomplete fragment sets so packet loss does not leak memory. */
        if (qm_utils_time_is_expired(&stream->cache[i].expire_timer)) {
            QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_STATS, "stream cache expired msg_id=%s count=%u total=%u",
                    stream->cache[i].msg_id,
                    (unsigned int)stream->cache[i].count,
                    (unsigned int)stream->cache[i].total);
            cache_clear(&stream->cache[i]);
        }
    }
}

static agora_stream_cache_t *cache_find(qm_agent_agora_stream_t *stream,
                                        const char *msg_id)
{
    uint32_t i;

    if (stream == NULL || msg_id == NULL) {
        return NULL;
    }

    for (i = 0; i < QM_AGENT_AGORA_STREAM_CACHE_MAX; i++) {
        if (stream->cache[i].used &&
            qm_strncmp(stream->cache[i].msg_id,
                       msg_id,
                       QM_AGENT_AGORA_STREAM_MSG_ID_MAX) == 0) {
            return &stream->cache[i];
        }
    }

    return NULL;
}

static agora_stream_cache_t *cache_alloc(qm_agent_agora_stream_t *stream,
                                         const char *msg_id,
                                         uint32_t total)
{
    uint32_t i;
    uint32_t selected = QM_AGENT_AGORA_STREAM_CACHE_MAX;
    uint32_t oldest_ms = UINT32_MAX;
    agora_stream_cache_t *cache;

    if (stream == NULL || msg_id == NULL || total == 0U) {
        return NULL;
    }

    /*
     * Reuse an empty slot first. If all slots are occupied, replace the oldest
     * incomplete message; newer complete stream events are more useful than
     * holding stale fragments indefinitely.
     */
    for (i = 0; i < QM_AGENT_AGORA_STREAM_CACHE_MAX; i++) {
        if (!stream->cache[i].used) {
            selected = i;
            break;
        }
        if (stream->cache[i].updated_ms < oldest_ms) {
            oldest_ms = stream->cache[i].updated_ms;
            selected = i;
        }
    }

    if (selected >= QM_AGENT_AGORA_STREAM_CACHE_MAX) {
        return NULL;
    }

    cache = &stream->cache[selected];
    if (cache->used) {
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_STATS, "replace stream cache msg_id=%s", cache->msg_id);
        cache_clear(cache);
    }

    qm_memset(cache, 0, sizeof(*cache));
    cache->packets = (agora_stream_packet_t **)qm_malloc(total * sizeof(*cache->packets));
    if (cache->packets == NULL) {
        return NULL;
    }

    qm_memset(cache->packets, 0, total * sizeof(*cache->packets));
    qm_strncpy(cache->msg_id, msg_id, sizeof(cache->msg_id) - 1U);
    cache->used = QM_TRUE;
    cache->total = total;
    cache->updated_ms = qm_now_ms();
    qm_utils_time_countdown_ms(&cache->expire_timer,
                               QM_AGENT_AGORA_STREAM_CACHE_TIMEOUT_MS);
    return cache;
}

static int parse_packet_header(const char *data,
                               uint32_t len,
                               char *msg_id,
                               uint32_t msg_id_size,
                               uint32_t *index,
                               uint32_t *total,
                               const char **payload,
                               uint32_t *payload_len)
{
    uint32_t sep[3];
    uint32_t sep_count = 0;
    uint32_t i;
    char number_buf[12];
    uint32_t field_len;

    if (data == NULL || msg_id == NULL || index == NULL || total == NULL ||
        payload == NULL || payload_len == NULL || msg_id_size == 0U) {
        return -QM_EINVAL;
    }

    /*
     * The SDK gives a pointer plus length, not guaranteed NUL-terminated text.
     * Parse by explicit separator offsets instead of sscanf/strlen.
     */
    for (i = 0; i < len && sep_count < 3U; i++) {
        if (data[i] == '|') {
            sep[sep_count++] = i;
        }
    }

    if (sep_count != 3U || sep[0] == 0U || sep[0] >= msg_id_size) {
        return -QM_EINVAL;
    }

    qm_memset(msg_id, 0, msg_id_size);
    qm_memcpy(msg_id, data, sep[0]);

    field_len = sep[1] - sep[0] - 1U;
    if (field_len == 0U || field_len >= sizeof(number_buf)) {
        return -QM_EINVAL;
    }
    qm_memset(number_buf, 0, sizeof(number_buf));
    qm_memcpy(number_buf, data + sep[0] + 1U, field_len);
    if (qm_str2uint(number_buf, (uint8_t)field_len, index) != QM_EOK) {
        return -QM_EINVAL;
    }

    field_len = sep[2] - sep[1] - 1U;
    if (field_len == 0U || field_len >= sizeof(number_buf)) {
        return -QM_EINVAL;
    }
    qm_memset(number_buf, 0, sizeof(number_buf));
    qm_memcpy(number_buf, data + sep[1] + 1U, field_len);
    if (qm_str2uint(number_buf, (uint8_t)field_len, total) != QM_EOK) {
        return -QM_EINVAL;
    }

    *payload = data + sep[2] + 1U;
    *payload_len = len - sep[2] - 1U;
    if (*index == 0U || *total == 0U || *index > *total || *payload_len == 0U) {
        return -QM_EINVAL;
    }

    return QM_EOK;
}

static qm_agent_dialog_state_t parse_agent_state(const char *state)
{
    if (state == NULL) {
        return QM_AGENT_DIALOG_STATE_NONE;
    }

    if (qm_strcmp(state, "silent") == 0) {
        return QM_AGENT_DIALOG_STATE_SILENT;
    }
    if (qm_strcmp(state, "speaking") == 0) {
        return QM_AGENT_DIALOG_STATE_SPEAKING;
    }
    if (qm_strcmp(state, "thinking") == 0) {
        return QM_AGENT_DIALOG_STATE_THINKING;
    }
    if (qm_strcmp(state, "listening") == 0) {
        return QM_AGENT_DIALOG_STATE_LISTENING;
    }

    return QM_AGENT_DIALOG_STATE_NONE;
}

static int json_get_int(const cJSON *root, const char *key, int32_t *out)
{
    const cJSON *item;

    if (root == NULL || key == NULL || out == NULL) {
        return -QM_EINVAL;
    }

    item = cJSON_GetObjectItem(root, key);
    if (!cJSON_IsNumber(item)) {
        return -QM_ENOENT;
    }

    *out = (int32_t)cJSON_GetNumberValue(item);
    return QM_EOK;
}

static uint32_t parse_tts_duration_ms(const cJSON *root)
{
    const cJSON *words;
    const cJSON *word;
    const cJSON *duration;
    const cJSON *start;
    uint32_t duration_ms = 0U;
    uint32_t first_start_ms = 0U;
    uint32_t last_end_ms = 0U;
    qm_bool has_start = QM_FALSE;

    if (root == NULL) {
        return 0U;
    }

    words = cJSON_GetObjectItem(root, AGORA_STREAM_WORDS);
    if (!cJSON_IsArray(words)) {
        return 0U;
    }

    /*
     * Preferred timing is the sum of words[].duration_ms. If a backend only
     * provides start_ms, fall back to the first-start to last-end span.
     */
    cJSON_ArrayForEach(word, words) {
        int32_t start_ms_value;
        int32_t duration_ms_value;

        if (!cJSON_IsObject(word)) {
            continue;
        }

        duration = cJSON_GetObjectItem(word, AGORA_STREAM_DURATION_MS);
        if (cJSON_IsNumber(duration) && duration->valueint > 0) {
            duration_ms += (uint32_t)duration->valueint;
        }

        start = cJSON_GetObjectItem(word, AGORA_STREAM_START_MS);
        if (cJSON_IsNumber(start)) {
            start_ms_value = (int32_t)cJSON_GetNumberValue(start);
            duration_ms_value = cJSON_IsNumber(duration) ?
                                (int32_t)cJSON_GetNumberValue(duration) : 0;
            if (start_ms_value >= 0) {
                uint32_t end_ms = (uint32_t)start_ms_value;

                if (!has_start) {
                    first_start_ms = (uint32_t)start_ms_value;
                    has_start = QM_TRUE;
                }
                if (duration_ms_value > 0) {
                    end_ms += (uint32_t)duration_ms_value;
                }
                if (end_ms > last_end_ms) {
                    last_end_ms = end_ms;
                }
            }
        }
    }

    if (duration_ms == 0U && has_start && last_end_ms > first_start_ms) {
        duration_ms = last_end_ms - first_start_ms;
    }

    return duration_ms;
}

static int handle_user_transcription(qm_agent_agora_stream_t *stream, cJSON *root)
{
    const cJSON *text;
    const cJSON *final;
    qm_agent_event_t event = {0};

    text = cJSON_GetObjectItem(root, AGORA_STREAM_TEXT);
    final = cJSON_GetObjectItem(root, AGORA_STREAM_FINAL);
    if (!cJSON_IsString(text)) {
        return -QM_EINVAL;
    }

    /* Missing final is treated as final so older backend payloads still work. */
    event.id = QM_AGENT_EVENT_ASR;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.asr.text = cJSON_GetStringValue(text);
    event.data.asr.final = cJSON_IsBool(final) ? (cJSON_IsTrue(final) ? QM_TRUE : QM_FALSE) : QM_TRUE;
    emit_event(stream, &event);
    return QM_EOK;
}

static int handle_assistant_transcription(qm_agent_agora_stream_t *stream, cJSON *root)
{
    const cJSON *text;
    const cJSON *turn_status;
    const char *text_value;
    const char *delta_text;
    uint32_t text_len;
    uint32_t delta_len;
    int32_t turn_id = -1;
    uint32_t duration_ms;
    qm_bool final = QM_FALSE;
    qm_agent_event_t event = {0};

    text = cJSON_GetObjectItem(root, AGORA_STREAM_TEXT);
    if (!cJSON_IsString(text)) {
        return -QM_EINVAL;
    }
    duration_ms = parse_tts_duration_ms(root);

    (void)json_get_int(root, AGORA_STREAM_TURN_ID, &turn_id);
    if (turn_id != stream->assistant_turn_id) {
        stream->assistant_turn_id = turn_id;
        stream->assistant_last_text_len = 0U;
    }

    text_value = cJSON_GetStringValue(text);
    text_len = text_value ? (uint32_t)qm_strlen(text_value) : 0U;
    if (text_value == NULL || text_len == 0U) {
        return -QM_EINVAL;
    }

    if (text_len < stream->assistant_last_text_len) {
        /* Backend restarted or rewrote the turn; restart delta emission. */
        stream->assistant_last_text_len = 0U;
    }

    delta_text = text_value + stream->assistant_last_text_len;
    delta_len = text_len - stream->assistant_last_text_len;

    turn_status = cJSON_GetObjectItem(root, AGORA_STREAM_TURN_STATUS);
    if (cJSON_IsNumber(turn_status) && turn_status->valueint == 1) {
        final = QM_TRUE;
    }

    if (delta_len > 0U) {
        event.id = QM_AGENT_EVENT_TTS;
        event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
        event.data.tts.text = delta_text;
        event.data.tts.duration_ms = duration_ms;
        event.data.tts.final = final;
        emit_event(stream, &event);
    }

    if (final) {
        stream->assistant_last_text_len = 0U;
    } else {
        stream->assistant_last_text_len = text_len;
    }

    return QM_EOK;
}

static int handle_message_state(qm_agent_agora_stream_t *stream, cJSON *root)
{
    const cJSON *state;
    qm_agent_event_t event = {0};

    state = cJSON_GetObjectItem(root, AGORA_STREAM_STATE);
    if (!cJSON_IsString(state)) {
        return -QM_EINVAL;
    }

    event.id = QM_AGENT_EVENT_AGENT_STATE;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.data.agent_state.text = cJSON_GetStringValue(state);
    event.data.agent_state.state = parse_agent_state(event.data.agent_state.text);
    emit_event(stream, &event);
    return QM_EOK;
}

static int handle_message_interrupt(qm_agent_agora_stream_t *stream, cJSON *root)
{
    int32_t turn_id = 0;
    qm_agent_event_t event = {0};

    (void)json_get_int(root, AGORA_STREAM_TURN_ID, &turn_id);
    stream->assistant_last_text_len = 0U;
    stream->assistant_turn_id = -1;

    event.id = QM_AGENT_EVENT_AGENT_ABORTED;
    event.source = QM_AGENT_EVENT_SOURCE_TRANSPORT;
    event.code = turn_id;
    emit_event(stream, &event);
    return QM_EOK;
}

static int dispatch_json(qm_agent_agora_stream_t *stream,
                         const uint8_t *json,
                         uint32_t json_len)
{
    cJSON *root;
    const cJSON *object;
    const char *object_value;
    int ret = QM_EOK;

    if (stream == NULL || json == NULL || json_len == 0U) {
        return -QM_EINVAL;
    }

    root = cJSON_ParseWithLength((const char *)json, json_len);
    if (root == NULL) {
        return -QM_EINVAL;
    }

    object = cJSON_GetObjectItem(root, AGORA_STREAM_OBJECT);
    if (!cJSON_IsString(object)) {
        ret = -QM_EINVAL;
        goto out;
    }

    object_value = cJSON_GetStringValue(object);
    /* Keep the dispatch table tiny and explicit: these are the known agent objects. */
    if (qm_strcmp(object_value, "assistant.transcription") == 0) {
        ret = handle_assistant_transcription(stream, root);
    } else if (qm_strcmp(object_value, "user.transcription") == 0) {
        ret = handle_user_transcription(stream, root);
    } else if (qm_strcmp(object_value, "message.state") == 0) {
        ret = handle_message_state(stream, root);
    } else if (qm_strcmp(object_value, "message.interrupt") == 0) {
        ret = handle_message_interrupt(stream, root);
    } else {
        QM_AGENT_LOGD(LOG_TAG, QM_AGENT_LOG_PAYLOAD, "ignore stream object=%s", object_value ? object_value : "");
    }

out:
    cJSON_Delete(root);
    return ret;
}

static int decode_and_dispatch(qm_agent_agora_stream_t *stream,
                               const char *base64,
                               uint32_t base64_len)
{
    uint8_t *decoded;
    uint32_t decoded_len = 0U;
    int ret;

    if (stream == NULL || base64 == NULL || base64_len == 0U) {
        return -QM_EINVAL;
    }

    decoded = (uint8_t *)qm_malloc(base64_len + 1U);
    if (decoded == NULL) {
        return -QM_ENOMEM;
    }

    qm_memset(decoded, 0, base64_len + 1U);
    ret = qm_utils_base64decode((const uint8_t *)base64,
                                base64_len,
                                base64_len,
                                decoded,
                                &decoded_len);
    if (ret != QM_EOK || decoded_len == 0U) {
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_STATS, "stream base64 decode failed ret=%d len=%u", ret, (unsigned int)base64_len);
        qm_free(decoded);
        return -QM_EINVAL;
    }

    ret = dispatch_json(stream, decoded, decoded_len);
    qm_free(decoded);
    return ret;
}

static int cache_insert_packet(qm_agent_agora_stream_t *stream,
                               const char *msg_id,
                               uint32_t index,
                               uint32_t total,
                               const char *payload,
                               uint32_t payload_len)
{
    agora_stream_cache_t *cache;
    agora_stream_packet_t *packet;
    char *assembled;
    uint32_t total_len = 0U;
    uint32_t offset = 0U;
    uint32_t i;
    int ret = QM_EOK;

    cache = cache_find(stream, msg_id);
    if (cache == NULL) {
        cache = cache_alloc(stream, msg_id, total);
    }
    if (cache == NULL || cache->total != total) {
        return -QM_ENOMEM;
    }

    if (cache->packets[index - 1U] != NULL) {
        /* Duplicate fragments are harmless; keep the first copy. */
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_STATS, "duplicate stream packet msg_id=%s index=%u", msg_id, (unsigned int)index);
        return QM_EOK;
    }

    packet = (agora_stream_packet_t *)qm_malloc(sizeof(*packet));
    if (packet == NULL) {
        return -QM_ENOMEM;
    }
    qm_memset(packet, 0, sizeof(*packet));
    packet->payload = (char *)qm_malloc(payload_len + 1U);
    if (packet->payload == NULL) {
        qm_free(packet);
        return -QM_ENOMEM;
    }
    qm_memset(packet->payload, 0, payload_len + 1U);
    qm_memcpy(packet->payload, payload, payload_len);
    packet->payload_len = payload_len;
    packet->index = index;
    packet->total = total;

    cache->packets[index - 1U] = packet;
    cache->count++;
    cache->updated_ms = qm_now_ms();
    qm_utils_time_countdown_ms(&cache->expire_timer,
                               QM_AGENT_AGORA_STREAM_CACHE_TIMEOUT_MS);

    if (cache->count != cache->total) {
        return QM_EOK;
    }

    /* All fragments are present; concatenate base64 payloads in packet order. */
    for (i = 0; i < cache->total; i++) {
        if (cache->packets[i] == NULL) {
            return QM_EOK;
        }
        total_len += cache->packets[i]->payload_len;
    }

    assembled = (char *)qm_malloc(total_len + 1U);
    if (assembled == NULL) {
        cache_clear(cache);
        return -QM_ENOMEM;
    }

    qm_memset(assembled, 0, total_len + 1U);
    for (i = 0; i < cache->total; i++) {
        qm_memcpy(assembled + offset,
                  cache->packets[i]->payload,
                  cache->packets[i]->payload_len);
        offset += cache->packets[i]->payload_len;
    }

    ret = decode_and_dispatch(stream, assembled, total_len);
    qm_free(assembled);
    cache_clear(cache);
    return ret;
}

/**
 * @brief Process one owned parser message on the stream task.
 * 在流解析任务中处理一条自有消息。
 *
 * generation filters packets queued before reset/leave, preventing old turns
 * from producing events after a reconnect or interruption.
 * generation 会过滤 reset/leave 前排队的包，避免重连或中断后旧轮次继续产生事件。
 */
static int qm_agent_agora_stream_process(qm_agent_agora_stream_t *stream,
                                         const char *data,
                                         uint32_t len)
{
    char msg_id[QM_AGENT_AGORA_STREAM_MSG_ID_MAX];
    const char *payload;
    uint32_t payload_len;
    uint32_t index;
    uint32_t total;
    int ret;

    if (stream == NULL || data == NULL || len == 0U) {
        return -QM_EINVAL;
    }

    clear_expired_caches(stream);

    /* Fast path for single-packet messages, cache path for fragmented ones. */
    ret = parse_packet_header(data,
                              len,
                              msg_id,
                              sizeof(msg_id),
                              &index,
                              &total,
                              &payload,
                              &payload_len);
    if (ret != QM_EOK) {
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_STATS, "stream packet parse failed ret=%d len=%u", ret, (unsigned int)len);
        return ret;
    }

    if (total == 1U) {
        return decode_and_dispatch(stream, payload, payload_len);
    }

    return cache_insert_packet(stream,
                               msg_id,
                               index,
                               total,
                               payload,
                               payload_len);
}

/**
 * @brief Run the bounded asynchronous Agora stream parser queue.
 * 运行有界的异步 Agora 流解析队列。
 */
static void qm_agent_agora_stream_task(void *arg)
{
    qm_agent_agora_stream_t *stream = (qm_agent_agora_stream_t *)arg;
    agora_stream_msg_t msg;
    unsigned int size;

    if (stream == NULL) {
        qm_task_exit(NULL);
        return;
    }

    while (1) {
        size = sizeof(msg);
        qm_memset(&msg, 0, sizeof(msg));
        if (qm_queue_recv(&stream->queue, &msg, &size, QM_WAIT_FOREVER) != QM_EOK) {
            continue;
        }

        if (msg.type == AGORA_STREAM_MSG_STOP) {
            stream_msg_free(&msg);
            break;
        }

        if (!stream->stop_requested && msg.data != NULL && msg.len > 0U) {
            if (stream->lock_valid) {
                (void)qm_mutex_lock(&stream->lock, QM_WAIT_FOREVER);
            }
            if (msg.generation == stream->generation) {
                (void)qm_agent_agora_stream_process(stream, msg.data, msg.len);
            }
            if (stream->lock_valid) {
                (void)qm_mutex_unlock(&stream->lock);
            }
        }
        stream_msg_free(&msg);
    }

    stream_queue_drain(stream);
    stream->task_running = QM_FALSE;
    if (stream->exit_sem_valid) {
        (void)qm_sem_signal(&stream->exit_sem);
    }
    qm_task_exit(NULL);
}

static void qm_agent_agora_stream_reset_locked(qm_agent_agora_stream_t *stream)
{
    uint32_t i;

    if (stream == NULL) {
        return;
    }

    stream_queue_drain(stream);
    stream->generation++;

    /* Reset is called on leave/stop, so discard partial messages and TTS deltas. */
    for (i = 0; i < QM_AGENT_AGORA_STREAM_CACHE_MAX; i++) {
        cache_clear(&stream->cache[i]);
    }
    stream->assistant_turn_id = -1;
    stream->assistant_last_text_len = 0U;
}

/**
 * @brief Allocate parser storage and start its processing task.
 * 分配解析器存储并启动其处理任务。
 */
int qm_agent_agora_stream_init(qm_agent_agora_stream_t **stream,
                               qm_agent_agora_stream_event_cb_t event_cb,
                               void *user_ctx)
{
    qm_agent_agora_stream_t *ctx;
    int ret;

    if (stream == NULL) {
        return -QM_EINVAL;
    }

    ctx = (qm_agent_agora_stream_t *)qm_malloc(sizeof(*ctx));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }

    qm_memset(ctx, 0, sizeof(*ctx));
    ctx->event_cb = event_cb;
    ctx->user_ctx = user_ctx;
    ctx->assistant_turn_id = -1;

    ret = qm_mutex_new(&ctx->lock);
    if (ret != QM_EOK) {
        qm_free(ctx);
        return ret;
    }
    ctx->lock_valid = QM_TRUE;

    ret = qm_queue_new(&ctx->queue,
                       CONFIG_QM_AGENT_AGORA_STREAM_QUEUE_LEN,
                       sizeof(agora_stream_msg_t));
    if (ret != QM_EOK) {
        (void)qm_mutex_free(&ctx->lock);
        qm_free(ctx);
        return ret;
    }
    ctx->queue_valid = QM_TRUE;

    ret = qm_sem_new(&ctx->exit_sem, 0);
    if (ret != QM_EOK) {
        (void)qm_queue_free(&ctx->queue);
        (void)qm_mutex_free(&ctx->lock);
        qm_free(ctx);
        return ret;
    }
    ctx->exit_sem_valid = QM_TRUE;

    ctx->task_running = QM_TRUE;
    ret = qm_task_new(&ctx->task,
                      "agent_agora_stream",
                      qm_agent_agora_stream_task,
                      ctx,
                      CONFIG_QM_AGENT_AGORA_STREAM_TASK_STACK_SIZE,
                      CONFIG_QM_AGENT_AGORA_STREAM_TASK_PRIORITY);
    if (ret != QM_EOK) {
        ctx->task_running = QM_FALSE;
        (void)qm_sem_free(&ctx->exit_sem);
        (void)qm_queue_free(&ctx->queue);
        (void)qm_mutex_free(&ctx->lock);
        qm_free(ctx);
        return ret;
    }

    QM_AGENT_LOGI(LOG_TAG, QM_AGENT_LOG_LIFECYCLE, "agora stream task started queue=%u stack=%u prio=%u",
            (unsigned int)CONFIG_QM_AGENT_AGORA_STREAM_QUEUE_LEN,
            (unsigned int)CONFIG_QM_AGENT_AGORA_STREAM_TASK_STACK_SIZE,
            (unsigned int)CONFIG_QM_AGENT_AGORA_STREAM_TASK_PRIORITY);
    *stream = ctx;
    return QM_EOK;
}

/**
 * @brief Stop the parser task before freeing queues, cache, and callback context.
 * 在释放队列、缓存和回调上下文前停止解析任务。
 */
void qm_agent_agora_stream_deinit(qm_agent_agora_stream_t *stream)
{
    agora_stream_msg_t stop_msg;

    if (stream == NULL) {
        return;
    }

    stream->stop_requested = QM_TRUE;
    if (stream->task_running && stream->queue_valid) {
        qm_memset(&stop_msg, 0, sizeof(stop_msg));
        stop_msg.type = AGORA_STREAM_MSG_STOP;
        while (qm_queue_send(&stream->queue, &stop_msg, sizeof(stop_msg)) != QM_EOK &&
               stream->task_running) {
            qm_msleep(10);
        }
        if (stream->exit_sem_valid) {
            (void)qm_sem_wait(&stream->exit_sem, QM_WAIT_FOREVER);
        }
    }

    qm_agent_agora_stream_reset(stream);

    if (stream->exit_sem_valid) {
        (void)qm_sem_free(&stream->exit_sem);
        stream->exit_sem_valid = QM_FALSE;
    }
    if (stream->queue_valid) {
        stream_queue_drain(stream);
        (void)qm_queue_free(&stream->queue);
        stream->queue_valid = QM_FALSE;
    }
    if (stream->lock_valid) {
        (void)qm_mutex_free(&stream->lock);
        stream->lock_valid = QM_FALSE;
    }
    qm_free(stream);
}

/**
 * @brief Drop partial packets and invalidate queued data from the previous turn.
 * 丢弃不完整包，并使上一轮已排队数据失效。
 */
void qm_agent_agora_stream_reset(qm_agent_agora_stream_t *stream)
{
    if (stream == NULL) {
        return;
    }

    if (stream->lock_valid) {
        (void)qm_mutex_lock(&stream->lock, QM_WAIT_FOREVER);
    }
    qm_agent_agora_stream_reset_locked(stream);
    if (stream->lock_valid) {
        (void)qm_mutex_unlock(&stream->lock);
    }
}

/**
 * @brief Copy one borrowed SDK message into the bounded parser queue.
 * 将一条借用的 SDK 消息复制到有界解析队列。
 *
 * This function never retains data directly; -QM_EFULL means the caller should
 * report or apply its provider-specific backpressure policy.
 * 本函数不会直接保留 data；返回 -QM_EFULL 表示调用方应上报或执行供应商特定的背压策略。
 */
int qm_agent_agora_stream_input(qm_agent_agora_stream_t *stream,
                                const char *data,
                                uint32_t len)
{
    agora_stream_msg_t msg;
    uint32_t generation;

    if (stream == NULL || data == NULL || len == 0U) {
        return -QM_EINVAL;
    }
    if (stream->stop_requested || !stream->queue_valid) {
        return -QM_EINVAL;
    }

    if (stream->lock_valid) {
        (void)qm_mutex_lock(&stream->lock, QM_WAIT_FOREVER);
    }
    generation = stream->generation;
    if (stream->lock_valid) {
        (void)qm_mutex_unlock(&stream->lock);
    }

    qm_memset(&msg, 0, sizeof(msg));
    msg.type = AGORA_STREAM_MSG_DATA;
    msg.generation = generation;
    msg.len = len;
    msg.data = (char *)qm_malloc(len + 1U);
    if (msg.data == NULL) {
        return -QM_ENOMEM;
    }
    qm_memset(msg.data, 0, len + 1U);
    qm_memcpy(msg.data, data, len);

    if (qm_queue_send(&stream->queue, &msg, sizeof(msg)) != QM_EOK) {
        stream_msg_free(&msg);
        QM_AGENT_LOGW(LOG_TAG, QM_AGENT_LOG_STATS, "stream queue full len=%u", (unsigned int)len);
        return -QM_EFULL;
    }

    return QM_EOK;
}
