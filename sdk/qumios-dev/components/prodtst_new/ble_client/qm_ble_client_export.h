#ifndef QM_BLE_API_EXPORT_H
#define QM_BLE_API_EXPORT_H

#if defined(__cplusplus) /* If this is a C++ compiler, use C linkage */
extern "C"
{
#endif


#define BD_ADDR_LEN      (6)      /**< Length of Bluetooth Device Address. */


/**
 * @brief Callback when device status changed.
 *
 * @param[in] event @n ble event.
 * @return None.
 * @see None.
 * @note This API should be implemented by user, and will be called by SDK
 *       when device statuc changed, e.g. bluetooth connection status change.
 */
typedef void (*qm_ble_client_status_changed_cb)(uint8_t conn_id, qm_ble_client_event_t event);

/**
 * @brief Callback when there is production test to notify.
 *
 * @param[in] connection id.
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n The data to production test.
 * @param[in] length @n Length of the data.
 * @return ota reason @n qm_ble_ota_reason_t
 * @see None.
 * @note This API should be implemented by user and will be called by SDK.
 */
typedef int (*qm_ble_client_prodtst_cb)(uint8_t conn_id, uint8_t msg_id, uint8_t *buffer, uint32_t length);


/**
 * @brief Init qm_ble SDK services.
 *
 * @param[in] dev_info @n Device information
 * @return null.
 * @see None.
 */
int qm_ble_client_init(qm_ble_client_status_changed_cb status_change_cb);

/**
 * @brief disconnect the peer connected device.
 *
 * @param[in] mac @n mac address
 * @param[in] connection id.
 * @return result 0: success; others:err code.
 * @see None.
 */
int qm_ble_client_open(uint8_t mac[BD_ADDR_LEN], uint8_t *conn_id);

/**
 * @brief disconnect the peer connected device.
 *
 * @param[in] connection id.
 * @return None.
 * @see None.
 */
void qm_ble_client_close(uint8_t conn_id);

/**
 * @brief register production test callback
 *
 * @param[in] prodtst_cb @n the callback of production test.
 * @return result 0: success; others:err code.
 * @see None.
 */
int qm_ble_client_prodtst_register_callback(qm_ble_client_prodtst_cb prodtst_cb);

/**
 * @brief Post ack data with response.
 *
 * @param[in] connection id.
 * @param[in] msg_id @n msg id of the data.
 * @param[in] buffer @n Data to post.
 * @param[in] length @n Length of the data.
 * @return result 0: success; others:err code.
 * @see None.
 * @note This API uses ble notification way to send the data.
 */
int qm_ble_client_prodtst_post(uint8_t conn_id, uint8_t *msg_id, uint8_t *buffer, uint32_t length);

#if defined(__cplusplus) /* If this is a C++ compiler, use C linkage */
}
#endif

#endif // QM_BLE_CLIENT_API_EXPORT_H
