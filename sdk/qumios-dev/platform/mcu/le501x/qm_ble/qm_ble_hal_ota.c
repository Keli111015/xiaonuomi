#include "qm.h"

#include "qm_ble_bzopt.h"

#if QB_ENABLE_OTA
#include "qm_ota.h"
#include "qm_ble_hal_ota.h"


void qm_ble_ota_start( void )
{
    qm_ota_start(NULL);
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
        qm_ota_end(NULL);
    }
}

#endif


