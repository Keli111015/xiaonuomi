/****************************************************************************
 * arch/risc-v/src/bl602/bl602_pwm_lowerhalf.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/
#include "qm.h"

#if 1

#include <nuttx/config.h>

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include <debug.h>

#include <arch/board/board.h>

#include "riscv_arch.h"
#include "riscv_internal.h"

#include "bl602_gpio.h"
#include "bl602_pwm_lowerhalf.h"
#include "hardware/bl602_pwm.h"

#include "qm_pwm.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL_PWM_FREQ_DEFAULT       (1000)
#define BL_PWM_STOP_TIMEOUT_COUNT (160*1000)

#define BL_PWM_XTAL_CLK           (40000000)
#define BL_PWM_BUS_BCLK           (80000000)

#ifdef CONFIG_PWM_NCHANNELS
#  if (CONFIG_PWM_NCHANNELS > 5)
#    error "The maximum number of BL602 PWM channels is 5!"
#  endif
#endif

#ifdef CONFIG_BL602_PWM0_BUS_BCLK_SRC
#define BL_PWM_CLK BL_PWM_BUS_BCLK
#else
#define BL_PWM_CLK BL_PWM_XTAL_CLK
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct bl602_pwm_ch_cfgs
{
    int ch;                                    /* PWM channel */
    int clk;                                   /* PWM Clock */
    int stop_mode;                             /* PWM Stop Mode */
    int pol;                                   /* PWM mode type */
    uint16_t clk_div;                          /* PWM clkDiv num */
    uint16_t period;                           /* PWM period set */
    uint16_t threshold1;                       /* PWM threshold1 num */
    uint16_t threshold2;                       /* PWM threshold2 num */
    uint16_t int_pulse_cnt;                    /* PWM interrupt pulse count */
};

static void pwm_channel_disable(uint8_t ch)
{
  modifyreg32(BL602_PWM_N_CONFIG(ch), 0, CONFIG_PWM_STOP_EN);
  modifyreg32(BL602_PWM_N_CONFIG(ch), INTERRUPT_PWM_INT_ENABLE, 0);
}

static void pwm_channel_enable(uint8_t ch)
{
  modifyreg32(BL602_PWM_N_CONFIG(ch), CONFIG_PWM_STOP_EN, 0);
}

static void pwm_channel_init(uint8_t ch, struct bl602_pwm_ch_cfgs *ch_cfg)
{
  uint32_t timeout_cnt = BL_PWM_STOP_TIMEOUT_COUNT;

  /* Config pwm clock and polarity */

  modifyreg32(BL602_PWM_N_CONFIG(ch), 0, CONFIG_PWM_STOP_EN);

  while (!(getreg32(BL602_PWM_N_CONFIG(ch)) & CONFIG_PWM_STS_TOP))
    {
      timeout_cnt--;
      if (timeout_cnt == 0)
        {
          return;
        }
    }

  modifyreg32(BL602_PWM_N_CONFIG(ch),
              CONFIG_REG_CLK_SEL_MASK & (~ch_cfg->clk),
              CONFIG_REG_CLK_SEL_MASK & ch_cfg->clk);

  modifyreg32(BL602_PWM_N_CONFIG(ch),
              CONFIG_PWM_OUT_INV,
              ch_cfg->pol ? CONFIG_PWM_OUT_INV : 0);

  modifyreg32(BL602_PWM_N_CONFIG(ch),
              CONFIG_PWM_STOP_MODE,
              ch_cfg->stop_mode ? CONFIG_PWM_STOP_MODE : 0);

  /* Config pwm division */

  putreg32(ch_cfg->clk_div, BL602_PWM_N_CLKDIV(ch));

  /* Config pwm period and duty */

  putreg32(ch_cfg->threshold1, BL602_PWM_N_THRE1(ch));
  putreg32(ch_cfg->threshold2, BL602_PWM_N_THRE2(ch));
  putreg32(ch_cfg->period, BL602_PWM_N_PERIOD(ch));

  /* Config interrupt pulse count */

  modifyreg32(BL602_PWM_N_INTERRUPT(ch), 0,
    (ch_cfg->int_pulse_cnt & PERIOD_PWM_PERIOD_MASK));
}

static int32_t pwm_init(uint8_t id, uint32_t freq)
{
  struct bl602_pwm_ch_cfgs pwm_cfg =
    {
      .ch = BL602_PWM_CH0,
#ifdef CONFIG_BL602_PWM0_BUS_BCLK_SRC
      .clk = BL602_PWM_CLK_BCLK,
#else
      .clk = BL602_PWM_CLK_XCLK,
#endif
      .stop_mode = BL602_PWM_STOP_ABRUPT,
      .pol = BL602_PWM_POL_NORMAL,
      .clk_div = 0,
      .period = 100,
      .threshold1 = 0,
      .threshold2 = 0,
      .int_pulse_cnt = 0,
    };

  pwm_cfg.period = BL_PWM_CLK / freq;
  pwm_cfg.ch = id;

  pwm_channel_disable(id);
  pwm_channel_init(id, &pwm_cfg);

  return 0;
}

static inline uint16_t pwm_cal_clk_div(uint32_t target_freq)
{
#define PWM_MAX_PERIOD    (0xFFFF)
  uint32_t period;
  uint16_t divisor = 1;

  /* Calculate the period */

  period = BL_PWM_CLK / divisor / target_freq;

  while (period > PWM_MAX_PERIOD)
    {
      divisor <<= 1;
      period = BL_PWM_CLK / divisor / target_freq;
    }

  return divisor;
}

/****************************************************************************
 * Name: bl602_pwm_param_change
 *
 * Description:
 *   Configure PWM frequency
 *
 ****************************************************************************/
static int bl602_pwm_param_change( uint8_t chan,
                          uint32_t freq, uint32_t duty)
{

  uint16_t period;
  uint16_t threshold1;
  uint16_t threshold2;

  period = BL_PWM_CLK / freq;
  threshold1 = 0;
  threshold2 = period * duty / 65535.0;

  // putreg32(period, BL602_PWM_N_PERIOD(chan));
  putreg32(threshold1, BL602_PWM_N_THRE1(chan));
  putreg32(threshold2, BL602_PWM_N_THRE2(chan));

  return 0;
}


/****************************************************************************
 * Name: bl602_pwm_duty
 *
 * Description:
 *   Configure PWM duty
 *
 ****************************************************************************/

static int bl602_pwm_duty(uint8_t chan, ub16_t duty)
{
  uint16_t period;
  uint16_t threshold1;
  uint16_t threshold2;
  uint32_t tmp_val;

  /* get pwm period and duty */

  tmp_val = getreg32(BL602_PWM_N_THRE1(chan));
  threshold1 = tmp_val & THRE1_PWM_THRE1_MASK;

  tmp_val = getreg32(BL602_PWM_N_THRE2(chan));
  threshold2 = tmp_val & THRE2_PWM_THRE2_MASK;

  tmp_val = getreg32(BL602_PWM_N_PERIOD(chan));
  period = tmp_val & PERIOD_PWM_PERIOD_MASK;

  threshold1 = 0;
  threshold2 = (uint16_t)((uint32_t)period * duty / 65535.0);

  pwminfo("duty=%d, period=%d, threshold1=%d, threshold2=%d\n",
          duty, period, threshold1, threshold2);

  putreg32(threshold1, BL602_PWM_N_THRE1(chan));
  putreg32(threshold2, BL602_PWM_N_THRE2(chan));

  return OK;
}

/****************************************************************************
 * Name: bl602_pwm_freq
 *
 * Description:
 *   Configure PWM frequency
 *
 ****************************************************************************/

static int bl602_pwm_freq(uint8_t chan, uint32_t freq)
{
  uint16_t clk_div = pwm_cal_clk_div(freq);
  uint16_t period = BL_PWM_CLK / (clk_div) / freq;
  uint16_t threshold1 = 0;
  uint16_t threshold2 = 0;

  pwm_channel_disable(chan);

  /* Config pwm division and period */

  putreg32(clk_div, BL602_PWM_N_CLKDIV(chan));
  putreg32(period, BL602_PWM_N_PERIOD(chan));

  /* Config pwm duty */

  putreg32(threshold1, BL602_PWM_N_THRE1(chan));
  putreg32(threshold2, BL602_PWM_N_THRE2(chan));

  return OK;
}

typedef enum 
{
	QM_PWM_CHANNEL_TYPE_0 = 0,
	QM_PWM_CHANNEL_TYPE_1 = 1,
	QM_PWM_CHANNEL_TYPE_2 = 2,
	QM_PWM_CHANNEL_TYPE_3 = 3,
	QM_PWM_CHANNEL_TYPE_4 = 4,
}qm_pwm_channel_type_t;

typedef struct 
{
	uint8_t pwm_ch;
	uint8_t pwm_pin;
}qm_pwm_info_t;

static const qm_pwm_info_t g_pwm_info[] = 
{
	{QM_PWM_CHANNEL_TYPE_0, GPIO_PIN0},
	{QM_PWM_CHANNEL_TYPE_0, GPIO_PIN5},
	{QM_PWM_CHANNEL_TYPE_0, GPIO_PIN20},
	{QM_PWM_CHANNEL_TYPE_1, GPIO_PIN1},
	{QM_PWM_CHANNEL_TYPE_1, GPIO_PIN11},
	{QM_PWM_CHANNEL_TYPE_1, GPIO_PIN21},
	{QM_PWM_CHANNEL_TYPE_2, GPIO_PIN2},
	{QM_PWM_CHANNEL_TYPE_2, GPIO_PIN12},
	{QM_PWM_CHANNEL_TYPE_2, GPIO_PIN17},
	{QM_PWM_CHANNEL_TYPE_2, GPIO_PIN22},
	{QM_PWM_CHANNEL_TYPE_3, GPIO_PIN3},
	{QM_PWM_CHANNEL_TYPE_3, GPIO_PIN8},
	{QM_PWM_CHANNEL_TYPE_4, GPIO_PIN4},
	{QM_PWM_CHANNEL_TYPE_4, GPIO_PIN14},
};

static const qm_pwm_info_t *bl602_pwm_info_get(uint8_t pwm_pin)
{
	const qm_pwm_info_t *pwm_info = NULL;
	for(int index = 0; index < QM_ARRAY_SIZE(g_pwm_info); index++){
		if((pwm_pin << GPIO_PIN_SHIFT) == g_pwm_info[index].pwm_pin){
			pwm_info = &g_pwm_info[index];
			break;
		}
	}
	return pwm_info;
}

int32_t qm_pwm_init(qm_pwm_dev_t *pwm)
{
    uint16_t pin = 0;
    const qm_pwm_info_t *pwm_info = NULL;
      
    if(pwm == NULL){
          return -QM_EINVAL;
      }

    pin = (GPIO_OUTPUT | GPIO_PULLDOWN | GPIO_FUNC_PWM | (pwm->pin.pwm_pin << GPIO_PIN_SHIFT));

    pwm_info = bl602_pwm_info_get(pwm->pin.pwm_pin);
    if(pwm_info == NULL){
      return -QM_EINVAL;
    }

    bl602_configgpio(pin);
    pwm_init(pwm_info->pwm_ch, pwm->config.freq);

    pwm->priv = (void *)pwm_info;
    return QM_EOK;
}


int32_t qm_pwm_start(qm_pwm_dev_t *pwm)
{
	  const qm_pwm_info_t *pwm_info = NULL;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

	  pwm_info = (const qm_pwm_info_t *)pwm->priv;
    
    bl602_pwm_freq(pwm_info->pwm_ch, pwm->config.freq);
    bl602_pwm_duty(pwm_info->pwm_ch, pwm->config.pulse);

    pwm_channel_enable(pwm_info->pwm_ch);
    return QM_EOK;
}



int32_t qm_pwm_para_change(qm_pwm_dev_t *pwm, qm_pwm_config_t para)
{
    const qm_pwm_info_t *pwm_info = NULL;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

    pwm_info = (const qm_pwm_info_t *)pwm->priv;

    pwm->config.freq = para.freq;
    pwm->config.pulse = para.pulse;
    bl602_pwm_duty(pwm_info->pwm_ch, para.pulse);
    return QM_EOK;
}

int32_t qm_pwm_stop(qm_pwm_dev_t *pwm)
{
	const qm_pwm_info_t *pwm_info = NULL;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

	  pwm_info = (const qm_pwm_info_t *)pwm->priv;
    pwm_channel_disable(pwm_info->pwm_ch);
    return QM_EOK;
}

int32_t qm_pwm_deinit(qm_pwm_dev_t *pwm)
{
	  const qm_pwm_info_t *pwm_info = NULL;

    if(pwm == NULL || pwm->priv == NULL){
        return -QM_EINVAL;
    }

	  pwm_info = (const qm_pwm_info_t *)pwm->priv;

    pwm_channel_disable(pwm_info->pwm_ch);
    bl602_gpio_deinit((pwm->pin.pwm_pin << GPIO_PIN_SHIFT));
    
	  pwm->priv = NULL;

	  return QM_EOK;
}
#endif
