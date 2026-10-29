#include "qm.h"

#include "qm_log.h"

#include "qm_hal_kv.h"
#include "tinyfs.h"

#define LOG_TAG "adc"

#define DIR_KV_INDEX_NAME 0x1
#define DIR_KV_STORE_NAME 0x2

#define KV_STORE_MAX_NUM  (5)

typedef struct 
{
    uint8_t is_use;
    uint16_t record_id;
    uint8_t buffer[16];
    uint32_t record_len;
}qm_hal_kv_store_t;

static tinyfs_dir_t dir_kv_index = {0};
static tinyfs_dir_t dir_kv_store = {0};
static qm_hal_kv_store_t kv_store[KV_STORE_MAX_NUM] = {0};

int qm_hal_kv_init(void)
{
    uint8_t ret_val = TINYFS_NO_ERROR;
    uint16_t length = sizeof(kv_store);
	ret_val= tinyfs_mkdir(&dir_kv_index, ROOT_DIR, DIR_KV_INDEX_NAME);  
    if( ret_val != TINYFS_NO_ERROR && 
        ret_val != TINYFS_DIR_KEY_ALREADY_EXISTED){
        return -QM_ERROR;
    }

	ret_val= tinyfs_mkdir(&dir_kv_store, ROOT_DIR, DIR_KV_STORE_NAME);  
    if( ret_val != TINYFS_NO_ERROR && 
        ret_val != TINYFS_DIR_KEY_ALREADY_EXISTED){
        return -QM_ERROR;
    }

    tinyfs_read(dir_kv_index, DIR_KV_INDEX_NAME, (uint8_t *)kv_store, &length);
    return QM_EOK;
}

static qm_hal_kv_store_t *kv_store_index_read(const char *key)
{
    int index = 0;
    qm_hal_kv_store_t *kv_store_handle = NULL;
    for(index = 0; index < QM_ARRAY_SIZE(kv_store); index++){

        if(!kv_store[index].is_use){
            continue;
        }

        if(!memcmp(key, kv_store[index].buffer, strlen(key))){
            kv_store_handle = &kv_store[index];
            break;
        }
    }
    return kv_store_handle;
}

static qm_hal_kv_store_t *kv_store_index_add(const char *key)
{
    int index = 0;
    qm_hal_kv_store_t *kv_store_handle = NULL;
    for(index = 0; index < QM_ARRAY_SIZE(kv_store); index++){
        if(kv_store[index].is_use){
            continue;
        }
        kv_store_handle = &kv_store[index];
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
    qm_hal_kv_store_t *kv_store_handle = NULL;
    for(index = 0; index < QM_ARRAY_SIZE(kv_store); index++){
        if(!kv_store[index].is_use){
            continue;
        }
        
        if(!memcmp(key, kv_store[index].buffer, strlen(key))){
            kv_store_handle = &kv_store[index];
            if(kv_store_handle->record_len != record_len){
                kv_store_handle->record_len = record_len;
            }
            tinyfs_write(dir_kv_index, DIR_KV_INDEX_NAME, (uint8_t *)kv_store, sizeof(kv_store));
            break;
        }
    }
    return kv_store_handle;
}

static qm_hal_kv_store_t *kv_store_index_del(const char *key)
{
    int index = 0;
    qm_hal_kv_store_t *kv_store_handle = NULL;
    for(index = 0; index < QM_ARRAY_SIZE(kv_store); index++){
        if(!kv_store[index].is_use){
            continue;
        }
        
        kv_store_handle = &kv_store[index];

        if(!memcmp(key, kv_store_handle->buffer, strlen(key))){
            memset(kv_store_handle, 0, sizeof(qm_hal_kv_store_t));
            tinyfs_write(dir_kv_index, DIR_KV_INDEX_NAME, (uint8_t *)kv_store, sizeof(kv_store));
            break;
        }
    }
    return kv_store;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int ret = QM_EOK;
    qm_hal_kv_store_t *kv_store_handle = NULL;

    QM_RETURN_ON_FALSE(key, -QM_EINVAL, LOG_TAG, "key invail");
    QM_RETURN_ON_FALSE(buffer_len, -QM_EINVAL, LOG_TAG, "buffer_len invail");
    QM_RETURN_ON_FALSE(*buffer_len, -QM_EINVAL, LOG_TAG, "buffer_len == 0");
    
    kv_store_handle = kv_store_index_read(key);
    if (kv_store_handle == NULL){
        return -QM_EINVAL;
    }
    
	//读出 dir_11 目录下的 DIR_11_RECORD_2 记录数据，读回的数据放在 dir_11_record_2， 读回数据长度为 length
    if(buffer){
        ret = tinyfs_read(dir_kv_store,kv_store_handle->record_id,(uint8_t *)buffer, (uint16_t *)buffer_len);
    }else{
        *buffer_len = kv_store_handle->record_len;
    }
    
    if(ret != TINYFS_NO_ERROR){
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
    
    kv_store_handle = kv_store_index_read(key);
    if (kv_store_handle == NULL){
       kv_store_handle = kv_store_index_add(key);
       if(kv_store_handle == NULL){
            QM_LOGE(LOG_TAG, "kv_store_handle null ");

            return -QM_EFULL;
       }
    }

    ret = tinyfs_write(dir_kv_store, kv_store_handle->record_id, (uint8_t *)value, len);
    if(ret != TINYFS_NO_ERROR){
        QM_LOGE(LOG_TAG, "tinyfs_write err %d ", ret);
        return -QM_ERROR;
    }

    kv_store_index_write(key, len);

    tinyfs_write_through();

    return QM_EOK;
}

int qm_hal_kv_del(const char *key)
{
    qm_hal_kv_store_t *kv_store = NULL;

    QM_RETURN_ON_FALSE(key, -QM_EINVAL, LOG_TAG, "key invail");
    
    kv_store = kv_store_index_read(key);
    if (kv_store == NULL){
        return -QM_EINVAL;
    }

    kv_store_index_del(key);

    tinyfs_del_record(dir_kv_store, kv_store->record_id);

    tinyfs_write_through();

    return QM_EOK;
}

int qm_hal_kv_clean(void)
{

    return QM_EOK;
}
