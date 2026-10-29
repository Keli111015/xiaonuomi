/**
 * @file comtrace_log.h
 * @brief CELP/1 edge log capability.
 */
#ifndef COMTRACE_LOG_H
#define COMTRACE_LOG_H

#include "comtrace_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMTRACE_LOG_DEFAULT_HEARTBEAT_S 30U
#define COMTRACE_LOG_DEFAULT_QUEUE_DEPTH 256U
#define COMTRACE_LOG_MAX_TAGS 8U

typedef struct comtrace_log comtrace_log_t;

/** @brief CELP/1 log severity. */
typedef enum {
    COMTRACE_LOG_LEVEL_DEBUG = 0,
    COMTRACE_LOG_LEVEL_INFO,
    COMTRACE_LOG_LEVEL_WARN,
    COMTRACE_LOG_LEVEL_ERROR,
} comtrace_log_level_t;

/** @brief Direction of the logged data relative to the device. */
typedef enum {
    COMTRACE_LOG_DIRECTION_RX = 0,
    COMTRACE_LOG_DIRECTION_TX,
    COMTRACE_LOG_DIRECTION_EVENT,
} comtrace_log_direction_t;

/** @brief CELP/1 server message type accepted by the device. */
typedef enum {
    COMTRACE_LOG_MESSAGE_REGISTER_ACK = 0,
    COMTRACE_LOG_MESSAGE_HEARTBEAT_ACK,
    COMTRACE_LOG_MESSAGE_ERROR,
} comtrace_log_message_type_t;

/** @brief One CELP/1 log tag. Strings are borrowed by write calls. */
typedef struct {
    const char *key;
    const char *value;
} comtrace_log_tag_t;

/** @brief Decoded CELP/1 server acknowledgement or error. */
typedef struct {
    comtrace_log_message_type_t type;
    uint16_t max_frame;
    uint16_t heartbeat_interval;
    uint32_t error_code;
} comtrace_log_message_t;

typedef void (*comtrace_log_message_cb_t)(const comtrace_log_message_t *message, void *user);

/** @brief CELP/1 log instance configuration copied by comtrace_log_init(). */
typedef struct {
    const char *sn;          /**< Required; copied by init. */
    const char *device_name; /**< Optional; copied by init. */
    uint32_t sid; /**< Required non-zero session identifier. */
    uint16_t max_frame;          /**< Zero uses the configured default. */
    uint16_t queue_depth;        /**< Zero uses the configured default. */
    uint16_t heartbeat_interval; /**< Zero uses the protocol default. */
    comtrace_transport_t transport;       /**< Framing and queueing policy. */
    comtrace_send_cb_t send;              /**< Required transport callback. */
    comtrace_log_message_cb_t on_message; /**< Optional receive callback. */
    void *user;                           /**< Passed unchanged to callbacks. */
} comtrace_log_config_t;

/** @brief Allocate and initialize a CELP/1 log instance. */
int comtrace_log_init(comtrace_log_t **out, const comtrace_log_config_t *config);
/** @brief Release a log instance and all queued frames. */
int comtrace_log_deinit(comtrace_log_t *log);
/** @brief Send a CELP/1 register frame. */
int comtrace_log_register(comtrace_log_t *log);
/** @brief Send a CELP/1 heartbeat frame. */
int comtrace_log_heartbeat(comtrace_log_t *log);
/** @brief Send a CELP/1 goodbye frame. */
int comtrace_log_goodbye(comtrace_log_t *log);
/** @brief Encode and send one binary log record. */
int comtrace_log_write_data(comtrace_log_t *log, comtrace_log_direction_t direction,
                            comtrace_log_level_t level, const void *data, uint16_t data_len,
                            const comtrace_log_tag_t *tags, uint8_t tag_count);
/** @brief Encode and send one text log record. */
int comtrace_log_write_text(comtrace_log_t *log, comtrace_log_direction_t direction,
                            comtrace_log_level_t level, const char *text,
                            const comtrace_log_tag_t *tags, uint8_t tag_count);
/** @brief Retry all locally queued TCP frames in FIFO order. */
int comtrace_log_flush(comtrace_log_t *log);
/** @brief Feed received TCP stream bytes into the CELP/1 decoder. */
int comtrace_log_feed_tcp(comtrace_log_t *log, const uint8_t *bytes, uint16_t length);
/** @brief Feed one received UDP datagram into the CELP/1 decoder. */
int comtrace_log_feed_udp(comtrace_log_t *log, const uint8_t *datagram, uint16_t length);
/** @brief Return the number of frames discarded because the local queue was full. */
uint32_t comtrace_log_get_dropped_count(const comtrace_log_t *log);

#ifdef __cplusplus
}
#endif
#endif
