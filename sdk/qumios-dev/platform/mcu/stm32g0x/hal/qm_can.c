#include "qm.h"
#include "qm_platform.h"

#if CONFIG_QM_CAN_SUPPORT
#include "qm_can.h"
#include "qm_ringbuf.h"

#include "stm32g0xx_hal.h"
#include "stm32g0xx_hal_conf.h"
#include "stm32g0xx_hal_fdcan.h"

#include "FreeRTOS.h"
#include "portmacro.h"

#define LOG_TAG "hal"

#define CONFIG_CAN_BUFF_SIZE    (200)

typedef struct 
{
    FDCAN_RxHeaderTypeDef RxHeader;
    uint8_t RxData[FDCAN_DLC_BYTES_8];
}qm_can_data_handle_t;

typedef struct 
{
    FDCAN_GlobalTypeDef *Instance;
    uint32_t Alternate;
    IRQn_Type IRQn;

    qm_gpio_pin_t tx_pin;
    qm_gpio_pin_t rx_pin;

    FDCAN_HandleTypeDef hfdcan;

    qm_queue_t rx_queue;

    qm_can_dev_t *can_dev;
    qm_can_receive_callback_t recv_cb;
    qm_can_transmit_callback_t transmit_cb;

}qm_can_hal_info_t;

static qm_can_hal_info_t g_can_info[] = 
{
    {FDCAN1, GPIO_AF3_FDCAN1, TIM16_FDCAN_IT0_IRQn, QM_GPIO_PIN_NONE, QM_GPIO_PIN_NONE, {0}, {0}, NULL, NULL, NULL},
    {FDCAN2, GPIO_AF3_FDCAN2, TIM16_FDCAN_IT0_IRQn, QM_GPIO_PIN_NONE, QM_GPIO_PIN_NONE, {0}, {0}, NULL, NULL, NULL},
};

void TIM16_FDCAN_IT0_IRQHandler(void)
{
    qm_can_hal_info_t *can_info = &g_can_info[0];

    HAL_FDCAN_IRQHandler(&can_info->hfdcan);
}

void HAL_FDCAN_MspInit(FDCAN_HandleTypeDef* hfdcan)
{   
    int i = 0;
    qm_can_hal_info_t *can_info = NULL;
    RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

    for(i = 0; i < QM_ARRAY_SIZE(g_can_info); i++)
    {
        if(hfdcan->Instance== g_can_info[i].Instance){
            can_info = &g_can_info[i];
            break;
        }
    }

    if(can_info == NULL){
        return;
    }

    PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_FDCAN;
    PeriphClkInit.FdcanClockSelection = RCC_FDCANCLKSOURCE_PCLK1;

    if(HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK){
        return;
    }

    /* Peripheral clock enable */
    __HAL_RCC_FDCAN_CLK_ENABLE();

    qm_platform_gpio_set_alternate_function(can_info->tx_pin, 
                                            QM_GPIO_MODE_OUTPUT,
                                            QM_GPIO_FLOATING, 
                                            can_info->Alternate);

    qm_platform_gpio_set_alternate_function(can_info->rx_pin, 
                                            QM_GPIO_MODE_INPUT,
                                            QM_GPIO_FLOATING, 
                                            can_info->Alternate);

    HAL_NVIC_SetPriority(can_info->IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(can_info->IRQn);
}


/**
  * @brief  Rx FIFO 0 callback.
  * @param  hfdcan pointer to an FDCAN_HandleTypeDef structure that contains
  *         the configuration information for the specified FDCAN.
  * @param  RxFifo0ITs indicates which Rx FIFO 0 interrupts are signaled.
  *         This parameter can be any combination of @arg FDCAN_Rx_Fifo0_Interrupts.
  * @retval None
  */
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *hfdcan, uint32_t RxFifo0ITs)
{
    uint32_t recv_size = 0;
    qm_can_data_handle_t data_handle = {0};
    qm_can_hal_info_t *can_info = &g_can_info[0];

    if( (RxFifo0ITs & FDCAN_IT_RX_FIFO0_NEW_MESSAGE) != 0){
        if (HAL_FDCAN_GetRxMessage(hfdcan, FDCAN_RX_FIFO0, &data_handle.RxHeader, data_handle.RxData) != HAL_OK){
            return;
        }

        qm_queue_send_from_isr(&can_info->rx_queue, &data_handle, sizeof(qm_can_data_handle_t));

        if(can_info->recv_cb){
            can_info->recv_cb(can_info->can_dev);
        }

    }
}


    /*                    位时间特性配置
            Bit time parameter         | Nominal      |  Data
            ---------------------------|--------------|----------------
            fdcan_ker_ck               | 20 MHz       | 20 MHz
            Time_quantum (tq)          | 50 ns        | 50 ns
            Synchronization_segment    | 1 tq         | 1 tq
            Propagation_segment        | 23 tq        | 1 tq
            Phase_segment_1            | 8 tq         | 4 tq
            Phase_segment_2            | 8 tq         | 4 tq
            Synchronization_Jump_width | 8 tq         | 4 tq
            Bit_length                 | 40 tq = 2us  | 10 tq = 0.5us
            Bit_rate                   | 0.5 MBit/s   | 2 MBit/s
    */

/**
 * 
 *
 * @param[in]  
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_can_init(qm_can_dev_t *can_dev)
{
    int ret = QM_EOK;
    qm_can_hal_info_t *can_info = NULL;
    FDCAN_FilterTypeDef sFilterConfig = {0};

    QM_RETURN_ON_FALSE(can_dev, -QM_EINVAL, LOG_TAG, "can dev inval");

    if(can_dev->port > 1){
        return -QM_EINVAL;
    }

    can_info = &g_can_info[can_dev->port];

    can_info->tx_pin = (qm_gpio_pin_t)can_dev->can_config.tx_pin;
    can_info->rx_pin = (qm_gpio_pin_t)can_dev->can_config.rx_pin;

    can_info->hfdcan.Instance = can_info->Instance;
    can_info->hfdcan.Init.ClockDivider = FDCAN_CLOCK_DIV1;
    can_info->hfdcan.Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    can_info->hfdcan.Init.AutoRetransmission = DISABLE;
    can_info->hfdcan.Init.TransmitPause = DISABLE;
    can_info->hfdcan.Init.ProtocolException = DISABLE;
    can_info->hfdcan.Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    can_info->hfdcan.Init.StdFiltersNbr = 1;
    can_info->hfdcan.Init.ExtFiltersNbr = 1;
    switch (can_dev->can_config.mode)
    {
        case QM_CAN_MODE_NORMAL:
        {
            can_info->hfdcan.Init.Mode = FDCAN_MODE_NORMAL;
        }
        break;
        default:
        break;
    }

    /*
            配置仲裁阶段波特率
            CAN时钟20MHz时，仲裁阶段的波特率就是
            CAN FD Freq / (Sync_Seg + Pro_Seg + Phase_Seg1 + Phase_Seg2) = 20MHz / （1+0x1F + 8） = 0.5Mbps        
            
            其中Sync_Seg是固定值 = 1 ， Pro_Seg + Phase_Seg1 = NominalTimeSeg1， Phase_Seg2 = NominalTimeSeg2
    */

    /*
            配置数据阶段波特率
            CAN时钟20MHz时，数据阶段的波特率就是
            CAN FD Freq / (Sync_Seg + Pro_Seg + Phase_Seg1 + Phase_Seg2) = 20MHz / （1+5+ 4） = 2Mbps
            
            其中Sync_Seg是固定值 = 1 ， Pro_Seg + Phase_Seg1 = DataTimeSeg1， Phase_Seg2 = DataTimeSeg2
    */

    switch (can_dev->can_config.clock)
    {
        case QM_CAN_CLOCK_1MHZ:
        {
            can_info->hfdcan.Init.NominalPrescaler = 4;     /* CAN时钟分配设置，一般设置为1即可，全部由PLL配置好，tq = NominalPrescaler x (1/ fdcan_ker_ck) */
            can_info->hfdcan.Init.NominalSyncJumpWidth = 1;  /* 用于动态调节  Phase_Seg1和 Phase_Seg1，所以不可以比Phase_Seg1和 Phase_Seg1大 */
            can_info->hfdcan.Init.NominalTimeSeg1 = 10;     /* 特别注意这里的Seg1，这里是两个参数之和，对应位时间特性图的 Pro_Seg + Phase_Seg1 */
            can_info->hfdcan.Init.NominalTimeSeg2 = 5;      /* 对应位时间特性图的 Phase_Seg2 */
            
            can_info->hfdcan.Init.DataPrescaler = 4;        /* CAN时钟分配设置，一般设置为1即可，全部由PLL配置好，tq = NominalPrescaler x (1/ fdcan_ker_ck)，范围1-32 */
            can_info->hfdcan.Init.DataSyncJumpWidth = 1;    /* 用于动态调节  Phase_Seg1和 Phase_Seg1，所以不可以比Phase_Seg1和 Phase_Seg1大，范围1-16 */
            can_info->hfdcan.Init.DataTimeSeg1 = 10;        /* 特别注意这里的Seg1，这里是两个参数之和，对应位时间特性图的 Pro_Seg + Phase_Seg1，范围 */
            can_info->hfdcan.Init.DataTimeSeg2 = 5;          /* 对应位时间特性图的 Phase_Seg2 */   
        }
        break;
        
        case QM_CAN_CLOCK_500KHZ:
        {
            can_info->hfdcan.Init.NominalPrescaler = 4;     /* CAN时钟分配设置，一般设置为1即可，全部由PLL配置好，tq = NominalPrescaler x (1/ fdcan_ker_ck) */
            can_info->hfdcan.Init.NominalSyncJumpWidth = 2;  /* 用于动态调节  Phase_Seg1和 Phase_Seg1，所以不可以比Phase_Seg1和 Phase_Seg1大 */
            can_info->hfdcan.Init.NominalTimeSeg1 = 20;     /* 特别注意这里的Seg1，这里是两个参数之和，对应位时间特性图的 Pro_Seg + Phase_Seg1 */
            can_info->hfdcan.Init.NominalTimeSeg2 = 10;      /* 对应位时间特性图的 Phase_Seg2 */
            
            can_info->hfdcan.Init.DataPrescaler = 4;        /* CAN时钟分配设置，一般设置为1即可，全部由PLL配置好，tq = NominalPrescaler x (1/ fdcan_ker_ck)，范围1-32 */
            can_info->hfdcan.Init.DataSyncJumpWidth = 2;    /* 用于动态调节  Phase_Seg1和 Phase_Seg1，所以不可以比Phase_Seg1和 Phase_Seg1大，范围1-16 */
            can_info->hfdcan.Init.DataTimeSeg1 = 20;        /* 特别注意这里的Seg1，这里是两个参数之和，对应位时间特性图的 Pro_Seg + Phase_Seg1，范围 */
            can_info->hfdcan.Init.DataTimeSeg2 = 10;          /* 对应位时间特性图的 Phase_Seg2 */   
        }
        break;

        default:
        break;
    }

    if (HAL_FDCAN_Init(&can_info->hfdcan) != HAL_OK){
        QM_LOGE(LOG_TAG, "HAL_FDCAN_Init ERROR\n");
        return -QM_EIO;
    }

    sFilterConfig.IdType = FDCAN_STANDARD_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = can_dev->filter_config.acceptance_code; //过滤器
    sFilterConfig.FilterID2 = can_dev->filter_config.acceptance_mask; //掩码器

    sFilterConfig.IdType = FDCAN_EXTENDED_ID;
    sFilterConfig.FilterIndex = 0;
    sFilterConfig.FilterType = FDCAN_FILTER_MASK;
    sFilterConfig.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    sFilterConfig.FilterID1 = can_dev->filter_config.acceptance_code; //过滤器
    sFilterConfig.FilterID2 = (~(can_dev->filter_config.acceptance_mask)) & 0x1FFFFFFF; //掩码器

    if (HAL_FDCAN_ConfigFilter(&can_info->hfdcan, &sFilterConfig) != HAL_OK){
        QM_LOGE(LOG_TAG, "HAL_FDCAN_ConfigFilter ERROR\n");
        return -QM_EIO;
    }

    if(HAL_FDCAN_ConfigGlobalFilter(&can_info->hfdcan, FDCAN_REJECT, FDCAN_REJECT, FDCAN_FILTER_REMOTE, FDCAN_FILTER_REMOTE) != HAL_OK){
        QM_LOGE(LOG_TAG, "HAL_FDCAN_ConfigGlobalFilter ERROR\n");
        return -QM_EIO;
    }

    if (HAL_FDCAN_ActivateNotification(&can_info->hfdcan, FDCAN_IT_RX_FIFO0_NEW_MESSAGE, 0) != HAL_OK){
        QM_LOGD(LOG_TAG, "HAL_FDCAN_ActivateNotification RX ERROR\n");
        return -QM_EIO;
    }

    if (HAL_FDCAN_Start(&can_info->hfdcan) != HAL_OK){
        QM_LOGD(LOG_TAG, "HAL_FDCAN_Start ERROR\n");
        return -QM_EIO;
    }

    qm_queue_new(&can_info->rx_queue, CONFIG_CAN_BUFF_SIZE, sizeof(qm_can_data_handle_t));

    can_info->can_dev = can_dev;
    can_dev->priv = (void *)can_info;
    
    return QM_EOK;
}

/**
 * 
 *
 * @param[in]  
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_can_ctrl(qm_can_dev_t *can_dev, qm_can_ctrl_cmd_t cmd, void *param)
{
    qm_can_hal_info_t *can_info = NULL;
    QM_RETURN_ON_FALSE(can_dev, -QM_EINVAL, LOG_TAG, "can dev inval");
    QM_RETURN_ON_FALSE(can_dev->priv, -QM_EINVAL, LOG_TAG, "can dev inval");

    can_info = (qm_can_hal_info_t *)can_dev->priv;

    switch (cmd)
    {
        case QM_CAN_CTRL_CMD_RECEIVE_CLEAR:
            // xQueueReset(can_info->rx_queue);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            can_info->recv_cb = (qm_can_receive_callback_t)param;
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            can_info->transmit_cb = (qm_can_transmit_callback_t)param;
        break;       
        default:
        break;
    }
    return QM_EOK;
}

/**
 * .
 *
 * @param[in]  
 */
int32_t qm_can_write(qm_can_dev_t *can_dev, const qm_can_message_t *message, int timeout)
{
    int ret = QM_EOK;
    qm_can_hal_info_t *can_info = NULL;
    FDCAN_TxHeaderTypeDef TxHeader = {0};

    QM_RETURN_ON_FALSE(can_dev, -QM_EINVAL, LOG_TAG, "can dev inval");
    QM_RETURN_ON_FALSE(can_dev->priv, -QM_EINVAL, LOG_TAG, "can dev inval");
    QM_RETURN_ON_FALSE(message, -QM_EINVAL, LOG_TAG, "can tx message inval");

    can_info = (qm_can_hal_info_t *)can_dev->priv;
    
    TxHeader.Identifier = message->identifier;
    TxHeader.IdType = (message->extd ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID);
    TxHeader.TxFrameType = FDCAN_DATA_FRAME;
    TxHeader.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
    TxHeader.BitRateSwitch = FDCAN_BRS_OFF;
    TxHeader.FDFormat = FDCAN_CLASSIC_CAN;
    TxHeader.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
    TxHeader.MessageMarker = 0;

    switch(message->data_length)
    {
        case 1:
            TxHeader.DataLength = FDCAN_DLC_BYTES_1;
        break;
        case 2:
            TxHeader.DataLength = FDCAN_DLC_BYTES_2;
        break;
        case 3:
            TxHeader.DataLength = FDCAN_DLC_BYTES_3;
        break;
        case 4:
            TxHeader.DataLength = FDCAN_DLC_BYTES_4;
        break;
        case 5:
            TxHeader.DataLength = FDCAN_DLC_BYTES_5;
        break;
        case 6:
            TxHeader.DataLength = FDCAN_DLC_BYTES_6;
        break;
        case 7:
            TxHeader.DataLength = FDCAN_DLC_BYTES_7;
        break;
        case 8:
            TxHeader.DataLength = FDCAN_DLC_BYTES_8;
        break;
        case 12:
            TxHeader.DataLength = FDCAN_DLC_BYTES_12;
        break;
        case 16:
            TxHeader.DataLength = FDCAN_DLC_BYTES_16;
        break;
        case 20:
            TxHeader.DataLength = FDCAN_DLC_BYTES_20;
        break;
        case 24:
            TxHeader.DataLength = FDCAN_DLC_BYTES_24;
        break;
        case 32:
            TxHeader.DataLength = FDCAN_DLC_BYTES_32;
        break;
        case 48:
            TxHeader.DataLength = FDCAN_DLC_BYTES_48;
        break;
        case 64:
            TxHeader.DataLength = FDCAN_DLC_BYTES_64;
        break;
        default:
        break;
    }

    while (HAL_FDCAN_GetTxFifoFreeLevel(&can_info->hfdcan) == 0)
    {}

    ret = HAL_FDCAN_AddMessageToTxFifoQ(&can_info->hfdcan, &TxHeader, message->data);
    if (ret != HAL_OK){
        QM_LOGE(LOG_TAG, "FD CAN ERROR %d", ret);
        return -QM_EIO;
    }

    /* Wait that the data is completely sent (sent FIFO empty) */
    while (((&can_info->hfdcan)->Instance->IR & FDCAN_IR_TFE) != FDCAN_IR_TFE)
    {}

    /* Clear the complete flag */
    (&can_info->hfdcan)->Instance->IR &= FDCAN_IR_TFE;

    return ret;
}

/**
 * .
 *
 * @param[in]  
 */
int32_t qm_can_read(qm_can_dev_t *can_dev, qm_can_message_t *message, int timeout)
{
    int ret = QM_EOK;
    int is_empty = 0;
    int read_len = sizeof(qm_can_data_handle_t);

    qm_can_hal_info_t *can_info = NULL;
    qm_can_data_handle_t data_handle = {0};

    QM_RETURN_ON_FALSE(can_dev, -QM_EINVAL, LOG_TAG, "can dev inval");
    QM_RETURN_ON_FALSE(can_dev->priv, -QM_EINVAL, LOG_TAG, "can dev inval");
    QM_RETURN_ON_FALSE(message, -QM_EINVAL, LOG_TAG, "can tx message inval");

    can_info = (qm_can_hal_info_t *)can_dev->priv;

    if (qm_queue_recv(&can_info->rx_queue, &data_handle, &read_len, timeout) != QM_EOK) {
        return -QM_ETIMEOUT;
    }

    message->identifier = data_handle.RxHeader.Identifier;
    message->data_length = data_handle.RxHeader.DataLength;
    message->extd = (data_handle.RxHeader.IdType == FDCAN_STANDARD_ID ? 0 : 1);
    memcpy(&message->data[0], &data_handle.RxData[0], message->data_length);

    return QM_EOK;  
}

/**
 * 
 *
 * @param[in]  
 *
 * @return  
 */
int32_t qm_can_deinit(qm_can_dev_t *can_dev)
{
    return QM_EOK;
}
#endif
