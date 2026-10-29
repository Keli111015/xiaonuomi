#include "qm.h"

#ifdef CONFIG_QM_CAN_SUPPORT
#include "qm_can.h"

#include "driver/twai.h"

#define LOG_TAG "hal"

/**
 * 
 *
 * @param[in]  
 *
 * @return  0 : on success,  otherwise is error
 */
int32_t qm_can_init(qm_can_dev_t *can_dev)
{
    twai_timing_config_t t_config = {0};
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(0, 0, 0);
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    QM_RETURN_ON_FALSE(can_dev, -QM_EINVAL, LOG_TAG, "can dev inval");

    switch (can_dev->can_config.clock)
    {
        case QM_CAN_CLOCK_1MHZ:
        {
            twai_timing_config_t tmp = TWAI_TIMING_CONFIG_1MBITS();
            memcpy(&t_config, &tmp, sizeof(twai_timing_config_t));
        }
        break;
        
        case QM_CAN_CLOCK_500KHZ:
        {
            twai_timing_config_t tmp = TWAI_TIMING_CONFIG_500KBITS();
            memcpy(&t_config, &tmp, sizeof(twai_timing_config_t));
        }
        break;

        case QM_CAN_CLOCK_50KHZ:
        {
            twai_timing_config_t tmp = TWAI_TIMING_CONFIG_50KBITS();
            memcpy(&t_config, &tmp, sizeof(twai_timing_config_t));
        }
        break;

        default:
        break;
    }

    f_config.acceptance_code = can_dev->filter_config.acceptance_code;
    f_config.acceptance_mask = ((can_dev->filter_config.acceptance_mask << 3) | 0x7);

    switch (can_dev->can_config.mode)
    {
        case QM_CAN_MODE_NORMAL:
        {
            g_config.mode = TWAI_MODE_NORMAL;
        }
        break;
        
        default:
        break;
    }
    g_config.tx_queue_len = 100;
    g_config.rx_queue_len = 200;
    g_config.tx_io = can_dev->can_config.tx_pin;
    g_config.rx_io = can_dev->can_config.rx_pin;

    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));

    ESP_ERROR_CHECK(twai_start());

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
    return QM_EOK;
}

/**
 * .
 *
 * @param[in]  
 */
int32_t qm_can_write(qm_can_dev_t *can_dev, const qm_can_message_t *message, int timeout)
{
    twai_message_t start_message = {0};

    start_message.extd = message->extd;
    start_message.identifier = message->identifier;
    start_message.data_length_code = message->data_length;
    memcpy(start_message.data, message->data, message->data_length);
    
    twai_transmit(&start_message, timeout);
    return QM_EOK;
}

/**
 * .
 *
 * @param[in]  
 */
int32_t qm_can_read(qm_can_dev_t *can_dev, qm_can_message_t *message, int timeout)
{
    twai_message_t rx_msg = {0};
    twai_receive(&rx_msg, timeout);
    
    message->identifier = rx_msg.identifier;
    message->data_length = rx_msg.data_length_code;
    memcpy(message->data, rx_msg.data, message->data_length);

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