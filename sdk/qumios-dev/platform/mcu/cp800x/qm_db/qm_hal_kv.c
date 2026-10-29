#include "qm.h"
#include "qm_hal_kv.h"

//简单适配

int qm_hal_kv_init(void)
{
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{

    return QM_EOK;
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
    return 0;
}
