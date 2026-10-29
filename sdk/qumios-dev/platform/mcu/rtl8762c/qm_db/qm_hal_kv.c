#include "qm.h"
#include "qm_flash.h"
#include "qm_utils_string.h"

#include "qm_log.h"
#define MIBLE_USER_REC_ID_BASE   0x50

int qm_hal_kv_init(void)
{

    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int ret = QM_EOK;
    uint32_t record_id = 0; 

    if(key == NULL || buffer_len == NULL){
        return -QM_EINVAL;
    }

    if(*buffer_len == 0){
        return -QM_EINVAL;
    }

    if(buffer){
        record_id = int_str_to_num(key, strlen(key));
        ret = qm_flash_read(QM_PARTITION_CUSTOM_1, &record_id, buffer, *buffer_len);
        if(ret != QM_EOK){
            return -QM_ERROR;
        }
    }

    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    uint16_t record_id = 0; 
    if(key == NULL || value == NULL || len == 0){
        return -QM_EINVAL;
    }

    record_id = int_str_to_num(key, strlen(key));
    return qm_flash_write(QM_PARTITION_CUSTOM_1, &record_id, value, len);
}

int qm_hal_kv_del(const char *key)
{
    uint16_t record_id = 0; 
    if(key == NULL){
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int qm_hal_kv_clean(void)
{

    return QM_EOK;
}
