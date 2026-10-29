#include "qm.h"


#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <errno.h>
#include "qm_pwm.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_gpio.h"


#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <assert.h>

#include "hosal_gpio.h"


int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    hosal_gpio_dev_t hosal_gpio = {0};
    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }
    if(gpio->config.mode == QM_GPIO_MODE_INPUT)
    {
        if(gpio->config.pull_en == QM_GPIO_PULLUP_ONLY) //上拉输入
        {
            hosal_gpio.config = INPUT_PULL_UP;
        }
        else if(gpio->config.pull_en == QM_GPIO_PULLDOWN_ONLY) //下拉输入
        {
            hosal_gpio.config = INPUT_PULL_DOWN;
        }
    }
    else if(gpio->config.mode == QM_GPIO_MODE_OUTPUT)
    {
        hosal_gpio.config = OUTPUT_PUSH_PULL;
    }

    hosal_gpio.port = gpio->port;

    hosal_gpio_init(&hosal_gpio);

    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    hosal_gpio_dev_t hosal_gpio = {0};
    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }

    if(gpio->config.mode == QM_GPIO_MODE_INPUT)
    {
        if(gpio->config.pull_en == QM_GPIO_PULLUP_ONLY) //上拉输入
        {
            hosal_gpio.config = INPUT_PULL_UP;
        }
        else if(gpio->config.pull_en == QM_GPIO_PULLDOWN_ONLY) //下拉输入
        {
            hosal_gpio.config = INPUT_PULL_DOWN;
        }
    }
    else if(gpio->config.mode == QM_GPIO_MODE_OUTPUT)
    {
        hosal_gpio.config = OUTPUT_PUSH_PULL;
    }

    hosal_gpio.port = gpio->port;


    hosal_gpio_output_set(&hosal_gpio, level);
    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    uint8_t value;
    hosal_gpio_dev_t hosal_gpio = {0};
    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }
    if(gpio->config.mode == QM_GPIO_MODE_INPUT)
    {
        if(gpio->config.pull_en == QM_GPIO_PULLUP_ONLY) //上拉输入
        {
            hosal_gpio.config = INPUT_PULL_UP;
        }
        else if(gpio->config.pull_en == QM_GPIO_PULLDOWN_ONLY) //下拉输入
        {
            hosal_gpio.config = INPUT_PULL_DOWN;
        }
    }
    else if(gpio->config.mode == QM_GPIO_MODE_OUTPUT)
    {
        hosal_gpio.config = OUTPUT_PUSH_PULL;
    }

    hosal_gpio.port = gpio->port;

    hosal_gpio_input_get(&hosal_gpio, &value);
    return value;
}
#if 1
int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type, qm_gpio_irq_handler_t handler, void *arg)
{
    hosal_gpio_dev_t hosal_gpio = {0};
    qm_err_t ret = QM_EOK;
    uint8_t gpio_pin;
    hosal_gpio_irq_trigger_t irq_type = 0;
    if (gpio == NULL || handler == NULL)
    {
        return -QM_EINVAL;
    }
   
    switch(intr_type)
    {
        case QM_GPIO_INTR_POSEDGE:    //上升沿
            irq_type = HOSAL_IRQ_TRIG_POS_PULSE;
            break;
        case QM_GPIO_INTR_NEGEDGE: //下降沿
            irq_type = HOSAL_IRQ_TRIG_NEG_PULSE;
            break;
        case QM_GPIO_INTR_LOW_LEVEL: //低电平
            irq_type = HOSAL_IRQ_TRIG_NEG_LEVEL;
            break;
        case QM_GPIO_INTR_HIGH_LEVEL: //高电平
            irq_type = HOSAL_IRQ_TRIG_POS_LEVEL;
            break;

        default:
            return QM_ERROR;
    }

    if(gpio->config.mode == QM_GPIO_MODE_INPUT)
    {
        if(gpio->config.pull_en == QM_GPIO_PULLUP_ONLY) //上拉输入
        {
            hosal_gpio.config = INPUT_PULL_UP;
        }
        else if(gpio->config.pull_en == QM_GPIO_PULLDOWN_ONLY) //下拉输入
        {
            hosal_gpio.config = INPUT_PULL_DOWN;
        }
    }

    hosal_gpio.port = gpio->port;

    hosal_gpio_irq_set(&hosal_gpio, irq_type, handler, arg);
    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }
    // if(gpio_intr_disable(gpio->port) != ESP_OK){
    //     return -QM_ERROR;
    // }
    // gpint_enable(gpio->port, 0);
    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }
    // if(gpio_set_intr_type(gpio->port, GPIO_INTR_DISABLE) != ESP_OK){
    //     return -QM_ERROR;
    // }
    // gpint_enable(gpio->port, 0);
    return QM_EOK;
}
#endif
int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    if (gpio == NULL)
    {
        return -QM_EINVAL;
    }
    // bl602_gpio_deinit(gpio->port);
    // gpint_enable(gpio->port, 0);
    return QM_EOK;
}

