#include "qm.h"
#include "qm_flash.h"

#include "include.h"
#include "driver_flash.h"

#define LOG_TAG "FLASH"

#define MIN_FLASH_SIZE  (256)
#define FLASH_ADDR_BASE  (FLASH_BASE_ADDR + FLASH_TOTAL_SIZE - FLASH_TAIL_RS)


int32_t qm_flash_erase(qm_partition_t in_partition, uint32_t off_set, uint32_t size)
{
    uint32_t addr = 0;
    uint32_t erase_pages = 0;
    
    if( size ==  0){
        return -QM_EINVAL;
    }

    if(in_partition < QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    } 


    erase_pages = size / FLASH_SECTOR_SIZE;
    
    if(size % FLASH_SECTOR_SIZE != 0) {
        erase_pages++;
    }


    flash_unlock();
    for (uint8_t i = 0; i < erase_pages; i++) {
        flash_sector_erase(addr + FLASH_ADDR_BASE + (FLASH_SECTOR_SIZE * i));
        
    } 
    flash_lock();

    return QM_EOK;
}

int32_t qm_flash_write(qm_partition_t in_partition, uint32_t *off_set, const void *in_buf, uint32_t in_buf_size)
{
    uint32_t addr = 0;
    if( in_buf_size ==  0 || in_buf == NULL || off_set == NULL){
        return -QM_EINVAL;
    }

    if(in_partition <QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    } 

    flash_unlock();
    flash_write(addr + FLASH_ADDR_BASE,(uint8_t *)in_buf,in_buf_size);
    flash_lock();

    *off_set += in_buf_size;

    return QM_EOK;
}

int32_t qm_flash_read(qm_partition_t in_partition, uint32_t *off_set, void *out_buf, uint32_t out_buf_size)
{

    uint32_t addr = 0;
    if( out_buf_size ==  0 || out_buf == NULL || off_set == NULL){
        return -QM_EINVAL;
    }

    if(in_partition <QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    }    


    flash_unlock();
    flash_read(addr + FLASH_ADDR_BASE, out_buf, out_buf_size);
    flash_lock();

    *off_set += out_buf_size;
     
    return QM_EOK;
}





