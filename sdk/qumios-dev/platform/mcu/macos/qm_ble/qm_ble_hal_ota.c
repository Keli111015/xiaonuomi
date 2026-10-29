#include "qm_ble_hal_ota.h"
#include "qm_ota.h"

void qm_ble_ota_start(void) { (void)qm_ota_start(NULL); }
int qm_ble_ota_flash_write(unsigned int *offset, unsigned char *buf, int buf_len)
{
    return offset == NULL || buf_len < 0 ? -QM_EINVAL : qm_ota_write(offset, buf, (uint32_t)buf_len);
}
int qm_ble_ota_flash_read(unsigned int *offset, unsigned char *buf, int buf_len)
{
    return offset == NULL || buf_len < 0 ? -QM_EINVAL : qm_ota_read(offset, buf, (uint32_t)buf_len);
}
unsigned int qm_ble_flash_erase_sector_size(void) { return 4096u; }
void qm_ble_ota_end(int finish) { if (finish) (void)qm_ota_end(NULL); else (void)qm_ota_abort(NULL); }
int qm_ble_ota_breakpoint_set(unsigned int break_point) { (void)break_point; return 0; }
int qm_ble_ota_breakpoint_get(unsigned int break_point) { (void)break_point; return 0; }

