#ifndef SERIAL_MCU_OTA_H
#define SERIAL_MCU_OTA_H

/**
 * @file serial_mcu_ota.h
 * @brief MCU OTA sender API over the Qumi serial component.
 */

#include <stdint.h>
#include "qm_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup serial_mcu_ota Serial MCU OTA
 * @brief MCU OTA sender using commands 0x35 to 0x3B.
 * @{
 */

/** @brief Operation completed successfully. */
#define SERIAL_MCU_OTA_OK                   0
/** @brief Invalid argument or invalid configuration. */
#define SERIAL_MCU_OTA_ERR_PARAM           (-1)
/** @brief Current OTA state does not allow the requested operation. */
#define SERIAL_MCU_OTA_ERR_STATE           (-2)
/** @brief Serial frame checksum is invalid. */
#define SERIAL_MCU_OTA_ERR_CHECKSUM        (-3)
/** @brief Serial frame format is invalid. */
#define SERIAL_MCU_OTA_ERR_FRAME           (-4)
/** @brief Failed to send a serial frame through Qumi serial. */
#define SERIAL_MCU_OTA_ERR_SEND            (-5)
/** @brief Failed to read firmware data from the user callback. */
#define SERIAL_MCU_OTA_ERR_READ            (-6)
/** @brief ACK wait timed out after all retries. */
#define SERIAL_MCU_OTA_ERR_TIMEOUT         (-7)
/** @brief Requested check type or feature is unsupported. */
#define SERIAL_MCU_OTA_ERR_UNSUPPORTED     (-8)
/** @brief Received ACK is invalid or reports a terminal error. */
#define SERIAL_MCU_OTA_ERR_ACK             (-9)

/** @brief CRC32 check value length in bytes. */
#define SERIAL_MCU_OTA_CRC32_VALUE_LEN     4U
/** @brief MD5 check value length in bytes. */
#define SERIAL_MCU_OTA_MD5_VALUE_LEN       16U
/** @brief Maximum length of the OTA check value field. */
#define SERIAL_MCU_OTA_CHECK_VALUE_LEN     SERIAL_MCU_OTA_MD5_VALUE_LEN
/** @brief Default OTA data payload size in bytes. */
#define SERIAL_MCU_OTA_DEFAULT_CHUNK_SIZE  512U
/** @brief Maximum supported OTA data payload size in bytes. */
#define SERIAL_MCU_OTA_MAX_CHUNK_SIZE      1024U

/** @brief Default ACK timeout in milliseconds. */
#define SERIAL_MCU_OTA_DEFAULT_TIMEOUT_MS  1000U
/** @brief Default retry count for ACK timeout retransmission. */
#define SERIAL_MCU_OTA_DEFAULT_RETRY_MAX   3U

/**
 * @brief Firmware integrity check type.
 */
typedef enum {
    SERIAL_MCU_OTA_CHECK_DEFAULT = 0, /**< Use the default check type, currently CRC32. */
    SERIAL_MCU_OTA_CHECK_CRC32 = 1,   /**< CRC32 check; 4 bytes of check_value are sent in OTA_BEGIN. */
    SERIAL_MCU_OTA_CHECK_MD5 = 2,     /**< MD5 check; 16 bytes of check_value are sent in OTA_BEGIN. */
} serial_mcu_ota_check_t;

/**
 * @brief MCU asynchronous OTA notify reason.
 */
typedef enum {
    SERIAL_MCU_OTA_NOTIFY_REASON_NONE = 0,       /**< No specific reason. */
    SERIAL_MCU_OTA_NOTIFY_REASON_READY = 1,      /**< MCU is ready to receive OTA_BEGIN. */
    SERIAL_MCU_OTA_NOTIFY_REASON_ERROR = 2,      /**< MCU reported an OTA error; see notify_error_code. */
} serial_mcu_ota_notify_reason_t;

/**
 * @brief OTA sender runtime state.
 */
typedef enum {
    SERIAL_MCU_OTA_STATE_IDLE = 0, /**< No OTA transfer is running. */
    SERIAL_MCU_OTA_STATE_RUNNING,  /**< OTA transfer is running or waiting for ACK. */
    SERIAL_MCU_OTA_STATE_DONE,     /**< OTA transfer finished successfully. */
    SERIAL_MCU_OTA_STATE_ABORTED,  /**< OTA transfer was aborted and ACKed by MCU. */
    SERIAL_MCU_OTA_STATE_FAILED,   /**< OTA transfer failed with an error. */
} serial_mcu_ota_state_t;

/**
 * @brief OTA event passed to the notify callback.
 */
typedef enum {
    SERIAL_MCU_OTA_EVENT_START = 0, /**< OTA_BEGIN has been sent. */
    SERIAL_MCU_OTA_EVENT_PROGRESS,  /**< A data ACK was accepted and progress changed. */
    SERIAL_MCU_OTA_EVENT_RETRY,     /**< Current frame was retransmitted after ACK timeout. */
    SERIAL_MCU_OTA_EVENT_FINISH,    /**< OTA_END was ACKed successfully. */
    SERIAL_MCU_OTA_EVENT_ABORT,     /**< OTA_ABORT was ACKed successfully. */
    SERIAL_MCU_OTA_EVENT_ERROR,     /**< OTA transfer failed. */
    SERIAL_MCU_OTA_EVENT_ENTER,     /**< OTA_ENTER has been sent. */
    SERIAL_MCU_OTA_EVENT_MCU_NOTIFY, /**< MCU sent an asynchronous OTA_NOTIFY frame. */
} serial_mcu_ota_event_t;

/**
 * @brief Snapshot of current OTA progress and last command result.
 */
typedef struct {
    uint32_t total_size;     /**< Total firmware size in bytes. */
    uint32_t offset;         /**< Next firmware offset expected to be sent. */
    uint16_t seq;            /**< Next OTA_DATA sequence number expected to be sent. */
    uint8_t last_cmd;        /**< Last OTA command sent, such as 0x35 OTA_ENTER. */
    uint8_t last_ack_code;   /**< Last ACK status code received from MCU. */
    uint8_t retry_count;     /**< Retry count used for the current waiting frame. */
    int32_t error_code;      /**< Last local error code when state is FAILED or ABORTED. */
    serial_mcu_ota_notify_reason_t notify_reason; /**< Last MCU OTA_NOTIFY reason. */
    uint16_t notify_next_seq;      /**< Next sequence number carried by OTA_NOTIFY. */
    uint32_t notify_next_offset;   /**< Next firmware offset carried by OTA_NOTIFY. */
    uint32_t notify_error_code;    /**< MCU-defined error code carried by OTA_NOTIFY. */
} serial_mcu_ota_info_t;

/**
 * @brief Read a firmware chunk.
 *
 * The callback must fill exactly @p len bytes starting at @p offset.
 *
 * @param[in] offset Firmware offset to read from.
 * @param[out] data Destination buffer provided by the OTA component.
 * @param[in] len Number of bytes to read.
 * @param[in] user User pointer from serial_mcu_ota_config_t.
 * @return 0 on success, non-zero on read failure.
 */
typedef int32_t (*serial_mcu_ota_read_chunk_fn)(uint32_t offset,
                                                uint8_t *data,
                                                uint16_t len,
                                                void *user);

/**
 * @brief Notify OTA state changes and progress.
 *
 * @param[in] event Event type.
 * @param[in] info Current OTA information snapshot. Valid only during the callback.
 * @param[in] user User pointer from serial_mcu_ota_config_t.
 */
typedef void (*serial_mcu_ota_notify_fn)(serial_mcu_ota_event_t event,
                                         const serial_mcu_ota_info_t *info,
                                         void *user);

/**
 * @brief OTA sender configuration.
 */
typedef struct {
    uint32_t total_size;                  /**< Total firmware size in bytes. Must be greater than 0. */
    uint16_t chunk_size;                  /**< OTA_DATA payload size. 0 uses CONFIG/default 512. */
    serial_mcu_ota_check_t check_type;    /**< Firmware check type. 0 uses CRC32. */
    uint8_t flags;                        /**< OTA_ENTER and OTA_BEGIN flags field. Currently set to 0. */
    uint8_t check_value[SERIAL_MCU_OTA_CHECK_VALUE_LEN]; /**< Firmware check value sent in OTA_BEGIN. CRC32 uses 4 bytes; MD5 uses 16 bytes. */
    uint32_t timeout_ms;                  /**< ACK timeout in milliseconds. 0 uses CONFIG/default 1000. */
    uint8_t retry_max;                    /**< Maximum ACK timeout retry count. 0 uses CONFIG/default 3. */
    qm_uart_dev_t *uart;                  /**< Initialized Qumi UART device used to send OTA frames. */
    serial_mcu_ota_read_chunk_fn read_chunk; /**< Firmware chunk read callback. Must not be NULL. */
    serial_mcu_ota_notify_fn notify;      /**< Optional event notify callback. May be NULL. */
    void *user;                           /**< User pointer passed to callbacks. */
} serial_mcu_ota_config_t;

/**
 * @brief Start a new MCU OTA transfer.
 *
 * This function sends OTA_ENTER first, waits for OTA notify READY, then sends
 * OTA_BEGIN through the configured Qumi UART device. The caller must provide
 * serial_mcu_ota_config_t::check_type and serial_mcu_ota_config_t::check_value
 * before calling this function.
 *
 * @param[in] config OTA sender configuration.
 * @return SERIAL_MCU_OTA_OK on success, otherwise a negative SERIAL_MCU_OTA_ERR_* code.
 */
int32_t serial_mcu_ota_start(const serial_mcu_ota_config_t *config);

/**
 * @brief Handle a complete MCU OTA serial frame from MCU.
 *
 * Pass only a complete raw serial frame using the protocol format
 * HEAD | VER | CMD | LEN | PAYLOAD | CHKSUM. This function accepts OTA_ACK
 * and OTA_NOTIFY frames.
 *
 * @param[in] frame Complete raw serial frame.
 * @param[in] frame_len Length of @p frame in bytes.
 * @return SERIAL_MCU_OTA_OK on success, otherwise a negative SERIAL_MCU_OTA_ERR_* code.
 */
int32_t serial_mcu_ota_on_frame(const uint8_t *frame, uint16_t frame_len);

/**
 * @brief Process ACK timeout and retransmission.
 *
 * Call this function periodically from the main loop or serial task.
 *
 * @return SERIAL_MCU_OTA_OK on success, otherwise a negative SERIAL_MCU_OTA_ERR_* code.
 */
int32_t serial_mcu_ota_process(void);

/**
 * @brief Abort the current OTA transfer.
 *
 * Sends OTA_ABORT to MCU and waits for OTA_ACK through serial_mcu_ota_on_frame().
 *
 * @param[in] reason Application-defined abort reason stored in serial_mcu_ota_info_t::error_code.
 * @return SERIAL_MCU_OTA_OK on success, otherwise a negative SERIAL_MCU_OTA_ERR_* code.
 */
int32_t serial_mcu_ota_abort(int32_t reason);

/**
 * @brief Get current OTA state.
 *
 * @return Current OTA sender state.
 */
serial_mcu_ota_state_t serial_mcu_ota_state(void);

/**
 * @brief Get current OTA information snapshot.
 *
 * @param[out] info Destination info structure. No action is taken if @p info is NULL.
 */
void serial_mcu_ota_get_info(serial_mcu_ota_info_t *info);

/** @} */

#ifdef __cplusplus
}
#endif

#endif
