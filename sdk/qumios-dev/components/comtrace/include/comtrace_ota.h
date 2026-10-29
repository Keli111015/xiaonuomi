/**
 * @file comtrace_ota.h
 * @brief CEUP/1 OTA capability using the QUMI OTA HAL.
 */
#ifndef COMTRACE_OTA_H
#define COMTRACE_OTA_H

#include "comtrace_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define COMTRACE_OTA_SHA256_SIZE 32U

typedef struct comtrace_ota comtrace_ota_t;

/** @brief CEUP/1 transfer state reported to the peer and application. */
typedef enum {
    COMTRACE_OTA_UNREGISTERED = 0, /**< No accepted registration on the connection. */
    COMTRACE_OTA_REGISTERING,     /**< Waiting for update_register_ack. */
    COMTRACE_OTA_IDLE,            /**< Registered and ready for an update. */
    COMTRACE_OTA_PREPARED,        /**< OTA storage opened; no chunk committed yet. */
    COMTRACE_OTA_TRANSFERRING,    /**< Firmware chunks are being written. */
    COMTRACE_OTA_VERIFYING,       /**< Received image digest is being finalized and checked. */
    COMTRACE_OTA_REBOOTING,       /**< Image committed; reboot requested. */
    COMTRACE_OTA_SUCCESS,         /**< First boot confirmed the new image. */
    COMTRACE_OTA_FAILED,          /**< Transfer, verification, or boot failed. */
    COMTRACE_OTA_ROLLED_BACK,     /**< Bootloader restored the previous image. */
    COMTRACE_OTA_CANCELLED,       /**< Uncommitted transfer was cancelled. */
} comtrace_ota_state_t;

/** @brief CEUP/1 protocol and local connection error codes. */
typedef enum {
    COMTRACE_OTA_ERROR_NONE = 0,
    COMTRACE_OTA_ERROR_INVALID_FRAME = 1,
    COMTRACE_OTA_ERROR_UNSUPPORTED_VERSION = 2,
    COMTRACE_OTA_ERROR_UNSUPPORTED_TYPE = 3,
    COMTRACE_OTA_ERROR_OTA_NOT_SUPPORTED = 10,
    COMTRACE_OTA_ERROR_TASK_BUSY = 11,
    COMTRACE_OTA_ERROR_PRODUCT_MISMATCH = 12,
    COMTRACE_OTA_ERROR_VERSION_REJECTED = 13,
    COMTRACE_OTA_ERROR_IMAGE_INVALID = 14,
    COMTRACE_OTA_ERROR_OFFSET_INVALID = 15,
    COMTRACE_OTA_ERROR_WRITE_FAILED = 16,
    COMTRACE_OTA_ERROR_VERIFY_FAILED = 17,
    COMTRACE_OTA_ERROR_BOOT_FAILED = 18,
    COMTRACE_OTA_ERROR_ROLLBACK = 19,
    COMTRACE_OTA_ERROR_TASK_CANCELLED = 20,
    COMTRACE_OTA_ERROR_CONNECTION_LOST = 21,
} comtrace_ota_error_t;

/** @brief Current CEUP/1 transfer status. */
typedef struct {
    comtrace_ota_state_t state;
    uint32_t received_bytes;
    uint32_t image_size;
    uint32_t error_code;
} comtrace_ota_status_t;

typedef void (*comtrace_ota_status_cb_t)(const comtrace_ota_status_t *status, void *user);

/** @brief Validated metadata for one proposed update. Callback-lifetime data. */
typedef struct {
    const char *target_version; /**< Requested version; valid only during callback. */
    const char *product_id;     /**< Requested product; valid only during callback. */
    const uint8_t *sha256;      /**< 32-byte digest; valid only during callback. */
    uint32_t image_size;        /**< Complete image size in bytes. */
    uint16_t chunk_size;        /**< Exact non-final chunk size in bytes. */
} comtrace_ota_request_t;

/**
 * @brief Authorize an update before the OTA partition is opened.
 *
 * The callback must authenticate the transport or compare the supplied digest
 * with trusted release metadata. Return QM_EOK to allow the update.
 */
typedef int (*comtrace_ota_authorize_cb_t)(const comtrace_ota_request_t *request, void *user);

/**
 * @brief Reboot immediately or schedule a reboot after `rebooting` was sent.
 *
 * Return QM_EOK only when reboot is guaranteed. A NULL callback uses qm_reboot().
 */
typedef int (*comtrace_ota_reboot_cb_t)(void *user);

/**
 * @brief CEUP/1 instance configuration copied by comtrace_ota_init().
 *
 * All APIs and callbacks for one instance must be serialized by the caller.
 */
typedef struct {
    const char *sn; /**< Required; copied by init. */
    uint32_t sid;                 /**< Required non-zero session identifier. */
    const char *current_version; /**< Required; copied by init. */
    const char *product_id;      /**< Required; copied by init. */
    uint16_t max_frame;          /**< Zero uses the configured default. */
    uint16_t max_chunk;           /**< Maximum accepted firmware chunk bytes. */
    uint32_t max_image_size;      /**< Maximum accepted complete image size. */
    comtrace_send_cb_t send;      /**< Required reliable stream send callback. */
    comtrace_ota_authorize_cb_t authorize; /**< Required update policy callback. */
    comtrace_ota_status_cb_t on_status; /**< Optional local state notification. */
    comtrace_ota_reboot_cb_t reboot; /**< Optional reboot scheduler; NULL uses qm_reboot(). */
    void *user;                   /**< Passed unchanged to protocol callbacks. */
} comtrace_ota_config_t;

/** @brief Allocate and initialize a CEUP/1 OTA instance. */
int comtrace_ota_init(comtrace_ota_t **out, const comtrace_ota_config_t *config);
/** @brief Abort an active image and release the instance; retry or reboot if abort is unavailable. */
int comtrace_ota_deinit(comtrace_ota_t *ota);
/** @brief Send an update_register frame. */
int comtrace_ota_register(comtrace_ota_t *ota);
/** @brief Feed received reliable-stream bytes into the CEUP/1 decoder. */
int comtrace_ota_feed(comtrace_ota_t *ota, const uint8_t *bytes, uint16_t length);
/** @brief Abort an uncommitted transfer after disconnect; may require reboot without HAL abort. */
int comtrace_ota_disconnect(comtrace_ota_t *ota);
/** @brief Cancel the current transfer and report the resulting status. */
int comtrace_ota_cancel(comtrace_ota_t *ota);
/** @brief Report first-boot success or rollback after registration. */
int comtrace_ota_report_boot_result(comtrace_ota_t *ota, comtrace_ota_state_t state,
                                    uint32_t error_code);
/** @brief Copy the current transfer status to application storage. */
int comtrace_ota_get_status(const comtrace_ota_t *ota, comtrace_ota_status_t *status);

#ifdef __cplusplus
}
#endif
#endif
