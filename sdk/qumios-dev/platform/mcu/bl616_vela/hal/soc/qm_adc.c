#include "qm.h"

#include "qm_adc.h"
#include "qm_utils_timer.h"

#include <nuttx/config.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <nuttx/arch.h>
#include <nuttx/ioexpander/gpio.h>
#include <arch/board/board.h>
#include "riscv_arch.h"
#include "bl602_gpio.h"
#include "bl602_adc.h"
#include "bl602_romapi.h"
#include "hardware/bl602_aon.h"
#include "hardware/bl602_ef.h"
#include "hardware/bl602_glb.h"
#include "hardware/bl602_gpip.h"
#include "hardware/bl602_memorymap.h"

#define LOG_TAG "adc"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/*
 * @brief ADC configuration structure
 *
 * @param clk_div                ADC clock divison, use @ref ADC_CLK_DIV, adc clk must be less than 500K.
 * @param scan_conv_mode         ADC scan mode enable
 * @param continuous_conv_mode   ADC continuous conversion enable
 * @param differential_mode      ADC differential mode enable
 * @param resolution             ADC resolution, use @ref ADC_RESOLUTION
 * @param vref                   ADC reference select, use @ref ADC_VREF
 */

struct bl602_adc_config_s
{
  uint8_t clk_div;
  uint8_t scan_conv_mode;
  uint8_t continuous_conv_mode;
  uint8_t differential_mode;
  uint8_t resolution;
  uint8_t vref;
};

/*
 * @brief ADC result select
 *
 * @param pos_chan         ADC positive channel
 * @param neg_chan         ADC negative channel
 * @param value            ADC value
 * @param millivolt        ADC voltage result
 */

struct bl602_adc_result_s
{
  int8_t pos_chan;
  int8_t neg_chan;
  int32_t value;
  int32_t millivolt;
};

typedef enum 
{
	QM_ADC_CHANNEL_TYPE_0 = 0,
	QM_ADC_CHANNEL_TYPE_1 = 1,
	QM_ADC_CHANNEL_TYPE_2 = 2,
	QM_ADC_CHANNEL_TYPE_3 = 3,
	QM_ADC_CHANNEL_TYPE_4 = 4,
	QM_ADC_CHANNEL_TYPE_5 = 5,
	QM_ADC_CHANNEL_TYPE_6 = 6,
	QM_ADC_CHANNEL_TYPE_7 = 7,
	QM_ADC_CHANNEL_TYPE_8 = 8,
	QM_ADC_CHANNEL_TYPE_9 = 9,
	QM_ADC_CHANNEL_TYPE_10 = 10,
	QM_ADC_CHANNEL_TYPE_11 = 10,
}qm_adc_channel_type_t;

typedef struct 
{
	uint8_t adc_ch;
	uint8_t gpio_pin;
}qm_adc_handle_t;


/* ADC clock reference */

static uint32_t g_clk_ref = 0;
static volatile float g_adc_gain_coe = 1.0f;
static volatile int32_t g_adc_ref_chan = -1;
static volatile int32_t g_adc_ref_chan_mv = -1;
static volatile uint32_t g_gnd_offset = 0;

static const qm_adc_handle_t g_adc_handle[] = 
{
	{QM_ADC_CHANNEL_TYPE_1, GPIO_PIN4},
	{QM_ADC_CHANNEL_TYPE_4, GPIO_PIN5},
	{QM_ADC_CHANNEL_TYPE_5, GPIO_PIN6},
	{QM_ADC_CHANNEL_TYPE_6, GPIO_PIN9},
	{QM_ADC_CHANNEL_TYPE_7, GPIO_PIN9},
	{QM_ADC_CHANNEL_TYPE_8, GPIO_PIN10},
	{QM_ADC_CHANNEL_TYPE_9, GPIO_PIN10},
    {QM_ADC_CHANNEL_TYPE_10, GPIO_PIN11},
    {QM_ADC_CHANNEL_TYPE_0, GPIO_PIN12},
    {QM_ADC_CHANNEL_TYPE_3, GPIO_PIN13},
    {QM_ADC_CHANNEL_TYPE_2, GPIO_PIN14},
    {QM_ADC_CHANNEL_TYPE_11, GPIO_PIN15},
};


/****************************************************************************
 * Name: bl602_set_adc_clk
 *
 * Description:
 *   Set ADC clock.
 *
 * Input Parameters:
 *   enable - Enable or disable adc clock
 *   clkSel - Clock type
 *   dev - ADC clock divider
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl602_set_adc_clk(uint8_t enable,
                              BL602_ADC_CLK_Type clkSel,
                              uint8_t div)
{
    uint32_t regval = 0;
    uint32_t reg_base = 0;

    reg_base = BL602_GLB_BASE;
    regval = getreg32(reg_base + BL602_GPADC_32M_SRC_CTRL_OFFSET);
    regval &= ~GPADC_32M_SRC_CTRL_GPADC_32M_DIV_EN_MASK;
    putreg32(regval, reg_base + BL602_GPADC_32M_SRC_CTRL_OFFSET);

    regval = getreg32(reg_base + BL602_GPADC_32M_SRC_CTRL_OFFSET);
    regval &= ~GPADC_32M_SRC_CTRL_GPADC_32M_CLK_DIV_MASK;
    regval |= div << GPADC_32M_SRC_CTRL_GPADC_32M_CLK_DIV_SHIFT;

    regval &= ~GPADC_32M_SRC_CTRL_GPADC_32M_CLK_SEL_MASK;
    regval |= clkSel << GPADC_32M_SRC_CTRL_GPADC_32M_CLK_SEL_SHIFT;
    putreg32(regval, reg_base + BL602_GPADC_32M_SRC_CTRL_OFFSET);

    regval = getreg32(reg_base + BL602_GPADC_32M_SRC_CTRL_OFFSET);

    if(enable){
        regval |= 1 << GPADC_32M_SRC_CTRL_GPADC_32M_DIV_EN_SHIFT;
    }else{
        regval &= ~GPADC_32M_SRC_CTRL_GPADC_32M_DIV_EN_MASK;
    }

    putreg32(regval, reg_base + BL602_GPADC_32M_SRC_CTRL_OFFSET);
}


/****************************************************************************
 * Name: bl602_adc_enable_clk
 *
 * Description:
 *   Enable ADC clock.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl602_adc_enable_clk(void)
{
    irqstate_t flags;

    flags = enter_critical_section();

    if(!g_clk_ref){
        bl602_set_adc_clk(1, BL602_ADC_CLK_XCLK, 1);
    }

    g_clk_ref++;

    leave_critical_section(flags);
}

/****************************************************************************
 * Name: bl602_adc_disable_clk
 *
 * Description:
 *   Disable ADC clock.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None.
 *
 ****************************************************************************/

static void bl602_adc_disable_clk(void)
{
  irqstate_t flags;

  flags = enter_critical_section();

  g_clk_ref--;

  if (!g_clk_ref)
    {
      bl602_set_adc_clk(0, BL602_ADC_CLK_XCLK, 1);
    }

  leave_critical_section(flags);
}


/****************************************************************************
 * Name: bl602_adc_get_count
 *
 * Description:
 *   Get adc number of completed conversions.
 *
 * Input Parameters:
 *   None.
 *
 * Returned Value:
 *   Return number of completed conversions
 *
 ****************************************************************************/

static uint8_t bl602_adc_get_count(void)
{
  return ((getreg32(BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET) &
           GPADC_CONFIG_GPADC_FIFO_DATA_COUNT_MASK) >>
          GPADC_CONFIG_GPADC_FIFO_DATA_COUNT_SHIFT);
}


/****************************************************************************
 * Name: bl602_adc_read_raw
 *
 * Description:
 *   Read adc conversion value
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   Return conversion value.
 *
 ****************************************************************************/

static uint32_t bl602_adc_read_raw(void)
{
  return getreg32(BL602_GPIP_BASE + BL602_GPADC_DMA_RDATA_OFFSET) &
         GPADC_DMA_RDATA_MASK;
}


/****************************************************************************
 * Name: bl602_adc_parse_result
 *
 * Description:
 *   Parse adc conversion value into millivolt and actual numerical value.
 *
 * Input Parameters:
 *   buffer - pointer to adc original value from bl602_adc_read_raw api
 *   result - pointer to save parse result
 *   count - count to parse
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_adc_parse_result(uint32_t *buffer,
                                   struct bl602_adc_result_s *result,
                                   uint16_t count)
{
  uint32_t reg_base;
  uint8_t resolution;
  uint8_t diff_mode;
  uint8_t vref;
  uint32_t conv_result = 0;
  uint16_t ref = 3200;
  uint8_t neg = 0;
  uint32_t tmp;
  int32_t chan_vref = 0;

  reg_base = BL602_AON_BASE;

  resolution = (getreg32(reg_base + BL602_GPADC_REG_CONFIG1_OFFSET) &
                GPADC_REG_CONFIG1_GPADC_RES_SEL_MASK) >>
               GPADC_REG_CONFIG1_GPADC_RES_SEL_SHIFT;
  diff_mode = (getreg32(reg_base + BL602_GPADC_REG_CONFIG2_OFFSET) &
               GPADC_REG_CONFIG2_GPADC_DIFF_MODE) >>
              2;
  vref = (getreg32(reg_base + BL602_GPADC_REG_CONFIG2_OFFSET) &
          GPADC_REG_CONFIG2_GPADC_VREF_SEL) >>
         3;

  if (vref == BL602_ADC_VREF_2P0V)
    {
      ref = 2000;
    }

  /* single mode */

  if (diff_mode == 0)
    {
      for (uint16_t i = 0; i < count; i++)
        {
          if ((buffer[i] >> 21) == g_adc_ref_chan)
            {
              if (resolution == BL602_ADC_RESOLUTION_12B)
                {
                  conv_result =
                    (uint32_t)(((buffer[i] & 0xffff) >> 4) / g_adc_gain_coe);
                  if (conv_result > 4095)
                    {
                      conv_result = 4095;
                    }
                  chan_vref = (int32_t)conv_result * ref / 4096;
                }
              else if (resolution == BL602_ADC_RESOLUTION_14B)
                {
                  conv_result =
                    (uint32_t)(((buffer[i] & 0xffff) >> 2) / g_adc_gain_coe);
                  if (conv_result > 16383)
                    {
                      conv_result = 16383;
                    }
                  chan_vref = (int32_t)conv_result * ref / 16384;
                }
              else if (resolution == BL602_ADC_RESOLUTION_16B)
                {
                  conv_result =
                    (uint32_t)((buffer[i] & 0xffff) / g_adc_gain_coe);
                  if (conv_result > 65535)
                    {
                      conv_result = 65535;
                    }
                  chan_vref = (int32_t)conv_result * ref / 65536;
                }
              else
                {
                }
            }
        }

      for (uint16_t i = 0; i < count; i++)
        {
          result[i].pos_chan = buffer[i] >> 21;
          result[i].neg_chan = -1;

          if (resolution == BL602_ADC_RESOLUTION_12B)
            {
              conv_result =
                (uint32_t)(((buffer[i] & 0xffff) >> 4) / g_adc_gain_coe);
              if (conv_result > 4095)
                {
                  conv_result = 4095;
                }
              if (conv_result > g_gnd_offset)
                {
                  conv_result -= g_gnd_offset;
                }
              result[i].value = conv_result;
              result[i].millivolt = (int32_t)result[i].value * ref / 4096;
            }
          else if (resolution == BL602_ADC_RESOLUTION_14B)
            {
              conv_result =
                (uint32_t)(((buffer[i] & 0xffff) >> 2) / g_adc_gain_coe);
              if (conv_result > 16383)
                {
                  conv_result = 16383;
                }
              if (conv_result > g_gnd_offset)
                {
                  conv_result -= g_gnd_offset;
                }
              result[i].value = conv_result;
              result[i].millivolt = (int32_t)result[i].value * ref / 16384;
            }
          else if (resolution == BL602_ADC_RESOLUTION_16B)
            {
              conv_result = (uint32_t)((buffer[i] & 0xffff) / g_adc_gain_coe);
              if (conv_result > 65535)
                {
                  conv_result = 65535;
                }
              if (conv_result > g_gnd_offset)
                {
                  conv_result -= g_gnd_offset;
                }
              result[i].value = conv_result;
              result[i].millivolt = (int32_t)result[i].value * ref / 65536;
            }
          else
            {
            }

          if ((g_adc_ref_chan != -1) && ((buffer[i] >> 21) != g_adc_ref_chan))
            {
              if (chan_vref)
                {
                  result[i].millivolt =
                    result[i].millivolt * g_adc_ref_chan_mv / chan_vref;
                }
              else
                {
                  result[i].millivolt = 0;
                }
            }
        }
    }
  else
    {
      for (uint16_t i = 0; i < count; i++)
        {
          result[i].pos_chan = buffer[i] >> 21;
          result[i].neg_chan = (buffer[i] >> 16) & 0x1F;

          tmp = buffer[i];

          if (tmp & 0x8000)
            {
              tmp = ~tmp;
              tmp += 1;
              neg = 1;
            }

          if (resolution == BL602_ADC_RESOLUTION_12B)
            {
              conv_result =
                (uint32_t)(((tmp & 0xffff) >> 4) / g_adc_gain_coe);
              if (conv_result > 2047)
                {
                  conv_result = 2047;
                }
              result[i].value = conv_result;
              result[i].millivolt = (int32_t)result[i].value * ref / 2048;
            }
          else if (resolution == BL602_ADC_RESOLUTION_14B)
            {
              conv_result =
                (uint32_t)(((tmp & 0xffff) >> 2) / g_adc_gain_coe);
              if (conv_result > 8191)
                {
                  conv_result = 8191;
                }
              result[i].value = conv_result;
              result[i].millivolt = (int32_t)result[i].value * ref / 8192;
            }
          else if (resolution == BL602_ADC_RESOLUTION_16B)
            {
              conv_result = (uint32_t)((tmp & 0xffff) / g_adc_gain_coe);
              if (conv_result > 32767)
                {
                  conv_result = 32767;
                }
              result[i].value = conv_result;
              result[i].millivolt = (int32_t)result[i].value * ref / 32768;
            }
          else
            {
            }

          if (neg)
            {
              result[i].value = -result[i].value;
              result[i].millivolt = -result[i].millivolt;
            }
        }
    }
}


/****************************************************************************
 * Name: bl602_adc_start_conversion
 *
 * Description:
 *   Start adc conversion
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_adc_start_conversion(void)
{
  uint32_t regval;
  uint32_t reg_base;

  reg_base = BL602_AON_BASE;

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval &= ~GPADC_REG_CMD_GPADC_CONV_START;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval |= GPADC_REG_CMD_GPADC_CONV_START;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);
}

/****************************************************************************
 * Name: bl602_adc_stop_conversion
 *
 * Description:
 *   Stop adc conversion
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_adc_stop_conversion(void)
{
  uint32_t regval;
  uint32_t reg_base;

  reg_base = BL602_AON_BASE;
  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval &= ~GPADC_REG_CMD_GPADC_CONV_START;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);
}


/****************************************************************************
 * Name: bl602_adc_gain_trim_update
 *
 * Description:
 *   Update adc gain trim value from efuse
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_adc_gain_trim_update(void)
{
  uint32_t adcGainCoeff;
  uint32_t adcGainCoeffParity;
  uint32_t adcGainCoeffEn;
  uint32_t tmp = 0;
  int ret;

  /* Switch to AHB clock */

  bl602_romapi_efuse_ctrl_switch_ahb_clk_0();

  bl602_romapi_efuse_ctrl_load_r0();

  /* read adc gain trim */

  tmp = getreg32(BL602_EF_BASE + BL602_EF_KEY_SLOT_5_W3_OFFSET);
  adcGainCoeff = (tmp >> 1) & 0xfff;
  adcGainCoeffParity = (tmp >> 13) & 0x01;
  adcGainCoeffEn = (tmp >> 14) & 0x01;

  if (adcGainCoeffEn)
    {
      if (adcGainCoeffParity ==
          bl602_romapi_efuse_get_trim_parity(adcGainCoeff, 12))
        {
          tmp = adcGainCoeff;
          if (tmp & 0x800)
            {
              tmp = ~tmp;
              tmp += 1;
              tmp = tmp & 0xfff;
              g_adc_gain_coe = (1.0 + ((float)tmp / 2048.0));
            }
          else
            {
              g_adc_gain_coe = (1.0 - ((float)tmp / 2048.0));
            }
        }
    }

  return;
}


/****************************************************************************
 * Name: bl602_adc_init
 *
 * Description:
 *   Update adc gain trim value from efuse
 *
 * Input Parameters:
 *   config - adc config
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_adc_init(const struct bl602_adc_config_s *config)
{
  uint32_t regval;
  uint32_t reg_base;
  uint32_t gnd_val = 0;
  qm_utils_time_t time = {0};

  reg_base = BL602_AON_BASE;

  /* adc disable */

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval &= ~GPADC_REG_CMD_GPADC_GLOBAL_EN;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval |= GPADC_REG_CMD_GPADC_GLOBAL_EN;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  /* adc reset */

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval |= GPADC_REG_CMD_GPADC_SOFT_RST;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval &= ~GPADC_REG_CMD_GPADC_SOFT_RST;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  regval = 0;
  regval |=
    (2 << GPADC_REG_CONFIG1_GPADC_V18_SEL_SHIFT); /* V18 select 1.82V */
  regval |=
    (1 << GPADC_REG_CONFIG1_GPADC_V11_SEL_SHIFT); /* V11 select 1.1V */
  regval |= (config->clk_div
             << GPADC_REG_CONFIG1_GPADC_CLK_DIV_RATIO_SHIFT); /* clock div */
  regval |= (config->resolution
             << GPADC_REG_CONFIG1_GPADC_RES_SEL_SHIFT); /* resolution */

  if (config->scan_conv_mode)
    {
      regval |= GPADC_REG_CONFIG1_GPADC_SCAN_EN;
      regval |= GPADC_REG_CONFIG1_GPADC_CLK_ANA_INV;
    }
  if (config->continuous_conv_mode)
    {
      regval |= GPADC_REG_CONFIG1_GPADC_CONT_CONV_EN;
    }

  putreg32(regval, reg_base + BL602_GPADC_REG_CONFIG1_OFFSET);

  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");
  __asm volatile("nop");

  /* CONFIG2 setting
   *
   * gpadc_dly_sel = 0x02
   * pga bit enable
   * pga1 gain = pga2 gain = 1
   * pgadc_pga_os_cal = 8
   * gpadc_chop_mode = 2
   * gpadc_pga_vcm = 1
   * 
   */

  regval = 0;
  regval |= (2 << GPADC_REG_CONFIG2_GPADC_DLY_SEL_SHIFT);
  regval |=
    (2
     << GPADC_REG_CONFIG2_GPADC_CHOP_MODE_SHIFT); /* Vref AZ and PGA chop on */
  regval |= (1 << GPADC_REG_CONFIG2_GPADC_PGA1_GAIN_SHIFT); /* gain 1 */
  regval |= (1 << GPADC_REG_CONFIG2_GPADC_PGA2_GAIN_SHIFT); /* gain 2 */
  regval |= GPADC_REG_CONFIG2_GPADC_PGA_EN;
  regval |= (8 << GPADC_REG_CONFIG2_GPADC_PGA_OS_CAL_SHIFT);
  regval |=
    (1
     << GPADC_REG_CONFIG2_GPADC_PGA_VCM_SHIFT); /* PGA output common mode control 1.2V */

  if (config->vref == BL602_ADC_VREF_2P0V)
    {
      regval |= GPADC_REG_CONFIG2_GPADC_VREF_SEL;
    }

  if (config->differential_mode)
    {
      regval |= GPADC_REG_CONFIG2_GPADC_DIFF_MODE;
    }

  putreg32(regval, reg_base + BL602_GPADC_REG_CONFIG2_OFFSET);

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval |= GPADC_REG_CMD_GPADC_MIC2_DIFF; /* mic2 diff enable */
  if (config->differential_mode)
    {
      regval &= ~GPADC_REG_CMD_GPADC_NEG_GND;
    }
  else
    {
      regval |= GPADC_REG_CMD_GPADC_NEG_GND;
    }
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  /* calibration offset */

  regval = getreg32(reg_base + BL602_GPADC_REG_DEFINE_OFFSET);
  regval &= ~GPADC_REG_DEFINE_GPADC_OS_CAL_DATA_MASK;
  putreg32(regval, reg_base + BL602_GPADC_REG_DEFINE_OFFSET);

  /* disable int and clear status */

  regval = getreg32(BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET);
  regval |=
    (GPADC_CONFIG_GPADC_FIFO_UNDERRUN_MASK |
     GPADC_CONFIG_GPADC_FIFO_OVERRUN_MASK | GPADC_CONFIG_GPADC_RDY_MASK |
     GPADC_CONFIG_GPADC_FIFO_UNDERRUN_CLR |
     GPADC_CONFIG_GPADC_FIFO_OVERRUN_CLR | GPADC_CONFIG_GPADC_RDY_CLR);

  regval |= GPADC_CONFIG_GPADC_FIFO_CLR;
  regval &= ~GPADC_CONFIG_GPADC_FIFO_THL_MASK;
  regval &= ~GPADC_CONFIG_GPADC_DMA_EN;
  putreg32(regval, BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET);

  regval = getreg32(BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET);
  regval &= ~(GPADC_CONFIG_GPADC_FIFO_UNDERRUN_CLR |
              GPADC_CONFIG_GPADC_FIFO_OVERRUN_CLR |
              GPADC_CONFIG_GPADC_RDY_CLR | GPADC_CONFIG_GPADC_FIFO_CLR);
  putreg32(regval, BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET);

  regval = getreg32(reg_base + BL602_GPADC_REG_ISR_OFFSET);
  regval |= GPADC_REG_ISR_GPADC_NEG_SATUR_MASK;
  regval |= GPADC_REG_ISR_GPADC_POS_SATUR_MASK;
  putreg32(regval, reg_base + BL602_GPADC_REG_ISR_OFFSET);

  bl602_adc_gain_trim_update();

  regval = getreg32(reg_base + BL602_GPADC_REG_CONFIG1_OFFSET);
  regval |= GPADC_REG_CONFIG1_GPADC_CONT_CONV_EN;
  regval &= ~GPADC_REG_CONFIG1_GPADC_SCAN_EN;
  putreg32(regval, reg_base + BL602_GPADC_REG_CONFIG1_OFFSET);

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval &= ~GPADC_REG_CMD_GPADC_NEG_GND;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval &= ~GPADC_REG_CMD_GPADC_POS_SEL_MASK;
  regval &= ~GPADC_REG_CMD_GPADC_NEG_SEL_MASK;
  regval |= (BL602_ADC_CHANNEL_GND << GPADC_REG_CMD_GPADC_POS_SEL_SHIFT);
  regval |= (BL602_ADC_CHANNEL_GND << GPADC_REG_CMD_GPADC_NEG_SEL_SHIFT);
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  bl602_adc_start_conversion();

  for (uint16_t i = 0; i < 10; i++)
    {
      regval = getreg32(BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET);
      regval |= GPADC_CONFIG_GPADC_FIFO_CLR;
      putreg32(regval, BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET);

      qm_utils_time_init(&time);
      qm_utils_time_countdown_ms(&time, 100);

      while (bl602_adc_get_count() == 0)
      {
        if(qm_utils_time_left(&time) <= 0){
            break;
        }
      }

      regval = bl602_adc_read_raw();

      if (i > 4)
        {
          gnd_val += (regval & 0xffff);
        }
    }

  bl602_adc_stop_conversion();
  bl_os_msleep(10);

  if (gnd_val > 0)
    {
      g_gnd_offset = gnd_val / 5;
    }
  else
    {
      g_gnd_offset = 0;
    }

  /* restore config from user */

  regval = getreg32(reg_base + BL602_GPADC_REG_CONFIG1_OFFSET);
  if (config->continuous_conv_mode)
    {
      regval |= GPADC_REG_CONFIG1_GPADC_CONT_CONV_EN;
    }
  else
    {
      regval &= ~GPADC_REG_CONFIG1_GPADC_CONT_CONV_EN;
    }
  if (config->scan_conv_mode)
    {
      regval |= GPADC_REG_CONFIG1_GPADC_SCAN_EN;
    }
  putreg32(regval, reg_base + BL602_GPADC_REG_CONFIG1_OFFSET);

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  if (config->differential_mode)
    {
      regval &= ~GPADC_REG_CMD_GPADC_NEG_GND;
    }
  else
    {
      regval |= GPADC_REG_CMD_GPADC_NEG_GND;
    }
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  regval = getreg32(BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET);
  regval |= GPADC_CONFIG_GPADC_FIFO_CLR;
  putreg32(regval, BL602_GPIP_BASE + BL602_GPADC_CONFIG_OFFSET);
}


/****************************************************************************
 * Name: bl602_adc_deinit
 *
 * Description:
 *   Deinitialize adc.
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_adc_deinit(void)
{
  uint32_t regval;
  uint32_t reg_base;

  reg_base = BL602_AON_BASE;

  /* adc disable */

  regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
  regval &= ~GPADC_REG_CMD_GPADC_GLOBAL_EN;
  putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);

  putreg32(0, reg_base + BL602_GPADC_REG_CONFIG1_OFFSET);
  putreg32(0, reg_base + BL602_GPADC_REG_CONFIG2_OFFSET);
}


/****************************************************************************
 * Name: bl602_adc_gpio_init
 *
 * Description:
 *   ADC GPIO initialize
 *
 * Input Parameters:
 *   pin - GPIO number
 *   cfgset - GPIO config
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_adc_gpio_init(uint8_t pin, uint32_t cfgset)
{
  uint32_t cfg = 0;
  uint32_t reg_base;
  uint32_t cfg_address;
  uint8_t drive;
  uint8_t function;
  uint16_t mode;

  reg_base = BL602_GLB_BASE;

  function = (cfgset & BL602_ADC_GPIO_FUNC_MASK) >> BL602_ADC_GPIO_FUNC_SHIFT;
  mode = (cfgset & BL602_ADC_GPIO_MODE_MASK);
  drive = (cfgset & BL602_ADC_GPIO_DRV_MASK) >> BL602_ADC_GPIO_DRV_SHIFT;

  uint32_t regval;
  uint8_t real_pin;
  uint8_t is_odd = 0;

  /* Disable output anyway */

  regval =
    getreg32(reg_base + BL602_GPIO_CFGCTL34_OFFSET + ((pin >> 5) << 2));
  regval &= ~(1 << (pin & 0x1f));
  putreg32(regval, reg_base + BL602_GPIO_CFGCTL34_OFFSET + ((pin >> 5) << 2));

  real_pin = pin;
  is_odd = real_pin & 1;

  cfg_address = reg_base + BL602_GPIO_CFGCTL0_OFFSET + (real_pin / 2 * 4);
  cfg = getreg32(cfg_address);
  cfg &= ~(0xffff << (16 * is_odd));

  regval =
    getreg32(reg_base + BL602_GPIO_CFGCTL34_OFFSET + ((pin >> 5) << 2));

  if (mode & BL602_ADC_GPIO_ANALOG)
    {
      regval &= ~(1 << (pin & 0x1f));
      function = 10;
    }
  else if (mode & BL602_ADC_GPIO_ALTERNATE)
    {
      cfg |= (1 << (is_odd * 16 + 0));
      regval &= ~(1 << (pin & 0x1f));
    }
  else
    {
      function = 11;

      if (mode & BL602_ADC_GPIO_INPUT)
        {
          cfg |= (1 << (is_odd * 16 + 0));
        }

      if (mode & BL602_ADC_GPIO_OUTPUT)
        {
          regval |= (1 << (pin & 0x1f));
        }
    }

  putreg32(regval, reg_base + BL602_GPIO_CFGCTL34_OFFSET + ((pin >> 5) << 2));

  if (cfgset & BL602_ADC_GPIO_PULLUP)
    {
      cfg |= (1 << (is_odd * 16 + 4));
    }
  else if (cfgset & BL602_ADC_GPIO_PULLDOWN)
    {
      cfg |= (1 << (is_odd * 16 + 5));
    }
  else
    {
    }

  if (cfgset & BL602_ADC_GPIO_SMT_EN)
    {
      cfg |= (1 << (is_odd * 16 + 1));
    }

  cfg |= (drive << (is_odd * 16 + 2));
  cfg |= (function << (is_odd * 16 + 8));

  putreg32(cfg, cfg_address);
}


/****************************************************************************
 * Name: bl602_adc_channel_config
 *
 * Description:
 *   Config adc channels to sample.
 *
 * Input Parameters:
 *   chan - pointer to the channel configurations.
 *   channels - number of channels
 *
 * Returned Value:
 *   Zero on success; a negated errno val
 *
 ****************************************************************************/

static int bl602_adc_channel_config(uint8_t pos_chan,
                                    uint8_t channels)
{
  uint32_t regval;
  uint32_t regval2;
  uint32_t reg_base;

  reg_base = BL602_AON_BASE;

    if(!(getreg32(reg_base + BL602_GPADC_REG_CONFIG1_OFFSET) &
        GPADC_REG_CONFIG1_GPADC_SCAN_EN)){
        if (channels > 1){
            return -EINVAL;
        }

        regval = getreg32(reg_base + BL602_GPADC_REG_CMD_OFFSET);
        regval &= ~GPADC_REG_CMD_GPADC_POS_SEL_MASK;
        regval &= ~GPADC_REG_CMD_GPADC_NEG_SEL_MASK;
        regval |= (pos_chan << GPADC_REG_CMD_GPADC_POS_SEL_SHIFT);
        regval |= (BL602_ADC_CHANNEL_GND << GPADC_REG_CMD_GPADC_NEG_SEL_SHIFT);
        putreg32(regval, reg_base + BL602_GPADC_REG_CMD_OFFSET);
    }else{
        if (channels < 6){
            regval = 0;
            regval2 = 0;
            for (uint8_t i = 0; i < channels; i++)
            {
                regval |= (pos_chan << (i * 5));
                regval2 |= (BL602_ADC_CHANNEL_GND << (i * 5));
            }
            putreg32(regval, reg_base + BL602_GPADC_REG_SCN_POS1_OFFSET);
            putreg32(regval2, reg_base + BL602_GPADC_REG_SCN_NEG1_OFFSET);
        }
    }
  return 0;
}


/**ADC最多检测端口为5个**/
int32_t qm_adc_init(qm_adc_dev_t *adc)
{
    qm_adc_handle_t *adc_handle = NULL;
    struct bl602_adc_config_s adc_cfg = {.clk_div = BL602_ADC_CLK_DIV_32,
                                       .scan_conv_mode = true,
                                       .continuous_conv_mode = false,
                                       .differential_mode = false,
                                       .resolution = BL602_ADC_RESOLUTION_16B,
                                       .vref = BL602_ADC_VREF_3P2V};

    if(adc == NULL){
        return -QM_EINVAL;
    }

    for (size_t i = 0; i < QM_ARRAY_SIZE(g_adc_handle); i++)
    {
        if(g_adc_handle[i].gpio_pin == adc->pin.adc_pin){
            adc_handle = &g_adc_handle[i];
            break;
        }
    }

    /* Enable ADC clock */

    bl602_adc_enable_clk();

    /* Disable GPIO input and output */

    bl602_adc_gpio_init(adc->pin.adc_pin,
                        BL602_ADC_GPIO_ANALOG | BL602_ADC_GPIO_SMT_EN |
                            BL602_ADC_GPIO_DRV_0);

    /* Config ADC hardware */
    bl602_adc_init(&adc_cfg);

    adc->priv = adc_handle;

    return QM_EOK;
}

int32_t qm_adc_value_get(qm_adc_dev_t *adc, void *output, uint32_t timeout)
{
    qm_utils_time_t time = {0};
    uint32_t value[1] = {0};
    qm_adc_handle_t *adc_handle = NULL;
    struct bl602_adc_result_s result[1] = {0};
    int32_t *output_value = (int32_t *)output;

    if(adc == NULL || adc->priv == NULL){
        return -QM_EINVAL;
    }

    adc_handle = (qm_adc_handle_t *)adc->priv;

    bl602_adc_channel_config(adc_handle->adc_ch, 1);
    bl602_adc_start_conversion();

    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);  

    while(bl602_adc_get_count() < 1)
    {
      if(qm_utils_time_left(&time) <= 0){
          break;
      }
      bl_os_msleep(1);
    }

    /* read raw data */

    value[0] = bl602_adc_read_raw();

    /* Parse adc conversion value into millivolt and actual numerical value. */

    bl602_adc_parse_result(value, result, 1);

    *output_value = result[0].millivolt;


    bl602_adc_stop_conversion();
    return QM_EOK;
}

int32_t qm_adc_deinit(qm_adc_dev_t *adc)
{


    return QM_EOK;
}

#if 0 //暂不支持
int32_t  qm_adc_start(qm_adc_dev_t *adc, void *data, uint32_t size)
{
    return -QM_EINVAL;
}

int32_t qm_adc_stop(qm_adc_dev_t *adc)
{
    return -QM_EINVAL;
}
#endif