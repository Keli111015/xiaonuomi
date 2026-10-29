#include "qm.h"

#include "qm_ble_bzopt.h"

#if QB_ENABLE_OTA
#include "qm_ota.h"
#include "qm_ble_hal_ota.h"

void qm_ble_ota_start( void )
{

}

int qm_ble_ota_flash_write(unsigned int* offset, unsigned char *buf ,int buf_len )
{

}

int qm_ble_ota_flash_read( unsigned int* offset, unsigned char *buf ,int buf_len )
{

}

void qm_ble_ota_end( int finish )
{   

}

#endif


