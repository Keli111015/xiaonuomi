#include "qm_hal_kv.h"
#include "qm.h"

#if CONFIG_QM_KV_SUPPORT && CONFIG_QM_KV_SYSTEM_SUPPORT

#include <fcntl.h>
#include <unistd.h>

#define KVS_KEY_LEN             32

#ifndef CONFIG_QM_KV_STORAGE_NAMESPACE
#define CONFIG_QM_KV_STORAGE_NAMESPACE "kv"
#endif

static uint8_t kvs_crc8(uint8_t *buf, uint16_t len)
{
    uint8_t crc = 0x00;
    while (len--){
        crc ^= *buf++;
        for (uint8_t i = 8; i > 0; i--) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x31;
            else
                crc <<= 1;
        }
    }
    return crc;
}

int qm_hal_kv_init(void)
{
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    uint8_t crc;
    int payload = -1;
    int fd, ret = -1;
    char name[KVS_KEY_LEN] = {0};
    
    snprintf(name, KVS_KEY_LEN, "%s/%s.%s",  "/user_data", CONFIG_QM_KV_STORAGE_NAMESPACE, key);

    fd = open(name, O_RDONLY);
    if (fd < QM_EOK){
         return -QM_ERROR;
    }   
    ret = lseek(fd, 0, SEEK_END);
    if (ret <= QM_EOK){
        goto _exit;
    }
        
    payload = ret - 1;

    if (payload > *buffer_len){  //用于返回真实长度
        goto _exit;
    }
    
    if(buffer == NULL){
        goto _exit;
    }    

    ret = lseek(fd, 0, SEEK_SET);
    if (ret < 0){
        goto _exit;
    }

    ret = read(fd, buffer, payload);
    if (ret != payload){
        ret = -QM_ERROR;
        goto _exit;
    }
        

    ret = read(fd, &crc, sizeof(uint8_t));
    if (ret != sizeof(uint8_t)){
        ret = -QM_ERROR;
        goto _exit;
    }
        
    if (crc != kvs_crc8(buffer, payload)){
        ret = -QM_ERROR;
    }

_exit:
    close(fd);
    if(ret > 0){
        if(buffer_len != NULL){
            *buffer_len = payload;
        }
        return QM_EOK;
    }else{
        return ret;
    }
}

int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    uint8_t crc;
    
    int oflags = O_RDWR;
    int fd, ret, ret_close;
    char name[KVS_KEY_LEN] = {0};   

    snprintf(name, KVS_KEY_LEN, "%s/%s.%s",  "/user_data", CONFIG_QM_KV_STORAGE_NAMESPACE, key);

    fd = open(name, O_RDWR, 0777);
    if (fd < QM_EOK){
        oflags = O_RDWR | O_CREAT;
        fd = open(name, O_RDWR | O_CREAT, 0777);
        if (fd < 0){
            return -QM_ERROR;
        }  
    }else{
        if ((ret = remove(name)) < QM_EOK){
            goto _exit;
        }
        close(fd);
        oflags = O_RDWR | O_CREAT;
        fd = open(name, O_RDWR | O_CREAT, 0777);
        if (fd < 0){
            return -QM_ERROR;
        }      
    }
    ret = write(fd, value, len);
    if (ret != len){
        goto _exit;
    }
    crc = kvs_crc8((uint8_t *)value, len);
    ret = write(fd, &crc, sizeof(uint8_t));

_exit:
    ret_close = close(fd);
    if ((ret_close < 0) && (oflags & O_CREAT)) {
      unlink(name);
    }
    return ret < 0 ? ret : (ret_close < 0 ? ret_close : QM_EOK);
}

int qm_hal_kv_del(const char *key)
{
    char name[KVS_KEY_LEN] = {0};

    snprintf(name, KVS_KEY_LEN, "%s/%s.%s",  "/user_data", CONFIG_QM_KV_STORAGE_NAMESPACE, key);

    (void)remove(name);

    return QM_EOK;
}


int qm_hal_kv_clean(void)
{
    return QM_EOK;
}

#endif