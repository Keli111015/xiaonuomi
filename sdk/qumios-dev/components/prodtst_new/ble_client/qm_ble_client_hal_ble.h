#ifndef _QM_BLE_CLIENT_HAL_BLE_H_
#define _QM_BLE_CLIENT_HAL_BLE_H_

#include "qm_types.h"

/* UUIDs */
#define BLE_UUID_SERVICE 0xFEB7 /* The UUID of the xiaojiang IOT Service. */
#define BLE_UUID_RC \
    0xFEA6 /* The UUID of the "Read Characteristics" Characteristic. */
#define BLE_UUID_WC \
    0xFEA7 /* The UUID of the "Write Characteristics" Characteristic. */
#define BLE_UUID_IC \
    0xFEA8 /* The UUID of the "Indicate Characteristics" Characteristic. */
#define BLE_UUID_WWNRC                                      \
    0xFEA9 /* The UUID of the "Write WithNoRsp Characteristics" \
              Characteristic. */
#define BLE_UUID_NC \
    0xFEAA /* The UUID of the "Notify Characteristics" Characteristic. */


typedef void (*ble_connect_failed_callback_t)(void *conn_handle);
typedef void (*ble_connected_callback_t)(void *conn_handle);
typedef void (*ble_disconnected_callback_t)(void *conn_handle);
typedef int (*ble_char_read_t)(void *conn_handle, void *buf, uint16_t len);
typedef int (*ble_char_write_t)(void *conn_handle, void *buf, uint16_t len);

typedef struct
{
    /* Characteristics UUID */
    uint16_t uuid;
    /* Characteristics value read handler, NULL if not used */
    ble_char_read_t ble_read;
    /* Characteristics value write handler, NULL if not used */
    ble_char_write_t ble_write;
} qm_ble_char_init_t;

typedef struct
{
    /* QM primamry service */
    uint16_t uuid_svc;
    /* QM's Write WithNoRsp Characteristics */
    qm_ble_char_init_t wwnrc;
    /* QM's Notify Characteristics */
    qm_ble_char_init_t nc;
    /* Callback function when bluetooth connected */
    ble_connected_callback_t on_connected;
    /* Callback function when bluetooth connect failed */
    ble_connect_failed_callback_t on_connect_failed;
    /* Callback function when bluetooth disconnected */
    ble_disconnected_callback_t on_disconnected;
    
} qm_ble_client_service_init_t;

/**
 * API to initialize ble stack.
 * @parma[in]  qms_init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
int qm_ble_client_stack_init(qm_ble_client_service_init_t *ble_client_init);

/**
 * API to de-initialize ble stack.
 * @return     0 on success, error code if failure.
 */
int qm_ble_client_stack_deinit(void);

/**
 * API to send data via QM's Notify Characteristics.
 * @parma[in]  conn_handle  connnection handle.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @return     0 on success, error code if failure.
 */
int qm_ble_client_send_notification(void *conn_handle, uint8_t *p_data, uint16_t length);

/**
 * API to connect BLE connection.
 * @param[in]   * @parma[in]  conn_handle  connnection handle.
 */
void *qm_ble_client_connect(uint8_t mac[6]);

/**
 * API to disconnect BLE connection.
 * @parma[in]  conn_handle  connnection handle.
 */
void qm_ble_client_disconnect(void *conn_handle);

/**
 * API to get mac of connection.
 * @parma[in]  conn_handle  connnection handle.
 * @parma[in]  mac  mac buffer.
 */
int qm_ble_client_get_mac(void *conn_handle, uint8_t *mac);

#ifdef EN_LONG_MTU
/**
 * API to obtain ble link MTU, if ble stack didn't provide, use DEFAULT_ATT_MTU .
 * @parma[in]  conn_handle  connnection handle.
 * @parma[out]  mac  the uint8_t[BD_ADDR_LEN] space the save the mac address.
 * @return     0 on success, erro code if failure.
 */

int qm_ble_client_get_att_mtu(void *conn_handle, uint16_t *att_mtu);
#endif

#endif
