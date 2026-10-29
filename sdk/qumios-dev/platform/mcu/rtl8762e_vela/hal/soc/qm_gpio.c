#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "rtl876x_gpio.h"
#include "rtl876x_rcc.h"
#include "rtl876x_pinmux.h"
#include "rtl876x_nvic.h"

#define GPIOX_PIN_MAX_NUM       (38) // 0-38 

#define QM_GPIO_PIN_0   0
#define QM_GPIO_PIN_MAX (38U)

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    int32_t ret = QM_EOK;
    uint32_t APBPeriph_Clock;
    GPIO_InitTypeDef GPIO_InitStruct;
	
    if (gpio == NULL) 
    {
        return -QM_EINVAL;
    }

	/* rtl876x最大到38 */
    if ( gpio->port < QM_GPIO_PIN_0 || gpio->port >= QM_GPIO_PIN_MAX)
    {
        return -QM_EINVAL;
    }

	RCC_PeriphClockCmd(APBPeriph_GPIO, APBPeriph_GPIO_CLOCK, ENABLE);
	GPIO_StructInit(&GPIO_InitStruct);

    GPIO_InitStruct.GPIO_Pin = GPIO_GetPin(gpio->port);

    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_INPUT:
        {
            GPIO_InitStruct.GPIO_Mode = GPIO_Mode_IN;  
            GPIO_InitStruct.GPIO_ITCmd = DISABLE;
            break;
        }
        case QM_GPIO_MODE_OUTPUT:
        {
            GPIO_InitStruct.GPIO_Mode = GPIO_Mode_OUT; 
            GPIO_InitStruct.GPIO_ITCmd = DISABLE;	
            break;
        }
        default :
        {
            return -QM_EINVAL;
        }
    }

    GPIO_Init(&GPIO_InitStruct); 
	
	switch(gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
        {
            Pad_Config(gpio->port, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, PAD_OUT_HIGH);
            break;
        }
        case QM_GPIO_PULLDOWN_ONLY: 
        {
            Pad_Config(gpio->port, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_DOWN, PAD_OUT_ENABLE, PAD_OUT_LOW);
            break;
        }
        case QM_GPIO_PULLUP_PULLDOWN:  /* ???? */
        {
            Pad_Config(gpio->port, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_HIGH);
            break;
        }
        case QM_GPIO_FLOATING:  
        {
            Pad_Config(gpio->port, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_NONE, PAD_OUT_ENABLE, PAD_OUT_HIGH);
            break;
        }
        default:                
        {
            break;
        }
    }    
	
	Pinmux_Config(gpio->port, DWGPIO);

    return QM_EOK;
}


int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    if (gpio == NULL) 
    {
        return -QM_EINVAL;
    }

    Pad_Config(gpio->port, PAD_SW_MODE, PAD_IS_PWRON, PAD_PULL_UP, PAD_OUT_ENABLE, level);

    // if (level == QM_TRUE)
    // {
    //     GPIO_SetBits(GPIO_GetPin(gpio->port));
    // } 
    // else if (level == QM_FALSE)
    // {
    //     GPIO_ResetBits(GPIO_GetPin(gpio->port));
    // }
    // else
    // {
    //     return -QM_EINVAL;  
    // }
    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    uint8_t level = 0;

    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }
 
    if (gpio->config.mode == QM_GPIO_MODE_INPUT)
    {
        return GPIO_ReadInputDataBit(GPIO_GetPin(gpio->port));
    }
    else if (gpio->config.mode == QM_GPIO_MODE_OUTPUT)
    {
        return GPIO_ReadOutputDataBit(GPIO_GetPin(gpio->port));
    }
    return 0;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
#if 0
    gpio_config_t *gpio_config = NULL;
    GPIO_InitTypeDef *GPIO_InitStruct = NULL;

    if(gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }

    gpio_config = (gpio_config_t *)gpio->priv;
    GPIO_InitStruct = &gpio_config->GPIO_TypeDef;

    GPIO_StructInit(GPIO_InitStruct);
    
    GPIO_InitStruct->GPIO_Pin = gpio_config->GPIO_PIN;

    GPIO_Init(GPIO_InitStruct);

    qm_free(gpio_config);
    gpio->priv = NULL;
#endif

    return QM_EOK;
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type, \
							qm_gpio_irq_handler_t handler, void *arg)
{
    qm_err_t ret = QM_EOK;

    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }

    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }

    return QM_EOK;;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }

    return QM_EOK;
}
