#ifndef QM_PROVISION_H
#define QM_PROVISION_H

/**
 * @file qm_provision.h
 * @brief BLE provisioning session API.
 */

#include "qm.h"
#include "qm_errno.h"
#include "qm_provision_log.h"
#include "qm_types.h"


#ifndef CONFIG_QM_PROVISION_SSID_MAX_LEN
#define CONFIG_QM_PROVISION_SSID_MAX_LEN 32
#endif

#ifndef CONFIG_QM_PROVISION_PASSWORD_MAX_LEN
#define CONFIG_QM_PROVISION_PASSWORD_MAX_LEN 64
#endif

#ifndef CONFIG_QM_PROVISION_DYNREG_HOST_MAX_LEN
#define CONFIG_QM_PROVISION_DYNREG_HOST_MAX_LEN 127
#endif

#ifndef CONFIG_QM_PROVISION_AUDIO_URI_MAX_LEN
#define CONFIG_QM_PROVISION_AUDIO_URI_MAX_LEN 64
#endif

#ifndef CONFIG_QM_PROVISION_AUTHCODE_MAX_LEN
#define CONFIG_QM_PROVISION_AUTHCODE_MAX_LEN 64
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief WiFi SSID receive buffer size, including the trailing '\0'.
 */
#define QM_PROVISION_SSID_SIZE (CONFIG_QM_PROVISION_SSID_MAX_LEN + 1)

/**
 * @brief WiFi password receive buffer size, including the trailing '\0'.
 */
#define QM_PROVISION_PASSWORD_SIZE (CONFIG_QM_PROVISION_PASSWORD_MAX_LEN + 1)

/**
 * @brief Dynamic registration host receive buffer size, including the trailing '\0'.
 */
#define QM_PROVISION_DYNREG_HOST_SIZE (CONFIG_QM_PROVISION_DYNREG_HOST_MAX_LEN + 1)

/**
 * @brief Audio URI receive buffer size, including the trailing '\0'.
 */
#define QM_PROVISION_AUDIO_URI_SIZE (CONFIG_QM_PROVISION_AUDIO_URI_MAX_LEN + 1)

/**
 * @brief Authentication code receive buffer size, including the trailing '\0'.
 */
#define QM_PROVISION_AUTHCODE_SIZE (CONFIG_QM_PROVISION_AUTHCODE_MAX_LEN + 1)

/**
 * @brief Opaque BLE provisioning session handle.
 */
typedef struct qm_provision *qm_provision_handle_t;

/** @brief BLE provisioning environment type values. */
typedef enum {
    QM_PROVISION_ENV_TYPE_DEV  = 0x00,
    QM_PROVISION_ENV_TYPE_TEST = 0x01,
    QM_PROVISION_ENV_TYPE_PROD = 0x02,
} qm_provision_env_type_t;

/**
 * @brief WiFi provisioning information received from the BLE app.
 */
typedef struct {
    /**
     * @brief WiFi SSID.
     */
    char ssid[QM_PROVISION_SSID_SIZE];

    /**
     * @brief WiFi password.
     */
    char password[QM_PROVISION_PASSWORD_SIZE];

    /**
     * @brief Dynamic registration host carried by the BLE provisioning payload.
     */
    char dynreg_host[QM_PROVISION_DYNREG_HOST_SIZE];

    /**
     * @brief Timezone value carried by the BLE provisioning payload.
     */
    int8_t timezone;

    /**
     * @brief Audio resource URI carried by the BLE provisioning payload.
     */
    char audio_uri[QM_PROVISION_AUDIO_URI_SIZE];

    /**
     * @brief App token/authentication code carried by the BLE payload.
     */
    char authcode[QM_PROVISION_AUTHCODE_SIZE];

    /**
     * @brief Number of valid bytes in @ref authcode, excluding the trailing '\0'.
     */
    uint8_t authcode_len;

    /**
     * @brief Token type supplied by the version 2 BLE provisioning protocol.
     */
    uint8_t token_type;

    /**
     * @brief Environment type carried by the BLE APINFO payload.
     */
    uint8_t env_type;

    /**
     * @brief Whether @ref env_type was carried by the BLE APINFO payload.
     */
    qm_bool env_type_valid;
} qm_provision_recv_t;

/**
 * @brief BLE provisioning data receive callback.
 *
 * @param[in] handle   Provisioning session handle.
 * @param[in] recv     Provisioning data received from the BLE app. The pointer
 *                     and its fields are valid only for the callback duration.
 * @param[in] userdata User context configured in @ref qm_provision_config_t.
 */
typedef void (*qm_provision_recv_handler_t)(qm_provision_handle_t handle,
                                            const qm_provision_recv_t *recv,
                                            void *userdata);

/**
 * @brief BLE provisioning session configuration.
 */
typedef struct {
    /**
     * @brief Product ID used by the BLE authentication and advertising payload.
     */
    uint32_t product_id;

    /**
     * @brief Product secret used by BLE authentication. The string is borrowed
     *        and must remain valid until the session is destroyed.
     */
    const char *product_secret;

    /**
     * @brief Advertising CID prefix. Use NULL to select the default value. A
     *        non-NULL string is borrowed until the session is destroyed.
     */
    const char *adv_cid;

    /**
     * @brief Callback invoked when BLE provisioning data is received.
     */
    qm_provision_recv_handler_t recv_handler;

    /**
     * @brief User context passed back to @ref qm_provision_recv_handler_t.
     */
    void *userdata;
} qm_provision_config_t;

/**
 * @brief Create a BLE provisioning session.
 *
 * @param[in] config Provisioning configuration.
 *
 * @return Provisioning session handle on success, NULL on failure.
 */
qm_provision_handle_t qm_provision_create(const qm_provision_config_t *config);

/**
 * @brief Destroy a BLE provisioning session.
 *
 * @param[in] handle Provisioning session handle.
 */
void qm_provision_destroy(qm_provision_handle_t handle);

/**
 * @brief Start BLE provisioning advertising.
 *
 * @param[in] handle Provisioning session handle.
 *
 * If a previous stop is still waiting for BLE disconnection, this function
 * returns `-QM_EBUSY`; the caller may retry after disconnection completes.
 *
 * @return QM_EOK on success, negative error code on failure.
 */
int32_t qm_provision_start(qm_provision_handle_t handle);

/**
 * @brief Stop BLE provisioning advertising while retaining the session handle.
 *
 * @param[in] handle Provisioning session handle.
 *
 * The handle can be started again or released with @ref qm_provision_destroy.
 *
 * @return QM_EOK on success, negative error code on failure.
 */
int32_t qm_provision_stop(qm_provision_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* QM_PROVISION_H */
