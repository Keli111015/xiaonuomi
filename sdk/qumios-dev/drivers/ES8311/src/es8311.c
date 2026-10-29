#include "qm_log.h"
#include "qm_kernel.h"

#include "es8311.h"
#include "es8311_reg.h"

#define LOG_TAG "es8311"

#ifndef BIT
#define BIT(n) (1UL << (n))
#endif

struct _coeff_div {
    uint32_t mclk;
    uint32_t rate;
    uint8_t pre_div;
    uint8_t pre_multi;
    uint8_t adc_div;
    uint8_t dac_div;
    uint8_t fs_mode;
    uint8_t lrck_h;
    uint8_t lrck_l;
    uint8_t bclk_div;
    uint8_t adc_osr;
    uint8_t dac_osr;
};

static const struct _coeff_div coeff_div[] = {
    {12288000, 8000, 0x06, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 8000, 0x03, 0x01, 0x03, 0x03, 0x00, 0x05, 0xff, 0x18, 0x10, 0x10},
    {16384000, 8000, 0x08, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 8000, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 8000, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 8000, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 8000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000, 8000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 8000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000, 8000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {11289600, 11025, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 11025, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 11025, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 11025, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 12000, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 12000, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 12000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 12000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 16000, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 16000, 0x03, 0x01, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x10},
    {16384000, 16000, 0x04, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 16000, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 16000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 16000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000, 16000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 16000, 0x03, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1024000, 16000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {11289600, 22050, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 22050, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 22050, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 22050, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {705600, 22050, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 24000, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 24000, 0x03, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 24000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 24000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 24000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 32000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 32000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x10},
    {16384000, 32000, 0x02, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 32000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 32000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000, 32000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 32000, 0x03, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000, 32000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 32000, 0x03, 0x03, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
    {1024000, 32000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {11289600, 44100, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 44100, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 48000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 48000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 48000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 64000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 64000, 0x03, 0x02, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {16384000, 64000, 0x01, 0x00, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000, 64000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 64000, 0x01, 0x02, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {4096000, 64000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 64000, 0x01, 0x03, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {2048000, 64000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 64000, 0x01, 0x03, 0x01, 0x01, 0x01, 0x00, 0xbf, 0x03, 0x18, 0x18},
    {1024000, 64000, 0x01, 0x03, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
    {11289600, 88200, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800, 88200, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400, 88200, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200, 88200, 0x01, 0x03, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
    {12288000, 96000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 96000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000, 96000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000, 96000, 0x01, 0x03, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000, 96000, 0x01, 0x03, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
};

static qm_err_t write_reg(es8311_dev_t *es8311, uint8_t reg_addr, uint8_t value)
{
    uint8_t write_buf[2] = {reg_addr, value};

    /* ES8311 register access is always a simple [reg, value] write transaction. */
    return qm_i2c_master_write(es8311->i2c_dev,
                               es8311->dev_addr,
                               write_buf,
                               sizeof(write_buf),
                               1000);
}

static qm_err_t read_reg(es8311_dev_t *es8311, uint8_t reg_addr, uint8_t *value)
{
    if (value == NULL) {
        return -QM_EINVAL;
    }

    /* Reads use a register-address write followed by a single-byte readback. */
    return qm_i2c_master_write_read(es8311->i2c_dev,
                                    es8311->dev_addr,
                                    &reg_addr,
                                    1,
                                    value,
                                    1,
                                    1000);
}

static int get_coeff(uint32_t mclk, uint32_t rate)
{
    int i = 0;

    for (i = 0; i < (int)(sizeof(coeff_div) / sizeof(coeff_div[0])); i++) {
        if ((coeff_div[i].mclk == mclk) && (coeff_div[i].rate == rate)) {
            return i;
        }
    }

    return -1;
}

static qm_err_t resolution_to_reg(es8311_resolution_t resolution, uint8_t *reg)
{
    if (reg == NULL) {
        return -QM_EINVAL;
    }

    switch (resolution) {
        case ES8311_RESOLUTION_16:
            *reg |= (3 << 2);
            break;
        case ES8311_RESOLUTION_18:
            *reg |= (2 << 2);
            break;
        case ES8311_RESOLUTION_20:
            *reg |= (1 << 2);
            break;
        case ES8311_RESOLUTION_24:
            *reg |= (0 << 2);
            break;
        case ES8311_RESOLUTION_32:
            *reg |= (4 << 2);
            break;
        default:
            return -QM_EINVAL;
    }

    return QM_EOK;
}

static qm_err_t configure_clock(es8311_dev_t *es8311,
                                const es8311_clock_config_t *clk_cfg,
                                es8311_resolution_t resolution)
{
    uint8_t reg01 = 0x3F;
    uint8_t reg06 = 0;
    int32_t mclk_hz = 0;
    qm_err_t ret = QM_EOK;

    if (clk_cfg->mclk_from_mclk_pin) {
        mclk_hz = clk_cfg->mclk_frequency;
    } else {
        mclk_hz = clk_cfg->sample_frequency * (int32_t)resolution * 2;
        reg01 |= BIT(7);
    }

    if (clk_cfg->mclk_inverted) {
        reg01 |= BIT(6);
    }

    /* Register 0x01 selects the clock source and inversion policy. */
    ret = write_reg(es8311, ES8311_CLK_MANAGER_REG01, reg01);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = read_reg(es8311, ES8311_CLK_MANAGER_REG06, &reg06);
    if (ret != QM_EOK) {
        return ret;
    }

    if (clk_cfg->sclk_inverted) {
        reg06 |= BIT(5);
    } else {
        reg06 &= (uint8_t)(~BIT(5));
    }

    ret = write_reg(es8311, ES8311_CLK_MANAGER_REG06, reg06);
    if (ret != QM_EOK) {
        return ret;
    }

    /* Divider programming is split out so runtime sample-rate changes can reuse the same path. */
    return es8311_set_sample_frequency(es8311, mclk_hz, clk_cfg->sample_frequency);
}

static qm_err_t configure_format(es8311_dev_t *es8311,
                                 es8311_resolution_t input_resolution,
                                 es8311_resolution_t output_resolution)
{
    uint8_t reg00 = 0;
    uint8_t reg09 = 0;
    uint8_t reg0a = 0;
    qm_err_t ret = QM_EOK;

    ret = read_reg(es8311, ES8311_RESET_REG00, &reg00);
    if (ret != QM_EOK) {
        return ret;
    }

    reg00 &= 0xBF;
    ret = write_reg(es8311, ES8311_RESET_REG00, reg00);
    if (ret != QM_EOK) {
        return ret;
    }

    /* SDP input/output resolution is carried by registers 0x09 and 0x0A. */
    ret = resolution_to_reg(input_resolution, &reg09);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = resolution_to_reg(output_resolution, &reg0a);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = write_reg(es8311, ES8311_SDPIN_REG09, reg09);
    if (ret != QM_EOK) {
        return ret;
    }

    return write_reg(es8311, ES8311_SDPOUT_REG0A, reg0a);
}

qm_err_t es8311_set_sample_frequency(es8311_dev_t *es8311, int32_t mclk_frequency, int32_t sample_frequency)
{
    int coeff = 0;
    const struct _coeff_div *selected_coeff = NULL;
    uint8_t regv = 0;
    uint8_t reg03 = 0;
    uint8_t reg05 = 0;
    qm_err_t ret = QM_EOK;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    coeff = get_coeff((uint32_t)mclk_frequency, (uint32_t)sample_frequency);
    if (coeff < 0) {
        QM_LOGE(LOG_TAG, "unsupported sample frequency %d with mclk %d", sample_frequency, mclk_frequency);
        return -QM_EINVAL;
    }

    selected_coeff = &coeff_div[coeff];

    /* These divider registers are programmed from the coefficient table validated in the app driver. */
    ret = read_reg(es8311, ES8311_CLK_MANAGER_REG02, &regv);
    if (ret != QM_EOK) {
        return ret;
    }
    regv &= 0x07;
    regv |= (uint8_t)((selected_coeff->pre_div - 1) << 5);
    regv |= (uint8_t)(selected_coeff->pre_multi << 3);
    ret = write_reg(es8311, ES8311_CLK_MANAGER_REG02, regv);
    if (ret != QM_EOK) {
        return ret;
    }

    reg03 = (uint8_t)((selected_coeff->fs_mode << 6) | selected_coeff->adc_osr);
    ret = write_reg(es8311, ES8311_CLK_MANAGER_REG03, reg03);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = write_reg(es8311, ES8311_CLK_MANAGER_REG04, selected_coeff->dac_osr);
    if (ret != QM_EOK) {
        return ret;
    }

    reg05 = (uint8_t)(((selected_coeff->adc_div - 1) << 4) | (selected_coeff->dac_div - 1));
    ret = write_reg(es8311, ES8311_CLK_MANAGER_REG05, reg05);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = read_reg(es8311, ES8311_CLK_MANAGER_REG06, &regv);
    if (ret != QM_EOK) {
        return ret;
    }
    regv &= 0xE0;
    if (selected_coeff->bclk_div < 19) {
        regv |= (uint8_t)(selected_coeff->bclk_div - 1);
    } else {
        regv |= selected_coeff->bclk_div;
    }
    ret = write_reg(es8311, ES8311_CLK_MANAGER_REG06, regv);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = read_reg(es8311, ES8311_CLK_MANAGER_REG07, &regv);
    if (ret != QM_EOK) {
        return ret;
    }
    regv &= 0xC0;
    regv |= selected_coeff->lrck_h;
    ret = write_reg(es8311, ES8311_CLK_MANAGER_REG07, regv);
    if (ret != QM_EOK) {
        return ret;
    }

    return write_reg(es8311, ES8311_CLK_MANAGER_REG08, selected_coeff->lrck_l);
}

qm_err_t es8311_init(es8311_dev_t *es8311)
{
    qm_err_t ret = QM_EOK;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    if ((es8311->clock_config.sample_frequency < 8000) ||
        (es8311->clock_config.sample_frequency > 96000)) {
        return -QM_EINVAL;
    }

    if ((!es8311->clock_config.mclk_from_mclk_pin) &&
        (es8311->input_resolution != es8311->output_resolution)) {
        return -QM_EINVAL;
    }

    /* Reset -> release -> power-on follows the same sequence as the validated app-side driver. */
    ret = write_reg(es8311, ES8311_RESET_REG00, 0x1F);
    if (ret != QM_EOK) {
        goto _error;
    }
    qm_msleep(20);

    ret = write_reg(es8311, ES8311_RESET_REG00, 0x00);
    if (ret != QM_EOK) {
        goto _error;
    }

    ret = write_reg(es8311, ES8311_RESET_REG00, 0x80);
    if (ret != QM_EOK) {
        goto _error;
    }

    ret = configure_clock(es8311, &es8311->clock_config, es8311->output_resolution);
    if (ret != QM_EOK) {
        goto _error;
    }

    ret = configure_format(es8311, es8311->input_resolution, es8311->output_resolution);
    if (ret != QM_EOK) {
        goto _error;
    }

    /* The remaining writes enable the analog path and un-bypass the functional blocks we need. */
    ret = write_reg(es8311, ES8311_SYSTEM_REG0D, 0x01);
    if (ret != QM_EOK) {
        goto _error;
    }
    ret = write_reg(es8311, ES8311_SYSTEM_REG0E, 0x02);
    if (ret != QM_EOK) {
        goto _error;
    }
    ret = write_reg(es8311, ES8311_SYSTEM_REG12, 0x00);
    if (ret != QM_EOK) {
        goto _error;
    }
    ret = write_reg(es8311, ES8311_SYSTEM_REG13, 0x10);
    if (ret != QM_EOK) {
        goto _error;
    }
    ret = write_reg(es8311, ES8311_ADC_REG1C, 0x6A);
    if (ret != QM_EOK) {
        goto _error;
    }
    ret = write_reg(es8311, ES8311_DAC_REG37, 0x08);
    if (ret != QM_EOK) {
        goto _error;
    }
    return QM_EOK;

_error:
    return ret;
}

qm_err_t es8311_deinit(es8311_dev_t *es8311)
{
    qm_err_t ret = QM_EOK;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    /* Mute first so board-level shutdown is less likely to pop on the speaker path. */
    ret = es8311_set_voice_mute(es8311, QM_TRUE);
    if (ret != QM_EOK) {
        return ret;
    }

    /* Bring down output and analog blocks in the opposite direction of init. */
    ret = write_reg(es8311, ES8311_SYSTEM_REG13, 0x00);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = write_reg(es8311, ES8311_SYSTEM_REG12, 0xFF);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = write_reg(es8311, ES8311_SYSTEM_REG0E, 0x00);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = write_reg(es8311, ES8311_SYSTEM_REG0D, 0x00);
    if (ret != QM_EOK) {
        return ret;
    }

    /* Reset digital state so a later init starts from a known baseline. */
    return write_reg(es8311, ES8311_RESET_REG00, 0x1F);
}

qm_err_t es8311_set_voice_volume(es8311_dev_t *es8311, int32_t volume, int32_t *volume_set)
{
    int32_t reg32 = 0;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    if (volume < 0) {
        volume = 0;
    } else if (volume > 100) {
        volume = 100;
    }

    if (volume == 0) {
        reg32 = 0;
    } else {
        reg32 = ((volume * 256) / 100) - 1;
    }

    if (volume_set != NULL) {
        *volume_set = volume;
    }

    /* User-facing 0~100 volume is mapped into ES8311 DAC register space. */
    return write_reg(es8311, ES8311_DAC_REG32, (uint8_t)reg32);
}

qm_err_t es8311_get_voice_volume(es8311_dev_t *es8311, int32_t *volume)
{
    uint8_t reg32 = 0;
    qm_err_t ret = QM_EOK;

    if (es8311 == NULL || es8311->i2c_dev == NULL || volume == NULL) {
        return -QM_EINVAL;
    }

    ret = read_reg(es8311, ES8311_DAC_REG32, &reg32);
    if (ret != QM_EOK) {
        return ret;
    }

    if (reg32 == 0) {
        *volume = 0;
    } else {
        *volume = ((reg32 * 100) / 256) + 1;
    }

    return QM_EOK;
}

qm_err_t es8311_set_voice_mute(es8311_dev_t *es8311, bool_t mute)
{
    uint8_t reg31 = 0;
    qm_err_t ret = QM_EOK;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    ret = read_reg(es8311, ES8311_DAC_REG31, &reg31);
    if (ret != QM_EOK) {
        return ret;
    }

    if (mute) {
        reg31 |= (uint8_t)(BIT(6) | BIT(5));
    } else {
        reg31 &= (uint8_t)(~(BIT(6) | BIT(5)));
    }

    return write_reg(es8311, ES8311_DAC_REG31, reg31);
}

qm_err_t es8311_config_microphone(es8311_dev_t *es8311, bool_t digital_mic)
{
    uint8_t reg14 = 0x1A;
    qm_err_t ret = QM_EOK;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    if (digital_mic) {
        reg14 |= BIT(6);
    }

    ret = write_reg(es8311, ES8311_ADC_REG17, 0xC8);
    if (ret != QM_EOK) {
        return ret;
    }

    return write_reg(es8311, ES8311_SYSTEM_REG14, reg14);
}

qm_err_t es8311_set_microphone_gain(es8311_dev_t *es8311, es8311_mic_gain_t gain_db)
{
    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    if ((gain_db <= ES8311_MIC_GAIN_MIN) || (gain_db >= ES8311_MIC_GAIN_MAX)) {
        return -QM_EINVAL;
    }

    return write_reg(es8311, ES8311_ADC_REG16, (uint8_t)gain_db);
}

qm_err_t es8311_set_voice_fade(es8311_dev_t *es8311, es8311_fade_t fade)
{
    uint8_t reg37 = 0;
    qm_err_t ret = QM_EOK;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    ret = read_reg(es8311, ES8311_DAC_REG37, &reg37);
    if (ret != QM_EOK) {
        return ret;
    }

    reg37 &= 0x0F;
    reg37 |= (uint8_t)(fade << 4);

    return write_reg(es8311, ES8311_DAC_REG37, reg37);
}

qm_err_t es8311_set_microphone_fade(es8311_dev_t *es8311, es8311_fade_t fade)
{
    uint8_t reg15 = 0;
    qm_err_t ret = QM_EOK;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return -QM_EINVAL;
    }

    ret = read_reg(es8311, ES8311_ADC_REG15, &reg15);
    if (ret != QM_EOK) {
        return ret;
    }

    reg15 &= 0x0F;
    reg15 |= (uint8_t)(fade << 4);

    return write_reg(es8311, ES8311_ADC_REG15, reg15);
}

void es8311_register_dump(es8311_dev_t *es8311)
{
    int reg = 0;
    uint8_t value = 0;

    if (es8311 == NULL || es8311->i2c_dev == NULL) {
        return;
    }

    for (reg = 0; reg < 0x4A; reg++) {
        if (read_reg(es8311, (uint8_t)reg, &value) == QM_EOK) {
            QM_LOGI(LOG_TAG, "reg[%02x] = 0x%02x", reg, value);
        }
    }
}
