#include "qm.h"

#if 1

#include "qm_rtc.h"
#include "qm_utils_timer.h"
#include "miio_user_api.h"
#include "em_rtcc.h"

#define SERVER_UTC_TIME  3
#define RTC_UPDATE_TIME_MS (30 * 60 * 1000)


typedef struct 
{
    uint8_t is_init;
    uint32_t systime;
    uint32_t rtc_cnt;
    qm_timer_t rtc_update_timer;
}qm_rtc_hal_ctx_t;

static qm_rtc_hal_ctx_t rtc_ctx = {0};

static uint32_t qm_utc_timestamp_get(void)
{
    uint64_t ticks = 0;
    uint32_t rtc_counter = RTCC_CounterGet();   //The Real Time Clock with Capture (RTCC) is a 32-bit counter providing timekeeping down to EM3. 
    if(rtc_counter >= rtc_ctx.rtc_cnt){
        ticks = rtc_counter - rtc_ctx.rtc_cnt;
    }else{
        ticks = rtc_counter + 0x100000000ull - rtc_ctx.rtc_cnt;
    }
    rtc_ctx.rtc_cnt = rtc_counter;
    rtc_ctx.systime += ticks/32768;
    return rtc_ctx.systime;
}

void qm_rtc_update_utc_timestamp(uint32_t utc)
{   
    uint32_t systime = qm_utc_timestamp_get();

    if(utc - systime > 0){
        rtc_ctx.systime = utc;
        qm_timer_change(&rtc_ctx.rtc_update_timer, RTC_UPDATE_TIME_MS);
    }
}

static void qm_rtc_update_callback(qm_timer_t *timer, void *arg)
{
    miio_mesh_request_property(SERVER_UTC_TIME);
}

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
	rtc_ctx.rtc_cnt = RTCC_CounterGet();
    qm_timer_new(&rtc_ctx.rtc_update_timer,qm_rtc_update_callback, NULL, RTC_UPDATE_TIME_MS, 1);
    qm_timer_start(&rtc_ctx.rtc_update_timer);
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
    *timestamp = qm_utc_timestamp_get();
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
