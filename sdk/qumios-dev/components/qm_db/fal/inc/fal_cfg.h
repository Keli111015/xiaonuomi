#ifndef _FAL_CFG_H_
#define _FAL_CFG_H_

#define FAL_DEBUG       1
#define FAL_PART_HAS_TABLE_CFG


#ifndef FAL_FLASH_DEV_NAME
#define FAL_FLASH_DEV_NAME             "flashdev"
#endif

#ifndef FAL_KV_FLASH_NAME
#define FAL_KV_FLASH_NAME              "kv"
#endif

/* ===================== Flash device Configuration ========================= */
extern struct fal_flash_dev flash_dev;

/* flash device table */
#define FAL_FLASH_DEV_TABLE \
{                           \
    &flash_dev,             \
}

#endif /* _FAL_CFG_H_ */