#include "qm_ota.h"
#include "qm_log.h"

#include <hal_boot2.h>
#include <hal_sys.h>
#include <hosal_ota.h>
#include <bl_sys_ota.h>
#include <bl_mtd.h>


#define HOSAL_OTA_FILE_SIZE_MAX    0x100000  // 1M
#define CHECK_HASH                  1
#define AUTO_RESET                  1

static int file_size = 0;
int qm_ota_start(void *arg)
{   
    file_size = atoi(arg);
    int ret = hosal_ota_start(file_size);

    if (ret) {
        // printf("start ota failed\r\n");
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{
    int ret = hosal_ota_update(file_size, off_set, in_buf, in_buf_len); 

    if (ret) {
        // printf("update error\r\n");
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    int ret = hosal_ota_read(off_set, in_buf, in_buf_len);  

    if (ret) {
        // printf("update error\r\n");
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int qm_ota_abort(void *arg)
{
    (void)arg;
    file_size = 0;
    return QM_EOK;
}

int qm_ota_end(void *arg)
{
    int ret = hosal_ota_finish(CHECK_HASH, AUTO_RESET);

    if (ret) {
        // printf("finish error\r\n");
        return -QM_EINVAL;
    }

    return QM_EOK;
}









