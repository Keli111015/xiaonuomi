#include "qm_config.h"
#include "qmos/qm.h"

#if CONFIG_QM_CLI_SUPPORT
#include "qm_cli.h"
#endif

#if CONFIG_QM_WORK_SUPPORT
#include "qm_work.h"
#endif

#if CONFIG_QM_KV_SUPPORT
#include "qm_kv.h"
#endif

#if CONFIG_QM_WIFI_SUPPORT
extern int qm_hal_wifi_init(void);
#endif

#if CONFIG_QM_NOOS_SUPPORT
#include "qm_noos.h"
#endif

#if CONFIG_QM_TIME_SUPPORT
#include "qm_time.h"
#endif

void qm_kernel_init(void)
{

    qm_init();

#if CONFIG_QM_NOOS_SUPPORT
    qm_noos_init();
#endif

#if CONFIG_QM_KV_SUPPORT
    qm_kv_init();
#endif
    
#if CONFIG_QM_CLI_SUPPORT
    qm_cli_init();
#endif

#if CONFIG_QM_WIFI_SUPPORT
    qm_hal_wifi_init();
#endif

#if CONFIG_QM_WORK_SUPPORT
    qm_work_on_init();
#endif

#if CONFIG_QM_TIME_SUPPORT
    qm_time_init();
#endif

}
