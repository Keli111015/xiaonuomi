/*
 * Copyright (c) 2020, Armink, <armink.ztl@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief configuration file
 */

#ifndef _FDB_CFG_H_
#define _FDB_CFG_H_

#include "qm_config.h"


#if CONFIG_QM_KV_DB_SUPPORT

/* using KVDB feature */
#define FDB_USING_KVDB

#ifdef FDB_USING_KVDB
/* Auto update KV to latest default when current KVDB version number is changed. @see fdb_kvdb.ver_num */
/* #define FDB_KV_AUTO_UPDATE */
#endif

#endif

#if CONFIG_QM_TS_SUPPORT

/* using TSDB (Time series database) feature */
#define FDB_USING_TSDB

#endif

/* Using FAL storage mode */
#define FDB_USING_FAL_MODE

/* the flash write granularity, unit: bit
 * only support 1(nor flash)/ 8(stm32f2/f4)/ 32(stm32f1) */

#if CONFIG_QM_FLASHDB_EXTERNAL_FLASH

#if CONFIG_QM_FLASHDB_EXTERNAL_FLASH_SPI 
#define FDB_WRITE_GRAN     1  /* @note you must define it for a value */
#endif

#elif CONFIG_QM_FLASHDB_INTERNAL_FLASH

#define FDB_WRITE_GRAN     CONFIG_QM_FLASH_WRITE_GRAN  /* @note you must define it for a value */

#endif

/* Using file storage mode by LIBC file API, like fopen/fread/fwrte/fclose */
/* #define FDB_USING_FILE_LIBC_MODE */

/* Using file storage mode by POSIX file API, like open/read/write/close */
/* #define FDB_USING_FILE_POSIX_MODE */

/* MCU Endian Configuration, default is Little Endian Order. */
/* #define FDB_BIG_ENDIAN */ 

/* log print macro. default EF_PRINT macro is printf() */
/* #define FDB_PRINT(...)              my_printf(__VA_ARGS__) */

/* print debug information */
#define FDB_DEBUG_ENABLE

#endif /* _FDB_CFG_H_ */
