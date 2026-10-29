#include "qm.h"
#include "qm_spi.h"
#include "qm_log.h"
#include "qm_check.h"

#define LOG_TAG "spi slave"

#define GPIO_MOSI 39
#define GPIO_MISO 40
#define GPIO_SCLK 38
#define GPIO_CS   -1

#define RECV_SIZE 4096

void qm_application_start(void)
{
    int ret = QM_EOK;

    uint8_t data = (uint8_t*)qm_malloc(RECV_SIZE);
    QM_RETURN_ON_FALSE(data == NULL, QM_ENOMEM, LOG_TAG, "malloc fail");

    qm_spi_dev_t spi_dev = {
        .config.clock_mode = QM_SPI_CLK_POL_LOW_PHA_1E,
        .config.data_shift = QM_SPI_DATA_SHIFT_MSB,
        .config.data_width = QM_SPI_DATA_WIDTH_8B,
        .config.mode = QM_SPI_MODE_SLAVE,
        .config.freq = 8 * 1000 * 1000,
        .pins.cs_pin   = GPIO_CS,
        .pins.miso_pin = GPIO_MISO,
        .pins.mosi_pin = GPIO_MOSI,
        .pins.sck_pin  = GPIO_SCLK,   
        .port = 1,  
    };

    ret = qm_spi_init(&spi_dev);

    ret = qm_spi_recv(&spi_dev, data, RECV_SIZE, 5000);
    if(ret == QM_EOK){

        
    }
}