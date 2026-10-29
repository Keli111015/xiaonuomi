#include "qm_uart.h"
#include "qm_ringbuf.h"
#include <rtl876x.h>
#include "rtl876x_gpio.h"
#include "rtl876x_rcc.h"
#include <rtl876x_nvic.h>
#include <rtl876x_uart.h>
#include <rtl876x_rcc.h>
#include <rtl876x_pinmux.h>
#include "qm_kernel.h"
#include "qm_errno.h"


typedef struct{
    UART_TypeDef *UARTx;
    uint32_t APBPeriph;
    uint32_t APBPeriph_Clock;
    IRQn_Type NVIC_IRQChannel;
    uint8_t tx_pin_fun;
    uint8_t rx_pin_fun;
}uart_config_t;

typedef struct{
    uart_config_t config;
    uint8_t *buf;
    int size;
    uint16_t sem_sign ;
    qm_ringbuf_t ringbuf;
    qm_sem_t sem;
}qm_uart_info_t;

static qm_uart_info_t g_uart_info[] = {
    {{UART,  APBPeriph_UART0, APBPeriph_UART0_CLOCK, UART0_IRQn, UART0_TX, UART0_RX}, NULL, 512, 0, {0},{0}},
    {{UART1, APBPeriph_UART1, APBPeriph_UART1_CLOCK, UART1_IRQn, UART1_TX, UART1_RX}, NULL, 512, 0, {0},{0}},
    {{UART2, APBPeriph_UART2, APBPeriph_UART2_CLOCK, UART2_IRQn, UART2_TX, UART2_RX}, NULL, 512, 0, {0},{0}},
};

#define QM_UART_INFO_NUM  (sizeof(g_uart_info)/sizeof(g_uart_info[0]))

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }
    
    uart->priv = NULL;

    if(uart_info->buf){
        qm_free(uart_info->buf);
        uart_info->buf = NULL;
    }
    qm_sem_free(&uart_info->sem);
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
	Pad_Config(uart->config.pins.rx_pin, PAD_PINMUX_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_DISABLE, PAD_OUT_HIGH);
    Pinmux_Config(uart->config.pins.tx_pin, uart_info->config.tx_pin_fun);
    Pinmux_Config(uart->config.pins.rx_pin, uart_info->config.rx_pin_fun);
    RCC_PeriphClockCmd(uart_info->config.APBPeriph, uart_info->config.APBPeriph_Clock, ENABLE);
    
    /* uart init */
    UART_InitTypeDef UART_InitStruct;
    UART_StructInit(&UART_InitStruct);
    if(uart->config.baud_rate == 9600){ //default 115200
        /*set BAUD_RATE_9600*/
        UART_InitStruct.div			  = 271;//271;
        UART_InitStruct.ovsr 		  = 10;//10;
        UART_InitStruct.ovsr_adj 	  = 0x24a;//0x24a;
    }else
    if(uart->config.baud_rate == 115200){ //default 115200
        /*set BAUD_RATE_9600*/
        UART_InitStruct.div			  = 20;//271;
        UART_InitStruct.ovsr 		  = 12;//10;
        UART_InitStruct.ovsr_adj 	  = 0x252;//0x24a;
    }

    switch(uart->config.parity){
        case QM_UART_PARITY_NONE:    
            UART_InitStruct.parity = UART_PARITY_NO_PARTY;
        break;
        default:break;
    }
    switch(uart->config.stop_bits){
        case QM_UART_STOP_BITS_1:    
            UART_InitStruct.stopBits  = UART_STOP_BITS_1;
        break;
        default:break;
    }
    switch(uart->config.data_bits){
        case QM_UART_DATA_8_BITS:    
            UART_InitStruct.wordLen  = UART_WROD_LENGTH_8BIT;
        break;
        default:break;
    }
    UART_InitStruct.rxTriggerLevel = UART_RX_FIFO_TRIGGER_LEVEL_8BYTE;  
    UART_InitStruct.idle_time      = UART_RX_IDLE_8BYTE;                //idle interrupt wait time
    UART_Init(uart_info->config.UARTx, &UART_InitStruct);

      /* enable rx interrupt and line status interrupt */
    UART_INTConfig(uart_info->config.UARTx, UART_INT_RD_AVA | UART_INT_IDLE, ENABLE);

    /*  Enable UART IRQ  */
    NVIC_InitTypeDef NVIC_InitStruct;
    NVIC_InitStruct.NVIC_IRQChannel         = uart_info->config.NVIC_IRQChannel;
    NVIC_InitStruct.NVIC_IRQChannelCmd      = (FunctionalState)ENABLE;
    NVIC_InitStruct.NVIC_IRQChannelPriority = 5;
    NVIC_Init(&NVIC_InitStruct);
    
    uart_info->buf = (uint8_t*)qm_malloc(uart_info->size);
    if(uart_info->buf == NULL){
        return -QM_ENOMEM;
    }
    
    qm_ringbuf_init(&uart_info->ringbuf, uart_info->buf, uart_info->size);
    
    qm_sem_new(&uart_info->sem, 0);
    
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
    qm_uart_info_t *uart_info = NULL;
    if(uart == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }
    if(uart->priv == NULL){
        return -QM_EINVAL;
    }
    uart_info = (qm_uart_info_t*)uart->priv;
    uart_send_bytes(uart_info->config.UARTx, (uint8_t*)data, size);
    return size;
}


static uint32_t _platform_time_left(uint32_t t_end, uint32_t t_now)
{
    uint32_t res = 0;
    if(t_now - t_end < (UINT32_MAX/2)){
      return 0;
    }
    else{
      res = t_end-t_now;
      return res;
    }
} 

       
int32_t  qm_uart_recv_run(void)
{
    qm_uart_info_t *uart_info = &g_uart_info[1];
    uart_info->sem_sign = 1;
    return qm_sem_signal(&uart_info->sem);
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_end = 0, t_left = 0;
    qm_uart_info_t *uart_info = NULL;
    if(uart == NULL || data == NULL || expect_size == 0){
        return -QM_EINVAL;
    }
    if(uart->priv == NULL){
        return -QM_EINVAL;
    }
    uart_info = (qm_uart_info_t*)uart->priv;
    
    t_end = qm_now_ms() + timeout;
    
    do{
        t_left = _platform_time_left(t_end, qm_now_ms());
        
        if(!qm_ringbuf_isempty(&uart_info->ringbuf)){
            len = qm_ringbuf_pop(&uart_info->ringbuf, data+recv_len, expect_size-recv_len);
            if(len > 0){
                recv_len += len;
            }else{
                ret = -QM_ERROR;
                break;
            }
        }else{

            if(t_left <= 0){
                t_left = 10;
            }

            ret = qm_sem_wait(&uart_info->sem, t_left);
            if(ret == -QM_ETIMEOUT || uart_info->sem_sign){
                ret = 0;
                uart_info->sem_sign = 0;
                break;
            }
        }            
    }while ((recv_len < expect_size) && (_platform_time_left(t_end, qm_now_ms()) > 0));
    
    return (0 != recv_len) ? recv_len : ret;        
}

static void uart_data_push(qm_uart_info_t *uart_info)
{
    uint16_t lenth = 0;
    uint8_t data = 0;
    lenth = UART_GetRxFIFOLen(uart_info->config.UARTx);
    while(lenth--){
        UART_ReceiveData(uart_info->config.UARTx, &data, 1);
        qm_ringbuf_push(&uart_info->ringbuf, &data, 1);
    }
}

//void UART1_Handler()
//{
//   qm_uart_info_t *uart_info = &g_uart_info[1];
//   /* read interrupt id */
//   uint32_t int_status = UART_GetIID(uart_info->config.UARTx);
//   /* disable interrupt */
//   UART_INTConfig(uart_info->config.UARTx, UART_INT_RD_AVA, DISABLE);
//   if (UART_GetFlagState(uart_info->config.UARTx, UART_FLAG_RX_IDLE) == SET)
//   {
//       UART_INTConfig(uart_info->config.UARTx, UART_INT_IDLE, DISABLE);
//       qm_sem_signal(&uart_info->sem);
//       UART_INTConfig(uart_info->config.UARTx, UART_INT_IDLE, ENABLE);

//   }
//   switch (int_status)
//   {
//      
//           /* rx data valiable */
//   case UART_INT_ID_RX_LEVEL_REACH:
//   case UART_INT_ID_RX_TMEOUT:
//       uart_data_push(uart_info);
//   
//   break;
//   case 0x02:
//       /* do nothing */
//       break;
//   /* modem status */
//   case 0x00:
//       break;
//   default:
//       break;
//   }
//   /* enable interrupt again */
//   UART_INTConfig(uart_info->config.UARTx, UART_INT_RD_AVA, ENABLE);
//}
