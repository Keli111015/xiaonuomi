#ifndef QB_CORE_H
#define QB_CORE_H

#include "qm_ble_client_common.h"
#include "qm_ble_client_service.h"
#include "qm_ble_client_transport.h"

#ifdef __cplusplus
extern "C"
{
#endif

int qm_ble_client_core_init(qm_ble_client_init_t *ble_client_init);
void qm_ble_client_core_reset(uint8_t conn_id);
void qm_ble_client_core_event_notify(uint8_t conn_id, uint8_t evt_type, uint8_t *data, uint16_t length);
void qm_ble_client_core_handle_err(uint8_t conn_id, uint8_t src, uint8_t code);

#ifdef __cplusplus
}
#endif

#endif // QB_CORE_H
