#include "qm.h"
#include "qm_rtc.h"

#include "sys/time.h" 

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    return QM_EOK;
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *timer)
{
    time_t timestamp = 0;
    struct tm *ptime = NULL;

    if(timer == NULL){
        return -QM_EINVAL;
    }

    time(&timestamp);
    ptime = localtime(&timestamp);
    if(ptime == NULL){
        return 0;
    }

    timer->sec     = (uint8_t)ptime->tm_sec;
    timer->min     = (uint8_t)ptime->tm_min;
    timer->hr      = (uint8_t)ptime->tm_hour;
    timer->date    = (uint8_t)ptime->tm_mday;
    timer->month   = (uint8_t)ptime->tm_mon + 1;
    //年份上限，目前驱动支持到2037年
    timer->year    = (uint16_t)ptime->tm_year + 1900;
    timer->weekday = (uint16_t)ptime->tm_wday;
    if(ptime->tm_wday == 0){
        timer->weekday = 7;
    }else{
        timer->weekday = (uint8_t)ptime->tm_wday;
    }
    return QM_EOK;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *time)
{
    int ret = 0;
    struct timeval tv;  
    struct tm tm = {0};  
    time_t set_timer = 0;

    if(time == NULL){
        return -QM_EINVAL;
    }

    // 设置时间为2023年6月23日，15:30:00（UTC）  
    // tv_sec表示秒数，以秒为单位，因此需要将时间转换为秒数  
    // 这里使用了mktime函数来将时间转换为秒数  
    tm.tm_year = time->year - 1900; // 年份是从1900年开始计算的，所以要减去1900  
    tm.tm_mon = time->month - 1; // 月份是从0开始计算的，所以要减去1  
    tm.tm_mday = time->date; // 日期  
    tm.tm_hour = time->hr; // 小时  
    tm.tm_min = time->min; // 分钟  
    tm.tm_sec = time->sec; // 秒数  
    set_timer = mktime(&tm); // 将时间转换为秒数  
  
    // 设置时间结构体  
    tv.tv_sec = set_timer; // 秒数  
    tv.tv_usec = 0; // 微秒数（可选）  
  
    // 调用settimeofday函数设置系统时间  
    ret = settimeofday(&tv, NULL);  
    if (ret != QM_EOK) {  
        return  -QM_ERROR;  
    }  
  
    return QM_EOK;  
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    struct timeval tv_now;

    if(timestamp == NULL){
        return -QM_EINVAL;
    }
    
    gettimeofday(&tv_now, NULL);
    *timestamp = (uint32_t)tv_now.tv_sec;
    
    return QM_EOK;
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{
    struct timeval tv;  
    qm_err_t ret = QM_EOK;

    if(timestamp == 0){
        return -QM_EINVAL;
    }

    // 设置时间结构体  
    tv.tv_sec = timestamp; // 秒数  
    tv.tv_usec = 0; // 微秒数（可选）  
  
    // 调用settimeofday函数设置系统时间  
    ret = settimeofday(&tv, NULL);  
    if (ret != QM_EOK) {  
        return  -QM_ERROR;  
    }  
    
    return QM_EOK;
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{  
    return QM_EOK; 
}