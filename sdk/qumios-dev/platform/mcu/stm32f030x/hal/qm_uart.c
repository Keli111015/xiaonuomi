#include "qm_uart.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"
#include "qm_types.h"
#include "qm_platform.h"

#include "stm32f0xx.h"
#include "stm32f0xx_usart.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_rcc.h"

#include "FreeRTOS.h"

#define QM_UART_PORT_MAX    (6)

#define GPIOX_PIN_MAX_NUM       (16) // GPIOPIN0 ~ GPIO15 
#define GPIOX_TYPEDEF_MAX_NUM   (4) // GPIOA GPIOB GPIOC GPIOD

typedef enum{
    QM_GPIOA = 0,
    QM_GPIOB = 1,
    QM_GPIOC = 2,
    QM_GPIOD = 3,
}gpio_typedef_t;


typedef enum
{
    APBPeriph_TYPE_1,
    APBPeriph_TYPE_2,
    APBPeriph_TYPE_MAX,
}uart_apbPeriph_t;

typedef struct{
    USART_TypeDef *USARTx;
    uint8_t APBPeriph_TYPE;
    uint32_t APBPeriph;
    IRQn_Type NVIC_IRQChannel;
}uart_config_t;

typedef struct{
    uint8_t GPIO_AF_x;
    uint8_t tx_pins;
    uint8_t rx_pins;
}uart_pins_config_t;

typedef struct
{
    uart_config_t config;
    uart_pins_config_t pins_confg;

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
    {{USART1, APBPeriph_TYPE_2, RCC_APB2Periph_USART1, USART1_IRQn},   {GPIO_AF_0, QM_GPIO_PIN_22, QM_GPIO_PIN_23}, NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
    {{USART2, APBPeriph_TYPE_1, RCC_APB1Periph_USART2, USART2_IRQn},   {GPIO_AF_1, QM_GPIO_PIN_2, QM_GPIO_PIN_3}, NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
    {{USART3, APBPeriph_TYPE_1, RCC_APB1Periph_USART3, USART3_6_IRQn}, {GPIO_AF_1, QM_GPIO_PIN_42, QM_GPIO_PIN_43},NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
    {{USART4, APBPeriph_TYPE_1, RCC_APB1Periph_USART4, USART3_6_IRQn}, {GPIO_AF_4, QM_GPIO_PIN_0, QM_GPIO_PIN_1},NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
    {{USART5, APBPeriph_TYPE_1, RCC_APB1Periph_USART5, USART3_6_IRQn}, {GPIO_AF_2, QM_GPIO_PIN_44, QM_GPIO_PIN_50},NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
    {{USART6, APBPeriph_TYPE_2, RCC_APB2Periph_USART6, USART3_6_IRQn}, {GPIO_AF_5, QM_GPIO_PIN_4, QM_GPIO_PIN_5},NULL, 512, 0, {0}, 0, NULL, NULL, NULL},
};

static void uartx_irq_recv_data(USART_TypeDef *USARTx, uint8_t id);
static int32_t qm_uart_pin_enable(uint8_t gpio_afx, uint8_t pins);

int32_t qm_uart_init(qm_uart_dev_t *uart)
{

    qm_uart_info_t *info = NULL;
    USART_InitTypeDef USART_InitStruct;
    NVIC_InitTypeDef NVIC_InitStructure;

    if(uart == NULL){
        return -QM_EINVAL;
    }

    if(uart->port >= QM_UART_PORT_MAX){
        return -QM_EINVAL;
    }

    info = &g_uart_info[uart->port];
    USART_DeInit(info->config.USARTx);

    if(info->config.APBPeriph_TYPE == APBPeriph_TYPE_1){
        RCC_APB1PeriphClockCmd(info->config.APBPeriph, ENABLE);
    }else if(info->config.APBPeriph_TYPE == APBPeriph_TYPE_2){
        RCC_APB2PeriphClockCmd(info->config.APBPeriph, ENABLE);
    }

    qm_uart_pin_enable(info->pins_confg.GPIO_AF_x, info->pins_confg.tx_pins);
    qm_uart_pin_enable(info->pins_confg.GPIO_AF_x, info->pins_confg.rx_pins);

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
    }else{
        NVIC_InitStructure.NVIC_IRQChannel = USART3_6_IRQn;
    }

    NVIC_InitStructure.NVIC_IRQChannelPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;

    NVIC_Init(&NVIC_InitStructure);
    USART_Cmd(info->config.USARTx, ENABLE);
    
    info->buf = (uint8_t*)qm_malloc(info->size);
    if(info->buf == NULL){
        return -QM_ENOMEM;
    }
    
    memset(info->buf, 0, info->size);
    qm_ringbuf_init(&info->ringbuf, info->buf, info->size);
    qm_sem_new(&info->rx_sem, 0);

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

            if(t_left == 0){
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

void USART3_6_IRQHandler(void)
{
    uartx_irq_recv_data(USART3, 2);
    uartx_irq_recv_data(USART4, 3);
    uartx_irq_recv_data(USART5, 4);
    uartx_irq_recv_data(USART6, 5);
}




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

static int32_t qm_uart_pin_enable(uint8_t gpio_afx, uint8_t pins)
{
    int ret = QM_EOK;
    GPIO_TypeDef *GPIOx;
    uint32_t AHBPeriph_Clock;
    GPIO_InitTypeDef GPIO_InitStructure;

    switch ((pins / GPIOX_PIN_MAX_NUM))
    {
        case  QM_GPIOA:
            GPIOx = GPIOA;
            AHBPeriph_Clock = RCC_AHBPeriph_GPIOA;
        break;

        case  QM_GPIOB:
            GPIOx = GPIOB;
            AHBPeriph_Clock = RCC_AHBPeriph_GPIOB;
        break;

        case  QM_GPIOC:
			GPIOx = GPIOC;
            AHBPeriph_Clock = RCC_AHBPeriph_GPIOC;
        break;

        case  QM_GPIOD:
			GPIOx = GPIOD;
            AHBPeriph_Clock = RCC_AHBPeriph_GPIOD;
        break;

        default:
           ret = -QM_ERROR;
        break;
    }

    if(ret != QM_EOK){
        return ret;
    }	
		
    RCC_AHBPeriphClockCmd(AHBPeriph_Clock, ENABLE);
    GPIO_PinAFConfig(GPIOx, pins % GPIOX_PIN_MAX_NUM, gpio_afx);
    GPIO_InitStructure.GPIO_Pin = 1 << (pins % GPIOX_PIN_MAX_NUM);
    GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF;
    GPIO_InitStructure.GPIO_OType=GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_PuPd=GPIO_PuPd_UP;
    GPIO_Init(GPIOx, &GPIO_InitStructure);

    return QM_EOK;
}
