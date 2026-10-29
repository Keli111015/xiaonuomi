#include "qm.h"

#if CONFIG_QM_I2C_SUPPORT

#include "qm_i2c.h"

#include "qm_utils_timer.h"

#include "qm_platform.h"
#include "stm32f10x_gpio.h"
#include "stm32f10x_rcc.h"
#include "misc.h"
#include "stm32f10x_i2c.h"



#define QM_I2C_PORT_MAX    (2)

typedef struct{
    I2C_TypeDef *I2Cx;
    uint32_t APBPeriph;
}i2c_config_t;

typedef struct
{
    i2c_config_t config;

}qm_i2c_info_t;


static qm_i2c_info_t g_i2c_info[] = {
    {{I2C1, RCC_APB1Periph_I2C1}},
    {{I2C2, RCC_APB1Periph_I2C2}},
};

static int32_t qm_i2c_pin_enable(uint8_t pins);

int32_t qm_i2c_init(qm_i2c_dev_t *i2c)
{
    qm_i2c_info_t *info = NULL;

    I2C_InitTypeDef I2C_InitStruct;

    if(i2c->port >= QM_I2C_PORT_MAX){
        return -QM_EINVAL;
    }

    info = &g_i2c_info[i2c->port];

    I2C_DeInit(info->config.I2Cx);

    RCC_APB1PeriphClockCmd(info->config.APBPeriph, ENABLE);

    qm_i2c_pin_enable(i2c->pins.sck_pin);
    qm_i2c_pin_enable(i2c->pins.sda_pin);

    I2C_InitStruct.I2C_Mode = I2C_Mode_I2C;
    I2C_InitStruct.I2C_DutyCycle = I2C_DutyCycle_2;
    I2C_InitStruct.I2C_ClockSpeed = i2c->config.freq;

    I2C_InitStruct.I2C_Ack = I2C_Ack_Enable;

    if(i2c->config.address_width == QM_I2C_ADDRESS_WIDTH_7BIT){
        I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    }else if(i2c->config.address_width == QM_I2C_ADDRESS_WIDTH_10BIT){
        I2C_InitStruct.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_10bit;
    }

    I2C_Init(info->config.I2Cx, &I2C_InitStruct);
    I2C_Cmd(info->config.I2Cx, ENABLE);

    i2c->priv = (void*)info;

    return QM_EOK;

}

int32_t qm_i2c_master_write(qm_i2c_dev_t *i2c, uint16_t dev_addr, const uint8_t *data,
                            uint16_t size, uint32_t timeout)
{
    int index = 0;
    uint32_t t_left = 0;
    qm_i2c_info_t *info = NULL;
    qm_utils_time_t time = {0};
    qm_utils_time_countdown_ms(&time, timeout);  

    if(i2c == NULL || i2c->priv == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    info = (qm_i2c_info_t *)i2c->priv;

    while (I2C_GetFlagStatus(info->config.I2Cx, I2C_FLAG_BUSY)){
        t_left = qm_utils_time_left(&time);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }
    
    I2C_GenerateSTART(info->config.I2Cx, ENABLE);
    qm_utils_time_countdown_ms(&time, timeout);  
    while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_MODE_SELECT)){
        t_left = qm_utils_time_left(&time);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }

    I2C_Send7bitAddress(info->config.I2Cx, dev_addr, I2C_Direction_Transmitter);
    qm_utils_time_countdown_ms(&time, timeout);  
    while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)){
        t_left = qm_utils_time_left(&time);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }

    I2C_Cmd(info->config.I2Cx, ENABLE);

    for(index = 0;index < size; index++){
        I2C_SendData(info->config.I2Cx, data[index]);
        qm_utils_time_countdown_ms(&time, timeout);  
        while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_BYTE_TRANSMITTED)){
            t_left = qm_utils_time_left(&time);
            if(t_left == 0){
                return -QM_ETIMEOUT;
            }
        }
    }

    I2C_GenerateSTOP(info->config.I2Cx, ENABLE);

    return QM_EOK;

}

int32_t qm_i2c_master_write_read(qm_i2c_dev_t *i2c, uint16_t dev_addr, 
                                const uint8_t* write_buffer, size_t write_size,
                                uint8_t* read_buffer, size_t read_size, uint32_t timeout)
{
    int index = 0;
    uint32_t t_left = 0;
    qm_i2c_info_t *info = NULL;
    qm_utils_time_t time = {0};
    qm_utils_time_countdown_ms(&time, timeout);  

    if(i2c == NULL || i2c->priv == NULL || write_buffer == NULL || read_buffer == NULL ){
        return -QM_EINVAL;
    }

    info = (qm_i2c_info_t *)i2c->priv;

    while (I2C_GetFlagStatus(info->config.I2Cx, I2C_FLAG_BUSY)){
        t_left = qm_utils_time_left(&time);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }
    
    I2C_GenerateSTART(info->config.I2Cx, ENABLE);
    qm_utils_time_countdown_ms(&time, timeout);  
    while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_MODE_SELECT)){
        t_left = qm_utils_time_left(&time);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }

    I2C_Send7bitAddress(info->config.I2Cx, dev_addr, I2C_Direction_Transmitter);
    qm_utils_time_countdown_ms(&time, timeout);  
    while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED)){
        t_left = qm_utils_time_left(&time);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }

    I2C_Cmd(info->config.I2Cx, ENABLE);
    for(index = 0;index < write_size; index++){
        I2C_SendData(info->config.I2Cx, write_buffer[index]);
        qm_utils_time_countdown_ms(&time, timeout);  
        while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_BYTE_TRANSMITTED)){
            t_left = qm_utils_time_left(&time);
            if(t_left == 0){
                return -QM_ETIMEOUT;
            }
        }
    }

    I2C_GenerateSTART(info->config.I2Cx, ENABLE);
    qm_utils_time_countdown_ms(&time, timeout);  
    while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_MODE_SELECT)){
        t_left = qm_utils_time_left(&time);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }

    I2C_Send7bitAddress(info->config.I2Cx, dev_addr, I2C_Direction_Receiver);
    qm_utils_time_countdown_ms(&time, timeout);  
    while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_RECEIVER_MODE_SELECTED)){
        t_left = qm_utils_time_left(&time);
        if(t_left == 0){
            return -QM_ETIMEOUT;
        }
    }

    for(index = 0;index < read_size; index++){
        if(read_size - index == 1){
            I2C_AcknowledgeConfig(info->config.I2Cx, DISABLE);
            I2C_GenerateSTOP(info->config.I2Cx, ENABLE);
        }

        qm_utils_time_countdown_ms(&time, timeout);  
        while(!I2C_CheckEvent(info->config.I2Cx, I2C_EVENT_MASTER_BYTE_RECEIVED)){
            t_left = qm_utils_time_left(&time);
            if(t_left == 0){
                return -QM_ETIMEOUT;
            }
        }
        read_buffer[index] = I2C_ReceiveData(info->config.I2Cx);
    }
    I2C_AcknowledgeConfig(info->config.I2Cx, ENABLE);
    return QM_EOK;
}

int32_t qm_i2c_deinit(qm_i2c_dev_t *i2c)
{

    return QM_EOK;

}

static int32_t qm_i2c_pin_enable(uint8_t pins)
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
    GPIO_InitStructure.GPIO_Mode=GPIO_Mode_AF_OD;
    GPIO_InitStructure.GPIO_Speed=GPIO_Speed_50MHz;
    GPIO_Init(GPIOx, &GPIO_InitStructure);

    return QM_EOK;
}

#endif