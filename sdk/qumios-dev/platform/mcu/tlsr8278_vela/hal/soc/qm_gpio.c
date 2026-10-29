#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#include "qm.h"
#include "qm_log.h"

#include <sys/ioctl.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

#include <nuttx/ioexpander/gpio.h>

#include "mible_api.h"

typedef struct{
    qm_mutex_t mutex;
}qm_gpio_crtl_t;

typedef struct{
    int32_t fd;
    uint8_t use_irq;
    uint8_t irq_index;
}qm_gpio_info_t;

typedef struct{
    int32_t is_use;
    qm_gpio_irq_handler_t handler;
    void *arg;
}qm_gpio_handle_t;


#define QM_GPIO_MAX_NUM  8
// static qm_gpio_crtl_t g_gpio_crtl = {0};
static qm_gpio_handle_t g_gpio_handle[QM_GPIO_MAX_NUM] = {0};



int32_t qm_gpio_init(qm_gpio_dev_t *gpio)
{
    uint8_t pintype;
    char dev_name[15];
    qm_err_t ret = QM_EOK;
    qm_gpio_info_t *config = NULL;
    if(gpio == NULL ){
        return -QM_EINVAL;
    }
    
    config = (qm_gpio_info_t *)qm_malloc(sizeof(qm_gpio_info_t));
    if(config == NULL){
        return -QM_ENOMEM;
    }
    
    memset(config, 0, sizeof(qm_gpio_info_t));
    snprintf(dev_name, sizeof(dev_name), "/dev/gpio%u", gpio->port);

    config->fd = open(dev_name, O_RDWR);
    if (config->fd < 0){
        goto _error;
    }
    
    switch (gpio->config.mode)
    {
        case QM_GPIO_MODE_OUTPUT:
            pintype = GPIO_OUTPUT_PIN;
        break; 
        case QM_GPIO_MODE_OUTPUT_OD:
            pintype = GPIO_OUTPUT_PIN_OPENDRAIN;
        break;     
        case QM_GPIO_MODE_INPUT:
            switch (gpio->config.pull_en)
            {
                case QM_GPIO_PULLUP_ONLY:
                    pintype = GPIO_INPUT_PIN_PULLUP;
                break;
                case QM_GPIO_PULLDOWN_ONLY:
                    pintype = GPIO_INPUT_PIN_PULLDOWN;
                break;  
            default:
                pintype = GPIO_INPUT_PIN;
                break;
            }
        break; 
        default:
            break;
    }
    ret = ioctl(config->fd, GPIOC_SETPINTYPE, (unsigned long) pintype);
    if(ret < 0){
        goto _error;
    }

    gpio->priv = (void *)config;

    return QM_EOK;

_error:
    if(config->fd > 0){
        close(config->fd);
    }

    if(config){
        qm_free(config);
        config = NULL;
    }
    return -QM_ERROR;
}

int32_t qm_gpio_set_level(qm_gpio_dev_t *gpio, uint8_t level)
{
    qm_gpio_info_t *config = NULL;
    if(gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }

    if( gpio->config.mode == QM_GPIO_MODE_INPUT || 
        gpio->config.mode == QM_GPIO_MODE_DISABLE){
        return -QM_EINVAL;
    }

    config = (qm_gpio_info_t *)gpio->priv;
    return ioctl(config->fd, GPIOC_WRITE, (unsigned long)level);
}

uint8_t qm_gpio_get_level(qm_gpio_dev_t *gpio)
{
    uint8_t level = 0;
    qm_gpio_info_t *config = NULL;

    if(gpio == NULL || gpio->priv == NULL){
        return 0;
    }

    if( gpio->config.mode == QM_GPIO_MODE_OUTPUT || 
        gpio->config.mode == QM_GPIO_MODE_DISABLE){
        return 0;
    }

    config = (qm_gpio_info_t *)gpio->priv;
    ioctl(config->fd, GPIOC_READ, (unsigned long)((uintptr_t)&level));  
    return level;
}


static qm_gpio_handle_t *gpio_handle_get(uint8_t index)
{   
    return &g_gpio_handle[index];
}

static int32_t gpio_handle_unused_index_get(void)
{
    for(int i = 0; i < QM_GPIO_MAX_NUM; i++){
        if(!g_gpio_handle[i].is_use){
            return i;  
        }
    }
    return -QM_EINVAL;
}

static void gpio_irq_handler(void *args)
{
    int32_t index = (int32_t)args;
    qm_gpio_handle_t *gpio_handle = NULL;

    if(args == NULL){
        return;
    }

    // qm_printf("gpio_irq_handler\r\n");

    index = index - 17;
    gpio_handle = gpio_handle_get(index);

    // qm_mutex_lock(&g_gpio_crtl.mutex, QM_WAIT_FOREVER);
    if(gpio_handle->handler){
        gpio_handle->handler(gpio_handle->arg);
    }
    // qm_mutex_unlock(&g_gpio_crtl.mutex);
}

static void gpio_irq_sighandler(int signo, FAR siginfo_t *siginfo,
                              FAR void *context)
{
	mible_task_post(gpio_irq_handler, (void *)signo);
}

int32_t qm_gpio_enable_irq(qm_gpio_dev_t *gpio, qm_gpio_intr_type_t intr_type,
                            qm_gpio_irq_handler_t handler, void *arg)
{
    uint8_t pintype;

	struct sigaction act;
    struct sigevent notify;
    static is_init = QM_TRUE;
    
    qm_err_t ret = QM_EOK;
    qm_gpio_info_t *config = NULL;
    qm_gpio_handle_t *gpio_handle = NULL;

    if(gpio == NULL || gpio->priv == NULL){
        return 0;
    }

    if( gpio->config.mode == QM_GPIO_MODE_OUTPUT || 
        gpio->config.mode == QM_GPIO_MODE_DISABLE){
        return -QM_EINVAL;
    }

    if(intr_type <= QM_GPIO_INTR_DISABLE || intr_type >= QM_GPIO_INTR_MAX){
        return -QM_EINVAL;
    }

    config = (qm_gpio_info_t *)gpio->priv;

    // if(is_init){
    //     is_init = QM_FALSE;
    //     qm_mutex_new(&g_gpio_crtl.mutex);
    // }

    switch(intr_type){
        case QM_GPIO_INTR_POSEDGE:
            pintype = GPIO_INTERRUPT_RISING_PIN;
        break;
        case QM_GPIO_INTR_NEGEDGE: 
            pintype = GPIO_INTERRUPT_FALLING_PIN;
        break;
        case QM_GPIO_INTR_ANYEDGE:
            pintype = GPIO_INTERRUPT_BOTH_PIN;
        break;
        case QM_GPIO_INTR_LOW_LEVEL:
            pintype = GPIO_INTERRUPT_LOW_PIN;
        break;
        case QM_GPIO_INTR_HIGH_LEVEL:
            pintype = GPIO_INTERRUPT_HIGH_PIN;
        break;
        default: 
            pintype = GPIO_INTERRUPT_PIN;
        break;
    }

    ret = ioctl(config->fd, GPIOC_SETPINTYPE, (unsigned long) pintype);
    if(ret < 0){
        return -QM_EINVAL;
    }

    config->irq_index = gpio_handle_unused_index_get();
    if(config->irq_index < 0){
        return -QM_EINVAL;
    }

    gpio_handle = gpio_handle_get(config->irq_index);

    notify.sigev_notify = SIGEV_SIGNAL;
    notify.sigev_signo  = 17 + config->irq_index;
    ret = ioctl(config->fd, GPIOC_REGISTER, (unsigned long)&notify);
    if(ret < 0){
        return -QM_ERROR;
    }

	act.sa_sigaction = gpio_irq_sighandler;
	act.sa_flags  = SA_SIGINFO;

	sigfillset(&act.sa_mask);
	sigdelset(&act.sa_mask, notify.sigev_signo);

	ret = sigaction(notify.sigev_signo, &act, NULL);
	if (ret != QM_EOK) {
        ioctl(config->fd, GPIOC_UNREGISTER, 0);
		return -QM_ERROR;
	}

    config->use_irq = QM_TRUE;

    // qm_mutex_lock(&g_gpio_crtl.mutex, QM_WAIT_FOREVER);
    gpio_handle->is_use = QM_TRUE;
    gpio_handle->handler = handler;
    gpio_handle->arg = arg;
    // qm_mutex_unlock(&g_gpio_crtl.mutex);

    return QM_EOK;
}

int32_t qm_gpio_disable_irq(qm_gpio_dev_t *gpio)
{

    qm_gpio_info_t *config = NULL;
    qm_gpio_handle_t * gpio_handle = NULL;

    if(gpio == NULL || gpio->priv == NULL){
        return -QM_EINVAL;
    }

    config = (qm_gpio_info_t *)gpio->priv;
    if(config->use_irq == QM_FALSE){
        return -QM_EINVAL;
    }

    gpio_handle = gpio_handle_get(config->irq_index);
    if(gpio_handle == NULL ){
        return -QM_EINVAL;
    }
    
    ioctl(config->fd, GPIOC_UNREGISTER, 0);

    // qm_mutex_lock(&g_gpio_crtl.mutex, QM_WAIT_FOREVER);
    memset(gpio_handle, 0, sizeof(qm_gpio_info_t));
    // qm_mutex_unlock(&g_gpio_crtl.mutex);   

    config->use_irq = QM_FALSE;

    return QM_EOK;
}

int32_t qm_gpio_clear_irq(qm_gpio_dev_t *gpio)
{

    return QM_EOK;
}

int32_t qm_gpio_deinit(qm_gpio_dev_t *gpio)
{
    qm_gpio_info_t *config = NULL;
    qm_gpio_handle_t * gpio_handle = NULL;

    if(gpio == NULL || gpio->priv == NULL){
        return 0;
    }

    gpio_handle = gpio_handle_get(config->irq_index);
    if(gpio_handle == NULL){
        return -QM_EINVAL;
    }
    
    config = (qm_gpio_info_t *)gpio->priv;
    if(config->use_irq == QM_FALSE){
        goto _next;
    }

    ioctl(config->fd, GPIOC_UNREGISTER, 0);

    // qm_mutex_lock(&g_gpio_crtl.mutex, QM_WAIT_FOREVER);
    memset(gpio_handle, 0, sizeof(qm_gpio_info_t));
    // qm_mutex_unlock(&g_gpio_crtl.mutex);   

_next:

    config->use_irq = QM_FALSE;

    if(config->fd > 0){
        close(config->fd);
    }

    if(config){
        qm_free(config);
        config = NULL;
    }

    gpio->priv = NULL;

    return QM_EOK;
}
