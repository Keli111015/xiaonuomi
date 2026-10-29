#ifndef QM_BLE_API_EXPORT_H
#define QM_BLE_API_EXPORT_H

#if defined(__cplusplus) /* If this is a C++ compiler, use C linkage */
extern "C"
{
#endif

#include "qm_ble_bzopt.h"
#include "qm_ble_log.h"

#define BD_ADDR_LEN      (6)      /**< Length of Bluetooth Device Address. */
#define STR_DEV_SEC_LEN  (32 + 1) /**< Length of device secret. */
#define STR_PROD_SEC_LEN (32 + 1) /**< Length of product secret. */
#define STR_DID_LEN      (12 + 1) /**< Length of device id */
#define STR_VER_LEN      (8 + 1) /**< Length of device ver */


#define MAX_QM_TOKEN_PARAM_LEN 64
#define MAX_QM_URL_LEN    64

/***** BLE STATUS ******/

typedef enum {
    QM_BLE_CONNECTED,                                // connect with phone success
    QM_BLE_DISCONNECTED,                             // lost connection with phone
    QM_BLE_AUTHENTICATED,                            // success authentication, security key auth
    QM_BLE_TX_DONE,                                  // send user payload data complete
    QM_BLE_USER_BIND,                            // user binded
    QM_BLE_USER_UNBIND,                          // user unbind
    QM_BLE_USER_SIGNED,                          // user AuthCode sign pass
    QM_BLE_NONE
} qm_ble_event_t;


enum{
    IOT_REGION_TYPE_ID,
    IOT_REGION_TYPE_URL,
};

typedef struct{
    uint8_t main_ver;
    uint8_t sub_ver;
    uint8_t fix_ver;
}qm_ble_ver_t;

typedef struct {
    uint8_t protocol_ver;                     // ble protocol version
    char    ssid[32 + 1];                     // ble ap ssid
    char    pw[64 * 2 + 1];                   // ap password
    uint8_t bssid[6];
    uint8_t apptoken_len;
    uint8_t apptoken[MAX_QM_TOKEN_PARAM_LEN];
    uint8_t token_type;
    uint8_t region_type;
    int     region_id;
    char    region_url[MAX_QM_URL_LEN];
    uint8_t rand[3];
    uint8_t env_type;
    uint8_t env_type_valid;
} qm_ble_apinfo_t;

typedef struct {
    uint32_t product_id;
    char *product_key;
    char *product_secret;
    char *device_id;
    char *device_secret;
    uint8_t *dev_adv_mac;
    qm_ble_ver_t ble_ver;
    qm_ble_ver_t mcu_ver;
} qm_ble_dev_info_t;


typedef enum{
    QM_BLE_HASH_NONE   = 0,
    QM_BLE_HASH_MD5    = 1,
    QM_BLE_HASH_SHA1   = 2,
    QM_BLE_HASH_CRC32  = 3,
}qm_ble_hash_t;

enum {
    QM_BLE_HASH_MD5_SIZE      = 16,
    QM_BLE_HASH_SHA1_SIZE     = 20,
    QM_BLE_HASH_CRC32_SIZE    = 4,
    QM_BLE_HASH_MAX_SIZE      = 64,
};

typedef enum{
    QM_BLE_OTA_REQ,
    QM_BLE_OTA_DATA,
    QM_BLE_OTA_DATA_LOOP,
    QM_BLE_OTA_DATA_PROGRESS,
    QM_BLE_OTA_VERIFY,

}qm_ble_ota_event_t;

typedef enum{

    QM_BLE_OTA_TYPE_BLE = 1,
    QM_BLE_OTA_TYPE_MCU = 2,
}qm_ble_ota_type_t;

typedef enum{
    QM_BLE_DEV_NORMAL,
    QM_BLE_DEV_LOWBATTERY,
    QM_BLE_DEV_BUSY,
    QM_BLE_DEV_VER_ERR,
    QM_BLE_DEV_VERIFY_ERR
}qm_ble_ota_reason_t;

typedef struct{
    qm_ble_hash_t hash;
    uint8_t hash_len;
    uint8_t hash_value[QM_BLE_HASH_MAX_SIZE];
    uint32_t file_size;
}qm_ble_ota_notify_t;

typedef struct{
    uint8_t percent;
}qm_ble_ota_progress_t;


/**
 * @brief Callback when device status changed.
 *
 * @param[in] event @n ble event.
 * @return None.
 * @see None.
 * @note This API should be implemented by user, and will be called by SDK
 *       when device statuc changed, e.g. bluetooth connection status change.
 */
typedef void (*qm_ble_dev_status_changed_cb)(qm_ble_event_t event);

/**
 * @brief Callback when there is device status to set.
 *
 * @param[in] msg_id @n msg id of the data.
 * @param[in] cmd @n cmd of the data.
 * @param[in] buffer @n The data to device set.
 * @param[in] length @n Length of the data.
 * @return None.
 * @see None.
 * @note This API should be implemented by user and will be called by SDK.
 */
typedef void (*qm_ble_set_dev_status_cb)(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length);

/**
 * @brief Callback when there is device status to get.
 *
 * @param[in] msg_id @n msg id of the data.
 * @param[in] cmd @n cmd of the data.
 * @param[in] buffer @n The data to device get.
 * @param[in] length @n Length of the data.
 * @return None.
 * @see None.
 * @note This API should be implemented by user and will be called by SDK.
 */
typedef void (*qm_ble_get_dev_status_cb)(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length);

/**
 * @brief Callback when there is networkconfig info to get.
 *
 * @param[out] buffer @n The data struct of AP info.
 * @return None.
 * @see None.
 * @note This API should be implemented by user and will be called by SDK.
 */
#if (QB_VERSION < 3)
typedef void (*qm_ble_apinfo_ready_cb)(qm_ble_apinfo_t *ap);
#else
typedef void (*qm_ble_apinfo_ready_cb)(uint8_t *buffer, uint32_t length);
#endif

/**
 * @brief Callback when there is device post to response.
 *
 * @param[in] msg_id @n msg id of the data.
 * @param[in] cmd @n cmd of the data.
 * @param[in] buffer @n The data to response.
 * @param[in] length @n Length of the data.
 * @return None.
 * @see None.
 * @note This API should be implemented by user and will be called by SDK.
 */
typedef void (*qm_ble_response_cb)(uint8_t msg_id, uint8_t cmd, uint8_t *buffer, uint32_t length);
/**
 * @brief Callback when there is device extinfo to set.
 *
 * @param[in] msg_id @n msg id of the data.
 * @param[in] cmd @n cmd of the data.
 * @param[in] buffer @n The data to extra infomation.
 * @param[in] length @n Length of the data.
 * @return None.
 * @see None.
 * @note This API should be implemented by user and will be called by SDK.
 */
typedef void (*qm_ble_extinfo_cb)(uint8_t msg_id, uint8_t cmd,uint8_t *buffer, uint32_t length);
/**
 * @brief Callback when there is device ota to notify.
 *
 * @param[in] ota_type @n The type to ota.
 * @param[in] event @n ota event
 * @param[in] buffer @n The data to be set.
 * @param[in] length @n Length of the data.
 * @return ota reason @n qm_ble_ota_reason_t
 * @see None.
 * @note This API should be implemented by user and will be called by SDK.
 */
typedef int (*qm_ble_ota_cb)(qm_ble_ota_type_t ota_type, qm_ble_ota_event_t event, uint8_t *buffer, uint32_t length);

/**
 * @brief Callback when there is production test to notify.
 *
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n The data to production test.
 * @param[in] length @n Length of the data.
 * @return ota reason @n qm_ble_ota_reason_t
 * @see None.
 * @note This API should be implemented by user and will be called by SDK.
 */
typedef int (*qm_ble_prodtst_cb)(uint8_t msg_id, uint8_t *buffer, uint32_t length);

/**
 * @brief set mcu reboot time when mcu upgrade successfully
 *
 * @param[in] time_s @n reboot time,unit second
 * @return error code.
 * @see None.
 * @note This API should be called before ota request
 */
uint32_t qm_ble_mcu_reboot_time_set(uint8_t time_s);

/**
 * @brief set mcu version
 *
 * @param[in] mcu_ver @n The version to mcu.
 * @return error code.
 * @see None.
 * @note This API should be called before app acquire version
 */
uint32_t qm_ble_mcu_ver_set(qm_ble_ver_t *mcu_ver);


/**
 * @brief response data ack every loop 
 *
 * @return error code.
 * @see None.
 */
uint32_t qm_ble_ota_data_async_rsp(void);

/**
 * This structure includes the information which is 
 * required to initialize the SDK.
 */
typedef struct 
{
    uint8_t         bd_addr[BD_ADDR_LEN];
    uint8_t         bd_adv_addr[BD_ADDR_LEN]; 
    uint32_t        product_id;
    char            product_secret[STR_PROD_SEC_LEN];
    uint8_t         product_secret_len;
    char            device_id[STR_DID_LEN];
    uint8_t         device_id_len;
    char            device_secret[STR_DEV_SEC_LEN];
    uint8_t         device_secret_len;
    qm_ble_ver_t    ble_ver;
    qm_ble_ver_t    mcu_ver;
    qm_ble_dev_status_changed_cb status_changed_cb;
    qm_ble_set_dev_status_cb     set_cb;
    qm_ble_get_dev_status_cb     get_cb;
    qm_ble_extinfo_cb            ext_cb;
    qm_ble_apinfo_ready_cb       apinfo_cb;
    qm_ble_response_cb           rsp_cb;
    qm_ble_ota_cb                ota_cb;
}qm_ble_dev_conf_t;


/**
 * @brief Init qm_ble SDK services.
 *
 * @param[in] dev_info @n Device information
 * @return null.
 * @see None.
 */
uint32_t qm_ble_init(qm_ble_dev_info_t *info, 
                   qm_ble_dev_status_changed_cb status_change_cb,
                   qm_ble_set_dev_status_cb set_cb,
                   qm_ble_get_dev_status_cb get_cb,
                   qm_ble_response_cb rsp_cb,
                   qm_ble_extinfo_cb ext_cb,
                   qm_ble_apinfo_ready_cb apinfo_rx_cb,
                   qm_ble_ota_cb ota_cb);


/**
 * @brief Start qm_ble SDK services.
 *
 * @param[in] dev_info @n Device information
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API is called by user to initialize and start qm_ble services.
 */
uint32_t qm_ble_start(qm_ble_dev_conf_t *dev_conf);

/**
 * @brief Stop qm_ble services.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API is called by user to stop the qm_ble services.
 */
uint32_t qm_ble_end(void);

/**
 * @brief Post device status with cmd.
 *
 * @param[in] cmd @n cmda to post.0:default, other:for internal use
 * @param[in] buffer @n Data to post.
 * @param[in] length @n Length of the data.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API can be used to update date to server, in non-blocked way.
 *       This API uses ble indicate way to send the data.
 */
uint32_t qm_ble_post(uint8_t cmd, uint8_t *msg_id, uint8_t *buffer, uint32_t length);

/**
 * @brief Post data without response.
 *
 * @param[in] cmd @n cmd of the data.
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n Data to post.
 * @param[in] length @n Length of the data.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API uses ble notification way to send the data.
 */
uint32_t qm_ble_post_fast(uint8_t cmd, uint8_t *msg_id, uint8_t *buffer, uint32_t length);


/**
 * @brief Post device status with cmd.
 *
 * @param[in] cmd @n cmd of the data.
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n Data to post.
 * @param[in] length @n Length of the data.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API uses ble notification way to send the data.
 */
uint32_t qm_ble_post_async(uint8_t cmd, uint8_t *msg_id, uint8_t *buffer, uint32_t length);

/**
 * @brief Post data with response.
 *
 * @param[in] cmd @n cmd of the data.
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n Data to post.
 * @param[in] length @n Length of the data.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API uses ble notification way to send the data.
 */
uint32_t qm_ble_post_async_fast(uint8_t cmd, uint8_t *msg_id, uint8_t *buffer, uint32_t length);

/**
 * @brief Post ack data with response.
 *
 * @param[in] cmd @n cmd of the data.
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n Data to post.
 * @param[in] length @n Length of the data.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API uses ble notification way to send the data.
 */
uint32_t qm_ble_ack_async(uint8_t msg_id, uint8_t cmd, uint8_t err_code, uint8_t *buffer, uint32_t length);

/**
 * @brief Post ack data without response.
 *
 * @param[in] cmd @n cmd of the data.
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n Data to post.
 * @param[in] length @n Length of the data.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API uses ble notification way to send the data.
 */
uint32_t qm_ble_ack_async_fast(uint8_t msg_id, uint8_t cmd, uint8_t err_code, uint8_t *buffer, uint32_t length);

/**
 * @brief Append user specific data to the tail of the qm_ble adv data.
 *
 * @param[in] data @n Data to append.
 * @param[in] len @n Data length.
 * @return None.
 * @see None.
 * @note User can call this API if additional adv data is needed.
 *       qm ble SDK has its own adv data and format, find more details
 *       in qm ble spec.
 */
void qm_ble_append_adv_data(uint8_t *data, uint32_t len);

/**
 * @brief Restart BLE advertisement. This API will stop and then start the adv.
 *
 * @param None.
 * @return None.
 * @see None.
 * @note User can call this API if he/she wants to update the adv
 *       content from time to time.
 */
void qm_ble_restart_advertising();

/**
 * @brief Start BLE advertisement. This API will start the adv.
 *
 * @param[in] sub_type @n device advertising type.
 * @param[in] sec_type @n security type, per-product or per-device.
 * @param[in] bind_state @n device bind state.
 * @param[in] local_name @n device local name
 * @return result 0: success; others:err code.
 * @see None.
 * @note User can call this API if he/she wants to start the adv
 */
uint32_t qm_ble_start_advertising(uint8_t sub_type, uint8_t sec_type, uint8_t bind_state, char *local_name);

/**
 * @brief Stop BLE advertisement. This API will stop the adv.
 *
 * @param None.
 * @return result 0: success; others:err code.
 * @see None.
 * @note User can call this API if he/she wants to stop the adv
 */
uint32_t qm_ble_stop_advertising(void);

/**
 * @brief get qm_ble device's bind state.
 *
 * @param None.
 * @return bind_state 0-not bind, 1-binded.
 * @see None.
 */
uint8_t qm_ble_get_bind_state(void);

/**
 * @brief clear qm_ble device's bind state.
 *
 * @param None.
 * @return result 0: success; others:err code.
 * @see None.
 */
uint32_t qm_ble_clear_bind_info(void);

/**
 * @brief disconnect the peer connected device.
 *
 * @param None.
 * @return None.
 * @see None.
 */
void qm_ble_disconnect_ble(void);

/**
 * @brief register production test callback
 *
 * @param[in] prodtst_cb @n the callback of production test.
 * @return result 0: success; others:err code.
 * @see None.
 */
uint32_t qm_ble_prodtst_register_callback(qm_ble_prodtst_cb prodtst_cb);

/**
 * @brief Post ack data with response.
 *
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n Data to post.
 * @param[in] length @n Length of the data.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API uses ble notification way to send the data.
 */
uint32_t qm_ble_prodtst_post(uint8_t msg_id, uint8_t *buffer, uint32_t length);

#if defined(__cplusplus) /* If this is a C++ compiler, use C linkage */
}
#endif

#endif // QM_BLE_API_EXPORT_H
