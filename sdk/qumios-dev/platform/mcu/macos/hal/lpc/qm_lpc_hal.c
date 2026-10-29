#include "qm_lpc_hal.h"
#include "qm_errno.h"

int32_t qm_hal_lpc_init(void) { return QM_EOK; }
int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode) { (void)mode; return -QM_ENOSYS; }
int32_t qm_hal_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t cb, bool_t enable)
{
    (void)pin; (void)type; (void)cb; (void)enable; return -QM_ENOSYS;
}
int32_t qm_hal_lpc_enable(void) { return -QM_ENOSYS; }
int32_t qm_hal_lpc_disable(void) { return -QM_ENOSYS; }
int32_t qm_lpc_check(void) { return -QM_ENOSYS; }
int32_t qm_lpc_resume(void) { return -QM_ENOSYS; }
int32_t qm_lpc_prepare(void) { return -QM_ENOSYS; }

