#include "qm_ntp.h"
#include "ntpdate.h"
#include <netmgr.h>
#include <aos/aos.h>

#include "qm_config.h"

static void wifi_event_handler(input_event_t *event, void *priv_data)
{

    if (event->type != EV_WIFI) return;

    switch(event->code){

        case CODE_WIFI_ON_GOT_IP:
            qm_time_sync_start();
        break;
        case CODE_WIFI_ON_DISCONNECT:
            qm_time_sync_stop();
        break;
        default:
        break;  
    }
    
}

void qm_ntp_init(void)
{
    qm_time_init();
    aos_register_event_filter(EV_WIFI, wifi_event_handler, NULL);
}