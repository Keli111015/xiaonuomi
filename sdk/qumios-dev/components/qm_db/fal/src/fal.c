#include "fal.h"
#include "qm_db_log.h"
#include "qm_log.h"
#include "qm_config.h"

#if CONFIG_QM_KV_DB_SUPPORT || CONFIG_QM_TS_SUPPORT

#define LOG_TAG  "fal"

static uint8_t init_ok = 0;

/**
 * FAL (Flash Abstraction Layer) initialization.
 * It will initialize all flash device and all flash partition.
 *
 * @return >= 0: partitions total number
 */
int fal_init(void)
{
    extern int fal_flash_init(void);
    extern int fal_partition_init(void);

    int result;

    /* initialize all flash device on FAL flash table */
    result = fal_flash_init();

    if (result < 0) {
        goto __exit;
    }

    /* initialize all flash partition on FAL partition table */
    result = fal_partition_init();

__exit:

    if ((result > 0) && (!init_ok))
    {
        init_ok = 1;
        QM_DB_LOGD(LOG_TAG, QM_DB_LOG_FLASH, "Flash Abstraction Layer initialize success.");
    }
    else if(result <= 0)
    {
        init_ok = 0;
        QM_DB_LOGE(LOG_TAG, "Flash Abstraction Layer initialize failed.");
    }

    return result;
}

/**
 * Check if the FAL is initialized successfully
 *
 * @return 0: not init or init failed; 1: init success
 */
int fal_init_check(void)
{
    return init_ok;
}

#endif
