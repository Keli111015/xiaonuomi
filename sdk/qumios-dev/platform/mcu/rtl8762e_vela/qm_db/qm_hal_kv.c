#include "qm.h"

#include "qm_log.h"

#include "qm_hal_kv.h"
#include "miio_user_api.h"

#ifndef CONFIG_QM_HAL_KV_NAME_BUFFER_LEN
#define CONFIG_QM_HAL_KV_NAME_BUFFER_LEN 16
#endif

#define LOG_TAG "kv hal"

#define DIR_KV_INDEX_NAME 0

#define KV_STORE_MAX_NUM  (22)

typedef struct 
{
    int is_use;
    uint8_t buffer[CONFIG_QM_HAL_KV_NAME_BUFFER_LEN];
    uint16_t record_id;
    uint16_t record_len;
}qm_hal_kv_store_t;

static int is_init = 1;
static qm_hal_kv_store_t g_kv_store[KV_STORE_MAX_NUM] = {0};

int qm_hal_kv_init(void)
{
    return QM_EOK;
}

static qm_hal_kv_store_t *kv_store_index_read(const char *key)
{
    int index = 0;
    int ret = QM_EOK;
    qm_hal_kv_store_t *kv_store_handle = NULL;
    for(index = 0; index < QM_ARRAY_SIZE(g_kv_store); index++){

        if(!g_kv_store[index].is_use){
            continue;
        }

        if(!memcmp(key, g_kv_store[index].buffer, strlen(key))){
            kv_store_handle = &g_kv_store[index];
            break;
        }
    }
    return kv_store_handle;
}

static qm_hal_kv_store_t *kv_store_index_add(const char *key)
{
    int index = 0;
    qm_hal_kv_store_t *kv_store_handle = NULL;
    for(index = 0; index < QM_ARRAY_SIZE(g_kv_store); index++){
        if(g_kv_store[index].is_use){
            continue;
        }
        kv_store_handle = &g_kv_store[index];
        kv_store_handle->is_use = QM_TRUE;
        kv_store_handle->record_id = index + 1;
        memcpy(kv_store_handle->buffer, key, strlen(key));
        break;
    }
    return kv_store_handle;
}

static qm_hal_kv_store_t *kv_store_index_write(const char *key, uint32_t record_len)
{
    int index = 0;
    int ret = QM_EOK;
    
    qm_hal_kv_store_t *kv_store_handle = NULL;
    for(index = 0; index < QM_ARRAY_SIZE(g_kv_store); index++){
        if(!g_kv_store[index].is_use){
            continue;
        }
        
        if(!memcmp(key, g_kv_store[index].buffer, strlen(key))){
            kv_store_handle = &g_kv_store[index];
            if(kv_store_handle->record_len != record_len){
                kv_store_handle->record_len = record_len;
            }
            ret = miio_record_write(DIR_KV_INDEX_NAME, (uint8_t *)g_kv_store, sizeof(g_kv_store));
            if(ret != MI_SUCCESS){
                QM_LOGW(LOG_TAG, "RECORD WRITE %d", ret);
            }            
            break;
        }
    }
    return kv_store_handle;
}

static qm_hal_kv_store_t *kv_store_index_del(const char *key)
{
    int index = 0;
    int ret = QM_EOK;
    qm_hal_kv_store_t *kv_store_handle = NULL;
    for(index = 0; index < QM_ARRAY_SIZE(g_kv_store); index++){
        if(!g_kv_store[index].is_use){
            continue;
        }
        
        kv_store_handle = &g_kv_store[index];

        if(!memcmp(key, kv_store_handle->buffer, strlen(key))){
            memset(kv_store_handle, 0, sizeof(qm_hal_kv_store_t));
            ret = miio_record_write(DIR_KV_INDEX_NAME, (uint8_t *)g_kv_store, sizeof(g_kv_store));
            if(ret != MI_SUCCESS){
                QM_LOGW(LOG_TAG, "RECORD WRITE %d", ret);
            }                 
            break;
        }
    }
    return kv_store_handle;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int ret = QM_EOK;
    qm_hal_kv_store_t *kv_store_handle = NULL;

    QM_RETURN_ON_FALSE(key, -QM_EINVAL, LOG_TAG, "key invail");
    QM_RETURN_ON_FALSE(buffer_len, -QM_EINVAL, LOG_TAG, "buffer_len invail");
    QM_RETURN_ON_FALSE(*buffer_len, -QM_EINVAL, LOG_TAG, "buffer_len == 0");
    
    if(is_init){
        ret = miio_record_read(DIR_KV_INDEX_NAME, (uint8_t *)g_kv_store, sizeof(g_kv_store));
        if(ret != MI_SUCCESS){
            QM_LOGW(LOG_TAG, "RECORD READ %d", ret);
        }
        is_init = 0;
    }

    kv_store_handle = kv_store_index_read(key);
    if (kv_store_handle == NULL){
        return -QM_EINVAL;
    }
    
	//读出 dir_11 目录下的 DIR_11_RECORD_2 记录数据，读回的数据放在 dir_11_record_2， 读回数据长度为 length
    if(buffer){
        ret = miio_record_read(DIR_KV_INDEX_NAME + kv_store_handle->record_id,(uint8_t *)buffer, kv_store_handle->record_len);
    }else{
        *buffer_len = (int)kv_store_handle->record_len;
    }
    
    if(ret != MI_SUCCESS){
        ret = -QM_ERROR;
    }

    return ret;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    int ret = QM_EOK;
    qm_hal_kv_store_t *kv_store_handle = NULL;

    QM_RETURN_ON_FALSE(key, -QM_EINVAL, LOG_TAG, "key invail");
    QM_RETURN_ON_FALSE(value, -QM_EINVAL, LOG_TAG, "value invail");
    QM_RETURN_ON_FALSE(len, -QM_EINVAL, LOG_TAG, "len == 0");
    
    if(is_init){
        ret = miio_record_read(DIR_KV_INDEX_NAME, (uint8_t *)g_kv_store, sizeof(g_kv_store));
        if(ret != MI_SUCCESS){
            QM_LOGW(LOG_TAG, "RECORD READ %d", ret);
        }
        is_init = 0;
    }

    kv_store_handle = kv_store_index_read(key);
    if (kv_store_handle == NULL){
       kv_store_handle = kv_store_index_add(key);
       if(kv_store_handle == NULL){
            QM_LOGE(LOG_TAG, "kv_store_handle null ");

            return -QM_EFULL;
       }
    }
    
    ret = miio_record_write(DIR_KV_INDEX_NAME + kv_store_handle->record_id, (uint8_t *)value, (uint16_t)len);
    if(ret != MI_SUCCESS){
        QM_LOGE(LOG_TAG, "miio_record_write err %d ", ret);
        return -QM_ERROR;
    }

    kv_store_index_write(key, len);

    return QM_EOK;
}

int qm_hal_kv_del(const char *key)
{
    int ret = QM_EOK;
    qm_hal_kv_store_t *kv_store_handle = NULL;

    QM_RETURN_ON_FALSE(key, -QM_EINVAL, LOG_TAG, "key invail");
    
    if(is_init){
        ret = miio_record_read(DIR_KV_INDEX_NAME, (uint8_t *)g_kv_store, sizeof(g_kv_store));
        if(ret != MI_SUCCESS){
            QM_LOGW(LOG_TAG, "RECORD READ %d", ret);
        }
        is_init = 0;
    }

    kv_store_handle = kv_store_index_read(key);
    if (kv_store_handle == NULL){
        return -QM_EINVAL;
    }

    kv_store_index_del(key);
    mible_record_delete(DIR_KV_INDEX_NAME + kv_store_handle->record_id);

    return QM_EOK;
}

int qm_hal_kv_clean(void)
{

    return QM_EOK;
}
