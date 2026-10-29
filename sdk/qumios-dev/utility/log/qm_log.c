
#include "qm_log_impl.h"
#include "qm_log.h"
#include "qm.h"


unsigned int qm_log_level = QM_LL_V_DEBUG | QM_LL_V_INFO | QM_LL_V_WARN | QM_LL_V_ERROR | QM_LL_V_FATAL;

#if CONFIG_QM_OS_SUPPORT
qm_mutex_t g_log_mutex = {0};
#endif

void qm_set_log_level(qm_log_level_t log_level)
{
    unsigned int value = 0;

    switch (log_level)
    {
        case QM_LL_NONE:
            value |= QM_LL_V_NONE;
        break;
        case QM_LL_DEBUG:
            value |= QM_LL_V_DEBUG;
        break;
        case QM_LL_INFO:
            value |= QM_LL_V_INFO;
        break;
        case QM_LL_WARN:
            value |= QM_LL_V_WARN;
        break;
        case QM_LL_ERROR:
            value |= QM_LL_V_ERROR;
        break;
        case QM_LL_FATAL:
            value |= QM_LL_V_FATAL;
        break;
        default:
            break;
    }

    qm_log_level = value;
}

