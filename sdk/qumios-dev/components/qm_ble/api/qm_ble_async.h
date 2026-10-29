#ifndef _QM_BLE_ASYNC_H
#define _QM_BLE_ASYNC_H

#include "qm_ble_hal_os.h"
#include "qm_utils_list.h"

typedef struct{
    uint8_t is_busy;
    qm_ble_mutex_t mutex;
    qm_list_t *list;
}qm_ble_async_t;

typedef struct{

    uint8_t tx_type;
    uint8_t msg_id;
    uint8_t cmd;
    uint32_t len;
    uint8_t *buffer;  
}qm_ble_async_data_t;

uint32_t qm_ble_async_deinit(void);
uint32_t qm_ble_async_init(void);
uint32_t qm_ble_async_post_internal(void);
uint32_t qm_ble_generic_post(uint8_t         is_ack, uint8_t tx_type, uint8_t *msg_id, uint8_t cmd, uint8_t *err_code, uint8_t *buffer, uint32_t length);
void qm_ble_async_done(void);
uint32_t qm_ble_async_clear(void);

#endif