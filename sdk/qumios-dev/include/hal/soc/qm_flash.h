#ifndef QM_FLASH_H
#define QM_FLASH_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_flash FLASH
 *  qm flash API.
 *
 *  @{
 */

#include "qm_types.h"

/* Define for partition_options set */
#define QM_PAR_OPT_READ_POS  ( 0 ) /**< Read attribute bit in partition_options */
#define QM_PAR_OPT_WRITE_POS ( 1 ) /**< write attribute bit in partition_options */

#define QM_PAR_OPT_READ_MASK  ( 0x1u << QM_PAR_OPT_READ_POS )     /**< Read attribute mask bit */
#define QM_PAR_OPT_WRITE_MASK ( 0x1u << QM_PAR_OPT_WRITE_POS )    /**< Write attribute mask bit */

#define QM_PAR_OPT_READ_DIS  ( 0x0u << QM_PAR_OPT_READ_POS )      /**< Read disable bit set */
#define QM_PAR_OPT_READ_EN   ( 0x1u << QM_PAR_OPT_READ_POS )      /**< Read enable bit set */
#define QM_PAR_OPT_WRITE_DIS ( 0x0u << QM_PAR_OPT_WRITE_POS )     /**< write disable bit set */
#define QM_PAR_OPT_WRITE_EN  ( 0x1u << QM_PAR_OPT_WRITE_POS )     /**< write enable bit set */


/* Define for partition owner */
typedef enum {
    QM_FLASH_EMBEDDED,
    QM_FLASH_SPI,
    QM_FLASH_QSPI,
    QM_FLASH_MAX,
    QM_FLASH_NONE,
} qm_flash_t;

/* hal flash partition define */
typedef enum {
    QM_PARTITION_ERROR = -1,
    QM_PARTITION_BOOTLOADER,   /**< Bootloader partition index */
    QM_PARTITION_APPLICATION,  /**< App partition index; Or OTA A partition */
    QM_PARTITION_OTA_TEMP,     /**< For OTA upgrade */
    QM_PARTITION_PARAMETER_1,  /**< For OTA args */
    QM_PARTITION_PARAMETER_2,  /**< For flashdb */
    QM_PARTITION_PARAMETER_3,  /**< For User defined */
    QM_PARTITION_PARAMETER_4,  /**< Used by security */
    QM_PARTITION_SPIFFS,       /**< For spiffs file system */
    QM_PARTITION_CUSTOM_1,     /**< For User defined */
    QM_PARTITION_CUSTOM_2,     /**< For User defined */
    QM_PARTITION_2ND_BOOT,     /**< For 2nd boot */
    QM_PARTITION_OTA_SUB,      /**< For Sub device OTA */
    QM_PARTITION_MAX,
    QM_PARTITION_NONE,
} qm_partition_t;

/* Hal flash partition manage struct */
typedef struct {
    qm_flash_t  partition_owner;
    const char  *partition_description;
    uint32_t     partition_start_addr;
    uint32_t     partition_length;
    uint32_t     partition_options; /**< Read\write enable or disable */
} qm_logic_partition_t;

/**
 * Get the information of the specified flash area
 *
 * @param[in]  in_partition     The target flash logical partition
 * @param[out]  partition       The buffer to store partition info
 *
 * @return  0: On success,  otherwise is error
 */
int32_t qm_flash_info_get(qm_partition_t in_partition, qm_logic_partition_t *partition);

/**
 * Erase an area on a Flash logical partition
 *
 * @note  Erase on an address will erase all data on a sector that the
 *        address is belonged to, this function does not save data that
 *        beyond the address area but in the affected sector, the data
 *        will be lost.
 *
 * @param[in]  in_partition  The target flash logical partition which should be erased
 * @param[in]  off_set       Start address of the erased flash area
 * @param[in]  size          Size of the erased flash area
 *
 * @return  0 : On success,  otherwise is error
 */
int32_t qm_flash_erase(qm_partition_t in_partition, uint32_t off_set, uint32_t size);

/**
 * Write data to an area on a flash logical partition without erase
 *
 * @param[in]  in_partition    The target flash logical partition which should be read which should be written
 * @param[in/out]  off_set     Point to the start address that the data is written to, and
 *                             point to the last unwritten address after this function is
 *                             returned, so you can call this function serval times without
 *                             update this start address.
 * @param[in]  inBuffer        point to the data buffer that will be written to flash
 * @param[in]  inBufferLength  The size of the buffer
 *
 * @return  0 : On success,  otherwise is error
 */
int32_t qm_flash_write(qm_partition_t in_partition, uint32_t *off_set, const void *in_buf, uint32_t in_buf_size);


/**
 * Read data from an area on a Flash to data buffer in RAM
 *
 * @param[in]  in_partition    The target flash logical partition which should be read
 * @param[in/out]  off_set     Point to the start address that the data is read, and
 *                             point to the last unread address after this function is
 *                             returned, so you can call this function serval times without
 *                             update this start address.
 * @param[in]  outBuffer       Point to the data buffer that stores the data read from flash
 * @param[in]  inBufferLength  The length of the buffer
 *
 * @return  0 : On success,  otherwise is error
 */
int32_t qm_flash_read(qm_partition_t in_partition, uint32_t *off_set, void *out_buf, uint32_t out_buf_size);                    


/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_FLASH_H */

