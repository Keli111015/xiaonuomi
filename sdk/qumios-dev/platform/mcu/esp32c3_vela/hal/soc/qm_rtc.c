#include "qm.h"
#include "qm_rtc.h"

#if CONFIG_QM_RTC_SUPPORT

#include "sys/time.h" 
#include "virserial.h"

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    return virserial_timestamp_timer_start();
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
    *timestamp = virserial_fast_get_timestamp();
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