#include "qm_spi.h"
#include "qm_errno.h"

int32_t qm_spi_init(qm_spi_dev_t *spi)
{
    (void)spi;
    return QM_EOK;
}

int32_t qm_spi_write(qm_spi_dev_t *spi, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    (void)spi;
    (void)data;
    (void)timeout;
    return (int32_t)size;
}

int32_t qm_spi_read(qm_spi_dev_t *spi, uint8_t *data, uint16_t size, uint32_t timeout)
{
    (void)spi;
    (void)data;
    (void)timeout;
    return (int32_t)size;
}

int32_t qm_spi_write_read(qm_spi_dev_t *spi, const uint8_t *tx_data, uint16_t tx_size,
                          uint8_t *rx_data, uint16_t rx_size, uint32_t timeout)
{
    (void)spi;
    (void)tx_data;
    (void)tx_size;
    (void)rx_data;
    (void)timeout;
    return (int32_t)rx_size;
}

int32_t qm_spi_callback_reg(qm_spi_dev_t *spi, qm_spi_cb_t cb)
{
    (void)spi;
    (void)cb;
    return QM_EOK;
}

int32_t qm_spi_finalize(qm_spi_dev_t *spi)
{
    (void)spi;
    return QM_EOK;
}
