#ifndef XJIANG_HTTP_OTA_H
#define XJIANG_HTTP_OTA_H

#include "qm_errno.h"
#include "qm_http_ota.h"
#include "qm_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Xiaojiang HTTP OTA server environment.
 */
typedef enum {
    XJIANG_HTTP_OTA_ENV_PROD = 0,    /*!< Production environment */
    XJIANG_HTTP_OTA_ENV_DEV,         /*!< Development environment */
    XJIANG_HTTP_OTA_ENV_TEST,        /*!< Test environment */
} xjiang_http_ota_env_t;

/**
 * @brief OTA target selected by the application.
 */
typedef enum {
    XJIANG_HTTP_OTA_TARGET_MODULE = 0,   /*!< Upgrade module firmware */
    XJIANG_HTTP_OTA_TARGET_MCU,          /*!< Upgrade external MCU firmware */
} xjiang_http_ota_target_t;

/**
 * @brief Firmware metadata for one OTA target.
 */
typedef struct {
    qm_bool available;      /*!< QM_TRUE when an upgrade is available */
    char *version;          /*!< Upgrade version, allocated by component */
    char *url;              /*!< Firmware download URL, allocated by component */
    char *sign;             /*!< Signature returned by Xiaojiang API, allocated by component */
    uint32_t file_size;     /*!< Firmware file size in bytes */
    char *file_name;        /*!< Firmware file name, allocated by component */
} xjiang_http_ota_firmware_info_t;

/**
 * @brief OTA check response.
 */
typedef struct {
    int status_code;                            /*!< HTTP status code from version API */
    xjiang_http_ota_firmware_info_t module;     /*!< Module firmware metadata */
    xjiang_http_ota_firmware_info_t mcu;        /*!< MCU firmware metadata */
} xjiang_http_ota_info_t;

/**
 * @brief Xiaojiang HTTP OTA private error code base.
 */
#define QM_ERR_XJIANG_HTTP_OTA_BASE              0x9200

/**
 * @brief No module or MCU upgrade is available.
 */
#define QM_ERR_XJIANG_HTTP_OTA_NO_UPGRADE        (QM_ERR_XJIANG_HTTP_OTA_BASE + 1)

/**
 * @brief Version API response or firmware metadata is invalid.
 */
#define QM_ERR_XJIANG_HTTP_OTA_BAD_RESPONSE      (QM_ERR_XJIANG_HTTP_OTA_BASE + 2)

/**
 * @brief Upgrade is available but the selected target is not allowed.
 */
#define QM_ERR_XJIANG_HTTP_OTA_UPGRADE_NOT_ALLOWED (QM_ERR_XJIANG_HTTP_OTA_BASE + 3)

/**
 * @brief Set the Xiaojiang HTTP OTA server environment.
 *
 * If this API is not called, the component uses XJIANG_HTTP_OTA_ENV_PROD.
 *
 * @param[in] env Server environment.
 * @return QM_EOK on success, or a negative error code on failure.
 */
int xjiang_http_ota_set_env(xjiang_http_ota_env_t env);

/**
 * @brief Set the product secret used to verify OTA firmware HMAC-SHA256.
 *
 * The Xiaojiang API returns firmware sign as Base64-encoded HMAC-SHA256
 * digest. The component decodes it and passes the hex digest to qm_http_ota for
 * verification. Passing NULL or an empty string clears the product secret and
 * disables signature verification.
 *
 * @param[in] product_secret Product secret used as HMAC-SHA256 key, or NULL to clear.
 * @return QM_EOK on success, or a negative error code on failure.
 */
int xjiang_http_ota_set_product_secret(const char *product_secret);

/**
 * @brief Register MCU OTA callbacks used by xjiang_http_ota_perform().
 *
 * The callbacks are used only when performing XJIANG_HTTP_OTA_TARGET_MCU. The
 * component downloads the MCU firmware and forwards data to the business layer
 * through qm_http_ota_mcu_ops_t::write. Passing NULL clears the registration.
 *
 * @param[in] mcu_ota MCU OTA callback configuration, or NULL to clear it.
 * @return QM_EOK on success, or a negative error code on failure.
 */
int xjiang_http_ota_set_mcu_ota(const qm_http_ota_mcu_config_t *mcu_ota);

/**
 * @brief Release dynamic strings inside version check metadata.
 *
 * Call this after the application has finished using a successful
 * xjiang_http_ota_check() result.
 *
 * @param[in,out] info OTA metadata to release.
 */
void xjiang_http_ota_info_free(xjiang_http_ota_info_t *info);

/**
 * @brief Check available module and MCU firmware versions.
 *
 * Versions are compared as exactly three numeric fields, for example 1.0.0. If
 * any returned field is greater than the current version's corresponding field,
 * that target is marked available in @p info.
 *
 * On success, dynamic strings inside @p info are owned by the component and
 * must be released with xjiang_http_ota_info_free().
 *
 * @param[in] pid Product PID string.
 * @param[in] module_version Current module firmware version.
 * @param[in] mcu_version Current MCU firmware version.
 * @param[in] authorization_token Authorization header value, or NULL/empty to omit it.
 * @param[out] info Output version and firmware metadata.
 * @return QM_EOK on success, or a negative error code on failure.
 */
int xjiang_http_ota_check(const char *pid,
                          const char *module_version,
                          const char *mcu_version,
                          const char *authorization_token,
                          xjiang_http_ota_info_t *info);

/**
 * @brief Perform OTA for one target selected by the application.
 *
 * @param[in] info OTA metadata returned by xjiang_http_ota_check().
 * @param[in] target Upgrade target, module or MCU.
 * @param[out] result Optional output OTA result for the selected target.
 * @return QM_EOK on success, or a negative error code on failure.
 */
int xjiang_http_ota_perform(const xjiang_http_ota_info_t *info,
                            xjiang_http_ota_target_t target,
                            qm_http_ota_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* XJIANG_HTTP_OTA_H */
