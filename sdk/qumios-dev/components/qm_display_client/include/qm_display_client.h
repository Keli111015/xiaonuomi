#ifndef QM_DISPLAY_CLIENT_H
#define QM_DISPLAY_CLIENT_H

#include "qm_errno.h"
#include "qm_types.h"
#include "qm_display_client_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup qm_display_client Remote display controller client
 *  @brief Master-side client for the QUMI display controller protocol V1.0.
 *  @{
 */

/** @brief Opaque display client handle. */
typedef struct qm_display_client *qm_display_client_handle_t;

/** @brief Screen selection mask. */
typedef enum {
    QM_DISPLAY_SCREEN_1 = 0x01,   /**< Select logical screen 1. */
    QM_DISPLAY_SCREEN_2 = 0x02,   /**< Select logical screen 2. */
    QM_DISPLAY_SCREEN_BOTH = 0x03, /**< Select both logical screens atomically. */
} qm_display_screen_mask_t;

/** @brief Screen display-output power state. */
typedef enum {
    QM_DISPLAY_POWER_OFF = 0x00, /**< Disable the selected display output. */
    QM_DISPLAY_POWER_ON = 0x01,  /**< Enable the selected display output. */
} qm_display_power_state_t;

/** @brief Remote protocol status. */
typedef enum {
    QM_DISPLAY_STATUS_OK = 0x00,                  /**< Request completed successfully. */
    QM_DISPLAY_STATUS_UNKNOWN_CMD = 0x01,         /**< Command identifier is unknown. */
    QM_DISPLAY_STATUS_UNSUPPORTED_VERSION = 0x02, /**< Protocol version is unsupported. */
    QM_DISPLAY_STATUS_INVALID_LENGTH = 0x03,      /**< Payload length is invalid. */
    QM_DISPLAY_STATUS_INVALID_PARAM = 0x04,       /**< One or more parameters are invalid. */
    QM_DISPLAY_STATUS_BUSY = 0x05,                /**< Controller cannot accept the request now. */
    QM_DISPLAY_STATUS_ASSET_NOT_FOUND = 0x10,     /**< Referenced asset does not exist. */
    QM_DISPLAY_STATUS_VERIFY_FAILED = 0x11,       /**< Asset verification failed. */
    QM_DISPLAY_STATUS_OFFSET_ERROR = 0x12,        /**< Fragment offset is not the expected offset. */
    QM_DISPLAY_STATUS_STORAGE_ERROR = 0x13,       /**< Asset storage operation failed. */
    QM_DISPLAY_STATUS_DISPLAY_FAILED = 0x20,      /**< Display or screen setting failed. */
    QM_DISPLAY_STATUS_INTERNAL_ERROR = 0x30,      /**< Controller encountered an internal error. */
} qm_display_status_t;

/** @brief Remote request result. Valid when an API returns QM_EOK. */
typedef struct {
    qm_display_status_t status; /**< Remote request status. */
} qm_display_result_t;

/** @brief Deprecated compatibility state; V1.0 does not carry this field. */
typedef enum {
    QM_DISPLAY_FW_STATE_UNKNOWN = 0xFF,
    QM_DISPLAY_FW_APPLICATION = 0x00,
    QM_DISPLAY_FW_BOOTLOADER = 0x01,
    QM_DISPLAY_FW_UPGRADE_PENDING = 0x02,
} qm_display_fw_state_t;

/** @brief Remote firmware information. */
typedef struct {
    uint8_t major; /**< Firmware major version. */
    uint8_t minor; /**< Firmware minor version. */
    uint8_t patch; /**< Firmware patch version. */
    qm_display_fw_state_t state; /**< Deprecated; always UNKNOWN in V1.0. */
} qm_display_fw_info_t;

/** @brief Source used by a DISPLAY item. */
typedef enum {
    QM_DISPLAY_SOURCE_PERSISTENT = 0x00, /**< Use an asset from persistent storage. */
    QM_DISPLAY_SOURCE_RAM_READY = 0x01,  /**< Use the screen's prepared RAM asset. */
} qm_display_object_source_t;

/** @brief One target in a DISPLAY request. */
typedef struct {
    uint8_t screen_id;                  /**< Logical screen identifier: 1 or 2. */
    qm_display_object_source_t source;  /**< Storage source from which to display. */
    uint16_t asset_id;                  /**< Asset identifier expected at the source. */
} qm_display_item_t;

/** @brief Asset transfer mode. */
typedef enum {
    QM_DISPLAY_ASSET_RAM_DEFERRED = 0x00,     /**< Prepare in RAM for a later DISPLAY. */
    QM_DISPLAY_ASSET_RAM_AUTO_PRESENT = 0x01, /**< Present from RAM when transfer ends. */
    QM_DISPLAY_ASSET_FLASH_UPDATE = 0x02,     /**< Commit the asset to persistent storage. */
} qm_display_asset_mode_t;

/**
 * @brief Write bytes to the ordered byte-stream transport.
 *
 * @param[in] user User context from qm_display_client_config_t::user.
 * @param[in] data Bytes to write. The buffer is borrowed for this call.
 * @param[in] length Number of bytes requested.
 * @param[in] timeout_ms Maximum time to wait in milliseconds.
 * @return Positive bytes written, 0 on timeout, or a negative transport error.
 */
typedef int32_t (*qm_display_write_fn)(void *user, const uint8_t *data,
                                       uint32_t length, uint32_t timeout_ms);

/**
 * @brief Read bytes from the ordered byte-stream transport.
 *
 * @param[in] user User context from qm_display_client_config_t::user.
 * @param[out] data Destination buffer borrowed for this call.
 * @param[in] length Maximum number of bytes to read.
 * @param[in] timeout_ms Maximum time to wait in milliseconds.
 * @return Positive bytes read, 0 on timeout, or a negative transport error.
 */
typedef int32_t (*qm_display_read_fn)(void *user, uint8_t *data,
                                      uint32_t length, uint32_t timeout_ms);

/**
 * @brief Read one asset fragment for qm_display_client_upload_asset().
 *
 * The buffer is borrowed and valid only during the callback. On QM_EOK,
 * out_length must be in the range 1..max_length.
 *
 * @param[in] user User context from qm_display_asset_upload_t::read_user.
 * @param[in] offset Asset byte offset to read.
 * @param[out] buffer Destination buffer borrowed for this call.
 * @param[in] max_length Maximum number of bytes that may be returned.
 * @param[out] out_length Number of bytes written to @p buffer.
 * @return QM_EOK on success, or a negative QUMI error code.
 */
typedef qm_err_t (*qm_display_asset_read_fn)(void *user, uint32_t offset,
                                             uint8_t *buffer, uint16_t max_length,
                                             uint16_t *out_length);

/** @brief Asset metadata and source callback for a complete upload. */
typedef struct {
    qm_display_screen_mask_t screen_mask; /**< Logical screen or screens receiving the asset. */
    uint16_t asset_id;                    /**< Product-defined asset identifier. */
    qm_display_asset_mode_t mode;         /**< Transfer storage and presentation mode. */
    uint32_t total_size;                  /**< Complete asset size in bytes. */
    qm_display_asset_read_fn read;        /**< Callback used to read consecutive fragments. */
    void *read_user;                      /**< User context passed to @ref read. */
} qm_display_asset_upload_t;

/** @brief Playback-complete event reported by the display controller. */
typedef struct {
    uint16_t sequence; /**< Sequence of the DISPLAY or RAM_AUTO_PRESENT request. */
    uint8_t screen_id; /**< Logical screen that completed playback. */
    uint16_t asset_id; /**< Asset that completed playback. */
} qm_display_complete_event_t;

/**
 * @brief Callback invoked after a DISPLAY_COMPLETE event has been acknowledged.
 *
 * The callback runs on the transport owner context and must not recursively
 * call a client request API or destroy the client.
 */
typedef void (*qm_display_event_cb_t)(
    qm_display_client_handle_t client,
    const qm_display_complete_event_t *event,
    void *user_data);

/** @brief Read callback used by a complete firmware upload. */
typedef qm_display_asset_read_fn qm_display_ota_read_fn;

/** @brief Firmware metadata and source callback for a complete OTA upload. */
typedef struct {
    uint32_t total_size; /**< Complete firmware size in bytes. */
    qm_display_ota_read_fn read; /**< Callback used to read consecutive fragments. */
    void *read_user; /**< User context passed to @ref read. */
    uint8_t verify_version; /**< Query GET_FW_VERSION after a lost OTA_END response. */
    qm_display_fw_info_t expected_version; /**< Expected version when verify_version is set. */
} qm_display_ota_upload_t;

/** @brief Client configuration. The callbacks and user pointer are borrowed. */
typedef struct {
    qm_display_write_fn write; /**< Ordered byte-stream write callback. */
    qm_display_read_fn read;   /**< Ordered byte-stream read callback. */
    void *user;                /**< User context passed to @ref write and @ref read. */
    /** First sequence number; change it when reconnecting to a live server cache. */
    uint16_t initial_sequence;
    /** Fixed delay before a BUSY request is retried with a new sequence. */
    uint32_t busy_retry_interval_ms;
    /** Maximum number of new-sequence BUSY retries per request. */
    uint8_t busy_retry_max;
    /** Callback for DISPLAY_COMPLETE events; may be NULL. */
    qm_display_event_cb_t event_cb;
    /** User context passed to event_cb. */
    void *event_user_data;
} qm_display_client_config_t;

/**
 * @brief Create a synchronous display controller client.
 *
 * The client permits one request at a time. External serialization is required
 * when it is shared by multiple tasks.
 *
 * The configuration is copied. Resources referenced through its callback user
 * context are borrowed and must remain valid until destroy returns.
 *
 * @param[in] config Client transport configuration.
 * @return Created client handle, or NULL if the configuration is invalid or
 *         memory allocation fails.
 */
qm_display_client_handle_t qm_display_client_create(
    const qm_display_client_config_t *config);

/**
 * @brief Destroy a client.
 *
 * @param[in] client Client handle, or NULL. No callback is used after return.
 */
void qm_display_client_destroy(qm_display_client_handle_t client);

/**
 * @brief Query the remote firmware version.
 *
 * @param[in] client Client handle.
 * @param[out] info Firmware information, valid only when the remote status is OK.
 * @param[out] result Remote result, valid when this function returns QM_EOK.
 * @return QM_EOK after a valid matching response, or a negative local error.
 */
qm_err_t qm_display_client_get_fw_version(qm_display_client_handle_t client,
                                          qm_display_fw_info_t *info,
                                          qm_display_result_t *result);

/**
 * @brief Process one unsolicited display-controller event.
 *
 * The caller that owns the byte-stream read side should call this while the
 * client is idle. During a synchronous request, events are processed
 * internally. A successful return means one valid event was acknowledged.
 */
qm_err_t qm_display_client_poll(qm_display_client_handle_t client,
                                uint32_t timeout_ms);

/**
 * @brief Atomically display one or two target items.
 *
 * @param[in] client Client handle.
 * @param[in] items Array containing @p item_count items; borrowed for this call.
 * @param[in] item_count Number of items; must be one or two.
 * @param[in] activate_delay_ms Remote activation delay in milliseconds.
 * @param[out] result Remote result, valid when this function returns QM_EOK.
 * @return QM_EOK after a valid matching response, or a negative local error.
 */
qm_err_t qm_display_client_display(qm_display_client_handle_t client,
                                   const qm_display_item_t *items,
                                   uint8_t item_count,
                                   uint16_t activate_delay_ms,
                                   qm_display_result_t *result);

/**
 * @brief Enable or disable display output for one or both screens.
 *
 * Turning a screen off does not discard its current image or transferred
 * assets. Repeating the current state is valid.
 *
 * @param[in] client Client handle.
 * @param[in] screen_mask Logical screen or screens to update.
 * @param[in] power_state Requested display-output state.
 * @param[out] result Remote result, valid when this function returns QM_EOK.
 * @return QM_EOK after a valid matching response, or a negative local error.
 */
qm_err_t qm_display_client_set_screen_power(
    qm_display_client_handle_t client, qm_display_screen_mask_t screen_mask,
    qm_display_power_state_t power_state, qm_display_result_t *result);

/**
 * @brief Set brightness for one or both screens.
 *
 * This operation does not change screen power. A value of zero means the
 * product-defined minimum brightness, not display-output off.
 *
 * @param[in] client Client handle.
 * @param[in] screen_mask Logical screen or screens to update.
 * @param[in] brightness Brightness percentage in the range 0..100.
 * @param[out] result Remote result, valid when this function returns QM_EOK.
 * @return QM_EOK after a valid matching response, or a negative local error.
 */
qm_err_t qm_display_client_set_brightness(
    qm_display_client_handle_t client, qm_display_screen_mask_t screen_mask,
    uint8_t brightness, qm_display_result_t *result);

/**
 * @brief Run ASSET_BEGIN, repeated ASSET_DATA, and ASSET_END from a source callback.
 *
 * The callback receives a borrowed buffer and must not retain it. The client
 * computes the asset CRC incrementally while reading, retries BUSY with new
 * sequences, and restarts the transfer from ASSET_BEGIN after OFFSET_ERROR.
 *
 * @param[in] client Client handle.
 * @param[in] upload Asset metadata and source callback borrowed for this call.
 * @param[out] result Last remote result, valid when this function returns QM_EOK.
 * @return QM_EOK after a successful protocol exchange, or a negative local error.
 */
qm_err_t qm_display_client_upload_asset(
    qm_display_client_handle_t client, const qm_display_asset_upload_t *upload,
    qm_display_result_t *result);

/**
 * @brief Run OTA_BEGIN, repeated OTA_DATA, and OTA_END from a source callback.
 *
 * The client computes the firmware CRC incrementally and restarts from offset
 * zero after OFFSET_ERROR. The complete firmware size must be known in advance.
 */
qm_err_t qm_display_client_upload_ota(
    qm_display_client_handle_t client, const qm_display_ota_upload_t *upload,
    qm_display_result_t *result);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_DISPLAY_CLIENT_H */
