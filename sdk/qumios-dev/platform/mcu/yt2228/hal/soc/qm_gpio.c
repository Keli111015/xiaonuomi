#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "qm.h"
#include "cpu.h"
#include "user_gpio.h"

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    int ret = QM_EOK;
    GPIO_MODE mode = GPIO_MODE_MAX;
    GPIO_PULL_MODE pull_mode = GPIO_PULL_UP_DOWN;
    if(gpio == NULL){
        return -QM_EINVAL;
    }
    
    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_OUTPUT:
            mode = GPIO_MODE_OUT;
        break;    
        case QM_GPIO_MODE_INPUT:
            mode = GPIO_MODE_IN;
        break; 
        default:
        
        break;
    }

    switch (gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
            pull_mode = GPIO_PULL_UP;
        break;
        case QM_GPIO_PULLDOWN_ONLY:
            pull_mode = GPIO_PULL_DOWN;
        break;  
        default:

        break;
    }

    return user_gpio_init(gpio->port, mode, pull_mode); 
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{

    if(gpio == NULL){
        return -QM_EINVAL;
    }

    return user_gpio_set_value(gpio->port, level);
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return 0;
    }
    return user_gpio_get_value(gpio->port);
}
