#ifndef QB_QM_H
#define QB_QM_H


#include "qm_ble_client_common.h"

#ifdef __cplusplus
extern "C" {
#endif


int qm_ble_client_service_init(void);
int qm_ble_client_service_send_notification(uint8_t conn_id, uint8_t *p_data, uint16_t length);
int qm_ble_client_service_send_indication(uint8_t conn_id, uint8_t *p_data, uint16_t length);

#ifdef __cplusplus
}
#endif

#endif // QB_QM_H
