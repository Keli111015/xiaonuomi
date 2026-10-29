#include "qm_rtc.h"
#include "qm_errno.h"
#include "hal/soc/rtc.h"
#include "time.h"

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    qm_err_t ret = QM_EOK;
    int res = 0;
    rtc_dev_t *dev_rtc = NULL;

    if(rtc == NULL){
        return -QM_EINVAL;
    }

    dev_rtc = (rtc_dev_t*)qm_malloc(sizeof(rtc_dev_t));
    if(dev_rtc == NULL){
        return -QM_ENOMEM;
    }
    memset(dev_rtc, 0, sizeof(rtc_dev_t));

    dev_rtc->port = rtc->port;
    /* set to DEC format */
    if(rtc->config.format == QM_RTC_FORMAT_DEC){
        dev_rtc->config.format = HAL_RTC_FORMAT_DEC;
    }else if(rtc->config.format == QM_RTC_FORMAT_BCD){
        dev_rtc->config.format = HAL_RTC_FORMAT_BCD;
    }else{
        ret = -QM_EINVAL;
        goto __exit;
    }
    
    res = hal_rtc_init(&dev_rtc);  
    if(res != 0){
        ret = -QM_ERROR;
        goto __exit;
    }

    rtc->priv = (void*)dev_rtc;

    return QM_EOK;

__exit:
    qm_free(dev_rtc);
    dev_rtc = NULL;
    return ret;
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *time)
{
    int ret = 0;
    rtc_dev_t *dev_rtc = NULL;
    rtc_time_t  time_buf = {0};
    if(rtc == NULL || time == NULL){
        return -QM_EINVAL;
    }

    dev_rtc = (rtc_dev_t*)rtc->priv;

    ret = hal_rtc_get_time(rtc, &time_buf);
    if(ret != 0){
        return -QM_ERROR;
    }

    time->sec     = time_buf.sec;
    time->min     = time_buf.min;
    time->hr      = time_buf.hr;
    time->date    = time_buf.date;
    time->month   = time_buf.month;
    //年份上限，目前驱动支持到2037年
    time->year    = time_buf.year;

    return QM_EOK;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *time)
{
    int ret = 0;
    rtc_dev_t *dev_rtc = NULL;
    rtc_time_t  time_buf = {0};
    if(rtc == NULL || time == NULL){
        return -QM_EINVAL;
    }

    dev_rtc = (rtc_dev_t*)rtc->priv;
    
    time_buf.sec     = time->sec;
    time_buf.min     = time->min;
    time_buf.hr      = time->hr;
    time_buf.date    = time->date;
    time_buf.month   = time->month;
    //年份上限，目前驱动支持到2037年
    time_buf.year    = time->year;
    ret = hal_rtc_set_time(rtc, &time_buf);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    qm_err_t ret = QM_EOK;
    struct tm tm = {0};
    time_t time = 0;
    qm_rtc_time_t rtc_time = {0};
    if(rtc == NULL || timestamp == NULL){
        return -QM_EINVAL;
    }
    
    ret = qm_rtc_get_time(rtc, &rtc_time);
    if(ret != QM_EOK){
        return ret;
    }

    tm.tm_year = rtc_time.year+100;
    tm.tm_mon = rtc_time.month-1;
    tm.tm_mday = rtc_time.date;
    tm.tm_hour = rtc_time.hr;
    tm.tm_min = rtc_time.min ;
    tm.tm_sec = rtc_time.sec;

    time = mktime(&tm);
    *timestamp = (uint32_t)time;
    
    return QM_EOK;
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{
    struct tm *tm;
    time_t time = 0;
    qm_err_t ret = QM_EOK;
    qm_rtc_time_t rtc_time = {0};

    if(rtc == NULL || timestamp == 0){
        return -QM_EINVAL;
    }

    time = (time_t)timestamp;
    tm = localtime(&time);

    rtc_time.sec     = (uint8_t)tm->tm_sec;
    rtc_time.min     = (uint8_t)tm->tm_min;
    rtc_time.hr      = (uint8_t)tm->tm_hour;
    rtc_time.date    = (uint8_t)tm->tm_mday;
    rtc_time.month   = (uint8_t)tm->tm_mon+1;
    rtc_time.year   = (uint16_t)tm->tm_year-100;

    if(tm->tm_wday == 0){
        rtc_time.weekday = 7;
    }else{
        rtc_time.weekday = (uint8_t)tm->tm_wday;
    }

    return qm_rtc_set_time(rtc, &rtc_time);
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{  
    int ret = 0;
    rtc_dev_t *dev_rtc = NULL;
    if(rtc == NULL){
        return -QM_EINVAL;
    }

    dev_rtc = (rtc_dev_t*)rtc->priv;

    ret = hal_rtc_finalize(dev_rtc);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK; 
}