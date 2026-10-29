#include "qm.h"
#include "qm_log.h"
#include "qm_adc.h"

#define LOG_TAG "TEST"

static qm_adc_dev_t adc_config = {
    .priv = NULL,
    .port        = 0,
    .pin.adc_pin = 2,
};

static qm_adc_dev_t adc_config2 = {
    .priv = NULL,
    .port        = 1,
    .pin.adc_pin = 2,
};

static qm_task_t adc_task = {0};

static void example_task_callback(void *arg)
{
    uint8_t count = 10;
    QM_LOGD(LOG_TAG,"qm_adc_init!!!");
    qm_adc_init(&adc_config);
    qm_adc_init(&adc_config2);

    while (count--)
    {
        int32_t value = 0;
        qm_adc_value_get(&adc_config, &value, 0);
        QM_LOGD(LOG_TAG,"ADC0 GET VALUE %d !!!", value);

        qm_adc_value_get(&adc_config2, &value, 0);
        QM_LOGD(LOG_TAG,"ADC1 GET VALUE %d !!!", value);

        qm_msleep(100);
    }
    qm_msleep(1000);
    QM_LOGD(LOG_TAG,"qm_adc_deinit!!!");
    qm_adc_deinit(&adc_config);
    qm_adc_deinit(&adc_config2);

    qm_task_exit(&example_task);
}

void qm_application_start(void)
{
    qm_task_new(&adc_task, "example", example_task_callback, NULL, 2048, 20);
}