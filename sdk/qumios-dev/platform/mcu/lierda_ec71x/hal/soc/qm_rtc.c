#include "qm.h"
#include "qm_time.h"
#include "qm_rtc.h"
#include "liot_rtc.h"

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    return QM_EOK;
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *time)
{
    liot_rtc_time_s tm;
    liot_errcode_rtc_e ret;
    if(rtc == NULL || time == NULL){
        return -QM_EINVAL;
    }
    ret = liot_rtc_get_time(&tm);
    if(ret != LIOT_RTC_SUCCESS){
        return -QM_ERROR;
    }

    time->year = tm.tm_year + 2000;
    time->month = tm.tm_mon;
    time->date = tm.tm_mday;
    time->hr = tm.tm_hour;
    time->min = tm.tm_min;
    time->sec = tm.tm_sec;

    if(tm.tm_wday == 0){
        time->weekday = 7;
    }else{
        time->weekday = (uint8_t)tm.tm_wday;
    } 
    return QM_EOK;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *time)
{
    liot_rtc_time_s tm;
    liot_errcode_rtc_e ret;
    if(rtc == NULL || time == NULL){
        return -QM_EINVAL;
    }

    tm.tm_year = time->year - 2000; 
    tm.tm_mon = time->month; 
    tm.tm_mday = time->date;  
    tm.tm_hour = time->hr;
    tm.tm_min = time->min;  
    tm.tm_sec = time->sec;
    ret = liot_rtc_set_time(&tm);
    if(ret != LIOT_RTC_SUCCESS){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    if(rtc == NULL || timestamp == NULL){
        return -QM_EINVAL;
    }
    *timestamp = liot_rtc_get_time_s();
    return QM_EOK;
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{    
    struct qm_tm tm;
    qm_time_t time;
    liot_rtc_time_s liot_tm;
    liot_errcode_rtc_e ret;

    if(rtc == NULL){
        return -QM_EINVAL;
    }

    time = (qm_time_t)timestamp;
    qm_gmtime_r(&time, &tm);
    liot_tm.tm_year =  tm.tm_year + 1900 - 2000;
    liot_tm.tm_mon = tm.tm_mon + 1;
    liot_tm.tm_mday = tm.tm_mday;
    liot_tm.tm_hour = tm.tm_hour;
    liot_tm.tm_min = tm.tm_min;
    liot_tm.tm_sec = tm.tm_sec;

    ret = liot_rtc_set_time(&liot_tm);
    if(ret != LIOT_RTC_SUCCESS){
        return -QM_ERROR;
    }

    return QM_EOK;
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{  
    return QM_EOK; 
}