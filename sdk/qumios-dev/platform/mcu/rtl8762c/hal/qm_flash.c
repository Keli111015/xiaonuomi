#include "qm_flash.h"
#include "flash_map.h"
#include "ftl_map.h"
#include "ftl.h"
#include "flash_device.h"
#include "qm_errno.h"

typedef struct{
    qm_partition_t in_partition;
    qm_logic_partition_t partition;
}qm_flash_info_t;

static qm_flash_info_t g_flash_info[] = {
    {QM_PARTITION_BOOTLOADER    ,{NULL}},
    {QM_PARTITION_APPLICATION   ,{NULL}},
    {QM_PARTITION_PARAMETER_1   ,{NULL}},
    {QM_PARTITION_PARAMETER_2   ,{QM_FLASH_EMBEDDED,"kv_storage",BKP_DATA2_ADDR,BKP_DATA2_SIZE,QM_PAR_OPT_READ_EN | QM_PAR_OPT_WRITE_EN}},
    {QM_PARTITION_PARAMETER_3   ,{NULL}},
    {QM_PARTITION_PARAMETER_4   ,{NULL}},
    {QM_PARTITION_SPIFFS        ,{NULL}},
    {QM_PARTITION_CUSTOM_1      ,{QM_FLASH_EMBEDDED,"usr_info",FTL_MAP_USER_RECORD_OFFSET_OFFSET,FTL_MAP_USER_RECORD_SIZE,QM_PAR_OPT_READ_EN | QM_PAR_OPT_WRITE_EN}},
    {QM_PARTITION_CUSTOM_2      ,{NULL}}, 
    {QM_PARTITION_2ND_BOOT      ,{NULL}},
    {QM_PARTITION_OTA_SUB       ,{NULL}},
};

int32_t qm_flash_info_get(qm_partition_t in_partition, qm_logic_partition_t *partition)
{
    qm_flash_info_t *flash_info = NULL;

    if(partition == NULL){
        return -QM_EINVAL;
    }

    if(in_partition < QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    } 

    flash_info = &g_flash_info[in_partition];

    *partition  = flash_info->partition;

    return 0;
}



int32_t qm_flash_erase(qm_partition_t in_partition, uint32_t off_set, uint32_t size)
{
    qm_flash_info_t *flash_info = NULL;
    
    if( size ==  0 || off_set < 0){
        return -QM_EINVAL;
    }
    if(in_partition < QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    } 

    flash_info = &g_flash_info[in_partition];

    uint32_t addr = flash_info->partition.partition_start_addr + off_set;
    
    if(addr > flash_info->partition.partition_start_addr 
            + flash_info->partition.partition_length){
        return -QM_EFULL;        
    }

    switch (flash_info->in_partition)
    {
        case QM_PARTITION_BOOTLOADER:  break; /**< Bootloader partition index */
        case QM_PARTITION_APPLICATION: break; /**< App partition index; Or OTA A partition */
        case QM_PARTITION_PARAMETER_1: break; /**< For OTA args */                              
        case QM_PARTITION_PARAMETER_2:{  /**< For kv storage */
        
            uint32_t erase_pages = size/4096;
            
            if (size % 4096 != 0) {
                erase_pages++;
            }

            for (uint8_t i = 0; i < erase_pages; i++) {
                flash_erase_locked(FLASH_ERASE_SECTOR,addr + (4096 * i));
            } 

        }break;
        case QM_PARTITION_PARAMETER_3:  break;/**< For User defined */
        case QM_PARTITION_PARAMETER_4:  break;/**< Used by security */
        case QM_PARTITION_SPIFFS:       break;/**< For spiffs file system */
        case QM_PARTITION_CUSTOM_1:{    /**< For User defined */
        // FTL NO  Erase api
        }break;
        case QM_PARTITION_CUSTOM_2:     break;/**< For User defined */
        case QM_PARTITION_2ND_BOOT:     break;/**< For 2nd boot */
        case QM_PARTITION_OTA_SUB:      break;/**< For Sub device OTA */
        default:
            break;
    }
      
    return 0;
}

int32_t qm_flash_write(qm_partition_t in_partition, uint32_t *off_set,
                        const void *in_buf, uint32_t in_buf_size)
{
    qm_flash_info_t *flash_info = NULL;
    int ret = 0;
    if( in_buf_size ==  0 || in_buf == NULL || off_set == NULL){
        return -QM_EINVAL;
    }
    if(in_partition <QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    } 
    
    flash_info = &g_flash_info[in_partition];

    uint32_t addr = flash_info->partition.partition_start_addr + *off_set;

    if(addr > flash_info->partition.partition_start_addr 
            + flash_info->partition.partition_length){
        return -QM_EFULL;        
    }
    switch (flash_info->in_partition)
    {
        case QM_PARTITION_BOOTLOADER:  break; /**< Bootloader partition index */
        case QM_PARTITION_APPLICATION: break; /**< App partition index; Or OTA A partition */
        case QM_PARTITION_PARAMETER_1: break; /**< For OTA args */                    
        case QM_PARTITION_PARAMETER_2:{       /**< For kv storage */

        ret = flash_auto_write_buffer_locked(addr,(uint32_t *)in_buf,in_buf_size);  
       
        }break;        
        case QM_PARTITION_PARAMETER_3:  break;/**< For User defined */
        case QM_PARTITION_PARAMETER_4:  break;/**< Used by security */
        case QM_PARTITION_SPIFFS:       break;/**< For spiffs file system */
        case QM_PARTITION_CUSTOM_1:{          /**< For User defined */
     
        ret = ftl_save((void *)in_buf,addr,(uint16_t)in_buf_size);

        }break;
        case QM_PARTITION_CUSTOM_2:     break;/**< For User defined */
        case QM_PARTITION_2ND_BOOT:     break;/**< For 2nd boot */
        case QM_PARTITION_OTA_SUB:      break;/**< For Sub device OTA */
        default:
            break;
    }
    *off_set += in_buf_size;

    return ret;
}


int32_t qm_flash_read(qm_partition_t in_partition, uint32_t *off_set,
                       void *out_buf, uint32_t out_buf_size)
{

    qm_flash_info_t *flash_info = NULL;
    int ret = 0;
    if( out_buf_size ==  0 || out_buf == NULL || off_set == NULL){
        return -QM_EINVAL;
    }

    if(in_partition <QM_PARTITION_BOOTLOADER || in_partition >= QM_PARTITION_MAX){
        return -QM_EINVAL;
    }    

    flash_info = &g_flash_info[in_partition];

    uint32_t addr = flash_info->partition.partition_start_addr + *off_set;

    if(addr > flash_info->partition.partition_start_addr 
            + flash_info->partition.partition_length){
        return -QM_EFULL;        
    }

    switch (flash_info->in_partition)
    {
        case QM_PARTITION_BOOTLOADER:  break; /**< Bootloader partition index */
        case QM_PARTITION_APPLICATION: break; /**< App partition index; Or OTA A partition */
        case QM_PARTITION_PARAMETER_1: break; /**< For OTA args */                     
        case QM_PARTITION_PARAMETER_2:{       /**< For kv storage */

            ret = flash_read_locked(addr,out_buf_size,out_buf); 
       
        }break;
        case QM_PARTITION_PARAMETER_3:  break;/**< For User defined */
        case QM_PARTITION_PARAMETER_4:  break;/**< Used by security */
        case QM_PARTITION_SPIFFS:       break;/**< For spiffs file system */
        case QM_PARTITION_CUSTOM_1:{          /**< For User defined */
            
            ret = ftl_load(out_buf,addr,out_buf_size);

        }break;
        case QM_PARTITION_CUSTOM_2:     break;/**< For User defined */
        case QM_PARTITION_2ND_BOOT:     break;/**< For 2nd boot */
        case QM_PARTITION_OTA_SUB:      break;/**< For Sub device OTA */
        default:
            break;
    }

     *off_set += out_buf_size;
     
    return ret;
}

