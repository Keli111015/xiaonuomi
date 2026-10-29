#ifndef __HARMONY_BLE_HAL_H__
#define __HARMONY_BLE_HAL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"
#include "qm_ble_gap.h"


//11C8B310-80E4-4276-AFC0-F81590B2177F
#define HONOR_BLE_UUID_SERVICE  { \
            0x7F, 0x17, 0xB2, 0x90, 0x15, 0xF8, 0xC0, 0xAF, \
            0x76, 0x42, 0xE4, 0x80, 0x10, 0xB3, 0xC8, 0x11   \
        } 

//00002B01-0000-1000-8000-00805F9B34FB
#define HONOR_BLE_UUID_ALL  { \
            0x01, 0x2B \
        } 

typedef enum
{
    HONOR_BLE_CCC_VALUE_NONE     = 0,
    HONOR_BLE_CCC_VALUE_NOTIFY   = 1,
    HONOR_BLE_CCC_VALUE_INDICATE = 2
}honor_ble_ccc_value_t;

typedef enum
{
    HONOR_BLE_STACK_EVENT_INTT_SUCCESS,
    HONOR_BLE_STACK_EVENT_CONNECTED, 
    HONOR_BLE_STACK_EVENT_DISCONNECT, 
    HONOR_BLE_STACK_EVENT_DEINTT_SUCCESS,

}honor_ble_stack_event_t;

typedef struct
{
    uint8_t pair_mode;
    uint8_t *service_uuid;
    uint8_t *service_normal_uuid;
    void (*txdone)(uint8_t res);
    /* Read Characteristics */
    struct 
    {
        uint8_t *uuid;
        /* Characteristics value read handler, NULL if not used */
        int (*on_char_read_t)(void *buf, int len);
        /* Characteristics value read handler, NULL if not used */
        int (*on_char_write_t)(void *buf, int len);

        int (*on_char_write_without_rsp_t)(void *buf, int len);

        int (*on_char_notify_t)(void *buf, int len);

        int (*on_char_indicate_t)(void *buf, int len);

    }honor_chara;

    /* Read Characteristics */
    struct 
    {
        uint8_t *uuid;
        /* Characteristics value read handler, NULL if not used */
        int (*on_char_read_t)(void *buf, int len);
        /* Characteristics value read handler, NULL if not used */
        int (*on_char_write_t)(void *buf, int len);

        int (*on_char_write_without_rsp_t)(void *buf, int len);

        int (*on_char_notify_t)(void *buf, int len);

        int (*on_char_indicate_t)(void *buf, int len);

    }normal_chara;

    /* Callback function when bluetooth stack status change */
    void (*event_notify_callback_t)(honor_ble_stack_event_t event);
}honor_ble_init_t;

/**
 * API to initialize ble stack.
 * @parma[in]  init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
int honor_ble_hal_gatt_stack_init(honor_ble_init_t *ble_init);

/**
 * API to de-initialize ble stack.
 * @return     0 on success, error code if failure.
 */
int honor_ble_hal_gatt_stack_deinit(void);

/**
 * 
 * @parma[in]  dev_name  device name.
 * @parma[in]  length  data length.
 * @return     0 on success, erro code if failure.
 */
int honor_ble_hal_dev_name_set(char *dev_name, int len);

/**
 * API to send data via QM's Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
int honor_ble_hal_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t pkt_sent));

/**
 * API to send data via OTA Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
int honor_ble_hal_normal_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res));

/**
 * API to disconnect BLE connection.
 * @param[in]  reason  the reason to disconnect the connection.
 */
void honor_ble_hal_disconnect(uint8_t reason);

// void honor_start_adv(void);
int honor_start_advertising(void);
void honor_adv_data_set(uint8_t *data, uint8_t len);
#ifdef __cplusplus
}
#endif

#endif