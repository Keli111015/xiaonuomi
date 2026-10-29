#include "qm_db_log.h"
#include "qm_errno.h"

static volatile uint32_t g_qm_db_log_flags = QM_DB_KCONFIG_LOG_FLAGS;

uint32_t qm_db_get_log_flags(void)
{
    return g_qm_db_log_flags;
}

int qm_db_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)QM_DB_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_db_log_flags = log_flags;
    return QM_EOK;
}
