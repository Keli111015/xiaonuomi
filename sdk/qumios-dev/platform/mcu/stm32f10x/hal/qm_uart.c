#include "qm.h"

#ifdef CONFIG_QM_UART_SUPPORT

#include "qm_uart.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"
#include "qm_platform.h"

#include "misc.h"
#include "stm32f10x.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "stm32f10x_usart.h"


typedef struct{
    USART_TypeDef *USARTx;
    uint8_t APBPeriph_TYPE;
    uint32_t APBPeriph;
    IRQn_Type NVIC_IRQChannel;
}uart_config_t;

typedef struct
{
    uart_config_t config;

    uint8_t *buf;
    uint16_t size;
    uint16_t rx_size;
    qm_sem_t rx_sem;
    qm_ringbuf_t ringbuf;

    uint8_t is_wait;
    qm_uart_dev_t *uart_dev;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;

}qm_uart_info_t;

static qm_uart_info_t g_uart_info[] = {
    {{USART1, APBPeriph_TYPE_2, RCC_APB2Periph_USART1, USART1_IRQn}, NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
    {{USART2, APBPeriph_TYPE_1, RCC_APB1Periph_USART2, USART2_IRQn}, NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
    {{USART3, APBPeriph_TYPE_1, RCC_APB1Periph_USART3, USART3_IRQn}, NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
#if defined(STM32F10X_HD) || defined(STM32F10X_HD_VL) || defined(STM32F10X_XL) || defined(STM32F10X_CL)
    {{UART4, APBPeriph_TYPE_1, RCC_APB1Periph_UART4, UART4_IRQn}, NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
    {{UART5, APBPeriph_TYPE_1, RCC_APB1Periph_UART5, UART5_IRQn}, NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
#endif
};

static void uartx_irq_recv_data(USART_TypeDef *USARTx, uint8_t id);
static int32_t qm_uart_pin_enable(uint8_t pins, uint8_t is_output);

int32_t qm_uart_init(qm_uart_dev_t *uart)
{

    qm_uart_info_t *info = NULL;
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef NVIC_InitStructure;

    if(uart == NULL){
        return -QM_EINVAL;
    }

    if(uart->port >= QM_ARRAY_SIZE(g_uart_info)){
        return -QM_EINVAL;
    }

    info = &g_uart_info[uart->port];
    USART_DeInit(info->config.USARTx);

    if(info->config.APBPeriph_TYPE == APBPeriph_TYPE_1){
        RCC_APB1PeriphClockCmd(info->config.APBPeriph, ENABLE);
    }else if(info->config.APBPeriph_TYPE == APBPeriph_TYPE_2){
        RCC_APB2PeriphClockCmd(info->config.APBPeriph, ENABLE);
    }

    qm_uart_pin_enable(uart->config.pins.tx_pin, QM_TRUE);
    qm_uart_pin_enable(uart->config.pins.rx_pin, QM_FALSE);

    USART_InitStruct.USART_BaudRate = uart->config.baud_rate;
    USART_InitStruct.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    switch(uart->config.parity){
        case QM_UART_PARITY_NONE:    
            USART_InitStruct.USART_Parity = USART_Parity_No;
        break;
        default:
        break;
    }
    switch(uart->config.stop_bits){
        case QM_UART_STOP_BITS_1:    
            USART_InitStruct.USART_StopBits = USART_StopBits_1;
        break;
        default:
        break;
    }

    switch(uart->config.data_bits){
        case QM_UART_DATA_8_BITS:    
            USART_InitStruct.USART_WordLength = USART_WordLength_8b;
        break;
        default:
        break;
    }

    switch(uart->config.flow_ctrl){
        case QM_UART_FLOW_CTRL_NONE:    
            USART_InitStruct.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
        break;
        default:
        break;
    }    

    USART_Init(info->config.USARTx,&USART_InitStruct);

    USART_ITConfig(info->config.USARTx,USART_IT_TC,DISABLE);
    USART_ITConfig(info->config.USARTx,USART_IT_IDLE,ENABLE);    
    USART_ITConfig(info->config.USARTx, USART_IT_RXNE, ENABLE);
    USART_ClearITPendingBit(info->config.USARTx,USART_IT_IDLE);       
    USART_ClearITPendingBit(info->config.USARTx,USART_IT_RXNE); 
    
    if(info->config.USARTx == USART1){
        NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;        
    }else if(info->config.USARTx == USART2){
        NVIC_InitStructure.NVIC_IRQChannel = USART2_IRQn;
    }else if(info->config.USARTx == USART3){
        NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    }
#if defined(STM32F10X_HD) || defined(STM32F10X_HD_VL) || defined(STM32F10X_XL) || defined(STM32F10X_CL)
    else if(info->config.USARTx == UART4){
        NVIC_InitStructure.NVIC_IRQChannel = UART4_IRQn;
    }else if(info->config.USARTx == UART5){
        NVIC_InitStructure.NVIC_IRQChannel = UART5_IRQn;
    }
#endif
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;

    NVIC_Init(&NVIC_InitStructure);
    USART_Cmd(info->config.USARTx, ENABLE);
    
    info->buf = (uint8_t*)qm_malloc(info->size);
    if(info->buf == NULL){
        return -QM_ENOMEM;
    }
    memset(info->buf, 0, info->size);

    qm_sem_new(&info->rx_sem, 0);
    qm_ringbuf_init(&info->ringbuf, info->buf, info->size);

    uart->priv = (void*)info;
    info->uart_dev = uart;
    return 0;
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    qm_uart_info_t *info = NULL;

    if (uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

    info = (qm_uart_info_t *)uart->priv;   

    USART_ITConfig(info->config.USARTx,USART_IT_TC,DISABLE);
    USART_ITConfig(info->config.USARTx,USART_IT_IDLE,DISABLE);    
    USART_ITConfig(info->config.USARTx, USART_IT_RXNE, DISABLE);

    USART_Cmd(info->config.USARTx, DISABLE);
    USART_DeInit(info->config.USARTx);

    if(qm_sem_is_valid(&info->rx_sem)){
        qm_sem_free(&info->rx_sem);
    }

    qm_ringbuf_flush(&info->ringbuf);
    if(info->buf){
        qm_free(info->buf);
        info->buf = NULL;
    }
    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{

    int i = 0;
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    if(uart == NULL || uart->priv == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    for(i=0; i<size; i++){
        uint8_t chr = *((uint8_t *)data + i);
        USART_SendData(uart_info->config.USARTx, chr);    
        if(i == size - 1){
            while(USART_GetFlagStatus(uart_info->config.USARTx, USART_FLAG_TC) == RESET);
        }else{
            while(USART_GetFlagStatus(uart_info->config.USARTx, USART_FLAG_TXE) == RESET);
        }
    }
    return QM_EOK;
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
    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);  

    do{
        t_left = qm_utils_time_left(&time);
        
        isempty = qm_ringbuf_isempty(&uart_info->ringbuf);

        if(!isempty){
            
            len = qm_ringbuf_pop(&uart_info->ringbuf, (uint8_t *)data + recv_len, expect_size-recv_len);
            if(len > 0){
                recv_len += len;
            }else{
                ret = 0;
                break;
            }
        }else{

            if(t_left == 0 || uart_info->read){
                ret = 0;
                break;
            }

            ret = qm_sem_wait(&uart_info->rx_sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }            
    }while ((recv_len < expect_size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret; 
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_ringbuf_flush(&uart_info->ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_info->read = (qm_uart_read_callback_t)param;
            qm_sem_free(&uart_info->rx_sem); //节省资源,注册回调时无需使用sem
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            uart_info->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}

void USART1_IRQHandler(void)
{
    uartx_irq_recv_data(USART1, 0);
}

void USART2_IRQHandler(void)
{
    uartx_irq_recv_data(USART2, 1);
}

void USART3_IRQHandler(void)
{
    uartx_irq_recv_data(USART3, 2);
}

#if defined(STM32F10X_HD) || defined(STM32F10X_HD_VL) || defined(STM32F10X_XL) || defined(STM32F10X_CL)
void UART4_IRQHandler(void)
{
    uartx_irq_recv_data(UART4, 3);
}

void UART5_IRQHandler(void)
{
    uartx_irq_recv_data(UART5, 4);
}
#endif

static void uartx_irq_recv_data(USART_TypeDef *USARTx, uint8_t id)
{
    uint8_t chr = 0;
    qm_uart_info_t *uart_info = &g_uart_info[id];

    if(USART_GetITStatus(USARTx, USART_IT_IDLE) != RESET)      
    {     
        USART_ClearITPendingBit(USARTx,USART_IT_IDLE);
        if(uart_info->read){
            uart_info->read(uart_info->uart_dev, uart_info->rx_size);
        }
        uart_info->rx_size = 0;
    } 
 
    if(USART_GetITStatus(USARTx, USART_IT_RXNE) != RESET)      
    {          
        USART_ClearITPendingBit(USARTx,USART_IT_RXNE);                   
        chr = (uint8_t)USART_ReceiveData(USARTx);
        uart_info->rx_size += qm_ringbuf_push(&uart_info->ringbuf, &chr, 1);
    }
}

static int32_t qm_uart_pin_enable(uint8_t pins, uint8_t is_output)
{
    int ret = QM_EOK;
    GPIO_TypeDef *GPIOx;
    uint32_t AHBPeriph_Clock;
    GPIO_InitTypeDef GPIO_InitStructure;

     switch ((pins / GPIOX_PIN_MAX_NUM))
    {
        case  QM_GPIOA:
            GPIOx = GPIOA;
            AHBPeriph_Clock = RCC_APB2Periph_GPIOA;
        break;

        case  QM_GPIOB:
            GPIOx = GPIOB;
            AHBPeriph_Clock = RCC_APB2Periph_GPIOB;
        break;

        case  QM_GPIOC:
            GPIOx = GPIOC;
            AHBPeriph_Clock = RCC_APB2Periph_GPIOC;
        break;

        case  QM_GPIOD:
            GPIOx = GPIOD;
            AHBPeriph_Clock = RCC_APB2Periph_GPIOC;
        break;

        case  QM_GPIOE:
            GPIOx = GPIOE;
            AHBPeriph_Clock = RCC_APB2Periph_GPIOC;
        break;
    
        case  QM_GPIOF:
            GPIOx = GPIOF;
            AHBPeriph_Clock = RCC_APB2Periph_GPIOF;
        break;

        case  QM_GPIOG:
            GPIOx = GPIOG;
            AHBPeriph_Clock = RCC_APB2Periph_GPIOG;
        break;

        default:
           ret = -QM_ERROR;
        break;
    }
    if(ret != QM_EOK){
        return ret;
    }	
		
    RCC_APB2PeriphClockCmd(AHBPeriph_Clock, ENABLE);
    GPIO_InitStructure.GPIO_Pin = 1 << (pins % GPIOX_PIN_MAX_NUM);
    if(is_output){
        GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_PP;
    }else{
        GPIO_InitStructure.GPIO_Mode=GPIO_Mode_IN_FLOATING;
    }
    GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
    GPIO_Init(GPIOx, &GPIO_InitStructure);

    return QM_EOK;
}

#endif