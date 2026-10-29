/****************************************************************************
 * vendor/bouffalolab/chip/bl602/bl602_ir.c 
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

#ifdef CONFIG_QM_IR_SUPPORT

#include <nuttx/config.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <nuttx/arch.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/kmalloc.h>
#include <nuttx/rc/lirc_dev.h>
#include <nuttx/wqueue.h>
#include <riscv_arch.h>
#include <unistd.h>

#include "qm_ir.h"

#include "bl602_gpio.h"
#include "bl602_ir.h"
#include "bl602_os_hal.h"
#include "bl602_tim.h"
#include "bl_os_adapter/bl_os_type.h"
#include "hardware/bl602_glb.h"
#include "hardware/bl602_ir.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define BL602_IR_MAX_EVENT_SIZE 512
#define BL602_IR_WORK_PERIOD    SEC2TICK(2)
#undef BL602_IR_USE_BLOCKED_WORK_QUEUE

#ifdef CONFIG_BL602_IR_SWM
#define BL602_IR_RX_PIN           (11)

#define RMT_ITEM_BUFFER_SIZE      100
#define BL602_IR_RECV_BUFFER_SIZE (2 * RMT_ITEM_BUFFER_SIZE)
#else
#define BL602_IR_TX_PIN (11)
#define BL602_IR_RX_PIN (12)
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/**
 * @brief IR TX configuration structure
 *
 * @param tx_mode              TX mode select, use @ref IR_TX_MODE
 * @param data_bits            Bit count of data phase (don't care if tx freerun mode is enabled)
 * @param tail_inverse         Enable or disable signal of tail pulse inverse (don't care if SWM is enabled)
 * @param tail_enable          Enable or disable signal of tail pulse (don't care if SWM is enabled)
 * @param head_inverse         Enable or disable signal of head pulse inverse (don't care if SWM is enabled)
 * @param head_enable          Enable or disable signal of head pulse (don't care if SWM is enabled)
 * @param logic1_inverse       Enable or disable signal of logic 1 pulse inverse (don't care if SWM is enabled)
 * @param logic0_inverse       Enable or disable signal of logic 0 pulse inverse (don't care if SWM is enabled)
 * @param data_enable          Enable or disable signal of data pulse (don't care if SWM is enabled)
 * @param swm_enable           Enable or disable software mode(SWM)
 * @param output_modulation    Enable or disable signal of output modulation
 * @param output_inverse       Enable or disable signal of output inverse,0:output stays at low during idle state,1:stay at high
 * @param freerun_enable       Enable or disable tx freerun mode (don't care if SWM is enabled)
 * @param continue_enable      Disable:idle time between frames = (tailPulseWidth_0+tailPulseWidth_1)*pulseWidthUnit,Enable:no idle time between frames
 * @param fifo_width           IR frame size(also the valid width for each fifo entry), use @ref IR_TX_FIFO_WIDTH
 * @param fifo_threshold       TX FIFO threshold
 * @param logic0_pulse_width_1 Pulse width of logic 0 pulse phase 1 (don't care if SWM is enabled)
 * @param logic0_pulse_width_0 Pulse width of logic 0 pulse phase 0 (don't care if SWM is enabled)
 * @param logic1_pulse_width_1 Pulse width of logic 1 pulse phase 1 (don't care if SWM is enabled)
 * @param logic1_pulse_width_0 Pulse width of logic 1 pulse phase 0 (don't care if SWM is enabled)
 * @param head_pulse_width_1   Pulse width of head pulse phase 1 (don't care if SWM is enabled)
 * @param head_pulse_width_0   Pulse width of head pulse phase 0 (don't care if SWM is enabled)
 * @param tail_pulse_width_1   Pulse width of tail pulse phase 1 (don't care if SWM is enabled)
 * @param tail_pulse_width_0   Pulse width of tail pulse phase 0 (don't care if SWM is enabled)
 * @param modu_width_1         Modulation phase 1 width
 * @param modu_width_0         Modulation phase 0 width
 * @param pulse_width_unit     Pulse width unit
 */

struct bl602_ir_tx_config_s
{
  uint8_t tx_mode;
  uint8_t data_bits;
  uint8_t tail_inverse;
  uint8_t tail_enable;
  uint8_t head_inverse;
  uint8_t head_enable;
  uint8_t logic1_inverse;
  uint8_t logic0_inverse;
  uint8_t data_enable;
  uint8_t swm_enable;
  uint8_t output_modulation;
  uint8_t output_inverse;
  uint8_t freerun_enable;
  uint8_t continue_enable;
  uint8_t fifo_width;
  uint8_t fifo_threshold;
  uint8_t logic0_pulse_width_1;
  uint8_t logic0_pulse_width_0;
  uint8_t logic1_pulse_width_1;
  uint8_t logic1_pulse_width_0;
  uint8_t head_pulse_width_1;
  uint8_t head_pulse_width_0;
  uint8_t tail_pulse_width_1;
  uint8_t tail_pulse_width_0;
  uint8_t modu_width_1;
  uint8_t modu_width_0;
  uint16_t pulse_width_unit;
};

/**
 * @brief IR RX configuration structure
 *
 * @param rx_mode         RX mode select, use @ref IR_RX_MODE
 * @param input_inverse   Enable or disable signal of input inverse
 * @param deglitch_enable Enable or disable signal of rx input de-glitch function
 * @param deglitch_cnt    De-glitch function cycle count
 * @param  data_threshold Pulse width threshold for logic 0/1 detection (don't care if SWM is enabled)
 * @param  end_threshold  Pulse width threshold to trigger end condition
 * @param  fifo_threshold RX FIFO threshold
 */

struct bl602_ir_rx_config_s
{
  uint8_t rx_mode;
  uint8_t input_inverse;
  uint8_t deglitch_enable;
  uint8_t deglitch_cnt;
  uint16_t data_threshold;
  uint16_t end_threshold;
  uint16_t fifo_threshold;
};

/* IR private structure */

struct bl602_ir_dev_s
{
  struct lirc_lowerhalf_s lower;
  struct work_s work;
#ifdef BL602_IR_USE_BLOCKED_WORK_QUEUE
  sem_t sem_isr; /* Interrupt wait semaphore */
#endif
#ifdef CONFIG_BL602_IR_SWM
  uint16_t data[BL602_IR_RECV_BUFFER_SIZE];
  uint8_t len;
  uint8_t rx_pin;
#else
  uint64_t sample;
  uint8_t tx_pin;
  uint8_t rx_pin;
#endif
};

typedef union rmt_item32_u
{
  struct
  {
    uint32_t duration0 : 15;
    uint32_t level0    : 1;
    uint32_t duration1 : 15;
    uint32_t level1    : 1;
  } pulse;
  uint32_t val;
} bl602_ir_sample_t;

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int ir_open(FAR struct lirc_lowerhalf_s *lower);
static void ir_close(FAR struct lirc_lowerhalf_s *lower);
static int ir_s_tx_mask(FAR struct lirc_lowerhalf_s *lower,
                        unsigned int mask);
static int ir_s_tx_carrier(FAR struct lirc_lowerhalf_s *lower,
                           unsigned int carrier);
static int ir_s_tx_duty_cycle(FAR struct lirc_lowerhalf_s *lower,
                              unsigned int duty_cycle);
static int ir_s_rx_carrier_range(FAR struct lirc_lowerhalf_s *lower,
                                 unsigned int min,
                                 unsigned int max);
 int ir_tx_ir(FAR unsigned int *txbuf,
                    unsigned int n);
static int ir_tx_scancode(FAR struct lirc_lowerhalf_s *lower,
                          FAR struct lirc_scancode *txbuf);
static int ir_s_learning_mode(FAR struct lirc_lowerhalf_s *lower, int enable);
static int ir_s_carrier_report(FAR struct lirc_lowerhalf_s *lower,
                               int enable);
static int ir_s_timeout(FAR struct lirc_lowerhalf_s *lower,
                        unsigned int timeout);

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* IR interface operations */

static const struct lirc_ops_s g_ir_ops = {
  .driver_type = LIRC_DRIVER_IR_RAW,
  .open = ir_open,
  .close = ir_close,
  .s_tx_mask = ir_s_tx_mask,
  .s_tx_carrier = ir_s_tx_carrier,
  .s_tx_duty_cycle = ir_s_tx_duty_cycle,
  .s_rx_carrier_range = ir_s_rx_carrier_range,
  .tx_ir = ir_tx_ir,
  .tx_scancode = ir_tx_scancode,
  .s_learning_mode = ir_s_learning_mode,
  .s_carrier_report = ir_s_carrier_report,
  .s_timeout = ir_s_timeout,
};

static struct bl602_ir_dev_s g_ir_dev = {
  .lower.ops = &g_ir_ops,
  .lower.buffer_bytes = BL602_IR_MAX_EVENT_SIZE * sizeof(uint32_t),
  .work.worker = NULL,
#ifndef CONFIG_BL602_IR_SWM
  .sample = 0,
  .tx_pin = BL602_IR_TX_PIN,
  .rx_pin = BL602_IR_RX_PIN,
#else
  .len = 0,
  .rx_pin = BL602_IR_RX_PIN,
#endif
};

static BL_Timer_t g_ir_timer;
static sem_t g_ir_sem;                      /* Mutual exclusion semaphore */
static volatile uint8_t g_ir_timer_irq = 0; /* timer irq count */
static volatile uint8_t g_ir_end_irq = 0;   /* IR rx end irq count */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void bl602_ir_txint_clear()
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;

  /* Clear ir tx interrupt */

  regval = getreg32(reg_base + BL602_IRTX_INT_STS_OFFSET);
  regval |= IRTX_INT_STS_CR_END_CLR;
  putreg32(regval, reg_base + BL602_IRTX_INT_STS_OFFSET);
}

static void bl602_ir_rxint_clear()
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;

  /* Clear ir tx interrupt */

  regval = getreg32(reg_base + BL602_IRRX_INT_STS_OFFSET);
  regval |= IRRX_INT_STS_CR_END_CLR;
  putreg32(regval, reg_base + BL602_IRRX_INT_STS_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_txint_mask
 *
 * Description:
 *   Mask or unmask ir tx interrupt
 *
 * Input Parameters:
 *   int_type - ir tx interrupt type
 *   mask - mask or unmask
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_txint_mask(uint8_t int_type, bool mask)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;
  regval = getreg32(reg_base + BL602_IRTX_INT_STS_OFFSET);
  if (mask)
    {
      regval |= (int_type & 0x7) << 8;
    }
  else
    {
      regval &= ~((int_type & 0x7) << 8);
    }
  putreg32(regval, reg_base + BL602_IRTX_INT_STS_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_rxint_mask
 *
 * Description:
 *   Mask or unmask ir rx interrupt
 *
 * Input Parameters:
 *   int_type - ir rx interrupt type
 *   mask - mask or unmask
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_rxint_mask(uint8_t int_type, bool mask)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;
  regval = getreg32(reg_base + BL602_IRRX_INT_STS_OFFSET);
  if (mask)
    {
      regval |= (int_type & 0x7) << 8;
    }
  else
    {
      regval &= ~((int_type & 0x7) << 8);
    }
  putreg32(regval, reg_base + BL602_IRRX_INT_STS_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_txint_enable
 *
 * Description:
 *   Enable or disable ir tx interrupt.
 *
 * Input Parameters:
 *   enable - enable or disable
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_txint_enable(bool enable)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;
  regval = getreg32(reg_base + BL602_IRTX_INT_STS_OFFSET);
  if (enable)
    {
      regval |= IRTX_INT_STS_CR_END_EN;
    }
  else
    {
      regval &= ~IRTX_INT_STS_CR_END_EN;
    }
  putreg32(regval, reg_base + BL602_IRTX_INT_STS_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_rxint_enable
 *
 * Description:
 *   Enable or disable ir rx interrupt.
 *
 * Input Parameters:
 *   enable - enable or disable
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_rxint_enable(bool enable)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;
  regval = getreg32(reg_base + BL602_IRRX_INT_STS_OFFSET);
  if (enable)
    {
      regval |= IRRX_INT_STS_CR_END_EN;
    }
  else
    {
      regval &= ~IRRX_INT_STS_CR_END_EN;
    }
  putreg32(regval, reg_base + BL602_IRRX_INT_STS_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_tx_enable
 *
 * Description:
 *   Enable or disable ir tx.
 *
 * Input Parameters:
 *   enable - enable or disable
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_tx_enable(bool enable)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;
  regval = getreg32(reg_base + BL602_IRTX_CONFIG_OFFSET);
  if (enable)
    {
      regval |= IRTX_CONFIG_CR_EN;
    }
  else
    {
      regval &= ~IRTX_CONFIG_CR_EN;
    }
  putreg32(regval, reg_base + BL602_IRTX_CONFIG_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_rx_enable
 *
 * Description:
 *   Enable or disable ir rx.
 *
 * Input Parameters:
 *   enable - enable or disable
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_rx_enable(bool enable)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;
  regval = getreg32(reg_base + BL602_IRRX_CONFIG_OFFSET);
  if (enable)
    {
      regval |= IRRX_CONFIG_CR_EN;
    }
  else
    {
      regval &= ~IRRX_CONFIG_CR_EN;
    }
  putreg32(regval, reg_base + BL602_IRRX_CONFIG_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_rx_fifo_cnt_get
 *
 * Description:
 *   Get ir rx fifo cnt
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   FIFO cnt
 *
 ****************************************************************************/

static uint8_t bl602_ir_rx_fifo_cnt_get(void)
{
  uint32_t regval;

  regval = getreg32(BL602_IRRX_SWM_FIFO_CONFIG_0);

  return ((regval & IRRX_SWM_FIFO_CONFIG_0_RX_FIFO_CNT_MASK) >>
          IRRX_SWM_FIFO_CONFIG_0_RX_FIFO_CNT_SHIFT);
}

/****************************************************************************
 * Name: bl602_ir_rx_fifo_rdata_get
 *
 * Description:
 *   Get fifo data
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   data
 *
 ****************************************************************************/

static uint16_t bl602_ir_rx_fifo_rdata_get(void)
{
  uint16_t regval;

  regval = getreg16(BL602_IRRX_SWM_FIFO_RDATA);

  return (regval);
}

/****************************************************************************
 * Name: bl602_gpio_int_clear
 *
 * Description:
 *   Clear gpio interrupt
 *
 * Input Parameters:
 *   pin - GPIO pin
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_gpio_int_clear(uint8_t pin)
{
  uint32_t regval;

  regval = getreg32(BL602_GPIO_INT_CLR1);
  regval |= (1 << pin);
  regval &= ~(1 << pin);
  putreg32(regval, BL602_GPIO_INT_CLR1);
}

/****************************************************************************
 * Name: bl602_gpio_int_mask
 *
 * Description:
 *   Mask or unmask gpio interrupt
 *
 * Input Parameters:
 *   pin - GPIO pin
 *   mask - mask or unmask
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_gpio_int_mask(uint8_t pin, bool mask)
{
  uint32_t reg_base;
  uint32_t regval;

  regval = getreg32(BL602_GPIO_INT_MASK1);
  if (mask)
    {
      regval |= (1 << pin);
    }
  else
    {
      regval &= ~(1 << pin);
    }
  putreg32(regval, BL602_GPIO_INT_MASK1);
}

/****************************************************************************
 * Name: bl602_gpio_int_init
 *
 * Description:
 *   IR GPIO initialize
 *
 * Input Parameters:
 *   pin - GPIO number
 *   mode - GPIO trigger mode
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_gpio_int_init(uint8_t pin, uint8_t mode)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_GLB_BASE;

  /* int mask */
  regval = getreg32(reg_base + BL602_GPIO_INT_MASK1_OFFSET);
  regval |= (1 << pin);
  putreg32(regval, reg_base + BL602_GPIO_INT_MASK1_OFFSET);
  printf("mask : reg:0x%08x val:0x%08x\r\n",
         reg_base + BL602_GPIO_INT_MASK1_OFFSET,
         regval);

  /* int Clear */
  regval = getreg32(reg_base + BL602_GPIO_INT_CLR1_OFFSET);
  regval |= (1 << pin);
  putreg32(regval, reg_base + BL602_GPIO_INT_CLR1_OFFSET);
  regval &= ~(1 << pin);
  putreg32(regval, reg_base + BL602_GPIO_INT_CLR1_OFFSET);
  printf("clear: reg:0x%08x val:0x%08x\r\n",
         reg_base + BL602_GPIO_INT_CLR1_OFFSET,
         regval);

  /* set trigger mode */
  regval =
    getreg32(reg_base + BL602_GPIO_INT_MODE_SET1_OFFSET + ((pin / 10) << 2));
  regval &= ~(0x07 << ((pin % 10) * 3));
  regval |= (mode << ((pin % 10) * 3));
  putreg32(regval,
           reg_base + BL602_GPIO_INT_MODE_SET1_OFFSET + ((pin / 10) << 2));
  printf("int  : reg:0x%08x val:0x%08x\r\n",
         reg_base + BL602_GPIO_INT_MODE_SET1_OFFSET + ((pin / 10) << 2),
         regval);
}

/****************************************************************************
 * Name: bl602_ir_gpio_init
 *
 * Description:
 *   IR GPIO initialize
 *
 * Input Parameters:
 *   pin - GPIO number
 *   cfgset - GPIO config
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_gpio_init(uint8_t pin, uint32_t cfgset)
{
  uint32_t cfg = 0;
  uint32_t reg_base;
  uint32_t cfg_address;
  uint8_t drive;
  uint8_t function;
  uint16_t mode;

  reg_base = BL602_GLB_BASE;

  function = (cfgset & BL602_IR_GPIO_FUNC_MASK) >> BL602_IR_GPIO_FUNC_SHIFT;
  mode = (cfgset & BL602_IR_GPIO_MODE_MASK);
  drive = (cfgset & BL602_IR_GPIO_DRV_MASK) >> BL602_IR_GPIO_DRV_SHIFT;

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

  if (mode & BL602_IR_GPIO_ANALOG)
    {
      regval &= ~(1 << (pin & 0x1f));
      function = 10;
    }
  else if (mode & BL602_IR_GPIO_ALTERNATE)
    {
      cfg |= (1 << (is_odd * 16 + 0));
      regval &= ~(1 << (pin & 0x1f));
    }
  else
    {
      function = 11;

      if (mode & BL602_IR_GPIO_INPUT)
        {
          cfg |= (1 << (is_odd * 16 + 0));
        }

      if (mode & BL602_IR_GPIO_OUTPUT)
        {
          regval |= (1 << (pin & 0x1f));
        }
    }

  putreg32(regval, reg_base + BL602_GPIO_CFGCTL34_OFFSET + ((pin >> 5) << 2));

  if (cfgset & BL602_IR_GPIO_PULLUP)
    {
      cfg |= (1 << (is_odd * 16 + 4));
    }
  else if (cfgset & BL602_IR_GPIO_PULLDOWN)
    {
      cfg |= (1 << (is_odd * 16 + 5));
    }
  else
    {
    }

  if (cfgset & BL602_IR_GPIO_SMT_EN)
    {
      cfg |= (1 << (is_odd * 16 + 1));
    }

  cfg |= (drive << (is_odd * 16 + 2));
  cfg |= (function << (is_odd * 16 + 8));

  putreg32(cfg, cfg_address);
}

/****************************************************************************
 * Name: bl602_ir_txrx_gpio_init
 *
 * Description:
 *   IR TX/RX GPIO initialize
 *
 * Input Parameters:
 *   None
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

void bl602_ir_tx_gpio_init(uint8_t tx_pin, uint8_t rx_pin)
{

  uint32_t regval;

  /*
     * GPIO initialize,
     * IR TX only support GPIO 11,
     * IR RX support GPIO 11 ~ GPIO 13.
     */
  bl602_ir_gpio_init(tx_pin,
                     BL602_IR_GPIO_ANALOG | BL602_IR_GPIO_SMT_EN |
                       BL602_IR_GPIO_DRV_0);

}

/****************************************************************************
 * Name: bl602_ir_send
 *
 * Description:
 *   Send data in NEC/RC5/customize mode.
 *
 * Input Parameters:
 *   data - data buffer to send 
 *   length - length of data buffer
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_send(uint32_t *data, uint32_t length)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;

  /* Clear ir tx interrupt */

  regval = getreg32(reg_base + BL602_IRTX_INT_STS_OFFSET);
  regval |= IRRX_INT_STS_CR_END_CLR;
  putreg32(regval, reg_base + BL602_IRTX_INT_STS_OFFSET);

  putreg32(data[0], reg_base + BL602_IRTX_DATA_WORD0_OFFSET);
  if (length > 1)
    {
      putreg32(data[1], reg_base + BL602_IRTX_DATA_WORD1_OFFSET);
    }

  regval = getreg32(reg_base + BL602_IRTX_CONFIG_OFFSET);
  regval |= IRTX_CONFIG_CR_EN;
  putreg32(regval, reg_base + BL602_IRTX_CONFIG_OFFSET);

  /* Get ir tx interrupt status */

  while (((getreg32(reg_base + BL602_IRTX_INT_STS_OFFSET) & 0x7) &
          IRTX_INT_STS_END_INT) == 0)
    {
      /* Waiting for sending */
    }

  regval &= ~IRTX_CONFIG_CR_EN;
  putreg32(regval, reg_base + BL602_IRTX_CONFIG_OFFSET);

  /* Clear ir tx interrupt */

  regval = getreg32(reg_base + BL602_IRTX_INT_STS_OFFSET);
  regval |= IRRX_INT_STS_CR_END_CLR;
  putreg32(regval, reg_base + BL602_IRTX_INT_STS_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_swm_receive
 *
 * Description:
 *   Receive data in swm mode.
 *
 * Input Parameters:
 *   data - Receive data
 *   length - Receive data length
 *
 * Returned Value:
 *   Bit count of data received
 *
 ****************************************************************************/

static uint8_t bl602_ir_swm_receive(uint16_t *data, uint8_t length)
{
  uint32_t reg_base;
  uint32_t regval;
  uint8_t i = 0;

  reg_base = BL602_IR_BASE;

  /* Clear ir tx interrupt */

  regval = getreg32(reg_base + BL602_IRRX_INT_STS_OFFSET);
  regval |= IRRX_INT_STS_CR_END_CLR;
  putreg32(regval, reg_base + BL602_IRRX_INT_STS_OFFSET);

  /* enable IR RX */

  regval = getreg32(reg_base + BL602_IRRX_CONFIG_OFFSET);
  regval |= IRRX_CONFIG_CR_EN;
  putreg32(regval, reg_base + BL602_IRRX_CONFIG_OFFSET);

  while (((getreg32(BL602_IRRX_INT_STS) & 0x7) & IRRX_INT_STS_END_INT) == 0)
    {
      /* Waiting for receiving */

      if (((getreg32(BL602_IRRX_SWM_FIFO_CONFIG_0) &
            IRRX_SWM_FIFO_CONFIG_0_RX_FIFO_CNT_MASK) >>
           IRRX_SWM_FIFO_CONFIG_0_RX_FIFO_CNT_SHIFT) != 0 &&
          i < length)
        {
          data[i] = getreg32(BL602_IRRX_SWM_FIFO_RDATA);
          ++i;
          continue;
        }
      break;
    }

  data[i] = getreg32(BL602_IRRX_SWM_FIFO_RDATA);

  /* disable IR RX */

  regval = getreg32(reg_base + BL602_IRRX_CONFIG_OFFSET);
  regval &= ~IRRX_CONFIG_CR_EN;
  putreg32(regval, reg_base + BL602_IRRX_CONFIG_OFFSET);

  /* Clear ir tx interrupt */

  regval = getreg32(reg_base + BL602_IRRX_INT_STS_OFFSET);
  regval |= IRRX_INT_STS_CR_END_CLR;
  putreg32(regval, reg_base + BL602_IRRX_INT_STS_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_receive
 *
 * Description:
 *   Receive data in NEC/RC5/customize mode.
 *
 * Input Parameters:
 *   data - Receive data
 *
 * Returned Value:
 *   Bit count of data received
 *
 ****************************************************************************/

static uint8_t bl602_ir_receive(uint64_t *data)
{
  uint32_t reg_base;
  uint32_t regval;

  reg_base = BL602_IR_BASE;

#if 0
    /* Clear ir tx interrupt */
    regval = getreg32(reg_base + BL602_IRRX_INT_STS_OFFSET);
    regval |= IRRX_INT_STS_CR_END_CLR;
    putreg32(regval, reg_base + BL602_IRRX_INT_STS_OFFSET);

    regval = getreg32(reg_base + BL602_IRRX_CONFIG_OFFSET);
    regval |= IRRX_CONFIG_CR_EN;
    putreg32(regval, reg_base + BL602_IRRX_CONFIG_OFFSET);
    while (((getreg32(reg_base +BL602_IRRX_INT_STS_OFFSET) & 0x7) & IRRX_INT_STS_END_INT) == 0) {
        /* Waiting for receiving */
    }

    regval &= ~IRRX_CONFIG_CR_EN;
    putreg32(regval, reg_base + BL602_IRRX_CONFIG_OFFSET);

    /* Clear ir tx interrupt */
    regval = getreg32(reg_base + BL602_IRRX_INT_STS_OFFSET);
    regval |= IRRX_INT_STS_CR_END_CLR;
    putreg32(regval, reg_base + BL602_IRRX_INT_STS_OFFSET);
#endif

  regval = getreg32(reg_base + BL602_IRRX_DATA_COUNT_OFFSET) &
           IRRX_DATA_COUNT_STS_DATA_CNT_MASK;
  if (regval <= 32)
    {
      *data = getreg32(reg_base + BL602_IRRX_DATA_WORD0_OFFSET);
    }
  else
    {
      *data = getreg32(reg_base + BL602_IRRX_DATA_WORD0_OFFSET) |
              (uint64_t)getreg32(reg_base + BL602_IRRX_DATA_WORD1_OFFSET)
                << 32;
    }

  return regval;
}

/****************************************************************************
 * Name: bl602_ir_clk_init
 *
 * Description:
 *   Initialize ir clock.
 *
 * Input Parameters:
 *   div - IR remote clock divider XCLK
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_clk_init(uint8_t div)
{
  uint32_t regval;

  regval = getreg32(BL602_CLK_CFG2);
  regval &= ~CLK_CFG2_IR_CLK_DIV_MASK;
  regval |= (div & 0x3f) << CLK_CFG2_IR_CLK_DIV_SHIFT;
  putreg32(regval, BL602_CLK_CFG2);
}

/****************************************************************************
 * Name: bl602_ir_rx_init
 *
 * Description:
 *   Initialize ir rx.
 *
 * Input Parameters:
 *   config - A pointer to ir rx configure structure
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_rx_init(struct bl602_ir_rx_config_s *config)
{
  uint32_t reg_base;
  uint32_t regval;
  uint32_t ir_clock;
  uint16_t data_threshold, end_threshold;

  ir_clock = 2000000;
  reg_base = BL602_IR_BASE;

  regval = getreg32(reg_base + BL602_IRRX_CONFIG_OFFSET);
  regval &= ~IRRX_CONFIG_CR_MODE_MASK;
  regval |= (config->rx_mode & 0x3) << IRRX_CONFIG_CR_MODE_SHIFT;
  if (config->input_inverse)
    {
      regval |= IRRX_CONFIG_CR_IN_INV;
    }
  else
    {
      regval &= ~IRRX_CONFIG_CR_IN_INV;
    }
  if (!config->deglitch_enable)
    {
      regval &= ~IRRX_CONFIG_CR_DEG_EN;
    }
  else
    {
      regval |= IRRX_CONFIG_CR_DEG_EN;
      regval &= ~IRRX_CONFIG_CR_DEG_CNT_MASK;
      regval |= config->deglitch_cnt << IRRX_CONFIG_CR_DEG_CNT_SHIFT;
    }
  putreg32(regval, reg_base + BL602_IRRX_CONFIG_OFFSET);

  if (config->rx_mode == BL602_IR_RX_NEC)
    {
      data_threshold = (ir_clock / 588 - 1) & 0xffff;
      end_threshold = (ir_clock / 222 - 1) & 0xffff;
    }
  else if (config->rx_mode == BL602_IR_RX_RC5)
    {
      data_threshold = (ir_clock / 750 - 1) & 0xffff;
      end_threshold = (ir_clock / 400 - 1) & 0xffff;
    }
  else
    {
      data_threshold = config->data_threshold;
      end_threshold = config->end_threshold;
    }

  regval = getreg32(reg_base + BL602_IRRX_PW_CONFIG_OFFSET);
  regval = end_threshold << IRRX_PW_CONFIG_CR_END_TH_SHIFT | data_threshold;
  putreg32(regval, reg_base + BL602_IRRX_PW_CONFIG_OFFSET);
}

/****************************************************************************
 * Name: bl602_ir_tx_init
 *
 * Description:
 *   Initialize ir tx.
 *
 * Input Parameters:
 *   config - A pointer to ir tx configure structure
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_tx_init(struct bl602_ir_tx_config_s *config)
{
  uint32_t reg_base;
  uint32_t regval;
  uint32_t ir_clock;
  struct bl602_ir_tx_config_s *tx_config =
    (struct bl602_ir_tx_config_s *)config;

  *(uint32_t *)0x40000224 |= 1 << 31;

  ir_clock = 2000000;

  if (tx_config->tx_mode == BL602_IR_TX_NEC)
    {
      tx_config->data_bits = 32;
      tx_config->tail_inverse = 0;
      tx_config->tail_enable = 1;
      tx_config->head_inverse = 0;
      tx_config->head_enable = 1;
      tx_config->logic1_inverse = 0;
      tx_config->logic0_inverse = 0;
      tx_config->data_enable = 1;
      tx_config->swm_enable = 0;
      tx_config->output_modulation = 1;
      tx_config->output_inverse = 0;
      tx_config->freerun_enable = 0;
      tx_config->continue_enable = 0;
      tx_config->fifo_width = BL602_IR_TX_FIFO_WIDTH_32BIT;
      tx_config->fifo_threshold = 0;
      tx_config->logic0_pulse_width_1 = 0;
      tx_config->logic0_pulse_width_0 = 0;
      tx_config->logic1_pulse_width_1 = 2;
      tx_config->logic1_pulse_width_0 = 0;
      tx_config->head_pulse_width_1 = 7;
      tx_config->head_pulse_width_0 = 15;
      tx_config->tail_pulse_width_1 = 0;
      tx_config->tail_pulse_width_0 = 0;
      tx_config->pulse_width_unit = (ir_clock * 10 / 17777 - 1) & 0xfff;
    }
  else if (tx_config->tx_mode == BL602_IR_TX_RC5)
    {
      tx_config->data_bits = 13;
      tx_config->tail_inverse = 0;
      tx_config->tail_enable = 0;
      tx_config->head_inverse = 1;
      tx_config->head_enable = 1;
      tx_config->logic1_inverse = 1;
      tx_config->logic0_inverse = 0;
      tx_config->data_enable = 1;
      tx_config->swm_enable = 0;
      tx_config->output_modulation = 1;
      tx_config->output_inverse = 0;
      tx_config->freerun_enable = 0;
      tx_config->continue_enable = 0;
      tx_config->fifo_width = BL602_IR_TX_FIFO_WIDTH_32BIT;
      tx_config->fifo_threshold = 0;
      tx_config->logic0_pulse_width_1 = 0;
      tx_config->logic0_pulse_width_0 = 0;
      tx_config->logic1_pulse_width_1 = 0;
      tx_config->logic1_pulse_width_0 = 0;
      tx_config->head_pulse_width_1 = 0;
      tx_config->head_pulse_width_0 = 0;
      tx_config->tail_pulse_width_1 = 0;
      tx_config->tail_pulse_width_0 = 0;
      tx_config->pulse_width_unit = (ir_clock * 10 / 11248 - 1) & 0xfff;
    }
  else if (tx_config->tx_mode == BL602_IR_TX_SWM)
    {
      tx_config->swm_enable = 1;
      tx_config->output_modulation = 1;
      tx_config->output_inverse = 0;
      tx_config->fifo_width = BL602_IR_TX_FIFO_WIDTH_32BIT;
      tx_config->fifo_threshold = 0;
    }

  if (tx_config->tx_mode != BL602_IR_TX_CUSTOMIZE)
    {
      tx_config->modu_width_1 = ((ir_clock / 11310 + 5) / 10 - 1) & 0xff;
      tx_config->modu_width_0 = ((ir_clock / 5655 + 5) / 10 - 1) & 0xff;
    }
  else
    {
      if (tx_config->output_modulation != 0 && tx_config->freerun_enable != 0)
        {
          tx_config->continue_enable = 0;
          if (tx_config->tail_pulse_width_1 < 5)
            {
              tx_config->tail_pulse_width_1 = 5;
            }
          if (tx_config->tail_pulse_width_0 < 5)
            {
              tx_config->tail_pulse_width_0 = 5;
            }
        }
    }

  reg_base = BL602_IR_BASE;
  regval = (tx_config->pulse_width_unit & 0xfff) |
           tx_config->modu_width_1 << 16 | tx_config->modu_width_0 << 24;
  putreg32(regval, reg_base + BL602_IRTX_PULSE_WIDTH_OFFSET);

  regval = getreg32(reg_base + BL602_IRTX_CONFIG_OFFSET);
  regval &=
    ~(IRTX_CONFIG_CR_SWM_EN | IRTX_CONFIG_CR_MOD_EN | IRTX_CONFIG_CR_OUT_INV);

  if (tx_config->swm_enable)
    {
      regval |= IRTX_CONFIG_CR_SWM_EN;
    }
  if (tx_config->output_modulation)
    {
      regval |= IRTX_CONFIG_CR_MOD_EN;
    }
  if (tx_config->output_inverse)
    {
      regval |= IRTX_CONFIG_CR_OUT_INV;
    }
  if (tx_config->tx_mode == BL602_IR_TX_SWM)
    {
      putreg32(regval, reg_base + BL602_IRTX_CONFIG_OFFSET);
      return;
    }

  regval &=
    IRTX_CONFIG_CR_SWM_EN | IRTX_CONFIG_CR_MOD_EN | IRTX_CONFIG_CR_OUT_INV;
  regval |= (tx_config->data_bits - 1) << IRTX_CONFIG_CR_DATA_NUM_SHIFT;

  if (tx_config->tail_inverse)
    {
      regval |= IRTX_CONFIG_CR_TAIL_HL_INV;
    }
  if (tx_config->tail_enable)
    {
      regval |= IRTX_CONFIG_CR_TAIL_EN;
    }
  if (tx_config->head_inverse)
    {
      regval |= IRTX_CONFIG_CR_HEAD_HL_INV;
    }
  if (tx_config->head_enable)
    {
      regval |= IRTX_CONFIG_CR_HEAD_EN;
    }
  if (tx_config->logic1_inverse)
    {
      regval |= IRTX_CONFIG_CR_LOGIC1_HL_INV;
    }
  if (tx_config->logic0_inverse)
    {
      regval |= IRTX_CONFIG_CR_LOGIC0_HL_INV;
    }
  if (tx_config->data_enable)
    {
      regval |= IRTX_CONFIG_CR_DATA_EN;
    }

  putreg32(regval, reg_base + BL602_IRTX_CONFIG_OFFSET);

  regval = (tx_config->tail_pulse_width_1 & 0xf) << 28 |
           (tx_config->tail_pulse_width_0 & 0xf) << 24 |
           (tx_config->head_pulse_width_1 & 0xf) << 20 |
           (tx_config->head_pulse_width_0 & 0xf) << 16 |
           (tx_config->logic1_pulse_width_1 & 0xf) << 12 |
           (tx_config->logic1_pulse_width_0 & 0xf) << 8 |
           (tx_config->logic0_pulse_width_1 & 0xf) << 4 |
           (tx_config->logic0_pulse_width_0 & 0xf);

  putreg32(regval, reg_base + BL602_IRTX_PW_OFFSET);
}

#ifndef CONFIG_BL602_IR_SWM
/****************************************************************************
 * Name: bl602_ir_worker
 *
 * Description:
 *   This is the IR rx worker handler.
 *   Used to read raw data from register.
 *
 * Input Parameters:
 *   arg - Pointer to private data
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_worker(FAR void *arg)
{
  DEBUGASSERT(arg);

  struct bl602_ir_dev_s *dev = arg;
  uint32_t sample;
  uint8_t bit;

#ifdef BL602_IR_USE_BLOCKED_WORK_QUEUE
  int ret;

  /* wait for IR rx interrupt finished */

  ret = nxsem_wait_uninterruptible(&dev->sem_isr);
  if (ret < 0)
    {
      rcerr("Waiting IR rx semaphore error, errno %d.\n", ret);
    }
#endif

  bit = bl602_ir_receive(&dev->sample);
  if (bit > 32)
    {
      rcerr("BL602 IR read raw data is %d bit, but sample data is 32 bit.\n",
            bit);
    }

  sample = (uint32_t)dev->sample;
  if (sample % 2 == 0)
    {
      sample = LIRC_SPACE(sample);
    }
  else
    {
      sample = LIRC_PULSE(sample);
    }

  lirc_sample_event(&dev->lower, sample);

  // printf("BL602 IR read raw data 0x%llx, sample data 0x%08x\n",
  //        dev->sample,
  //        sample);

  dev->sample = 0;

  bl602_ir_rxint_clear();
  bl602_ir_rx_enable(true);

#ifdef BL602_IR_USE_BLOCKED_WORK_QUEUE
  work_queue(LPWORK, &dev->work, bl602_ir_worker, dev, BL602_IR_WORK_PERIOD);
#endif
}

/****************************************************************************
 * Name: bl602_ir_rx_irq
 *
 * Description:
 *   This is the common IR rx interrupt handler.  It will be invoked
 *   when an interrupt received on the device.
 *
 * Input Parameters:
 *   arg - Pointer to private data
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int bl602_ir_rx_irq(int cpuint, void *context, FAR void *arg)
{
  DEBUGASSERT(arg);

  struct bl602_ir_dev_s *dev = arg;

  bl602_ir_rx_enable(false);

  bl602_ir_rxint_clear();

#ifdef BL602_IR_USE_BLOCKED_WORK_QUEUE
  nxsem_post(&dev->sem_isr);
#else
  if (dev->work.worker != NULL)
    {
      work_cancel(LPWORK, &dev->work);
    }

  work_queue(LPWORK, &dev->work, bl602_ir_worker, dev, 0);
#endif /* BL602_IR_USE_BLOCKED_WORK_QUEUE */

  return OK;
}

#else

#define IR_RECV_DEBUG
#ifdef IR_RECV_DEBUG
static volatile uint8_t g_ir_stats[100];
static volatile uint8_t g_ir_idx = 0;
static volatile uint8_t g_ir_gpio_irq = 0;
static volatile uint8_t g_ir_worker_irq = 0;
#endif

#define TIMER_CHANNEL_USE TIMER_CH1
#if (TIMER_CHANNEL_USE == TIMER_CH0)
#define TIMER_IRQ_ID BL602_IRQ_TIMER_CH0
#else
#define TIMER_IRQ_ID BL602_IRQ_TIMER_CH1
#endif

/****************************************************************************
 * Name: bl602_sample_handler_worker
 *
 * Description:
 *   Used to upload ir data from buffer
 *
 * Input Parameters:
 *   arg - Pointer to private data
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static void bl602_sample_handler_worker(FAR void *arg)
{
  DEBUGASSERT(arg);
  struct bl602_ir_dev_s *dev = arg;
  uint16_t *buffer = dev->data;
  bl602_ir_sample_t sample;
  uint8_t i = 0;
  int ret;

#ifdef IR_RECV_DEBUG
  g_ir_worker_irq++;
#endif
  DEBUGASSERT(dev->len <= BL602_IR_RECV_BUFFER_SIZE);

  ret = nxsem_wait(&g_ir_sem);
  if (ret < 0)
    {
      return ret;
    }

#define ledinfo printf
  ledinfo("Receive value: %d \n", dev->len);
  for (i = 0; i < dev->len; i++)
    {
      if (i % 2 == 0)
        {
          sample.pulse.duration1 = (buffer[i] / 2) & 0x7fff;
          sample.pulse.level1 = 1;
        }
      else if (i % 2 == 1)
        {
          sample.pulse.duration0 = (buffer[i] / 2) & 0x7fff;
          sample.pulse.level0 = 0;
          lirc_sample_event(&dev->lower, sample.val);
          ledinfo("[%02d]: %08x %08x, sample:%08x\n",
                  i,
                  buffer[i - 1],
                  buffer[i],
                  sample.val);
        }
    }

  if (i % 2 == 1)
    {
      sample.pulse.duration0 = 0;
      sample.pulse.level0 = 0;
      lirc_sample_event(&dev->lower, sample.val);
      ledinfo("[%02d]: %08x %08x, sample:%08x\n",
              i,
              buffer[i - 1],
              buffer[i],
              sample.val);
    }
  ledinfo("Over!\n");

#ifdef IR_RECV_DEBUG
  for (i = 0; i < g_ir_idx; ++i)
    {
      printf("ir [%02d]: %03d\n", i, g_ir_stats[i]);
    }
  g_ir_idx = 0;
  printf("irq stat: gpio %d timer %d end %d worker %d\n",
         g_ir_gpio_irq,
         g_ir_timer_irq,
         g_ir_end_irq,
         g_ir_worker_irq);
  g_ir_gpio_irq = g_ir_worker_irq = 0;
#endif

  g_ir_timer_irq = g_ir_end_irq = 0;
  nxsem_post(&g_ir_sem);
  bl602_gpio_int_clear(dev->rx_pin);
  bl602_gpio_int_mask(dev->rx_pin, false);
}

/****************************************************************************
 * Name: bl602_gpio_irq_cb
 *
 * Description:
 *   This is the common IR rx gpio interrupt handler.  It will be invoked
 *   when an interrupt received on the device.
 *
 * Input Parameters:
 *   arg - Pointer to private data
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int bl602_gpio_irq_cb(int cpuint, void *context, FAR void *arg)
{
  DEBUGASSERT(arg);
  struct bl602_ir_dev_s *dev = arg;
  uint32_t tmp = 0;

#ifdef IR_RECV_DEBUG
  g_ir_gpio_irq++;
#endif
  bl602_ir_rxint_mask(1, false);
  bl602_gpio_int_mask(dev->rx_pin, true);
  bl602_gpio_int_clear(dev->rx_pin);
  dev->len = 0;
  bl602_timer_clearintstatus(TIMER_CHANNEL_USE, TIMER_COMP_ID_0);
  tmp = bl602_timer_getcountervalue(TIMER_CHANNEL_USE);
  if (tmp >= (0xffffffff - 35))
    {
      tmp = 35 - (0xffffffff - tmp);
    }
  else
    {
      tmp += 35;
    }
  bl602_timer_setcompvalue(TIMER_CHANNEL_USE, TIMER_COMP_ID_0, tmp);
  bl602_timer_enable(TIMER_CHANNEL_USE);

  return OK;
}

/****************************************************************************
 * Name: bl602_ir_timer_cb
 *
 * Description:
 *   Recieve data from fifo
 *
 * Input Parameters:
 *   arg - A pointer to an instance of lirc driver.
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_timer_cb(int cpuint, void *context, FAR void *arg)
{
  DEBUGASSERT(arg);
  struct bl602_ir_dev_s *dev = arg;
  uint16_t *buffer = dev->data;
  uint8_t i = 0;
  uint8_t cnt;

  g_ir_timer_irq++;
  bl602_timer_disable(TIMER_CHANNEL_USE);
  bl602_timer_clearintstatus(TIMER_CHANNEL_USE, TIMER_COMP_ID_0);

  /* read IR RX FIFO cnt */

  cnt = bl602_ir_rx_fifo_cnt_get();

  for (i = 0; i < cnt && i < BL602_IR_RECV_BUFFER_SIZE; ++i)
    {
      buffer[i] = bl602_ir_rx_fifo_rdata_get();
    }

  dev->len += cnt;

  DEBUGASSERT(dev->len <= BL602_IR_RECV_BUFFER_SIZE);

#ifdef IR_RECV_DEBUG
  if (g_ir_idx < 100)
    {
      g_ir_stats[g_ir_idx++] = cnt;
    }
#endif
}

/****************************************************************************
 * Name: bl602_ir_rx_end_irq_cb
 *
 * Description:
 *   IR rx end irq callback. Used to recieve left IR data from fifo.
 *
 * Input Parameters:
 *   arg - A pointer to an instance of lirc driver.
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static int bl602_ir_rx_end_irq_cb(int cpuint, void *context, FAR void *arg)
{
  DEBUGASSERT(arg);
  struct bl602_ir_dev_s *dev = arg;
  uint16_t *buffer = dev->data;
  uint8_t i = 0, j = 0;
  uint8_t cnt;

  g_ir_end_irq++;
  bl602_ir_rx_enable(false);
  bl602_timer_disable(TIMER_CHANNEL_USE);
  bl602_timer_clearintstatus(TIMER_CHANNEL_USE, TIMER_COMP_ID_0);
  bl602_ir_rxint_mask(1, true);

  /* read IR RX FIFO cnt */

  cnt = bl602_ir_rx_fifo_cnt_get();

  for (i = 0, j = dev->len; i < cnt && j < BL602_IR_RECV_BUFFER_SIZE;
       ++i, ++j)
    {
      buffer[j] = bl602_ir_rx_fifo_rdata_get();
    }

  dev->len += cnt;

  DEBUGASSERT(dev->len <= BL602_IR_RECV_BUFFER_SIZE);

#ifdef IR_RECV_DEBUG
  if (g_ir_idx < 100)
    {
      g_ir_stats[g_ir_idx++] = cnt;
    }
#endif

  bl602_ir_rxint_clear();
  bl602_ir_rx_enable(true);

  work_queue(LPWORK, &dev->work, bl602_sample_handler_worker, dev, 0);
}
#endif

/****************************************************************************
 * Name: bl602_ir_rx_int_init
 *
 * Description:
 *   Initialize ir rx interrupt.
 *
 * Input Parameters:
 *   dev - A pointer to an instance of lirc driver.
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

void bl602_ir_rx_int_init(void)
{
//   DEBUGASSERT(dev);

#ifdef CONFIG_BL602_IR_SWM
  up_disable_irq(BL602_IRQ_GPIO_INT0);

  /* setup timer */
  struct timer_cfg_s timer_cfg = {TIMER_CHANNEL_USE,
                                  TIMER_CLKSRC_32K,
                                  TIMER_PRELOAD_TRIG_COMP1,
                                  TIMER_COUNT_PRELOAD,
                                  31,
                                  35,
                                  0xffffffff,
                                  0xffffffff,
                                  0};
  bl602_timer_init(&timer_cfg);
  irq_attach(TIMER_IRQ_ID, bl602_ir_timer_cb, dev);
  up_enable_irq(TIMER_IRQ_ID);
  bl602_timer_intmask(TIMER_CHANNEL_USE, TIMER_INT_COMP_0, false);
  bl602_timer_clearintstatus(TIMER_CHANNEL_USE, TIMER_COMP_ID_0);

  /* setup gpio int */

  bl602_gpio_int_init(dev->rx_pin, GLB_GPIO_INT_TRIG_NEG_PULSE);
  irq_attach(BL602_IRQ_GPIO_INT0, bl602_gpio_irq_cb, dev);
  bl602_gpio_int_mask(dev->rx_pin, false);

  /* IRRX END int */

  irq_attach(BL602_IRQ_IRRX_IRQn, bl602_ir_rx_end_irq_cb, dev);
  up_enable_irq(BL602_IRQ_IRRX_IRQn);
  bl602_ir_rxint_mask(IRRX_INT_STS_END_INT, false);

  up_enable_irq(BL602_IRQ_GPIO_INT0);
#else
  irq_attach(BL602_IRQ_IRRX_IRQn, bl602_ir_rx_irq, NULL);
  up_enable_irq(BL602_IRQ_IRRX_IRQn);
  bl602_ir_rx_enable(true);
  bl602_ir_rxint_mask(IRRX_INT_STS_END_INT, false);
#endif
}

/****************************************************************************
 * Name: bl602_ir_sem_init
 *
 * Description:
 *   Initialize ir semaphore.
 *
 * Input Parameters:
 *   sem - semaphore
 *
 * Returned Value:
 *   None 
 *
 ****************************************************************************/

static void bl602_ir_sem_init(sem_t *sem)
{
  nxsem_init(sem, 0, 1);
  nxsem_set_protocol(sem, SEM_PRIO_NONE);
}

/****************************************************************************
 * Name: ir_open
 *
 * Description:
 *   Used to turn on the infrared device of the device,
 *   corresponding to the application open operation.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

int ir_open(FAR struct lirc_lowerhalf_s *lower)
{
//   DEBUGASSERT(lower);
  struct bl602_ir_dev_s *dev = (struct bl602_ir_dev_s *)lower;

  rcinfo("Called %s\n", __func__);

#ifdef BL602_IR_USE_BLOCKED_WORK_QUEUE
  if (dev->work.worker != NULL)
    {
      rcwarn("IR has been opened\n");
      return OK;
    }
#endif

  // dev->sample = 0;

  /* Enable ir rx */

  bl602_ir_rxint_clear();
  bl602_ir_rx_enable(true);

#ifdef BL602_IR_USE_BLOCKED_WORK_QUEUE
  return work_queue(LPWORK,
                    &dev->work,
                    bl602_ir_worker,
                    dev,
                    BL602_IR_WORK_PERIOD);
#else
  return OK;
#endif
}

/****************************************************************************
 * Name: ir_close
 *
 * Description:
 *   Used to turn off the infrared device of the device,
 *   corresponding to the application close operation.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *
 * Returned Value:
 *   None
 *
 ****************************************************************************/

static void ir_close(FAR struct lirc_lowerhalf_s *lower)
{
  DEBUGASSERT(lower);
  struct bl602_ir_dev_s *dev = (struct bl602_ir_dev_s *)lower;

  rcinfo("Called %s\n", __func__);

#ifdef BL602_IR_USE_BLOCKED_WORK_QUEUE
  if (dev->work.worker == NULL)
    {
      rcwarn("IR closed\n");
      return;
    }
#endif

  /* Disable ir rx */

  bl602_ir_rx_enable(false);
  bl602_ir_rxint_clear();

#ifdef BL602_IR_USE_BLOCKED_WORK_QUEUE
  work_cancel(LPWORK, &dev->work);
#endif
}

/****************************************************************************
 * Name: ir_s_tx_mask
 *
 * Description:
 *   Used to set the transmission channel mask,
 *   only for infrared devices with multiple tx channels,
 *   corresponding to the command: LIRC_SET_TRANSMITTER_MASK.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   mask - channel mask
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int ir_s_tx_mask(FAR struct lirc_lowerhalf_s *lower, unsigned int mask)
{
  DEBUGASSERT(lower);

  rcinfo("Called %s, mask:%u\n", __func__, mask);

  return OK;
}

/****************************************************************************
 * Name: ir_s_tx_carrier
 *
 * Description:
 *   Used to set the transmission carrier frequency,
 *   corresponding to the command: LIRC_SET_SEND_CARRIER.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   carrier - transmission carrier frequency
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int ir_s_tx_carrier(FAR struct lirc_lowerhalf_s *lower,
                           unsigned int carrier)
{
  DEBUGASSERT(lower);

  rcinfo("Called %s, carrier:%u\n", __func__, carrier);

  return OK;
}

/****************************************************************************
 * Name: ir_s_tx_duty_cycle
 *
 * Description:
 *   Used to set the duty cycle,
 *   corresponding to the command: LIRC_SET_SEND_DUTY_CYCLE.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   duty_cycle - duty_cycle, range 0%-100%,
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int ir_s_tx_duty_cycle(FAR struct lirc_lowerhalf_s *lower,
                              unsigned int duty_cycle)
{
  DEBUGASSERT(lower);

  rcinfo("Called %s, duty_cycle:%u\n", __func__, duty_cycle);

  return OK;
}

/****************************************************************************
 * Name: ir_s_rx_carrier_range
 *
 * Description:
 *   Used to tell the driver the carrier frequency range that can be processed,
 *   corresponding to the command: LIRC_SET_REC_CARRIER.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   min - minimum hardware send dalay, in microseconds
 *   max - maximum hardware send dalay, in microseconds
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int ir_s_rx_carrier_range(FAR struct lirc_lowerhalf_s *lower,
                                 unsigned int min,
                                 unsigned int max)
{
  DEBUGASSERT(lower);

  rcinfo("Called %s, min:%u, max:%u\n", __func__, min, max);

  return OK;
}

/****************************************************************************
 * Name: ir_tx_ir
 *
 * Description:
 *   Used to send IR raw data,
 *   Called by the upper layer write method.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   txbuf - is the data area
 *   n - is the data length
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

int ir_tx_ir(FAR unsigned int *txbuf,
                    unsigned int n)
{
//   DEBUGASSERT(lower);

  rcinfo("BL602 IR send raw data: 0x%08x(size:%02d) to device\n", *txbuf, n);

  bl602_ir_send(txbuf, n);

  return OK;
}

/****************************************************************************
 * Name: ir_tx_scancode
 *
 * Description:
 *   Used to send IR data of scancode type,
 *   Called by the upper layer write method.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   txbuf - is the data area
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int ir_tx_scancode(FAR struct lirc_lowerhalf_s *lower,
                          FAR struct lirc_scancode *txbuf)
{
  DEBUGASSERT(lower);

  rcinfo("Dummy RC send scancode data:%" PRIu64 " to device\n",
         txbuf->scancode);

  return OK;
}

/****************************************************************************
 * Name: ir_s_learning_mode
 *
 * Description:
 *   Opens the wide band receiver for learning.
 *   Corresponding command: LIRC_SET_WIDEBAND_RECEIVER.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   enable - enable/disable 
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int ir_s_learning_mode(FAR struct lirc_lowerhalf_s *lower, int enable)
{
  DEBUGASSERT(lower);

  rcinfo("Called %s, enable:%d\n", __func__, enable);

  return OK;
}

/****************************************************************************
 * Name: ir_s_carrier_report
 *
 * Description:
 *   Opens carrier reporting.
 *   Corresponding command: LIRC_SET_MEASURE_CARRIER_MODE.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   enable - enable/disable 
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int ir_s_carrier_report(FAR struct lirc_lowerhalf_s *lower, int enable)
{
  DEBUGASSERT(lower);

  rcinfo("Called %s, enable:%d\n", __func__, enable);

  return OK;
}

/****************************************************************************
 * Name: ir_s_timeout
 *
 * Description:
 *   Used to set the hardware send time delay.
 *   Corresponding command: LIRC_SET_REC_TIMEOUT.
 *
 * Input Parameters:
 *   lower - A pointer to an instance of lower half lirc driver.
 *   timeout - hardware send dalay, in microseconds
 *
 * Returned Value:
 *   errno
 *
 ****************************************************************************/

static int ir_s_timeout(FAR struct lirc_lowerhalf_s *lower,
                        unsigned int timeout)
{
  DEBUGASSERT(lower);

  rcinfo("Called %s, timeout:%u\n", __func__, timeout);

  return OK;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: bl602_ir_initialize
 *
 * Description:
 *   Initialize the IR device.
 *
 * Input Parameters:
 *   devno - The user specifies device number, from 0. If the
 *           devno alerady exists, -EEXIST will be returned.
 *
 * Returned Value:
 *   OK if the driver was successfully register; A negated errno value is
 *   returned on any failure.
 *
 ****************************************************************************/

int bl602_ir_initialize(void)
{
  struct bl602_ir_tx_config_s tx_cfg;
  struct bl602_ir_rx_config_s rx_cfg;
  static uint8_t ref = 0;
  int ret;

  printf("Called %s\n", __func__);

  /* init IR tx/rx GPIO */

  bl602_ir_tx_gpio_init(1, 12);


  tx_cfg.tx_mode = BL602_IR_TX_NEC;
  bl602_ir_tx_init(&tx_cfg);
  bl602_ir_tx_enable(true);


  return ret;
}

#endif
