#include "qm.h"
#include "qm_hal_kv.h"
#include <ef_def.h>
#include <easyflash.h>

#define PSM_KV_FORMAT "qm_kv/%s"

int qm_hal_kv_init(void)
{

    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int length = 0;

    if(buffer == NULL || key == NULL || buffer_len == NULL){
        return -QM_EINVAL;
    }

    char *name = (char *)qm_malloc(strlen(key) + strlen(PSM_KV_FORMAT) + 1);
    if(name == NULL){
        return -QM_ENOMEM;
    }
    memset(name, 0, strlen(key) + strlen(PSM_KV_FORMAT) + 1);

    qm_snprintf(name, strlen(key) + strlen(PSM_KV_FORMAT), PSM_KV_FORMAT, key);

    length = ef_get_env_blob(name, buffer, *buffer_len, NULL);
    
    if(length <= 0){
        *buffer_len = 0;
        goto __exit;
    }

    *buffer_len = length; 

__exit:
    qm_free(name);

    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    int ret = 0;
    int length = 0;

    if(value == NULL || key == NULL || !len){
        return -QM_EINVAL;
    }

    char *name = (char *)qm_malloc(strlen(key) + strlen(PSM_KV_FORMAT) + 1);
    if(name == NULL){
        return -QM_ENOMEM;
    }
    memset(name, 0, strlen(key) + strlen(PSM_KV_FORMAT) + 1);

    qm_snprintf(name, strlen(key) + strlen(PSM_KV_FORMAT), PSM_KV_FORMAT, key);

    QM_LOGD("1", "[%s]:%d", name, len);
    ret = ef_set_env_blob(name, value, len);

    qm_free(name);

    return ret;
}

int qm_hal_kv_del(const char *key)
{
    int ret = 0;
    if(key == NULL){
        return -QM_EINVAL;
    }

    char *name = (char *)qm_malloc(strlen(key) + strlen(PSM_KV_FORMAT) + 1);
    if(name == NULL){
        return -QM_ENOMEM;
    }
    memset(name, 0, strlen(key) + strlen(PSM_KV_FORMAT) + 1);

    qm_snprintf(name, strlen(key) + strlen(PSM_KV_FORMAT), PSM_KV_FORMAT, key);

    ret = ef_del_env(name);

    qm_free(name);

    return ret;
}

int qm_hal_kv_clean(void)
{
    return 0;
}
