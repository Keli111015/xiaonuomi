#ifndef QM_DISPLAY_SERVER_H
#define QM_DISPLAY_SERVER_H

#include "qm_errno.h"
#include "qm_types.h"
#include "qm_display_server_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup qm_display_server Remote display controller server
 *  @brief Screen-controller side of the display protocol V1.0.
 *  @{
 */

/** @brief Opaque display server handle. */
typedef struct qm_display_server *qm_display_server_handle_t;

/** @brief Protocol status returned to the master. */
typedef enum {
    QM_DISPLAY_SERVER_STATUS_OK = 0x00,                  /**< Request completed successfully. */
    QM_DISPLAY_SERVER_STATUS_UNKNOWN_CMD = 0x01,         /**< Command identifier is unknown. */
    QM_DISPLAY_SERVER_STATUS_UNSUPPORTED_VERSION = 0x02, /**< Protocol version is unsupported. */
    QM_DISPLAY_SERVER_STATUS_INVALID_LENGTH = 0x03,      /**< Payload length is invalid. */
    QM_DISPLAY_SERVER_STATUS_INVALID_PARAM = 0x04,       /**< One or more parameters are invalid. */
    QM_DISPLAY_SERVER_STATUS_BUSY = 0x05,                /**< Server cannot accept the request now. */
    QM_DISPLAY_SERVER_STATUS_ASSET_NOT_FOUND = 0x10,     /**< Referenced asset does not exist. */
    QM_DISPLAY_SERVER_STATUS_VERIFY_FAILED = 0x11,       /**< Asset verification failed. */
    QM_DISPLAY_SERVER_STATUS_OFFSET_ERROR = 0x12,        /**< Fragment offset is unexpected. */
    QM_DISPLAY_SERVER_STATUS_STORAGE_ERROR = 0x13,       /**< Backend storage operation failed. */
    QM_DISPLAY_SERVER_STATUS_DISPLAY_FAILED = 0x20,      /**< Backend display or setting failed. */
    QM_DISPLAY_SERVER_STATUS_INTERNAL_ERROR = 0x30,      /**< Server encountered an internal error. */
} qm_display_server_status_t;

/** @brief Logical screen selection mask. */
typedef enum {
    QM_DISPLAY_SERVER_SCREEN_1 = 0x01,   /**< Select logical screen 1. */
    QM_DISPLAY_SERVER_SCREEN_2 = 0x02,   /**< Select logical screen 2. */
    QM_DISPLAY_SERVER_SCREEN_BOTH = 0x03, /**< Select both logical screens atomically. */
} qm_display_server_screen_mask_t;

/** @brief Screen display-output power state. */
typedef enum {
    QM_DISPLAY_SERVER_POWER_OFF = 0x00, /**< Disable the selected display output. */
    QM_DISPLAY_SERVER_POWER_ON = 0x01,  /**< Enable the selected display output. */
} qm_display_server_power_state_t;

/** @brief Firmware execution state reported to the client. */
typedef enum {
    QM_DISPLAY_SERVER_FW_APPLICATION = 0x00,     /**< Running the application. */
    QM_DISPLAY_SERVER_FW_BOOTLOADER = 0x01,      /**< Running the bootloader. */
    QM_DISPLAY_SERVER_FW_UPGRADE_PENDING = 0x02, /**< A firmware upgrade is pending. */
} qm_display_server_fw_state_t;

/** @brief Application firmware version reported to the client. */
typedef struct {
    uint8_t major; /**< Firmware major version. */
    uint8_t minor; /**< Firmware minor version. */
    uint8_t patch; /**< Firmware patch version. */
} qm_display_server_fw_version_t;

/** @brief Asset source requested by a DISPLAY item. */
typedef enum {
    QM_DISPLAY_SERVER_SOURCE_PERSISTENT = 0x00, /**< Load from persistent storage. */
    QM_DISPLAY_SERVER_SOURCE_RAM_READY = 0x01,  /**< Use the screen's prepared RAM asset. */
} qm_display_server_object_source_t;

/** @brief One target decoded from a DISPLAY request. */
typedef struct {
    uint8_t screen_id;                         /**< Logical screen identifier: 1 or 2. */
    qm_display_server_object_source_t source;  /**< Storage source from which to display. */
    uint16_t asset_id;                         /**< Asset identifier expected at the source. */
} qm_display_server_item_t;

/** @brief Asset transfer storage and presentation mode. */
typedef enum {
    QM_DISPLAY_SERVER_ASSET_RAM_DEFERRED = 0x00,     /**< Prepare in RAM for later DISPLAY. */
    QM_DISPLAY_SERVER_ASSET_RAM_AUTO_PRESENT = 0x01, /**< Present from RAM after validation. */
    QM_DISPLAY_SERVER_ASSET_FLASH_UPDATE = 0x02,     /**< Commit to persistent storage. */
} qm_display_server_asset_mode_t;

/**
 * @brief Write bytes to the ordered byte-stream transport.
 *
 * @param[in] user User context from qm_display_server_config_t::transport_user.
 * @param[in] data Bytes to write. The buffer is borrowed for this call.
 * @param[in] length Number of bytes requested.
 * @param[in] timeout_ms Maximum time to wait in milliseconds.
 * @return Positive bytes written, 0 on timeout, or a negative transport error.
 */
typedef int32_t (*qm_display_server_write_fn)(void *user, const uint8_t *data,
                                               uint32_t length, uint32_t timeout_ms);

/**
 * @brief Read bytes from the ordered byte-stream transport.
 *
 * @param[in] user User context from qm_display_server_config_t::transport_user.
 * @param[out] data Destination buffer borrowed for this call.
 * @param[in] length Maximum number of bytes to read.
 * @param[in] timeout_ms Maximum time to wait in milliseconds.
 * @return Positive bytes read, 0 on timeout, or a negative transport error.
 */
typedef int32_t (*qm_display_server_read_fn)(void *user, uint8_t *data,
                                              uint32_t length, uint32_t timeout_ms);

/**
 * @brief Begin a new asset transfer in the backend.
 *
 * The server invokes asset_cancel for intersecting unfinished tasks before this
 * callback. Pointers are valid only during the callback.
 *
 * @param[in] user Backend context from qm_display_server_config_t::backend_user.
 * @param[in] screen_mask Logical screen or screens receiving the asset.
 * @param[in] asset_id Product-defined asset identifier.
 * @param[in] mode Requested transfer mode.
 * @param[in] total_size Complete asset size in bytes.
 * @return Protocol status to return to the client.
 */
typedef qm_display_server_status_t (*qm_display_server_asset_begin_fn)(
    void *user, qm_display_server_screen_mask_t screen_mask, uint16_t asset_id,
    qm_display_server_asset_mode_t mode, uint32_t total_size);

/**
 * @brief Atomically store one asset fragment for every selected screen.
 *
 * The data pointer is borrowed and valid only during the callback. A dual-screen
 * callback must not commit either logical offset unless both writes succeed.
 *
 * @param[in] user Backend context from qm_display_server_config_t::backend_user.
 * @param[in] screen_mask Screen mask of the active transfer.
 * @param[in] asset_id Asset identifier of the active transfer.
 * @param[in] offset Byte offset of this fragment.
 * @param[in] data Fragment bytes borrowed for this call.
 * @param[in] data_length Fragment size in bytes.
 * @return Protocol status to return to the client.
 */
typedef qm_display_server_status_t (*qm_display_server_asset_data_fn)(
    void *user, qm_display_server_screen_mask_t screen_mask, uint16_t asset_id,
    uint32_t offset, const uint8_t *data, uint16_t data_length);

/**
 * @brief Validate, commit, or present a completed backend asset.
 *
 * @param[in] user Backend context from qm_display_server_config_t::backend_user.
 * @param[in] screen_mask Screen mask of the active transfer.
 * @param[in] asset_id Asset identifier of the active transfer.
 * @param[in] mode Transfer mode selected by ASSET_BEGIN.
 * @param[in] asset_crc16 CRC-16/CCITT-FALSE submitted by the client.
 * @return Protocol status to return to the client.
 */
typedef qm_display_server_status_t (*qm_display_server_asset_end_fn)(
    void *user, qm_display_server_screen_mask_t screen_mask, uint16_t asset_id,
    qm_display_server_asset_mode_t mode, uint16_t asset_crc16);

/**
 * @brief Release an unfinished backend task after replacement or failure.
 *
 * This callback should be idempotent and must not retain any component-owned
 * pointer.
 *
 * @param[in] user Backend context from qm_display_server_config_t::backend_user.
 * @param[in] screen_mask Screen mask of the unfinished transfer.
 * @param[in] asset_id Asset identifier of the unfinished transfer.
 */
typedef void (*qm_display_server_asset_cancel_fn)(
    void *user, qm_display_server_screen_mask_t screen_mask, uint16_t asset_id);

/**
 * @brief Load and atomically switch all display items.
 *
 * Items are borrowed during the callback. Return OK only after every selected
 * screen has completed the requested switch.
 *
 * @param[in] user Backend context from qm_display_server_config_t::backend_user.
 * @param[in] items Decoded display items borrowed for this call.
 * @param[in] item_count Number of items; one or two.
 * @param[in] activate_delay_ms Requested activation delay in milliseconds.
 * @return Protocol status to return to the client.
 */
typedef qm_display_server_status_t (*qm_display_server_display_fn)(
    void *user, const qm_display_server_item_t *items, uint8_t item_count,
    uint16_t activate_delay_ms);

/**
 * @brief Enable or disable display output for one or both screens.
 *
 * Repeating the current state must return OK. Turning output off must preserve
 * the current image and transferred assets.
 *
 * @param[in] user Backend context from qm_display_server_config_t::backend_user.
 * @param[in] screen_mask Logical screen or screens to update.
 * @param[in] power_state Requested display-output state.
 * @return Protocol status to return to the client.
 */
typedef qm_display_server_status_t (*qm_display_server_set_screen_power_fn)(
    void *user, qm_display_server_screen_mask_t screen_mask,
    qm_display_server_power_state_t power_state);

/**
 * @brief Set brightness for one or both screens.
 *
 * Repeating the current value must return OK. This operation must not change
 * display-output power.
 *
 * @param[in] user Backend context from qm_display_server_config_t::backend_user.
 * @param[in] screen_mask Logical screen or screens to update.
 * @param[in] brightness Brightness percentage in the range 0..100.
 * @return Protocol status to return to the client.
 */
typedef qm_display_server_status_t (*qm_display_server_set_brightness_fn)(
    void *user, qm_display_server_screen_mask_t screen_mask, uint8_t brightness);

/** @brief Server configuration copied by qm_display_server_create(). */
typedef struct {
    qm_display_server_write_fn write; /**< Ordered byte-stream write callback. */
    qm_display_server_read_fn read;   /**< Ordered byte-stream read callback. */
    void *transport_user; /**< User context passed to transport callbacks. */
    void *backend_user;   /**< User context passed to backend callbacks. */
    qm_display_server_fw_version_t firmware_version; /**< Application version to report. */
    qm_display_server_fw_state_t firmware_state; /**< Firmware execution state to report. */
    qm_display_server_asset_begin_fn asset_begin; /**< Backend asset-begin operation. */
    qm_display_server_asset_data_fn asset_data;   /**< Backend asset-fragment operation. */
    qm_display_server_asset_end_fn asset_end;     /**< Backend asset-completion operation. */
    qm_display_server_asset_cancel_fn asset_cancel; /**< Backend temporary-task cleanup. */
    qm_display_server_display_fn display;         /**< Backend atomic display operation. */
    qm_display_server_set_screen_power_fn set_screen_power; /**< Backend power operation. */
    qm_display_server_set_brightness_fn set_brightness; /**< Backend brightness operation. */
} qm_display_server_config_t;

/** @brief Snapshot of protocol state for one logical screen. */
typedef struct {
    uint8_t transfer_active; /**< Non-zero while an asset transfer is active. */
    uint8_t has_ram_ready;   /**< Non-zero when @ref ram_ready_asset_id is valid. */
    uint8_t has_current_display; /**< Non-zero when current display fields are valid. */
    uint8_t has_power_state;     /**< Non-zero when @ref power_state is known. */
    uint8_t has_brightness;      /**< Non-zero when @ref brightness is known. */
    qm_display_server_screen_mask_t transfer_screen_mask; /**< Active transfer screen mask. */
    qm_display_server_asset_mode_t transfer_mode; /**< Mode of the active transfer. */
    uint16_t transfer_asset_id;                   /**< Asset ID of the active transfer. */
    uint32_t transfer_total_size;                 /**< Expected complete asset size. */
    uint32_t received_size;                       /**< Reliably received continuous bytes. */
    uint16_t ram_ready_asset_id;                  /**< Asset prepared in RAM for DISPLAY. */
    qm_display_server_object_source_t current_source; /**< Current display source. */
    uint16_t current_asset_id;                    /**< Asset currently displayed. */
    qm_display_server_power_state_t power_state;  /**< Last applied display-output state. */
    uint8_t brightness;                           /**< Last applied brightness percentage. */
} qm_display_server_screen_info_t;

/**
 * @brief Create a synchronous display protocol server.
 *
 * The configuration is copied, but its callback contexts are borrowed and must
 * remain valid until qm_display_server_destroy() returns. Serialize all calls
 * that use the same handle.
 *
 * @param[in] config Transport, backend, and firmware configuration.
 * @return Created server handle, or NULL if the configuration is invalid or
 *         memory allocation fails.
 */
qm_display_server_handle_t qm_display_server_create(
    const qm_display_server_config_t *config);

/**
 * @brief Cancel unfinished transfers and destroy a server.
 *
 * @param[in] server Server handle, or NULL. No callback is used after return.
 */
void qm_display_server_destroy(qm_display_server_handle_t server);

/**
 * @brief Receive, execute, and respond to one valid request.
 *
 * Corrupt frames are skipped until timeout. The call is synchronous and must
 * not run concurrently for the same handle.
 *
 * @param[in] server Server handle.
 * @param[in] timeout_ms Maximum time to wait for one valid request.
 * @return QM_EOK after responding to one valid request, or a negative local error.
 */
qm_err_t qm_display_server_process(qm_display_server_handle_t server,
                                   uint32_t timeout_ms);

/**
 * @brief Read the protocol state for one logical screen.
 *
 * @param[in] server Server handle.
 * @param[in] screen_id Logical screen identifier: 1 or 2.
 * @param[out] info Receives the current server state snapshot.
 * @return QM_EOK on success, or -QM_EINVAL for an invalid argument.
 */
qm_err_t qm_display_server_get_screen_info(qm_display_server_handle_t server,
                                           uint8_t screen_id,
                                           qm_display_server_screen_info_t *info);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_DISPLAY_SERVER_H */
