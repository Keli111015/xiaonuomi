#include "qm.h"
#include "arch_psm.h"
#include "qm_hal_kv.h"

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include "arch_psm.h"
#include "arch_dbg.h"
// #include "miio_ciphers.h"
// #include "miio_message.h"

#define MIIO_PSM_KV_FORMAT      "%s/%s.%s"

#ifndef CONFIG_MIIO_PSM_PATH
#define CONFIG_MIIO_PSM_PATH "psm.conf"
#endif

#define KVS_KEY_NAME            "qm_kv"
#define KVS_KEY_LEN             (2048)
#define KVS_BUF_MAX_LEN         (4 * 1024)


#if 0
static uint8_t kvs_crc8(uint8_t *buf, uint16_t len)
{
  uint8_t crc = 0x00;
  uint8_t i = 0;
  while (len--) {
    crc ^= *buf++;
    for (i = 8; i > 0; i--) {
      if (crc & 0x80)
        crc = (crc << 1) ^ 0x31;
      else
        crc <<= 1;
    }
  }
  return crc;
}
#endif

int qm_hal_kv_init(void)
{
    return QM_EOK;
}

int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
#if 0
    char name[CONFIG_MIIO_PSM_KEY_LEN];
    int payload = -1;
    struct stat buf;
    uint8_t crc;
    int fd, ret;

    if(key == NULL || buffer_len == NULL){
        return -QM_EINVAL;
    }
            
    snprintf(name, CONFIG_MIIO_PSM_KEY_LEN, MIIO_PSM_KV_FORMAT, CONFIG_MIIO_PSM_PATH, KVS_KEY_NAME, key);

    fd = open(name, O_RDONLY);
    if (fd < 0)
        return MIIO_ERROR;

    ret = fstat(fd, &buf);
    if (ret < 0){
        goto bail;
    }
    ret = buf.st_size - 1;
    payload = buf.st_size - 1;

    if (payload > *buffer_len){
        goto bail;
    }

    if(buffer == NULL){
        goto bail;
    }    

    ret = read(fd, buffer, payload);
    if (ret != payload){
        ret = -QM_ERROR;
        goto bail;
    }

    ret = read(fd, &crc, sizeof(uint8_t));
    if (ret != sizeof(uint8_t)){
        ret = -QM_ERROR;
        goto bail;
    }

    if (crc != kvs_crc8(buffer, payload)){
        ret = -QM_ERROR;
    }

bail:
    close(fd);
    if(ret > 0){
        if(buffer_len != NULL){
            *buffer_len = payload;
        }
        return QM_EOK;
    }
    
    return ret;
#endif
    //param check
    if ((key == NULL) || (buffer == NULL) || (buffer_len == NULL)) 
    {
        return -QM_EINVAL;
    }
    int ret = arch_psm_get_value(KVS_KEY_NAME, key, buffer, *buffer_len);
    if (ret < 0) 
    {
        return -QM_ERROR;
    }
    *buffer_len = ret;
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