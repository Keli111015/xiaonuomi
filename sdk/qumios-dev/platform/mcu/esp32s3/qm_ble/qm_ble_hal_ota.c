#include "qm.h"

#include "qm_ble_bzopt.h"

#if QB_ENABLE_OTA
#include "qm_ota.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_ota.h"


static qm_timer_t ota_reboot_timer = {0};

static void ota_delay_action(qm_timer_t *timer, void *arg)
{
    static int disconn = 1;
    if(disconn){
        qm_ble_disconnect(0);
    }else{
        qm_ota_end(NULL);
    }
    disconn = 0;
}   


void qm_ble_ota_start( void )
{
    qm_ota_start(NULL);
    if(ota_reboot_timer.hdl == NULL){
        qm_timer_new(&ota_reboot_timer, ota_delay_action, NULL, 200, 1);
    }
}


int qm_ble_ota_flash_write(unsigned int* offset, unsigned char *buf ,int buf_len )
{
    return qm_ota_write(offset, buf, buf_len);
}


int qm_ble_ota_flash_read( unsigned int* offset, unsigned char *buf ,int buf_len )
{
    return qm_ota_read(offset, buf, buf_len);
}



void qm_ble_ota_end( int finish )
{   
    if(finish){
        qm_timer_start(&ota_reboot_timer);
    }
}

#endif


