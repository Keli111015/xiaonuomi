/*
 * qm_rtc.c - QUMI RTC HAL for JieLi AC7911BX/WL82
 *
 * JieLi exposes the RTC as a device named "rtc".  This adapter keeps the
 * vendor calendar representation at the platform boundary and exposes the
 * QUMI timestamp API in UTC seconds.
 */

#include <string.h>

#include "device/device.h"
#include "driver/device/ioctl_cmds.h"
#include "system/sys_time.h"
#include "asm/rtc.h"

#include "qm_config.h"
#include "qm_errno.h"
#include "qm_rtc.h"

#ifndef CONFIG_QM_TIMEZONE_MINUTESWEST
#ifdef CONFIG_QM_IOT_NTP_TIMEZONE
#define CONFIG_QM_TIMEZONE_MINUTESWEST (CONFIG_QM_IOT_NTP_TIMEZONE * 60)
#else
#define CONFIG_QM_TIMEZONE_MINUTESWEST (8 * 60)
#endif
#endif

#define RTC_MIN_VALID_YEAR       2024U
#define RTC_MAX_VALID_YEAR       2105U
#define RTC_SECONDS_PER_DAY      86400ULL
#define RTC_LOCAL_OFFSET_SECONDS ((int64_t)CONFIG_QM_TIMEZONE_MINUTESWEST * 60LL)

extern void set_alarm_ctrl(u8 set_alarm);

static uint8_t rtc_is_leap_year(uint16_t year)
{
    return ((year % 4U) == 0U && (year % 100U) != 0U) ||
           ((year % 400U) == 0U);
}

static uint8_t rtc_days_in_month(uint16_t year, uint8_t month)
{
    static const uint8_t days[12] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    if (month == 0U || month > 12U) {
        return 0U;
    }
    if (month == 2U && rtc_is_leap_year(year)) {
        return 29U;
    }
    return days[month - 1U];
}

static int rtc_calendar_to_timestamp(const struct sys_time *calendar,
                                     uint32_t *timestamp)
{
    uint64_t days = 0;
    int64_t local_seconds;
    int64_t utc_seconds;
    uint16_t year;
    uint8_t month;

    if (calendar == NULL || timestamp == NULL ||
        calendar->year < RTC_MIN_VALID_YEAR ||
        calendar->year > RTC_MAX_VALID_YEAR ||
        calendar->month == 0U || calendar->month > 12U ||
        calendar->day == 0U ||
        calendar->day > rtc_days_in_month(calendar->year, calendar->month) ||
        calendar->hour > 23U || calendar->min > 59U || calendar->sec > 59U) {
        return -QM_EINVAL;
    }

    for (year = 1970U; year < calendar->year; year++) {
        days += rtc_is_leap_year(year) ? 366ULL : 365ULL;
    }
    for (month = 1U; month < calendar->month; month++) {
        days += rtc_days_in_month(calendar->year, month);
    }
    days += (uint64_t)calendar->day - 1ULL;

    local_seconds = (int64_t)(days * RTC_SECONDS_PER_DAY) +
                    (int64_t)calendar->hour * 3600LL +
                    (int64_t)calendar->min * 60LL +
                    calendar->sec;
    utc_seconds = local_seconds - RTC_LOCAL_OFFSET_SECONDS;
    if (utc_seconds <= 0 || (uint64_t)utc_seconds > 0xFFFFFFFFULL) {
        return -QM_EINVAL;
    }

    *timestamp = (uint32_t)utc_seconds;
    return QM_EOK;
}

static int rtc_timestamp_to_calendar(uint32_t timestamp,
                                     struct sys_time *calendar)
{
    int64_t local_seconds = (int64_t)timestamp + RTC_LOCAL_OFFSET_SECONDS;
    uint64_t days;
    uint32_t seconds_of_day;
    uint16_t year = 1970U;
    uint8_t month = 1U;
    uint16_t days_in_year;
    uint8_t days_in_month;

    if (calendar == NULL || local_seconds <= 0) {
        return -QM_EINVAL;
    }

    days = (uint64_t)local_seconds / RTC_SECONDS_PER_DAY;
    seconds_of_day = (uint32_t)((uint64_t)local_seconds % RTC_SECONDS_PER_DAY);

    while (year <= RTC_MAX_VALID_YEAR) {
        days_in_year = rtc_is_leap_year(year) ? 366U : 365U;
        if (days < days_in_year) {
            break;
        }
        days -= days_in_year;
        year++;
    }
    if (year < RTC_MIN_VALID_YEAR || year > RTC_MAX_VALID_YEAR) {
        return -QM_EINVAL;
    }

    while (month <= 12U) {
        days_in_month = rtc_days_in_month(year, month);
        if (days < days_in_month) {
            break;
        }
        days -= days_in_month;
        month++;
    }
    if (month > 12U) {
        return -QM_EINVAL;
    }

    memset(calendar, 0, sizeof(*calendar));
    calendar->year = year;
    calendar->month = month;
    calendar->day = (uint8_t)days + 1U;
    calendar->hour = (uint8_t)(seconds_of_day / 3600U);
    calendar->min = (uint8_t)((seconds_of_day % 3600U) / 60U);
    calendar->sec = (uint8_t)(seconds_of_day % 60U);
    return QM_EOK;
}

static int rtc_device_get_calendar(struct sys_time *calendar)
{
    void *rtc_hdl;
    int ret;

    if (calendar == NULL) {
        return -QM_EINVAL;
    }

    rtc_hdl = dev_open("rtc", NULL);
    if (rtc_hdl == NULL) {
        return -QM_EIO;
    }
    memset(calendar, 0, sizeof(*calendar));
    ret = dev_ioctl(rtc_hdl, IOCTL_GET_SYS_TIME, (u32)calendar);
    dev_close(rtc_hdl);
    return ret == 0 ? QM_EOK : -QM_EIO;
}

static int rtc_device_set_calendar(const struct sys_time *calendar)
{
    void *rtc_hdl;
    int ret;

    if (calendar == NULL) {
        return -QM_EINVAL;
    }

    rtc_hdl = dev_open("rtc", NULL);
    if (rtc_hdl == NULL) {
        return -QM_EIO;
    }
    ret = dev_ioctl(rtc_hdl, IOCTL_SET_SYS_TIME, (u32)calendar);
    dev_close(rtc_hdl);
    return ret == 0 ? QM_EOK : -QM_EIO;
}

static int rtc_device_set_alarm(const struct sys_time *alarm_time)
{
    void *rtc_hdl;
    int ret;

    if (alarm_time == NULL) {
        return -QM_EINVAL;
    }

    rtc_hdl = dev_open("rtc", NULL);
    if (rtc_hdl == NULL) {
        return -QM_EIO;
    }

    set_alarm_ctrl(1);
    ret = dev_ioctl(rtc_hdl, IOCTL_SET_ALARM, (u32)alarm_time);
    dev_close(rtc_hdl);
    if (ret != 0) {
        set_alarm_ctrl(0);
        return -QM_EIO;
    }

    return QM_EOK;
}

static uint8_t rtc_weekday_from_timestamp(uint32_t timestamp)
{
    int64_t local_seconds = (int64_t)timestamp + RTC_LOCAL_OFFSET_SECONDS;
    uint8_t weekday;

    if (local_seconds <= 0) {
        return 0U;
    }

    /* Public QUMI RTC uses Monday=1 ... Sunday=7. */
    weekday = (uint8_t)((((uint64_t)local_seconds / RTC_SECONDS_PER_DAY) + 4ULL) % 7ULL);
    return weekday == 0U ? 7U : weekday;
}

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    (void)rtc;
    /* rtc_early_init() is performed by the JieLi board initialization. */
    return QM_EOK;
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    struct sys_time calendar;
    int ret;

    (void)rtc;
    if (timestamp == NULL) {
        return -QM_EINVAL;
    }

    ret = rtc_device_get_calendar(&calendar);
    if (ret != QM_EOK) {
        return ret;
    }

    return rtc_calendar_to_timestamp(&calendar, timestamp);
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *time)
{
    struct sys_time calendar;
    uint32_t timestamp;
    int ret;

    (void)rtc;
    if (time == NULL) {
        return -QM_EINVAL;
    }

    ret = rtc_device_get_calendar(&calendar);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = rtc_calendar_to_timestamp(&calendar, &timestamp);
    if (ret != QM_EOK) {
        return ret;
    }

    time->sec = calendar.sec;
    time->min = calendar.min;
    time->hr = calendar.hour;
    time->date = calendar.day;
    time->month = calendar.month;
    time->year = calendar.year;
    time->weekday = rtc_weekday_from_timestamp(timestamp);
    return QM_EOK;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *time)
{
    struct sys_time calendar;
    uint32_t timestamp;

    (void)rtc;
    if (time == NULL) {
        return -QM_EINVAL;
    }

    memset(&calendar, 0, sizeof(calendar));
    calendar.year = time->year;
    calendar.month = time->month;
    calendar.day = time->date;
    calendar.hour = time->hr;
    calendar.min = time->min;
    calendar.sec = time->sec;
    if (rtc_calendar_to_timestamp(&calendar, &timestamp) != QM_EOK) {
        return -QM_EINVAL;
    }

    return rtc_device_set_calendar(&calendar);
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{
    struct sys_time calendar;
    int ret;

    (void)rtc;
    ret = rtc_timestamp_to_calendar(timestamp, &calendar);
    if (ret != QM_EOK) {
        return ret;
    }

    return rtc_device_set_calendar(&calendar);
}

int32_t qm_rtc_alarm_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{
    struct sys_time alarm_time;
    int ret;

    (void)rtc;
    ret = rtc_timestamp_to_calendar(timestamp, &alarm_time);
    if (ret != QM_EOK) {
        return ret;
    }

    return rtc_device_set_alarm(&alarm_time);
}

int32_t qm_rtc_alarm_cancel(qm_rtc_dev_t *rtc)
{
    (void)rtc;
    set_alarm_ctrl(0);
    return QM_EOK;
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{
    (void)rtc;
    /* The vendor RTC handle is opened and closed for every operation. */
    return QM_EOK;
}
