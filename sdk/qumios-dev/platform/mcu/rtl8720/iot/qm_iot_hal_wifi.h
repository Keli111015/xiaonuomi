#ifndef __QM_IOT_HAL_WIFI_H__
#define __QM_IOT_HAL_WIFI_H__

#include "qm_types.h"
#include "qm_config.h"

#include "miio_api.h"
#include "arch_get_did_ex.h"
#include "action_operation.h"
#include "operation_executor.h"
#include "property_operation.h"

int qm_iot_hal_wifi_init(void);
int qm_iot_hal_wifi_reset(void);
int qm_iot_hal_wifi_status_set(iot_generic_event_t status);
int qm_iot_hal_wifi_handle_set(void *handle);
int qm_iot_hal_wifi_adapter_prop_report(property_operation_t *operation, uint8_t num);
int qm_iot_hal_wifi_adapter_event_report(event_operation_t  *operation);

#endif
