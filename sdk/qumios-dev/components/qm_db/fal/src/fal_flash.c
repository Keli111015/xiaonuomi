#include "fal.h"
#include "qm_db_log.h"
#include "qm_log.h"
#include "qm_config.h"

#if CONFIG_QM_KV_DB_SUPPORT || CONFIG_QM_TS_SUPPORT

#if CONFIG_SFUD_SUPPORT == 1
#include "sfud.h"
#endif

#define LOG_TAG  "fal_flash"

/* flash device table, must defined by user */
#if !defined(FAL_FLASH_DEV_TABLE)
#error "You must defined flash device table (FAL_FLASH_DEV_TABLE) on 'fal_cfg.h'"
#endif

static const struct fal_flash_dev * const device_table[] = FAL_FLASH_DEV_TABLE;
static const size_t device_table_len = sizeof(device_table) / sizeof(device_table[0]);
static uint8_t init_ok = 0;

/**
 * Initialize all flash device on FAL flash table
 *
 * @return result
 */
int fal_flash_init(void)
{
    size_t i;

    if (init_ok)
    {
        return 0;
    }
    
#if CONFIG_SFUD_SUPPORT == 1
    if (sfud_init() != SFUD_SUCCESS) {
        return -1;
    }
#endif

    for (i = 0; i < device_table_len; i++)
    {
        /* init flash device on flash table */
        if (device_table[i]->ops.init)
        {
            device_table[i]->ops.init();
        }
        QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "Flash device | %*.*s | addr: 0x%08lx | len: 0x%08x | blk_size: 0x%08x |initialized finish.",
                FAL_DEV_NAME_MAX, FAL_DEV_NAME_MAX, device_table[i]->name, device_table[i]->addr, device_table[i]->len,
                device_table[i]->blk_size);
    }

    init_ok = 1;
    return 0;
}

/**
 * find flash device by name
 *
 * @param name flash device name
 *
 * @return != NULL: flash device
 *            NULL: not found
 */
const struct fal_flash_dev *fal_flash_device_find(const char *name)
{
    if(init_ok == 0 || name == NULL){
        return NULL;
    }

    size_t i;

    for (i = 0; i < device_table_len; i++)
    {
        if (!strncmp(name, device_table[i]->name, FAL_DEV_NAME_MAX)) {
            return device_table[i];
        }
    }

    return NULL;
}

#endif

