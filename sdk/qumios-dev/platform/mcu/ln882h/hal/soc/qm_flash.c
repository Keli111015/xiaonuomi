#include "qm_flash.h"
#include "qm_errno.h"

//这里不太懂#if 0

#if 0
int32_t qm_flash_info_get(qm_partition_t in_partition, qm_logic_partition_t *partition)
{
    if (in_partition == QM_PARTITION_2ND_BOOT || in_partition == QM_PARTITION_OTA_SUB) {
        return -QM_EINVAL;
    }

    switch (in_partition)
    {
    case QM_PARTITION_MAX:
        // in_partition = HAL_PARTITION_MAX;
        in_partition = FLASH_SECURITY_SIZE_MAX;
        break;
    case QM_PARTITION_NONE:
        // in_partition = HAL_PARTITION_NONE;
        in_partition = FLASH_SECURITY_SIZE_MAX;
        break;
    default:
        break;
    }

    // partition = hal_flash_get_info(in_partition);
    printf("partition->partition_length = %ld\r\n", partition->partition_length);
    return QM_EOK;
}

int32_t qm_flash_erase(qm_partition_t in_partition, uint32_t off_set, uint32_t size)
{
    if (in_partition == QM_PARTITION_2ND_BOOT || in_partition == QM_PARTITION_OTA_SUB) {
        return -QM_EINVAL;
    }
    if (off_set == NULL) {
        return -QM_EINVAL;
    }

    switch (in_partition)
    {
    case QM_PARTITION_MAX:
        in_partition = HAL_PARTITION_MAX;
        break;
    case QM_PARTITION_NONE:
        in_partition = HAL_PARTITION_NONE;
        break;
    default:
        break;
    }
    // uint32_t addr = off_set + CONFIG_QM_FLASH_PARAMETER_2_START_ADDR;
    // int ret = hosal_flash_raw_erase(addr, size);
    // if (ret) {
    //     return -QM_EINVAL;
    // }

    return QM_EOK;
}

int32_t qm_flash_write(qm_partition_t in_partition, uint32_t *off_set, const void *in_buf, uint32_t in_buf_size)
{
    if (in_partition == QM_PARTITION_2ND_BOOT || in_partition == QM_PARTITION_OTA_SUB) {
        return -QM_EINVAL;
    }
    if (off_set == NULL) {
        return -QM_EINVAL;
    }

    switch (in_partition)
    {
    case QM_PARTITION_MAX:
        in_partition = HAL_PARTITION_MAX;//这里要改
        break;
    case QM_PARTITION_NONE:
        in_partition = HAL_PARTITION_NONE;//这里也要改
        break;
    default:
        break;
    }
    // uint32_t addr = *off_set + CONFIG_QM_FLASH_PARAMETER_2_START_ADDR;
    // int ret = hosal_flash_raw_write(in_buf, addr, in_buf_size);
    // if (ret) {
    //     return -QM_EINVAL;
    // }

    return QM_EOK;
}

int32_t qm_flash_read(qm_partition_t in_partition, uint32_t *off_set, void *out_buf, uint32_t out_buf_size)
{
    if (in_partition == QM_PARTITION_2ND_BOOT || in_partition == QM_PARTITION_OTA_SUB) {
        return -QM_EINVAL;
    }

    switch (in_partition)
    {
    case QM_PARTITION_MAX:
        in_partition = HAL_PARTITION_MAX;//要改
        break;
    case QM_PARTITION_NONE:
        in_partition = HAL_PARTITION_NONE;//要改
        break;
    default:
        break;
    }
    // uint32_t addr = *off_set + CONFIG_QM_FLASH_PARAMETER_2_START_ADDR;
    // int ret = hosal_flash_raw_read(out_buf, addr, out_buf_size);
    // if (ret) {
    //     return -QM_EINVAL;
    // }

    return QM_EOK;
}


#endif

int qm_hal_kv_init(void)
{

    return QM_EOK;
}


int qm_hal_kv_set(const char *key, const void *value, int len, int sync)
{
    if(key == NULL || value == NULL || len == 0){
        return -QM_EINVAL;
    }
    if(ef_set_env_blob(key, value, len) != QM_EOK){
        return -QM_EINVAL;
    }
    return QM_EOK;
}


int qm_hal_kv_get(const char *key, void *buffer, int *buffer_len)
{
    size_t len = 0;
    if(key == NULL || buffer == NULL || *buffer_len == 0){
        return -QM_EINVAL;
    }
    if(ef_get_env_blob(key, buffer, *buffer_len, &len) <= 0){
        return -QM_EINVAL;
    }
    return len;
}


int qm_hal_kv_del(const char *key)
{

    return QM_EOK;
}


int qm_hal_kv_clean(void)
{

    return QM_EOK;
}




