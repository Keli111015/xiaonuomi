#include "qm_ble_bzopt.h"
#include "qm_ble_hal_ota.h"

#if QB_ENABLE_OTA
void qm_ble_ota_start( void )
{
 
}

int qm_ble_ota_flash_write(unsigned int* offset, unsigned char *buf ,int buf_len )
{
    return 0;
}

int qm_ble_ota_flash_read( unsigned int* offset, unsigned char *buf ,int buf_len )
{
    return 0;
}

void qm_ble_ota_end( int finish )
{

}

#endif


