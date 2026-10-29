#include "qm_spi.h"
#include "qm_errno.h"

#include "qm_gpio.h"

#include "esp_system.h"

#include "qm.h"
#include "qm_log.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"


#include "driver/spi_master.h"
#include "driver/spi_slave.h"
#include "driver/gpio.h"

#define LOG_TAG "hal"

typedef struct 
{
    spi_device_handle_t spi;
}qm_spi_info_t;

int32_t qm_spi_init(qm_spi_dev_t *spi)
{
	int ret = QM_EOK;
    qm_spi_info_t *spi_info = NULL;
    spi_bus_config_t buscfg = {0};
    spi_device_interface_config_t devcfg = {0};

    if(spi == NULL){
        return -QM_EINVAL;
    }

    spi_info = (qm_spi_info_t *)qm_malloc(sizeof(qm_spi_info_t));
    if(spi_info == NULL){
        return -QM_ENOMEM;
    }
    memset(spi_info, 0, sizeof(qm_spi_info_t));

    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.sclk_io_num = spi->pins.sck_pin;
    buscfg.miso_io_num = spi->pins.miso_pin;
    buscfg.mosi_io_num = spi->pins.mosi_pin;

    devcfg.queue_size = 100;
    if(spi->pins.cs_pin < 0){
        devcfg.spics_io_num = -1;
    }else{
        devcfg.spics_io_num = spi->pins.cs_pin;
    }

    devcfg.clock_speed_hz = spi->config.freq;
    
    switch (spi->config.clock_mode)
    {
        case QM_SPI_CLK_POL_LOW_PHA_1E:
            devcfg.mode = 0;
        break;
        case QM_SPI_CLK_POL_LOW_PHA_2E:
            devcfg.mode = 1;
        break;
        case QM_SPI_CLK_POL_HIGH_PHA_1E:
            devcfg.mode = 2;
        break;
        case QM_SPI_CLK_POL_HIGH_PHA_2E:
            devcfg.mode = 3;
        break;         
        default:
            ret = -QM_EIO;      
        break;
    }

    if(ret != QM_EOK){
        return ret;
    }	

    switch (spi->config.data_shift)
    {
        case QM_SPI_DATA_SHIFT_MSB:
            
        break;
        case QM_SPI_DATA_SHIFT_LSB:
            devcfg.flags |= SPI_DEVICE_BIT_LSBFIRST;
        break;      
        default:
            ret = -QM_EIO;    
        break;
    }

    if(ret != QM_EOK){
        return ret;
    }	

    ret = spi_bus_initialize((spi_host_device_t)spi->port, 
                            &buscfg, SPI_DMA_CH_AUTO);

    if(ret != QM_EOK){
        goto _error;
    }

       

    switch (spi->config.mode)
    {
        case QM_SPI_MODE_MASTER:
            ret = spi_bus_add_device((spi_host_device_t)spi->port, 
                            &devcfg, &spi_info->spi);

        break;
        case QM_SPI_MODE_SLAVE:

        break;       
        default:
            ret = -QM_EIO;      
        break;
    }

    if(ret != QM_EOK){
        goto _error;
    }

    spi->priv = (void *)spi_info;

    return QM_EOK;

_error:
    
    if(spi_info){
        qm_free(spi_info);
        spi_info = NULL;
    }
    return ret;
}

int32_t qm_spi_write(qm_spi_dev_t *spi, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    esp_err_t ret = ESP_OK;
    spi_transaction_t trans_desc = {0};
    spi_transaction_t *trans_desc_ptr = NULL;
    qm_spi_info_t *spi_info = (qm_spi_info_t *)spi->priv;

    if(spi == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    trans_desc.tx_buffer = data;
    trans_desc.length = 8 * size;

    ret = spi_device_queue_trans(spi_info->spi, &trans_desc, portMAX_DELAY);
    if (ret != ESP_OK) {
        return -QM_EIO;
    }

    ret = spi_device_get_trans_result(spi_info->spi, &trans_desc_ptr, timeout);
    if (ret != ESP_OK) {
        return -QM_EIO;
    }
    return QM_EOK;
}

int32_t qm_spi_read(qm_spi_dev_t *spi, uint8_t *data, uint16_t size, uint32_t timeout)
{
    esp_err_t ret = ESP_OK;
    spi_transaction_t trans_desc = {0};
    spi_transaction_t *trans_desc_ptr = NULL;

    qm_spi_info_t *spi_info = (qm_spi_info_t *)spi->priv;

    if(spi == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    trans_desc.rxlength = 8 * size;
    trans_desc.rx_buffer = data;

    ret = spi_device_queue_trans(spi_info->spi, &trans_desc, portMAX_DELAY);
    if (ret != ESP_OK) {
        return -QM_EIO;
    }

    ret = spi_device_get_trans_result(spi_info->spi, &trans_desc_ptr, timeout);
    if (ret != ESP_OK) {
        return -QM_EIO;
    }

    return QM_EOK;
}

int32_t qm_spi_write_read(qm_spi_dev_t *spi, const uint8_t *tx_data, uint16_t tx_size, uint8_t *rx_data, uint16_t rx_size, uint32_t timeout)
{
    spi_transaction_t transaction = {0};
    qm_spi_info_t *spi_info = (qm_spi_info_t *)spi->priv;

    if(spi == NULL || rx_data == NULL || tx_data == NULL  || rx_size == 0){
        return -QM_EINVAL;
    }

    transaction.rxlength = 8 * rx_size;
    transaction.length = 8 * tx_size;
    transaction.rx_buffer = rx_data;
    transaction.tx_buffer = tx_data;

    spi_device_acquire_bus(spi_info->spi, portMAX_DELAY);
    spi_device_polling_transmit(spi_info->spi, &transaction); 
    spi_device_release_bus(spi_info->spi);

    return QM_EOK;
}