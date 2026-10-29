#include "qm.h"
#include "qm_config.h"

#ifdef CONFIG_QM_UART_SUPPORT
#include "qm_uart.h"
#include "qm_platform.h"
#include "stm32g0xx_hal.h"
#include "stm32g0xx_ll_usart.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"

typedef struct{
    USART_TypeDef *USARTx;
    GPIO_TypeDef *GPIOx;
    uint32_t tx_Pin;
    uint32_t rx_Pin;
    uint32_t Alternate;
    IRQn_Type NVIC_IRQChannel;
    UART_HandleTypeDef huart;
}uart_config_t;

typedef struct {
    uart_config_t config;
    uint8_t *buf;
    uint16_t size;
    uint16_t rx_size;
    qm_sem_t rx_sem;
    qm_ringbuf_t ringbuf;
    qm_uart_dev_t *uart_dev;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
}uart_info_t;

static uart_info_t g_uart_info_t[QM_UART_MAX] = {
    {{USART1, GPIOB, GPIO_PIN_6,  GPIO_PIN_7,  GPIO_AF0_USART1, USART1_IRQn, {0}},                NULL, 512, 0, {0},{0}, NULL, NULL, NULL},
    {{USART2, GPIOA, GPIO_PIN_2,  GPIO_PIN_3,  GPIO_AF1_USART2, USART2_LPUART2_IRQn, {0}},        NULL, 512, 0, {0},{0}, NULL, NULL, NULL},
    {{USART3, GPIOB, GPIO_PIN_11, GPIO_PIN_10, GPIO_AF4_USART3, USART3_4_5_6_LPUART1_IRQn, {0}},  NULL, 512, 0, {0},{0}, NULL, NULL, NULL},
    {{USART4, GPIOA, GPIO_PIN_0,  GPIO_PIN_1,  GPIO_AF4_USART4, USART3_4_5_6_LPUART1_IRQn, {0}},  NULL, 512, 0, {0},{0}, NULL, NULL, NULL},
    {{USART5, GPIOD, GPIO_PIN_3,  GPIO_PIN_2,  GPIO_AF3_USART5, USART3_4_5_6_LPUART1_IRQn, {0}},  NULL, 512, 0, {0},{0}, NULL, NULL, NULL},
    {{USART6, GPIOB, GPIO_PIN_8,  GPIO_PIN_9,  GPIO_AF8_USART6, USART3_4_5_6_LPUART1_IRQn, {0}},  NULL, 512, 0, {0},{0}, NULL, NULL, NULL}  
};

static int uart_data_set(qm_uart_port_t uart_port, uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }
    uart_info_t *uart_info = &g_uart_info_t[uart_port];
    uart_info->rx_size += qm_ringbuf_push(&uart_info->ringbuf, data, 1);
    return QM_EOK;
}

static int uart_data_callback(qm_uart_port_t uart_port)
{
    uart_info_t *uart_info = &g_uart_info_t[uart_port];
    
    if(uart_info->read){
        uart_info->read(uart_info->uart_dev, uart_info->rx_size);
    }
    return QM_EOK;
}

void USART1_IRQHandler(void)
{
    uint8_t tmp = 0;
    if (LL_USART_IsActiveFlag_RXNE(USART1) && LL_USART_IsEnabledIT_RXNE(USART1)) { //判断接收数据寄存器是否有数据
   		 //清除接收中断标志方式一：读取RDR寄存器以自动清除RXNE标志
		//清除接收中断标志方式二：往RXFRQ寄存器写1(刷新数据)以清除RXNE中断标志
        tmp = LL_USART_ReceiveData8(USART1);
        uart_data_set(QM_UART1, &tmp, 1);
    }

    if(LL_USART_IsActiveFlag_IDLE(USART1) && LL_USART_IsEnabledIT_IDLE(USART1)){
        LL_USART_ClearFlag_IDLE(USART1);
        uart_data_callback(QM_UART1);    
    }

    HAL_UART_IRQHandler(&g_uart_info_t[QM_UART1].config.huart);
}

void USART2_LPUART2_IRQHandler(void)
{
    uint8_t tmp = 0;
    if (LL_USART_IsActiveFlag_RXNE(USART2) && LL_USART_IsEnabledIT_RXNE(USART2)) { //判断接收数据寄存器是否有数据
   		 //清除接收中断标志方式一：读取RDR寄存器以自动清除RXNE标志
		//清除接收中断标志方式二：往RXFRQ寄存器写1(刷新数据)以清除RXNE中断标志
        tmp = LL_USART_ReceiveData8(USART2);
        uart_data_set(QM_UART2, &tmp, 1);
    }

    if(LL_USART_IsActiveFlag_IDLE(USART2) && LL_USART_IsEnabledIT_IDLE(USART2)){
        LL_USART_ClearFlag_IDLE(USART2);
        uart_data_callback(QM_UART2);    
    }

    HAL_UART_IRQHandler(&g_uart_info_t[QM_UART2].config.huart);
}

void USART3_4_5_6_LPUART1_IRQHandler(void)
{
    uint8_t tmp = 0;
    if (LL_USART_IsActiveFlag_RXNE(USART3) && LL_USART_IsEnabledIT_RXNE(USART3)) { //判断接收数据寄存器是否有数据
   		 //清除接收中断标志方式一：读取RDR寄存器以自动清除RXNE标志
		//清除接收中断标志方式二：往RXFRQ寄存器写1(刷新数据)以清除RXNE中断标志
        tmp = LL_USART_ReceiveData8(USART3);
        uart_data_set(QM_UART3, &tmp, 1);
    }

    if (LL_USART_IsActiveFlag_RXNE(USART4) && LL_USART_IsEnabledIT_RXNE(USART4)) { //判断接收数据寄存器是否有数据
   		 //清除接收中断标志方式一：读取RDR寄存器以自动清除RXNE标志
		//清除接收中断标志方式二：往RXFRQ寄存器写1(刷新数据)以清除RXNE中断标志
        tmp = LL_USART_ReceiveData8(USART4);
        uart_data_set(QM_UART4, &tmp, 1);
    }

    if (LL_USART_IsActiveFlag_RXNE(USART5) && LL_USART_IsEnabledIT_RXNE(USART5)) { //判断接收数据寄存器是否有数据
   		 //清除接收中断标志方式一：读取RDR寄存器以自动清除RXNE标志
		//清除接收中断标志方式二：往RXFRQ寄存器写1(刷新数据)以清除RXNE中断标志
        tmp = LL_USART_ReceiveData8(USART5);
        uart_data_set(QM_UART5, &tmp, 1);
    }

    if (LL_USART_IsActiveFlag_RXNE(USART6) && LL_USART_IsEnabledIT_RXNE(USART6)) { //判断接收数据寄存器是否有数据
   		 //清除接收中断标志方式一：读取RDR寄存器以自动清除RXNE标志
		//清除接收中断标志方式二：往RXFRQ寄存器写1(刷新数据)以清除RXNE中断标志
        tmp = LL_USART_ReceiveData8(USART6);
        uart_data_set(QM_UART6, &tmp, 1);
    }

    if(LL_USART_IsActiveFlag_IDLE(USART3) && LL_USART_IsEnabledIT_IDLE(USART3)){
        LL_USART_ClearFlag_IDLE(USART3);
        uart_data_callback(QM_UART3);    
    }

    if(LL_USART_IsActiveFlag_IDLE(USART4) && LL_USART_IsEnabledIT_IDLE(USART4)){
        LL_USART_ClearFlag_IDLE(USART4);
        uart_data_callback(QM_UART4);    
    }

    if(LL_USART_IsActiveFlag_IDLE(USART5) && LL_USART_IsEnabledIT_IDLE(USART5)){
        LL_USART_ClearFlag_IDLE(USART5);
        uart_data_callback(QM_UART5);    
    }

    if(LL_USART_IsActiveFlag_IDLE(USART6) && LL_USART_IsEnabledIT_IDLE(USART6)){
        LL_USART_ClearFlag_IDLE(USART6);
        uart_data_callback(QM_UART6);    
    }

    HAL_UART_IRQHandler(&g_uart_info_t[QM_UART3].config.huart);
    HAL_UART_IRQHandler(&g_uart_info_t[QM_UART4].config.huart);
    HAL_UART_IRQHandler(&g_uart_info_t[QM_UART5].config.huart);
    HAL_UART_IRQHandler(&g_uart_info_t[QM_UART6].config.huart);
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    UART_HandleTypeDef *huart = NULL;
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    if(uart == NULL){
        return -QM_EINVAL;
    }
    
    if(uart->port >= QM_UART_MAX){
        return -QM_EINVAL;
    }
    
    uart_info_t *uart_info = &g_uart_info_t[uart->port];
    
    switch(uart->port){
        
        case QM_UART1:
            PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART1;
            PeriphClkInit.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK1;
            HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

            __HAL_RCC_USART1_CLK_ENABLE();
            __HAL_RCC_GPIOB_CLK_ENABLE();
        break;

        case QM_UART2:
            
            PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART2;
            PeriphClkInit.Usart2ClockSelection = RCC_USART2CLKSOURCE_PCLK1;
            HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

            __HAL_RCC_USART2_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
        break;

        case QM_UART3:

            PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_USART3;
            PeriphClkInit.Usart3ClockSelection = RCC_USART3CLKSOURCE_PCLK1;
            HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit);

            __HAL_RCC_USART3_CLK_ENABLE();
            __HAL_RCC_GPIOB_CLK_ENABLE();
        break; 
        
        case QM_UART4:
            __HAL_RCC_USART4_CLK_ENABLE();
            __HAL_RCC_GPIOA_CLK_ENABLE();
        break; 
        
        case QM_UART5:
            __HAL_RCC_USART5_CLK_ENABLE();
            __HAL_RCC_GPIOD_CLK_ENABLE();
        break; 
        
        case QM_UART6: 
            __HAL_RCC_USART6_CLK_ENABLE();
            __HAL_RCC_GPIOB_CLK_ENABLE();
        break; 
        
        default:
        break;
    }

    GPIO_InitStruct.Pin = uart_info->config.tx_Pin;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP; 
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = uart_info->config.Alternate;
    HAL_GPIO_Init(uart_info->config.GPIOx, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = uart_info->config.rx_Pin;;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_PP; 
    GPIO_InitStruct.Pull = GPIO_PULLUP; 
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = uart_info->config.Alternate;
    HAL_GPIO_Init(uart_info->config.GPIOx, &GPIO_InitStruct);

    huart = &uart_info->config.huart;
    huart->Instance = uart_info->config.USARTx;
    huart->Init.BaudRate = uart->config.baud_rate;
    
    if(uart->config.data_bits == QM_UART_DATA_8_BITS){
        huart->Init.WordLength = UART_WORDLENGTH_8B;
    }else if(uart->config.data_bits == QM_UART_DATA_7_BITS){
        huart->Init.WordLength = UART_WORDLENGTH_7B;
    }
    
    if(uart->config.flow_ctrl == QM_UART_FLOW_CTRL_NONE){  
        huart->Init.HwFlowCtl = UART_HWCONTROL_NONE;
    }
    
    if(uart->config.stop_bits == QM_UART_STOP_BITS_1){  
        huart->Init.StopBits = UART_STOPBITS_1;
    }else if(uart->config.stop_bits == QM_UART_STOP_BITS_1_5){
        huart->Init.StopBits = UART_STOPBITS_1_5;
    }
    
    if(uart->config.parity == QM_UART_PARITY_NONE){  
        huart->Init.Parity = UART_PARITY_NONE;
    }else if(uart->config.parity == QM_UART_PARITY_EVEN){
        huart->Init.Parity = UART_PARITY_EVEN;
    }else if(uart->config.parity == QM_UART_PARITY_ODD){
        huart->Init.Parity = UART_PARITY_ODD;
    }
    
    huart->Init.Mode = UART_MODE_TX_RX;
    huart->Init.OverSampling = UART_OVERSAMPLING_16;
    huart->Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
    huart->Init.ClockPrescaler = UART_PRESCALER_DIV1;
    huart->AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
    
    if(HAL_UART_Init(huart) != HAL_OK){
        return -QM_EIO;
    }     
    
    if (HAL_UARTEx_SetTxFifoThreshold(huart, UART_TXFIFO_THRESHOLD_1_8) != HAL_OK){
        return -QM_EIO;
    }

    if (HAL_UARTEx_SetRxFifoThreshold(huart, UART_RXFIFO_THRESHOLD_1_8) != HAL_OK){
        return -QM_EIO;
    }

    if (HAL_UARTEx_DisableFifoMode(huart) != HAL_OK){
        return -QM_EIO;
    }

    HAL_NVIC_SetPriority(uart_info->config.NVIC_IRQChannel, 0, 0);
    HAL_NVIC_EnableIRQ(uart_info->config.NVIC_IRQChannel);

    LL_USART_EnableIT_RXNE(huart->Instance);
    LL_USART_EnableIT_IDLE(huart->Instance);

    uart_info->buf = (uint8_t*)qm_malloc(uart_info->size);
    if(uart_info->buf == NULL){
        return -QM_ENOMEM;
    }
    memset(uart_info->buf, 0, uart_info->size);

    qm_sem_new(&uart_info->rx_sem, 0);
    qm_ringbuf_init(&uart_info->ringbuf, uart_info->buf, uart_info->size);

    uart->priv = (void*)uart_info;
    uart_info->uart_dev = uart;
    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

    uart_info_t *uart_info = (uart_info_t *)uart->priv;

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_ringbuf_flush(&uart_info->ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            qm_sem_free(&uart_info->rx_sem);
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

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{ 
    if(uart == NULL || uart->priv == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }
    
    if(timeout == 0){
        timeout = 10000;
    }
    
    uart_info_t *uart_info = (uart_info_t *)uart->priv;
    
    if(HAL_UART_Transmit(&uart_info->config.huart, (uint8_t*)data, size, timeout)!= HAL_OK){
        return -QM_EIO;
    }
    if(uart_info->write){
        uart_info->write(uart_info->uart_dev, size);
    } 
    return size; 
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
    uart_info_t *uart_info = NULL;

    if(uart == NULL || data == NULL || expect_size == 0){
        return -QM_EINVAL;
    }

    if(uart->priv == NULL){
        return -QM_EINVAL;
    }
    
    uart_info = (uart_info_t*)uart->priv;

    if(timeout == QM_WAIT_FOREVER){
        wait = 1;
        t_left = QM_WAIT_FOREVER;
    }else if(timeout){
        qm_utils_time_init(&time);
        qm_utils_time_countdown_ms(&time, timeout);  
    }

    do{
        if(!wait && timeout){
            t_left = qm_utils_time_left(&time);
        }
        
        isempty = qm_ringbuf_isempty(&uart_info->ringbuf);

        if(!isempty){
            
            len = qm_ringbuf_pop(&uart_info->ringbuf, (uint8_t *)data + recv_len, expect_size - recv_len);
            if(len > 0){
                recv_len += len;
                uart_info->rx_size -= len;
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
    }while ((recv_len < expect_size) && (!wait));

    return (0 != recv_len) ? recv_len : ret; 
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    uart_info_t *uart_info = NULL;

    if (uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

    uart_info = (uart_info_t *)uart->priv;   

    LL_USART_DisableIT_RXNE(uart_info->config.huart.Instance);
    LL_USART_DisableIT_IDLE(uart_info->config.huart.Instance);

    HAL_NVIC_DisableIRQ(uart_info->config.NVIC_IRQChannel);

    HAL_UART_DeInit(&uart_info->config.huart);

    if(qm_sem_is_valid(&uart_info->rx_sem)){
        qm_sem_free(&uart_info->rx_sem);
    }

    qm_ringbuf_flush(&uart_info->ringbuf);
    if(uart_info->buf){
        qm_free(uart_info->buf);
        uart_info->buf = NULL;
    }
    return QM_EOK;

}

#endif
