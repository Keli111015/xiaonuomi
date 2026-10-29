#include "qm_spi.h"
#include "qm_errno.h"

#include "stm32f0xx.h"
#include "stm32f0xx_gpio.h"
#include "stm32f0xx_spi.h"
#include "stm32f0xx_dma.h"
#include "stm32f0xx_rcc.h"

#include "FreeRTOS.h"
#include "task.h"

#include "qm.h"
#include "qm_ringbuf.h"
#include "qm_platform.h"
#include "qm_utils_timer.h"

#define GPIOX_PIN_MAX_NUM       (16) // GPIOPIN0 ~ GPIO15 
#define GPIOX_TYPEDEF_MAX_NUM   (3) // GPIOA GPIOB GPIOC 

#define SPI_CMD_WRITE    (0x34)
#define SPI_CMD_READ     (0x12)

typedef enum{
    QM_GPIOA = 0,
    QM_GPIOB = 1,
    QM_GPIOC = 2,
}gpio_typedef_t;

typedef struct{
    SPI_TypeDef *SPIx;
    uint32_t APBPeriph;
    IRQn_Type NVIC_IRQChannel;
    SPI_InitTypeDef SPI_InitStructure;
}spi_config_t;

typedef struct{
    GPIO_TypeDef *GPIOx;
    uint16_t pin;
}cs_config_t;

typedef struct{
    spi_config_t config;
    cs_config_t cs;
    uint32_t size;
    qm_sem_t tx_sem;
    qm_mutex_t tx_mutex;
    uint8_t *tx_buf;
    qm_ringbuf_t tx_ringbuf;

    uint8_t *rx_buf;
    qm_mutex_t rx_mutex;
    qm_ringbuf_t rx_ringbuf;
    qm_sem_t rx_sem;
    
    uint8_t is_wait;
    qm_spi_cb_t cb;
}qm_spi_info_t;

static qm_spi_info_t g_spi_info[] = {
    {{SPI1, RCC_APB2Periph_SPI1, SPI1_IRQn, {0}}, {0}, 256, {0}, {0}, NULL, {0}, NULL, {0}, {0}, {0}, 0, NULL},
    {{SPI2, RCC_APB1Periph_SPI2, SPI2_IRQn, {0}}, {0}, 1024, {0}, {0}, NULL, {0}, NULL, {0}, {0}, {0}, 0, NULL},
};

static int32_t qm_spi_pin_enable(qm_spi_info_t *spi, uint8_t pins, uint8_t is_cs);

int32_t qm_spi_init(qm_spi_dev_t *spi)
{
	int ret = QM_EOK;
    qm_spi_info_t *spi_info = NULL;
    NVIC_InitTypeDef NVIC_InitStructure;    

    if(spi == NULL || spi->port > 1){
        return -QM_EINVAL;
    }

    spi_info = &g_spi_info[spi->port];

    if(spi->config.mode == QM_SPI_MODE_MASTER){
        spi_info->config.SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
    }else{
        spi_info->config.SPI_InitStructure.SPI_Mode = SPI_Mode_Slave;
    }

    if(spi->config.data_shift == QM_SPI_DATA_SHIFT_MSB){
        spi_info->config.SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
    }else{
        spi_info->config.SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_LSB;
    }

    switch (spi->config.data_width)
    {
        case QM_SPI_DATA_WIDTH_4B:
            spi_info->config.SPI_InitStructure.SPI_DataSize = SPI_DataSize_4b;
        break;
        case QM_SPI_DATA_WIDTH_7B:
            spi_info->config.SPI_InitStructure.SPI_DataSize = SPI_DataSize_7b;
        break;
        case QM_SPI_DATA_WIDTH_8B:
            spi_info->config.SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
        break;
        case QM_SPI_DATA_WIDTH_9B:
            spi_info->config.SPI_InitStructure.SPI_DataSize = SPI_DataSize_9b;
        break;
        case QM_SPI_DATA_WIDTH_16B:
            spi_info->config.SPI_InitStructure.SPI_DataSize = SPI_DataSize_16b;
        break;           
        default:
            ret = -QM_EIO;    
        break;
    }
		
    if(ret != QM_EOK){
        return ret;
    }	
		
    switch (spi->config.clock_mode)
    {
        case QM_SPI_CLK_POL_LOW_PHA_1E:
            spi_info->config.SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
            spi_info->config.SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
        break;
        case QM_SPI_CLK_POL_LOW_PHA_2E:
            spi_info->config.SPI_InitStructure.SPI_CPOL = SPI_CPOL_Low;
            spi_info->config.SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
        break;
        case QM_SPI_CLK_POL_HIGH_PHA_1E:
            spi_info->config.SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
            spi_info->config.SPI_InitStructure.SPI_CPHA = SPI_CPHA_1Edge;
        break;
        case QM_SPI_CLK_POL_HIGH_PHA_2E:
            spi_info->config.SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
            spi_info->config.SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
        break;         
        default:
            ret = -QM_EIO;      
        break;
    }
		
    if(ret != QM_EOK){
        return ret;
    }	
		
    if(spi->port == 1){
        RCC_APB1PeriphClockCmd(spi_info->config.APBPeriph, ENABLE);
    }else if (spi->port == 0){
        RCC_APB2PeriphClockCmd(spi_info->config.APBPeriph, ENABLE);
    }
    
    /* Enable SCK, MOSI, MISO and NSS GPIO clocks */
    qm_spi_pin_enable(spi_info, spi->pins.mosi_pin, QM_FALSE);
    qm_spi_pin_enable(spi_info, spi->pins.miso_pin, QM_FALSE);
    qm_spi_pin_enable(spi_info, spi->pins.sck_pin, QM_FALSE);
    qm_spi_pin_enable(spi_info, spi->pins.cs_pin, QM_TRUE);

    SPI_I2S_DeInit(spi_info->config.SPIx);
    spi_info->config.SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi_info->config.SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
    spi_info->config.SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_2;
    spi_info->config.SPI_InitStructure.SPI_CRCPolynomial = 7;

    /* Configure the SPI interrupt priority */
    NVIC_InitStructure.NVIC_IRQChannel = spi_info->config.NVIC_IRQChannel;
    NVIC_InitStructure.NVIC_IRQChannelPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);


    spi_info->tx_buf = (uint8_t*)qm_malloc(spi_info->size);
    if(spi_info->tx_buf == NULL){
        ret = -QM_ENOMEM;
        goto _error;
    }
    
    memset(spi_info->tx_buf, 0, spi_info->size);
    qm_ringbuf_init(&spi_info->tx_ringbuf, spi_info->tx_buf, spi_info->size);

    spi_info->rx_buf = (uint8_t*)qm_malloc(spi_info->size);
    if(spi_info->tx_buf == NULL){
        ret = -QM_ENOMEM;
        goto _error;
    }

    qm_mutex_new(&spi_info->rx_mutex);
    qm_ringbuf_init(&spi_info->rx_ringbuf, spi_info->rx_buf, spi_info->size);
    ret = qm_sem_new(&spi_info->rx_sem, 0);
    if(ret != QM_EOK){
        goto _error;
    }

    spi->priv = (void*)spi_info;

    SPI_Init(spi_info->config.SPIx, &spi_info->config.SPI_InitStructure);
    SPI_RxFIFOThresholdConfig(spi_info->config.SPIx, SPI_RxFIFOThreshold_QF);
    /* Enable the Rx buffer not empty interrupt */
    // SPI_I2S_ITConfig(spi_info->config.SPIx, SPI_I2S_IT_TXE, ENABLE);

    SPI_I2S_ITConfig(spi_info->config.SPIx, SPI_I2S_IT_RXNE, ENABLE);
    /* Enable the SPI Error interrupt */
    SPI_I2S_ITConfig(spi_info->config.SPIx, SPI_I2S_IT_ERR, ENABLE);
    /* Enable the SPI peripheral */
    SPI_Cmd(spi_info->config.SPIx, ENABLE);

    return QM_EOK;

_error:
    if(spi_info->rx_buf){
        qm_free(spi_info->rx_buf);
        spi_info->rx_buf = NULL;
    }

    if(spi_info->tx_buf){
        qm_free(spi_info->tx_buf);
        spi_info->rx_buf = NULL;
    }  

    if(qm_sem_is_valid(&spi_info->rx_sem)){
        qm_sem_free(&spi_info->rx_sem);
    }
    return ret;
}

int32_t qm_spi_send(qm_spi_dev_t *spi, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    qm_spi_info_t *spi_info = (qm_spi_info_t *)spi->priv;

    if(spi == NULL || spi->priv == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }
    
    qm_ringbuf_push(&spi_info->tx_ringbuf, (uint8_t *)data, (int)size);
    spi_info->cs.GPIOx->BSRR = spi_info->cs.pin;

    return ret;
}


int32_t qm_spi_recv(qm_spi_dev_t *spi, uint8_t *data, uint16_t size, uint32_t timeout)
{
    int len = 0;
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_left = 0;
    uint8_t isempty = 0;
    qm_utils_time_t time = {0};
    qm_spi_info_t *spi_info = NULL;

    if(spi == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    if(spi->priv == NULL){
        return -QM_EINVAL;
    }

    if(timeout >= (UINT32_MAX / 2)){
        timeout = (UINT32_MAX / 2);
    }

    spi_info = (qm_spi_info_t *)spi->priv;
    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);  

    do{
        t_left = qm_utils_time_left(&time);
        
        isempty = qm_ringbuf_isempty(&spi_info->rx_ringbuf);
        
        if(!isempty){
            len = qm_ringbuf_pop(&spi_info->rx_ringbuf, (uint8_t *)data + recv_len, size-recv_len);
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

            ret = qm_sem_wait(&spi_info->rx_sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }            
    }while ((recv_len < size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret; 
}


int32_t qm_spi_send_recv_cb_reg(qm_spi_dev_t *spi, qm_spi_cb_t cb)
{
    qm_spi_info_t *spi_info = (qm_spi_info_t *)spi->priv;
    
    if(spi == NULL || spi->priv == NULL || cb == NULL){
        return -QM_EINVAL;
    }

    spi_info->cb = cb;
    return QM_EOK;
}

void SPI2_IRQHandler( void )
{
/* SPI in Slave Tramitter mode--------------------------------------- */
    uint8_t CmdReceived = 0;
    int isempty = 0;
    int isfull = 0;
    static int count = 0;
    qm_spi_info_t *spi_info = &g_spi_info[1];
    /* SPI in Slave Receiver mode--------------------------------------- */
    if (SPI_I2S_GetITStatus(SPI2, SPI_I2S_IT_RXNE) == SET)
    {
        CmdReceived = *(__IO uint8_t*)&(SPI2->DR);
        qm_ringbuf_push(&spi_info->rx_ringbuf, &CmdReceived, 1);
        spi_info->cb(QM_SPI_EVENT_RC, &CmdReceived, 1);
    }
    if (SPI_I2S_GetITStatus(SPI2, SPI_I2S_IT_OVR) == SET)
    {
        CmdReceived = *(__IO uint8_t*)&(SPI2->DR);
        qm_ringbuf_push(&spi_info->rx_ringbuf, &CmdReceived, 1);
        spi_info->cb(QM_SPI_EVENT_RC, &CmdReceived, 1);
    }

    CmdReceived = 0;
    isempty = qm_ringbuf_isempty(&spi_info->tx_ringbuf);
    if(!isempty){
        spi_info->cs.GPIOx->BSRR = spi_info->cs.pin;
        qm_ringbuf_pop(&spi_info->tx_ringbuf, &CmdReceived, 1);
        *(__IO uint8_t*)&(SPI2->DR) = CmdReceived;
    }else{
        *(__IO uint8_t*)&(SPI2->DR) = CmdReceived;
        if(count++ > 5){
            count = 0;
            spi_info->cs.GPIOx->BRR = spi_info->cs.pin;
        }
    }
}


static int32_t qm_spi_pin_enable(qm_spi_info_t *spi, uint8_t pins, uint8_t is_cs)
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

        default:
           ret = -QM_ERROR;
        break;
    }

    if(ret != QM_EOK){
        return ret;
    }	
		
    RCC_AHBPeriphClockCmd(AHBPeriph_Clock, ENABLE);
  
    if(is_cs){
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
        spi->cs.GPIOx = GPIOx;
        spi->cs.pin = 1 << (pins % GPIOX_PIN_MAX_NUM);
    }else{
        GPIO_PinAFConfig(GPIOx, pins % GPIOX_PIN_MAX_NUM, GPIO_AF_0);
        GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
    }

    GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_DOWN;
    GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_Level_3;

    GPIO_InitStructure.GPIO_Pin = 1 << (pins % GPIOX_PIN_MAX_NUM);
    GPIO_Init(GPIOx, &GPIO_InitStructure);

    return QM_EOK;
}
