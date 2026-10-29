/**
 * @file comtrace_log.c
 * @brief CELP/1 edge log implementation.
 */
#include "comtrace_log.h"
#include "comtrace_frame.h"
#include "comtrace_internal.h"
#include "qm_config.h"
#include "qm_kernel.h"
#include "qm_string.h"

#ifndef CONFIG_QM_COMTRACE_MAX_FRAME
#define CONFIG_QM_COMTRACE_MAX_FRAME COMTRACE_MAX_FRAME
#endif
#ifndef CONFIG_QM_COMTRACE_QUEUE_DEPTH
#define CONFIG_QM_COMTRACE_QUEUE_DEPTH COMTRACE_LOG_DEFAULT_QUEUE_DEPTH
#endif

#define COMTRACE_LOG_MAX_TAG_KEY_LEN 64U
#define COMTRACE_LOG_MAX_TAG_VALUE_LEN 128U

typedef struct {
    uint8_t *data;
    uint16_t length;
    uint32_t reported_dropped;
} frame_slot_t;

struct comtrace_log {
    char sn[COMTRACE_MAX_SN_LEN + 1U];
    char device_name[COMTRACE_MAX_NAME_LEN + 1U];
    uint32_t sid;
    uint16_t max_frame;
    uint16_t heartbeat_interval;
    comtrace_transport_t transport;
    comtrace_send_cb_t send;
    comtrace_log_message_cb_t on_message;
    void *user;
    uint32_t sequence;
    uint32_t dropped;
    uint32_t pending_dropped;
    frame_slot_t *queue;
    uint16_t queue_depth;
    uint16_t queue_count;
    uint16_t queue_head;
    uint16_t queue_tail;
    comtrace_stream_t stream;
    char rx_sn[COMTRACE_MAX_SN_LEN + 1U];
};

typedef struct {
    comtrace_log_direction_t direction;
    comtrace_log_level_t level;
    const void *data;
    uint16_t data_len;
    const char *text;
    const comtrace_log_tag_t *tags;
    uint8_t tag_count;
    uint32_t sequence;
    uint32_t dropped;
} log_body_t;

static const char *const direction_names[] = {"rx", "tx", "event"};
static const char *const level_names[] = {"debug", "info", "warn", "error"};

static int body_register(CborEncoder *map, void *user)
{
    const comtrace_log_t *log = (const comtrace_log_t *)user;
    return comtrace_cbor_put_text(map, "device_name", log->device_name);
}

static int body_log(CborEncoder *map, void *user)
{
    log_body_t *body = (log_body_t *)user;
    CborEncoder tags;
    char dropped[11];
    size_t tag_count = body->tag_count + (body->dropped ? 1U : 0U);
    uint8_t i;
    int result;

    result = comtrace_cbor_put_uint(map, "sequence", body->sequence);
    if (result == QM_EOK)
        result = comtrace_cbor_put_text(map, "direction", direction_names[body->direction]);
    if (result == QM_EOK)
        result = comtrace_cbor_put_text(map, "level", level_names[body->level]);
    if (result == QM_EOK && body->text)
        result = comtrace_cbor_put_text(map, "text", body->text);
    if (result == QM_EOK && body->data) {
        result = cbor_encode_text_stringz(map, "data") == CborNoError ? QM_EOK : -QM_EFULL;
        if (result == QM_EOK)
            result = cbor_encode_byte_string(map, (const uint8_t *)body->data, body->data_len) ==
                             CborNoError
                         ? QM_EOK
                         : -QM_EFULL;
    }
    if (result != QM_EOK || tag_count == 0U)
        return result;
    if (cbor_encode_text_stringz(map, "tags") != CborNoError ||
        cbor_encoder_create_map(map, &tags, tag_count) != CborNoError)
        return -QM_EFULL;
    for (i = 0U; i < body->tag_count; ++i) {
        result = comtrace_cbor_put_text(&tags, body->tags[i].key, body->tags[i].value);
        if (result != QM_EOK)
            return result;
    }
    if (body->dropped) {
        uint8_t dropped_len = 0U;
        qm_uint2str(body->dropped, dropped, &dropped_len);
        dropped[dropped_len] = '\0';
        result = comtrace_cbor_put_text(&tags, "dropped_local", dropped);
        if (result != QM_EOK)
            return result;
    }
    return cbor_encoder_close_container_checked(map, &tags) == CborNoError ? QM_EOK : -QM_EFULL;
}

static int send_frame(comtrace_log_t *log, uint8_t *frame, uint16_t length,
                      uint32_t reported_dropped)
{
    int result = log->send(frame, length, log->user);
    if (result == QM_EOK || result == (int)length) {
        qm_free(frame);
        return QM_EOK;
    }
    if (log->transport == COMTRACE_TRANSPORT_TCP && log->queue && log->queue_depth) {
        if (log->queue_count == log->queue_depth) {
            log->pending_dropped += log->queue[log->queue_head].reported_dropped;
            qm_free(log->queue[log->queue_head].data);
            log->queue[log->queue_head].data = NULL;
            log->queue[log->queue_head].reported_dropped = 0U;
            log->queue_head = (uint16_t)((log->queue_head + 1U) % log->queue_depth);
            --log->queue_count;
            ++log->dropped;
            ++log->pending_dropped;
        }
        log->queue[log->queue_tail].data = frame;
        log->queue[log->queue_tail].length = length;
        log->queue[log->queue_tail].reported_dropped = reported_dropped;
        log->queue_tail = (uint16_t)((log->queue_tail + 1U) % log->queue_depth);
        ++log->queue_count;
        return QM_EOK;
    }
    qm_free(frame);
    return result < 0 ? result : -QM_EIO;
}

static int send_simple(comtrace_log_t *log, const char *type)
{
    uint8_t *frame;
    uint16_t length;
    int result = comtrace_frame_encode(log->sn, log->sid, log->max_frame, type, 4U, NULL, NULL,
                                       &frame, &length);
    return result == QM_EOK ? send_frame(log, frame, length, 0U) : result;
}

static int valid_tags(const comtrace_log_tag_t *tags, uint8_t tag_count)
{
    uint8_t i;
    uint8_t j;
    if (tag_count > COMTRACE_LOG_MAX_TAGS || (tag_count && !tags))
        return 0;
    for (i = 0U; i < tag_count; ++i) {
        size_t key_len = tags[i].key ? qm_strlen(tags[i].key) : 0U;
        size_t value_len = tags[i].value ? qm_strlen(tags[i].value) : 0U;
        if (key_len == 0U || key_len > COMTRACE_LOG_MAX_TAG_KEY_LEN || value_len == 0U ||
            value_len > COMTRACE_LOG_MAX_TAG_VALUE_LEN ||
            (key_len == 13U && qm_memcmp(tags[i].key, "dropped_local", 13U) == 0))
            return 0;
        for (j = 0U; j < i; ++j) {
            if (qm_strlen(tags[j].key) == key_len &&
                qm_memcmp(tags[j].key, tags[i].key, (uint32_t)key_len) == 0)
                return 0;
        }
    }
    return 1;
}

static int parse_error(CborValue *root, comtrace_log_t *log)
{
    comtrace_log_message_t message;
    uint64_t code;
    if (comtrace_cbor_uint(root, "code", &code) != QM_EOK || code > UINT32_MAX)
        return -QM_EINVAL;
    if (!log->on_message)
        return QM_EOK;
    qm_memset(&message, 0, sizeof(message));
    message.type = COMTRACE_LOG_MESSAGE_ERROR;
    message.error_code = (uint32_t)code;
    log->on_message(&message, log->user);
    return QM_EOK;
}

static int parse_payload(const uint8_t *payload, uint16_t length, void *user)
{
    comtrace_log_t *log = (comtrace_log_t *)user;
    CborParser parser;
    CborValue root;
    comtrace_log_message_t message;
    char type[24];
    size_t type_len;
    uint64_t version;
    uint64_t number;
    uint32_t sid;

    if (comtrace_cbor_root(payload, length, &parser, &root) != QM_EOK ||
        comtrace_cbor_uint(&root, "v", &version) != QM_EOK ||
        version != COMTRACE_PROTOCOL_VERSION ||
        comtrace_cbor_text(&root, "type", type, sizeof(type), &type_len) != QM_EOK)
        return -QM_EINVAL;
    if (qm_strcmp(type, "error") == 0)
        return parse_error(&root, log);
    if (comtrace_cbor_common(&root, type, sizeof(type), log->rx_sn, sizeof(log->rx_sn), &sid) !=
            QM_EOK ||
        qm_strcmp(log->rx_sn, log->sn) != 0 || sid != log->sid)
        return -QM_EINVAL;
    qm_memset(&message, 0, sizeof(message));
    if (qm_strcmp(type, "register_ack") == 0) {
        message.type = COMTRACE_LOG_MESSAGE_REGISTER_ACK;
        if (comtrace_cbor_uint(&root, "max_frame", &number) != QM_EOK || number == 0U ||
            number > log->max_frame)
            return -QM_EINVAL;
        message.max_frame = (uint16_t)number;
        if (comtrace_cbor_uint(&root, "heartbeat_interval", &number) != QM_EOK || number < 5U ||
            number > 300U)
            return -QM_EINVAL;
        message.heartbeat_interval = (uint16_t)number;
        log->heartbeat_interval = message.heartbeat_interval;
    } else if (qm_strcmp(type, "heartbeat_ack") == 0) {
        message.type = COMTRACE_LOG_MESSAGE_HEARTBEAT_ACK;
    } else {
        return -QM_ENOSYS;
    }
    if (log->on_message)
        log->on_message(&message, log->user);
    return QM_EOK;
}

int comtrace_log_init(comtrace_log_t **out, const comtrace_log_config_t *config)
{
    comtrace_log_t *log;
    size_t sn_len;
    size_t name_len = 0U;
    uint16_t queue_depth;
    uint16_t max_frame;
    int result;

    if (!out || !config || !config->sn || !config->send || config->sid == 0U ||
        config->transport > COMTRACE_TRANSPORT_UDP)
        return -QM_EINVAL;
    sn_len = qm_strlen(config->sn);
    if (sn_len == 0U || sn_len > COMTRACE_MAX_SN_LEN ||
        !comtrace_text_is_printable(config->sn, sn_len))
        return -QM_EINVAL;
    if (config->device_name) {
        name_len = qm_strlen(config->device_name);
        if (name_len > COMTRACE_MAX_NAME_LEN ||
            !comtrace_text_is_printable(config->device_name, name_len))
            return -QM_EINVAL;
    }
    max_frame = config->max_frame ? config->max_frame : CONFIG_QM_COMTRACE_MAX_FRAME;
    queue_depth =
        config->transport == COMTRACE_TRANSPORT_TCP
            ? (config->queue_depth ? config->queue_depth : CONFIG_QM_COMTRACE_QUEUE_DEPTH)
            : 0U;
    if (max_frame == 0U || max_frame > COMTRACE_MAX_FRAME ||
        queue_depth > COMTRACE_LOG_DEFAULT_QUEUE_DEPTH)
        return -QM_EINVAL;
    log = (comtrace_log_t *)qm_calloc(1U, sizeof(*log));
    if (!log)
        return -QM_ENOMEM;
    qm_memcpy(log->sn, config->sn, (uint32_t)(sn_len + 1U));
    if (name_len)
        qm_memcpy(log->device_name, config->device_name, (uint32_t)(name_len + 1U));
    log->sid = config->sid;
    log->max_frame = max_frame;
    log->heartbeat_interval =
        config->heartbeat_interval ? config->heartbeat_interval : COMTRACE_LOG_DEFAULT_HEARTBEAT_S;
    log->transport = config->transport;
    log->send = config->send;
    log->on_message = config->on_message;
    log->user = config->user;
    log->queue_depth = queue_depth;
    if (log->heartbeat_interval < 5U || log->heartbeat_interval > 300U) {
        qm_free(log);
        return -QM_EINVAL;
    }
    if (queue_depth) {
        log->queue = (frame_slot_t *)qm_calloc(queue_depth, sizeof(*log->queue));
        if (!log->queue) {
            qm_free(log);
            return -QM_ENOMEM;
        }
    }
    if (log->transport == COMTRACE_TRANSPORT_TCP) {
        result = comtrace_stream_init(&log->stream, max_frame);
        if (result != QM_EOK) {
            qm_free(log->queue);
            qm_free(log);
            return result;
        }
    }
    *out = log;
    return QM_EOK;
}

int comtrace_log_deinit(comtrace_log_t *log)
{
    uint16_t i;
    if (!log)
        return -QM_EINVAL;
    for (i = 0U; i < log->queue_depth; ++i)
        qm_free(log->queue[i].data);
    qm_free(log->queue);
    comtrace_stream_deinit(&log->stream);
    qm_free(log);
    return QM_EOK;
}

int comtrace_log_register(comtrace_log_t *log)
{
    uint8_t *frame;
    uint16_t length;
    int result;
    if (!log)
        return -QM_EINVAL;
    result = comtrace_frame_encode(
        log->sn, log->sid, log->max_frame, "register", log->device_name[0] ? 5U : 4U,
        log->device_name[0] ? body_register : NULL, log, &frame, &length);
    return result == QM_EOK ? send_frame(log, frame, length, 0U) : result;
}

int comtrace_log_heartbeat(comtrace_log_t *log)
{
    return log ? send_simple(log, "heartbeat") : -QM_EINVAL;
}

int comtrace_log_goodbye(comtrace_log_t *log)
{
    return log ? send_simple(log, "goodbye") : -QM_EINVAL;
}

static int write_log(comtrace_log_t *log, comtrace_log_direction_t direction,
                     comtrace_log_level_t level, const void *data, uint16_t data_len,
                     const char *text, const comtrace_log_tag_t *tags, uint8_t tag_count)
{
    log_body_t body;
    uint8_t *frame;
    uint16_t length;
    size_t text_len = text ? qm_strlen(text) : 0U;
    int result;

    if (!log || direction > COMTRACE_LOG_DIRECTION_EVENT || level > COMTRACE_LOG_LEVEL_ERROR ||
        (text && (text_len == 0U || text_len > log->max_frame)) || (data && data_len == 0U) ||
        ((data == NULL) == (text == NULL)) || !valid_tags(tags, tag_count) ||
        log->sequence == UINT32_MAX)
        return -QM_EINVAL;
    qm_memset(&body, 0, sizeof(body));
    body.direction = direction;
    body.level = level;
    body.data = data;
    body.data_len = data_len;
    body.text = text;
    body.tags = tags;
    body.tag_count = tag_count;
    body.sequence = ++log->sequence;
    body.dropped = log->pending_dropped;
    result = comtrace_frame_encode(log->sn, log->sid, log->max_frame, "log",
                                   8U + ((tag_count || body.dropped) ? 1U : 0U), body_log, &body,
                                   &frame, &length);
    if (result != QM_EOK) {
        --log->sequence;
        return result;
    }
    result = send_frame(log, frame, length, body.dropped);
    if (result == QM_EOK && body.dropped && log->pending_dropped >= body.dropped)
        log->pending_dropped -= body.dropped;
    return result;
}

int comtrace_log_write_data(comtrace_log_t *log, comtrace_log_direction_t direction,
                            comtrace_log_level_t level, const void *data, uint16_t data_len,
                            const comtrace_log_tag_t *tags, uint8_t tag_count)
{
    return write_log(log, direction, level, data, data_len, NULL, tags, tag_count);
}

int comtrace_log_write_text(comtrace_log_t *log, comtrace_log_direction_t direction,
                            comtrace_log_level_t level, const char *text,
                            const comtrace_log_tag_t *tags, uint8_t tag_count)
{
    return write_log(log, direction, level, NULL, 0U, text, tags, tag_count);
}

int comtrace_log_flush(comtrace_log_t *log)
{
    int result;
    if (!log)
        return -QM_EINVAL;
    while (log->queue_count) {
        frame_slot_t *slot = &log->queue[log->queue_head];
        result = log->send(slot->data, slot->length, log->user);
        if (result != QM_EOK && result != (int)slot->length)
            return result < 0 ? result : -QM_EIO;
        qm_free(slot->data);
        slot->data = NULL;
        slot->length = 0U;
        if (slot->reported_dropped && log->pending_dropped >= slot->reported_dropped)
            log->pending_dropped -= slot->reported_dropped;
        slot->reported_dropped = 0U;
        log->queue_head = (uint16_t)((log->queue_head + 1U) % log->queue_depth);
        --log->queue_count;
    }
    return QM_EOK;
}

int comtrace_log_feed_tcp(comtrace_log_t *log, const uint8_t *bytes, uint16_t length)
{
    if (!log || log->transport != COMTRACE_TRANSPORT_TCP)
        return -QM_EINVAL;
    return comtrace_stream_feed(&log->stream, bytes, length, parse_payload, log);
}

int comtrace_log_feed_udp(comtrace_log_t *log, const uint8_t *datagram, uint16_t length)
{
    if (!log || log->transport != COMTRACE_TRANSPORT_UDP)
        return -QM_EINVAL;
    return comtrace_datagram_feed(log->max_frame, datagram, length, parse_payload, log);
}

uint32_t comtrace_log_get_dropped_count(const comtrace_log_t *log)
{
    return log ? log->dropped : 0U;
}
