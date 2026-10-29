#include "qm_lpc_hal.h"
#include "qm_errno.h"
#include "esp_pm.h"
#include "esp_sleep.h"

int32_t qm_hal_lpc_init(void)
{

    return QM_EOK;
}

int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode)
{
#if CONFIG_IDF_TARGET_ESP32S3
    esp_pm_config_esp32s3_t pm_config = 
    {
        .max_freq_mhz =  40,
        .min_freq_mhz =  10,
    };
#endif

    if(mode == QM_LPC_LIGHT_SLEEP){
        pm_config.light_sleep_enable = 1;
        esp_pm_configure(&pm_config);
    }else if(mode == QM_LPC_NO_SLEEP){
        pm_config.light_sleep_enable = 0;
        esp_pm_configure(&pm_config);
    }else{
        return -QM_EINVAL;
    }
    return QM_EOK;
}