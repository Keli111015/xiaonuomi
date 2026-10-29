#include "qm_gpio.h"
#include "qm_errno.h"
#include "driver/gpio.h"

static int gpio_isr_install = 0;

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    gpio_config_t io_conf = {0};

    if(gpio == NULL){
        return -QM_EINVAL;
    }

    io_conf.intr_type = GPIO_INTR_DISABLE;

    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_DISABLE:
            io_conf.mode = GPIO_MODE_DISABLE;
        break;
        case QM_GPIO_MODE_INPUT:
            io_conf.mode = GPIO_MODE_INPUT;
        break;
        case QM_GPIO_MODE_OUTPUT:
            io_conf.mode = GPIO_MODE_OUTPUT;
        break;
        case QM_GPIO_MODE_OUTPUT_OD:
            io_conf.mode = GPIO_MODE_OUTPUT_OD;
        break;
        case QM_GPIO_MODE_INPUT_OUTPUT_OD:
            io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
        break;
        case QM_GPIO_MODE_INPUT_OUTPUT:
            io_conf.mode = GPIO_MODE_INPUT_OUTPUT;
        break;
        default :
            return -QM_EIO;
        break;
    }

    io_conf.pin_bit_mask = (1ULL<<(gpio->port)); 

    switch (gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        break;
        case QM_GPIO_PULLDOWN_ONLY:
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        break;       
        case QM_GPIO_PULLUP_PULLDOWN:
            io_conf.pull_down_en = GPIO_PULLDOWN_ENABLE;
            io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
        break;      
        case QM_GPIO_FLOATING:
            io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
            io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
        break;       
        default:
            return -QM_EIO;
        break;
    }

    if(gpio_config(&io_conf) != ESP_OK){
        return -QM_EIO;
    }

    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }

    if(gpio_set_level(gpio->port, (uint32_t)level) != ESP_OK){
        return -QM_EIO;
    }
    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }
    return (uint8_t)gpio_get_level(gpio->port);
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type, qm_gpio_irq_handler_t handler, void *arg)
{
    qm_err_t ret = QM_EOK;

    if(gpio == NULL || handler == NULL){
        return -QM_EINVAL;
    }

    switch (intr_type)
    {
        case QM_GPIO_INTR_DISABLE:
            ret = (gpio_set_intr_type(gpio->port, GPIO_INTR_DISABLE) == ESP_OK) ? QM_EOK : -QM_EINTR;
        break;
        case QM_GPIO_INTR_POSEDGE:
            ret = (gpio_set_intr_type(gpio->port, GPIO_INTR_POSEDGE) == ESP_OK) ? QM_EOK : -QM_EINTR;
        break;
        case QM_GPIO_INTR_NEGEDGE:
            ret = (gpio_set_intr_type(gpio->port, GPIO_INTR_NEGEDGE) == ESP_OK) ? QM_EOK : -QM_EINTR;
        break;
        case QM_GPIO_INTR_ANYEDGE:
            ret = (gpio_set_intr_type(gpio->port, GPIO_INTR_ANYEDGE) == ESP_OK) ? QM_EOK : -QM_EINTR;
        break;
        case QM_GPIO_INTR_LOW_LEVEL:
            ret = (gpio_set_intr_type(gpio->port, GPIO_INTR_LOW_LEVEL) == ESP_OK) ? QM_EOK : -QM_EINTR;
        break;
        case QM_GPIO_INTR_HIGH_LEVEL:
            ret = (gpio_set_intr_type(gpio->port, GPIO_INTR_HIGH_LEVEL) == ESP_OK) ? QM_EOK : -QM_EINTR;
        break;
        case QM_GPIO_INTR_MAX:
            ret = (gpio_set_intr_type(gpio->port, GPIO_INTR_MAX) == ESP_OK) ? QM_EOK : -QM_EINTR;
        break;
        default:
            return -QM_EINTR;
        break;
    }

    if(ret != QM_EOK){
        return ret;
    }

    if(gpio_intr_enable(gpio->port) != ESP_OK){
        return -QM_EINTR;
    }

    if(!gpio_isr_install){
        gpio_install_isr_service(0);
        gpio_isr_install = 1;
    }

    if(gpio_isr_handler_add(gpio->port, handler, arg) != ESP_OK){
        return -QM_EINTR;
    }

    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }
    if(gpio_intr_disable(gpio->port) != ESP_OK){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    if(gpio == NULL){
        return -QM_EINVAL;
    }
    if(gpio_set_intr_type(gpio->port, GPIO_INTR_DISABLE) != ESP_OK){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    return QM_EOK;
}
