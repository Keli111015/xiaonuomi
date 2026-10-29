#include "qm.h"

#if CONFIG_QM_I2C_SUPPORT

#include "qm_i2c.h"
#include "qm_log.h"
#define LOG_TAG "TEST"

#include "qm_utils_timer.h"

#include "qm_platform.h"
#include "stm32g0xx_hal.h"
#include "stm32g0xx_hal_i2c.h"
#include "stm32g0xx_hal_gpio.h"


#define QM_I2C_PORT_MAX    (3)

static int32_t qm_i2c_pin_enable(uint8_t pins);

int32_t qm_i2c_init(qm_i2c_dev_t *i2c)
{
    I2C_HandleTypeDef *I2C_Handle = {0};

    if(i2c == NULL){
        return -QM_EINVAL;
    }

    if(i2c->port >= QM_I2C_PORT_MAX){
        return -QM_EINVAL;
    }

    // qm_i2c_pin_enable(i2c->pins.sck_pin);
    // qm_i2c_pin_enable(i2c->pins.sda_pin);
    I2C_Handle = (I2C_HandleTypeDef *)qm_malloc(sizeof(I2C_HandleTypeDef));

    switch (i2c->port)
    {
        case 0:
            I2C_Handle->Instance = I2C1;
            // __HAL_RCC_I2C1_CLK_ENABLE();
        break;

        case 1:
            I2C_Handle->Instance = I2C2;
            // __HAL_RCC_I2C2_CLK_ENABLE();
        break;

        case 2:
            I2C_Handle->Instance = I2C3;
            // __HAL_RCC_I2C3_CLK_ENABLE();
        break;

        default:
            return -QM_EIO;
    }

    if(i2c->config.address_width == QM_I2C_ADDRESS_WIDTH_7BIT){
        I2C_Handle->Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    }else if(i2c->config.address_width == QM_I2C_ADDRESS_WIDTH_10BIT){
        I2C_Handle->Init.AddressingMode = I2C_ADDRESSINGMODE_10BIT;
    }
    I2C_Handle->Init.Timing = 0x6010C7FF; // 控制 I2C 时序
    I2C_Handle->Init.OwnAddress1 = i2c->config.dev_addr;
    I2C_Handle->Init.DualAddressMode = I2C_DUALADDRESS_DISABLE; // 双地址使能
    I2C_Handle->Init.OwnAddress2 = 0x00; // 如果不启用双地址模式，可以设置为0
    I2C_Handle->Init.OwnAddress2Masks = I2C_OA2_NOMASK; // 通常不使用掩码
    I2C_Handle->Init.GeneralCallMode = I2C_GENERALCALL_DISABLE; // 通常禁用
    I2C_Handle->Init.NoStretchMode = I2C_NOSTRETCH_DISABLE; // 通常禁用

    if (HAL_I2C_Init(I2C_Handle) != HAL_OK) {
        return -QM_EIO;
    }

    if (HAL_I2CEx_ConfigAnalogFilter(I2C_Handle, I2C_ANALOGFILTER_ENABLE) != HAL_OK) {
        return -QM_EIO;
    }

    if (HAL_I2CEx_ConfigDigitalFilter(I2C_Handle, 0) != HAL_OK) {
        return -QM_EIO;
    }
    
    i2c->priv = (void *)I2C_Handle;

    return QM_EOK;
}

int32_t qm_i2c_master_write(qm_i2c_dev_t *i2c, uint16_t dev_addr, const uint8_t *data,
                            uint16_t size, uint32_t timeout)
{
    HAL_StatusTypeDef ret = HAL_OK;
    I2C_HandleTypeDef *I2C_Handle = NULL;
    dev_addr = dev_addr << 1;

    if(i2c == NULL || i2c->priv == NULL || data == NULL || size == 0){
        QM_LOGD(LOG_TAG, "---- QM_EINVAL ----\r\n");

        return -QM_EINVAL;
    }

    I2C_Handle = (I2C_HandleTypeDef *)i2c->priv;

    if(HAL_I2C_Master_Transmit(I2C_Handle, dev_addr, (uint8_t *)data, size, timeout) != HAL_OK){
        return -QM_ERROR;
    }

    return QM_EOK;
}

int32_t qm_i2c_master_write_read(qm_i2c_dev_t *i2c, uint16_t dev_addr, 
                                const uint8_t* write_buffer, size_t write_size,
                                uint8_t* read_buffer, size_t read_size, uint32_t timeout)
{
    I2C_HandleTypeDef *I2C_Handle = NULL;

    if(i2c == NULL || i2c->priv == NULL){
        return -QM_EINVAL;
    }

    if(read_buffer == NULL ){
        return -QM_EINVAL;
    }
    dev_addr = dev_addr << 1;

    I2C_Handle = (I2C_HandleTypeDef *)i2c->priv;
    if(write_buffer != NULL){
        if (HAL_I2C_Master_Transmit(I2C_Handle, dev_addr, (uint8_t *)write_buffer, write_size, timeout) != HAL_OK) {
            QM_LOGD(LOG_TAG, "---- HAL_I2C_Master_Transmit error ----\r\n");
            return -QM_ERROR;
        }
    }
    dev_addr = dev_addr + 1;
    // 读取寄存器数据
    if (HAL_I2C_Master_Receive(I2C_Handle, dev_addr, read_buffer, read_size, timeout) != HAL_OK) {
        QM_LOGD(LOG_TAG, "---- HAL_I2C_Master_Receive error ----\r\n");
        return -QM_ERROR;
    }

    return QM_EOK;
}

int32_t qm_i2c_deinit(qm_i2c_dev_t *i2c)
{
    I2C_HandleTypeDef *I2C_Handle = NULL;

    if(i2c == NULL){
        return -QM_EINVAL;
    }

    I2C_Handle = (I2C_HandleTypeDef *)i2c->priv;
    if(HAL_I2C_DeInit(I2C_Handle) != HAL_OK) {
        return -QM_ERROR;
    }

    return QM_EOK;

}

static int32_t qm_i2c_pin_enable(uint8_t pins)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    const qm_platform_gpio_t *gpio_pins = qm_platform_gpio_get_port((qm_gpio_pin_t)pins);
    
    switch((uint32_t)gpio_pins->port)
    {
        case GPIOA_BASE:
            __HAL_RCC_GPIOA_CLK_ENABLE();
        break;
        case GPIOB_BASE:
            __HAL_RCC_GPIOB_CLK_ENABLE();
        break;
        case GPIOC_BASE:
            __HAL_RCC_GPIOC_CLK_ENABLE();
        break;
        case GPIOD_BASE:
            __HAL_RCC_GPIOD_CLK_ENABLE();
        break;
        case GPIOF_BASE:
            __HAL_RCC_GPIOF_CLK_ENABLE();
        break;
        default:
            return -QM_EIO;
    }
    GPIO_InitStruct.Pin = (uint32_t)pins;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    // GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(gpio_pins->port, &GPIO_InitStruct);

    return QM_EOK;
}

#endif
