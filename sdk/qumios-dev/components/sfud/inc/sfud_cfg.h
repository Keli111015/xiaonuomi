/*
 * This file is part of the Serial Flash Universal Driver Library.
 *
 * Copyright (c) 2016-2018, Armink, <armink.ztl@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * 'Software'), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED 'AS IS', WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Function: It is the configure head file for this library.
 * Created on: 2016-04-23
 */

#ifndef _SFUD_CFG_H_
#define _SFUD_CFG_H_

#include "qm.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONFIG_SFUD_DEBUG_ON
#define CONFIG_SFUD_DEBUG_ON    (0)
#endif

#ifndef CONFIG_SFUD_USING_SFDP
#define CONFIG_SFUD_USING_SFDP    (0)
#endif

#ifndef CONFIG_SFUD_USING_FLASH_INFO_TABLE
#define CONFIG_SFUD_USING_FLASH_INFO_TABLE    (0)
#endif

#ifndef CONFIG_SFUD_USING_QSPI
#define CONFIG_SFUD_USING_QSPI          (0)
#endif

#ifndef CONFIG_SFUD_SPI_CLOCK_SPEED
#define CONFIG_SFUD_SPI_CLOCK_SPEED     (8 * 1000 * 1000)
#endif

#ifndef CONFIG_SFUD_SPI_PORT
#define CONFIG_SFUD_SPI_PORT            (1)
#endif

#ifndef CONFIG_SFUD_SPI_MISO_PIN
#define CONFIG_SFUD_SPI_MISO_PIN        (0)
#endif

#ifndef CONFIG_SFUD_SPI_MOSI_PIN
#define CONFIG_SFUD_SPI_MOSI_PIN        (0)
#endif

#ifndef CONFIG_SFUD_SPI_SCLK_PIN
#define CONFIG_SFUD_SPI_SCLK_PIN        (0)
#endif

#ifndef CONFIG_SFUD_SPI_CS_PIN
#define CONFIG_SFUD_SPI_CS_PIN          (0)
#endif

#ifndef CONFIG_SFUD_RETRY_DELAY_US
#define CONFIG_SFUD_RETRY_DELAY_US      (100)
#endif

#ifndef CONFIG_SFUD_ERROR_RETRY_TIME
#define CONFIG_SFUD_ERROR_RETRY_TIME    (300)
#endif


#ifndef CONFIG_SFUD_DEVICE_NAME
#define CONFIG_SFUD_DEVICE_NAME     "XJ_SPI_FLASH"
#endif

#ifndef CONFIG_SFUD_SPI_NAME
#define CONFIG_SFUD_SPI_NAME        "EXT_SPI1"
#endif

#if CONFIG_SFUD_DEBUG_ON
#define SFUD_DEBUG_MODE
#endif

#if CONFIG_SFUD_USING_SFDP
#define SFUD_USING_SFDP
#endif

#if CONFIG_SFUD_USING_FLASH_INFO_TABLE
#define SFUD_USING_FLASH_INFO_TABLE
#endif

#if CONFIG_SFUD_USING_QSPI
#define SFUD_USING_QSPI
#endif

enum {
    SFUD_XXXX_DEVICE_INDEX = 0,
};

#define SFUD_FLASH_DEVICE_TABLE                                                                                 \
{                                                                                                               \
    [SFUD_XXXX_DEVICE_INDEX] = {.name = CONFIG_SFUD_DEVICE_NAME, .spi.name = CONFIG_SFUD_SPI_NAME},             \
}

#ifdef __cplusplus
}
#endif

#endif /* _SFUD_CFG_H_ */
