#ifndef __HARMONY_BLE_HAL_H__
#define __HARMONY_BLE_HAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"
#include "qm_ble_gap.h"

/* UUIDs LSB */
#define HARMONY_BLE_UUID_OTA_SERVICE  { \
            0x00, 0x91, 0x8a, 0xef, 0x39, 0xdd, 0x84, 0xa4, \
            0xfc, 0x43, 0x77, 0xa2, 0x10, 0xe6, 0xf1, 0x15   \
        } 

#define HARMONY_BLE_UUID_SERVICE  { \
            0x00, 0x91, 0x8a, 0xef, 0x39, 0xdd, 0x84, 0xa4, \
            0xfc, 0x43, 0x77, 0xa2, 0x0, 0xe6, 0xf1, 0x15   \
        } 

#define HARMONY_BLE_UUID_READ  { \
            0x00, 0x91, 0x8a, 0xef, 0x39, 0xdd, 0x84, 0xa4, \
            0xfc,0x43, 0x77, 0xa2,0x1, 0xe6, 0xf1, 0x15     \
        } 

#define HARMONY_BLE_UUID_WRITE  { \
            0x00, 0x91, 0x8a, 0xef, 0x39, 0xdd, 0x84, 0xa4, \
            0xfc, 0x43, 0x77, 0xa2, 0x2, 0xe6, 0xf1, 0x15   \
        }

#define HARMONY_BLE_UUID_OTACTRL  { \
            0x00, 0x91, 0x8a, 0xef, 0x39, 0xdd, 0x84, 0xa4, \
            0xfc,0x43, 0x77, 0xa2,0x11, 0xe6, 0xf1, 0x15     \
        } 

#define HARMONY_BLE_UUID_OTADATA  { \
            0x00, 0x91, 0x8a, 0xef, 0x39, 0xdd, 0x84, 0xa4, \
            0xfc, 0x43, 0x77, 0xa2, 0x12, 0xe6, 0xf1, 0x15   \
        }

typedef enum
{
    HARMONY_BLE_CCC_VALUE_NONE     = 0,
    HARMONY_BLE_CCC_VALUE_NOTIFY   = 1,
    HARMONY_BLE_CCC_VALUE_INDICATE = 2
}harmony_ble_ccc_value_t;

typedef enum
{
    HARMONY_BLE_STACK_EVENT_INTT_SUCCESS,
    HARMONY_BLE_STACK_EVENT_CONNECT, 
    HARMONY_BLE_STACK_EVENT_DISCONNECT, 
    HARMONY_BLE_STACK_EVENT_DEINTT_SUCCESS,
}harmony_ble_stack_event_t;

typedef struct
{
    uint8_t pair_mode;
    uint8_t service_uuid[16];
    uint8_t service_otauuid[16];

    /* Read Characteristics */
    struct 
    {
        /* Characteristics UUID 128*/
        uint8_t uuid[16];
        /* Characteristics value read handler, NULL if not used */
        int (*on_char_read_t)(void *buf, int len);
    }rc;

    /* Write Characteristics */
    struct 
    {
        /* Characteristics UUID 128*/
        uint8_t uuid[16];
        /* Characteristics value read handler, NULL if not used */
        int (*on_char_write_t)(void *buf, int len);
    }wc;

    /* OTA CTRL Characteristics */
    struct 
    {
        /* Characteristics UUID 128*/
        uint8_t uuid[16];
        /* Characteristics value read handler, NULL if not used */
        int (*ota_ctrl_t)(void *buf, int len);
    }ota_ctrl;

    /* OTA DATA Characteristics */
    struct 
    {
        /* Characteristics UUID 128*/
        uint8_t uuid[16];
        /* Characteristics value read handler, NULL if not used */
        int (*on_data_t)(void *buf, int len);
    }ota_data;

    /* Callback function when bluetooth stack status change */
    void (*event_notify_callback_t)(harmony_ble_stack_event_t event);
}harmony_ble_init_t;

/**
 * API to initialize ble stack.
 * @parma[in]  init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
int harmony_ble_hal_gatt_stack_init(harmony_ble_init_t *ble_init);

/**
 * API to de-initialize ble stack.
 * @return     0 on success, error code if failure.
 */
int harmony_ble_hal_gatt_stack_deinit(void);

/**
 * 
 * @parma[in]  dev_name  device name.
 * @parma[in]  length  data length.
 * @return     0 on success, erro code if failure.
 */
int harmony_ble_hal_dev_name_set(char *dev_name, int len);

/**
 * API to send data via QM's Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
int harmony_ble_hal_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t pkt_sent));

/**
 * API to send data via OTA Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
int harmony_ble_hal_ota_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res));

/**
 * API to disconnect BLE connection.
 * @param[in]  reason  the reason to disconnect the connection.
 */
void harmony_ble_hal_disconnect(uint8_t reason);


#ifdef __cplusplus
}
#endif

#endif