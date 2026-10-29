#include "qm.h"

#include "qm_rtc.h"
#include "qm_utils_timer.h"
#include "miio_user_api.h"
#include "rtl876x_rtc.h"

#define SERVER_UTC_TIME  3
#define RTC_UPDATE_TIME_MS (30 * 60 * 1000)

#define TS_START (1736229663) //2025-01-07 14:01:03(+8)

typedef struct 
{
    // uint8_t is_init;
    uint32_t systime;
    uint32_t time_diff;
    qm_timer_t rtc_update_timer;
}qm_rtc_hal_ctx_t;

static qm_rtc_hal_ctx_t rtc_ctx = {0};

static uint32_t qm_utc_timestamp_get(void)
{
    uint32_t rtc_counter = rtc_ctx.systime;
    return rtc_counter + rtc_ctx.time_diff;
}

void qm_rtc_update_utc_timestamp(uint32_t utc)
{   
    uint32_t systime = qm_utc_timestamp_get();
    if (utc < TS_START)
    {
        return;
    }
    if(utc > systime)
    {
        rtc_ctx.time_diff += utc - systime;
    }
    else
    {
        rtc_ctx.time_diff -= systime - utc;
    }
    qm_timer_change(&rtc_ctx.rtc_update_timer, RTC_UPDATE_TIME_MS);
}

static void qm_rtc_update_callback(qm_timer_t *timer, void *arg)
{
    miio_mesh_request_property(SERVER_UTC_TIME);
}


#define RTC_PRESCALER_VALUE     (3200-1)//f = 10Hz
/* RTC has 4 comparators,comparator0~3 . */
#define RTC_COMP_INDEX          RTC_COMP1
#define RTC_COMP_INDEX_INT      RTC_INT_COMP1
#define RTC_COMP_VALUE          (10)
#define RTC_HANDLER             rtc_handler

static int RTC_HANDLER(int irq, void *context, void *arg)
{
    if (RTC_GetINTStatus(RTC_COMP_INDEX_INT) == SET)
    {
        rtc_ctx.systime++;
        // syslog(0,"++%d\n", rtc_ctx.systime);
        RTC_SetCompValue(RTC_COMP_INDEX, RTC_GetCounter() + RTC_COMP_VALUE);
        RTC_ClearCompINT(RTC_COMP_INDEX);
        RTC_ClearINTPendingBit(RTC_COMP_INDEX);
    }
    return 0;
}

static void driver_rtc_init(void)
{
    RTC_DeInit();

    RTC_SetPrescaler(RTC_PRESCALER_VALUE);
    RTC_SetCompValue(RTC_COMP_INDEX, RTC_COMP_VALUE);

    RTC_INTConfig(RTC_COMP_INDEX_INT, ENABLE);

    up_enable_irq(RTC_VECTORn);
    irq_attach(RTC_VECTORn, RTC_HANDLER, NULL);

    RTC_NvCmd(ENABLE);
    /* Start RTC */
    RTC_ResetCounter();
    RTC_Cmd(ENABLE);
}
int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    driver_rtc_init();
	// rtc_ctx.rtc_cnt = RTC_GetCounter();
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
    if (timestamp == NULL) {
        return QM_EINVAL;
    }
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
