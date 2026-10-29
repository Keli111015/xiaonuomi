#ifndef QB_COMMON_H
#define QB_COMMON_H

#include "qm_ble_bzopt.h"

/* qm ble Bluetooth and Device specified definition */
#define QB_BT_MAC_LEN                             6
#define QB_DEV_PRODUCT_ID_LEN                     8
#define QB_DEV_PRODUCT_KEY_LEN                    11
#define QB_DEV_PRODUCT_SECRET_LEN                 32
#define QB_DEV_MAX_DEVICE_NAME_LEN                32
#define QB_DEV_DEVICE_SECRET_LEN                  32
#define QB_DEV_RANDOM_LEN                         16


/* qm ble core profile specified definition */

#define QM_BLE_CMD_AUTH                 0x01
#define QM_BLE_CMD_INIT                 0x02
#define QM_BLE_CMD_SET                  0x81
#define QM_BLE_CMD_REPORT               0x03
#define QM_BLE_CMD_GET                  0x82
#define QM_BLE_CMD_ENTER_BACKGROUD      0x83
#define QM_BLE_CMD_GET_SNAPSHOT         0x84
#define QM_BLE_CMD_APINFO               0x85
#define QM_BLE_CMD_SET_RAND             0x86
#define QM_BLE_CMD_BIND_NOTIFY          0x87

#define QM_BLE_CMD_OTA_QUERY_VER        0xA0
#define QM_BLE_CMD_OTA_REQUEST          0xA1
#define QM_BLE_CMD_OTA_DATA             0xA2
#define QM_BLE_CMD_OTA_VERIFY           0xA3

#define QM_BLE_CMD_PRODTST              0xB0

/*communication Error*/
#define QM_BLE_COM_NO_ERR                         0
#define QM_BLE_COM_ERR_SYSTEM                     1
#define QM_BLE_COM_ERR_NEED_AUTH                  2
#define QM_BLE_COM_ERR_DECODE                     3
#define QM_BLE_COM_ERR_INVALID_PARA               4
#define QM_BLE_COM_ERR_AUTH                       5
#define QM_BLE_COM_ERR_LOW_BATTERY                6
#define QM_BLE_COM_ERR_VER                        7
#define QM_BLE_COM_ERR_BUSY                       8
#define QM_BLE_COM_ERR_VERIFY                     9




typedef uint8_t qm_ble_ret_code_t;

/* Error codes internal. */
#define QB_SUCCESS                                0
#define QB_EINVALIDPARAM                          1
#define QB_EDATASIZE                              2
#define QB_EINVALIDSTATE                          3
#define QB_EGATTNOTIFY                            4
#define QB_EGATTINDICATE                          5
#define QB_ETIMEOUT                               6
#define QB_EBUSY                                  7
#define QB_EINVALIDDATA                           8
#define QB_EINTERNAL                              9
#define QB_EINVALIDADDR                           10
#define QB_ENOTSUPPORTED                          11
#define QB_ENOMEM                                 12
#define QB_EFORBIDDEN                             13
#define QB_ENULL                                  14
#define QB_EINVALIDLEN                            15
#define QB_EINVALIDTLV                            16
#define QB_EMEM                                   17
#define QB_EINIT                                  18
#define QB_EFLASH                                 19


#define QB_ERR_MASK                               0xf0
#define QB_TRANS_ERR                              0x10
#define QM_ERROR_SRC_TRANSPORT_TX_TIMER          0x10
#define QM_ERROR_SRC_TRANSPORT_RX_TIMER          0x11
#define QM_ERROR_SRC_TRANSPORT_1ST_FRAME         0x12
#define QM_ERROR_SRC_TRANSPORT_OTHER_FRAMES      0x13
#define QM_ERROR_SRC_TRANSPORT_ENCRYPTED         0x14
#define QM_ERROR_SRC_TRANSPORT_RX_BUFF_SIZE      0x15
#define QM_ERROR_SRC_TRANSPORT_PKT_CFM_SENT      0x16
#define QM_ERROR_SRC_TRANSPORT_FW_DATA_DISC      0x17
#define QM_ERROR_SRC_TRANSPORT_SEND              0x18

#define QB_AUTH_ERR                              0x20
#define QM_ERROR_AUTH_CIPHER_RPT                 0x20
#define QM_ERROR_SRC_AUTH_PROC_TIMER_0           0x21
#define QM_ERROR_SRC_AUTH_PROC_TIMER_1           0x22
#define QM_ERROR_SRC_AUTH_PROC_TIMER_2           0x23
#define QM_ERROR_SRC_AUTH_SVC_ENABLED            0x24
#define QM_ERROR_SRC_AUTH_SEND_ERROR             0x25
#define QM_ERROR_SRC_AUTH_SEND_KEY               0x26

#define QB_EXTCMD_ERR                            0x30
#define QM_ERROR_SRC_TYPE_EXT                    0x30
#define QM_ERROR_SRC_EXT_SEND_RSP                0x31

#define QB_BIND_ERR                               0x40
#define QB_ERROR_AC_AS_DATA_LEN                   0x40
#define QB_ERROR_AC_AS_NO_PERMIT                  0x41
#define QB_ERROR_AC_AS_DELETE                     0x42
#define QB_ERROR_AC_AS_STORE                      0x43
#define QB_ERROR_AUTH_DATA                        0x44
#define QB_ERROR_AUTH_SIGN                        0x45

#define QB_OTA_ERR                                0x50
#define QM_ERROR_SRC_OTA_TIMER_0                  0x50


#define BLE_CONN_HANDLE_INVALID                   0xffff
#define BLE_CONN_HANDLE_MAGIC                     0x1234

/* qm ble sign and kv-key related definition */
#define QB_PRODUCT_ID_STR                        "productId"
#define QB_DEVICE_NAME_STR                        "deviceName"
#define QB_DEVICE_SECRET_STR                      "deviceSecret"
#define QB_PRODUCT_KEY_STR                        "productKey"
#define QB_PRODUCT_SECRET_STR                     "productSecret"

enum {
    QB_EVENT_CONNECTED,                  // BLE connect
    QB_EVENT_DISCONNECTED,               // BLE disconnect
    QB_EVENT_AUTHENTICATED,              // Authenticated
    QB_EVENT_TX_DONE,                    // User payload tx done
    QB_EVENT_TX_DONE_TIMEOUT,            // User payload tx done
    QB_EVENT_RX_INFO,                    // User payload received
    QB_EVENT_APINFO,                     // Get AP info data, for ble
};

// User bind data operaton result
enum {
    QB_AC_AS_ADD,
    QB_AC_AS_UPDATE,
    QB_AC_AS_DELETE,
};

// User sign data operaton result
enum {
    QB_AUTH_SIGN_NO_CHECK_PASS,
    QB_AUTH_SIGN_CHECK_PASS,
};

typedef struct {
    uint8_t *p_data;
    uint16_t length;
}qm_ble_data_t;

typedef struct {
    uint8_t type;
    qm_ble_data_t rx_data;
}qm_ble_event_info_t;

typedef void (*qm_ble_event_handler_t)(qm_ble_event_info_t *p_event);

typedef struct {
    qm_ble_event_handler_t event_handler;
    uint32_t   product_id;
    qm_ble_data_t product_key;                // PK 11 to 20 bytes)
    qm_ble_data_t product_secret;             // secret 16 to 40 bytes
    qm_ble_data_t device_id;                // DN 20 to 32 bytes
    qm_ble_data_t device_secret;              // secret 16 to 40 bytes
    uint8_t *adv_mac;                      // mac address filled in qm adv data(maybe bt addr or wifi mac)
    uint32_t transport_timeout;            // Timeout of Tx/Rx, in number of ms. 0 if not used.
    uint16_t max_mtu;                      // Maximum MTU.
    uint8_t  *user_adv_data;               // User's adv data, if any.
    uint32_t user_adv_len;                 // User's adv data length
} qm_ble_init_t;

typedef uint32_t (*qm_ble_tx_func_t)(uint8_t cmd, uint8_t *p_data, uint16_t length);

#endif  // QB_COMMON_H
