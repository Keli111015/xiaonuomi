#include "bsp_vibrator.h"

#include "qm_gpio.h"
#include "qm_log.h"

#define LOG_TAG "bsp_vib"

static qm_gpio_dev_t s_gpio = {
    .port = BSP_VIBRATOR_GPIO,
    .config = {
        .mode = QM_GPIO_MODE_OUTPUT,
        .pull_en = QM_GPIO_PULLDOWN_ONLY,
    },
};

static uint8_t s_inited;

qm_err_t bsp_vibrator_init(void)
{
    if (s_inited) {
        return QM_EOK;
    }

    if (qm_gpio_init(&s_gpio) != QM_EOK) {
        QM_LOGE(LOG_TAG, "gpio%d init fail", BSP_VIBRATOR_GPIO);
        return -QM_EIO;
    }

    (void)qm_gpio_set_level(&s_gpio, 0);
    s_inited = 1;
    QM_LOGI(LOG_TAG, "init ok gpio%d", BSP_VIBRATOR_GPIO);
    return QM_EOK;
}

void bsp_vibrator_on(void)
{
    if (!s_inited) {
        QM_LOGW(LOG_TAG, "on ignored, not inited");
        return;
    }
    (void)qm_gpio_set_level(&s_gpio, 1);
    QM_LOGI(LOG_TAG, "on");
}

void bsp_vibrator_off(void)
{
    if (!s_inited) {
        QM_LOGW(LOG_TAG, "off ignored, not inited");
        return;
    }
    (void)qm_gpio_set_level(&s_gpio, 0);
    QM_LOGI(LOG_TAG, "off");
}

void bsp_vibrator_pulse(uint32_t ms)
{
    if (!s_inited) {
        QM_LOGW(LOG_TAG, "pulse ignored, not inited");
        return;
    }
    if (ms == 0) {
        return;
    }

    QM_LOGI(LOG_TAG, "pulse %u ms", (unsigned)ms);
    (void)qm_gpio_set_level(&s_gpio, 1);
    qm_msleep(ms);
    (void)qm_gpio_set_level(&s_gpio, 0);
}
