#include "qm.h"
#include "qm_wdg.h"

#include <hosal_wdg.h>

#define QM_WDG_DEBUG_LOG    (0)



typedef struct 
{
    hosal_wdg_dev_t wdg;
}qm_wdg_hal_handle_t;

static qm_wdg_hal_handle_t g_wdg_handle = {0};


/**
 * This function will initialize the on board CPU hardware watch dog
 *
 * @param[in]  wdg  the watch dog device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_wdg_init(qm_wdg_dev_t *wdg)
{
    int ret = QM_EOK;
    if(wdg == NULL || wdg->port != 0){
        return -QM_EINVAL;
    }

    g_wdg_handle.wdg.port = wdg->port;
    g_wdg_handle.wdg.config.timeout = wdg->config.timeout;

    ret = hosal_wdg_init(&g_wdg_handle.wdg);
    if(ret != QM_EOK){
       ret = -QM_ERROR;
    }
    
    return  ret;
}

/**
 * Reload watchdog counter.
 *
 * @param[in]  wdg  the watch dog device
 */
void qm_wdg_reload(qm_wdg_dev_t *wdg)
{
    if(wdg == NULL || wdg->port != 0){
        return ;
    }
    hosal_wdg_reload(&g_wdg_handle.wdg);
}

/**
 * @brief 暂不实现
 *  
 * This function performs any platform-specific cleanup needed for hardware watch dog.
 *
 * @param[in]  wdg  the watch dog device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_wdg_deinit(qm_wdg_dev_t *wdg)
{
    int ret = QM_EOK;
    if(wdg == NULL || wdg->port != 0){
        return -QM_EINVAL;
    }
    hosal_wdg_finalize(&g_wdg_handle.wdg);
    if(ret != QM_EOK){
       ret = -QM_ERROR;
    }
    return  ret;
}
