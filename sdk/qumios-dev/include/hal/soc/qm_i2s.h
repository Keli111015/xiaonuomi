#ifndef QM_I2S_H
#define QM_I2S_H

#ifdef __cplusplus
extern "C" {
#endif

/** @addtogroup qm_i2s I2S
 *  qm i2s API.
 *
 *  @{
 */

#include "qm_types.h"
#include "qm_kernel.h"

/**
 * @brief I2S work mode
 */
typedef enum {
    QM_I2S_MODE_MASTER = 0,   /*!< I2S controller drives clocks */
    QM_I2S_MODE_SLAVE,        /*!< I2S controller follows external clocks */
} qm_i2s_mode_t;

/**
 * @brief I2S transfer direction
 */
typedef enum {
    QM_I2S_DIR_RX = 1 << 0,                   /*!< Receive only */
    QM_I2S_DIR_TX = 1 << 1,                   /*!< Transmit only */
    QM_I2S_DIR_RX_TX = QM_I2S_DIR_RX | QM_I2S_DIR_TX, /*!< Full duplex */
} qm_i2s_dir_t;

/**
 * @brief I2S serial data standard
 */
typedef enum {
    QM_I2S_STD_PHILIPS = 0,   /*!< Philips I2S standard */
    QM_I2S_STD_LEFT_JUSTIFIED,/*!< Left justified format */
    QM_I2S_STD_RIGHT_JUSTIFIED,/*!< Right justified format */
    QM_I2S_STD_PCM,           /*!< PCM short frame format */
} qm_i2s_standard_t;

/**
 * @brief I2S sample bit width
 */
typedef enum {
    QM_I2S_BITS_16 = 16,      /*!< 16-bit samples */
    QM_I2S_BITS_24 = 24,      /*!< 24-bit samples */
    QM_I2S_BITS_32 = 32,      /*!< 32-bit samples */
} qm_i2s_bits_t;

/**
 * @brief I2S channel mode
 */
typedef enum {
    QM_I2S_CHANNEL_MONO = 1,  /*!< Mono */
    QM_I2S_CHANNEL_STEREO = 2,/*!< Stereo */
} qm_i2s_channels_t;

/**
 * @brief I2S active slot selection
 */
typedef enum {
    QM_I2S_SLOT_MASK_DEFAULT = 0, /*!< Use the standard default for the selected channel mode */
    QM_I2S_SLOT_MASK_LEFT,        /*!< Left slot only */
    QM_I2S_SLOT_MASK_RIGHT,       /*!< Right slot only */
    QM_I2S_SLOT_MASK_BOTH,        /*!< Both slots */
} qm_i2s_slot_mask_t;

/**
 * @brief I2S signal pins
 */
typedef struct {
    int8_t mclk_pin;    /*!< Master clock pin, set to negative value if unused */
    int8_t bclk_pin;    /*!< Bit clock pin */
    int8_t ws_pin;      /*!< Word select / LRCK pin */
    int8_t din_pin;     /*!< Serial data input pin, set to negative value if unused */
    int8_t dout_pin;    /*!< Serial data output pin, set to negative value if unused */
    qm_bool mclk_inv;   /*!< Invert master clock when supported */
    qm_bool bclk_inv;   /*!< Invert bit clock */
    qm_bool ws_inv;     /*!< Invert word select / LRCK */
} qm_i2s_pins_t;

/**
 * @brief I2S standard slot override
 */
typedef struct {
    qm_bool custom;                  /*!< Set to true to apply the fields below */
    qm_i2s_bits_t data_bits;         /*!< Data bit width, 0 keeps the standard default */
    qm_i2s_bits_t slot_bits;         /*!< Slot bit width, 0 keeps the standard default */
    qm_i2s_slot_mask_t slot_mask;    /*!< Active slot selection */
    uint16_t ws_width;               /*!< Word select width in BCLK cycles, 0 keeps the standard default */
    qm_bool ws_pol;                  /*!< Word select polarity */
    qm_bool bit_shift;               /*!< Enable I2S bit shift */
    qm_bool left_align;              /*!< Left-align sample bits inside the slot */
    qm_bool big_endian;              /*!< Big-endian sample order */
    qm_bool lsb_first;               /*!< Send or receive least significant bit first */
} qm_i2s_std_slot_config_t;

/**
 * @brief I2S configuration
 */
typedef struct {
    qm_i2s_mode_t mode;               /*!< Master or slave */
    qm_i2s_dir_t dir;                 /*!< RX, TX or RX/TX */
    qm_i2s_standard_t standard;       /*!< Serial data standard */
    uint32_t sample_rate;             /*!< Sample rate in Hz */
    qm_i2s_bits_t bits_per_sample;    /*!< Valid bits per sample */
    qm_i2s_channels_t channels;       /*!< Mono or stereo */
    qm_i2s_pins_t pins;               /*!< GPIO assignment */
    qm_i2s_std_slot_config_t slot_cfg;/*!< Optional standard slot override */
    uint16_t dma_desc_num;            /*!< DMA descriptor count */
    uint16_t dma_frame_num;           /*!< Frames per DMA buffer */
} qm_i2s_config_t;

/**
 * @brief I2S device handle
 */
typedef struct {
    uint8_t port;             /*!< I2S controller port */
    qm_i2s_config_t config;   /*!< I2S configuration */
    void *priv;               /*!< Platform private data */
} qm_i2s_dev_t;

/**
 * @brief I2S control commands
 */
typedef enum {
    QM_I2S_CTRL_CMD_START = 0,        /*!< Start configured directions */
    QM_I2S_CTRL_CMD_STOP,             /*!< Stop configured directions */
    QM_I2S_CTRL_CMD_RX_START,         /*!< Start RX only */
    QM_I2S_CTRL_CMD_RX_STOP,          /*!< Stop RX only */
    QM_I2S_CTRL_CMD_TX_START,         /*!< Start TX only */
    QM_I2S_CTRL_CMD_TX_STOP,          /*!< Stop TX only */
    QM_I2S_CTRL_CMD_RX_CLEAR,         /*!< Reset RX data path */
    QM_I2S_CTRL_CMD_TX_CLEAR,         /*!< Reset TX data path */
    QM_I2S_CTRL_CMD_SET_SAMPLE_RATE,  /*!< Update sample rate configuration */
} qm_i2s_ctrl_cmd_t;

/**
 * @brief Initialize an I2S device
 *
 * @param[in] i2s  I2S device handle
 *
 * @return 0 on success, otherwise negative error code
 */
int32_t qm_i2s_init(qm_i2s_dev_t *i2s);

/**
 * @brief Control an I2S device
 *
 * @param[in] i2s    I2S device handle
 * @param[in] cmd    Control command
 * @param[in] param  Optional command parameter
 *
 * @return 0 on success, otherwise negative error code
 */
int32_t qm_i2s_ctrl(qm_i2s_dev_t *i2s, qm_i2s_ctrl_cmd_t cmd, void *param);

/**
 * @brief Read PCM data from an I2S device
 *
 * @param[in]  i2s          I2S device handle
 * @param[out] data         Receive buffer
 * @param[in]  expect_size  Expected byte count
 * @param[in]  timeout      Timeout in milliseconds, set to QM_WAIT_FOREVER to wait forever
 *
 * @return Number of bytes read on success, otherwise negative error code
 */
int32_t qm_i2s_read(qm_i2s_dev_t *i2s, void *data, uint32_t expect_size, uint32_t timeout);

/**
 * @brief Write PCM data to an I2S device
 *
 * @param[in] i2s      I2S device handle
 * @param[in] data     Transmit buffer
 * @param[in] size     Byte count to send
 * @param[in] timeout  Timeout in milliseconds, set to QM_WAIT_FOREVER to wait forever
 *
 * @return Number of bytes written on success, otherwise negative error code
 */
int32_t qm_i2s_write(qm_i2s_dev_t *i2s, const void *data, uint32_t size, uint32_t timeout);

/**
 * @brief Deinitialize an I2S device
 *
 * @param[in] i2s  I2S device handle
 *
 * @return 0 on success, otherwise negative error code
 */
int32_t qm_i2s_deinit(qm_i2s_dev_t *i2s);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* QM_I2S_H */
