#include "qm.h"
#include "qm_flash.h"

#include "ls_hal_flash.h"

//参考链接 https://docs.linkedsemi.com/doc?id=873&key=14

#define LOG_TAG "FLASH"
// #define LE501X_OTA_APP_ADDR_2   (0x18034000 + 0x24000 + 0x16800)
#define LE501X_OTA_APP_ADDR_2   (0x18034000 + 0x24000 + 0x1E000) 

#include "reg_base_addr.h"

typedef struct{
    qm_partition_t in_partition;
    qm_logic_partition_t partition;
}qm_flash_info_t;

static qm_flash_info_t g_flash_info[] = {
    {QM_PARTITION_BOOTLOADER        ,{NULL}},
    {QM_PARTITION_CUSTOM_1          ,{NULL}},
    {QM_PARTITION_APPLICATION       ,{NULL}},
    {QM_PARTITION_OTA_TEMP          ,{NULL}},
#if CONFIG_QM_FLASH_PARTITION_PARAMETER_2_SUPPORT
    {QM_PARTITION_PARAMETER_2, {QM_FLASH_EMBEDDED, "flashdb", CONFIG_QM_FLASH_PARAMETER_2_START_ADDR,CONFIG_QM_FLASH_PARAMETER_2_SIZE,QM_PAR_OPT_READ_EN | QM_PAR_OPT_WRITE_EN}},
#endif
    {QM_PARTITION_PARAMETER_3       ,{NULL}},
    {QM_PARTITION_OTA_SUB           ,{NULL}},
};

static qm_flash_info_t *qm_flash_info_handle_get(qm_partition_t in_partition)
{
    int i = 0;
    qm_flash_info_t *flash_info = NULL;
    for(i = 0; i < QM_ARRAY_SIZE(g_flash_info); i++)
    {
        if (g_flash_info[i].in_partition == in_partition){
            flash_info = &g_flash_info[i];
            break;
        }
    }

    return flash_info;
}

int32_t qm_flash_info_get(qm_partition_t in_partition, qm_logic_partition_t *partition)
{
    qm_flash_info_t *flash_info = NULL;

    if(partition == NULL){
        return -QM_EINVAL;
    }

    if(in_partition < QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    } 

    flash_info = qm_flash_info_handle_get(in_partition);
    if (flash_info == NULL){
        return -QM_EINVAL;
    }

    memcpy(partition, &flash_info->partition, sizeof(qm_logic_partition_t));

    return QM_EOK;
}

int32_t qm_flash_erase(qm_partition_t in_partition, uint32_t off_set, uint32_t size)
{
    uint32_t addr = 0;
    uint32_t erase_pages = 0;
    qm_flash_info_t *flash_info = NULL;
    
    if( size ==  0){
        return -QM_EINVAL;
    }

    if(in_partition < QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    } 

    flash_info = qm_flash_info_handle_get(in_partition);
    if (flash_info == NULL){
        return -QM_EINVAL;
    }
    
    addr = flash_info->partition.partition_start_addr + off_set;
    if(addr > flash_info->partition.partition_start_addr 
            + flash_info->partition.partition_length){
        return -QM_EFULL;        
    }

    erase_pages = size / FLASH_SECTOR_SIZE;
    
    if(size % FLASH_SECTOR_SIZE != 0) {
        erase_pages++;
    }

    switch (flash_info->in_partition)
    {
        case QM_PARTITION_PARAMETER_2:  /**< For flashdb */
        
            for (uint8_t i = 0; i < erase_pages; i++) {
                hal_flash_sector_erase(addr + (FLASH_SECTOR_SIZE * i) - FLASH_BASE_ADDR);
            } 

        break;

        default:
        break;
    }    

    return QM_EOK;
}

int32_t qm_flash_write(qm_partition_t in_partition, uint32_t *off_set, const void *in_buf, uint32_t in_buf_size)
{
    uint32_t addr = 0;
    qm_flash_info_t *flash_info = NULL;

    if( in_buf_size ==  0 || in_buf == NULL || off_set == NULL){
        return -QM_EINVAL;
    }

    if(in_partition <QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    } 
    
    flash_info = qm_flash_info_handle_get(in_partition);
    if (flash_info == NULL){
        return -QM_EINVAL;
    }

    addr = flash_info->partition.partition_start_addr + *off_set;

    if(addr > flash_info->partition.partition_start_addr 
            + flash_info->partition.partition_length){
        return -QM_EFULL;        
    }

    switch (flash_info->in_partition)
    {
                     
        case QM_PARTITION_PARAMETER_2:/**< For flashdb */
            hal_flash_multi_io_page_program(addr,(uint8_t *)in_buf,in_buf_size);
        break;

        default:
        break;
    }

    *off_set += in_buf_size;

    return QM_EOK;
}

int32_t qm_flash_read(qm_partition_t in_partition, uint32_t *off_set, void *out_buf, uint32_t out_buf_size)
{

    uint32_t addr = 0;
    qm_flash_info_t *flash_info = NULL;

    if( out_buf_size ==  0 || out_buf == NULL || off_set == NULL){
        return -QM_EINVAL;
    }

    if(in_partition <QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    }    

    flash_info = qm_flash_info_handle_get(in_partition);
    if (flash_info == NULL){
        return -QM_EINVAL;
    }

    addr = flash_info->partition.partition_start_addr + *off_set;

    if(addr > flash_info->partition.partition_start_addr 
            + flash_info->partition.partition_length){
        return -QM_EFULL;        
    }

    switch (flash_info->in_partition)
    {
                     
        case QM_PARTITION_PARAMETER_2:/**< For flashdb */
            hal_flash_multi_io_read(addr - FLASH_BASE_ADDR, out_buf, out_buf_size);
        break;

        default:
        break;
    }

    *off_set += out_buf_size;
     
    return QM_EOK;
}





