#include "qm_spi.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

#define LOG_TAG "linux spi"

typedef struct {
    int fd;
    uint8_t mode;
    uint8_t bits;
    uint32_t speed;
} linux_spi_ctx_t;

static uint8_t linux_spi_mode_from_cfg(qm_spi_clk_mode_t clock_mode)
{
    switch (clock_mode) {
    case QM_SPI_CLK_POL_LOW_PHA_1E:  return SPI_MODE_0;
    case QM_SPI_CLK_POL_LOW_PHA_2E:  return SPI_MODE_1;
    case QM_SPI_CLK_POL_HIGH_PHA_1E: return SPI_MODE_2;
    case QM_SPI_CLK_POL_HIGH_PHA_2E: return SPI_MODE_3;
    default:                         return SPI_MODE_0;
    }
}

static uint8_t linux_spi_bits_from_cfg(qm_spi_data_width_t width)
{
    switch (width) {
    case QM_SPI_DATA_WIDTH_4B:  return 4;
    case QM_SPI_DATA_WIDTH_7B:  return 7;
    case QM_SPI_DATA_WIDTH_8B:  return 8;
    case QM_SPI_DATA_WIDTH_9B:  return 9;
    case QM_SPI_DATA_WIDTH_16B: return 16;
    default:                    return 8;
    }
}

static int linux_spi_dev_path(uint8_t port, char *path, size_t path_len)
{
    snprintf(path, path_len, "/dev/spidev0.%u", port);
    return 0;
}

static int32_t linux_spi_transfer(linux_spi_ctx_t *ctx, const uint8_t *tx, uint8_t *rx, uint16_t size)
{
    struct spi_ioc_transfer tr;

    memset(&tr, 0, sizeof(tr));
    tr.tx_buf = (unsigned long)tx;
    tr.rx_buf = (unsigned long)rx;
    tr.len = size;
    tr.speed_hz = ctx->speed;
    tr.bits_per_word = ctx->bits;
    tr.delay_usecs = 0;

    if (ioctl(ctx->fd, SPI_IOC_MESSAGE(1), &tr) < 0) {
        return -QM_EIO;
    }

    return (int32_t)size;
}

int32_t qm_spi_init(qm_spi_dev_t *spi)
{
    linux_spi_ctx_t *ctx;
    char dev_path[32];
    uint8_t mode;
    uint8_t bits;
    uint32_t speed;

    if (spi == NULL) {
        return -QM_EINVAL;
    }

    if (spi->config.mode != QM_SPI_MODE_MASTER) {
        QM_LOGW(LOG_TAG, "only master mode supported");
        return -QM_ENOSYS;
    }

    ctx = (linux_spi_ctx_t *)qm_malloc(sizeof(linux_spi_ctx_t));
    if (ctx == NULL) {
        return -QM_ENOMEM;
    }
    memset(ctx, 0, sizeof(*ctx));
    ctx->fd = -1;

    linux_spi_dev_path(spi->port, dev_path, sizeof(dev_path));
    ctx->fd = open(dev_path, O_RDWR);
    if (ctx->fd < 0) {
        QM_LOGW(LOG_TAG, "open %s failed errno=%d", dev_path, errno);
        qm_free(ctx);
        return -QM_EIO;
    }

    mode = linux_spi_mode_from_cfg(spi->config.clock_mode);
    if (spi->config.data_shift == QM_SPI_DATA_SHIFT_LSB) {
        mode |= SPI_LSB_FIRST;
    }
    bits = linux_spi_bits_from_cfg(spi->config.data_width);
    speed = spi->config.freq ? spi->config.freq : 1000000;

    if (ioctl(ctx->fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(ctx->fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(ctx->fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        close(ctx->fd);
        qm_free(ctx);
        return -QM_EIO;
    }

    ctx->mode = mode;
    ctx->bits = bits;
    ctx->speed = speed;
    spi->priv = ctx;
    QM_LOGI(LOG_TAG, "open %s speed=%u bits=%u", dev_path, speed, bits);
    return QM_EOK;
}

int32_t qm_spi_write(qm_spi_dev_t *spi, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    linux_spi_ctx_t *ctx;

    (void)timeout;

    if (spi == NULL || spi->priv == NULL || data == NULL || size == 0) {
        return -QM_EINVAL;
    }

    ctx = (linux_spi_ctx_t *)spi->priv;
    return linux_spi_transfer(ctx, data, NULL, size);
}

int32_t qm_spi_read(qm_spi_dev_t *spi, uint8_t *data, uint16_t size, uint32_t timeout)
{
    linux_spi_ctx_t *ctx;

    (void)timeout;

    if (spi == NULL || spi->priv == NULL || data == NULL || size == 0) {
        return -QM_EINVAL;
    }

    ctx = (linux_spi_ctx_t *)spi->priv;
    return linux_spi_transfer(ctx, NULL, data, size);
}

int32_t qm_spi_write_read(qm_spi_dev_t *spi, const uint8_t *tx_data, uint16_t tx_size,
                          uint8_t *rx_data, uint16_t rx_size, uint32_t timeout)
{
    linux_spi_ctx_t *ctx;
    uint16_t size;
    uint8_t *tx_buf = NULL;
    uint8_t *rx_buf = NULL;
    int32_t ret;

    (void)timeout;

    if (spi == NULL || spi->priv == NULL || tx_size == 0 || rx_size == 0) {
        return -QM_EINVAL;
    }

    size = (tx_size > rx_size) ? tx_size : rx_size;
    ctx = (linux_spi_ctx_t *)spi->priv;

    tx_buf = (uint8_t *)qm_malloc(size);
    rx_buf = (uint8_t *)qm_malloc(size);
    if (tx_buf == NULL || rx_buf == NULL) {
        qm_free(tx_buf);
        qm_free(rx_buf);
        return -QM_ENOMEM;
    }

    memset(tx_buf, 0, size);
    memset(rx_buf, 0, size);
    if (tx_data != NULL) {
        memcpy(tx_buf, tx_data, tx_size);
    }

    ret = linux_spi_transfer(ctx, tx_buf, rx_buf, size);
    if (ret >= 0 && rx_data != NULL) {
        memcpy(rx_data, rx_buf, rx_size);
    }

    qm_free(tx_buf);
    qm_free(rx_buf);
    return ret;
}

int32_t qm_spi_callback_reg(qm_spi_dev_t *spi, qm_spi_cb_t cb)
{
    (void)spi;
    (void)cb;
    return QM_EOK;
}

int32_t qm_spi_finalize(qm_spi_dev_t *spi)
{
    linux_spi_ctx_t *ctx;

    if (spi == NULL || spi->priv == NULL) {
        return -QM_EINVAL;
    }

    ctx = (linux_spi_ctx_t *)spi->priv;
    if (ctx->fd >= 0) {
        close(ctx->fd);
    }
    qm_free(ctx);
    spi->priv = NULL;
    return QM_EOK;
}
