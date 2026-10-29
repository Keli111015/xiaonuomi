/**
 * @file comtrace_types.h
 * @brief Types shared by the CELP/1 and CEUP/1 capabilities.
 */
#ifndef COMTRACE_TYPES_H
#define COMTRACE_TYPES_H

#include "qm_errno.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMTRACE_PROTOCOL_VERSION 1U
#define COMTRACE_MAX_FRAME 1200U
#define COMTRACE_FRAME_HEADER_SIZE 2U
#define COMTRACE_MAX_SN_LEN 64U
#define COMTRACE_MAX_NAME_LEN 64U

/** @brief Transport framing used by the CELP/1 log capability. */
typedef enum {
    COMTRACE_TRANSPORT_TCP = 0, /**< Length-prefixed byte stream with retry queueing. */
    COMTRACE_TRANSPORT_UDP = 1, /**< One complete length-prefixed frame per datagram. */
} comtrace_transport_t;

/**
 * @brief Send one complete ComTrace wire frame.
 *
 * The frame is borrowed and remains valid only during the callback.
 */
typedef int (*comtrace_send_cb_t)(const uint8_t *frame, uint16_t frame_len, void *user);

#ifdef __cplusplus
}
#endif
#endif
