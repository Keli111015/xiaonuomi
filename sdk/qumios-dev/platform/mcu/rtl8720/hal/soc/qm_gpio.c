#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "gpio_api.h"
#include "gpio_ex_api.h"
#include "gpio_irq_api.h"

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }
    int ret = QM_EOK;

	if( (gpio->port < PA_0) ||
        (gpio->port > PA_23) ||
        (gpio->port > PA_6 && gpio->port < PA_13)) {
        return -QM_EINVAL; 
	}

    gpio_t *pgpio = (gpio_t *)qm_malloc(sizeof(gpio_t));
    if(pgpio == NULL){
        return -QM_ENOMEM;
    }

    gpio_init(pgpio, gpio->port);

    switch(gpio->config.mode){
        case QM_GPIO_MODE_INPUT:
            gpio_dir(pgpio, PIN_INPUT);
        break;
        case QM_GPIO_MODE_OUTPUT: 
            gpio_dir(pgpio, PIN_OUTPUT);
        break;
        default: 
            goto _error;
        break;
    }
    switch(gpio->config.pull_en){
        case QM_GPIO_PULLUP_ONLY:
            gpio_mode(pgpio, PullUp);
        break;
        case QM_GPIO_PULLDOWN_ONLY: 
            gpio_mode(pgpio, PullDown);
        break;
        case QM_GPIO_PULLUP_PULLDOWN:
            gpio_mode(pgpio, OpenDrain);
        break;
        case QM_GPIO_FLOATING:  
            gpio_mode(pgpio, PullNone);
        break;
        default:                
            goto _error;
        break;
    }    
    
    gpio->priv = (void *)pgpio;
    return QM_EOK;

_error:
    if(pgpio){
        qm_free(pgpio);
        pgpio = NULL;
    }
    return -QM_EIO;  
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    if(gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }
    
    if(gpio->config.mode == QM_GPIO_MODE_INPUT){
        return -QM_EINVAL;
    }

    if(gpio->config.mode == QM_GPIO_MODE_DISABLE){
        return -QM_EINVAL;
    }   

    gpio_t *pgpio = (gpio_t *)gpio->priv;
    gpio_write(pgpio, level);
    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL || gpio->priv == NULL){
        return 0;
    }
    
    gpio_t *pgpio = (gpio_t *)gpio->priv;
    return gpio_read(pgpio); 
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type,
                            qm_gpio_irq_handler_t handler, void *arg)
{
    int ret;
    uint8_t type;

    if( gpio->config.mode == QM_GPIO_MODE_OUTPUT){
        return 0;
    }

    if( gpio->config.mode == QM_GPIO_MODE_DISABLE){
        return 0;       
    }
    if(intr_type <= QM_GPIO_INTR_DISABLE || intr_type >= QM_GPIO_INTR_MAX){
        return -QM_EINVAL;
    }
    switch(intr_type){
        case QM_GPIO_INTR_POSEDGE:
        break;
        case QM_GPIO_INTR_NEGEDGE: 
        break;
        case QM_GPIO_INTR_ANYEDGE:
        break;
        case QM_GPIO_INTR_LOW_LEVEL:
        break;
        case QM_GPIO_INTR_HIGH_LEVEL:
        break;
        default: 
        break;
    }

    return QM_EOK;

}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    gpio_t *gpio_irq = NULL;
    if((gpio->priv == NULL || gpio->config.mode != QM_GPIO_MODE_INPUT))
    {
        return -QM_EINVAL;
    } 

    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    gpio_t *gpio_irq = NULL;
    if((gpio->priv == NULL || gpio->config.mode != QM_GPIO_MODE_INPUT))
    {
        return -QM_EINVAL;
    } 
    return QM_EOK;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    gpio_t *pgpio = (gpio_t *)gpio->priv; 

    gpio_deinit(pgpio);

    if(gpio->priv){
        qm_free(gpio->priv);
        gpio->priv = NULL;
    }
    return QM_EOK;
}