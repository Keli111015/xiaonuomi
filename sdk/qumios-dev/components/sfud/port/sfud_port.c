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
 * Function: Portable interface for each platform.
 * Created on: 2016-04-23
 */

#include "qm.h"
#include "qm_log.h"
#include "qm_spi.h"
#include "qm_gpio.h"

#include "sfud.h"
#include "sfud_cfg.h"

#define CONFIG_SUFD_LOG_BUFF  (256)

#define LOG_TAG "SFUD"

static char log_buf[CONFIG_SUFD_LOG_BUFF] = {0};

void sfud_log_debug(const char *file, const long line, const char *format, ...);

typedef struct 
{
    qm_gpio_dev_t gpio;
    qm_spi_dev_t  spi;
    qm_mutex_t mutex;
}sfud_port_ctx_t;

static inline void spi_lock(const sfud_spi *spi) 
{
    #ifdef CONFIG_QM_OS_MUTEX_SUPPORT

    sfud_port_ctx_t *port_ctx = (sfud_port_ctx_t *) spi->user_data;
    qm_mutex_lock(&port_ctx->mutex, QM_WAIT_FOREVER);

    #endif
}

static inline void spi_unlock(const sfud_spi *spi) 
{
    #ifdef CONFIG_QM_OS_MUTEX_SUPPORT
    sfud_port_ctx_t *port_ctx = (sfud_port_ctx_t *) spi->user_data;
    qm_mutex_unlock(&port_ctx->mutex);
    #endif
}

static void inline retry_delay_us(void) 
{
    qm_usleep(CONFIG_SFUD_RETRY_DELAY_US);
}

/**
 * SPI write data then read data
 */
static sfud_err spi_write_read(const sfud_spi *spi, const uint8_t *write_buf, size_t write_size, uint8_t *read_buf,
        size_t read_size) {
    sfud_err result = SFUD_SUCCESS;
    
    sfud_port_ctx_t *port_ctx = (sfud_port_ctx_t *) spi->user_data;

    if (write_size) {
        SFUD_ASSERT(write_buf);
    }

    if (read_size) {
        SFUD_ASSERT(read_buf);
    }

    qm_gpio_set_level(&port_ctx->gpio, QM_FALSE);
    
    if(write_size){
        qm_spi_write(&port_ctx->spi, write_buf, write_size, 1000);
    }

    if(read_size){
        qm_spi_read(&port_ctx->spi, read_buf, read_size, 1000);
    }

    qm_gpio_set_level(&port_ctx->gpio, QM_TRUE);

    return result;
}

#ifdef SFUD_USING_QSPI
/**
 * read flash data by QSPI
 */
static sfud_err qspi_read(const struct __sfud_spi *spi, uint32_t addr, sfud_qspi_read_cmd_format *qspi_read_cmd_format,
        uint8_t *read_buf, size_t read_size) {
    sfud_err result = SFUD_SUCCESS;

    return result;
}
#endif /* SFUD_USING_QSPI */

static sfud_err sfud_spi_cs_pin_init(qm_gpio_dev_t *gpio)
{
    sfud_err result = SFUD_SUCCESS;
    if(gpio == NULL){
    return SFUD_ERR_NOT_FOUND;
    }    

    gpio->config.mode = QM_GPIO_MODE_OUTPUT;
    gpio->config.pull_en = QM_GPIO_PULLUP_ONLY;
    gpio->port = CONFIG_SFUD_SPI_CS_PIN;

    result = qm_gpio_init(gpio);
    if(result != SFUD_SUCCESS){
    return result;
    }

    return qm_gpio_set_level(gpio, QM_TRUE);
}

static sfud_err sfud_spi_dev_init(qm_spi_dev_t *spi)
{
    if(spi == NULL){
        return SFUD_ERR_NOT_FOUND;
    }

    spi->port = CONFIG_SFUD_SPI_PORT;
    spi->config.mode = QM_SPI_MODE_MASTER;
    spi->config.data_shift = QM_SPI_DATA_SHIFT_MSB;
    spi->config.data_width = QM_SPI_DATA_WIDTH_8B;
    spi->config.clock_mode = QM_SPI_CLK_POL_LOW_PHA_1E;
    spi->config.freq = CONFIG_SFUD_SPI_CLOCK_SPEED;
    spi->pins.miso_pin = CONFIG_SFUD_SPI_MISO_PIN;
    spi->pins.mosi_pin = CONFIG_SFUD_SPI_MOSI_PIN;
    spi->pins.sck_pin  = CONFIG_SFUD_SPI_SCLK_PIN;   

    return qm_spi_init(spi);    
}

sfud_err sfud_spi_port_init(sfud_flash *flash) 
{
    sfud_err result = SFUD_SUCCESS;
    sfud_port_ctx_t *port_ctx = NULL;

    if(flash == NULL){
        return SFUD_ERR_NOT_FOUND;
    }

    port_ctx = (sfud_port_ctx_t *)qm_malloc(sizeof(sfud_port_ctx_t));
    if(port_ctx == NULL){
        return SFUD_ERR_NOT_FOUND;
    }
    memset(port_ctx, 0, sizeof(sfud_port_ctx_t));

    result = sfud_spi_dev_init(&port_ctx->spi);
    if(result != SFUD_SUCCESS){
        QM_LOGI("tag", "result = %d", result);
        return SFUD_ERR_NOT_FOUND;;
    }

    result = sfud_spi_cs_pin_init(&port_ctx->gpio);
    if(result != SFUD_SUCCESS){
        return SFUD_ERR_NOT_FOUND;
    }

//    result = qm_mutex_new(&port_ctx->mutex);
//    if(result != SFUD_SUCCESS){
//        return SFUD_ERR_NOT_FOUND;
//    }

    flash->spi.wr = spi_write_read;
    flash->spi.lock = spi_lock;
    flash->spi.unlock = spi_unlock;
    flash->spi.user_data = port_ctx;
    flash->retry.delay = retry_delay_us;
    /* about 300 seconds timeout */
    /* a large delay for benchmark */
    flash->retry.times = CONFIG_SFUD_ERROR_RETRY_TIME * 10000;

    return result;
}

/**
 * This function is print debug info.
 *
 * @param file the file which has call this function
 * @param line the line number which has call this function
 * @param format output format
 * @param ... args
 */
void sfud_log_debug(const char *file, const long line, const char *format, ...) 
{
    va_list args;
    va_start(args, format);
    vsnprintf(log_buf, sizeof(log_buf), format, args);
    QM_LOGD(LOG_TAG, "(%s:%ld)%s", file, line, log_buf);
    va_end(args);
}

/**
 * This function is print routine info.
 *
 * @param format output format
 * @param ... args
 */
void sfud_log_info(const char *format, ...) 
{
    va_list args;
    va_start(args, format);
    vsnprintf(log_buf, sizeof(log_buf), format, args);
    QM_LOGI(LOG_TAG, "%s", log_buf);
    va_end(args);
}
