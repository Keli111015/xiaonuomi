#ifndef QB_QM_H
#define QB_QM_H


#include "qm_ble_common.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    uint16_t mtu;
}qm_ble_service_init_t;

typedef struct {
    uint16_t chrc_handle;
    uint16_t value_handle;
    uint16_t user_desc_handle;
    uint16_t cccd_handle;
    uint16_t sccd_handle;
} qm_ble_gatts_char_handles_t;

typedef struct {
    uint16_t service_handle;
    qm_ble_gatts_char_handles_t rc_handles;  // Handles related to Read Characteristics
    qm_ble_gatts_char_handles_t wc_handles;  // Handles related to Write Characteristics
    qm_ble_gatts_char_handles_t ic_handles;  // Handles related to Indicate Characteristics
    qm_ble_gatts_char_handles_t wwnrc_handles;  // Handles related to Write WithNoRsp Characteristics
    qm_ble_gatts_char_handles_t nc_handles;  // Handles related to Notify Characteristics
    uint16_t conn_handle;  // Handle of the current connection
    bool_t is_indication_enabled;
    bool_t is_notification_enabled;
    void *p_context;
    uint16_t max_pkt_size;
}qm_ble_service_t;


uint32_t qm_ble_service_init(const qm_ble_service_init_t * p_qms_init);
uint32_t qm_ble_service_send_notification(uint8_t * p_data, uint16_t length);
uint32_t qm_ble_service_send_indication(uint8_t * p_data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif // QB_QM_H
