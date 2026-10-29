#include "qm_hal_kv.h"
#include "qm_types.h"
#include "qm_errno.h"

#include "qm.h"
#include "qm_log.h"

#include "syscfg_id.h"

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int id = 0;
    int recv_len = 0;
    if(key == NULL || buffer == NULL || buffer_len == 0){
        return -QM_EINVAL;
    }

    sscanf(key, "%d", &id);

    recv_len = syscfg_read(id, buffer, *buffer_len);
    if(recv_len <= 0){
        *buffer_len = 0;
        return -QM_ERROR;
    }    

    *buffer_len = recv_len;
    
    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    int id = 0;
    int ret = QM_EOK;

    if(key == NULL || value == NULL || len == 0){
        return -QM_EINVAL;
    }

    sscanf(key, "%d", &id);


    ret = syscfg_dma_write(id, value, len);
    if(ret <= 0){
        QM_LOGD("1","KVS_KEY_ID_USER WRITE FAIL %d", ret);
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_kv_del(const char *key)
{
    return -QM_EINVAL;
}

