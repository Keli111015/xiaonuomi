#include "qm_ota.h"
#include "qm_log.h"
#include "liot_os.h"
#include "liot_power.h"
#include "liot_dev.h"
#include "liot_fota.h"
#include "liot_fs_api.h"
#include "liot_nw.h"

#define LOG_TAG "ota"

int qm_ota_start(void *arg)
{
    liot_fota_errcode_e ret = LIOT_FOTA_UPGRADE_FAIL;
    ret = liot_fota_nvm_init();
    if(ret != LIOT_FOTA_UPGRADE_SUCCESS){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{
    uint32 addr = *off_set;
    liot_fota_errcode_e ret = LIOT_FOTA_UPGRADE_FAIL;
    ret = liot_fota_nvm_write(addr, in_buf, in_buf_len);
    if(ret != LIOT_FOTA_UPGRADE_SUCCESS){
        return -QM_ERROR;
    }

    *off_set = addr + in_buf_len;
    
    return QM_EOK;
}

int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    return QM_EOK;
}

int qm_ota_abort(void *arg)
{
    (void)arg;
    liot_fota_clear(NULL, TRUE);
    return QM_EOK;
}

int qm_ota_end(void *arg)
{
    liot_fota_errcode_e ret = LIOT_FOTA_UPGRADE_FAIL;
    ret = liot_fota_nvm_image_verify();
    if(ret != LIOT_FOTA_UPGRADE_SUCCESS){
        liot_fota_clear(NULL,TRUE);
        return -QM_ERROR;
    }else{
        liot_rtos_task_sleep_s(5);
        liot_fota_power_reset(LIOT_RESET_NORMAL);
    }
    return QM_EOK;
}
