#include "qm.h"

#if CONFIG_QM_GPIO_SUPPORT

#include "ls_soc_gpio.h"
#include "qm_gpio.h"
#include "qm_utils_list.h"

#define LOG_TAG "gpio"

typedef struct 
{
    int port;
    void *arg;
    void (*fn)(void *);
}qm_gpio_hal_ctx_t;


static qm_list_t *gpio_intr_list = NULL;

static int list_match(void *a, void *b)
{
    int a_handle = (int)a;
    qm_gpio_hal_ctx_t *b_handle = (qm_gpio_hal_ctx_t*)b;

    if(b_handle->port == a_handle){
        return QM_TRUE;
    }
    return QM_FALSE;
}

void io_exti_callback(uint8_t pin, exti_edge_t edge) 
{
    qm_list_node_t *node = NULL;
    qm_gpio_hal_ctx_t *intr_handle = NULL;
    node = qm_list_find(gpio_intr_list, (void *)pin);
    if (node == NULL){
        return;   
    }
    intr_handle = (qm_gpio_hal_ctx_t *)node->val;
    intr_handle->fn(intr_handle->arg);
}

int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio inval");
    switch(gpio->config.mode)
    {
        case QM_GPIO_MODE_INPUT:
            io_cfg_input(gpio->port);
        break;
        case QM_GPIO_MODE_OUTPUT: 
            io_cfg_output(gpio->port);
        break;
        case QM_GPIO_MODE_OUTPUT_OD:
            io_cfg_opendrain(gpio->port);
        break;
        default: break;
    }

    switch(gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
            io_pull_write(gpio->port, IO_PULL_UP); 
        break;
        case QM_GPIO_PULLDOWN_ONLY: 
            io_pull_write(gpio->port, IO_PULL_DOWN); 
        break;
        case QM_GPIO_PULLUP_PULLDOWN:
            io_pull_write(gpio->port, IO_PULL_UP_DOWN); 
        break;
        case QM_GPIO_FLOATING:  
            io_pull_write(gpio->port, IO_PULL_DISABLE); 
        break;
        default:                break;
    }    		

    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio inval");
    io_write_pin(gpio->port,level);  //LED1 write low level

    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio inval");

    return io_read_pin(gpio->port);
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio inval");

    return QM_EOK;
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type, qm_gpio_irq_handler_t handler, void *arg)
{
    static int is_init = 1;
    qm_list_node_t *node = NULL;
    qm_gpio_hal_ctx_t *intr_handle = NULL;

    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio inval");

    if(is_init){
        gpio_intr_list = qm_list_new();
        if(gpio_intr_list == NULL){
            return -QM_ENOMEM;
        }
        gpio_intr_list->match = list_match;
        is_init = 0;
    }

    node = qm_list_find(gpio_intr_list, (void *)gpio->port);
    if (node){
        return -QM_ENOMEM;
    }

    node = qm_list_node_extra_new(sizeof(qm_gpio_hal_ctx_t));
    if(node == NULL){
        return -QM_ENOMEM;
    }
    intr_handle = (qm_gpio_hal_ctx_t *)node->val;

    intr_handle->port = gpio->port;
    intr_handle->fn = handler;
    intr_handle->arg = arg;

    qm_list_rpush(gpio_intr_list, node);

    switch(intr_type){
        case QM_GPIO_INTR_POSEDGE:
            io_exti_config(gpio->port, INT_EDGE_RISING);
        break;
        case QM_GPIO_INTR_NEGEDGE: 
            io_exti_config(gpio->port, INT_EDGE_FALLING);
            
        break;
        case QM_GPIO_INTR_ANYEDGE:
            io_exti_config(gpio->port, INT_EDGE_BOTH);
        break;
        default: 
        break;
    }

    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    qm_list_node_t *node = NULL;
    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio inval");
    QM_LOGI(LOG_TAG, "disable irq");

    node = qm_list_find(gpio_intr_list, (void *)gpio->port);
    if (node == NULL){
        return -QM_ENOMEM;
    }
    qm_list_remove(gpio_intr_list, node);
    qm_list_node_destroy(node);

    io_exti_config(gpio->port, INT_EDGE_DISABLE);
    
    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    QM_RETURN_ON_FALSE(gpio, -QM_EINVAL, LOG_TAG, "gpio inval");

    return QM_EOK;
}
 

#endif
