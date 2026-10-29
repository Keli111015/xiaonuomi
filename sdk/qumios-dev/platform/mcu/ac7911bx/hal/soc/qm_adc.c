#include "generic/typedef.h"

#include "qm.h"
#include "qm_adc.h"

#include "asm/adc_api.h"
#include "gpio.h"

typedef struct {
    uint32_t gpio_pin;
    uint32_t adc_ch;
} qm_adc_handle_t;

static qm_adc_handle_t g_adc_handle[] = {
    {IO_PORTA_10, AD_CH_PA10},
};

int32_t qm_adc_init(qm_adc_dev_t *adc)
{
    qm_adc_handle_t *adc_handle = NULL;

    if (adc == NULL) {
        return -QM_EINVAL;
    }

    for (size_t i = 0; i < QM_ARRAY_SIZE(g_adc_handle); i++) {
        if (g_adc_handle[i].gpio_pin == adc->pin.adc_pin) {
            adc_handle = &g_adc_handle[i];
            break;
        }
    }

    if (adc_handle == NULL) {
        return -QM_EINVAL;
    }

    gpio_latch_en(adc->pin.adc_pin, 0);
    gpio_set_pull_down(adc->pin.adc_pin, 0);
    gpio_set_pull_up(adc->pin.adc_pin, 0);
    gpio_set_direction(adc->pin.adc_pin, 1);
    gpio_set_die(adc->pin.adc_pin, 0);
    gpio_set_dieh(adc->pin.adc_pin, 0);
    adc_add_sample_ch(adc_handle->adc_ch);

    adc->priv = adc_handle;
    return QM_EOK;
}

int32_t qm_adc_value_get(qm_adc_dev_t *adc, void *output, uint32_t timeout)
{
    int32_t *output_value = (int32_t *)output;
    qm_adc_handle_t *adc_handle = NULL;

    (void)timeout;

    if (adc == NULL || adc->priv == NULL || output == NULL) {
        return -QM_EINVAL;
    }

    adc_handle = (qm_adc_handle_t *)adc->priv;
    *output_value = (int32_t)adc_get_voltage(adc_handle->adc_ch);
    return QM_EOK;
}

int32_t qm_adc_start(qm_adc_dev_t *adc, void *data, uint32_t size)
{
    (void)adc;
    (void)data;
    (void)size;
    return -QM_ENOSYS;
}

int32_t qm_adc_stop(qm_adc_dev_t *adc)
{
    (void)adc;
    return -QM_ENOSYS;
}

int32_t qm_adc_deinit(qm_adc_dev_t *adc)
{
    if (adc != NULL) {
        adc->priv = NULL;
    }
    return QM_EOK;
}
