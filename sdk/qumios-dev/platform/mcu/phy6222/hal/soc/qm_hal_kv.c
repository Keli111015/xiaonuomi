#include "qm.h"

#include "qm_log.h"

#include "qm_hal_kv.h"

#define LOG_TAG "adc"

#define DIR_KV_INDEX_NAME 0x1
#define DIR_KV_STORE_NAME 0x2

#define KV_STORE_MAX_NUM  (5)



int qm_hal_kv_init(void)
{
    
    return QM_EOK;
}


int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
   int ret = -1;

    return ret;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
   

    return QM_EOK;
}

int qm_hal_kv_del(const char *key)
{
   
    return QM_EOK;
}

int qm_hal_kv_clean(void)
{

    return QM_EOK;
}
