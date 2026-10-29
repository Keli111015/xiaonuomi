#include "qm.h"
#include "qm_hal_kv.h"
#include "liot_fs_api.h"
#include "liot_nv.h"
#include "liot_os.h"
#include "liot_type.h"
#include "lfs_port.h"

#define QM_KV_STORAGE_NAMESPACE "qm_kv"

int qm_hal_kv_init(void)
{
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    int ret = -1;
    int size =  0;

    if(key == NULL || buffer == NULL || buffer_len == NULL){
        return -QM_EINVAL;
    }


    lfs_file_t *nv_file = qm_malloc(sizeof(lfs_file_t));
    if(nv_file == NULL){
        QM_LOGD("1", "can't malloc memory");
        return LIOT_NV_ERR_OPEN;
    }

    if (LFS_fileOpen(nv_file, key, LFS_O_RDONLY) != LFS_ERR_OK){
        QM_LOGD("1", "can't open file %s", buffer);
        free(nv_file);
        return LIOT_NV_ERR_OPEN;
    }

//get size
    size = LFS_fileSize(nv_file);
    QM_LOGD("1", "LFS_fileSize %d", size);
    LFS_fileClose(nv_file);
    qm_free(nv_file);

    *buffer_len = size;

    if(buffer){
        ret = liot_nvm_fread(key, buffer, (size_t)size, 1);
        if (ret != size){
            return -QM_ERROR;
        }
    }

    return QM_EOK;
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    int ret = -1;
    int size =  0;
    lfs_file_t nv_file = {0};

    LFS_remove(key);
    
    ret = liot_nvm_fwrite(key, (void*)value, (size_t)len, 1);
    if (ret != len){
        return -QM_ERROR;
    }
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
