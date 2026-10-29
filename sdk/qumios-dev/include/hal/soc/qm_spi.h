#ifndef QM_SPI_H
#define QM_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_spi SPI
 *  qm spi API.
 *
 *  @{
 */

#include "qm.h"

/* SPI interrupt events */
typedef enum {
    QM_SPI_EVENT_CRC,       /* crc check error */
    QM_SPI_EVENT_FE,        /* Frame error */
    QM_SPI_EVENT_OV,        /* Overrun error */
    QM_SPI_EVENT_RC,        /* Receive complete */
    QM_SPI_EVENT_TC,        /* Transmit complete */
    QM_SPI_EVENT_TXE,       /* Transmit data register empty */
    QM_SPI_EVENT_DMA_TH,    /* DMA transmit half */
    QM_SPI_EVENT_DMA_TC,    /* DMA transmit complete */
    QM_SPI_EVENT_DMA_TE,    /* DMA transmit error */
    QM_SPI_EVENT_DMA_RH,    /* DMA receive half */
    QM_SPI_EVENT_DMA_RC,    /* DMA receive complete */
    QM_SPI_EVENT_DMA_RE     /* DMA receive error */
} qm_spi_event_t;

/* SPI interrupt callback */
typedef void (*qm_spi_cb_t)(qm_spi_event_t event, void *data, uint32_t size);

/* spi mode */
typedef enum{
    QM_SPI_MODE_MASTER = 1, /* spi communication is master mode */
    QM_SPI_MODE_SLAVE = 2,  /* spi communication is slave mode */
}qm_spi_mode_t;

/* data size */
typedef enum{
    QM_SPI_DATA_WIDTH_4B  = 4,   /* spi data width is 4 bits */
    QM_SPI_DATA_WIDTH_7B  = 7,   /* spi data width is 7 bits */
    QM_SPI_DATA_WIDTH_8B  = 8,   /* spi data width is 8 bits */
    QM_SPI_DATA_WIDTH_9B  = 9,   /* spi data width is 9 bits */
    QM_SPI_DATA_WIDTH_16B = 16   /* spi data width is 16 bits */
}qm_spi_data_width_t;

/* data shift mode */
typedef enum {
    QM_SPI_DATA_SHIFT_MSB = 0, /* spi data shift msb */
    QM_SPI_DATA_SHIFT_LSB = 1  /* spi data shift lsb */
}qm_spi_data_shift_t;

/* clock mode */
typedef enum {
    QM_SPI_CLK_POL_LOW_PHA_1E   = 0, /* spi clock polarity low, phase 1 edge */
    QM_SPI_CLK_POL_LOW_PHA_2E   = 1, /* spi clock polarity low, phase 2 edge */
    QM_SPI_CLK_POL_HIGH_PHA_1E  = 2, /* spi clock polarity high, phase 1 edge */
    QM_SPI_CLK_POL_HIGH_PHA_2E  = 3  /* spi clock polarity high, phase 2 edge */
}qm_spi_clk_mode_t;

/**
 * @brief spi pins
 */
typedef struct {
    int8_t mosi_pin;
    int8_t miso_pin;
    int8_t cs_pin;
    int8_t sck_pin;
} qm_spi_pins_t;

/* Define spi config args */
typedef struct {
    qm_spi_mode_t mode;           /* spi communication mode */
    qm_spi_data_width_t data_width;     /* spi data width */
    qm_spi_data_shift_t data_shift;     /* spi data shift mode */
    qm_spi_clk_mode_t clock_mode;     /* spi clock mode */
    uint32_t freq;          /* communication frequency Hz */
} qm_spi_config_t;

/* Define spi dev handle */
typedef struct {
    uint8_t             port;   /**< spi port */
    qm_spi_config_t     config; /**< spi config */
    qm_spi_pins_t       pins;    /**< spi pins */
    void               *priv;   /**< priv data */
} qm_spi_dev_t;

/**
 * Initialises the SPI interface for a given SPI device
 *
 * @param[in]  spi  the spi device
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_spi_init(qm_spi_dev_t *spi);

/**
 * Spi send
 *
 * @param[in]  spi      the spi device
 * @param[in]  data     spi send data
 * @param[in]  size     spi send data size
 * @param[in]  timeout  timeout in milisecond, set this value to QM_WAIT_FOREVER
 *                      if you want to wait forever
 *
 * @return
 *     - (-1) Parameter error
 *     - OTHERS (>=0) The number of bytes pushed to the TX FIFO
 */
int32_t qm_spi_write(qm_spi_dev_t *spi, const uint8_t *data, uint16_t size, uint32_t timeout);

/**
 * qm_spi_recv
 *
 * @param[in]   spi      the spi device
 * @param[out]  data     spi recv data
 * @param[in]   size     spi recv data size
 * @param[in]   timeout  timeout in milisecond, set this value to QM_WAIT_FOREVER
 *                       if you want to wait forever
 *
 * @return   
 *     - (-1) Error
 *     - OTHERS (>=0) The number of bytes read from SPI FIFO
 */
int32_t qm_spi_read(qm_spi_dev_t *spi, uint8_t *data, uint16_t size, uint32_t timeout);

/**
 * spi send data and recv
 *
 * @param[in]  spi      the spi device
 * @param[in]  tx_data  spi send data
 * @param[in]  tx_size  spi send data size
 * @param[out] rx_data  spi recv data
 * @param[in]  rx_size  spi recv data size
 * @param[in]  timeout  timeout in milisecond, set this value to QM_WAIT_FOREVER
 *                      if you want to wait forever
 *
 * @return   
 *     - (-1) Error
 *     - OTHERS (>=0) The number of bytes read from SPI FIFO
 */
int32_t qm_spi_write_read(qm_spi_dev_t *spi, const uint8_t *tx_data, uint16_t tx_size, uint8_t *rx_data, uint16_t rx_size, uint32_t timeout);
/**
 *
 * @param [in]   spi          the SPI interface
 * @param [in]   cb           Non-zero pointer is the transceive callback handler;
 *                            NULL pointer for transceive unregister operation
 *                            spi in cb must be the same pointer with spi pointer passed to hal_qm_spi_send_recv_cb_reg
 *                            driver must notify upper layer by calling cb if data transceive done or error in SPI's hw
 * @return 0: on success, negative no.: if an error occured with any step
 */
int32_t qm_spi_callback_reg(qm_spi_dev_t *spi, qm_spi_cb_t cb);

/**
 * De-initialises a SPI interface
 *
 *
 * @param[in]  spi  the SPI device to be de-initialised
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_spi_finalize(qm_spi_dev_t *spi);

/** @} */
#ifdef __cplusplus
}
#endif

#endif /* QM_SPI_H */
