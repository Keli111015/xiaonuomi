
#include "fal.h"
#include "qm_db_log.h"
#include "fal_def.h"
#include "qm_types.h"
#include "qm_log.h"
#include "qm_config.h"
#include "qm_kernel.h"

#if CONFIG_QM_KV_DB_SUPPORT || CONFIG_QM_TS_SUPPORT

#define LOG_TAG  "fal_partition"

/* partition magic word */
#define FAL_PART_MAGIC_WORD         0x45503130
#define FAL_PART_MAGIC_WORD_H       0x4550L
#define FAL_PART_MAGIC_WORD_L       0x3130L
#define FAL_PART_MAGIC_WROD         0x45503130

struct part_flash_info
{
    const struct fal_flash_dev *flash_dev;
};

/**
 * FAL partition table config has defined on 'fal_cfg.h'.
 * When this option is disable, it will auto find the partition table on a specified location in flash partition.
 */

static const struct fal_partition *partition_table = NULL;
/* partition and flash object information cache table */
static struct part_flash_info *part_flash_cache = NULL;

static uint8_t init_ok = 0;
static size_t partition_table_len = 0;

/**
 * print the partition table
 */
void fal_show_part_table(void)
{
    char *item1 = "name", *item2 = "flash_dev";
    size_t i, part_name_max = strlen(item1), flash_dev_name_max = strlen(item2);
    const struct fal_partition *part;

    if (partition_table_len)
    {
        for (i = 0; i < partition_table_len; i++)
        {
            part = &partition_table[i];
            if (strlen(part->name) > part_name_max)
            {
                part_name_max = strlen(part->name);
            }
            if (strlen(part->flash_name) > flash_dev_name_max)
            {
                flash_dev_name_max = strlen(part->flash_name);
            }
        }
    }
    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "==================== FAL partition table ====================");
    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "| %-*.*s | %-*.*s |   offset   |    length  |", part_name_max, FAL_DEV_NAME_MAX, item1, flash_dev_name_max,
            FAL_DEV_NAME_MAX, item2);
    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "-------------------------------------------------------------");
    for (i = 0; i < partition_table_len; i++)
    {
        part = &partition_table[i];

        QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "| %-*.*s | %-*.*s | 0x%08lx | 0x%08x |", part_name_max, FAL_DEV_NAME_MAX, part->name, flash_dev_name_max,
                FAL_DEV_NAME_MAX, part->flash_name, part->offset, part->len);
    }
    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "=============================================================");
}

static int check_and_update_part_cache(const struct fal_partition *table, uint32_t len)
{
    const struct fal_flash_dev *flash_dev = NULL;
    int i = 0;

    if(part_flash_cache == NULL){
        part_flash_cache = (struct part_flash_info*)qm_malloc(sizeof(struct part_flash_info) * len);
        if(part_flash_cache == NULL){
            return -1;
        }
    }

    for (i = 0; i < len; i++)
    {
        QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH,
                   "table[%d].flash_name: %s", i, table[i].flash_name);
        flash_dev = fal_flash_device_find(table[i].flash_name);
        if (flash_dev == NULL)
        {
            QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "Warning: Do NOT found the flash device(%s).", table[i].flash_name);
            continue;
        }

        if (table[i].offset >= flash_dev->len)
        {
            QM_DB_LOGE(LOG_TAG, "Initialize failed! Partition(%s) offset address(%ld) out of flash bound(<%d).",
                    table[i].name, table[i].offset, flash_dev->len);
            partition_table_len = 0;
            qm_free(part_flash_cache);
            return -1;
        }

        part_flash_cache[i].flash_dev = flash_dev;
    }

    return 0;
}

/**
 * Initialize all flash partition on FAL partition table
 *
 * @return partitions total number
 */
int fal_partition_init(void)
{
    int comma_num = 0;
#ifdef CONFIG_QM_KV_DB_FLASH_PARTITION_INFO
    char *kv_partition_info = NULL;
#endif
#ifdef CONFIG_QM_TS_DB_FLASH_PARTITION_INFO
    int i = 0;
    int j = 0;
    char *ts_partition_info = NULL;
#endif
    fal_partition_t partition = NULL;
    int table_len = 0;

    if (init_ok)
    {
        return partition_table_len;
    }

#ifdef CONFIG_QM_KV_DB_FLASH_PARTITION_INFO

    kv_partition_info = CONFIG_QM_KV_DB_FLASH_PARTITION_INFO;
    if(strlen(kv_partition_info) == 0){
        return 0;
    }
    //check
    while(1){
        if(*kv_partition_info == '\0'){
            break;
        }
        if(*kv_partition_info == ','){
            comma_num++;
        }
        kv_partition_info++;
    }
    if(comma_num != 2){
        return 0;
    }
    table_len += 1;
#endif

#ifdef CONFIG_QM_TS_DB_FLASH_PARTITION_INFO

    ts_partition_info = CONFIG_QM_TS_DB_FLASH_PARTITION_INFO;
    
    if(strlen(ts_partition_info) == 0){
        return 0;
    }
    
    comma_num = 0;
    //calculate partition table num
    while(1){
        if(*ts_partition_info == '\0'){
            break;
        }
        if(*ts_partition_info == ','){
            comma_num++;
        }
        ts_partition_info++;
    }

    QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "comma_num: %d", comma_num);

    if(comma_num != 2 && (comma_num - 2) % 3 != 0){
        QM_DB_LOGE(LOG_TAG, "flash partition info error");
        return 0;
    }

    if(comma_num == 2){
        table_len += 1;
    }else{
        table_len += (comma_num - 2) / 3 + 1;
    }

#endif

    partition = (fal_partition_t)qm_malloc(sizeof(struct fal_partition) * table_len);
    if(partition == NULL){
        return 0;
    }
    memset(partition, 0, sizeof(struct fal_partition) * table_len);

#ifdef CONFIG_QM_KV_DB_FLASH_PARTITION_INFO

    kv_partition_info = CONFIG_QM_KV_DB_FLASH_PARTITION_INFO;
    memcpy(partition[i].flash_name, FAL_FLASH_DEV_NAME, strlen(FAL_FLASH_DEV_NAME));
    memcpy(partition[i].name, FAL_KV_FLASH_NAME, strlen(FAL_KV_FLASH_NAME));
    partition[i].magic_word = FAL_PART_MAGIC_WORD;

    kv_partition_info = strstr(kv_partition_info, ",") + 1;
    partition[i].offset = (uint32_t)atoi(kv_partition_info);

    kv_partition_info = strstr(kv_partition_info, ",") + 1;
    partition[i].len = (uint32_t)atoi(kv_partition_info);

    i++;
#endif

#ifdef CONFIG_QM_TS_DB_FLASH_PARTITION_INFO
    
    ts_partition_info = CONFIG_QM_TS_DB_FLASH_PARTITION_INFO;

    for(; i < table_len; i++){

        memcpy(partition[i].flash_name, FAL_FLASH_DEV_NAME, strlen(FAL_FLASH_DEV_NAME));
        j = 0;
        while(*ts_partition_info != ','){
            partition[i].name[j++] = *ts_partition_info;
            ts_partition_info++;
        }
        ts_partition_info++;

        partition[i].magic_word = FAL_PART_MAGIC_WORD;
        partition[i].offset = (uint32_t)atoi(ts_partition_info);

        ts_partition_info = strstr(ts_partition_info, ",") + 1;
        partition[i].len = (uint32_t)atoi(ts_partition_info);

        ts_partition_info = strstr(ts_partition_info, ",") + 1;
        
    }
#endif

    partition_table_len = table_len;
    partition_table = partition;

    /* check the partition table device exists */
    if (check_and_update_part_cache(partition_table, partition_table_len) != 0)
    {
        goto _exit;
    }

    init_ok = 1;

_exit:

#if FAL_DEBUG
    fal_show_part_table();
#endif

    return partition_table_len;
}

/**
 * find the partition by name
 *
 * @param name partition name
 *
 * @return != NULL: partition
 *            NULL: not found
 */
const struct fal_partition *fal_partition_find(const char *name)
{
    if(init_ok == 0){
        return NULL;
    }

    size_t i;

    for (i = 0; i < partition_table_len; i++)
    {
        if (!strcmp(name, partition_table[i].name))
        {
            return &partition_table[i];
        }
    }

    return NULL;
}

static const struct fal_flash_dev *flash_device_find_by_part(const struct fal_partition *part)
{
    if(part < partition_table || part > &partition_table[partition_table_len - 1]){
        return NULL;
    }

    return part_flash_cache[part - partition_table].flash_dev;
}

/**
 * get the partition table
 *
 * @param len return the partition table length
 *
 * @return partition table
 */
const struct fal_partition *fal_get_partition_table(size_t *len)
{
    if(init_ok == 0 || len == NULL){
        return NULL;
    }

    *len = partition_table_len;

    return partition_table;
}

/**
 * set partition table temporarily
 * This setting will modify the partition table temporarily, the setting will be lost after restart.
 *
 * @param table partition table
 * @param len partition table length
 */
void fal_set_partition_table_temp(struct fal_partition *table, size_t len)
{
    if(init_ok == 0 || table == NULL){
        return;
    }

    check_and_update_part_cache(table, len);

    partition_table_len = len;
    partition_table = table;
}

/**
 * read data from partition
 *
 * @param part partition
 * @param addr relative address for partition
 * @param buf read buffer
 * @param size read size
 *
 * @return >= 0: successful read data size
 *           -1: error
 */
int fal_partition_read(const struct fal_partition *part, uint32_t addr, uint8_t *buf, size_t size)
{
    int ret = 0;
    const struct fal_flash_dev *flash_dev = NULL;

    if(part == NULL || buf == NULL){
        return -1;
    }

    if (addr + size > part->len)
    {
        QM_DB_LOGE(LOG_TAG, "Partition read error! Partition address out of bound.");
        return -1;
    }

    flash_dev = flash_device_find_by_part(part);
    if (flash_dev == NULL)
    {
        QM_DB_LOGE(LOG_TAG, "Partition read error! Don't found flash device(%s) of the partition(%s).", part->flash_name, part->name);
        return -1;
    }

    ret = flash_dev->ops.read(part->offset + addr, buf, size);
    if (ret < 0)
    {
        QM_DB_LOGE(LOG_TAG, "Partition read error! Flash device(%s) read error!", part->flash_name);
    }

    return ret;
}

/**
 * write data to partition
 *
 * @param part partition
 * @param addr relative address for partition
 * @param buf write buffer
 * @param size write size
 *
 * @return >= 0: successful write data size
 *           -1: error
 */
int fal_partition_write(const struct fal_partition *part, uint32_t addr, const uint8_t *buf, size_t size)
{
    int ret = 0;
    const struct fal_flash_dev *flash_dev = NULL;

    if(part == NULL || buf == NULL){
        return -1;
    }

    if (addr + size > part->len)
    {
        QM_DB_LOGE(LOG_TAG, "Partition write error! Partition address out of bound.");
        return -1;
    }

    flash_dev = flash_device_find_by_part(part);
    if (flash_dev == NULL)
    {
        QM_DB_LOGE(LOG_TAG, "Partition write error!  Don't found flash device(%s) of the partition(%s).", part->flash_name, part->name);
        return -1;
    }

    ret = flash_dev->ops.write(part->offset + addr, buf, size);
    if (ret < 0)
    {
        QM_DB_LOGE(LOG_TAG, "Partition write error! Flash device(%s) write error!", part->flash_name);
    }

    return ret;
}

/**
 * erase partition data
 *
 * @param part partition
 * @param addr relative address for partition
 * @param size erase size
 *
 * @return >= 0: successful erased data size
 *           -1: error
 */
int fal_partition_erase(const struct fal_partition *part, uint32_t addr, size_t size)
{
    int ret = 0;
    const struct fal_flash_dev *flash_dev = NULL;

    if(part == NULL){
        return -1;
    }

    if (addr + size > part->len)
    {
        QM_DB_LOGE(LOG_TAG, "Partition erase error! Partition address out of bound.");
        return -1;
    }

    flash_dev = flash_device_find_by_part(part);
    if (flash_dev == NULL)
    {
        QM_DB_LOGE(LOG_TAG, "Partition erase error! Don't found flash device(%s) of the partition(%s).", part->flash_name, part->name);
        return -1;
    }

    ret = flash_dev->ops.erase(part->offset + addr, size);
    if (ret < 0)
    {
        QM_DB_LOGE(LOG_TAG, "Partition erase error! Flash device(%s) erase error!", part->flash_name);
    }

    return ret;
}

/**
 * erase partition all data
 *
 * @param part partition
 *
 * @return >= 0: successful erased data size
 *           -1: error
 */
int fal_partition_erase_all(const struct fal_partition *part)
{
    return fal_partition_erase(part, 0, part->len);
}

#endif
