#include "qm.h"
#include "qm_log.h"
#include "qm_pwm.h"

#define LOG_TAG "TEST"

static qm_pwm_dev_t pwm_config = {
    .config = {
        .freq = 1000,
        .pulse= 10,
    },
    .pin.pwm_pin = 2,
    .port        = 0,
};

static qm_pwm_dev_t pwm_config2 = {
    .config = {
        .freq = 1000,
        .pulse= 100,
    },
    .pin.pwm_pin = 4,
    .port        = 1,
};

static qm_task_t example_task = {0};


static void example_task_callback(void *arg)
{
    qm_pwm_config_t para = {.freq = 1000, .pulse = 100};
    qm_pwm_config_t para2 = {.freq = 1000, .pulse = 50};

    QM_LOGD(LOG_TAG,"qm_pwm_init!!!");
    qm_pwm_init(&pwm_config);
    qm_pwm_init(&pwm_config2);

    qm_msleep(500);
    QM_LOGD(LOG_TAG,"qm_pwm_start!!!");
    qm_pwm_start(&pwm_config);
    qm_pwm_start(&pwm_config2);

    qm_msleep(500);
    QM_LOGD(LOG_TAG,"qm_pwm_para_change!!!");
    qm_pwm_para_change(&pwm_config, para);
    qm_pwm_para_change(&pwm_config2, para2);

    qm_msleep(500);
    QM_LOGD(LOG_TAG,"qm_pwm_stop!!!");
    qm_pwm_stop(&pwm_config);
    qm_pwm_stop(&pwm_config2);

    qm_msleep(500);
    QM_LOGD(LOG_TAG,"qm_pwm_start!!!");
    qm_pwm_start(&pwm_config);
    qm_pwm_start(&pwm_config2);

    qm_msleep(500);
    QM_LOGD(LOG_TAG,"qm_pwm_deinit!!!");
    qm_pwm_deinit(&pwm_config);
    qm_pwm_deinit(&pwm_config2);

    qm_task_exit(&example_task);
}

void qm_application_start(void)
{
    qm_task_new(&example_task, "example", example_task_callback, NULL, 2048, 20);
}
