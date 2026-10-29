#include "qm.h"
#include "qm_log.h"
#include "relay.h"

#define LOG_TAG  "relay"

void qm_application_start(void)
{
    relay_io_t relay_io = {
        .port = 10
    };
    relay_handle_t relay_handle = NULL;
    relay_handle = relay_create(&relay_io, RELAY_CLOSE_LOW);
    if(relay_handle == NULL){
        QM_LOGE(LOG_TAG, "relay create fail");
        return;
    }

    relay_state_write(relay_handle, RELAY_STATUS_OPEN);
    qm_msleep(1000);
    relay_state_write(relay_handle, RELAY_STATUS_CLOSE);
    
    relay_delete(relay_handle);
}