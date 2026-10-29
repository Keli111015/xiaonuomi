#include "qm.h"
#include "qm_gpio.h"

#include "liot_gpio.h"



typedef struct{
    liot_gpio_dir_e dir;
    liot_gpio_pull_mode_e pull;
}gpio_config_t;

typedef struct{
    uint8_t pin_id;
    uint8_t func_id;
    gpio_config_t *config;
}gpio_list_info_t;

static gpio_list_info_t g_gpio_info[200] = 
{
    [16] = {LIOT_GPIO_27, 0, NULL},
    [19] = {LIOT_GPIO_22, 0, NULL},
    [20] = {LIOT_GPIO_24, 0, NULL},
    [84] = {LIOT_GPIO_10, 0, NULL},
    [97] = {LIOT_GPIO_16, 4, NULL},
    [99] = {LIOT_GPIO_23, 0, NULL},
    [100] = {LIOT_GPIO_17, 4, NULL},
    [102] = {LIOT_GPIO_20, 0, NULL},
    [106] = {LIOT_GPIO_25, 0, NULL},
};




int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    int ret = 0;
    static int init = 1;
    liot_gpio_lvl_mode_e lvl_mode = LIOT_LVL_LOW;
    gpio_list_info_t *gpio_info = NULL;

    if(gpio == NULL){
        return -QM_EINVAL;
    }

    if(gpio->port >= QM_ARRAY_SIZE(g_gpio_info)){
        return -QM_EINVAL;
    }
    gpio_info = &g_gpio_info[gpio->port];

    gpio_config_t *gpio_config = (gpio_config_t *)qm_malloc(sizeof(gpio_config_t));
    if(gpio_config == NULL){
        return -QM_ENOMEM;
    }
    memset(gpio_config, 0, sizeof(gpio_config_t));
	
    if(init){
        init = 0;
        //使能AON POWER AON与普通GPIO电压不同，需要单独使能电源域
	    liot_aon_power_on();	
    }

    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_INPUT:
            gpio_config->dir = LIOT_GPIO_INPUT;
            break;
        case QM_GPIO_MODE_OUTPUT:
            gpio_config->dir = LIOT_GPIO_OUTPUT;
            break;
        default:
            ret = -QM_EIO;
            break;
    }

    switch (gpio->config.pull_en)
    {
        case QM_GPIO_PULLUP_ONLY:
            lvl_mode = LIOT_LVL_HIGH;
            gpio_config->pull = LIOT_FORCE_PULL_UP;
            break;
        case QM_GPIO_PULLDOWN_ONLY:
            lvl_mode = LIOT_LVL_LOW;
            gpio_config->pull = LIOT_FORCE_PULL_DOWN;
            break;
        case QM_GPIO_FLOATING:
            gpio_config->pull = LIOT_FORCE_PULL_NONE;
            break;
        default:
            ret = -QM_EIO;
            break;
    }

    if(ret != QM_EOK){
        qm_free(gpio_config);
        return ret;
    }

#if 0
    ret = liot_pin_set_func(gpio->port, gpio_info->func_id);
    if(ret != LIOT_GPIO_SUCCESS){
        qm_free(gpio_config);
        return -QM_EIO;
    }

    ret = liot_gpio_init(gpio_info->pin_id, gpio_config->dir, gpio_config->pull, lvl_mode);
    if(ret != LIOT_GPIO_SUCCESS){
        qm_free(gpio_config);
        return -QM_EIO;
    }
#else
    liot_gpio_init_ex(gpio->port, gpio_config->dir, gpio_config->pull, lvl_mode);
#endif

    gpio_info->config = gpio_config;

    gpio->priv = gpio_info;

    return QM_EOK;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    int ret = QM_EOK;
    liot_gpio_lvl_mode_e lvl_mode = LIOT_LVL_LOW;
    if (gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }
    gpio_list_info_t *gpio_info = (gpio_list_info_t *)gpio->priv;

    if(level){
        lvl_mode = LIOT_LVL_HIGH;
    }else{
        lvl_mode = LIOT_LVL_LOW;
    }
    ret = liot_gpio_set_level(gpio_info->pin_id, lvl_mode);

    return QM_EOK;
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    uint8_t level = 0;
    liot_gpio_lvl_mode_e lvl_mode = LIOT_LVL_LOW;
    gpio_list_info_t *gpio_info = (gpio_list_info_t *)gpio->priv;
    if (gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }

    liot_gpio_get_level(gpio_info->pin_id, &lvl_mode);

    if(lvl_mode == LIOT_LVL_HIGH){
        level = 1;
    }else{
        level = 0;
    }

    return level;
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type, qm_gpio_irq_handler_t handler, void *arg)
{
    liot_gpio_edge_mode_e edge = 0;
    liot_gpio_trigger_mode_e trigger = 0;
    gpio_list_info_t *gpio_info = (gpio_list_info_t *)gpio->priv;
    if (gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }
    gpio_config_t *gpio_config = (gpio_config_t *)gpio_info->config;

    switch(intr_type)
    {
        case QM_GPIO_INTR_ANYEDGE: //下降沿
            edge = LIOT_EDGE_BOTH;
            trigger = LIOT_EDGE_TRIGGER;
            break;
        case QM_GPIO_INTR_POSEDGE:    //上升沿
            edge = LIOT_EDGE_RISING;
            trigger = LIOT_EDGE_TRIGGER;
            break;
        case QM_GPIO_INTR_NEGEDGE: //下降沿
            edge = LIOT_EDGE_FALLING;
            trigger = LIOT_EDGE_TRIGGER;
            break;
        case QM_GPIO_INTR_LOW_LEVEL: //低电平
            edge = LIOT_EDGE_RISING;
            trigger = LIOT_LEVEL_TRIGGER;
            break;
        case QM_GPIO_INTR_HIGH_LEVEL: //高电平
            edge = LIOT_EDGE_FALLING;
            trigger = LIOT_LEVEL_TRIGGER;
            break;

        default:
            
            break;
    }

    
    /*
    * MAIN_RI 引脚的中断回调中，获取中断引脚电平，并发送消息到消息队列处理任务中
    * 
    * Notes: 中断回调中不可执行耗时较长的操作，不可调用阻塞性接口，否则会引起系统异常。
    */
    liot_int_register(gpio_info->pin_id,trigger,LIOT_DEBOUNCE_DIS, edge, gpio_config->pull, handler, arg);
    
    liot_int_enable(gpio_info->pin_id);
    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{
    gpio_list_info_t *gpio_info = (gpio_list_info_t *)gpio->priv;
    if (gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }
    gpio_config_t *gpio_config = (gpio_config_t *)gpio_info->config;

    liot_int_disable(gpio_info->pin_id);

    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{
    return QM_EOK;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    return QM_EOK;
}