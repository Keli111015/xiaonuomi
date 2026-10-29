#if 1 //未验证
#include "qm_uart.h"

#include "qm_kernel.h"
#include "qm_errno.h"

#include "qm_log.h"
#include "qm_utils_timer.h"

#include <nuttx/config.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include "qm_ringbuf.h"
#include "poll.h"

#include "hardware/tlsr82_uart.h"
#include "hardware/tlsr82_dma.h"
#include "hardware/tlsr82_clock.h"

#define LOG_TAG "uart_hal"

#define QM_UART1_RECV_MAX_LEN       (256)

typedef struct 
{
    int fd;
    char *dev_name;
    int type;
    void *arg;
}qm_uart_info_t;

typedef struct 
{
    // qm_sem_t sem;
    qm_uart_dev_t *uart;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
    qm_ringbuf_t ringbuf;
    uint8_t ringbuff_data[QM_UART1_RECV_MAX_LEN];
}qm_uart_recv_ctx_t;

static qm_uart_info_t  uart_config[] = {
    {-1, "/dev/ttyS0",    O_RDWR | O_NONBLOCK, NULL},
};

extern void user_uart_recv_handle(uint8_t *recv_buff, uint16_t recv_len);
void uart_recv_thread(uint32_t recv_timeout)
{
    int len = 0;
    int recv_len = 0;
    uint32_t t_left = 0;
    struct pollfd fdp = {0};
    qm_utils_time_t time = {0};
    qm_uart_info_t *uart_info = (qm_uart_info_t *)&uart_config[0];
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;
    uint8_t recv_buff[QM_UART1_RECV_MAX_LEN];

    if (uart_info->arg == NULL)
    {
        //11S 没有task，uart_recv_thread在main中调用
        // qm_msleep(recv_timeout);
        return;
    }

    if(recv_timeout >= 50){
        recv_timeout = 50;
    }

    fdp.events = POLLIN;
    fdp.fd = uart_info->fd;

    t_left = recv_timeout;
    qm_utils_time_countdown_ms(&time, recv_timeout);  

    do{
        recv_len = poll((struct pollfd*)&fdp, 1, t_left);
        if (recv_len < 0) {
            t_left = qm_utils_time_left(&time);
            continue;
        }


        if (fdp.revents == POLLIN){
            recv_len = read(uart_info->fd, recv_buff + len, 64);
            if(recv_len > 0){
                len += recv_len;
            }
        }    
        t_left = qm_utils_time_left(&time);       
    }while ((len < QM_UART1_RECV_MAX_LEN) && (t_left > 0));
    
    if(len > 0){
        qm_ringbuf_push(&recv_ctx->ringbuf, recv_buff, len);
        if(recv_ctx->read){
            recv_ctx->read(recv_ctx->uart, len);
        }
    }
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{

    struct termios tio;
    qm_uart_info_t *uart_info = NULL;
    if(uart == NULL){
        return -QM_EINVAL;
    }

    if(uart->port < 0){
        return -QM_EINVAL;
    }
    
    int ret = 0;
    uart_info = &uart_config[0];//uart->port];

    uart_info->fd = open("/dev/ttyS0", O_RDWR|O_NONBLOCK);
    if (uart_info->fd < 0){
        return -QM_EINVAL;
    }
    uart->priv = (void*)uart_info;

    //init uart recv context
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)qm_malloc(sizeof(qm_uart_recv_ctx_t));
    if (recv_ctx == NULL){
        return -QM_ENOMEM;
    }
    memset(recv_ctx, 0, sizeof(qm_uart_recv_ctx_t));
    qm_ringbuf_init(&recv_ctx->ringbuf, recv_ctx->ringbuff_data, QM_UART1_RECV_MAX_LEN);
    recv_ctx->uart = uart;
    uart_info->arg = (void*)recv_ctx;
    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    int ret = QM_EOK;
    if(uart == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    if(uart->priv == NULL){
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t *)&uart_config[0];
    write(uart_info->fd, data, size);

    return ret;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_left = 0;
    uint8_t isempty = 0;
    qm_utils_time_t time = {0};
    qm_uart_info_t *uart_info = NULL;
    qm_uart_recv_ctx_t *recv_ctx = NULL;
    uint8_t recv_buff[QM_UART1_RECV_MAX_LEN];
    
    if(uart == NULL || data == NULL || expect_size == 0){
        return -QM_EINVAL;
    }

    if(uart->priv == NULL){
        return -QM_EINVAL;
    }

    if(timeout >= (UINT32_MAX / 2)){
        timeout = (UINT32_MAX / 2);
    }

    uart_info = (qm_uart_info_t*)uart->priv;
    recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;
    if(recv_ctx == NULL){
        return -QM_EINVAL;
    }

    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);  

    do{
        t_left = qm_utils_time_left(&time);
        
        isempty = qm_ringbuf_isempty(&recv_ctx->ringbuf);

        if (!isempty)
        {
            len = qm_ringbuf_pop(&recv_ctx->ringbuf, (uint8_t *)data + recv_len, expect_size-recv_len);
            if(len > 0){
                recv_len += len;
            }else{
                ret = 0;
                break;
            }
        }
        else
        {

            //没有数据，直接退出
            ret = 0;
            break;
        }            
    }while ((recv_len < expect_size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret; 

}

//  
// int32_t qm_uart_read_on_ringbuf(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
// {
//     int recv_len = 0;
//     qm_err_t ret = QM_EOK;
//     qm_uart_info_t *uart_info = NULL;
//     qm_uart_recv_ctx_t *recv_ctx = NULL;
    
//     if(uart == NULL || data == NULL || expect_size == 0){
//         return -QM_EINVAL;
//     }

//     if(uart->priv == NULL){
//         return -QM_EINVAL;
//     }

//     uart_info = (qm_uart_info_t*)uart->priv;
//     recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;
//     if(recv_ctx == NULL){
//         return -QM_EINVAL;
//     }

//     recv_len = read(uart_info->fd, (uint8_t *)data, expect_size);
//     if(recv_len > 0)
//     {
        
//         // extern void print_hex(uint8_t *tips, uint8_t *hex_buf, uint8_t hex_buf_len);
//         // print_hex("recv:", data, recv_len);
//     }

//     return (0 != recv_len) ? recv_len : 0; 
// }


int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{    
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;

    // ttyS0 不能关闭，因为console还需要使用
    close(uart_info->fd);

    if (recv_ctx != NULL)
    {
        qm_free(recv_ctx);
    }

    uart_info->fd = -1;
    uart_info->arg = NULL;
    uart->priv = NULL;
    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    qm_uart_recv_ctx_t *recv_ctx = NULL;
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;
    if(recv_ctx == NULL){
        return -QM_EINVAL;
    }

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_ringbuf_flush(&recv_ctx->ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            recv_ctx->read = (qm_uart_read_callback_t)param;
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            recv_ctx->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}

#endif 










#if 0



/****************************************************************************
 * arch/arm/src/tlsr82/tlsr82_serial.c
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

#include <errno.h>
// #include <nuttx/config.h>

// #include <nuttx/arch.h>
// #include <nuttx/irq.h>
// #include <nuttx/compiler.h>
// #include <nuttx/board.h>
// #include <nuttx/semaphore.h>
// #include <nuttx/serial/serial.h>
// #include <arch/board/board.h>

#include <assert.h>
#include <debug.h>
#include <semaphore.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "arm_internal.h"

#include "hardware/tlsr82_gpio.h"
#include "hardware/tlsr82_register.h"
// #include "tlsr82_spi_console.h"
// #include "tlsr82_gpio.h"

#include "hardware/tlsr82_uart.h"
#include "hardware/tlsr82_dma.h"
#include "hardware/tlsr82_clock.h"

#include "board_model_mhcb11l.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define UART_RAMCODE               locate_code(".ram_code")

#define CONSOLE_PORT               0
#define CONSOLE_PRIV               g_uart0priv
#define CONSOLE_DEV                g_uart0_dev

#define UART0_PIN_TX_MUX           BOARD_UART0_TX_MUX
#define UART0_PIN_RX_MUX           BOARD_UART0_RX_MUX

#define UART0_PIN_TX               BOARD_UART0_TX_PIN
#define UART0_PIN_RX               BOARD_UART0_RX_PIN

#define UART0_TX_BUF_SIZE          CONFIG_TLSR82_UART0_TX_BUF_SIZE
#define UART0_RX_BUF_SIZE          CONFIG_TLSR82_UART0_RX_BUF_SIZE

#define UART0_TXDMA_BUF_SIZE       CONFIG_TLSR82_UART0_TXDMA_BUF_SIZE
#define UART0_RXDMA_BUF_SIZE       CONFIG_TLSR82_UART0_RXDMA_BUF_SIZE

#define DMA_HEAD_LEN               4


/****************************************************************************
 * Private Types
 ****************************************************************************/

// typedef struct
// {
//   uint32_t baud;
//   uint32_t port;
//   uint32_t irq;
//   uint32_t txpin;
//   uint32_t rxpin;
//   uint32_t parity;
//   uint32_t stopbits;
//   bool     txdma;
//   bool     rxdma;

// } uart_priv_t;

// /****************************************************************************
//  * Private Function Prototypes
//  ****************************************************************************/

// /* Uart interrupt handler */

// static int UART_RAMCODE tlsr82_interrupt(int irq, void *context, void *arg);


// /* Uart operation function */

// static int  tlsr82_uart_setup(struct uart_dev_s *dev);
// static void tlsr82_uart_shutdown(struct uart_dev_s *dev);
// static int  tlsr82_uart_attach(struct uart_dev_s *dev);
// static void tlsr82_uart_detach(struct uart_dev_s *dev);
// static int  tlsr82_uart_ioctl(struct file *filep, int cmd,
//                               unsigned long arg);
// static void tlsr82_uart_rxint(struct uart_dev_s *dev, bool enable);
// static void tlsr82_uart_txint(uart_dev_t *dev, bool enable);
// static int  UART_RAMCODE tlsr82_uart_receive(struct uart_dev_s *dev,
//                                              unsigned int * status);
// static bool UART_RAMCODE tlsr82_uart_rxavailable(struct uart_dev_s *dev);
// static void UART_RAMCODE tlsr82_uart_send(struct uart_dev_s *dev, int ch);
// static bool UART_RAMCODE tlsr82_uart_txready(struct uart_dev_s *dev);
// static bool UART_RAMCODE tlsr82_uart_txempty(struct uart_dev_s *dev);

// /****************************************************************************
//  * Private Data
//  ****************************************************************************/

static uint8_t uart_rxindex;
static uint8_t uart_txindex;


// static char g_uart0rxbuffer[UART0_RX_BUF_SIZE];
// static char g_uart0txbuffer[UART0_TX_BUF_SIZE];


/* Private UART config and state */

// static uart_priv_t g_uart0priv =
// {
//   .baud       = 115200,
//   .port       = 0,
//   .irq        = NR_UART_IRQ,
//   .parity     = 0,
//   .stopbits   = 0,
//   .txpin      = UART0_PIN_TX,
//   .rxpin      = UART0_PIN_RX,
// #ifdef CONFIG_TLSR82_UART0_TXDMA
//   .txdma      = true,
//   .txdmasem   = &g_uart0txdmasem,
//   .txdmabuf   = g_uart0txdmabuf,
//   .txdmasize  = UART0_TXDMA_BUF_SIZE,
// #else
//   .txdma      = false,
// #endif
// #ifdef CONFIG_TLSR82_UART0_RXDMA
//   .rxdma      = true,
//   .rxdmabuf   = g_uart0rxdmabuf,
//   .rxdmasize  = UART0_RXDMA_BUF_SIZE,
//   .rxdmalen   = 0,
//   .rxdmaindex = 0,
// #else
//   .rxdma      = false,
// #endif
// };

/****************************************************************************
 * Inline Functions
 ****************************************************************************/

/****************************************************************************
 * Name: uart_clr_rx_index
 *
 * Description:
 *   Clear the uart receive software pointer, this function must be called
 *   after wakeup from power-saving mode or reset uart.
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static inline void uart_clr_rx_index(int uart_num)
{
  uart_rxindex = 0;
}

/****************************************************************************
 * Name: uart_clr_tx_index
 *
 * Description:
 *   Clear the uart transimit software pointer, this function must be called
 *   after wakeup from power-saving mode or reset uart.
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static inline void uart_clr_tx_index(int uart_num)
{
  uart_txindex = 0;
}

/****************************************************************************
 * Name: uart_reset
 *
 * Description:
 *   Reset the uart hardware, the software pointer must be set to zero
 *   (function uart_clr_rx_index() must be called).
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static inline void uart_reset(int uart_num)
{
  RESET_RST0_REG |= RESET_RST0_UART;
  RESET_RST0_REG &= ~RESET_RST0_UART;
  uart_clr_rx_index(uart_num);
  uart_clr_tx_index(uart_num);
}

/****************************************************************************
 * Name: uart_get_rxfifo_num
 *
 * Description:
 *   Get the recieved data numbers in the rx fifo.
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *
 * Returned Values:
 *   number
 *
 ****************************************************************************/

static inline uint8_t uart_get_rxfifo_num(int uart_num)
{
  return UART_GET_RX_BUF_CNT();
}

/****************************************************************************
 * Name: uart_get_txfifo_num
 *
 * Description:
 *   Get the transimit data numbers in the tx fifo.
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *
 * Returned Values:
 *   number
 *
 ****************************************************************************/

static inline uint8_t uart_get_txfifo_num(int uart_num)
{
  return UART_GET_TX_BUF_CNT();
}

/****************************************************************************
 * Name: uart_get_irq_status
 *
 * Description:
 *   Get the uart interrupt status
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *   flags     - interrupt bit mask: UART_IRQ_TXDONE
 *                                   UART_IRQ_TXBUF
 *                                   UART_IRQ_RXDONE
 *                                   UART_IRQ_RXBUF
 *
 * Returned Values:
 *   interrupt status: 0, interrupt not occured
 *                     1, interrupt occured
 *
 ****************************************************************************/

static inline uint8_t uart_get_irq_status(int uart_num, uint8_t flags)
{
  return (UART_IRQ_REG & flags);
}

/****************************************************************************
 * Name: uart_irq_clr
 *
 * Description:
 *   Clear the uart interrupt status
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *   flag      - interrupt bit mask: UART_IRQ_CLR_RX
 *                                   UART_IRQ_CLR_TX
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static inline void uart_irq_clr(int uart_num, uint8_t flag)
{
  BM_CLR_IRQ(UART_IRQ_REG, flag);
}

/****************************************************************************
 * Name: uart_irq_rx_enable
 *
 * Description:
 *   Enable/Disable the uart rx interrupt
 *
 * Parameters:
 *   enable  - true : enable the rx interrupt
 *             false: disable the rx interrupt
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static inline void uart_irq_rx_enable(bool enable)
{
  if (enable)
    {
      UART_CTRL0_REG |= UART_CTRL0_RX_IRQ_EN;
    }
  else
    {
      UART_CTRL0_REG &= ~UART_CTRL0_RX_IRQ_EN;
    }
}

/****************************************************************************
 * Name: uart_irq_tx_enable
 *
 * Description:
 *   Enable/Disable the uart tx interrupt
 *
 * Parameters:
 *   enable  - true : enable the tx interrupt
 *             false: disable the tx interrupt
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static inline void uart_irq_tx_enable(bool enable)
{
  if (enable)
    {
      UART_CTRL0_REG |= UART_CTRL0_TX_IRQ_EN;
    }
  else
    {
      UART_CTRL0_REG &= ~UART_CTRL0_TX_IRQ_EN;
    }
}

/****************************************************************************
 * Name: uart_txbufirq_isenable
 *
 * Description:
 *   Check weather the uart tx buffer interrupt is enable or not
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *
 * Returned Values:
 *   interrupt is enable or not: 1, enable
 *                               0, disable
 *
 ****************************************************************************/

static inline int uart_txbufirq_isenable(int uart_num)
{
  return (UART_CTRL0_REG & UART_CTRL0_TX_IRQ_EN);
}

/****************************************************************************
 * Name: uart_rxbufirq_isenable
 *
 * Description:
 *   Check weather the uart rx buffer interrupt is enable or not
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *
 * Returned Values:
 *   interrupt is enable or not: 1, enable
 *                               0, disable
 *
 ****************************************************************************/

static inline int uart_rxbufirq_isenable(int uart_num)
{
  return (UART_CTRL0_REG & UART_CTRL0_RX_IRQ_EN);
}

/****************************************************************************
 * Name: uart_rxbufirq_isenable
 *
 * Description:
 *   Check weather the uart tx done interrupt is enable or not,
 *   uart rx done interrupt is used for uart tx dma mode.
 *
 * Parameters:
 *   port  - the uart hardware index
 *
 * Returned Values:
 *   interrupt is enable or not: 1, enable
 *                               0, disable
 *
 ****************************************************************************/

static inline bool uart_txdoneirq_isenable(int port)
{
  return (UART_RXTIMEOUT1_REG & UART_RXTIMEOUT1_MASK_TXDONE);
}

/****************************************************************************
 * Name: uart_read_byte
 *
 * Description:
 *   Read a byte data from uart received buffer
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *
 * Returned Values:
 *   received char
 *
 ****************************************************************************/

static inline uint8_t uart_read_byte(int uart_num)
{
  uint8_t data = UART_BUF(uart_rxindex);

  uart_rxindex++;

  /* Cycle the four register 0x90 0x91 0x92 0x93 */

  uart_rxindex &= 0x03;
  return data;
}

/****************************************************************************
 * Name: uart_send_byte
 *
 * Description:
 *   Send a char by uart transimit buffer
 *
 * Parameters:
 *   uart_num  - the uart hardware index
 *   byte      - the char want to send
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static void uart_send_byte(uint8_t byte)
{
  irqstate_t flags;

  for (; ; )
    {
      while (UART_GET_TX_BUF_CNT() > 7);

      flags = enter_critical_section();

      if (UART_GET_TX_BUF_CNT() <= 7)
        {
          UART_BUF(uart_txindex) = byte;

          /* Cycle the four register 0x90 0x91 0x92 0x93 */

          uart_txindex++;
          uart_txindex &= 0x03;

          leave_critical_section(flags);
          return;
        }

      leave_critical_section(flags);
    }
}


void send_string(uint8_t *buf, uint16_t buflen)
{
    for(int i = 0; i < buflen; i++)
    {
        uart_send_byte(buf[i]);
    }
}

/****************************************************************************
 * Name: uart_irq_trilevel
 *
 * Description:
 *   Set the receive and transimit interrupt trigger threshold
 *
 * Parameters:
 *   rxlevel  - receive interrupt triiger threshold
 *   txlevel  - transimit interrupt triiger threshold
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static inline void uart_irq_trilevel(uint8_t rxlevel, uint8_t txlevel)
{
  UART_CTRL3_REG = rxlevel | (txlevel << 4);
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: uart_gpio_config
 *
 * Description:
 *   Config tx_pin and rx_pin as uart usage.
 *
 * Parameters:
 *   tx_pin  - uart transimit pin config
 *   rx_pin  - uart receive pin config
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static void uart_gpio_config(uint32_t tx_pin, uint32_t rx_pin)
{
  /* When the pad is configured with mux input and a pull-up resistor is
   * required, tlsr82_gpio_input_ctrl() needs to be placed before
   * tlsr82_gpioconfig, otherwise, the mux pad will may misread the short
   * low-level timing.
   */

  tlsr82_gpio_input_ctrl(tx_pin, true);
  tlsr82_gpio_input_ctrl(rx_pin, true);

  /* The pullup setting must before uart gpio config, cause it will lead
   * to ERR data to uart RX buffer (PM_PIN_PULLUP_1M or PM_PIN_PULLUP_10K),
   * then set Tx pin and RX pin mux function
   */

  tlsr82_gpioconfig(tx_pin | UART0_PIN_TX_MUX | GPIO_PUPD_PU10K);
  tlsr82_gpioconfig(rx_pin | UART0_PIN_RX_MUX | GPIO_PUPD_PU10K);
}

/****************************************************************************
 * Name: uart_baudrate_config
 *
 * Description:
 *   Config the uart baudrate
 *
 * Parameters:
 *   baudrate  - excepted baudrate
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static void uart_baudrate_config(uint32_t baudrate)
{
  int i;
  int j;
  int bwpc = 0;
  int clkdiv;
  int clkdivp1_arr[2];
  int tmp;
  int min;

  /* Caculate the uart clkdiv and bit width
   *     baudrate = CPU_CLK / ((clkdiv + 1) * (bwpc + 1)), 3 <= bwpc <= 15
   */

  min = INT32_MAX;
  for (i = 3; i <= 15; i++)
    {
      /* Consider the round up and round down clkdiv value */

      clkdivp1_arr[0] = CPU_CLK / (baudrate * (i + 1));
      clkdivp1_arr[1] = clkdivp1_arr[0] + 1;
      for (j = 0; j < 2; j++)
        {
          if (clkdivp1_arr[j] == 0)
            {
              continue;
            }

          tmp = CPU_CLK / clkdivp1_arr[j];
          tmp = abs(tmp / (i + 1) - baudrate);

          /* Get the clkdiv and bwpc that make the smallest difference
           * between expected baudrate and real baudrate, mealwhile the
           * clkdiv should be smaller as soon as possible to get a larger
           * uart clock, whick leads more precise baudrate.
           */

          if ((tmp == min && i > bwpc) || (tmp < min))
            {
              min    = tmp;
              bwpc   = i;
              clkdiv = clkdivp1_arr[j] - 1;
            }
        }
    }

  /* Set the bwpc (first clear) */

  BM_CLR(UART_CTRL0_REG, UART_CTRL0_BWPC);
  BM_SET(UART_CTRL0_REG, bwpc << UART_CTRL0_BWPC_SHIFT);

  /* Set the Clock Divider and Enable clock */

  UART_CLK_REG = (clkdiv & UART_CLK_DIV) | UART_CLK_EN;

  /* Set Rx timeout numbers, one byte includes 12 bits at most */

  UART_RXTIMEOUT0_REG = (bwpc + 1) * 12;
  BM_CLR(UART_RXTIMEOUT1_REG, UART_RXTIMEOUT1_SEL);
  BM_SET(UART_RXTIMEOUT1_REG, 2 << UART_RXTIMEOUT1_SEL_SHIFT);
}

/****************************************************************************
 * Name: uart_parity_config
 *
 * Description:
 *   Config the uart parity
 *
 * Parameters:
 *   parity  - 0                  , no parity
 *             odd number         , odd parity
 *             even number (not 0), even parity
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static void uart_parity_config(int parity)
{
  if (parity)
    {
      BM_SET(UART_CTRL1_REG, UART_CTRL1_PARITY_EN);
      if (parity % 2 == 1)
        {
          /* Odd parity */

          BM_CLR(UART_CTRL1_REG, UART_CTRL1_PARITY_SEL);
        }
      else
        {
          /* Even parity */

          BM_SET(UART_CTRL1_REG, UART_CTRL1_PARITY_SEL);
        }
    }
  else
    {
      BM_CLR(UART_CTRL1_REG, UART_CTRL1_PARITY_EN);
    }
}

/****************************************************************************
 * Name: uart_stopbits_config
 *
 * Description:
 *   Config the uart stop bits
 *
 * Parameters:
 *   stopbits  - 0, one stop bit
 *             - 1, one and a halp stop bit
 *             - 2, two stop bits
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static void uart_stopbits_config(int stopbits)
{
  BM_CLR(UART_CTRL1_REG, UART_CTRL1_STOPBIT);
  BM_SET(UART_CTRL1_REG, stopbits << UART_CTRL1_STOPBIT_SHIFT);
}


/****************************************************************************
 * Name: tlsr82_dma_interrupt
 *
 * Description:
 *   This is the UART dma interrupt handler. When the dma transfer finish,
 *   this interrupt will occur. In this interrupt, we should call
 *   uart_recvchars() to receive the char from the dma buffer.
 *
 ****************************************************************************/

#if 1//defined(CONFIG_SERIAL_RXDMA)
static int UART_RAMCODE tlsr82_dma_interrupt(int irq, void *context,
                                             void *arg)
{
  // struct uart_dev_s *dev = (struct uart_dev_s *)arg;
  // uart_priv_t *     priv = (uart_priv_t *)dev->priv;

  /* Check the uart dma rx interrupt status */

  if ((DMA_IRQ_STA_REG & DMA_CHAN_UART_RX) &&
      (DMA_IRQ_MASK_REG & DMA_CHAN_UART_RX))
    {
      /* Write 1 to clear the interrupt status */

      DMA_IRQ_STA_REG |= DMA_CHAN_UART_RX;

      /* The first 4 bytes in dma rx buffer indicates the received
       * data number, the real data start at DMA_HEAD_LEN
       * set rxdmalen be the total valid length (Include DMA HEADER)
       * set rxdmaindex be DMA_HEAD_LEN for skipping the DMA HEADER
       */

      priv->rxdmalen   = priv->rxdmabuf[0] + DMA_HEAD_LEN;
      priv->rxdmaindex = DMA_HEAD_LEN;

      /* Call uart_recvchars */

      uart_recvchars(dev);


    }

  return OK;
}
#endif

/****************************************************************************
 * Name: uart_dma_config
 *
 * Description:
 *   Disable the dma function for uart
 *
 * Parameters:
 *   void
 *
 * Returned Values:
 *   void
 *
 ****************************************************************************/

static void uart_dma_config(void)
{
  /* Disable dma interrupt */

  // up_disable_irq(NR_DMA_IRQ);

  //   {
  //     BM_CLR(UART_CTRL0_REG, UART_CTRL0_TX_DMA_EN);
  //     BM_CLR(DMA_IRQ_EN_REG, DMA_CHAN_UART_TX);
  //   }


  //   {
  //     BM_CLR(UART_CTRL0_REG, UART_CTRL0_RX_DMA_EN);
  //     BM_CLR(DMA_IRQ_EN_REG, DMA_CHAN_UART_RX);
  //   }

  // /* Disable uart dma interrupt, interrupt will be enabled when attach
  //  * the interrupt.
  //  */

  // BM_CLR(DMA_IRQ_MASK_REG, DMA_CHAN_UART_RX | DMA_CHAN_UART_TX);






   /* Disable dma interrupt */

  up_disable_irq(NR_DMA_IRQ);

#ifdef 0//CONFIG_SERIAL_TXDMA
  if (priv->txdma)
    {
      /* Enable the dma function in uart */

      BM_SET(UART_CTRL0_REG, UART_CTRL0_TX_DMA_EN);

      /* Enable the uart tx dma channel in dma */

      BM_SET(DMA_IRQ_EN_REG, DMA_CHAN_UART_TX);
    }
  else
#endif
    {
      BM_CLR(UART_CTRL0_REG, UART_CTRL0_TX_DMA_EN);
      BM_CLR(DMA_IRQ_EN_REG, DMA_CHAN_UART_TX);
    }

#ifdef 1//CONFIG_SERIAL_RXDMA
  if (priv->rxdma)
    {
      /* Enable the dma function in uart */

      BM_SET(UART_CTRL0_REG, UART_CTRL0_RX_DMA_EN);

      /* Enable the uart rx dma channel in dma */

      BM_SET(DMA_IRQ_EN_REG, DMA_CHAN_UART_RX);

      /* Set the rx dma buffer, this configuration must be earlier than
       * uart configuration, such as BaudRate, Parity etc.
       */

      DMA_UART_RX_ADDR_REG =
        (uint16_t)((uint32_t)priv->rxdmabuf & 0x0000ffff);
      DMA_UART_RX_ADDRHI_REG =
        (uint8_t)(((uint32_t)priv->rxdmabuf >> 16) & 0x000000ff);
      DMA_UART_RX_SIZE_REG = priv->rxdmasize / 16;

      /* Set dma mode to 0x01 for receive */

      DMA_UART_RX_MODE_REG |= DMA_MODE_WR_MEM;
    }
  else
#endif
    {
      BM_CLR(UART_CTRL0_REG, UART_CTRL0_RX_DMA_EN);
      BM_CLR(DMA_IRQ_EN_REG, DMA_CHAN_UART_RX);
    }

  /* Disable uart dma interrupt, interrupt will be enabled when attach
   * the interrupt.
   */

  BM_CLR(DMA_IRQ_MASK_REG, DMA_CHAN_UART_RX | DMA_CHAN_UART_TX);
}

/****************************************************************************
 * Name: tlsr82_uart_setup
 *
 * Description:
 *   Configure the UART baud, bits, parity, etc. This method is called the
 *   first time that the serial port is opened.
 *
 ****************************************************************************/

static int tlsr82_uart_setup(uint32_t port, uint32_t tx_pin, uint32_t rx_pin, uint32_t baud)
{
  uart_dma_config();

  /* Uart Gpio config */

  uart_gpio_config(tx_pin, rx_pin);
  uart_reset(port);

  uart_baudrate_config(baud);
  uart_parity_config(0);
  uart_stopbits_config(0);

  /* Diable uart rx_buff and tx_buff irq */

  uart_irq_rx_enable(true);
  uart_irq_tx_enable(false);

  uart_irq_trilevel(1, 0);
  return 0;
}


/****************************************************************************
 * Name: tlsr82_uart_interrupt
 *
 * Description:
 *   This is the UART status interrupt handler.  It will be invoked when an
 *   interrupt received on the 'irq'  It should call uart_transmitchars or
 *   uart_receivechar to perform the appropriate data transfers.  The
 *   interrupt handling logic must be able to map the 'irq' number into the
 *   appropriate uart_dev_s structure in order to call these functions.
 *
 ****************************************************************************/
static int UART_RAMCODE tlsr82_uart_receive(uint32_t port,
                                            unsigned int *status);
static void UART_RAMCODE tlsr82_uart_send(int ch);
static int UART_RAMCODE tlsr82_interrupt(int irq, FAR void *context, FAR void *arg)
{
    uint32_t status; 
  if ((UART_BUF_CNT1_REG & UART_BUF_CNT1_RX_ERR))
    {
      /* It will clear rx_fifo and rx_err_irq, rx_buff_irq, so it won't enter
       * rx_buff interrupt.
       */

      uart_irq_clr(0, UART_IRQ_CLR_RX);

      /* uart_reset() clear hardware and software fifo index */

      uart_reset(0);
    }


//   if (uart_get_irq_status(0, UART_IRQ_TXBUF) &&
//       uart_txbufirq_isenable(0))
//     {
//     //   uart_xmitchars(dev);
//     }


  if (uart_get_irq_status(0, UART_IRQ_RXBUF) &&
      uart_rxbufirq_isenable(0))
    {
    //   uart_recvchars(dev);
        tlsr82_uart_send(tlsr82_uart_receive(0, &status));
    }

  return OK;
}

/****************************************************************************
 * Name: tlsr82_uart_attach
 *
 * Description:
 *   Configure the UART to operation in interrupt driven mode.  This method
 *   is called when the serial port is opened.  Normally, this is just after
 *   the the setup() method is called, however, the serial console may
 *   operate in a non-interrupt driven mode during the boot phase.
 *
 *   RX and TX interrupts are not enabled when by the attach method (unless
 *   the hardware supports multiple levels of interrupt enabling).
 *   The RX and TX interrupts are not enabled until the txint() and rxint()
 *   methods are called.
 *
 ****************************************************************************/
// #include <sys/printk.h>
static int tlsr82_uart_attach(uint32_t irq, uint32_t port)
{
    int ret;

    ret = irq_attach(irq, tlsr82_interrupt, NULL);
    if (ret == OK)
    {
        // printk("tlsr82_uart_attach ok!\r\n");
        uart_irq_clr(port, UART_IRQ_CLR_RX);
        uart_clr_rx_index(port);

        uart_irq_rx_enable(true);

        up_enable_irq(NR_UART_IRQ);
    }

    return ret;
}

/****************************************************************************
 * Name: tlsr82_uart_detach
 *
 * Description:
 *   Detach UART interrupts.  This method is called when the serial port is
 *   closed normally just before the shutdown method is called.
 *   The exception is the serial console which is never shutdown.
 *
 ****************************************************************************/

static void tlsr82_uart_detach(uint32_t irq)
{
  up_disable_irq(irq);

  /* Detach from the interrupt(s) */

  irq_detach(irq);
}


/****************************************************************************
 * Name: tlsr82_uart_receive
 *
 * Description:
 *   Called (usually) from the interrupt level to receive one
 *   character from the UART.  Error bits associated with the
 *   receipt are provided in the return 'status'.
 *
 ****************************************************************************/

static int UART_RAMCODE tlsr82_uart_receive(uint32_t port,
                                            unsigned int *status)
{
  if (status)
    {
      *status = 0x00;
    }


  return uart_read_byte(port);
}

/****************************************************************************
 * Name: tlsr82_uart_rxint
 *
 * Description:
 *   Call to enable or disable RX interrupts
 *
 ****************************************************************************/

static void tlsr82_uart_rxint(bool enable)
{
  uart_irq_rx_enable(enable);
}

/****************************************************************************
 * Name: tlsr82_uart_rxavailable
 *
 * Description:
 *   Return true if the receive register is not empty
 *
 ****************************************************************************/

static bool UART_RAMCODE tlsr82_uart_rxavailable(uint32_t port)
{
  return 0 != uart_get_rxfifo_num(port);
}

/****************************************************************************
 * Name: tlsr82_txint
 *
 * Description:
 *   Call to enable or disable TX interrupts
 *
 ****************************************************************************/

static void tlsr82_uart_txint(bool enable)
{
  uart_irq_tx_enable(enable);
}


/****************************************************************************
 * Name: tlsr82_uart_send
 *
 * Description:
 *   This method will send one byte on the UART.
 *
 ****************************************************************************/

static void UART_RAMCODE tlsr82_uart_send(int ch)
{
  uart_send_byte((uint8_t)ch);
}

/****************************************************************************
 * Name: tlsr82_txready
 *
 * Description:
 *   Return true if the tranmsit data register is empty
 *
 ****************************************************************************/

static bool UART_RAMCODE tlsr82_uart_txready(uint32_t port)
{

  return uart_get_txfifo_num(port) < 8;
}

/****************************************************************************
 * Name: tlsr82_txempty
 *
 * Description:
 *   Return true if the transmit data register is empty
 *
 ****************************************************************************/

static bool UART_RAMCODE tlsr82_uart_txempty(uint32_t port)
{

  return 0 == uart_get_txfifo_num(port);
}

void my_user_uart_init(uint32_t port, uint32_t baud)
{
    tlsr82_uart_setup(port, UART0_PIN_TX, UART0_PIN_RX, baud);
    tlsr82_uart_attach(NR_UART_IRQ, port);
}
#endif