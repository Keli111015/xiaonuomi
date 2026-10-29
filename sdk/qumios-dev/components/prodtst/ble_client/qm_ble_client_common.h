#ifndef QM_BLE_CLIENT_COMMON_H
#define QM_BLE_CLIENT_COMMON_H

#include "qm_types.h"

#if defined(BLE_4_0)
#define QM_BLUETOOTH_VER 0x00
#define QM_BLE_MAX_SUPPORTED_MTU 23
#elif defined(BLE_4_2)
#define QM_BLUETOOTH_VER 0x01
#define QM_BLE_MAX_SUPPORTED_MTU 247
#elif defined(BLE_5_0)
#define QM_BLUETOOTH_VER 0x10
#define QM_BLE_MAX_SUPPORTED_MTU 247
#else
#define QM_BLUETOOTH_VER 0x00
#define QM_BLE_MAX_SUPPORTED_MTU 23
#endif

#define QM_BLE_MIN(a, b) (a)<(b)? (a) : (b)

#define QM_BLE_CLIENT_TRANSPORT_TIMEOUT         5000

#define QM_BLE_CLIENT_CMD_PRODTST              0xB0

#ifdef QM_BLE_CLIENT_LONG_MTU
#define QM_BLE_CLIENT_TRANSPORT_VER              1
#else
#define QM_BLE_CLIENT_TRANSPORT_VER              0
#endif

#define QM_BLE_CLIENT_ATT_HDR_SIZE               3
#define QM_BLE_CLIENT_FRAME_HDR_SIZE             4
#define QM_BLE_CLIENT_ENCRY_BLOCK_LENGTH         16     // aes 128, 16 byte a block
#define QM_BLE_CLIENT_MAX_FRAME_NUMBER           16     // user payload max frame number
#define QM_BLE_CLIENT_GATT_MTU_SIZE_DEFAULT      23     // connect init before mtu exchange, use default mtu size
#define QM_BLE_CLIENT_GATT_MTU_SIZE_MAX          247    // when use long mtu, adjust this max mtu value with BLE stack config
#define QM_BLE_CLIENT_GATT_MTU_SIZE_LIMIT        103

/* Larger MTU size consumes more memory. If out of memory, you need to reduce the MTU size */
#define QM_BLE_CLIENT_FRAME_SIZE_DEFAULT         (QM_BLE_CLIENT_GATT_MTU_SIZE_DEFAULT - QM_BLE_CLIENT_ATT_HDR_SIZE - QM_BLE_CLIENT_FRAME_HDR_SIZE)
#define QM_BLE_CLIENT_FRAME_SIZE_MAX             (QM_BLE_CLIENT_GATT_MTU_SIZE_MAX - QM_BLE_CLIENT_ATT_HDR_SIZE - QM_BLE_CLIENT_FRAME_HDR_SIZE)
#define QM_BLE_CLIENT_FRAME_SIZE_LIMIT           (QM_BLE_CLIENT_GATT_MTU_SIZE_LIMIT - QM_BLE_CLIENT_ATT_HDR_SIZE - QM_BLE_CLIENT_FRAME_HDR_SIZE)

#ifdef EN_QM_BLE_CLIENT_LONG_MTU
#define QM_BLE_CLIENT_MAX_PAYLOAD_SIZE           (QM_BLE_CLIENT_FRAME_SIZE_MAX * QM_BLE_CLIENT_MAX_FRAME_NUMBER)
#else
#define QM_BLE_CLIENT_MAX_PAYLOAD_SIZE           (QM_BLE_CLIENT_FRAME_SIZE_DEFAULT * QM_BLE_CLIENT_MAX_FRAME_NUMBER)
#endif  


#define QM_BLE_CLIENT_ERR_MASK                               0xf0
#define QM_BLE_CLIENT_TRANS_ERR                              0x10
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_TX_TIMER           0x10
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_RX_TIMER           0x11
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_1ST_FRAME          0x12
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_OTHER_FRAMES       0x13
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_ENCRYPTED          0x14
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_RX_BUFF_SIZE       0x15
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_PKT_CFM_SENT       0x16
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_FW_DATA_DISC       0x17
#define QM_BLE_CLIENT_ERROR_SRC_TRANSPORT_SEND               0x18

#define QM_BLE_CLIENT_EXTCMD_ERR                             0x30


/* Error codes internal. */
#define QM_BLE_CLIENT_SUCCESS                                0
#define QM_BLE_CLIENT_EINVALIDPARAM                          1
#define QM_BLE_CLIENT_EDATASIZE                              2
#define QM_BLE_CLIENT_EINVALIDSTATE                          3
#define QM_BLE_CLIENT_EGATTNOTIFY                            4
#define QM_BLE_CLIENT_EGATTINDICATE                          5
#define QM_BLE_CLIENT_ETIMEOUT                               6
#define QM_BLE_CLIENT_EBUSY                                  7
#define QM_BLE_CLIENT_EINVALIDDATA                           8
#define QM_BLE_CLIENT_EINTERNAL                              9
#define QM_BLE_CLIENT_EINVALIDADDR                           10
#define QM_BLE_CLIENT_ENOTSUPPORTED                          11
#define QM_BLE_CLIENT_ENOMEM                                 12
#define QM_BLE_CLIENT_EFORBIDDEN                             13
#define QM_BLE_CLIENT_ENULL                                  14
#define QM_BLE_CLIENT__EINVALIDLEN                           15
#define QM_BLE_CLIENT_EINVALIDTLV                            16
#define QM_BLE_CLIENT_EMEM                                   17
#define QM_BLE_CLIENT_EINIT                                  18
#define QM_BLE_CLIENT_EFLASH                                 19

typedef enum {
    QM_BLE_CLIENT_EVENT_CONNECTED,                  // BLE connected
    QM_BLE_CLIENT_EVENT_CONNECTE_FAILED,            // BLE connect failed
    QM_BLE_CLIENT_EVENT_DISCONNECTED,               // BLE disconnect
    QM_BLE_CLIENT_EVENT_AUTHENTICATED,              // Authenticated
    QM_BLE_CLIENT_EVENT_TX_DONE,                    // User payload tx done
    QM_BLE_CLIENT_EVENT_TX_DONE_TIMEOUT,            // User payload tx done
    QM_BLE_CLIENT_EVENT_RX_INFO,                    // User payload received
    QM_BLE_CLIENT_EVENT_APINFO,                     // Get AP info data, for ble
}qm_ble_client_event_t;

typedef struct {
    uint8_t *p_data;
    uint16_t length;
}qm_ble_client_data_t;

typedef struct {
    uint8_t type;
    qm_ble_client_data_t rx_data;
}qm_ble_client_event_info_t; 

typedef void (*qm_ble_client_event_handler_t)(uint8_t conn_id, qm_ble_client_event_info_t *p_event);

typedef struct {
    qm_ble_client_event_handler_t event_handler;
    uint32_t transport_timeout;            // Timeout of Tx/Rx, in number of ms. 0 if not used.
    uint16_t max_mtu;                      // Maximum MTU.
} qm_ble_client_init_t;

#endif  // QM_BLE_CLIENT_COMMON_H
