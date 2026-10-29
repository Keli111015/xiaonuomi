#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "gpio.h"


int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }
    
    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_INPUT:
            hal_gpio_pin_init(gpio->port, GPIO_INPUT);
        break;
        case QM_GPIO_MODE_OUTPUT:
            hal_gpio_pin_init(gpio->port, GPIO_OUTPUT);
        break;
        default :
            return -QM_EINVAL;
        break;
    }
    
    switch (gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
            hal_gpio_pull_set(gpio->port, GPIO_PULL_UP);
        break;
        case QM_GPIO_PULLDOWN_ONLY:
            hal_gpio_pull_set(gpio->port, GPIO_PULL_DOWN);
        break;       
        case QM_GPIO_FLOATING:
            hal_gpio_pull_set(gpio->port, GPIO_FLOATING);
        break;       
        default:
            return -QM_EINVAL;
        break;
    }

    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    if(gpio == NULL || level > 1){
        return -QM_EINVAL;
    }

    hal_gpio_write(gpio->port, level);
		
	return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }
    return (uint8_t)hal_gpio_read(gpio->port);
}

static void gpioin_hdl_negedge(gpio_pin_e pin,gpio_polarity_e type)
{

}

static void gpioin_hdl_posedge(gpio_pin_e pin,gpio_polarity_e type)
{

}

static qm_gpio_irq_handler_t qm_gpio_irq_info[23] = {0};

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type,qm_gpio_irq_handler_t handler, void *arg)
{
    int32_t ret = QM_EOK;

    if((gpio == NULL) || (handler == NULL) || QM_GPIO_INTR_DISABLE)
    {
        return -QM_EINVAL;
    }
    
    qm_gpio_irq_info[gpio->port] = handler;

    switch (intr_type)
    {
        case QM_GPIO_INTR_POSEDGE:
            ret = hal_gpioin_register(gpio->port,gpioin_hdl_posedge, NULL);
        break;
        case QM_GPIO_INTR_NEGEDGE:
            ret = hal_gpioin_register(gpio->port, NULL, gpioin_hdl_negedge);
        break;
        case QM_GPIO_INTR_ANYEDGE:
            ret = hal_gpioin_register(gpio->port, gpioin_hdl_posedge, gpioin_hdl_negedge);
        break;       
        default:
            ret = -QM_EINTR;
        break;
    }
    return ret;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL)
    {
        return -QM_EINVAL;
    }

    return hal_gpioin_unregister(gpio->port);
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL)
    {
        return -QM_EINVAL;
    }
    return QM_EOK;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    return QM_EOK;
}