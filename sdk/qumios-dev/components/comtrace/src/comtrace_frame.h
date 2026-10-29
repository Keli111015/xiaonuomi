/**
 * @file comtrace_frame.h
 * @brief Internal ComTrace length framing and CBOR helpers.
 */
#ifndef COMTRACE_FRAME_H
#define COMTRACE_FRAME_H

#include "cbor.h"
#include "comtrace_types.h"

typedef struct {
    uint8_t *data;
    uint16_t length;
    uint16_t max_frame;
} comtrace_stream_t;

typedef int (*comtrace_encode_body_cb_t)(CborEncoder *map, void *user);
typedef int (*comtrace_payload_cb_t)(const uint8_t *payload, uint16_t length, void *user);

int comtrace_frame_encode(const char *sn, uint32_t sid, uint16_t max_frame, const char *type,
                          size_t map_count, comtrace_encode_body_cb_t body, void *user,
                          uint8_t **frame, uint16_t *frame_len);
int comtrace_frame_encode_error(uint16_t max_frame, uint32_t code, uint8_t **frame,
                                uint16_t *frame_len);
int comtrace_frame_send(comtrace_send_cb_t send, void *user, uint8_t *frame, uint16_t frame_len);

int comtrace_stream_init(comtrace_stream_t *stream, uint16_t max_frame);
void comtrace_stream_reset(comtrace_stream_t *stream);
void comtrace_stream_deinit(comtrace_stream_t *stream);
int comtrace_stream_feed(comtrace_stream_t *stream, const uint8_t *bytes, uint16_t length,
                         comtrace_payload_cb_t callback, void *user);
int comtrace_datagram_feed(uint16_t max_frame, const uint8_t *datagram, uint16_t length,
                           comtrace_payload_cb_t callback, void *user);

int comtrace_cbor_root(const uint8_t *payload, uint16_t length, CborParser *parser,
                       CborValue *root);
int comtrace_cbor_text(CborValue *map, const char *key, char *buffer, size_t capacity,
                       size_t *length);
int comtrace_cbor_uint(CborValue *map, const char *key, uint64_t *value);
int comtrace_cbor_bytes(CborValue *map, const char *key, uint8_t *buffer, size_t capacity,
                        size_t *length);
int comtrace_cbor_common(CborValue *root, char *type, size_t type_capacity, char *sn,
                         size_t sn_capacity, uint32_t *sid);
int comtrace_cbor_put_text(CborEncoder *map, const char *key, const char *value);
int comtrace_cbor_put_uint(CborEncoder *map, const char *key, uint64_t value);

#endif
