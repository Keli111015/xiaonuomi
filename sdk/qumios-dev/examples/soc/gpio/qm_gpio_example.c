#include "qm.h"
#include "qm_log.h"
#include "qm_gpio.h"
#include "qm_errno.h"

#define LOG_TAG "TEST"


static qm_gpio_dev_t gpio_out =
{
    .port = 0,
    .config = {
        .mode = QM_GPIO_MODE_OUTPUT,
        .pull_en = QM_GPIO_PULLUP_ONLY,
    }
};

static qm_gpio_dev_t gpio_in=
{
    .port = 1,
    .config = {
        .mode = QM_GPIO_MODE_INPUT,
        .pull_en = QM_GPIO_PULLUP_ONLY,
    }
};

static qm_task_t example_task = {0};
static void qm_gpio_input_test_init(uint8_t port, int32_t id);

static void gpio_interrupt_callback(void *arg)
{
    static int irq_count = 0;
    qm_err_t ret = QM_EOK;
    int32_t id = (int32_t)arg;
    if(++irq_count > 10){
        irq_count = 0;
        ret = qm_gpio_disable_irq(&gpio_in);
        if(ret != QM_EOK){
            return;
        }    
        
        ret = qm_gpio_deinit(&gpio_in);
        if(ret != QM_EOK){
            return;
        }       
    }

    if(id == 123){
        qm_gpio_set_level(&gpio_out, QM_TRUE);
    }
}

static void qm_gpio_out_test(void)
{
    qm_err_t ret = QM_EOK;

    ret = qm_gpio_init(&gpio_out);
    if(ret != QM_EOK){
       QM_LOGE(LOG_TAG,"qm_gpio_init error = %d!!!", ret);
    }

    ret = qm_gpio_set_level(&gpio_out, QM_TRUE);
    if(ret != QM_EOK){
       QM_LOGE(LOG_TAG,"qm_gpio_set_level error = %d!!!", ret);
    }

    qm_msleep(2000);

    ret = qm_gpio_set_level(&gpio_out, QM_FALSE);
    if(ret != QM_EOK){
       QM_LOGE(LOG_TAG,"qm_gpio_set_level error = %d!!!", ret);
    }

}

static void qm_gpio_input_test_init(uint8_t port, int32_t id)
{
    qm_err_t ret = QM_EOK;

    gpio_in.port = port;
    ret = qm_gpio_init(&gpio_in);
    if(ret != QM_EOK){
       QM_LOGE(LOG_TAG,"qm_gpio_init error = %d!!!", ret);
    }

    QM_LOGD(LOG_TAG,"qm_gpio_get_level %d!!!", qm_gpio_get_level(&gpio_in));

    ret = qm_gpio_enable_irq(&gpio_in,QM_GPIO_INTR_NEGEDGE,gpio_interrupt_callback,(void *)id);
    if(ret != QM_EOK){
       QM_LOGE(LOG_TAG,"qm_gpio_enable_irq error = %d!!!", ret);
    }
}


static void example_task_callback(void *arg)
{
    qm_gpio_out_test();//
    qm_gpio_input_test_init(1, 123);
    qm_task_exit(&example_task);
}

void qm_application_start(void)
{
    qm_task_new(&example_task, "example", example_task_callback, NULL, 2048, 20);
}
