#include "qm.h"
#ifdef CONFIG_QM_UART_SUPPORT
#include "qm_platform.h"

#include "qm_uart.h"


#include "qm_ringbuf.h"
#include "qm_utils_timer.h"

#include <nuttx/irq.h>
#include <nuttx/serial/serial.h>
#include <arch/irq.h>
#include <arch/board/board.h>

#include "hardware/bl602_hbn.h"
#include "hardware/bl602_uart.h"
#include "bl602_lowputc.h"
#include "bl602_gpio.h"
#include "bl602_hbn.h"

#include "riscv_arch.h"
#include "riscv_internal.h"

#define LOG_TAG "uart"

#define _BL602_UART_CLOCK (160 * 1000 * 1000UL) /* UART clock */

#define UART_HANDLER_RECV_LEN       (128U)

typedef struct{
    uint8_t index;
    const uint32_t NVIC_IRQChannel;
}uart_config_t;

typedef struct 
{
    uart_config_t config;
    uint16_t recv_size;
    qm_sem_t rx_sem;
    qm_ringbuf_t ringbuf;
    qm_uart_dev_t *uart;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
    uint8_t buf[UART_HANDLER_RECV_LEN];
}qm_uart_info_t;

static qm_uart_info_t uart_config[QM_UART_MAX] = {
    {{0, BL602_IRQ_UART0}, 0, {0}, {0}, NULL, NULL, NULL, {0}},
    {{1, BL602_IRQ_UART1}, 0, {0}, {0}, NULL, NULL, NULL, {0}},
};

static void uart_data_push(qm_uart_info_t *uart_info)
{
    uint8_t data = 0;

    while(((getreg32(BL602_UART_FIFO_CONFIG_1(uart_info->config.index)) & \
            UART_FIFO_CONFIG_1_RX_CNT_MASK) != 0)){

        if(getreg32(BL602_UART_FIFO_CONFIG_1(uart_info->config.index)) & \
            UART_FIFO_CONFIG_1_RX_CNT_MASK){
            data = getreg32(BL602_UART_FIFO_RDATA(uart_info->config.index)) & \
                UART_FIFO_RDATA_MASK;
        }
        uart_info->recv_size += qm_ringbuf_push(&uart_info->ringbuf, &data, 1);
        if(uart_info->recv_size >= (UART_HANDLER_RECV_LEN / 2)){ //保证数据完整存入

            qm_sem_signal(&uart_info->rx_sem);

            if(uart_info->read){
                uart_info->read(uart_info->uart, uart_info->recv_size);
            }
        }
    }
}


static int __uart_interrupt(int irq, FAR void *context, FAR void *arg)
{
    uint32_t int_mask;
    uint32_t int_status;
    qm_uart_info_t *uart_info = (qm_uart_info_t *)arg;

    int_status = getreg32(BL602_UART_INT_STS(uart_info->config.index));
    int_mask = getreg32(BL602_UART_INT_MASK(uart_info->config.index));

    /* Length of uart rx data transfer arrived interrupt */

    if ((int_status & UART_INT_STS_URX_RTO_INT) &&
        !(int_mask & UART_INT_MASK_CR_URX_RTO_MASK)){
            putreg32(UART_INT_CLEAR_CR_URX_RTO_CLR,
                    BL602_UART_INT_CLEAR(uart_info->config.index));

        /* Receive Data ready */
        qm_sem_signal(&uart_info->rx_sem);

        if(uart_info->read && uart_info->recv_size){
            uart_info->read(uart_info->uart, uart_info->recv_size);
        }
    }

    if ((int_status & UART_INT_STS_URX_END_INT) &&
        !(int_mask & UART_INT_MASK_CR_URX_END_MASK)){
            putreg32(UART_INT_CLEAR_CR_URX_END_CLR,
                BL602_UART_INT_CLEAR(uart_info->config.index));
        uart_data_push(uart_info);
    }
    /* Rx fifo ready interrupt,auto-cleared when data is popped */

    if ((int_status & UART_INT_STS_URX_FIFO_INT) &&
        !(int_mask & UART_INT_MASK_CR_URX_FIFO_MASK)){
        /* Receive Data ready */

        uart_data_push(uart_info);
    }

    return OK;
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    int ret = 0;
    uint32_t div;
    uint32_t int_mask;
    uint32_t fraction;
    uint32_t tx_cfg;
    uint32_t rx_cfg;
    uint8_t clk_div = 3;
    qm_uart_info_t *uart_info = NULL;

    if(uart == NULL){
        return -QM_EINVAL;
    }

    if(uart->port > (QM_ARRAY_SIZE(uart_config) - 1)){
        return -QM_EINVAL;
    }

    uart_info = &uart_config[uart->port];
    if(uart_info->uart){
        uart_info->uart = uart;
        uart->priv = (void*)uart_info;
        return QM_EOK;
    }

    modifyreg32(BL602_CLK_CFG2, CLK_CFG2_UART_CLK_EN, 0);
    modifyreg32(BL602_CLK_CFG2, CLK_CFG2_UART_CLK_DIV_MASK, 3);
    bl602_set_uart_clk_sel(1);
    modifyreg32(BL602_CLK_CFG2, 0, CLK_CFG2_UART_CLK_EN);

    switch (uart->port)
    {
        case 0:
            bl602_config_uart_sel((GPIO_INPUT | GPIO_PULLUP | GPIO_FUNC_UART | (uart->config.pins.tx_pin << GPIO_PIN_SHIFT)), 
            UART_SIG_SEL_UART0_TXD);
            bl602_configgpio((GPIO_INPUT | GPIO_PULLUP | GPIO_FUNC_UART | (uart->config.pins.tx_pin << GPIO_PIN_SHIFT)));

            bl602_config_uart_sel((GPIO_INPUT | GPIO_PULLUP | GPIO_FUNC_UART | (uart->config.pins.rx_pin << GPIO_PIN_SHIFT)), 
                    UART_SIG_SEL_UART0_RXD);
            bl602_configgpio((GPIO_INPUT | GPIO_PULLUP | GPIO_FUNC_UART | (uart->config.pins.rx_pin << GPIO_PIN_SHIFT)));
        break;

        case 1:
            bl602_config_uart_sel((GPIO_INPUT | GPIO_PULLUP | GPIO_FUNC_UART | (uart->config.pins.tx_pin << GPIO_PIN_SHIFT)), 
            UART_SIG_SEL_UART1_TXD);
            bl602_configgpio((GPIO_INPUT | GPIO_PULLUP | GPIO_FUNC_UART | (uart->config.pins.tx_pin << GPIO_PIN_SHIFT)));

            bl602_config_uart_sel((GPIO_INPUT | GPIO_PULLUP | GPIO_FUNC_UART | (uart->config.pins.rx_pin << GPIO_PIN_SHIFT)), 
                    UART_SIG_SEL_UART1_RXD);
            bl602_configgpio((GPIO_INPUT | GPIO_PULLUP | GPIO_FUNC_UART | (uart->config.pins.rx_pin << GPIO_PIN_SHIFT)));
        break;
    
        default:
            break;
    }

    /* Disable all interrupt */
    modifyreg32(BL602_UART_INT_MASK(uart->port), 0, 0xff);
    /* Disable uart before config */
    modifyreg32(BL602_UART_UTX_CONFIG(uart->port), UART_UTX_CONFIG_CR_EN, 0);
    modifyreg32(BL602_UART_URX_CONFIG(uart->port), UART_URX_CONFIG_CR_EN, 0);
    
    fraction = (_BL602_UART_CLOCK / (clk_div + 1)) * 10 / uart->config.baud_rate % 10;
    div      = (_BL602_UART_CLOCK / (clk_div + 1)) / uart->config.baud_rate;
    if (fraction >= 5){
        ++div;
    }

    /* set the baud rate register value */
    putreg32(((div - 1) << 0x10) | ((div - 1) & 0xffff), BL602_UART_BIT_PRD(uart->port));

    tx_cfg = getreg32(BL602_UART_UTX_CONFIG(uart->port));
    rx_cfg = getreg32(BL602_UART_URX_CONFIG(uart->port));

    switch (uart->config.parity)
    {
        case QM_UART_PARITY_NONE:
            tx_cfg &= ~UART_UTX_CONFIG_CR_PRT_EN;
            rx_cfg &= ~UART_URX_CONFIG_CR_PRT_EN;
        break;
        case QM_UART_PARITY_ODD:
            tx_cfg |= UART_UTX_CONFIG_CR_PRT_EN;
            tx_cfg |= UART_UTX_CONFIG_CR_PRT_SEL;
            rx_cfg |= UART_URX_CONFIG_CR_PRT_EN;
            rx_cfg |= UART_URX_CONFIG_CR_PRT_SEL;
        break;
        case QM_UART_PARITY_EVEN:
            tx_cfg |= UART_UTX_CONFIG_CR_PRT_EN;
            tx_cfg &= ~UART_UTX_CONFIG_CR_PRT_SEL;
            rx_cfg |= UART_URX_CONFIG_CR_PRT_EN;
            rx_cfg &= ~UART_URX_CONFIG_CR_PRT_SEL;
        break;
        default:
        break;
    }

    /* Configure data and stop bits */

    rx_cfg &= ~UART_URX_CONFIG_CR_BIT_CNT_D_MASK;
    rx_cfg |= ((uint32_t)uart->config.data_bits - 1) << \
                UART_URX_CONFIG_CR_BIT_CNT_D_SHIFT;

    tx_cfg &= ~(UART_UTX_CONFIG_CR_BIT_CNT_D_MASK | \
                UART_UTX_CONFIG_CR_BIT_CNT_P_MASK);
    tx_cfg |= (((uint32_t)uart->config.data_bits - 1) << \
                UART_UTX_CONFIG_CR_BIT_CNT_D_SHIFT) | \
                (((uint32_t)uart->config.stop_bits) << \
                UART_UTX_CONFIG_CR_BIT_CNT_P_SHIFT);

    
    tx_cfg &= ~(UART_UTX_CONFIG_CR_CTS_EN);
    rx_cfg &= ~(UART_URX_CONFIG_CR_DEG_EN);
    rx_cfg &= ~(UART_URX_CONFIG_CR_RTS_SW_MODE);

    /* Write back */

    putreg32(rx_cfg, BL602_UART_URX_CONFIG(uart->port));
    putreg32(tx_cfg, BL602_UART_UTX_CONFIG(uart->port));

    /* Configure LSB-first */

    modifyreg32(BL602_UART_DATA_CONFIG(uart->port),
                UART_DATA_CONFIG_CR_UART_BIT_INV, 0);

    /* Enable tx free run mode */

    modifyreg32(BL602_UART_UTX_CONFIG(uart->port), 0,
                UART_UTX_CONFIG_CR_FRM_EN);

    /* Configure FIFO thresholds */

    modifyreg32(BL602_UART_FIFO_CONFIG_1(uart->port),
                (UART_FIFO_CONFIG_1_RX_TH_MASK | \
                UART_FIFO_CONFIG_1_TX_TH_MASK),
                ((0x10 - 1) << UART_FIFO_CONFIG_1_RX_TH_SHIFT) | \
                ((0x10 - 1) << UART_FIFO_CONFIG_1_TX_TH_SHIFT));

    /* Enable UART tx rx unit */

    modifyreg32(BL602_UART_UTX_CONFIG(uart->port), 0,
                UART_UTX_CONFIG_CR_EN);
    modifyreg32(BL602_UART_URX_CONFIG(uart->port), 0,
                UART_URX_CONFIG_CR_EN);

    unsigned int  flags = enter_critical_section();
    int_mask = getreg32(BL602_UART_INT_MASK(uart->port));
    int_mask &= ~(UART_INT_MASK_CR_URX_FIFO_MASK);
    int_mask &= ~(UART_INT_MASK_CR_URX_END_MASK);
    int_mask &= ~(UART_INT_MASK_CR_URX_RTO_MASK);
    putreg32(int_mask, BL602_UART_INT_MASK(uart->port));
    leave_critical_section(flags);

    up_enable_irq(uart_info->config.NVIC_IRQChannel);
    irq_attach(uart_info->config.NVIC_IRQChannel, __uart_interrupt, (void *)uart_info);

    putreg32(UART_INT_CLEAR_CR_URX_RTO_CLR,BL602_UART_INT_CLEAR(uart_info->config.index));

    qm_sem_new(&uart_info->rx_sem, 0);
    qm_ringbuf_init(&uart_info->ringbuf, uart_info->buf, UART_HANDLER_RECV_LEN);
    
    uart_info->uart = uart;

    uart->priv = (void*)uart_info;

    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_ringbuf_flush(&uart_info->ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_info->read = (qm_uart_read_callback_t)param;
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            uart_info->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}

static void uart_send_byte(uint8_t uart_idx, uint8_t data)
{
    while ((getreg32(BL602_UART_FIFO_CONFIG_1(uart_idx)) & \
            UART_FIFO_CONFIG_1_TX_CNT_MASK) == 0);

    putreg32(data, BL602_UART_FIFO_WDATA(uart_idx));
}

static void uart_send_bytes(uint8_t uart_idx, uint8_t *data, int len)
{
	while(len)
	{
		uart_send_byte(uart_idx, *data ++);
		len --;
	}
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    qm_uart_info_t *uart_info = NULL;
    if(uart == NULL || data == NULL || size == 0 || uart->priv == NULL){
        return -QM_EINVAL;
    }

    uart_info = (qm_uart_info_t*)uart->priv;

    uart_send_bytes(uart->port, data, size);

    if(uart_info->write){
        uart_info->write(uart_info->uart, size);
    } 

    return QM_EOK;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int wait = 0;
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_left = 0;
    uint8_t isempty = 0;
    qm_utils_time_t time = {0};
    qm_uart_info_t *uart_info = NULL;
    if(uart == NULL || data == NULL || expect_size == 0 || uart->priv == NULL){
        return -QM_EINVAL;
    }

    uart_info = (qm_uart_info_t*)uart->priv;
    
    if(timeout == QM_WAIT_FOREVER){
        wait = 1;
        t_left = QM_WAIT_FOREVER;
    }else{
        if(timeout){
            qm_utils_time_init(&time);
            qm_utils_time_countdown_ms(&time, timeout);  
        }
    }
    
    do{
        
        if(!wait && timeout){
            t_left = qm_utils_time_left(&time);
            if(t_left <= 0){
                break;
            }
        }
        
        isempty = qm_ringbuf_isempty(&uart_info->ringbuf);

        if(!isempty){
            
            len = qm_ringbuf_pop(&uart_info->ringbuf, (uint8_t *)data + recv_len, expect_size - recv_len);
            if(len > 0){
                recv_len += len;
                uart_info->recv_size -= len;
            }else{
                ret = 0;
                break;
            }
        }else{

            if(t_left == 0){
                ret = 0;
                break;
            }
                    
            ret = qm_sem_wait(&uart_info->rx_sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }     

    }while ((recv_len < expect_size));

    return (0 != recv_len) ? recv_len : ret;     
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    uint32_t int_mask;
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

    unsigned int flags = enter_critical_section();

    int_mask = getreg32(BL602_UART_INT_MASK(uart->port));
    int_mask |= UART_INT_MASK_CR_URX_FIFO_MASK;
    int_mask |= UART_INT_MASK_CR_URX_END_MASK;
    putreg32(int_mask, BL602_UART_INT_MASK(uart->port));
 
    leave_critical_section(flags);

    up_disable_irq(uart_info->config.NVIC_IRQChannel);
    irq_detach(uart_info->config.NVIC_IRQChannel);

    modifyreg32(BL602_UART_UTX_CONFIG(uart->port), UART_UTX_CONFIG_CR_EN, 0);
    modifyreg32(BL602_UART_URX_CONFIG(uart->port), UART_URX_CONFIG_CR_EN, 0);

    qm_sem_free(&uart_info->rx_sem);

    qm_free(uart_info);

    uart->priv = NULL;
    return QM_EOK;
}

#endif
