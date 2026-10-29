#include "qm_ble_hal_ota.h"

void qm_ble_ota_start(void)
{
}

int qm_ble_ota_flash_write(unsigned int *offset, unsigned char *buf, int buf_len)
{
    (void)offset;
    (void)buf;
    (void)buf_len;
    return -1;
}

int qm_ble_ota_flash_read(unsigned int *offset, unsigned char *buf, int buf_len)
{
    (void)offset;
    (void)buf;
    (void)buf_len;
    return -1;
}

unsigned int qm_ble_flash_erase_sector_size(void)
{
    return 4096;
}

void qm_ble_ota_end(int finish)
{
    (void)finish;
}

int qm_ble_ota_breakpoint_set(unsigned int break_point)
{
    (void)break_point;
    return -1;
}

int qm_ble_ota_breakpoint_get(unsigned int break_point)
{
    (void)break_point;
    return -1;
}
