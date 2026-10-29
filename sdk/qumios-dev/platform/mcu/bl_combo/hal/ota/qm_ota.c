#include "qm.h"
#include "qm_ota.h"

#include <blog.h>

#include <hal_boot2.h>
#include <hal_sys.h>
#include <hosal_ota.h>
#include <bl_sys_ota.h>
#include <bl_mtd.h>

#define HOSAL_OTA_FILE_SIZE_MAX    0x100000  // 1M
#define CHECK_HASH                  1
#define AUTO_RESET                  1

typedef struct ota_parm_s 
{
    HALPartition_Entry_Config ptEntry;
    bl_mtd_handle_t mtd_handle;
    uint32_t file_size;
    uint32_t sector_erased[((HOSAL_OTA_FILE_SIZE_MAX + 4095)/4096 + 31)/32];
}hosal_ota_parm_t;

static uint32_t offset_addr = 0;
static hosal_ota_parm_t *ota_parm = NULL; 

static int hosal_ota_erase(uint32_t offset, uint32_t len)
{
    uint32_t start_index, end_index, index;
    int ret;
    
    start_index = offset / 4096;
    end_index = (offset + len - 1) / 4096;
    
    for (index = start_index; index <= end_index; index++) {
        if ((ota_parm->sector_erased[index / 32] & (1U << (index % 32))) == 0) {
            ret = bl_mtd_erase(ota_parm->mtd_handle, index * 4096, 4096);
            if (ret) {
                QM_LOGE("1", "mtd erase failed\r\n");
                return -1;
            }
            ota_parm->sector_erased[index / 32] |= (1U << (index % 32));
        }
    }
    
    return 0;
}


int qm_ota_start(void *arg)
{
    int ret;
    uint32_t bin_size, ota_addr;
   
    if (ota_parm != NULL) {
        //重新开始时地址更改为0，防止flash地址踩踏
        QM_LOGD("1", "ota had start\r\n");
        goto __next;
    }
    
    ota_parm = (hosal_ota_parm_t *)qm_malloc(sizeof(hosal_ota_parm_t));
    if (NULL == ota_parm) {
        QM_LOGE("1", "have not enough memory\r\n");
        return -1;
    }
    memset(ota_parm, 0, sizeof(hosal_ota_parm_t));

#if 0
    ota_parm->file_size = file_size;
#endif

    ret = bl_mtd_open(BL_MTD_PARTITION_NAME_FW_DEFAULT, &ota_parm->mtd_handle, BL_MTD_OPEN_FLAG_BACKUP);
    if (ret) {
        qm_free(ota_parm);
        ota_parm = NULL;
        QM_LOGE("1", "Open Default FW partition failed\r\n");
        return -1;
    }

    if (hal_boot2_get_active_entries(BOOT2_PARTITION_TYPE_FW, &ota_parm->ptEntry)) {
        bl_mtd_close(ota_parm->mtd_handle);
        qm_free(ota_parm);
        ota_parm = NULL;
        
        QM_LOGE("1", "PtTable_Get_Active_Entries fail\r\n");
        return -1;
    }

__next:
    ota_addr = ota_parm->ptEntry.Address[!ota_parm->ptEntry.activeIndex];
    bin_size = ota_parm->ptEntry.maxLen[!ota_parm->ptEntry.activeIndex];
    
#if 0
    if (file_size > bin_size) {
        QM_LOGE("1", "file size is more than partition size\r\n");
        bl_mtd_close(ota_parm->mtd_handle);
        qm_free(ota_parm);
        ota_parm = NULL;
        return -1;
    }
#endif

    offset_addr = 0;

    QM_LOGD("1", "Starting OTA test. OTA size is %lu\r\n", bin_size);
    QM_LOGD("1", "[OTA] [TEST] activeIndex is %u, use OTA address=%08x\r\n", ota_parm->ptEntry.activeIndex, (unsigned int)ota_addr);

    return ret;
}

int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{
    uint32_t file_size, ret;
    if (ota_parm == NULL) {
        QM_LOGE("1", "please start ota first\r\n");
        return -1;
    }

    if (off_set == NULL || in_buf == NULL || !in_buf_len) {
        return -1;
    }
    
#if 0
    file_size = ota_parm->file_size;
    if ((filesize > file_size) || (NULL == in_buf) || ((offset + buf_len) > file_size) || (buf_len == 0)) {
        QM_LOGE("1", "parm is error!\r\n");
        return -1;
    }
#endif
    
    QM_LOGE("1", "write:[%d][%d]\r\n", *off_set, in_buf_len);   
    ret = hosal_ota_erase(*off_set, in_buf_len);
    if (ret) {
        QM_LOGE("1", "erase failed\r\n");
        return -1;
    }

    ret = bl_mtd_write(ota_parm->mtd_handle, *off_set, in_buf_len, in_buf);
    if (ret) {
        QM_LOGE("1", "mtd write failed\r\n");
        return -1;
    }
    
    offset_addr += in_buf_len;

    if(off_set){
        *off_set = offset_addr;
    }

    return ret;
}

int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    uint32_t src_offset = 0;
    if (ota_parm == NULL) {
        printf("please start ota first\r\n");
        return -1;
    }

    if(off_set == NULL || out_buf == NULL || out_buf_len == 0){
        printf("parm is error!\r\n");
        return -QM_EINVAL;
    }

    src_offset = *off_set;
    bl_mtd_read(ota_parm->mtd_handle, src_offset, out_buf_len, out_buf);

    *off_set += out_buf_len;

    return 0;
}

int qm_ota_abort(void *arg)
{
    (void)arg;

    if (ota_parm == NULL) {
        return QM_EOK;
    }

    bl_mtd_close(ota_parm->mtd_handle);
    qm_free(ota_parm);
    ota_parm = NULL;
    offset_addr = 0;
    return QM_EOK;
}

int qm_ota_end(void *arg)
{
    if (ota_parm == NULL) {
        QM_LOGE("1", "please start ota first\r\n");
        return -1;
    }
    uint32_t bin_size; 
    HALPartition_Entry_Config ptEntry;
   
    if (hal_boot2_get_active_entries(BOOT2_PARTITION_TYPE_FW, &ptEntry)) {
        QM_LOGE("1", "PtTable_Get_Active_Entries fail\r\n");
        bl_mtd_close(ota_parm->mtd_handle);
        qm_free(ota_parm);
        ota_parm = NULL;
        return -1;
    }
    QM_LOGD("1", "[OTA] prepare OTA partition info\r\n");

    ptEntry.len = offset_addr - 32;
    QM_LOGD("1", "[OTA] [TCP] Update PARTITION, partition len is %lu\r\n", ptEntry.len);
    hal_boot2_update_ptable(&ptEntry);
    bl_mtd_close(ota_parm->mtd_handle);
    qm_free(ota_parm);
    ota_parm = NULL;
    
    hal_reboot();
    
    return 0;
}
