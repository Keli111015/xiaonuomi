#include "fal.h"
#include "fal_cfg.h"
#include "qm_config.h"

#if CONFIG_QM_FLASHDB_EXTERNAL_FLASH_SPI || CONFIG_SFUD_SUPPORT

#include "sfud.h"

static int init(void);
static int read(uint32_t offset, uint8_t *buf, uint32_t size);
static int write(uint32_t offset, const uint8_t *buf, uint32_t size);
static int erase(uint32_t offset, uint32_t size);

static sfud_flash_t sfud_dev = NULL;
struct fal_flash_dev flash_dev =
{
    .name       = FAL_FLASH_DEV_NAME,
    .addr       = 0,
    .len        = 8 * 1024 * 1024,
    .blk_size   = 4096,
    .ops        = {init, read, write, erase},
    .write_gran = 1
};

static int init(void)
{
    sfud_dev = (sfud_flash_t)sfud_get_device_table();
    if (NULL == sfud_dev){
        return -1;
    }

    /* update the flash chip information */
    flash_dev.blk_size = sfud_dev->chip.erase_gran;
    flash_dev.len = sfud_dev->chip.capacity;

    return 0;
}

static int read(uint32_t offset, uint8_t *buf, uint32_t size)
{

    sfud_read(sfud_dev, flash_dev.addr + offset, size, buf);

    return size;
}

static int write(uint32_t offset, const uint8_t *buf, uint32_t size)
{
    if (sfud_write(sfud_dev, flash_dev.addr + offset, size, buf) != SFUD_SUCCESS)
    {
        return -1;
    }

    return size; 
}

static int erase(uint32_t offset, uint32_t size)
{
    if (sfud_erase(sfud_dev, flash_dev.addr + offset, size) != SFUD_SUCCESS)
    {
        return -1;
    }

    return size;
}

#elif CONFIG_QM_FLASHDB_INTERNAL_FLASH

#if CONFIG_QM_FLASH_SUPPORT || CONFIG_QM_FLASH_PARTITION_PARAMETER_2_SUPPORT

#include "qm_flash.h"


static int init(void);
static int read(uint32_t offset, uint8_t *buf, uint32_t size);
static int write(uint32_t offset, const uint8_t *buf, uint32_t size);
static int erase(uint32_t offset, uint32_t size);

struct fal_flash_dev flash_dev =
{
    .name       = FAL_FLASH_DEV_NAME,
    .addr       = CONFIG_QM_FLASH_PARAMETER_2_START_ADDR,
    .len        = CONFIG_QM_FLASH_PARAMETER_2_SIZE,
    .blk_size   = CONFIG_QM_FLASH_BLOCK_SIZE,
    .ops        = {init, read, write, erase},
    .write_gran = CONFIG_QM_FLASH_WRITE_GRAN
};

static int init(void)
{
    return 0;
}

static int read(uint32_t offset, uint8_t *buf, uint32_t size)
{
    qm_flash_read(QM_PARTITION_PARAMETER_2, &offset, buf, size);
    return size;
}

static int write(uint32_t offset, const uint8_t *buf, uint32_t size)
{
    qm_flash_write(QM_PARTITION_PARAMETER_2, &offset, buf, size);
    return size; 
}

static int erase(uint32_t offset, uint32_t size)
{
    qm_flash_erase(QM_PARTITION_PARAMETER_2, offset, size);
    return size;
}

#else

#error "must config qm flash and partition 2"

#endif

#endif //QM_FLASHDB_INTERNAL_FLASH

