#include "qm_flash.h"
#include "qm.h"
#include "qm_log.h"

#include "esp_partition.h"
#include "esp_spi_flash.h"

#define LOG_TAG "hal flash"

typedef struct{
    const esp_partition_t *handle;
    qm_partition_t in_partition;
    qm_logic_partition_t partition;
}qm_flash_info_t;


static qm_flash_info_t g_flash_info[] = {
#if CONFIG_QM_FLASH_PARTITION_PARAMETER_2_SUPPORT
    {NULL, QM_PARTITION_PARAMETER_2, {QM_FLASH_EMBEDDED, "flashdb", CONFIG_QM_FLASH_PARAMETER_2_START_ADDR,CONFIG_QM_FLASH_PARAMETER_2_SIZE,QM_PAR_OPT_READ_EN | QM_PAR_OPT_WRITE_EN}},
#endif
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

    if (flash_info->handle == NULL){
        flash_info->handle = esp_partition_find_first(  ESP_PARTITION_TYPE_DATA, 
                                                        ESP_PARTITION_SUBTYPE_ANY, 
                                                        flash_info->partition.partition_description);
        if(flash_info->handle == NULL){
            QM_LOGE(LOG_TAG, "ESP partiton find fail please check partiton table name: %s type: data", flash_info->partition.partition_description);
            return NULL;
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
    
    if( size ==  0 ){
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

    erase_pages = size / SPI_FLASH_SEC_SIZE;
    
    if(size % SPI_FLASH_SEC_SIZE != 0) {
        erase_pages++;
    }

    switch (flash_info->in_partition)
    {
                     
        case QM_PARTITION_PARAMETER_2:/**< For flashdb */

            for (uint8_t i = 0; i < erase_pages; i++) {
                esp_partition_erase_range(flash_info->handle, addr + (4096 * i), SPI_FLASH_SEC_SIZE);
            } 

        break;

        default:
        break;
    }
      
    return QM_EOK;
}

int32_t qm_flash_write(qm_partition_t in_partition, uint32_t *off_set,
                        const void *in_buf, uint32_t in_buf_size)
{
    int ret = QM_EOK;
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
            ret = esp_partition_write(flash_info->handle, addr, in_buf, in_buf_size);
        break;

        default:
        break;
    }

    if(ret != QM_EOK){
        return ret;
    }

    *off_set += in_buf_size;

    return ret;
}


int32_t qm_flash_read(qm_partition_t in_partition, uint32_t *off_set,
                       void *out_buf, uint32_t out_buf_size)
{
    int ret = QM_EOK;
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
            ret = esp_partition_read(flash_info->handle, addr, out_buf, out_buf_size);
        break;

        default:
        break;
    }

    if(ret != QM_EOK){
        return ret;
    }

    *off_set += out_buf_size;
     
    return ret;
}

