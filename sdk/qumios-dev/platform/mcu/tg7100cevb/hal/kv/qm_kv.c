#include "qm_kv.h"
#include <aos/aos.h>


int qm_kv_get(const char *key, void *buffer, int *buffer_len)
{
    return aos_kv_get(key, buffer, buffer_len);
}

int qm_kv_set(const char *key, const void *value, int len, int sync)
{
    return aos_kv_set(key, value, len, sync);
}

int qm_kv_del(const char *key)
{
    return aos_kv_del(key);
}


