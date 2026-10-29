#include "qm.h"
#include "qm_adc.h"

#include "cpu.h"
#include "platform.h"
#include "ls_soc_gpio.h"
#include "ls_hal_adc.h"
#include "ls_hal_dmac.h"
#include "qm_utils_timer.h"

#define LOG_TAG "adc"

#define ADC_CHANNLE_MAX 4

static int is_init = 1;
static uint8_t use_port_num = 0;
static ADC_HandleTypeDef ADCx_Hdl = {0};

static volatile uint8_t is_recved = false;

typedef struct
{
    uint16_t adc_value;
    uint8_t rank;
    uint8_t is_inited;
} adc_handle_t;


static adc_handle_t adc_handles[ADC_CHANNLE_MAX] = {0};

void HAL_ADCEx_InjectedConvCpltCallback(ADC_HandleTypeDef* hadc)  
{
    
    for (int i = 0;i < ADC_CHANNLE_MAX;i++)
    {
        if (adc_handles[i].is_inited)
        {
            adc_handles[i].adc_value = HAL_ADCEx_InjectedGetValue(hadc, adc_handles[i].rank);
        }
    }
    is_recved = true;
}


/**ADC最多检测端口为4个**/
int32_t qm_adc_init(qm_adc_dev_t *adc)
{
    uint32_t channel = 0;
    ADC_InjectionConfTypeDef sConfigInjected = {0};
    QM_RETURN_ON_FALSE(adc, -QM_EINVAL, LOG_TAG, "adc invail");

    if (adc->port >= ADC_CHANNLE_MAX)
    {
        return QM_EINVAL;
    }

    if(is_init){
        is_init = 0;
        /** Common config  */
        ADCx_Hdl.Instance = LSADC;
        ADCx_Hdl.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
        ADCx_Hdl.Init.ScanConvMode          = ADC_SCAN_ENABLE;              /* Sequencer disabled (ADC conversion on only 1 channel: channel set on rank 1) */
        ADCx_Hdl.Init.NbrOfConversion       = ADC_CHANNLE_MAX;                            /* Parameter discarded because sequencer is disabled */
        ADCx_Hdl.Init.DiscontinuousConvMode = DISABLE;                       /* Parameter discarded because sequencer is disabled */
        ADCx_Hdl.Init.NbrOfDiscConversion   = 1;                             /* Parameter discarded because sequencer is disabled */
        ADCx_Hdl.Init.ContinuousConvMode    = DISABLE;                        /* Continuous mode to have maximum conversion speed (no delay between conversions) */
        ADCx_Hdl.Init.TrigType      = ADC_INJECTED_SOFTWARE_TRIGT;            /* Trig of conversion start done by which event */
        ADCx_Hdl.Init.Vref          = ADC_VREF_INSIDE; 
        ADCx_Hdl.Init.AdcCkDiv = ADC_CLOCK_DIV32;
        ADCx_Hdl.Init.AdcDriveType = EINBUF_DRIVE_ADC;
        HAL_ADC_Init(&ADCx_Hdl);
    }

    switch (adc->pin.adc_pin)
    {
        case PB12:
            channel = 0;
            pinmux_adc12b_in0_init();
        break;
        case PB13:
            channel = 1;
            pinmux_adc12b_in1_init();
        break;
        case PC00:
            channel = 2;
            pinmux_adc12b_in2_init();
        break;
        case PC01:
            channel = 3;
            pinmux_adc12b_in3_init();
        break;
        case PA00:
            channel = 4;
            pinmux_adc12b_in4_init();
            break;
        case PA01:
            channel = 5;
            pinmux_adc12b_in5_init();
            break;
        case PA02:
            channel = 6;
            pinmux_adc12b_in6_init();
            break;
        default:
        break;
    }


    sConfigInjected.InjectedChannel = channel;       //ADC通道  
    sConfigInjected.InjectedRank = adc->port + 1;      //转化序列         
    sConfigInjected.InjectedSamplingTime = ADC_SAMPLETIME_15CYCLES;   //采样周期  
    sConfigInjected.InjectedOffset = 0;                   //转化数据偏移量  
    sConfigInjected.InjectedNbrOfConversion = ADC_CHANNLE_MAX;          //注入转换的次数  
    sConfigInjected.InjectedDiscontinuousConvMode = DISABLE;   //注入序列间断转换模式  
    sConfigInjected.AutoInjectedConv = DISABLE;                 //自动注入转换模式              
    HAL_ADCEx_InjectedConfigChannel(&ADCx_Hdl, &sConfigInjected);    

    adc_handles[adc->port].rank = adc->port + 1;
    adc_handles[adc->port].is_inited = QM_TRUE;        

    use_port_num++;
    adc->priv = (void *)1;
    return QM_EOK;
}

int32_t qm_adc_value_get(qm_adc_dev_t *adc, void *output, uint32_t timeout)
{
    int is_use = (int)adc->priv;

    QM_RETURN_ON_FALSE(adc, -QM_EINVAL, LOG_TAG, "adc invail");
    QM_RETURN_ON_FALSE(output, -QM_EINVAL, LOG_TAG, "output invail");
    QM_RETURN_ON_FALSE(adc->priv, -QM_EINVAL, LOG_TAG, "adc->priv invail");

    if(!is_use){
        return -QM_EINVAL;
    }

    qm_utils_time_t timer = {0};
    uint32_t t_left = 0;

    qm_utils_time_countdown_ms(&timer, timeout);  

    is_recved = false;
    HAL_ADCEx_InjectedStart_IT(&ADCx_Hdl);
    
    while (!is_recved)
    {
        t_left = qm_utils_time_left(&timer);
        if(t_left == 0){
			return -QM_ETIMEOUT;
        }
    }
	is_recved = false;

    *((uint32_t *)output) = adc_handles[adc->port].adc_value;

    HAL_ADCEx_InjectedStop_IT(&ADCx_Hdl);
    return QM_EOK;
}

int32_t qm_adc_deinit(qm_adc_dev_t *adc)
{
    int is_use = (int)adc->priv;

    QM_RETURN_ON_FALSE(adc, -QM_EINVAL, LOG_TAG, "adc invail");
    QM_RETURN_ON_FALSE(adc->priv, -QM_EINVAL, LOG_TAG, "adc->priv invail");

    if(!is_use){
        return -QM_EINVAL;
    }

    adc->priv = (void *)0;
    if(--use_port_num <= 0){
        is_init = 1;
        HAL_ADC_DeInit(&ADCx_Hdl);
    }
    is_recved = false;

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