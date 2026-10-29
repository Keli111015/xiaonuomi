#ifndef QM_OSAL_TASK_H
#define QM_OSAL_TASK_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

#define QM_APPS_TIMER_TASKID    9
#define QM_BLE_OSAL_TASKID  	10

uint16_t QM_BLE_ProcessEvent(uint8_t task_id, uint16_t events);
uint16_t QM_APPS_TIMER_ProcessEvent(uint8_t task_id, uint16_t events);


#ifdef __cplusplus
}
#endif


#endif /* QM_BLE_H */