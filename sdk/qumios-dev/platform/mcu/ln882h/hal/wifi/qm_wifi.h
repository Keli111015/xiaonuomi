#ifndef __QM_WIFI_H__
#define __QM_WIFI_H__

#ifdef __cplusplus
extern "c" {
#endif

//#include "wifi_mgmr_ext.h"
#include "qm_kernel.h"

#include "qm_wifi_dev.h"
#include "qm_errno.h"
#include "ln_wifi_err.h"
#include "wifi.h"
#include "usr_app.h"
#include <lwip/tcpip.h>
#include "system_parameter.h"
//#include "easyflash.h"
#include <lwip/igmp.h>


int qm_hal_wifi_init(void);
void qm_wifi_scan_item_register_callback(sta_scan_report_cb_t cb);
#ifdef __cplusplus
}
#endif

#endif
