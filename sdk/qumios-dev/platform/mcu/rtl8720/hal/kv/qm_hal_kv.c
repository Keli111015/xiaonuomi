#include "qm.h"
#include "arch_psm.h"
#include "qm_hal_kv.h"

#define KVS_KEY_NAME           "qm_kv"
#define KVS_KEY_LEN             (2048)
#define KVS_BUF_MAX_LEN         (4 * 1024)

int qm_hal_kv_init(void)
{
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    if ((key == NULL) || ((*buffer_len) > KVS_KEY_LEN)) {
		 return -QM_EINVAL;
	}
    *buffer_len = arch_psm_get_value(KVS_KEY_NAME, key, buffer, KVS_BUF_MAX_LEN);
	if((*buffer_len) < 0){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    if ((key == NULL) || (value == NULL) || (len > KVS_KEY_LEN) || (sync != 1)) {
		return -QM_EINVAL;
	}

    if(arch_psm_set_value(KVS_KEY_NAME, key, value, len) < 0){
        return -QM_ERROR;
    }

    return QM_EOK;
}

int qm_hal_kv_del(const char *key)
{
    if(key == NULL){
        return -QM_EINVAL;
    }
    
    if(arch_psm_erase_key(KVS_KEY_NAME, key) != MIIO_OK){
        return -QM_ERROR;
    }

    return QM_EOK;
}

int qm_hal_kv_clean(void)
{
    return 0;
}