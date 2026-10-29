#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_kernel.h"
//#include "qm_platform.h"

//#include "bl_gpio.h"

#include "hal/hal_common.h"
#include "hal/hal_gpio.h"


// #define BL_GPIO_PULL_CLOSE      0//上拉下拉
// #define BL_GPIO_PULL_OPEN       1
#define GROUP_A                 GPIOA_BASE//如果映射不会的话，可以考虑一下，在大于某些引脚的时候是GROUP_B,类似的思考吧
#define GROUP_B                 GPIOB_BASE

//直接对两个组进行操作了，缺少分组的参数，可能需要自己写一下引脚的映射
int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    
    if (gpio == NULL) {
        return -QM_EINVAL;
    }
    hal_gpio_pin_pull_set(GPIOA_BASE, gpio->port, GPIO_PULL_DOWN);
    hal_gpio_pin_pull_set(GPIOB_BASE, gpio->port, GPIO_PULL_DOWN);
    
    // int pullup = BL_GPIO_PULL_CLOSE;
    // int pulldown = BL_GPIO_PULL_CLOSE;

    switch (gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
            hal_gpio_pin_pull_set(GROUP_A, gpio->port, GPIO_PULL_UP);
            hal_gpio_pin_pull_set(GROUP_B, gpio->port, GPIO_PULL_UP);
            break;
        case QM_GPIO_PULLDOWN_ONLY:
            hal_gpio_pin_pull_set(GROUP_A, gpio->port, GPIO_PULL_DOWN);
            hal_gpio_pin_pull_set(GROUP_B, gpio->port, GPIO_PULL_DOWN);
            // pulldown = BL_GPIO_PULL_OPEN;
            break;
        case QM_GPIO_PULLUP_PULLDOWN:
            hal_gpio_pin_pull_set(GROUP_A, gpio->port, GPIO_PULL_UP_AND_PULL_DOWN);
            hal_gpio_pin_pull_set(GROUP_B, gpio->port, GPIO_PULL_UP_AND_PULL_DOWN);
            // pullup = BL_GPIO_PULL_OPEN;
            // pulldown = BL_GPIO_PULL_OPEN;
            break;
        case QM_GPIO_FLOATING:
            hal_gpio_pin_pull_set(GROUP_A, gpio->port, GPIO_PULL_NONE);
            hal_gpio_pin_pull_set(GROUP_B, gpio->port, GPIO_PULL_NONE);
            break;
        default:
            return -QM_EIO;
    }

    switch (gpio->config.mode)
    {   
        case QM_GPIO_MODE_INPUT:
            hal_gpio_pin_direction_set(GROUP_A, gpio->port, GPIO_INPUT);
            hal_gpio_pin_direction_set(GROUP_B, gpio->port, GPIO_INPUT);
            // bl_gpio_enable_input(gpio->port, pullup, pulldown);//博流的函数
            break;
        case QM_GPIO_MODE_OUTPUT:
            // bl_gpio_enable_output(gpio->port, pullup, pulldown);//博流的函数
            hal_gpio_pin_direction_set(GROUP_A, gpio->port, GPIO_OUTPUT);
            hal_gpio_pin_direction_set(GROUP_B, gpio->port, GPIO_OUTPUT);
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

    // bl_gpio_output_set(gpio->port, level);//博流的函数，输出设置
    hal_gpio_port_output_write(gpio->port, level);
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

    // bl_gpio_input_get_value(gpio->port);
    hal_gpio_pin_input_read(GROUP_A, gpio->port);
    hal_gpio_pin_input_read(GROUP_B, gpio->port);
    return QM_EOK;
}

#ifdef BL_HAL//这里得改一下
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
            hal_gpio_pin_it_cfg(GROUP_A, gpio->port, GPIO_INT_RISING);
            hal_gpio_pin_it_cfg(GROUP_B, gpio->port, GPIO_INT_RISING);
            // hal_gpio_enable_irq(gpio_dev, IRQ_TRIGGER_RISING_EDGE, handler, arg);
        break;
        case QM_GPIO_INTR_NEGEDGE: 
            hal_gpio_pin_it_cfg(GROUP_A, gpio->port, GPIO_INT_FALLING);
            hal_gpio_pin_it_cfg(GROUP_B, gpio->port, GPIO_INT_FALLING);
            // hal_gpio_enable_irq(gpio_dev, IRQ_TRIGGER_FALLING_EDGE, handler, arg);
        break;
        case QM_GPIO_INTR_ANYEDGE:
            hal_gpio_pin_it_cfg(GROUP_A, gpio->port, GPIO_INT_RISING_FALLING);
            hal_gpio_pin_it_cfg(GROUP_B, gpio->port, GPIO_INT_RISING_FALLING);
            // hal_gpio_enable_irq(gpio_dev, IRQ_TRIGGER_BOTH_EDGES, handler, arg);
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
    hal_gpio_pin_it_en(GROUP_A,  gpio->port, HAL_DISABLE);
    hal_gpio_pin_it_en(GROUP_B,  gpio->port, HAL_DISABLE);
    // hal_gpio_disable_irq(gpio_dev);
    
    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    if (gpio->priv == NULL || gpio->config.mode != QM_GPIO_MODE_INPUT) {
        return -QM_EINVAL;
    } 

    gpio_dev_t *gpio_dev = (gpio_dev_t *)gpio->priv;
    hal_gpio_pin_clr_it_flag(GROUP_A, gpio->port);
    hal_gpio_pin_clr_it_flag(GROUP_B, gpio->port);
    // hal_gpio_clear_irq(gpio_dev);

    return QM_EOK;
}
#endif
