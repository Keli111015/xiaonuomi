#include "qm_types.h"
#include "virserial.h"
#include "qm.h"

#include "qm_config.h"

#if CONFIG_QM_LOG_SUPPORT == 1
extern qm_mutex_t g_log_mutex;
#endif
void qm_main(void)
{  
    qm_kernel_init();
#if CONFIG_QM_LOG_SUPPORT == 1
    qm_mutex_new(&g_log_mutex);
#endif  
    virserial_cmd_init();
    qm_application_start();
}