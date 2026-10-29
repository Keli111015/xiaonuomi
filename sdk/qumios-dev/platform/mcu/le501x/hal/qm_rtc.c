#include "qm.h"
#include "qm_rtc.h"
#include "qm_time.h"

#include "ls_hal_rtc.h"
#define LOG_TAG "TEST"

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    if(rtc == NULL)
    {
        return -QM_EINVAL;
    }
    HAL_RTC_Init(RTC_CKSEL_LSI);
    return QM_EOK;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *time)
{
    calendar_time_t calendar_time;
    calendar_cal_t calendar_cal;

    if (time == NULL)
    {
        return -QM_EINVAL;
    }
    calendar_cal.year = time->year - 2000;
    calendar_cal.mon = time->month;
    calendar_cal.date = time->date;
    calendar_time.hour = time->hr;
    calendar_time.min = time->min;
    calendar_time.sec = time->sec;
    calendar_time.week = time->weekday;
    RTC_CalendarSet(&calendar_cal, &calendar_time);
    return QM_EOK;
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *time)
{
    int ret = 0;
    calendar_time_t calendar_time;
    calendar_cal_t calendar_cal;
    if (rtc == NULL || time == NULL)
    {
        return -QM_EINVAL;
    }
    ret = RTC_CalendarGet(&calendar_cal, &calendar_time);
    if (ret != HAL_OK)
    {
        return -QM_EINVAL;
    }
    time->year = calendar_cal.year + 2000;
    time->month = calendar_cal.mon;
    time->date = calendar_cal.date;
    time->weekday = calendar_time.week;
    time->hr = calendar_time.hour;
    time->min = calendar_time.min;
    time->sec = calendar_time.sec;
    return QM_EOK;
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    struct qm_tm timeinfo = {0};
    qm_rtc_time_t time = {0};
    uint64_t time_loc = 0;

    qm_rtc_get_time(rtc, &time);
    // QM_LOGD(LOG_TAG, "RTC: year : %d. mon: %d , day:%d", time.year, time.month, time.date);
    timeinfo.tm_year = time.year - 1900;
    timeinfo.tm_mon = time.month - 1;
    timeinfo.tm_mday = time.date;
    timeinfo.tm_hour = time.hr;
    timeinfo.tm_min = time.min;
    timeinfo.tm_sec = time.sec;

    time_loc = (uint32_t)qm_mktime(&timeinfo);
    *timestamp = time_loc;

    return QM_EOK;
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{
    struct qm_tm *timeinfo = NULL;
    qm_rtc_time_t time = {0};
    uint64_t time_stamp = timestamp;
    timeinfo = qm_gmtime(&time_stamp);

    time.year = timeinfo->tm_year + 1900;
    time.month = timeinfo->tm_mon + 1;
    time.date = timeinfo->tm_mday;
    time.hr = timeinfo->tm_hour;
    time.min = timeinfo->tm_min;
    time.sec = timeinfo->tm_sec;
    // QM_LOGD(LOG_TAG, "RTC: timestamp : %d, year: %d. mon: %d , day:%d", timestamp, time.year, time.month, time.date);

    qm_rtc_set_time(rtc, &time);
    return QM_EOK;
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{
    HAL_RTC_DeInit();
    return QM_EOK;
}