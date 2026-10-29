#include <string.h>

#include "qm_i2s.h"
#include "qm_errno.h"

#include "driver/i2s_std.h"
#include "freertos/FreeRTOS.h"
#include "hal/i2s_types.h"

typedef struct {
    i2s_chan_handle_t rx_handle;
    i2s_chan_handle_t tx_handle;
    uint8_t started;
} qm_i2s_priv_t;

static bool qm_i2s_dir_has_rx(qm_i2s_dir_t dir)
{
    return (dir & QM_I2S_DIR_RX) != 0;
}

static bool qm_i2s_dir_has_tx(qm_i2s_dir_t dir)
{
    return (dir & QM_I2S_DIR_TX) != 0;
}

static i2s_role_t qm_i2s_to_idf_role(qm_i2s_mode_t mode)
{
    return (mode == QM_I2S_MODE_SLAVE) ? I2S_ROLE_SLAVE : I2S_ROLE_MASTER;
}

static i2s_data_bit_width_t qm_i2s_to_idf_bits(qm_i2s_bits_t bits)
{
    switch (bits) {
        case QM_I2S_BITS_16:
            return I2S_DATA_BIT_WIDTH_16BIT;
        case QM_I2S_BITS_24:
            return I2S_DATA_BIT_WIDTH_24BIT;
        case QM_I2S_BITS_32:
            return I2S_DATA_BIT_WIDTH_32BIT;
        default:
            return I2S_DATA_BIT_WIDTH_16BIT;
    }
}

static i2s_slot_mode_t qm_i2s_to_idf_slot_mode(qm_i2s_channels_t channels)
{
    return (channels == QM_I2S_CHANNEL_MONO) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
}

static i2s_std_slot_mask_t qm_i2s_to_idf_slot_mask(qm_i2s_channels_t channels)
{
    return (channels == QM_I2S_CHANNEL_MONO) ? I2S_STD_SLOT_LEFT : I2S_STD_SLOT_BOTH;
}

static i2s_std_slot_mask_t qm_i2s_to_idf_slot_mask_override(qm_i2s_slot_mask_t mask,
                                                             qm_i2s_channels_t channels)
{
    switch (mask) {
        case QM_I2S_SLOT_MASK_LEFT:
            return I2S_STD_SLOT_LEFT;
        case QM_I2S_SLOT_MASK_RIGHT:
            return I2S_STD_SLOT_RIGHT;
        case QM_I2S_SLOT_MASK_BOTH:
            return I2S_STD_SLOT_BOTH;
        case QM_I2S_SLOT_MASK_DEFAULT:
        default:
            return qm_i2s_to_idf_slot_mask(channels);
    }
}

static i2s_slot_bit_width_t qm_i2s_to_idf_slot_bits(qm_i2s_bits_t bits)
{
    switch (bits) {
        case QM_I2S_BITS_16:
            return I2S_SLOT_BIT_WIDTH_16BIT;
        case QM_I2S_BITS_24:
            return I2S_SLOT_BIT_WIDTH_24BIT;
        case QM_I2S_BITS_32:
            return I2S_SLOT_BIT_WIDTH_32BIT;
        default:
            return I2S_SLOT_BIT_WIDTH_AUTO;
    }
}

static void qm_i2s_apply_slot_override(i2s_std_config_t *std_cfg, const qm_i2s_config_t *cfg)
{
    const qm_i2s_std_slot_config_t *slot_cfg;

    if (std_cfg == NULL || cfg == NULL) {
        return;
    }

    slot_cfg = &cfg->slot_cfg;
    std_cfg->slot_cfg.slot_mask = qm_i2s_to_idf_slot_mask_override(slot_cfg->slot_mask, cfg->channels);
    if (!slot_cfg->custom) {
        return;
    }

    if (slot_cfg->data_bits != 0) {
        std_cfg->slot_cfg.data_bit_width = qm_i2s_to_idf_bits(slot_cfg->data_bits);
    }
    if (slot_cfg->slot_bits != 0) {
        std_cfg->slot_cfg.slot_bit_width = qm_i2s_to_idf_slot_bits(slot_cfg->slot_bits);
    }
    if (slot_cfg->ws_width != 0U) {
        std_cfg->slot_cfg.ws_width = slot_cfg->ws_width;
    }

    std_cfg->slot_cfg.ws_pol = slot_cfg->ws_pol;
    std_cfg->slot_cfg.bit_shift = slot_cfg->bit_shift;
    std_cfg->slot_cfg.left_align = slot_cfg->left_align;
    std_cfg->slot_cfg.big_endian = slot_cfg->big_endian;
    std_cfg->slot_cfg.bit_order_lsb = slot_cfg->lsb_first;
}

static int32_t qm_i2s_create_channels(qm_i2s_dev_t *i2s, qm_i2s_priv_t *priv)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG((i2s_port_t)i2s->port, qm_i2s_to_idf_role(i2s->config.mode));

    /* Reuse the public config directly so upper and lower layers stay aligned on DMA sizing. */
    chan_cfg.auto_clear = true;
    chan_cfg.dma_desc_num = i2s->config.dma_desc_num;
    chan_cfg.dma_frame_num = i2s->config.dma_frame_num;

    if (qm_i2s_dir_has_rx(i2s->config.dir) && qm_i2s_dir_has_tx(i2s->config.dir)) {
        return i2s_new_channel(&chan_cfg, &priv->tx_handle, &priv->rx_handle);
    }
    if (qm_i2s_dir_has_tx(i2s->config.dir)) {
        return i2s_new_channel(&chan_cfg, &priv->tx_handle, NULL);
    }
    if (qm_i2s_dir_has_rx(i2s->config.dir)) {
        return i2s_new_channel(&chan_cfg, NULL, &priv->rx_handle);
    }
    return -QM_EINVAL;
}

static int32_t qm_i2s_init_std_mode(qm_i2s_dev_t *i2s, qm_i2s_priv_t *priv)
{
    i2s_std_config_t std_cfg = {0};

    std_cfg.clk_cfg = (i2s_std_clk_config_t)I2S_STD_CLK_DEFAULT_CONFIG(i2s->config.sample_rate);
    /* Keep the qm standard enum as a thin mapping over ESP-IDF standard slot presets. */
    switch (i2s->config.standard) {
        case QM_I2S_STD_PHILIPS:
            std_cfg.slot_cfg = (i2s_std_slot_config_t)I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(qm_i2s_to_idf_bits(i2s->config.bits_per_sample),
                                                                                         qm_i2s_to_idf_slot_mode(i2s->config.channels));
            break;
        case QM_I2S_STD_PCM:
            std_cfg.slot_cfg = (i2s_std_slot_config_t)I2S_STD_PCM_SLOT_DEFAULT_CONFIG(qm_i2s_to_idf_bits(i2s->config.bits_per_sample),
                                                                                     qm_i2s_to_idf_slot_mode(i2s->config.channels));
            break;
        case QM_I2S_STD_LEFT_JUSTIFIED:
        case QM_I2S_STD_RIGHT_JUSTIFIED:
        default:
            std_cfg.slot_cfg = (i2s_std_slot_config_t)I2S_STD_MSB_SLOT_DEFAULT_CONFIG(qm_i2s_to_idf_bits(i2s->config.bits_per_sample),
                                                                                     qm_i2s_to_idf_slot_mode(i2s->config.channels));
            break;
    }

    /* GPIO routing stays fully board-driven through qm_i2s_config_t. */
    std_cfg.gpio_cfg.mclk = i2s->config.pins.mclk_pin;
    std_cfg.gpio_cfg.bclk = i2s->config.pins.bclk_pin;
    std_cfg.gpio_cfg.ws = i2s->config.pins.ws_pin;
    std_cfg.gpio_cfg.din = i2s->config.pins.din_pin;
    std_cfg.gpio_cfg.dout = i2s->config.pins.dout_pin;
    std_cfg.gpio_cfg.invert_flags.mclk_inv = i2s->config.pins.mclk_inv;
    std_cfg.gpio_cfg.invert_flags.bclk_inv = i2s->config.pins.bclk_inv;
    std_cfg.gpio_cfg.invert_flags.ws_inv = i2s->config.pins.ws_inv;
    qm_i2s_apply_slot_override(&std_cfg, &i2s->config);

    if (qm_i2s_dir_has_tx(i2s->config.dir) && priv->tx_handle != NULL) {
        if (i2s_channel_init_std_mode(priv->tx_handle, &std_cfg) != ESP_OK) {
            return -QM_EIO;
        }
    }

    if (qm_i2s_dir_has_rx(i2s->config.dir) && priv->rx_handle != NULL) {
        if (i2s_channel_init_std_mode(priv->rx_handle, &std_cfg) != ESP_OK) {
            return -QM_EIO;
        }
    }

    return QM_EOK;
}

static int32_t qm_i2s_start_dir(qm_i2s_priv_t *priv, qm_i2s_dir_t dir)
{
    /* RX and TX can be started independently so simplex and duplex use the same path. */
    if ((dir & QM_I2S_DIR_TX) && priv->tx_handle != NULL) {
        if (i2s_channel_enable(priv->tx_handle) != ESP_OK) {
            return -QM_EIO;
        }
    }
    if ((dir & QM_I2S_DIR_RX) && priv->rx_handle != NULL) {
        if (i2s_channel_enable(priv->rx_handle) != ESP_OK) {
            return -QM_EIO;
        }
    }
    priv->started = 1;
    return QM_EOK;
}

static int32_t qm_i2s_stop_dir(qm_i2s_priv_t *priv, qm_i2s_dir_t dir)
{
    if ((dir & QM_I2S_DIR_TX) && priv->tx_handle != NULL) {
        if (i2s_channel_disable(priv->tx_handle) != ESP_OK) {
            return -QM_EIO;
        }
    }
    if ((dir & QM_I2S_DIR_RX) && priv->rx_handle != NULL) {
        if (i2s_channel_disable(priv->rx_handle) != ESP_OK) {
            return -QM_EIO;
        }
    }
    if ((dir & QM_I2S_DIR_RX_TX) == QM_I2S_DIR_RX_TX || dir == QM_I2S_DIR_RX || dir == QM_I2S_DIR_TX) {
        priv->started = 0;
    }
    return QM_EOK;
}

int32_t qm_i2s_init(qm_i2s_dev_t *i2s)
{
    qm_i2s_priv_t *priv = NULL;

    if (i2s == NULL) {
        return -QM_EINVAL;
    }

    if (i2s->config.sample_rate == 0 || i2s->config.dir == 0) {
        return -QM_EINVAL;
    }

    priv = (qm_i2s_priv_t *)qm_malloc(sizeof(qm_i2s_priv_t));
    if (priv == NULL) {
        return -QM_ENOMEM;
    }
    memset(priv, 0, sizeof(qm_i2s_priv_t));

    /* Allocate channel handles first, then bind them to the selected standard mode. */
    if (qm_i2s_create_channels(i2s, priv) != ESP_OK) {
        qm_free(priv);
        return -QM_EIO;
    }

    if (qm_i2s_init_std_mode(i2s, priv) != QM_EOK) {
        if (priv->tx_handle != NULL) {
            i2s_del_channel(priv->tx_handle);
        }
        if (priv->rx_handle != NULL) {
            i2s_del_channel(priv->rx_handle);
        }
        qm_free(priv);
        return -QM_EIO;
    }

    i2s->priv = priv;
    return QM_EOK;
}

int32_t qm_i2s_ctrl(qm_i2s_dev_t *i2s, qm_i2s_ctrl_cmd_t cmd, void *param)
{
    qm_i2s_priv_t *priv = NULL;

    if (i2s == NULL || i2s->priv == NULL) {
        return -QM_EINVAL;
    }

    priv = (qm_i2s_priv_t *)i2s->priv;

    switch (cmd) {
        case QM_I2S_CTRL_CMD_START:
            return qm_i2s_start_dir(priv, i2s->config.dir);
        case QM_I2S_CTRL_CMD_STOP:
            return qm_i2s_stop_dir(priv, i2s->config.dir);
        case QM_I2S_CTRL_CMD_RX_START:
            return qm_i2s_start_dir(priv, QM_I2S_DIR_RX);
        case QM_I2S_CTRL_CMD_RX_STOP:
            return qm_i2s_stop_dir(priv, QM_I2S_DIR_RX);
        case QM_I2S_CTRL_CMD_TX_START:
            return qm_i2s_start_dir(priv, QM_I2S_DIR_TX);
        case QM_I2S_CTRL_CMD_TX_STOP:
            return qm_i2s_stop_dir(priv, QM_I2S_DIR_TX);
        case QM_I2S_CTRL_CMD_RX_CLEAR:
            return (priv->rx_handle != NULL && i2s_channel_disable(priv->rx_handle) == ESP_OK &&
                    i2s_channel_enable(priv->rx_handle) == ESP_OK) ? QM_EOK : -QM_EIO;
        case QM_I2S_CTRL_CMD_TX_CLEAR:
            return (priv->tx_handle != NULL && i2s_channel_disable(priv->tx_handle) == ESP_OK &&
                    i2s_channel_enable(priv->tx_handle) == ESP_OK) ? QM_EOK : -QM_EIO;
        case QM_I2S_CTRL_CMD_SET_SAMPLE_RATE:
            if (param == NULL) {
                return -QM_EINVAL;
            }
            /* First version only records the requested rate; live reconfiguration will be added later. */
            i2s->config.sample_rate = *(uint32_t *)param;
            return -QM_ENOSYS;
        default:
            return -QM_EINVAL;
    }
}

int32_t qm_i2s_read(qm_i2s_dev_t *i2s, void *data, uint32_t expect_size, uint32_t timeout)
{
    qm_i2s_priv_t *priv = NULL;
    size_t read_size = 0;

    if (i2s == NULL || i2s->priv == NULL || data == NULL || expect_size == 0) {
        return -QM_EINVAL;
    }

    priv = (qm_i2s_priv_t *)i2s->priv;
    if (priv->rx_handle == NULL) {
        return -QM_ENOSYS;
    }

    if (i2s_channel_read(priv->rx_handle, data, expect_size, &read_size, timeout / portTICK_PERIOD_MS) != ESP_OK) {
        return -QM_ETIMEOUT;
    }

    return (int32_t)read_size;
}

int32_t qm_i2s_write(qm_i2s_dev_t *i2s, const void *data, uint32_t size, uint32_t timeout)
{
    qm_i2s_priv_t *priv = NULL;
    size_t write_size = 0;

    if (i2s == NULL || i2s->priv == NULL || data == NULL || size == 0) {
        return -QM_EINVAL;
    }

    priv = (qm_i2s_priv_t *)i2s->priv;
    if (priv->tx_handle == NULL) {
        return -QM_ENOSYS;
    }

    if (i2s_channel_write(priv->tx_handle, data, size, &write_size, timeout / portTICK_PERIOD_MS) != ESP_OK) {
        return -QM_ETIMEOUT;
    }

    return (int32_t)write_size;
}

int32_t qm_i2s_deinit(qm_i2s_dev_t *i2s)
{
    qm_i2s_priv_t *priv = NULL;

    if (i2s == NULL || i2s->priv == NULL) {
        return -QM_EINVAL;
    }

    priv = (qm_i2s_priv_t *)i2s->priv;

    /* Disable before delete so the channel state matches ESP-IDF lifecycle requirements. */
    if (priv->tx_handle != NULL) {
        i2s_channel_disable(priv->tx_handle);
        i2s_del_channel(priv->tx_handle);
    }

    if (priv->rx_handle != NULL) {
        i2s_channel_disable(priv->rx_handle);
        i2s_del_channel(priv->rx_handle);
    }

    qm_free(priv);
    i2s->priv = NULL;

    return QM_EOK;
}
