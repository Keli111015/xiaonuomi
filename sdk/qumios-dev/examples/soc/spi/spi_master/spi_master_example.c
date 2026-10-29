#include "qm.h"
#include "qm_spi.h"
#include "qm_log.h"
#include "qm_check.h"
#include "qm_platform.h"

#define LOG_TAG "spi master"

#define GPIO_MOSI QM_GPIO_PIN_1
#define GPIO_MISO QM_GPIO_PIN_0
#define GPIO_SCLK QM_GPIO_PIN_3
#define GPIO_CS   QM_GPIO_PIN_2

void qm_application_start(void)
{
    int ret = QM_EOK;
    uint8_t tx_data[4] = {0x01, 0x02, 0x03, 0x04};

    qm_spi_dev_t spi_dev = {
        .config.clock_mode = QM_SPI_CLK_POL_LOW_PHA_1E,
        .config.data_shift = QM_SPI_DATA_SHIFT_MSB,
        .config.data_width = QM_SPI_DATA_WIDTH_8B,
        .config.mode = QM_SPI_MODE_MASTER,
        .config.freq = 125 * 1000,
        .pins.cs_pin   = GPIO_CS,
        .pins.miso_pin = GPIO_MISO,
        .pins.mosi_pin = GPIO_MOSI,
        .pins.sck_pin  = GPIO_SCLK,   
        .port = 0,  
    };

    ret = qm_spi_init(&spi_dev);
    qm_spi_write(&spi_dev, tx_data, sizeof(tx_data), 0);
    

    

}