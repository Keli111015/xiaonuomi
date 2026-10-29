#include "qm.h"

#if CONFIG_QM_I2C_SUPPORT

#include "qm_i2c.h"

#include "qm_utils_timer.h"

#include "qm_platform.h"

#include "ls_hal_i2c.h"
#include "ls_soc_gpio.h"

#define QM_I2C_PORT_MAX    (2)



int32_t qm_i2c_init(qm_i2c_dev_t *i2c)
{
    I2C_HandleTypeDef *I2cHandle = NULL;
    I2cHandle = (I2C_HandleTypeDef *)qm_malloc(sizeof(I2C_HandleTypeDef));
    if(I2cHandle == NULL){
        return -QM_ENOMEM;
    }
    memset(I2cHandle, 0, sizeof(I2C_HandleTypeDef));

    if(i2c->port >= QM_I2C_PORT_MAX){
        return -QM_EINVAL;
    }

    switch (i2c->port)
    {
        case 0:
            pinmux_iic1_init(i2c->pins.sck_pin, i2c->pins.sda_pin);
            I2cHandle->Instance = I2C1;
        break;

        case 1:
            pinmux_iic2_init(i2c->pins.sck_pin, i2c->pins.sda_pin);
            I2cHandle->Instance = I2C2;

        break;

        default:
            return -QM_EIO;
    }


    switch (i2c->config.freq)
    {
        case QM_I2C_BUS_BIT_RATES_100K:
            I2cHandle->Init.ClockSpeed = I2C_SPEED_NORMAL_100K;
        break;

        case QM_I2C_BUS_BIT_RATES_400K:
            I2cHandle->Init.ClockSpeed = I2C_SPEED_FAST_400K;
        break;

        default:
            return -QM_EIO;
    }


    I2cHandle->Init.OwnAddress1 = i2c->config.dev_addr;
    I2cHandle->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    I2cHandle->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;      //外围设备将不会响应通用调用地址
    I2cHandle->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;          //启用时钟拉伸功能
    I2cHandle->Init.OwnAddress2 = 0x7E;

    if(i2c->config.address_width == QM_I2C_ADDRESS_WIDTH_7BIT){
        I2cHandle->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    }else if(i2c->config.address_width == QM_I2C_ADDRESS_WIDTH_10BIT){
        I2cHandle->Init.AddressingMode = I2C_ADDRESSINGMODE_10BIT;
    }
    
    if (HAL_I2C_Init(I2cHandle) != HAL_OK)
    {
        return -QM_EIO;
    }

    i2c->priv = (void*)I2cHandle;

    return QM_EOK;

}

int32_t qm_i2c_master_write(qm_i2c_dev_t *i2c, uint16_t dev_addr, const uint8_t *data,
                            uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef result = HAL_OK;
    I2C_HandleTypeDef *I2cHandle = NULL;

    if(i2c == NULL || i2c->priv == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    I2cHandle = (I2C_HandleTypeDef *)i2c->priv;
    
    result = HAL_I2C_Master_Transmit(I2cHandle, dev_addr, (uint8_t *)data, size, timeout);
    if(result != HAL_OK){
        return -QM_EIO;
    }

    return QM_EOK;

}

int32_t qm_i2c_master_write_read(qm_i2c_dev_t *i2c, uint16_t dev_addr, 
                                const uint8_t* write_buffer, size_t write_size,
                                uint8_t* read_buffer, size_t read_size, uint32_t timeout)
{

    HAL_StatusTypeDef result = HAL_OK;
    I2C_HandleTypeDef *I2cHandle = NULL;


    if(i2c == NULL || i2c->priv == NULL || read_buffer == NULL ||  read_size == 0){
        return -QM_EINVAL;
    }

    I2cHandle = (I2C_HandleTypeDef *)i2c->priv;

    if(write_size && write_buffer != NULL ){
        result = HAL_I2C_Master_Transmit(I2cHandle, dev_addr, (uint8_t *)write_buffer, write_size, timeout);
        if (result != HAL_OK)
        {
            return -QM_EIO;
        }
    }

    result = HAL_I2C_Master_Receive(I2cHandle, dev_addr, read_buffer, read_size, timeout);
    if (result != HAL_OK)
    {
        return -QM_EIO;
    }
    return QM_EOK;
}

int32_t qm_i2c_deinit(qm_i2c_dev_t *i2c)
{
    HAL_StatusTypeDef result = HAL_OK;
    I2C_HandleTypeDef *I2cHandle = NULL;

    if(i2c == NULL || i2c->priv == NULL){
        QM_LOGD(LOG_TAG, "i2c deinit error\n");
        return -QM_EINVAL;
    }

    I2cHandle = (I2C_HandleTypeDef *)i2c->priv;

    result = HAL_I2C_DeInit(I2cHandle);
    if (result != HAL_OK)
    {
        return -QM_EIO;
    }

    switch (i2c->port)
    {
        case 0:
            pinmux_iic1_deinit();
        break;

        case 1:
            pinmux_iic2_deinit();
        break;

        default:
            return -QM_EIO;
    }
    qm_free(i2c->priv);
    i2c->priv = NULL;
    return QM_EOK;

}

#endif