#include "qm.h"
#include "qm_log.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"
#include "qm_spi.h"
#include "qm_errno.h"
#include "qm_gpio.h"

#include "ls_soc_gpio.h"
#include "ls_hal_spi_i2s.h"
#include "ls_hal_ssi.h"
#include "ls_hal_dmac.h"
#include "app_config.h"

#define LOG_TAG "hal"

#define SSI_DMA_BUF_SIZE    (512)

static volatile int rx_flag = 0;
static volatile int tx_flag = 0;
static SSI_HandleTypeDef ssiHandle = {0};
static DEF_DMA_CONTROLLER(dmac1_SSI_inst,DMAC1);
static DMA_RAM_ATTR uint8_t ssi_dma_tx_buf[SSI_DMA_BUF_SIZE] = {0};
static DMA_RAM_ATTR uint8_t ssi_dma_rx_buf[SSI_DMA_BUF_SIZE] = {0};
static uint8_t is_used = QM_FALSE;
void HAL_SSI_TxDMACpltCallback(SSI_HandleTypeDef *hssi)
{
    tx_flag = 1;
}

void HAL_SSI_RxDMACpltCallback(SSI_HandleTypeDef *hssi)
{
    rx_flag = 1;
}

int32_t qm_spi_init(qm_spi_dev_t *spi)
{
	int ret = QM_EOK;
    uint8_t clk_div_val = 0;
    SSI_HandleTypeDef *ssiHandle_info = NULL;

    if(spi == NULL || spi->port > 0){
        return -QM_EINVAL;
    }

    ssiHandle_info = &ssiHandle;

    if (is_used == QM_TRUE)
    {
        spi->priv = (void*)ssiHandle_info;
        return QM_EOK;
    }

    if(spi->pins.cs_pin >= 0){
        pinmux_ssi_nss0_init((uint8_t)spi->pins.cs_pin);      /* SSN */
    }
    
    pinmux_ssi_clk_init((uint8_t)spi->pins.sck_pin);     /* CLK */
    pinmux_ssi_dq0_init((uint8_t)spi->pins.mosi_pin);    /* MOSI */
    pinmux_ssi_dq1_init((uint8_t)spi->pins.miso_pin);    /* MISO */
    //REVIEW 我们现在的使用场景下，应该将cs脚交给app控制。

	ssiHandle_info->REG = LSSSI;
    clk_div_val = ( (SDK_HCLK_MHZ * 1000 *1000) / (spi->config.freq) );
    ssiHandle_info->Init.clk_div = clk_div_val;
	ssiHandle_info->Init.rxsample_dly = 0;

    switch (spi->config.data_width)
    {
        case QM_SPI_DATA_WIDTH_4B:
	        ssiHandle_info->Init.ctrl.data_frame_size = DFS_32_4_bits;
        break;
        case QM_SPI_DATA_WIDTH_7B:
	        ssiHandle_info->Init.ctrl.data_frame_size = DFS_32_7_bits;
        break;
        case QM_SPI_DATA_WIDTH_8B:
	        ssiHandle_info->Init.ctrl.data_frame_size = DFS_32_8_bits;
        break;
        case QM_SPI_DATA_WIDTH_9B:
	        ssiHandle_info->Init.ctrl.data_frame_size = DFS_32_9_bits;
        break;
        case QM_SPI_DATA_WIDTH_16B:
	        ssiHandle_info->Init.ctrl.data_frame_size = DFS_32_16_bits;
        break;
        default:
            ret = -QM_ERROR;      
        break;
    }

    switch (spi->config.clock_mode)
    {
        case QM_SPI_CLK_POL_LOW_PHA_1E:
            ssiHandle_info->Init.ctrl.cpol = Inactive_Low;
            //REVIEW 使用SCLK_Toggle_In_Middle
            ssiHandle_info->Init.ctrl.cph = SCLK_Toggle_In_Middle;
        break;

        case QM_SPI_CLK_POL_LOW_PHA_2E:
            ssiHandle_info->Init.ctrl.cpol = Inactive_Low;
            //REVIEW SCLK_Toggle_At_Start
            ssiHandle_info->Init.ctrl.cph = SCLK_Toggle_At_Start ;

        break;

        case QM_SPI_CLK_POL_HIGH_PHA_1E:
            ssiHandle_info->Init.ctrl.cpol = Inactive_High;
            //REVIEW 同理
            ssiHandle_info->Init.ctrl.cph = SCLK_Toggle_In_Middle;    
        break;

        case QM_SPI_CLK_POL_HIGH_PHA_2E:
            ssiHandle_info->Init.ctrl.cpol = Inactive_High;
            //REVIEW 同理
            ssiHandle_info->Init.ctrl.cph = SCLK_Toggle_At_Start;     
        break;

        default:
            ret = -QM_ERROR;      
        break;
    }

    DMA_CONTROLLER_INIT(dmac1_SSI_inst);
	ssiHandle_info->DMAC_Instance = &dmac1_SSI_inst;
    // REVIEW DMA通道和串口一致。
	ssiHandle_info->Tx_Env.DMA.DMA_Channel = 0;
	ssiHandle_info->Rx_Env.DMA.DMA_Channel = 1;
	if(HAL_SSI_Init(ssiHandle_info) != HAL_OK){
        QM_LOGI(LOG_TAG, "ssi init fail");
        return -QM_ERROR;
    }

    spi->priv = (void *)ssiHandle_info;
    is_used = QM_TRUE;
    return ret;
}

int32_t qm_spi_write(qm_spi_dev_t *spi, const uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef ret = HAL_OK;
    uint32_t t_left = 0;
    qm_utils_time_t timer = {0};
    qm_utils_time_countdown_ms(&timer, timeout);  

    SSI_HandleTypeDef *ssiHandle_info = NULL;
		
    if (!is_used)
    {
            return -QM_EINIT;
    }
    
    if(spi == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }
    ssiHandle_info = (SSI_HandleTypeDef *)spi->priv;

    while (tx_flag)
    {
        qm_usleep(10);
        t_left = qm_utils_time_left(&timer);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }

    //REVIEW 为啥不用memcpy
    memcpy(ssi_dma_tx_buf, data, size);

    ret = HAL_SSI_Transmit_DMA(ssiHandle_info, ssi_dma_tx_buf, size);
    if (ret != HAL_OK) {
        return -QM_ERROR;
    }

    qm_utils_time_countdown_ms(&timer, timeout);  
    while (!tx_flag)
    {
        qm_usleep(10);
        t_left = qm_utils_time_left(&timer);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }
    tx_flag = 0;
    //REVIEW 没有使用发送完成回调判断

    return QM_EOK;
}

int32_t qm_spi_read(qm_spi_dev_t *spi, uint8_t *data, uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef ret = HAL_OK;
    SSI_HandleTypeDef *ssiHandle_info = NULL;
    uint32_t t_left = 0;
    qm_utils_time_t timer = {0};
    qm_utils_time_countdown_ms(&timer, timeout);  

    if (!is_used)
    {
            return -QM_EINIT;
    }
		
    if(spi == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }
    ssiHandle_info = (SSI_HandleTypeDef *)spi->priv;

    //REVIEW 得用DMA_RAM_ATTR声明的变量作为接收缓存。
    while (rx_flag)
    {
        qm_usleep(10);
        t_left = qm_utils_time_left(&timer);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }
    
    memset(ssi_dma_rx_buf, 0, SSI_DMA_BUF_SIZE);
    ret = HAL_SSI_Receive_DMA(ssiHandle_info, ssi_dma_rx_buf, size);
    if (ret != HAL_OK) {
        return -QM_ERROR;
    }
        
    while (!rx_flag)
    {
        qm_usleep(10);
        t_left = qm_utils_time_left(&timer);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }
    memcpy(data, ssi_dma_rx_buf, size);

    rx_flag = 0;

    return QM_EOK;
}

int32_t qm_spi_finalize(qm_spi_dev_t *spi)
{
    SSI_HandleTypeDef *ssiHandle_info = NULL;
    DMA_Controller_HandleTypeDef *DMAC_Instance = NULL;
    if(spi == NULL || spi->port > 0){
        return -QM_EINVAL;
    }

    rx_flag = 0;
    tx_flag = 0;
    is_used = false;
    ssiHandle_info = (SSI_HandleTypeDef *)spi->priv;
    DMAC_Instance = (DMA_Controller_HandleTypeDef *)ssiHandle_info->DMAC_Instance;
    if(HAL_SSI_Deinit(ssiHandle_info) != HAL_OK){
        QM_LOGI(LOG_TAG, "ssi deinit fail");
        return -QM_ERROR;
    }

    HAL_DMA_Controller_DeInit(DMAC_Instance);

    pinmux_ssi_clk_deinit();
    pinmux_ssi_dq0_deinit();
    pinmux_ssi_dq1_deinit();

    return QM_EOK;
}