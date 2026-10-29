#include "qm_gpio.h"
#include "qm_errno.h"

#include "stm32f0xx.h"
#include "stm32f0xx_gpio.h"

#include "qm.h"
#include "qm_platform.h"

#define GPIOX_PIN_MAX_NUM       (16) // GPIOPIN0 ~ GPIO15 
#define GPIOX_TYPEDEF_MAX_NUM   (3) // GPIOA GPIOB GPIOC 

typedef enum{
    QM_GPIOA = 0,
    QM_GPIOB = 1,
    QM_GPIOC = 2,
}gpio_typedef_t;

typedef struct{
    GPIO_TypeDef *GPIOx;
    GPIO_InitTypeDef GPIO_TypeDef;
    uint32_t GPIO_PIN;
}gpio_config_t;


int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    qm_err_t ret = QM_EOK;
    uint32_t APBPeriph_Clock;
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
    GPIO_InitStruct = &gpio_config->GPIO_TypeDef;

    switch (gpio->port / GPIOX_PIN_MAX_NUM)
    {
        case  QM_GPIOA:
            gpio_config->GPIOx = GPIOA;
            APBPeriph_Clock = RCC_AHBPeriph_GPIOA;
        break;

        case  QM_GPIOB:
            gpio_config->GPIOx = GPIOB;
            APBPeriph_Clock = RCC_AHBPeriph_GPIOB;
        break;

        case  QM_GPIOC:

			gpio_config->GPIOx = GPIOC;
            APBPeriph_Clock = RCC_AHBPeriph_GPIOC;
        break;

        default:
            break;
    }

    gpio_config->GPIO_PIN = 1 << (gpio->port % GPIOX_PIN_MAX_NUM);
    GPIO_InitStruct->GPIO_Pin = gpio_config->GPIO_PIN;

    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_INPUT:
           GPIO_InitStruct->GPIO_Mode = GPIO_Mode_IN;  
        break;
        case QM_GPIO_MODE_OUTPUT:
            
            GPIO_InitStruct->GPIO_Mode = GPIO_Mode_OUT;  
        break;
        default :
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
            GPIO_InitStruct->GPIO_PuPd = GPIO_PuPd_UP;
        break;
        case QM_GPIO_PULLDOWN_ONLY:
            GPIO_InitStruct->GPIO_PuPd = GPIO_PuPd_DOWN;
        break;       
        case QM_GPIO_FLOATING:
            GPIO_InitStruct->GPIO_PuPd = GPIO_PuPd_NOPULL;
        break;       
        default:
            qm_free(gpio_config);
            ret = -QM_EIO;
        break;
    }
    
    if(ret != QM_EOK){
        return ret;
    }

    GPIO_InitStruct->GPIO_OType = GPIO_OType_PP;
    GPIO_InitStruct->GPIO_Speed = GPIO_Speed_50MHz;
    
    RCC_AHBPeriphClockCmd(APBPeriph_Clock, ENABLE);
    GPIO_Init(gpio_config->GPIOx, GPIO_InitStruct);

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
        GPIO_SetBits(gpio_config->GPIOx, gpio_config->GPIO_PIN);
    }else if (level == QM_FALSE){
        GPIO_ResetBits(gpio_config->GPIOx, gpio_config->GPIO_PIN);
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
        return GPIO_ReadInputDataBit(gpio_config->GPIOx, gpio_config->GPIO_PIN);
    }else if (gpio->config.mode == QM_GPIO_MODE_OUTPUT){
        return GPIO_ReadOutputDataBit(gpio_config->GPIOx, gpio_config->GPIO_PIN);
    }
    return 0;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    gpio_config_t *gpio_config = NULL;
    GPIO_InitTypeDef *GPIO_InitStruct = NULL;

    if(gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }

    gpio_config = (gpio_config_t *)gpio->priv;
    GPIO_InitStruct = &gpio_config->GPIO_TypeDef;

    GPIO_StructInit(GPIO_InitStruct);
    
    GPIO_InitStruct->GPIO_PuPd = GPIO_PuPd_DOWN;
    GPIO_InitStruct->GPIO_Pin = gpio_config->GPIO_PIN;

    GPIO_Init(gpio_config->GPIOx, GPIO_InitStruct);

    qm_free(gpio_config);
    gpio->priv = NULL;

    return QM_EOK;
}

#if 0
int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type, qm_gpio_irq_handler_t handler, void *arg)
{
    qm_err_t ret = QM_EOK;

    if(gpio == NULL || gpio->priv == NULL || handler == NULL){
        return -QM_EINVAL;
    }


    return -QM_EINVAL;
}

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
