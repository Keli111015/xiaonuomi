#include "qm_lpc.h"
#include "qm_lpc_hal.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "pm.h"
#include "gpio.h"

static lpc_callback_t wakeup_cb = NULL;

int32_t qm_hal_lpc_init(void)
{
    //TODO: impl
    // SLEEP_SleepBlockBegin(sleepEM3); //run in EM0/EM1/EM2 state
    return QM_EOK;
}


int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode)
{
    //TODO: impl
    return QM_EOK;
}

void gpio_wakup(uint8_t pin)
{
    QM_LOGD("LOG", "gpio wakeup:%d\n", pin);
}

int32_t qm_hal_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t cb ,bool_t enable)
{
    int gpio = (pin & 0xF0) << 4; 
    gpio |= 1<< (pin & 0x0F);
    int level = Level_Low;
    if (type == QM_LPC_WAKEUP_HIGH_LEVEL || type == QM_LPC_WAKEUP_POSEDGE || type == QM_LPC_WAKEUP_ANYEDGE)
    {
        level = Level_High;
    }
    //setup input mode
    gpio_set_func(gpio, AS_GPIO);
    gpio_set_input_en(gpio, 1);
    gpio_set_output_en(gpio, 0);
    //setup pull up/down
    if (level == Level_High)
    {
        gpio_setup_up_down_resistor(gpio, PM_PIN_PULLDOWN_100K);
    }
    else
    {
        gpio_setup_up_down_resistor(gpio, PM_PIN_PULLUP_10K);
    }
    cpu_set_gpio_wakeup(gpio, level, enable);  

    return QM_EOK;
}
