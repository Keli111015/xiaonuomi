#include "qm_ota.h"

#include "qm.h"

#include <hal_boot2.h>
#include <hal_sys.h>
#include <bl_mtd.h>

static bl_mtd_handle_t handle;
static unsigned int flash_offset = 0;
static HALPartition_Entry_Config ptEntry;

/**
 * init ota partition
 *
 * @note   when ota start, maybe it need init something
 * @param  arg  extra info for ota init
 *
 * @return  0 : On success, -1 : If an error 
 * occurred with any step
 */
int qm_ota_start(void *arg)
{
    int ret;
    unsigned int ota_addr;
    uint32_t bin_size, part_size;
    uint8_t activeID;

    ret = bl_mtd_open(BL_MTD_PARTITION_NAME_FW_DEFAULT, &handle, BL_MTD_OPEN_FLAG_BACKUP);
    if (ret) {
        printf("Open Default FW partition failed\r\n");
        return;
    }

    activeID = hal_boot2_get_active_partition();
    printf("[OTA] activeID is %u\r\n", activeID);
    if (hal_boot2_get_active_entries(BOOT2_PARTITION_TYPE_FW, &ptEntry)) {
        printf("PtTable_Get_Active_Entries fail\r\n");
        bl_mtd_close(handle);
        return;
    }

    ota_addr = ptEntry.Address[!ptEntry.activeIndex];
    bin_size = ptEntry.maxLen[!ptEntry.activeIndex];
    part_size = ptEntry.maxLen[!ptEntry.activeIndex];
    (void) part_size;
    printf("Starting OTA test. OTA size is %lu\r\n", bin_size);

    printf("[OTA] activeIndex is %u, use OTA address=%08x\r\n", ptEntry.activeIndex, (unsigned int)ota_addr);

    printf("[OTA] Erase flash with size %lu...", bin_size);
    bl_mtd_erase_all(handle);

    printf("Done\r\n"); 
    return QM_EOK;
}

/**
 * Write data to an area on ota partition
 *
 * @param  off_set     Point to the start address that the data is written to, and
 *                     point to the last unwritten address after this function is
 *                     returned, so you can call this function serval times without
 *                     update this start address.
 * @param  inbuf       point to the data buffer that will be written to flash
 * @param  in_buf_len  The length of the buffer
 *
 * @return  0 : On success, -1 : If an error occurred with any step
 */
int qm_ota_write(uint32_t *off_set, uint8_t *in_buf, uint32_t in_buf_len)
{
    bl_mtd_write(handle, flash_offset, in_buf_len, in_buf);
    flash_offset += in_buf_len;
    return QM_EOK;
}

/**
 * Read data from an area on ota Flash to data buffer in RAM
 *
 * @param  off_set      Point to the start address that the data is read, and
 *                      point to the last unread address after this function is
 *                      returned, so you can call this function serval times without
 *                      update this start address.
 * @param  out_buf      Point to the data buffer that stores the data read from flash
 * @param  out_buf_len  The length of the buffer
 *
 * @return  0 : On success, -1 : If an error occurred with any step
 */
int qm_ota_read(uint32_t *off_set, uint8_t *out_buf, uint32_t out_buf_len)
{
    return -QM_EINVAL;
}

int qm_ota_abort(void *arg)
{
    (void)arg;
    flash_offset = 0;
    bl_mtd_close(handle);
    return QM_EOK;
}

/**
 * ota end
 *
 * @param  arg  extra info for ota end
 *
 * @return  0 : On success, -1 : If an error occurred with any step
 */
int qm_ota_end(void *arg)
{

#if 0
    if(!finish){
        flash_offset = 0;
        bl_mtd_close(handle);
        return 0;
    }
#endif

    ptEntry.len = flash_offset;
    printf("[OTA] Update PARTITION, partition len is %lu\r\n", ptEntry.len);
    hal_boot2_update_ptable(&ptEntry);
    printf("[OTA] Rebooting\r\n");
    bl_mtd_close(handle);
    hal_reboot();
    return QM_EOK;
}

