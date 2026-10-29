#include "qm_rtc.h"
#include "qm_errno.h"

#include <pthread.h>
#include <string.h>
#include <time.h>

static pthread_mutex_t g_rtc_lock = PTHREAD_MUTEX_INITIALIZER;
static int64_t g_rtc_offset_seconds;

static uint8_t bcd_to_dec(uint8_t value)
{
    return (uint8_t)(((value >> 4) * 10U) + (value & 0x0FU));
}

static uint8_t dec_to_bcd(uint8_t value)
{
    return (uint8_t)(((value / 10U) << 4) | (value % 10U));
}

static uint16_t bcd_year_to_dec(uint16_t value)
{
    return (uint16_t)(bcd_to_dec((uint8_t)(value >> 8)) * 100U +
                      bcd_to_dec((uint8_t)value));
}

static uint16_t dec_year_to_bcd(uint16_t value)
{
    return (uint16_t)((uint16_t)dec_to_bcd((uint8_t)(value / 100U)) << 8 |
                      dec_to_bcd((uint8_t)(value % 100U)));
}

static time_t rtc_now(void)
{
    time_t now;
    int64_t offset;

    now = time(NULL);
    pthread_mutex_lock(&g_rtc_lock);
    offset = g_rtc_offset_seconds;
    pthread_mutex_unlock(&g_rtc_lock);
    return now + (time_t)offset;
}

int32_t qm_rtc_init(qm_rtc_dev_t *rtc)
{
    if (rtc == NULL || (rtc->config.format != QM_RTC_FORMAT_DEC &&
                        rtc->config.format != QM_RTC_FORMAT_BCD)) {
        return -QM_EINVAL;
    }
    return QM_EOK;
}

int32_t qm_rtc_get_time(qm_rtc_dev_t *rtc, qm_rtc_time_t *output)
{
    struct tm value;
    time_t now;

    if (rtc == NULL || output == NULL) {
        return -QM_EINVAL;
    }
    now = rtc_now();
    if (gmtime_r(&now, &value) == NULL) {
        return -QM_EIO;
    }

    output->sec = (uint8_t)value.tm_sec;
    output->min = (uint8_t)value.tm_min;
    output->hr = (uint8_t)value.tm_hour;
    output->weekday = (uint8_t)(value.tm_wday == 0 ? 7 : value.tm_wday);
    output->date = (uint8_t)value.tm_mday;
    output->month = (uint8_t)(value.tm_mon + 1);
    output->year = (uint16_t)(value.tm_year + 1900);
    if (rtc->config.format == QM_RTC_FORMAT_BCD) {
        output->sec = dec_to_bcd(output->sec);
        output->min = dec_to_bcd(output->min);
        output->hr = dec_to_bcd(output->hr);
        output->weekday = dec_to_bcd(output->weekday);
        output->date = dec_to_bcd(output->date);
        output->month = dec_to_bcd(output->month);
        output->year = dec_year_to_bcd(output->year);
    }
    return QM_EOK;
}

int32_t qm_rtc_set_time(qm_rtc_dev_t *rtc, const qm_rtc_time_t *input)
{
    qm_rtc_time_t value;
    struct tm broken_down;
    time_t timestamp;

    if (rtc == NULL || input == NULL) {
        return -QM_EINVAL;
    }
    value = *input;
    if (rtc->config.format == QM_RTC_FORMAT_BCD) {
        value.sec = bcd_to_dec(value.sec);
        value.min = bcd_to_dec(value.min);
        value.hr = bcd_to_dec(value.hr);
        value.date = bcd_to_dec(value.date);
        value.month = bcd_to_dec(value.month);
        value.year = bcd_year_to_dec(value.year);
    }
    if (value.sec > 59U || value.min > 59U || value.hr > 23U ||
        value.date == 0U || value.date > 31U || value.month == 0U ||
        value.month > 12U || value.year < 1970U) {
        return -QM_EINVAL;
    }

    memset(&broken_down, 0, sizeof(broken_down));
    broken_down.tm_sec = value.sec;
    broken_down.tm_min = value.min;
    broken_down.tm_hour = value.hr;
    broken_down.tm_mday = value.date;
    broken_down.tm_mon = value.month - 1;
    broken_down.tm_year = value.year - 1900;
    timestamp = timegm(&broken_down);
    if (timestamp < 0) {
        return -QM_EINVAL;
    }
    return qm_rtc_set_timestamp(rtc, (uint32_t)timestamp);
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    time_t now;

    if (rtc == NULL || timestamp == NULL) {
        return -QM_EINVAL;
    }
    now = rtc_now();
    if (now < 0 || (uint64_t)now > UINT32_MAX) {
        return -QM_EINVAL;
    }
    *timestamp = (uint32_t)now;
    return QM_EOK;
}

int32_t qm_rtc_set_timestamp(qm_rtc_dev_t *rtc, uint32_t timestamp)
{
    if (rtc == NULL) {
        return -QM_EINVAL;
    }
    pthread_mutex_lock(&g_rtc_lock);
    g_rtc_offset_seconds = (int64_t)timestamp - (int64_t)time(NULL);
    pthread_mutex_unlock(&g_rtc_lock);
    return QM_EOK;
}

int32_t qm_rtc_deinit(qm_rtc_dev_t *rtc)
{
    return rtc != NULL ? QM_EOK : -QM_EINVAL;
}
