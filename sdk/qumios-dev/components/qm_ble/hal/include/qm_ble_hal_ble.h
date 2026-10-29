#ifndef _QM_BLE_HAL_BLE_H_
#define _QM_BLE_HAL_BLE_H_

#include "qm.h"
#include "qm_ble_bzopt.h"

/* UUIDs */
#ifndef BLE_UUID_QMS_SERVICE
#define BLE_UUID_QMS_SERVICE 0xFEB7 /* The UUID of the xiaojiang IOT Service. */
#endif

#ifndef BLE_UUID_QMS_RC
#define BLE_UUID_QMS_RC 0xFEA6 /* The UUID of the "Read Characteristics" Characteristic. */
#endif

#ifndef BLE_UUID_QMS_WC
#define BLE_UUID_QMS_WC 0xFEA7 /* The UUID of the "Write Characteristics" Characteristic. */
#endif

#ifndef BLE_UUID_QMS_IC
#define BLE_UUID_QMS_IC 0xFEA8 /* The UUID of the "Indicate Characteristics" Characteristic. */
#endif

#ifndef BLE_UUID_QMS_WWNRC
#define BLE_UUID_QMS_WWNRC 0xFEA9 /* The UUID of the "Write WithNoRsp Characteristics" Characteristic. */
#endif

#ifndef BLE_UUID_QMS_NC
#define BLE_UUID_QMS_NC  0xFEAA /* The UUID of the "Notify Characteristics" Characteristic. */
#endif

#define QMS_BT_MAC_LEN 6

#define ABIT(n) (1 << n)

/* Characteristic Properties Bit field values */
typedef enum
{
    /** @def QMS_GATT_CHRC_BROADCAST
     *  @brief Characteristic broadcast property.
     *
     *  If set, permits broadcasts of the Characteristic Value using Server
     *  Characteristic Configuration Descriptor.
     */
    QMS_GATT_CHRC_BROADCAST = ABIT(0),
    /** @def QMS_GATT_CHRC_READ
     *  @brief Characteristic read property.
     *
     *  If set, permits reads of the Characteristic Value.
     */
    QMS_GATT_CHRC_READ = ABIT(1),
    /** @def QMS_GATT_CHRC_WRITE_WITHOUT_RESP
     *  @brief Characteristic write without response property.
     *
     *  If set, permits write of the Characteristic Value without response.
     */
    QMS_GATT_CHRC_WRITE_WITHOUT_RESP = ABIT(2),
    /** @def QMS_GATT_CHRC_WRITE
     *  @brief Characteristic write with response property.
     *
     *  If set, permits write of the Characteristic Value with response.
     */
    QMS_GATT_CHRC_WRITE = ABIT(3),
    /** @def QMS_GATT_CHRC_NOTIFY
     *  @brief Characteristic notify property.
     *
     *  If set, permits notifications of a Characteristic Value without
     *  acknowledgment.
     */
    QMS_GATT_CHRC_NOTIFY = ABIT(4),
    /** @def QMS_GATT_CHRC_INDICATE
     *  @brief Characteristic indicate property.
     *
     * If set, permits indications of a Characteristic Value with
     * acknowledgment.
     */
    QMS_GATT_CHRC_INDICATE = ABIT(5),
    /** @def QMS_GATT_CHRC_AUTH
     *  @brief Characteristic Authenticated Signed Writes property.
     *
     *  If set, permits signed writes to the Characteristic Value.
     */
    QMS_GATT_CHRC_AUTH = ABIT(6),
    /** @def QMS_GATT_CHRC_EXT_PROP
     *  @brief Characteristic Extended Properties property.
     *
     * If set, additional characteristic properties are defined in the
     * Characteristic Extended Properties Descriptor.
     */
    QMS_GATT_CHRC_EXT_PROP = ABIT(7)
} qms_char_prop_t;

/* GATT attribute permission bit field values */
typedef enum
{
    /** No operations supported, e.g. for notify-only */
    QMS_GATT_PERM_NONE = 0,
    /** Attribute read permission. */
    QMS_GATT_PERM_READ = ABIT(0),
    /** Attribute write permission. */
    QMS_GATT_PERM_WRITE = ABIT(1),
    /* Attribute read permission with encryption. */
    QMS_GATT_PERM_READ_ENCRYPT = ABIT(2),
    /* Attribute write permission with encryption. */
    QMS_GATT_PERM_WRITE_ENCRYPT = ABIT(3),
    /* Attribute read permission with authentication. */
    QMS_GATT_PERM_READ_AUTHEN = ABIT(4),
    /* Attribute write permission with authentication. */
    QMS_GATT_PERM_WRITE_AUTHEN = ABIT(5),
    /* Attribute prepare write permission. */
    QMS_GATT_PERM_PREPARE_WRITE = ABIT(6)
} qms_attr_perm_t;

typedef enum
{
    QMS_ERR_SUCCESS = 0,
    QMS_ERR_STACK_FAIL,
    QMS_ERR_MEM_FAIL,
    QMS_ERR_INVALID_ADV_DATA,
    QMS_ERR_ADV_FAIL,
    QMS_ERR_STOP_ADV_FAIL,
    QMS_ERR_GATT_INDICATE_FAIL,
    QMS_ERR_GATT_NOTIFY_FAIL,
    /* Add more QM error code hereafter */
} qms_err_t;

typedef enum
{
    QMS_UUID_TYPE_16,
    QMS_UUID_TYPE_32,
    QMS_UUID_TYPE_128,
} qms_uuid_type_t;

typedef struct
{
    uint8_t type;
} qms_uuid_t;

typedef struct
{
    qms_uuid_t uuid;
    uint16_t   val;
} qms_uuid_16_t;

typedef struct
{
    qms_uuid_t uuid;
    uint32_t   val;
} qms_uuid_32_t;

typedef struct
{
    qms_uuid_t uuid;
    uint8_t    val[16];
} qms_uuid_128_t;

typedef enum
{
    QMS_CCC_VALUE_NONE     = 0,
    QMS_CCC_VALUE_NOTIFY   = 1,
    QMS_CCC_VALUE_INDICATE = 2
} qms_ccc_value_t;

#define QMS_UUID_INIT_16(value)                        \
    {                                                  \
        .uuid.type = QMS_UUID_TYPE_16, .val = (value), \
    }

#define QMS_UUID_INIT_32(value)                        \
    {                                                  \
        .uuid.type = QMS_UUID_TYPE_32, .val = (value), \
    }

#define QMS_UUID_INIT_128(value...)                       \
    {                                                     \
        .uuid.type = QMS_UUID_TYPE_128, .val = { value }, \
    }

#define QMS_UUID_DECLARE_16(value) \
    ((qms_uuid_t *)(&(qms_uuid_16_t)QMS_UUID_INIT_16(value)))
#define QMS_UUID_DECLARE_32(value) \
    ((qms_uuid_t *)(&(qms_uuid_32_t)QMS_UUID_INIT_32(value)))
#define QMS_UUID_DECLARE_128(value...) \
    ((qms_uuid_t *)(&(qms_uuid_128_t)QMS_UUID_INIT_128(value)))

typedef void (*connected_callback_t)();
typedef void (*disconnected_callback_t)();
typedef size_t (*on_char_read_t)(void *buf, uint16_t len);
typedef size_t (*on_char_write_t)(void *buf, uint16_t len);
typedef void (*on_char_ccc_change_t)(qms_ccc_value_t value);

typedef struct
{
    /* Characteristics UUID */
    qms_uuid_t *uuid;
    /* Characteristics property */
    uint8_t prop;
    /* Characteristics value attribute permission */
    uint8_t perm;
    /* Characteristics value read handler, NULL if not used */
    on_char_read_t on_read;
    /* Characteristics value write handler, NULL if not used */
    on_char_write_t on_write;
    /**
     * Characteristics value ccc changed handler.
     * Only applied to NOFITY and INDICATE type Characteristics,
     * NULL if not applied.
     */
    on_char_ccc_change_t on_ccc_change;
} qms_char_init_t;

typedef struct
{
    /* QM primamry service */
    qms_uuid_t *uuid_svc;
    /* QM's Read Characteristics */
    qms_char_init_t rc;
    /* QM's Write Characteristics */
    qms_char_init_t wc;
    /* QM's Indicate Characteristics */
    qms_char_init_t ic;
    /* QM's Write WithNoRsp Characteristics */
    qms_char_init_t wwnrc;
    /* QM's Notify Characteristics */
    qms_char_init_t nc;
    /* Callback function when bluetooth connected */
    connected_callback_t on_connected;
    /* Callback function when bluetooth disconnected */
    disconnected_callback_t on_disconnected;
} qms_bt_init_t;

typedef enum
{
    QMS_ADV_NAME_SHORT,
    QMS_ADV_NAME_FULL
} qms_adv_name_type_t;

typedef struct
{
    qms_adv_name_type_t ntype;
    char *              name;
} qms_adv_name_t;

/* DISCOVERABILITY MODES, spec v4.2, in 4.1 section */
typedef enum
{
    QMS_AD_LIMITED  = ABIT(0), /* Limited Discoverable */
    QMS_AD_GENERAL  = ABIT(1), /* General Discoverable */
    QMS_AD_NO_BREDR = ABIT(2)  /* BR/EDR not supported */
} qms_adv_flag_t;

#define MAX_VENDOR_DATA_LEN 24
typedef struct
{
    uint8_t  data[MAX_VENDOR_DATA_LEN];
    uint16_t len;
} qms_adv_vendor_data_t;

typedef struct
{
    qms_adv_flag_t        flag;
    qms_adv_name_t        name;
    qms_adv_vendor_data_t vdata;
    /* Subject to add more hereafter in the future */
} qms_adv_init_t;

enum
{
    QMS_BT_REASON_REMOTE_USER_TERM_CONN = 0,
    /* Add more supported reasons here. */
    QMS_BT_REASON_UNSPECIFIED = 0x0f
};

/**
 * API to initialize ble stack.
 * @parma[in]  qms_init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init);

/**
 * API to de-initialize ble stack.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_stack_deinit(void);

/**
 * API to send data via QM's Notify Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length);

/**
 * API to send data via QM's Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res));

/**
 * API to disconnect BLE connection.
 * @param[in]  reason  the reason to disconnect the connection.
 */
void qm_ble_disconnect(uint8_t reason);

/**
 * API to start bluetooth advertising.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv);

/**
 * API to stop bluetooth advertising.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_advertising_stop(void);

/**
 * API to start bluetooth advertising.
 * @parma[out]  mac  the uint8_t[BD_ADDR_LEN] space the save the mac address.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_get_mac(uint8_t *mac);

#ifdef EN_LONG_MTU
/**
 * API to obtain ble link MTU, if ble stack didn't provide, use DEFAULT_ATT_MTU .
 * @parma[out]  mac  the uint8_t[BD_ADDR_LEN] space the save the mac address.
 * @return     0 on success, erro code if failure.
 */

int qm_ble_get_att_mtu(uint16_t *att_mtu);
#endif

#endif
