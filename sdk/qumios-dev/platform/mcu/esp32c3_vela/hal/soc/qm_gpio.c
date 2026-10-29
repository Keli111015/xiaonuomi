#include "qm.h"
#include "qm_gpio.h"

#if CONFIG_QM_GPIO_SUPPORT

#include "applet.h"
#include "gpio.h"

typedef struct {
    uint8_t irq_isuse;
    gpio_config_t config;
} _config_t;

typedef struct {
    int port;
    void *arg;
    void (*fn)(void *);
} _gpio_intr_t;

static void *gpio_interrupt_handler(void *arg)
{
    _gpio_intr_t *gpio_intr = (_gpio_intr_t *)arg;

    gpio_intr->fn(gpio_intr->arg);
    
    return (void *)gpio_intr;     
}


int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    int ret = QM_EOK;
    _config_t *gpio_irq = NULL;

    if(gpio == NULL || gpio->priv){
        return -QM_EINVAL;
    }

    gpio_irq = (_config_t *)qm_malloc(sizeof(_config_t));
    if(gpio_irq == NULL){
        return -QM_ENOMEM;
    }
    memset(gpio_irq, 0, sizeof(_config_t));
    gpio_irq->config.pin = gpio->port;
    switch(gpio->config.mode){
        case QM_GPIO_MODE_INPUT:
            gpio_irq->config.pintype = GPIO_INPUT_PIN;
            gpio_irq->config.flag = INPUT_FUNCTION_1;
        break;
        case QM_GPIO_MODE_OUTPUT: 
            gpio_irq->config.pintype = GPIO_OUTPUT_PIN;
            gpio_irq->config.flag = OUTPUT_FUNCTION_1;
        break;
        case QM_GPIO_MODE_OUTPUT_OD:
            gpio_irq->config.pintype = GPIO_OUTPUT_PIN_OPENDRAIN;
            gpio_irq->config.flag = INPUT_FUNCTION_1 | OPEN_DRAIN;
        break;
        default: break;
    }

    switch(gpio->config.pull_en){
        case QM_GPIO_PULLUP_ONLY:
            gpio_irq->config.flag |= PULLUP;
        break;
        case QM_GPIO_PULLDOWN_ONLY: 
            gpio_irq->config.flag |= PULLDOWN;
        break;
        case QM_GPIO_PULLUP_PULLDOWN:
             gpio_irq->config.flag |= PULLDOWN | PULLUP;
        break;
        case QM_GPIO_FLOATING:  break;
        default:                break;
    }    

    ret = boardctl(APP_BOARDIOC_GPIO, &gpio_irq->config);
    if(ret != QM_EOK){
        return ret;
    }
    
    gpio->priv = (void *)gpio_irq;

    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }

    if(gpio->config.mode == QM_GPIO_MODE_INPUT){
        return -QM_EINVAL;
    }
    
    if(gpio->config.mode == QM_GPIO_MODE_DISABLE){
        return -QM_EINVAL;
    } 

    int ret = gpio_write(gpio->port, level);
    return ret;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }

    if( gpio->config.mode == QM_GPIO_MODE_OUTPUT){
        return 0;
    }

    if( gpio->config.mode == QM_GPIO_MODE_DISABLE){
        return 0;       
    }

    return gpio_read(gpio->port);   
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type,
                            qm_gpio_irq_handler_t handler, void *arg)
{
    int ret;
    uint8_t type;
    _config_t *gpio_irq = NULL;
    _gpio_intr_t *intr_handle = NULL;
    
    if(gpio->priv == NULL){
        return -QM_EINVAL;
    }

    if( gpio->config.mode == QM_GPIO_MODE_OUTPUT ||
        gpio->config.mode == QM_GPIO_MODE_DISABLE){
        return -QM_EINVAL;
    }

    if( intr_type <= QM_GPIO_INTR_DISABLE || 
        intr_type >= QM_GPIO_INTR_MAX){
        return -QM_EINVAL;
    }
    
    gpio_irq = (_config_t *)gpio->priv;

    gpio_irq->irq_isuse = 1;
    gpio_irq->config.pintype = GPIO_INTERRUPT_PIN;

    ret = boardctl(APP_BOARDIOC_GPIO, &gpio_irq->config);
    if(ret != QM_EOK){
        return ret;
    }

    switch(intr_type){
        case QM_GPIO_INTR_POSEDGE:
            type = RISING;
        break;
        case QM_GPIO_INTR_NEGEDGE: 
            type = FALLING;
        break;
        case QM_GPIO_INTR_ANYEDGE:
            type = CHANGE;
        break;
        case QM_GPIO_INTR_LOW_LEVEL:
            type = ONLOW;
        break;
        case QM_GPIO_INTR_HIGH_LEVEL:
            type = ONHIGH;
        break;
        default: 
            type = DISABLED;
        break;
    }

    intr_handle = (_gpio_intr_t *)qm_malloc(sizeof(_gpio_intr_t));
    if(intr_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(intr_handle, 0,sizeof(_gpio_intr_t));
   
    intr_handle->port = gpio_irq->config.pin;
    intr_handle->fn = handler;
    intr_handle->arg=   arg;

    /* 创建一个中断(malloc资源)，GPIO_IRQ_ID_1是标识符，整个应用中断中唯一的 */
    ret = gpio_interrupt_create(1, gpio_irq->config.pin, gpio_interrupt_handler, (void *)intr_handle);
    if (ret != QM_EOK) {
        return -QM_ERROR;
    }

    ret = gpio_interrupt_enable(gpio_irq->config.pin, type);
    if (ret != QM_EOK) {
        return -QM_ERROR;
    }

    return QM_EOK;

}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    _config_t *gpio_irq = NULL;

    if((gpio == NULL || gpio->priv == NULL || 
        gpio->config.mode != QM_GPIO_MODE_INPUT)){
        return -QM_EINVAL;
    } 
    
    gpio_irq = (_config_t *)gpio->priv;
    if(gpio_irq->irq_isuse == 0){
        return -QM_EINVAL;
    }

    return gpio_interrupt_enable(gpio->port, DISABLED);
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    _config_t *gpio_irq = NULL;

    if((gpio == NULL || gpio->priv == NULL || 
        gpio->config.mode != QM_GPIO_MODE_INPUT)){
        return -QM_EINVAL;
    } 

    gpio_irq = (_config_t *)gpio->priv;

    if(gpio_irq->irq_isuse == 1){
        gpio_irq->irq_isuse = 0;
        gpio_interrupt_enable(gpio->port, DISABLED);
    }

    gpio_interrupt_delete(gpio->port);
    return QM_EOK;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    _config_t *gpio_irq = (_config_t *)gpio->priv; 
    if(gpio->config.mode == QM_GPIO_MODE_INPUT){
        if(gpio_irq->irq_isuse == 1){
            gpio_irq->irq_isuse = 0;
            gpio_interrupt_enable(gpio->port, DISABLED);
            gpio_interrupt_delete(gpio->port);
        }
    }
    
    if(gpio_irq){
        qm_free(gpio_irq);
    }
    gpio->priv = NULL;
    return QM_EOK;
}
#endif