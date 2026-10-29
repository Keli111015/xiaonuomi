#include "qm_iot_import.h"
#include "qmos/qm.h"
#include "qm_kv.h"

void *hal_mutex_create(void)
{
    qm_mutex_t *mutex = hal_malloc(sizeof(qm_mutex_t));
    if(!mutex){
        return NULL;
    }
    
    if(qm_mutex_new(mutex) == 0){
        return (void*)mutex;
    }else{
        return NULL;
    }
}

void hal_mutex_destory(_IN_ void *mutex)
{
    if(!mutex){
        return;
    }
    qm_mutex_free((qm_mutex_t*)mutex);
    hal_free(mutex);
}

void hal_mutex_lock(_IN_ void *mutex)
{
    if(!mutex){
        return;
    }
    qm_mutex_lock((qm_mutex_t*)mutex, QM_WAIT_FOREVER);   
} 

void hal_mutex_unlock(_IN_ void *mutex)
{
    if(!mutex){
        return;
    }
    qm_mutex_unlock((qm_mutex_t*)mutex);    
}


void hal_free(void *ptr)
{
    qm_free(ptr);
}


void *hal_malloc(int size)
{
    return qm_malloc(size);
}

uint32_t hal_uptime_ms(void)
{
    return (uint32_t)qm_now_ms();
}

void hal_sleep_ms(_IN_ uint32_t ms)
{
    qm_msleep(ms);
}

void hal_srandom(uint32_t seed)
{
    qm_srandom(seed);
}

uint32_t hal_random_get(uint32_t region)
{
    return qm_random_get(region);
}

#if 0
int hal_snprintf(_IN_ char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int     rc;

    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);

    return rc;
}
#endif

int hal_kv_set(const char *key, const void *val, int len, int sync)
{
    int  ret = 0;
    ret = qm_kv_set(key, val, len, sync);
    return ret;
}

int hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int  ret = 0;
    ret = qm_kv_get(key, buffer, buffer_len);
    return ret;
}

int hal_kv_del(const char *key)
{
    int  ret = 0;
    ret = qm_kv_del(key);
    return ret;
}


