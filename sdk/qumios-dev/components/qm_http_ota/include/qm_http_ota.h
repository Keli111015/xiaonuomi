#ifndef QM_HTTP_OTA_H
#define QM_HTTP_OTA_H

#include "qm_errno.h"
#include "qm_http_client.h"
#include "qm_types.h"
#include "qm_http_ota_log.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP OTA event id.
 */
typedef enum {
    QM_HTTP_OTA_EVENT_START = 0,      /*!< OTA started */
    QM_HTTP_OTA_EVENT_CONNECTED,      /*!< HTTP connection established */
    QM_HTTP_OTA_EVENT_HEADER,         /*!< One response header was received */
    QM_HTTP_OTA_EVENT_DATA,           /*!< One image chunk was downloaded and written */
    QM_HTTP_OTA_EVENT_FINISH,         /*!< Image download completed */
    QM_HTTP_OTA_EVENT_ABORT,          /*!< OTA aborted by caller */
    QM_HTTP_OTA_EVENT_ERROR,          /*!< OTA failed; error_code carries the negative reason */
    QM_HTTP_OTA_EVENT_PROGRESS,       /*!< Download progress updated */
} qm_http_ota_event_id_t;

/**
 * @brief HTTP OTA event payload.
 *
 * The event object and pointed header/data buffers are owned by the component
 * and are valid only during the callback.
 */
typedef struct {
    qm_http_ota_event_id_t event_id;  /*!< Event type */
    void *user;                       /*!< User context from qm_http_ota_config_t */
    int status_code;                  /*!< Current HTTP status code when available */
    int error_code;                   /*!< ERROR/ABORT only; negative QM_* or -QM_ERR_HTTP_OTA_* code */
    uint32_t total_size;              /*!< Expected full image size */
    uint32_t downloaded_size;         /*!< Total image bytes written so far */
    const char *header_name;          /*!< Header name for HEADER event */
    const char *header_value;         /*!< Header value for HEADER event */
    const char *data;                 /*!< Data chunk for DATA event */
    int data_len;                     /*!< Data chunk length for DATA event */
    int progress_percent;             /*!< Download progress percent 0..100 for PROGRESS event */
} qm_http_ota_event_t;

/**
 * @brief HTTP OTA event callback.
 *
 * @param[in] event OTA event payload
 * @return QM_EOK to continue OTA
 * @return Other value to abort OTA progress
 */
typedef int (*qm_http_ota_event_cb_t)(const qm_http_ota_event_t *event);

/**
 * @brief MD5 digest length in hex characters, excluding trailing '\0'.
 */
#define QM_HTTP_OTA_MD5_HEX_LEN 32

/**
 * @brief HMAC-SHA256 digest length in hex characters, excluding trailing '\0'.
 */
#define QM_HTTP_OTA_HMAC_SHA256_HEX_LEN 64

/**
 * @brief HTTP OTA target type.
 */
typedef enum {
    QM_HTTP_OTA_TARGET_SOC = 0,   /*!< Upgrade this SoC through qm_ota */
    QM_HTTP_OTA_TARGET_MCU,       /*!< Download and forward image to an external MCU */
} qm_http_ota_target_t;

/**
 * @brief Digest verification mode.
 */
typedef enum {
    QM_HTTP_OTA_DIGEST_TYPE_MD5 = 0,         /*!< Verify MD5 digest using expected_md5_hex */
    QM_HTTP_OTA_DIGEST_TYPE_HMAC_SHA256,     /*!< Verify HMAC-SHA256 digest using expected_hmac_sha256_hex and product_secret */
} qm_http_ota_digest_type_t;

/**
 * @brief Digest data source mode.
 */
typedef enum {
    QM_HTTP_OTA_DIGEST_VERIFY_STREAM = 0,   /*!< Verify by updating digest from each downloaded data buffer */
    QM_HTTP_OTA_DIGEST_VERIFY_READBACK,     /*!< Verify by reading the written target data back before finish */
} qm_http_ota_digest_verify_mode_t;

/**
 * @brief MCU OTA begin information.
 */
typedef struct {
    const char *url;              /*!< Firmware URL */
    uint32_t total_size;          /*!< Expected full image size */
    uint32_t resume_offset;       /*!< Local offset that will be resumed from, 0 for full download */
} qm_http_ota_mcu_begin_info_t;

/**
 * @brief MCU OTA operation callbacks.
 *
 * The callbacks are used only when target is QM_HTTP_OTA_TARGET_MCU. All
 * callbacks must return QM_EOK on success or a negative error code on failure.
 * finish() should commit or activate the MCU image only after
 * qm_http_ota_finish() has completed optional digest verification.
 *
 * Digest verification supports two modes. Stream mode checks the
 * downloaded data buffer and does not require read(). Read-back mode rebuilds
 * the digest from the written target data and requires read().
 */
typedef struct {
    int (*begin)(void *user, const qm_http_ota_mcu_begin_info_t *info);       /*!< Prepare MCU OTA transport/storage */
    int (*write)(void *user, uint32_t offset, const uint8_t *data, uint32_t len); /*!< Write one image chunk to MCU */
    int (*read)(void *user, uint32_t offset, uint8_t *data, uint32_t len);    /*!< Optional unless read-back digest verification is used */
    int (*finish)(void *user);                                               /*!< Commit MCU OTA image */
    int (*abort)(void *user, int error_code);                                /*!< Abort MCU OTA image transfer */
} qm_http_ota_mcu_ops_t;

/**
 * @brief MCU OTA callback configuration.
 */
typedef struct {
    const qm_http_ota_mcu_ops_t *ops;    /*!< MCU OTA operation callbacks */
    void *user;                          /*!< User context passed to MCU OTA callbacks */
} qm_http_ota_mcu_config_t;

/**
 * @brief HTTP OTA configuration.
 *
 * This structure reuses qumi's qm_http_client_request_t as the underlying HTTP
 * request description.
 *
 * Resume behavior:
 * - resume is optional, not mandatory for OTA
 * - when resume_enable is QM_TRUE and resume_offset is 0, the component may
 *   try to load persisted progress for the same URL from KV storage
 * - persisted resume records are reused only when URL, expected_total_size and
 *   offset are valid for the current task
 * - the component captures ETag, or Last-Modified as a fallback, from response
 *   headers and persists it as the next If-Range value
 * - SoC resume depends on the platform qm_ota implementation preserving
 *   previously written OTA partition bytes across sessions
 * - resume_if_range is an optional caller-provided override; leave it NULL for
 *   the usual automatic behavior
 *
 * File length:
 * - expected_total_size is required and must be non-zero
 * - it is treated as the exact full image length
 * - if the server also reports Content-Length or Content-Range total, that
 *   reported length must match expected_total_size
 * - when http_config->max_body_len is 0, the underlying HTTP client's body
 *   limit is raised to expected_total_size, so streaming OTA is not capped by
 *   the HTTP client's small default body limit
 *
 * Progress:
 * - progress_step throttles QM_HTTP_OTA_EVENT_PROGRESS by percent buckets
 * - progress_step == 0 keeps the legacy behavior and dispatches progress after
 *   every written chunk
 */
typedef struct {
    const qm_http_client_request_t *http_config;  /*!< HTTP request configuration, URL must be set */
    qm_http_ota_event_cb_t on_event;              /*!< Optional OTA event callback */
    void *user;                                   /*!< User context passed to event callback */
    qm_bool auto_restart;                         /*!< Restart after successful qm_http_ota_finish() when QM_TRUE */
    qm_bool resume_enable;                        /*!< Enable resumable OTA */
    uint32_t resume_offset;                       /*!< Explicit resume offset, 0 means try KV auto-resume when enabled */
    const char *resume_if_range;                  /*!< Optional If-Range override; normally NULL to use persisted ETag/Last-Modified */
    const char *product_secret;                   /*!< Non-public product secret used as HMAC-SHA256 key when expected_hmac_sha256_hex is set */
    qm_http_ota_digest_type_t digest_type;        /*!< Digest algorithm used when an expected digest is configured */
    const char *expected_md5_hex;                 /*!< Expected image MD5 hex digest, 32 chars, used when digest_type is MD5 */
    const char *expected_hmac_sha256_hex;         /*!< Expected image HMAC-SHA256 hex digest, 64 chars, used when digest_type is HMAC_SHA256 */
    qm_http_ota_digest_verify_mode_t digest_verify_mode; /*!< Digest verification mode, default 0 is stream mode */
    qm_http_ota_target_t target;                  /*!< OTA target, default 0 is QM_HTTP_OTA_TARGET_SOC */
    const qm_http_ota_mcu_config_t *mcu_ota;      /*!< Required when target is QM_HTTP_OTA_TARGET_MCU */
    uint32_t expected_total_size;                 /*!< Required exact full image size in bytes; 0 is invalid */
    uint8_t progress_step;                        /*!< Progress event percent step; 0 dispatches every written chunk */
} qm_http_ota_config_t;

/**
 * @brief HTTP OTA result snapshot.
 */
typedef struct {
    int status_code;              /*!< Final or current HTTP status code */
    uint32_t total_size;          /*!< Expected full image size */
    uint32_t downloaded_size;     /*!< Total image bytes written */
} qm_http_ota_result_t;

/**
 * @brief Opaque HTTP OTA handle.
 */
typedef void *qm_http_ota_handle_t;

/**
 * @brief Maximum stored OTA resume URL length, including the trailing '\0'.
 */
#define QM_HTTP_OTA_RESUME_URL_LEN      192

/**
 * @brief Maximum stored OTA resume tag length, including the trailing '\0'.
 */
#define QM_HTTP_OTA_RESUME_TAG_LEN      96

/**
 * @brief Persisted resume information for a specific URL.
 */
typedef struct {
    qm_bool valid;                /*!< QM_TRUE when this record contains valid resume info */
    uint32_t offset;              /*!< Persisted downloaded offset */
    uint32_t total_size;          /*!< Persisted expected full image size */
    char url[QM_HTTP_OTA_RESUME_URL_LEN];        /*!< Original URL used to create this resume record */
    char if_range[QM_HTTP_OTA_RESUME_TAG_LEN];   /*!< Persisted If-Range value, usually ETag or Last-Modified */
} qm_http_ota_resume_info_t;

/**
 * @brief HTTP OTA private error code base.
 *
 * Values in this group are positive error identifiers. Public APIs and
 * qm_http_ota_event_t.error_code report failures as negative values, consistent
 * with qm_errno.h conventions.
 */
#define QM_ERR_HTTP_OTA_BASE            0x9100

/**
 * @brief OTA operation is still in progress.
 *
 * qm_http_ota_perform() returns `-QM_ERR_HTTP_OTA_IN_PROGRESS` while more data
 * remains to be downloaded and written.
 */
#define QM_ERR_HTTP_OTA_IN_PROGRESS     (QM_ERR_HTTP_OTA_BASE + 1)

/**
 * @brief OTA image HMAC-SHA256 digest does not match expected_hmac_sha256_hex.
 *
 * qm_http_ota_finish() returns `-QM_ERR_HTTP_OTA_HMAC_SHA256_MISMATCH`, and the
 * same value is delivered through QM_HTTP_OTA_EVENT_ERROR.
 */
#define QM_ERR_HTTP_OTA_HMAC_SHA256_MISMATCH (QM_ERR_HTTP_OTA_BASE + 2)

/**
 * @brief OTA image MD5 digest does not match expected_md5_hex.
 *
 * qm_http_ota_finish() returns `-QM_ERR_HTTP_OTA_MD5_MISMATCH`, and the same
 * value is delivered through QM_HTTP_OTA_EVENT_ERROR.
 */
#define QM_ERR_HTTP_OTA_MD5_MISMATCH (QM_ERR_HTTP_OTA_BASE + 3)

/**
 * @brief Perform a blocking HTTP OTA update.
 *
 * This helper internally performs `begin -> perform(loop) -> finish`.
 *
 * @param[in] config OTA configuration
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_ota(const qm_http_ota_config_t *config);

/**
 * @brief Begin an HTTP OTA session.
 *
 * This API initializes the HTTP client handle and sends the HTTP request.
 * For the default SoC target it also starts qm_ota immediately. For MCU target
 * the MCU begin callback is invoked after response headers are fetched, so the
 * callback can receive total size and final resume offset. After this call
 * succeeds, invoke qm_http_ota_perform() in a loop and then either
 * qm_http_ota_finish() or qm_http_ota_abort().
 *
 * @param[in] config OTA configuration
 * @param[out] handle OTA handle output
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_ota_begin(const qm_http_ota_config_t *config, qm_http_ota_handle_t *handle);

/**
 * @brief Download and write the next OTA chunk.
 *
 * This API performs incremental OTA work. Call it repeatedly until it returns
 * QM_EOK, which means the full image has been received. When OTA is still
 * active, it returns `-QM_ERR_HTTP_OTA_IN_PROGRESS`.
 *
 * @param[in] handle OTA handle returned by qm_http_ota_begin()
 * @return QM_EOK when OTA image download completed
 * @return -QM_ERR_HTTP_OTA_IN_PROGRESS when OTA is still in progress
 * @return Negative error code on failure
 */
int qm_http_ota_perform(qm_http_ota_handle_t handle);

/**
 * @brief Check whether the full OTA image has been received.
 *
 * @param[in] handle OTA handle
 * @return QM_TRUE when all image data has been received
 * @return QM_FALSE otherwise
 */
qm_bool qm_http_ota_is_complete_data_received(qm_http_ota_handle_t handle);

/**
 * @brief Finalize an OTA session after successful download.
 *
 * For the default SoC target this API ends the OTA write session, switches the
 * boot partition, releases the OTA handle, clears the persisted resume record
 * for this URL, and optionally restarts the system when auto_restart is
 * enabled. For MCU target it calls the MCU finish callback after optional
 * digest verification; auto_restart is ignored.
 *
 * @param[in] handle OTA handle
 * @return QM_EOK on success
 * @return -QM_ERR_HTTP_OTA_MD5_MISMATCH when MD5 verification fails
 * @return -QM_ERR_HTTP_OTA_HMAC_SHA256_MISMATCH when HMAC-SHA256 verification fails
 * @return Negative error code on failure
 */
int qm_http_ota_finish(qm_http_ota_handle_t handle);

/**
 * @brief Abort an OTA session and release upper-layer resources.
 *
 * Abort stops the HTTP OTA session, calls qm_ota_abort() for the default SoC
 * target or the MCU abort callback for MCU target, and frees upper-layer
 * resources. Persisted resume information is kept so the same URL can continue
 * later.
 *
 * @param[in] handle OTA handle
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_ota_abort(qm_http_ota_handle_t handle);

/**
 * @brief Get the total image bytes written so far.
 *
 * @param[in] handle OTA handle
 * @return Total bytes written so far
 * @return -1 on failure
 */
int qm_http_ota_get_image_len_read(qm_http_ota_handle_t handle);

/**
 * @brief Get the current HTTP status code.
 *
 * @param[in] handle OTA handle
 * @return HTTP status code, or negative error code on failure
 */
int qm_http_ota_get_status_code(qm_http_ota_handle_t handle);

/**
 * @brief Copy the current OTA result snapshot.
 *
 * @param[in] handle OTA handle
 * @param[out] result Result output buffer
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_ota_get_result(qm_http_ota_handle_t handle, qm_http_ota_result_t *result);

/**
 * @brief Read the persisted resume record for a specific URL.
 *
 * Resume records are stored by URL-derived key and verified again against the
 * original URL stored in the record.
 *
 * @param[in] url URL used for the OTA task
 * @param[out] info Resume info output buffer
 * @return QM_EOK on success
 * @return Negative error code on failure or when no valid record exists
 */
int qm_http_ota_resume_info_get(const char *url, qm_http_ota_resume_info_t *info);

/**
 * @brief Clear the persisted resume record for a specific URL.
 *
 * @param[in] url URL used for the OTA task
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
int qm_http_ota_resume_info_clear(const char *url);

#ifdef __cplusplus
}
#endif

#endif /* QM_HTTP_OTA_H */
