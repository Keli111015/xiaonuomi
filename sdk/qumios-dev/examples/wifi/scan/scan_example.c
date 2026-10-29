#include "qm.h"
#include "qm_wifi.h"
#include "qm_work.h"
#include "qm_event.h"
#include "qm_errno.h"
#include "qm_log.h"

#define LOG_TAG  "wifi scan"


static void qm_event_handler(void *event_handler_arg, qm_event_base_t event_base,
                             int32_t event_id, void *event_data, size_t event_data_size)
{
    qm_wifi_event_info_t *event_info = (qm_wifi_event_info_t*)event_data;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data_size);

    switch(event_id){

        case QM_WIFI_EVENT_SCAN_DONE:

            QM_LOGD(LOG_TAG, "wifi scan done, status: %d, ap num: %d", event_info->scan_done.status, event_info->scan_done.ap_num);

            

        break;

        default:
        break;
    }
}

void qm_application_start(void)
{
    qm_event_handler_register(QM_EVENT_WIFI, QM_EVENT_ANY_ID, qm_event_handler, NULL);

    qm_wifi_init();

    qm_wifi_set_mode(QM_WIFI_MODE_STA);

    qm_wifi_start();

    qm_wifi_scan_start(NULL);
    
}