#include "qm.h"

#ifdef CONFIG_QM_RTC_SUPPORT

#include "qm_rtc.h"

#include "arch_time.h"

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    return QM_EOK;
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *timer)
{
    return QM_EOK;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *time)
{
    return QM_EOK;  
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    if(timestamp == NULL){
        return -QM_EINVAL;
    }

    *timestamp = (uint32_t)arch_os_utc_now();
    
    return QM_EOK;
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{

    return QM_EOK;
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{  
    return QM_EOK; 
}

#endif