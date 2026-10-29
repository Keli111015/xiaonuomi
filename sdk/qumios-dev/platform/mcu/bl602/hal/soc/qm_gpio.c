#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_platform.h"

#include "bl_gpio.h"

#define BL_GPIO_PULL_CLOSE      0
#define BL_GPIO_PULL_OPEN       1


int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    if (gpio == NULL) {
        return -QM_EINVAL;
    }

    int pullup = BL_GPIO_PULL_CLOSE;
    int pulldown = BL_GPIO_PULL_CLOSE;

    switch (gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
            pullup = BL_GPIO_PULL_OPEN;
            break;
        case QM_GPIO_PULLDOWN_ONLY:
            pulldown = BL_GPIO_PULL_OPEN;
            break;
        case QM_GPIO_PULLUP_PULLDOWN:
            pullup = BL_GPIO_PULL_OPEN;
            pulldown = BL_GPIO_PULL_OPEN;
            break;
        case QM_GPIO_FLOATING:
            break;
        default:
            return -QM_EIO;
    }

    switch (gpio->config.mode)
    {   
        case QM_GPIO_MODE_INPUT:
            bl_gpio_enable_input(gpio->port, pullup, pulldown);
            break;
        case QM_GPIO_MODE_OUTPUT:
            bl_gpio_enable_output(gpio->port, pullup, pulldown);
            break;
        default:
            return -QM_EIO;
    }

    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    if (gpio == NULL) {
        return -QM_EINVAL;
    }
    if (gpio->config.mode == QM_GPIO_MODE_INPUT || 
            gpio->config.mode == QM_GPIO_MODE_DISABLE) {
        return -QM_EINVAL;
    }

    bl_gpio_output_set(gpio->port, level);
        
    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    if (gpio == NULL) {
        return 0;
    }
    if (gpio->config.mode == QM_GPIO_MODE_OUTPUT || 
            gpio->config.mode == QM_GPIO_MODE_DISABLE) {
        return -QM_EINVAL;
    }

    bl_gpio_input_get_value(gpio->port);

    return QM_EOK;
}

#ifdef BL_HAL
#include "hal_gpio.h"

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type,
                            qm_gpio_irq_handler_t handler, void *arg)
{

    if (gpio->config.mode == QM_GPIO_MODE_OUTPUT || 
            gpio->config.mode == QM_GPIO_MODE_DISABLE) {
        return 0;
    }
    if (intr_type <= QM_GPIO_INTR_DISABLE || intr_type >= QM_GPIO_INTR_MAX) {
        return -QM_EINVAL;
    }

    gpio_dev_t *gpio_dev = (gpio_dev_t *)gpio->priv;

    switch(intr_type){
        case QM_GPIO_INTR_POSEDGE:
            hal_gpio_enable_irq(gpio_dev, IRQ_TRIGGER_RISING_EDGE, handler, arg);
        break;
        case QM_GPIO_INTR_NEGEDGE: 
            hal_gpio_enable_irq(gpio_dev, IRQ_TRIGGER_FALLING_EDGE, handler, arg);
        break;
        case QM_GPIO_INTR_ANYEDGE:
            hal_gpio_enable_irq(gpio_dev, IRQ_TRIGGER_BOTH_EDGES, handler, arg);
        break;
        default: 
            return -QM_EINVAL;
    }

    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    if (gpio->priv == NULL || gpio->config.mode != QM_GPIO_MODE_INPUT) {
        return -QM_EINVAL;
    } 

    gpio_dev_t *gpio_dev = (gpio_dev_t *)gpio->priv;

    hal_gpio_disable_irq(gpio_dev);
    
    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    if (gpio->priv == NULL || gpio->config.mode != QM_GPIO_MODE_INPUT) {
        return -QM_EINVAL;
    } 

    gpio_dev_t *gpio_dev = (gpio_dev_t *)gpio->priv;

    hal_gpio_clear_irq(gpio_dev);

    return QM_EOK;
}
#endif
