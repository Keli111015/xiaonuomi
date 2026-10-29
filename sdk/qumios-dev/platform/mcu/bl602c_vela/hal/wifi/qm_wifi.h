#ifndef __QM_WIFI_H__
#define __QM_WIFI_H__

#ifdef __cplusplus
extern "c" {
#endif

#include "wifi_mgmr_ext.h"

int qm_hal_wifi_init(void);
void qm_wifi_scan_item_register_callback(scan_item_cb_t cb);
#ifdef __cplusplus
}
#endif

#endif
