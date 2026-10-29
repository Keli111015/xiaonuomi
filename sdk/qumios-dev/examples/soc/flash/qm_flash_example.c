#include "qm.h"
#include "qm_log.h"

#include "qm_flash.h"

#define LOG_TAG "TEST"

static char store_data[] = "QMOS FLASH Partition Operations Example (Read, Erase, Write)";
static char read_data[sizeof(store_data)] = {0};

void qm_application_start(void)
{
    uint32_t offset = 0;
    qm_logic_partition_t partition = {0};
    // Erase entire partition
    memset(read_data, 0xFF, sizeof(read_data));

    qm_flash_info_get(QM_PARTITION_PARAMETER_5, &partition);
    
    QM_LOGD(LOG_TAG, "FLASH INFO GET description : %s, start addr : 0x%04X, length: %d",
            partition.partition_description, partition.partition_start_addr, partition.partition_length);

    qm_flash_erase(QM_PARTITION_PARAMETER_5, partition.partition_start_addr, partition.partition_length);

    // Write the data, starting from the beginning of the partition
    offset = partition.partition_start_addr;
    qm_flash_write(QM_PARTITION_PARAMETER_5, &offset, store_data, sizeof(store_data));
    QM_LOGD(LOG_TAG, "Written data: %s", store_data);

    // Read back the data, checking that read data and written data match
    offset = partition.partition_start_addr;
    qm_flash_read(QM_PARTITION_PARAMETER_5, &offset, read_data, sizeof(read_data));

    if(memcmp(store_data, read_data, sizeof(read_data)) != 0){
        QM_LOGE(LOG_TAG, "flash read fail !! write : %s read: %s", store_data, read_data);
        while (1);   
    }
    QM_LOGD(LOG_TAG, "Read data: %s", read_data);

    // Erase the area where the data was written. Erase size shoud be a multiple of SPI_FLASH_SEC_SIZE
    // and also be SPI_FLASH_SEC_SIZE aligned
    qm_flash_erase(QM_PARTITION_PARAMETER_5, partition.partition_start_addr, partition.partition_length);

    // Read back the data (should all now be 0xFF's)
    memset(store_data, 0xFF, sizeof(read_data));
    qm_flash_read(QM_PARTITION_PARAMETER_5, &offset, read_data, sizeof(read_data));

    if(memcmp(store_data, read_data, sizeof(read_data)) != 0){
        QM_LOGE(LOG_TAG, "flash erase  fail !!");
        while (1);   
    }
    QM_LOGD(LOG_TAG, "Erased data");

    QM_LOGD(LOG_TAG, "Example end");
}