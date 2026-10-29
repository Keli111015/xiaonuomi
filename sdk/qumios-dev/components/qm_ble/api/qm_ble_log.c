#include "qm.h"
#include "qm_ble_log.h"

static volatile uint32_t g_qm_ble_log_flags = QM_BLE_KCONFIG_LOG_FLAGS;

uint32_t qm_ble_get_log_flags(void)
{
    return g_qm_ble_log_flags;
}

int qm_ble_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_BLE_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }

    g_qm_ble_log_flags = log_flags;
    return QM_EOK;
}
