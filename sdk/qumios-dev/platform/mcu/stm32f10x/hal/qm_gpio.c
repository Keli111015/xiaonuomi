#include "qm.h"

#if CONFIG_QM_GPIO_SUPPORT

#include "stm32f10x_gpio.h"

#include "qm_gpio.h"
#include "qm_platform.h"

typedef struct{
    GPIO_TypeDef *GPIOx;
    GPIO_InitTypeDef GPIO_TypeDef;
    uint32_t GPIO_PIN;
}gpio_config_t;

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
            
    static int is_remap = 0;
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

    switch ((gpio->port / GPIOX_PIN_MAX_NUM))
    {
        case  QM_GPIOA:
            gpio_config->GPIOx = GPIOA;
            APBPeriph_Clock = RCC_APB2Periph_GPIOA;
        break;

        case  QM_GPIOB:
            gpio_config->GPIOx = GPIOB;
            APBPeriph_Clock = RCC_APB2Periph_GPIOB;
        break;

        case  QM_GPIOC:
			gpio_config->GPIOx = GPIOC;
            APBPeriph_Clock = RCC_APB2Periph_GPIOC;
        break;

        case  QM_GPIOD:
			gpio_config->GPIOx = GPIOD;
            APBPeriph_Clock = RCC_APB2Periph_GPIOD;
        break;

        case  QM_GPIOE:
			gpio_config->GPIOx = GPIOE;
            APBPeriph_Clock = RCC_APB2Periph_GPIOE;
        break;
    
        case  QM_GPIOF:
			gpio_config->GPIOx = GPIOF;
            APBPeriph_Clock = RCC_APB2Periph_GPIOF;
        break;

        case  QM_GPIOG:
			gpio_config->GPIOx = GPIOG;
            APBPeriph_Clock = RCC_APB2Periph_GPIOG;
        break;

        default:
            break;
    }

    gpio_config->GPIO_PIN = 1 << (gpio->port % GPIOX_PIN_MAX_NUM);
    GPIO_InitStruct->GPIO_Pin = gpio_config->GPIO_PIN;

    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_INPUT:
           GPIO_InitStruct->GPIO_Mode = GPIO_Mode_IN_FLOATING;  
        break;

        case QM_GPIO_MODE_OUTPUT:
            GPIO_InitStruct->GPIO_Mode = GPIO_Mode_Out_PP;  
        break;

        case QM_GPIO_MODE_OUTPUT_OD:
            GPIO_InitStruct->GPIO_Mode = GPIO_Mode_Out_OD;  
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
            if(gpio->config.mode == QM_GPIO_MODE_INPUT){
                GPIO_InitStruct->GPIO_Mode = GPIO_Mode_IPU; 
            }
        break;

        case QM_GPIO_PULLDOWN_ONLY:
            if(gpio->config.mode == QM_GPIO_MODE_INPUT){
                GPIO_InitStruct->GPIO_Mode = GPIO_Mode_IPD; 
            }
        break;   

        case QM_GPIO_FLOATING:
            if(gpio->config.mode == QM_GPIO_MODE_INPUT){
                GPIO_InitStruct->GPIO_Mode = GPIO_Mode_IN_FLOATING; 
            }
        break;  

        default:

        break;
    }
    
    GPIO_InitStruct->GPIO_Speed = GPIO_Speed_50MHz;
    
    switch (gpio->port)
    {
        case QM_GPIO_PIN_15:
        case QM_GPIO_PIN_19:
        case QM_GPIO_PIN_20:
            RCC_APB2PeriphClockCmd(APBPeriph_Clock | RCC_APB2Periph_AFIO, ENABLE);
            if(!is_remap){
                is_remap = 1;
                GPIO_PinRemapConfig(GPIO_Remap_SWJ_NoJTRST, ENABLE);
                GPIO_PinRemapConfig(GPIO_Remap_SWJ_JTAGDisable, ENABLE);
            }
        break;
    
        default:
            RCC_APB2PeriphClockCmd(APBPeriph_Clock, ENABLE);
        break;
    }
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

#endif
