#include "qm.h"
#include "qm_uart.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"

#include <rtl876x.h>
#include "rtl876x_gpio.h"
#include "rtl876x_rcc.h"
#include <rtl876x_nvic.h>
#include <rtl876x_uart.h>
#include <rtl876x_rcc.h>
#include <rtl876x_pinmux.h>

#include <nuttx/irq.h>

#define LOG_TAG "uart hal"

typedef struct{
    UART_TypeDef *UARTx;
    uint32_t APBPeriph;
    uint32_t APBPeriph_Clock;
    const uint32_t    NVIC_IRQChannel;
    uint8_t tx_pin_fun;
    uint8_t rx_pin_fun;
}uart_config_t;

typedef struct{
    uart_config_t config;
    uint8_t *buf;
    uint16_t buf_size;
    uint16_t recv_size;
    qm_ringbuf_t ringbuf;
    qm_uart_dev_t *uart;
    qm_uart_read_callback_t read;
}qm_uart_info_t;

#define UART_HANDLER_RECV_LEN  (256)

static int uart1_handler(int irq, void *context, void *arg);

static qm_uart_info_t g_uart_info[] = {
    {{UART0,  APBPeriph_UART0, APBPeriph_UART0_CLOCK, Uart0_VECTORn,UART0_TX, UART0_RX}, NULL, UART_HANDLER_RECV_LEN, 0, {0},NULL ,NULL},
    {{UART1, APBPeriph_UART1, APBPeriph_UART1_CLOCK, Uart1_VECTORn,UART1_TX, UART1_RX}, NULL, UART_HANDLER_RECV_LEN, 0, {0}, NULL, NULL},
};


#define QM_UART_INFO_NUM  (sizeof(g_uart_info)/sizeof(g_uart_info[0]))


int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }
    
    unsigned int  flags = enter_critical_section();
 /* enable rx interrupt and line status interrupt */
    UART_INTConfig(uart_info->config.UARTx, UART_INT_RD_AVA,  DISABLE);
    UART_INTConfig(uart_info->config.UARTx, UART_INT_RX_IDLE, DISABLE);
    UART_MaskINTConfig(uart_info->config.UARTx, UART_INT_MASK_RD_AVA,  DISABLE);
    UART_MaskINTConfig(uart_info->config.UARTx, UART_INT_MASK_RX_IDLE, DISABLE);

    leave_critical_section(flags);

    NVIC_DisableIRQ(uart_info->config.NVIC_IRQChannel);
    up_disable_irq(uart_info->config.NVIC_IRQChannel);
    irq_detach(uart_info->config.NVIC_IRQChannel);
    uart->priv = NULL;
    
    // print
    if(uart_info->buf){
        qm_free(uart_info->buf);
        uart_info->buf = NULL;
    }
    return QM_EOK;
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    qm_uart_info_t *uart_info = NULL;
    if(uart == NULL){
        return -QM_EINVAL;
    }
    
    if(uart->port > QM_UART_INFO_NUM-1){
        return -QM_EINVAL;
    }
    
    uart_info = &g_uart_info[uart->port];

    UART_DeInit(uart_info->config.UARTx);
    
	Pinmux_Deinit(uart->config.pins.tx_pin);
	Pinmux_Deinit(uart->config.pins.rx_pin);
	Pinmux_Config(uart->config.pins.tx_pin, IDLE_MODE);
	Pinmux_Config(uart->config.pins.rx_pin, IDLE_MODE);
	Pad_Config(uart->config.pins.tx_pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
	Pad_Config(uart->config.pins.rx_pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_LOW);
    Pinmux_Config(uart->config.pins.tx_pin, uart_info->config.tx_pin_fun);
    Pinmux_Config(uart->config.pins.rx_pin, uart_info->config.rx_pin_fun);
    RCC_PeriphClockCmd(uart_info->config.APBPeriph, uart_info->config.APBPeriph_Clock, ENABLE);
    
    /* uart init */
    UART_InitTypeDef UART_InitStruct;
    UART_StructInit(&UART_InitStruct);
    if(uart->config.baud_rate == 4800){ 
        /*set 4800*/
        UART_InitStruct.div			  = 0x682;
        UART_InitStruct.ovsr 		  = 0x00;
        UART_InitStruct.ovsr_adj 	  = 0x00;
    }
    else if(uart->config.baud_rate == 9600){ //default 115200
        /*set BAUD_RATE_9600*/
        UART_InitStruct.div			  = 271;//271;
        UART_InitStruct.ovsr 		  = 10;//10;
        UART_InitStruct.ovsr_adj 	  = 0x24a;//0x24a;
    }
    else if(uart->config.baud_rate == 38400){ //default 38400
        /*set BAUD_RATE_9600*/
        UART_InitStruct.div			  = 85;//271;
        UART_InitStruct.ovsr 		  = 7;//10;
        UART_InitStruct.ovsr_adj 	  = 0x222;//0x24a;
    }
    else if(uart->config.baud_rate == 115200){ //default 115200
        /*set BAUD_RATE_9600*/
        UART_InitStruct.div			  = 20;//271;
        UART_InitStruct.ovsr 		  = 12;//10;
        UART_InitStruct.ovsr_adj 	  = 0x252;//0x24a;
    }
    else if(uart->config.baud_rate == 1000000){
        /*set 1000000*/
        UART_InitStruct.div			  = 4;
        UART_InitStruct.ovsr 		  = 5;
        UART_InitStruct.ovsr_adj 	  = 0;
    }
    else if(uart->config.baud_rate == 2000000){
        /*set 2000000*/
        UART_InitStruct.div			  = 2;
        UART_InitStruct.ovsr 		  = 5;
        UART_InitStruct.ovsr_adj 	  = 0;
    }
    else
    {
        return -QM_EINVAL;
    }

    #undef parity
    switch(uart->config.parity){
        case QM_UART_PARITY_NONE:    
            UART_InitStruct.UART_Parity = UART_PARITY_NO_PARTY;
        break;
        default:break;
    }

    switch(uart->config.stop_bits){
        case QM_UART_STOP_BITS_1:    
            UART_InitStruct.UART_StopBits  = UART_STOP_BITS_1;
        break;
        default:break;
    }
    switch(uart->config.data_bits){
        case QM_UART_DATA_8_BITS:    
            UART_InitStruct.UART_WordLen  = UART_WORD_LENGTH_8BIT;
        break;
        default:break;
    }
    UART_InitStruct.UART_RxThdLevel = 16;  
    UART_InitStruct.UART_IdleTime      = UART_RX_IDLE_8BYTE;                //idle interrupt wait time
    UART_Init(uart_info->config.UARTx, &UART_InitStruct);

    unsigned int  flags = enter_critical_section();

      /* enable rx interrupt and line status interrupt */
    UART_INTConfig(uart_info->config.UARTx, UART_INT_RD_AVA,  ENABLE);
    UART_INTConfig(uart_info->config.UARTx, UART_INT_RX_IDLE, ENABLE);
    UART_MaskINTConfig(uart_info->config.UARTx, UART_INT_MASK_RD_AVA,  DISABLE);
    UART_MaskINTConfig(uart_info->config.UARTx, UART_INT_MASK_RX_IDLE, DISABLE);

    leave_critical_section(flags);

    /*  Enable UART IRQ  */
    NVIC_ClearPendingIRQ(uart_info->config.NVIC_IRQChannel);
    NVIC_SetPriority(uart_info->config.NVIC_IRQChannel, 0);
    NVIC_EnableIRQ(uart_info->config.NVIC_IRQChannel);

    up_enable_irq(uart_info->config.NVIC_IRQChannel);
    irq_attach(uart_info->config.NVIC_IRQChannel, uart1_handler, uart_info);

    uart_info->buf = (uint8_t*)qm_malloc(uart_info->buf_size);
    if(uart_info->buf == NULL){
        return -QM_ENOMEM;
    }
    memset(uart_info->buf, 0, uart_info->buf_size);
    qm_ringbuf_init(&uart_info->ringbuf, uart_info->buf, uart_info->buf_size);
    
    uart_info->uart = uart;
    uart->priv = (void*)uart_info;

    return 0;
}

static void uart_send_byte(UART_TypeDef *UARTx, uint8_t data)
{
    UART_SendData(UARTx, &data, 1);
    /* wait tx fifo empty */
    while (UART_GetFlagState(UARTx, UART_FLAG_THR_TSR_EMPTY) != SET);
}

static void uart_send_bytes(UART_TypeDef *UARTx, uint8_t *data, int len)
{
	while(len)
	{
		uart_send_byte(UARTx, *data ++);
		len --;
	}
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t*)uart->priv;

    QM_RETURN_ON_FALSE(uart, -QM_EINVAL, LOG_TAG, "uart inval");
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");
    QM_RETURN_ON_FALSE(size, -QM_EINVAL, LOG_TAG, "size inval");
    QM_RETURN_ON_FALSE(uart->priv, -QM_EINVAL, LOG_TAG, "uart inval");

    uart_send_bytes(uart_info->config.UARTx, (uint8_t*)data, size);

    return size;
}




int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    qm_err_t ret = QM_EOK;
    qm_uart_info_t *uart_info =  (qm_uart_info_t *)uart->priv;

    QM_RETURN_ON_FALSE(uart, -QM_EINVAL, LOG_TAG, "uart inval");
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");
    QM_RETURN_ON_FALSE(uart->priv, -QM_EINVAL, LOG_TAG, "uart inval");
    QM_RETURN_ON_FALSE(expect_size, -QM_EINVAL, LOG_TAG, "size inval");

    if(qm_ringbuf_isempty(&uart_info->ringbuf)) {
        return 0;
    }

    len = qm_ringbuf_pop(&uart_info->ringbuf, data, expect_size);
    uart_info->recv_size -= len;
    return (len > 0 ? len : 0);   
}


static void uart_data_push(qm_uart_info_t *uart_info)
{
    uint16_t lenth = 0;
    uint8_t data = 0;
    lenth = UART_GetRxFIFOLen(uart_info->config.UARTx);
    while(lenth--){
        UART_ReceiveData(uart_info->config.UARTx, &data, 1);
        
        if(uart_info->buf == NULL){
            continue;
        }

        uart_info->recv_size += qm_ringbuf_push(&uart_info->ringbuf, &data, 1);
        if(uart_info->recv_size >= UART_HANDLER_RECV_LEN && uart_info->read){
            uart_info->read(uart_info->uart, uart_info->recv_size);
        }
    }
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    if(uart == NULL || param == NULL){
        return -QM_EINVAL;
    }
    qm_uart_info_t *uart_info = NULL;

    uart_info = (qm_uart_info_t *)uart->priv;

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_ringbuf_flush(&uart_info->ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_info->read = (qm_uart_read_callback_t)param;
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
        break;
        default:
            break;
    }
    return QM_EOK;
}


static int uart1_handler(int irq, void *context, void *arg)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)arg;

    unsigned int  flags = enter_critical_section();

    /* read interrupt id */
    uint32_t int_status = UART_GetIID(uart_info->config.UARTx);
    /* disable interrupt */
    UART_INTConfig(uart_info->config.UARTx, UART_INT_RD_AVA, DISABLE);
    if (UART_GetFlagState(uart_info->config.UARTx, UART_FLAG_RX_IDLE) == SET)
    {
        UART_INTConfig(uart_info->config.UARTx, UART_INT_RX_IDLE, DISABLE);
        UART_ClearRxFIFO(uart_info->config.UARTx);
        if(uart_info->read && uart_info->recv_size){
            uart_info->read(uart_info->uart, uart_info->recv_size);
        }
        UART_INTConfig(uart_info->config.UARTx, UART_INT_RX_IDLE, ENABLE);
    }
    switch (int_status & 0x0E)
    {
        
            /* rx data valiable */
    case UART_INT_ID_RX_LEVEL_REACH:
    case UART_INT_ID_RX_TMEOUT:
        uart_data_push(uart_info);
    break;
    case 0x02:
        /* do nothing */
        break;
    /* modem status */
    case 0x00:
        break;
    default:
        break;
    }
    /* enable interrupt again */
    UART_INTConfig(uart_info->config.UARTx, UART_INT_RD_AVA, ENABLE);

    leave_critical_section(flags);

    return 0;
}
