#include "qm_ble_bzopt.h"
#include "qm_ble_hal_ota.h"


#if QB_ENABLE_OTA

#define MAX_OTA_FLASH_BUF 6000
static uint8_t ota_flash_buf[MAX_OTA_FLASH_BUF] = {0};
static uint32_t ota_flash_write_offset = 0;
static uint32_t ota_flash_read_offset = 0;


void qm_ble_ota_start( void )
{
    ota_flash_write_offset = 0;
    ota_flash_read_offset = 0;

}


int qm_ble_ota_flash_write(unsigned int* offset, unsigned char *buf ,int buf_len )
{

    memcpy(ota_flash_buf+ota_flash_write_offset, buf, buf_len);
    ota_flash_write_offset += buf_len;
    *offset = ota_flash_write_offset;
    return 0;
}


int qm_ble_ota_flash_read( unsigned int* offset, unsigned char *buf ,int buf_len )
{
    memcpy( buf, ota_flash_buf+ota_flash_read_offset, buf_len);
    ota_flash_read_offset += buf_len;
    *offset = ota_flash_read_offset;
    return 0;
}


void qm_ble_ota_end( int finish )
{



}

#endif


