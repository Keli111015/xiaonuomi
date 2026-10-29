#include "qm.h"
#include "qm_adc.h"

#include "gpio.h"
#include "asm/adc_api.h"

#define LOG_TAG "adc"

typedef struct 
{
    uint32_t gpio_pin;
    uint32_t adc_ch;
}qm_adc_handle_t;

static qm_adc_handle_t g_adc_handle[] = {
    {IO_PORTA_01, AD_CH_PA1},
    {IO_PORTA_05, AD_CH_PA5},
    {IO_PORTA_06, AD_CH_PA6},
    {IO_PORTA_10, AD_CH_PA10},
    {IO_PORTA_12, AD_CH_PA12},
    {IO_PORTB_01, AD_CH_PB1},
    {IO_PORTB_03, AD_CH_PB3},
    {IO_PORTB_04, AD_CH_PB4},
    {IO_PORTB_08, AD_CH_PB8},
    {IO_PORTB_10, AD_CH_PB10},
    {IO_PORTC_04, AD_CH_PC4},
    {IO_PORTC_06, AD_CH_PC6},
    {IO_PORTC_05, AD_CH_PC5},
};

/**ADC最多检测端口为5个**/
int32_t qm_adc_init(qm_adc_dev_t *adc)
{
    qm_adc_handle_t *adc_handle = NULL;

    if(adc == NULL){
        return -QM_EINVAL;
    }

    for (size_t i = 0; i < QM_ARRAY_SIZE(g_adc_handle); i++)
    {
        if(g_adc_handle[i].gpio_pin == adc->pin.adc_pin){
            adc_handle = &g_adc_handle[i];
            break;
        }
    }
    
    gpio_set_pull_down(adc->pin.adc_pin, 0);
    gpio_set_pull_up(adc->pin.adc_pin, 0);
    gpio_direction_input(adc->pin.adc_pin);
    gpio_set_die(adc->pin.adc_pin, 0);
    adc_add_sample_ch(adc_handle->adc_ch);

    adc->priv = adc_handle;

    return QM_EOK;
}


int32_t qm_adc_value_get(qm_adc_dev_t *adc, void *output, uint32_t timeout)
{
    int32_t *output_value = (int32_t *)output;
    qm_adc_handle_t *adc_handle = NULL;

    if(adc == NULL || adc->priv == NULL){
        return -QM_EINVAL;
    }

    adc_handle = (qm_adc_handle_t *)adc->priv;

    *output_value = adc_get_voltage(adc_handle->adc_ch);
    return QM_EOK;
}

int32_t qm_adc_deinit(qm_adc_dev_t *adc)
{


    return QM_EOK;
}

#if 0 //暂不支持
int32_t  qm_adc_start(qm_adc_dev_t *adc, void *data, uint32_t size)
{
    return -QM_EINVAL;
}

int32_t qm_adc_stop(qm_adc_dev_t *adc)
{
    return -QM_EINVAL;
}
#endif