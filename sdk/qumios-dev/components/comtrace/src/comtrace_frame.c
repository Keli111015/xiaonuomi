/**
 * @file comtrace_frame.c
 * @brief Shared ComTrace length framing and CBOR helpers.
 */
#include "comtrace_frame.h"
#include "comtrace_internal.h"
#include "qm_kernel.h"
#include "qm_string.h"

static int cbor_status(CborError error)
{
    if (error == CborNoError)
        return QM_EOK;
    if (error == CborErrorOutOfMemory || error == CborErrorDataTooLarge ||
        error == CborErrorTooManyItems || error == CborErrorTooFewItems)
        return -QM_EFULL;
    return -QM_EINVAL;
}

/* Reduce queue retention without relying on platform-specific realloc semantics. */
static uint8_t *shrink_frame(uint8_t *buffer, size_t old_size, size_t new_size)
{
    uint8_t *shrunk;
    if (!buffer || new_size >= old_size)
        return buffer;
    shrunk = (uint8_t *)qm_malloc((unsigned int)new_size);
    if (!shrunk)
        return buffer;
    qm_memcpy(shrunk, buffer, (uint32_t)new_size);
    qm_free(buffer);
    return shrunk;
}

static int validate_value(const CborValue *value, unsigned depth)
{
    CborType type = cbor_value_get_type(value);
    CborValue iterator;

    if (depth > 8U)
        return -QM_EINVAL;
    if (type == CborTextStringType || type == CborByteStringType || type == CborBooleanType)
        return QM_EOK;
    if (type == CborIntegerType)
        return cbor_value_is_unsigned_integer(value) ? QM_EOK : -QM_EINVAL;
    if (type != CborArrayType && type != CborMapType)
        return -QM_ENOSYS;
    if (cbor_value_enter_container(value, &iterator) != CborNoError)
        return -QM_EINVAL;
    while (!cbor_value_at_end(&iterator)) {
        if (type == CborMapType) {
            if (!cbor_value_is_text_string(&iterator) ||
                validate_value(&iterator, depth + 1U) != QM_EOK ||
                cbor_value_advance(&iterator) != CborNoError)
                return -QM_EINVAL;
        }
        if (validate_value(&iterator, depth + 1U) != QM_EOK ||
            cbor_value_advance(&iterator) != CborNoError)
            return -QM_EINVAL;
    }
    return QM_EOK;
}

int comtrace_cbor_put_text(CborEncoder *map, const char *key, const char *value)
{
    int result;
    if (!map || !key || !value)
        return -QM_EINVAL;
    result = cbor_status(cbor_encode_text_stringz(map, key));
    return result == QM_EOK ? cbor_status(cbor_encode_text_stringz(map, value)) : result;
}

int comtrace_cbor_put_uint(CborEncoder *map, const char *key, uint64_t value)
{
    int result;
    if (!map || !key)
        return -QM_EINVAL;
    result = cbor_status(cbor_encode_text_stringz(map, key));
    return result == QM_EOK ? cbor_status(cbor_encode_uint(map, value)) : result;
}

int comtrace_frame_encode(const char *sn, uint32_t sid, uint16_t max_frame, const char *type,
                          size_t map_count, comtrace_encode_body_cb_t body, void *user,
                          uint8_t **frame, uint16_t *frame_len)
{
    CborEncoder encoder;
    CborEncoder map;
    uint8_t *buffer;
    size_t payload_len;
    int result;

    if (!sn || !sn[0] || sid == 0U || !type || !type[0] || max_frame == 0U ||
        max_frame > COMTRACE_MAX_FRAME || map_count < 4U || !frame || !frame_len)
        return -QM_EINVAL;
    buffer = (uint8_t *)qm_malloc((size_t)max_frame + COMTRACE_FRAME_HEADER_SIZE);
    if (!buffer)
        return -QM_ENOMEM;
    cbor_encoder_init(&encoder, buffer + COMTRACE_FRAME_HEADER_SIZE, max_frame, 0);
    result = cbor_status(cbor_encoder_create_map(&encoder, &map, map_count));
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(&map, "v", COMTRACE_PROTOCOL_VERSION);
    if (result == QM_EOK)
        result = comtrace_cbor_put_text(&map, "type", type);
    if (result == QM_EOK)
        result = comtrace_cbor_put_text(&map, "sn", sn);
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(&map, "sid", sid);
    if (result == QM_EOK && body)
        result = body(&map, user);
    if (result == QM_EOK)
        result = cbor_status(cbor_encoder_close_container_checked(&encoder, &map));
    if (result != QM_EOK) {
        qm_free(buffer);
        return result;
    }
    payload_len = cbor_encoder_get_buffer_size(&encoder, buffer + COMTRACE_FRAME_HEADER_SIZE);
    if (payload_len == 0U || payload_len > max_frame || payload_len > UINT16_MAX) {
        qm_free(buffer);
        return -QM_EFULL;
    }
    buffer = shrink_frame(buffer, (size_t)max_frame + COMTRACE_FRAME_HEADER_SIZE,
                          payload_len + COMTRACE_FRAME_HEADER_SIZE);
    buffer[0] = (uint8_t)(payload_len >> 8);
    buffer[1] = (uint8_t)payload_len;
    *frame = buffer;
    *frame_len = (uint16_t)(payload_len + COMTRACE_FRAME_HEADER_SIZE);
    return QM_EOK;
}

static int body_error(CborEncoder *map, void *user)
{
    return comtrace_cbor_put_uint(map, "code", *(const uint32_t *)user);
}

int comtrace_frame_encode_error(uint16_t max_frame, uint32_t code, uint8_t **frame,
                                uint16_t *frame_len)
{
    CborEncoder encoder;
    CborEncoder map;
    uint8_t *buffer;
    size_t payload_len;
    int result;

    if (max_frame == 0U || max_frame > COMTRACE_MAX_FRAME || !frame || !frame_len)
        return -QM_EINVAL;
    buffer = (uint8_t *)qm_malloc((size_t)max_frame + COMTRACE_FRAME_HEADER_SIZE);
    if (!buffer)
        return -QM_ENOMEM;
    cbor_encoder_init(&encoder, buffer + COMTRACE_FRAME_HEADER_SIZE, max_frame, 0);
    result = cbor_status(cbor_encoder_create_map(&encoder, &map, 3U));
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(&map, "v", COMTRACE_PROTOCOL_VERSION);
    if (result == QM_EOK)
        result = comtrace_cbor_put_text(&map, "type", "error");
    if (result == QM_EOK)
        result = body_error(&map, &code);
    if (result == QM_EOK)
        result = cbor_status(cbor_encoder_close_container_checked(&encoder, &map));
    if (result != QM_EOK) {
        qm_free(buffer);
        return result;
    }
    payload_len = cbor_encoder_get_buffer_size(&encoder, buffer + COMTRACE_FRAME_HEADER_SIZE);
    if (payload_len == 0U || payload_len > max_frame) {
        qm_free(buffer);
        return -QM_EFULL;
    }
    buffer = shrink_frame(buffer, (size_t)max_frame + COMTRACE_FRAME_HEADER_SIZE,
                          payload_len + COMTRACE_FRAME_HEADER_SIZE);
    buffer[0] = (uint8_t)(payload_len >> 8);
    buffer[1] = (uint8_t)payload_len;
    *frame = buffer;
    *frame_len = (uint16_t)(payload_len + COMTRACE_FRAME_HEADER_SIZE);
    return QM_EOK;
}

int comtrace_frame_send(comtrace_send_cb_t send, void *user, uint8_t *frame, uint16_t frame_len)
{
    int result;
    if (!send || !frame || frame_len <= COMTRACE_FRAME_HEADER_SIZE)
        return -QM_EINVAL;
    result = send(frame, frame_len, user);
    qm_free(frame);
    if (result == QM_EOK || result == (int)frame_len)
        return QM_EOK;
    return result < 0 ? result : -QM_EIO;
}

int comtrace_stream_init(comtrace_stream_t *stream, uint16_t max_frame)
{
    if (!stream || max_frame == 0U || max_frame > COMTRACE_MAX_FRAME)
        return -QM_EINVAL;
    qm_memset(stream, 0, sizeof(*stream));
    stream->data = (uint8_t *)qm_malloc((size_t)max_frame + COMTRACE_FRAME_HEADER_SIZE);
    if (!stream->data)
        return -QM_ENOMEM;
    stream->max_frame = max_frame;
    return QM_EOK;
}

void comtrace_stream_deinit(comtrace_stream_t *stream)
{
    if (!stream)
        return;
    qm_free(stream->data);
    qm_memset(stream, 0, sizeof(*stream));
}

void comtrace_stream_reset(comtrace_stream_t *stream)
{
    if (stream)
        stream->length = 0U;
}

static int dispatch_frames(comtrace_stream_t *stream, comtrace_payload_cb_t callback, void *user)
{
    uint16_t payload_len;
    int result;

    while (stream->length >= COMTRACE_FRAME_HEADER_SIZE) {
        payload_len = (uint16_t)(((uint16_t)stream->data[0] << 8) | stream->data[1]);
        if (payload_len == 0U || payload_len > stream->max_frame) {
            stream->length = 0U;
            return -QM_EINVAL;
        }
        if (stream->length < (uint16_t)(payload_len + COMTRACE_FRAME_HEADER_SIZE))
            return QM_EOK;
        result = callback(stream->data + COMTRACE_FRAME_HEADER_SIZE, payload_len, user);
        qm_memmove(stream->data, stream->data + payload_len + COMTRACE_FRAME_HEADER_SIZE,
                   (uint32_t)(stream->length - payload_len - COMTRACE_FRAME_HEADER_SIZE));
        stream->length = (uint16_t)(stream->length - payload_len - COMTRACE_FRAME_HEADER_SIZE);
        if (result != QM_EOK)
            return result;
    }
    return QM_EOK;
}

int comtrace_stream_feed(comtrace_stream_t *stream, const uint8_t *bytes, uint16_t length,
                         comtrace_payload_cb_t callback, void *user)
{
    uint16_t capacity;
    uint16_t copied;
    int result;

    if (!stream || !stream->data || (!bytes && length) || !callback)
        return -QM_EINVAL;
    capacity = (uint16_t)(stream->max_frame + COMTRACE_FRAME_HEADER_SIZE);
    while (length) {
        copied = (uint16_t)(capacity - stream->length);
        if (copied > length)
            copied = length;
        if (copied == 0U) {
            stream->length = 0U;
            return -QM_EFULL;
        }
        qm_memcpy(stream->data + stream->length, bytes, copied);
        stream->length = (uint16_t)(stream->length + copied);
        bytes += copied;
        length = (uint16_t)(length - copied);
        result = dispatch_frames(stream, callback, user);
        if (result != QM_EOK)
            return result;
    }
    return QM_EOK;
}

int comtrace_datagram_feed(uint16_t max_frame, const uint8_t *datagram, uint16_t length,
                           comtrace_payload_cb_t callback, void *user)
{
    uint16_t payload_len;
    if (!datagram || length <= COMTRACE_FRAME_HEADER_SIZE || !callback || max_frame == 0U)
        return -QM_EINVAL;
    payload_len = (uint16_t)(((uint16_t)datagram[0] << 8) | datagram[1]);
    if (payload_len == 0U || payload_len > max_frame ||
        length != (uint16_t)(payload_len + COMTRACE_FRAME_HEADER_SIZE))
        return -QM_EINVAL;
    return callback(datagram + COMTRACE_FRAME_HEADER_SIZE, payload_len, user);
}

int comtrace_cbor_root(const uint8_t *payload, uint16_t length, CborParser *parser, CborValue *root)
{
    CborValue end;
    size_t item_count;
    if (!payload || !length || !parser || !root ||
        cbor_parser_init(payload, length, 0, parser, root) != CborNoError ||
        cbor_value_validate_basic(root) != CborNoError || !cbor_value_is_map(root) ||
        cbor_value_get_map_length(root, &item_count) != CborNoError || item_count > 32U)
        return -QM_EINVAL;
    end = *root;
    if (cbor_value_advance(&end) != CborNoError ||
        cbor_value_get_next_byte(&end) != payload + length)
        return -QM_EINVAL;
    return validate_value(root, 0U) == QM_EOK ? QM_EOK : -QM_EINVAL;
}

int comtrace_cbor_text(CborValue *map, const char *key, char *buffer, size_t capacity,
                       size_t *length)
{
    CborValue value;
    CborValue next;
    size_t actual;
    size_t copied;
    if (!map || !key || !buffer || capacity == 0U || !length ||
        cbor_value_map_find_value(map, key, &value) != CborNoError ||
        !cbor_value_is_valid(&value) || !cbor_value_is_text_string(&value) ||
        !cbor_value_is_length_known(&value) ||
        cbor_value_get_string_length(&value, &actual) != CborNoError || actual >= capacity)
        return -QM_EINVAL;
    next = value;
    copied = capacity;
    if (cbor_value_copy_text_string(&value, buffer, &copied, &next) != CborNoError ||
        copied != actual)
        return -QM_EINVAL;
    buffer[copied] = '\0';
    *length = copied;
    return QM_EOK;
}

int comtrace_cbor_uint(CborValue *map, const char *key, uint64_t *number)
{
    CborValue value;
    if (!map || !key || !number || cbor_value_map_find_value(map, key, &value) != CborNoError ||
        !cbor_value_is_valid(&value) || !cbor_value_is_unsigned_integer(&value) ||
        cbor_value_get_uint64(&value, number) != CborNoError)
        return -QM_EINVAL;
    return QM_EOK;
}

int comtrace_cbor_bytes(CborValue *map, const char *key, uint8_t *buffer, size_t capacity,
                        size_t *length)
{
    CborValue value;
    CborValue next;
    size_t actual;
    size_t copied;
    if (!map || !key || !buffer || !length ||
        cbor_value_map_find_value(map, key, &value) != CborNoError ||
        !cbor_value_is_valid(&value) || !cbor_value_is_byte_string(&value) ||
        !cbor_value_is_length_known(&value) ||
        cbor_value_get_string_length(&value, &actual) != CborNoError || actual > capacity)
        return -QM_EINVAL;
    next = value;
    copied = capacity;
    if (cbor_value_copy_byte_string(&value, buffer, &copied, &next) != CborNoError ||
        copied != actual)
        return -QM_EINVAL;
    *length = copied;
    return QM_EOK;
}

int comtrace_cbor_common(CborValue *root, char *type, size_t type_capacity, char *sn,
                         size_t sn_capacity, uint32_t *sid)
{
    size_t type_len;
    size_t sn_len;
    uint64_t version;
    uint64_t session;
    if (!root || !type || !sn || !sid || comtrace_cbor_uint(root, "v", &version) != QM_EOK ||
        version != COMTRACE_PROTOCOL_VERSION ||
        comtrace_cbor_text(root, "type", type, type_capacity, &type_len) != QM_EOK ||
        type_len == 0U || comtrace_cbor_text(root, "sn", sn, sn_capacity, &sn_len) != QM_EOK ||
        sn_len == 0U || !comtrace_text_is_printable(sn, sn_len) ||
        comtrace_cbor_uint(root, "sid", &session) != QM_EOK || session == 0U ||
        session > UINT32_MAX)
        return -QM_EINVAL;
    *sid = (uint32_t)session;
    return QM_EOK;
}
