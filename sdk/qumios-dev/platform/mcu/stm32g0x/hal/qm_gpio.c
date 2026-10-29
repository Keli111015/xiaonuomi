#include "qm.h"

#if CONFIG_QM_GPIO_SUPPORT

#include "stm32g0xx_hal.h"

#include "qm_gpio.h"
#include "qm_platform.h"
#include "qm_utils_list.h"

typedef struct{
    uint32_t GPIO_PIN;
    platform_gpio_port_t *GPIOx;
    GPIO_InitTypeDef GPIO_Type_Def;
    void (*fn)(void *);
}gpio_config_t;

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{     
    qm_err_t ret = QM_EOK;
    gpio_config_t *gpio_config = NULL;
    GPIO_InitTypeDef *GPIO_InitStruct = NULL;

    if(gpio == NULL){
        return -QM_EINVAL;
    }

    if(gpio->port >= QM_GPIO_PIN_MAX){
        return -QM_EINVAL;
    }

    gpio_config = (gpio_config_t *)qm_malloc(sizeof(gpio_config_t));
    if(gpio_config == NULL){
        return -QM_ENOMEM;
    }
    memset(gpio_config, 0, sizeof(gpio_config_t));

    GPIO_InitStruct = &gpio_config->GPIO_Type_Def;
    const qm_platform_gpio_t *gpio_pins = qm_platform_gpio_get_port((qm_gpio_pin_t)gpio->port);

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
            ret = -QM_EIO;
        break;
    }
    gpio_config->GPIOx = gpio_pins->port;
    gpio_config->GPIO_PIN = gpio_pins->pin_number;
    GPIO_InitStruct->Pin = gpio_config->GPIO_PIN;

    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_INPUT:
           GPIO_InitStruct->Mode = GPIO_MODE_INPUT;  
        break;

        case QM_GPIO_MODE_OUTPUT:
            GPIO_InitStruct->Mode = GPIO_MODE_OUTPUT_PP;  
        break;

        case QM_GPIO_MODE_OUTPUT_OD:
            GPIO_InitStruct->Mode = GPIO_MODE_OUTPUT_OD;  
        break;

        default:
            qm_free(gpio_config);
            ret = -QM_EIO;
        break;
    }
    if(ret != QM_EOK){
        return ret;
    }

    switch (gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
            if(gpio->config.mode == QM_GPIO_MODE_INPUT){
                GPIO_InitStruct->Pull = GPIO_PULLUP; 
            }
        break;

        case QM_GPIO_PULLDOWN_ONLY:
            if(gpio->config.mode == QM_GPIO_MODE_INPUT){
                GPIO_InitStruct->Pull = GPIO_PULLDOWN; 
            }
        break;   

        case QM_GPIO_FLOATING:
            if(gpio->config.mode == QM_GPIO_MODE_INPUT){
                GPIO_InitStruct->Pull = GPIO_NOPULL; 
            }
        break;  

        default:
            qm_free(gpio_config);
            ret = -QM_EIO;
        break;
    }

    if(ret != QM_EOK){
        return ret;
    }
    
    GPIO_InitStruct->Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(gpio_config->GPIOx, GPIO_InitStruct);

    gpio->priv = (void *)gpio_config;
    
    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    gpio_config_t *gpio_config = NULL;

    if(gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }

    gpio_config = (gpio_config_t *)gpio->priv;
    
    if (level == QM_TRUE){
        HAL_GPIO_WritePin(gpio_config->GPIOx, gpio_config->GPIO_PIN ,GPIO_PIN_SET);
    }else if (level == QM_FALSE){
        HAL_GPIO_WritePin(gpio_config->GPIOx, gpio_config->GPIO_PIN ,GPIO_PIN_RESET);
    }else{
        return -QM_EINVAL;  
    }
    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    gpio_config_t *gpio_config = NULL;

    if(gpio == NULL || gpio->priv == NULL){
        return 0;
    }

    gpio_config = (gpio_config_t *)gpio->priv;
 
    if(gpio->config.mode == QM_GPIO_MODE_INPUT){
        return HAL_GPIO_ReadPin(gpio_config->GPIOx, gpio_config->GPIO_PIN);
    }else if (gpio->config.mode == QM_GPIO_MODE_OUTPUT){
        return HAL_GPIO_ReadPin(gpio_config->GPIOx, gpio_config->GPIO_PIN);
    }
    return 0;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    gpio_config_t *gpio_config = (gpio_config_t *)gpio->priv;

    if(gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }

    HAL_GPIO_DeInit(gpio_config->GPIOx, gpio_config->GPIO_PIN);
    qm_free(gpio_config);
    gpio->priv = NULL;

    return QM_EOK;
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type, qm_gpio_irq_handler_t handler, void *arg)
{
    qm_err_t ret = QM_EOK;
    // GPIO_InitTypeDef   *GPIO_InitStruct = NULL;
    // gpio_config_t *gpio_config = NULL;

    // if(gpio == NULL || gpio->priv == NULL || handler == NULL){
    //     return -QM_EINVAL;
    // }
    // gpio_config = (gpio_config_t *)gpio->priv;
    // GPIO_InitStruct = &gpio_config->GPIO_Type_Def;

    // const qm_platform_gpio_t *gpio_pins = qm_platform_gpio_get_port((qm_gpio_pin_t)gpio->port);


    // switch((uint32_t)gpio_pins->port)
    // {
    //     case GPIOA_BASE:
    //         __HAL_RCC_GPIOA_CLK_ENABLE();
    //     break;
    //     case GPIOB_BASE:
    //         __HAL_RCC_GPIOB_CLK_ENABLE();
    //     break;
    //     case GPIOC_BASE:
    //         __HAL_RCC_GPIOC_CLK_ENABLE();
    //     break;
    //     case GPIOD_BASE:
    //         __HAL_RCC_GPIOD_CLK_ENABLE();
    //     break;
    //     case GPIOF_BASE:
    //         __HAL_RCC_GPIOF_CLK_ENABLE();
    //     break;
    //     default:
    //         ret = -QM_EIO;
    //     break;
    // }

    // GPIO_InitStruct->Pin = gpio_pins->pin;

    // switch(intr_type)
    // {
    //     case QM_GPIO_INTR_POSEDGE:
    //         GPIO_InitStruct->Mode = GPIO_MODE_IT_RISING;
    //     break;
    //     case QM_GPIO_INTR_NEGEDGE:
    //         GPIO_InitStruct->Mode = GPIO_MODE_IT_FALLING;
    //     break;
    //     case QM_GPIO_INTR_ANYEDGE:
    //         GPIO_InitStruct->Mode = GPIO_MODE_IT_RISING_FALLING;
    //     break;
    //     default:
    //         ret = -QM_EINTR;
    //     break;
    // }

    // switch(gpio->config.pull_en)
    // {
    //     case QM_GPIO_PULLUP_ONLY:
    //         GPIO_InitStruct->Pull = GPIO_PULLUP; 
    //     break;

    //     case QM_GPIO_PULLDOWN_ONLY:
    //         GPIO_InitStruct->Pull = GPIO_PULLDOWN; 
    //     break;

    //     case QM_GPIO_FLOATING:
    //         GPIO_InitStruct->Pull = GPIO_NOPULL; 
    //     break;

    //     default:
    //         qm_free(gpio_config);
    //         ret = -QM_EIO;
    //     break;
    // }

    // if(ret != QM_EOK){
    //     return ret;
    // }

    // HAL_GPIO_Init(gpio_pins->port, GPIO_InitStruct);

    // HAL_NVIC_SetPriority(EXTI4_15_IRQn, 2, 0);
    // HAL_NVIC_EnableIRQ(EXTI4_15_IRQn);

    // gpio_config->fn = handler;

    return QM_EOK;
}

// void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
// {
//     qm_list_node_t *node = NULL;
//     gpio_config_t *gpio_config = NULL;
//     node = qm_list_find(gpio_intr_list, (void *)pin);
    
//     if (node == NULL){
//         return;   
//     }
//     intr_handle = (qm_gpio_hal_ctx_t *)node->val;
//     intr_handle->fn(intr_handle->arg);
// }


int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }

    return -QM_EINVAL;;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }

    return -QM_EINVAL;
}

#endif
