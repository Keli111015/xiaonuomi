#include "qm.h"
#include "qm_wdg.h"
#include "ls_hal_iwdg.h" 
#include "app_config.h"

int32_t qm_wdg_init(qm_wdg_dev_t *wdg)
{
    qm_err_t ret = QM_EOK;
    uint32_t wdg_timeout = 0;
    if(wdg == NULL){
        return -QM_EINVAL;
    }
    wdg_timeout = (wdg -> config.timeout) * 32768 /1000;
    /* 
    IWDG的计数时钟为32.768KHz，LoadValue =32768时  
    wdg_timeout单位为：ms
    */
    ret = HAL_IWDG_Init(wdg_timeout);
    if(ret != QM_EOK){
        return -QM_ERROR;

    }
    return QM_EOK;
}


void qm_wdg_reload(qm_wdg_dev_t *wdg)
{
    qm_err_t ret = QM_EOK;
    if(wdg == NULL){
        return;
    }
    ret = HAL_IWDG_Refresh();
}

int32_t qm_wdg_deinit(qm_wdg_dev_t *wdg)
{
    qm_err_t ret = QM_EOK;
    if(wdg == NULL){
        return -QM_EINVAL;
    }
    ret = HAL_IWDG_DeInit();
    if(ret != QM_EOK){
        return -QM_ERROR;
    }
    return QM_EOK;
}
