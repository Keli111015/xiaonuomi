#include "OSAL.h"
#include "qm_adc.h"
#include "qm.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "log.h"

#include "gpio.h"
#include "qm_adc.h"
#include "adc.h"

// 0 atten 直接测量 3.2  1 bypass 外部电压
#ifndef PHY6222_ADC_MODE 
#define PHY6222_ADC_MODE 0
#endif

#define LOG_TAG "adc"

#define MAX_SAMPLE_POINT    32
uint16_t adc_debug[6][MAX_SAMPLE_POINT];
static uint8_t channel_done_flag = 0;

// ADC_CH0 = 2,ADC_CH1N_P11 = 2,
// ADC_CH1 = 3,ADC_CH1P_P23 = 3,ADC_CH1DIFF = 3,/*P23 and P11*/
// ADC_CH2 = 4,ADC_CH2N_P24 = 4,
// ADC_CH3 = 5,ADC_CH2P_P14 = 5,ADC_CH2DIFF = 5,/*P14 and P24*/
// ADC_CH4 = 6,ADC_CH3N_P15 = 6,
// ADC_CH9 = 7,ADC_CH3P_P20 = 7,ADC_CH3DIFF = 7,/*P20 and P15*/

adc_Cfg_t adc_cfg =
{
    // .channel = ADC_BIT(ADC_CHANEL_SELE),
    .is_continue_mode = FALSE,
    .is_differential_mode = 0x00,
    .is_high_resolution = PHY6222_ADC_MODE, 
};

volatile int32_t new_value = 0;

static void p_adc_evt(adc_Evt_t *pev)
{
	// LOG("p_adc_evt: type:%d, ch:%d, size:%d\n", pev->type, pev->ch, pev->size);
	double value = 0;
	int i = 0;
	bool is_high_resolution = FALSE;
	bool is_differential_mode = FALSE;
	uint8_t ch = 0;

	if ((pev->type != HAL_ADC_EVT_DATA) || (pev->ch < 2))
		return;

	osal_memcpy(adc_debug[pev->ch - 2], pev->data, 2 * (pev->size));
	channel_done_flag |= BIT(pev->ch);

	if (channel_done_flag == adc_cfg.channel)
	{
		for (i = 2; i < 8; i++)
		{
			if (channel_done_flag & BIT(i))
			{
				is_high_resolution = (adc_cfg.is_high_resolution & BIT(i)) ? TRUE : FALSE;
				is_differential_mode = (adc_cfg.is_differential_mode & BIT(i)) ? TRUE : FALSE;
				value = hal_adc_value_cal((adc_CH_t)i, adc_debug[i - 2], pev->size, is_high_resolution, is_differential_mode);

				switch (i)
				{
				case ADC_CH1N_P11:
					ch = 11;
					break;

				case ADC_CH1P_P23:
					ch = 23;
					break;

				case ADC_CH2N_P24:
					ch = 24;
					break;

				case ADC_CH2P_P14:
					ch = 14;
					break;

				case ADC_CH3N_P15:
					ch = 15;
					break;

				case ADC_CH3P_P20:
					ch = 20;
					break;

				default:
					break;
				}

				if (ch != 0)
				{
					new_value = (int)(value * 1000);
					// LOG("P-%d  %d mv \n", ch, new_value);
				}
				else
				{
					LOG("invalid channel\n");
				}
			}
		}

		// LOG(" mode:%d \n", adc_cfg.is_continue_mode);
		channel_done_flag = 0;
	}
}

int32_t qm_adc_init(qm_adc_dev_t *adc)
{
	if (adc->port < 2 || adc->port > 7) // ADC_CH1N_P11 to ADC_CH3P_P20
	{
		return QM_EINVAL;
	}
    int ret;
	hal_adc_init();
	adc_cfg.channel = ADC_BIT(adc->port); // adc->pin.adc.pin;
	ret = hal_adc_config_channel(adc_cfg, p_adc_evt);
	if(ret)
    {
        LOG("hal_adc_config_channel ret = %d\n",ret);
        return -1;
    }
	hal_adc_clock_config(HAL_ADC_CLOCK_320K);
	// QM_LOGD(LOG_TAG, " qm_adc_init");
	// hal_adc_start(POLLING_MODE);
	return QM_EOK;
}

int32_t  qm_adc_start(qm_adc_dev_t *adc, void *data, uint32_t size)
{
	if (adc->port < 2 || adc->port > 7) // ADC_CH1N_P11 to ADC_CH3P_P20
	{
		return QM_EINVAL;
	}
	hal_adc_start(POLLING_MODE);
	return QM_EOK;
}

int32_t qm_adc_value_get(qm_adc_dev_t *adc, void *output, uint32_t timeout)
{
	hal_adc_value_read(adc->port);
	*(int32_t *)output = new_value;
    return QM_EOK;
}

int32_t qm_adc_stop(qm_adc_dev_t *adc)
{
	hal_poilling_adc_stop();
	return QM_EOK;
}

int32_t qm_adc_deinit(qm_adc_dev_t *adc)
{
	hal_adc_deinit();
	return QM_EOK;
}
