#include "qm.h"
#include "qm_rtc.h"
#include "qm_errno.h"
#include "qm_log.h"

#include <errno.h>
#include <sys/time.h>
#include <time.h>

#define LOG_TAG "mc3303 rtc"

static int rtc_weekday_from_tm(int tm_wday)
{
    if (tm_wday == 0) {
        return 7;
    }
    return tm_wday;
}

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    (void)rtc;
    return QM_EOK;
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *rtc_time)
{
    time_t now;
    struct tm tm_local;

    (void)rtc;

    if (rtc_time == NULL) {
        return -QM_EINVAL;
    }

    if (time(&now) == (time_t)-1) {
        return -QM_EIO;
    }

    if (localtime_r(&now, &tm_local) == NULL) {
        return -QM_EIO;
    }

    rtc_time->sec = (uint8_t)tm_local.tm_sec;
    rtc_time->min = (uint8_t)tm_local.tm_min;
    rtc_time->hr = (uint8_t)tm_local.tm_hour;
    rtc_time->date = (uint8_t)tm_local.tm_mday;
    rtc_time->month = (uint8_t)tm_local.tm_mon + 1;
    rtc_time->year = (uint16_t)tm_local.tm_year + 1900;
    rtc_time->weekday = (uint8_t)rtc_weekday_from_tm(tm_local.tm_wday);

    return QM_EOK;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *rtc_time)
{
    struct tm tm_local = {0};
    struct timeval tv;
    time_t sec;

    (void)rtc;

    if (rtc_time == NULL) {
        return -QM_EINVAL;
    }

    tm_local.tm_year = (int)rtc_time->year - 1900;
    tm_local.tm_mon = (int)rtc_time->month - 1;
    tm_local.tm_mday = (int)rtc_time->date;
    tm_local.tm_hour = (int)rtc_time->hr;
    tm_local.tm_min = (int)rtc_time->min;
    tm_local.tm_sec = (int)rtc_time->sec;

    sec = mktime(&tm_local);
    if (sec == (time_t)-1) {
        return -QM_EINVAL;
    }

    tv.tv_sec = sec;
    tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) != 0) {
        QM_LOGW(LOG_TAG, "set_time failed: %d", errno);
        return -QM_ERROR;
    }

    return QM_EOK;
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    struct timeval tv;

    (void)rtc;

    if (timestamp == NULL) {
        return -QM_EINVAL;
    }

    if (gettimeofday(&tv, NULL) != 0) {
        return -QM_EIO;
    }

    *timestamp = (uint32_t)tv.tv_sec;
    return QM_EOK;
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{
    struct timeval tv;

    (void)rtc;

    if (timestamp == 0) {
        return -QM_EINVAL;
    }

    tv.tv_sec = (time_t)timestamp;
    tv.tv_usec = 0;
    if (settimeofday(&tv, NULL) != 0) {
        QM_LOGW(LOG_TAG, "set_timestamp failed: %d", errno);
        return -QM_ERROR;
    }

    return QM_EOK;
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{
    (void)rtc;
    return QM_EOK;
}
