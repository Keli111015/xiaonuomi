#ifndef _QM_IOT_H_
#define _QM_IOT_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"

#define QM_LOG_MODULE_MCU 1

bool mi_get_pair_complete_status(void);
bool mi_get_connected_status(void);


#ifdef __cplusplus
}
#endif


#endif /* QM_BLE_GAP_H */


