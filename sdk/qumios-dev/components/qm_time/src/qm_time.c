#include "qm_time.h"
#include "qm_rtc.h"
#include "qm_errno.h"
#include "qm_kernel.h"

#if CONFIG_QM_TIME_SUPPORT

#ifndef CONFIG_QM_TIMEZONE_MINUTESWEST
#define CONFIG_QM_TIMEZONE_MINUTESWEST   8*60
#endif

static struct qm_timezone g_qm_timezone = {0};
static qm_rtc_dev_t g_rtc_dev = {
    .config = {
        .format = QM_RTC_FORMAT_DEC,
    },
};

int qm_time_init(void)
{
    qm_rtc_init(&g_rtc_dev);
    g_qm_timezone.minuteswest = CONFIG_QM_TIMEZONE_MINUTESWEST;
    return QM_EOK;
}

int qm_gettimeofday(struct qm_timeval *tv, struct qm_timezone *tz)
{
    qm_err_t ret = QM_EOK;
    uint32_t timestamp = 0;
    if(tv == NULL){
        return -QM_EINVAL;
    }

    ret = qm_rtc_get_timestamp(&g_rtc_dev, &timestamp);
    if(ret != QM_EOK){
        return 0;
    }
    tv->tv_sec = timestamp;

    if(tz){
        memcpy(tz, &g_qm_timezone.minuteswest, sizeof(struct qm_timezone));
    }
    return QM_EOK;
}

int qm_settimeofday(const struct qm_timeval *tv, const struct qm_timezone *tz)
{
    qm_err_t ret = QM_EOK;
    if(tv == NULL){
        return -QM_EINVAL;
    }

    ret = qm_rtc_set_timestamp(&g_rtc_dev, (uint32_t)tv->tv_sec);
    if(ret != QM_EOK){
        return ret;
    }

    if(tz){
        memcpy(&g_qm_timezone.minuteswest, tz, sizeof(struct qm_timezone));
    }
    return QM_EOK;
}

int qm_settimezone(const struct qm_timezone *tz)
{
    if(tz == NULL){
        return -QM_EINVAL;
    }
    memcpy(&g_qm_timezone.minuteswest, tz, sizeof(struct qm_timezone));
    return QM_EOK;
}

static struct qm_tm *_qm_gmtime_r(const qm_time_t *time, struct qm_tm *tm)
{
    uint32_t n32_Pass4year, n32_hpery;
    const static char Days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const static int ONE_YEAR_HOURS = 8760;
    qm_time_t qm_time = *time;

    tm->tm_sec = (int)(qm_time % 60);
    qm_time /= 60;
    tm->tm_min = (int)(qm_time % 60);
    qm_time /= 60;
    tm->tm_wday = (qm_time / 24 + 4) % 7;
    n32_Pass4year = ((uint32_t)qm_time / (1461L * 24L));
    tm->tm_year = (n32_Pass4year << 2) + 70;
    qm_time %= 1461L * 24L;
    tm->tm_yday = (qm_time / 24) % 365;

    for (;;){
        
        n32_hpery = ONE_YEAR_HOURS;
        if ((tm->tm_year & 3) == 0){
            n32_hpery += 24;
        }
        if (qm_time < n32_hpery){
            break;
        }
        tm->tm_year++;
        qm_time -= n32_hpery;
    }

    tm->tm_hour = (int)(qm_time % 24);
    qm_time /= 24;
    qm_time++;

    if ((tm->tm_year & 3) == 0){
        if (qm_time > 60){
            qm_time--;
        }
        else{
            if (qm_time == 60){
                tm->tm_mon = 1;
                tm->tm_mday = 29;
                return tm;
            }
        }
    }

    for (tm->tm_mon = 0; Days[tm->tm_mon] < qm_time; tm->tm_mon++){
        qm_time -= Days[tm->tm_mon];
    }

    tm->tm_mday = (int)(qm_time);
    return tm;
}

struct qm_tm *_qm_localtime_r(const qm_time_t *time, struct qm_tm *tm)
{
    qm_time_t qm_time = *time;
    qm_time = qm_time + g_qm_timezone.minuteswest * 60;
    return _qm_gmtime_r(&qm_time, tm);
}

struct qm_tm *qm_localtime_r(const qm_time_t *time, struct qm_tm *tm)
{
    qm_time_t time_tmp = *time;
    if(time == NULL || tm == NULL){
        return NULL;
    }
    return _qm_localtime_r(&time_tmp, tm);
}

struct qm_tm *qm_localtime(const qm_time_t *time)
{
    static struct qm_tm qm_tm = {0};
    if(time == NULL){
        return NULL;
    }
    return _qm_localtime_r(time, &qm_tm);
}

struct qm_tm *qm_gmtime(const qm_time_t *time)
{   
    static struct qm_tm qm_tm = {0};
    return _qm_gmtime_r(time, &qm_tm);
}

struct qm_tm *qm_gmtime_r(const qm_time_t *time, struct qm_tm *tm)
{
    return _qm_gmtime_r(time, tm);
}

static qm_time_t _qm_mktime(const unsigned int year0, const unsigned int mon0,
        const unsigned int day, const unsigned int hour,
        const unsigned int min, const unsigned int sec)
{
    unsigned int mon = mon0, year = year0;

    /* 1..12 -> 11,12,1..10 */
    if (0 >= (int) (mon -= 2)) {
        mon += 12;    /* Puts Feb last since it has leap day */
        year -= 1;
    }

    return ((((qm_time_t)
          (year/4 - year/100 + year/400 + 367*mon/12 + day) +
          year*365 - 719499
        )*24 + hour /* now have hours */
      )*60 + min /* now have minutes */
    )*60 + sec; /* finally seconds */
}

qm_time_t qm_mktime(struct qm_tm *tm)
{
    if(tm == NULL){
        return 0;
    }
    return _qm_mktime(tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
}

qm_time_t qm_time(qm_time_t *time)
{
    qm_err_t ret = QM_EOK;
    uint32_t timestamp = 0;

    ret = qm_rtc_get_timestamp(&g_rtc_dev, &timestamp);
    if(ret != QM_EOK){
        return 0;
    }

    if(time){
        *time = (qm_time_t)timestamp;
    }
    return (qm_time_t)timestamp;
}

char *qm_ctime(const qm_time_t *time)
{
    return qm_asctime(qm_localtime(time));
}

char *qm_ctime_r(const qm_time_t *time, char *buf)
{
    return qm_asctime_r(qm_localtime(time), buf);
}

char *qm_asctime(const struct qm_tm *tm)
{
    char *week = "SunMonTueWedThuFriSat";
    char *month = "JanFebMarAprMayJunJulAugSepOctNovDec";

    static char asctime_buf[26] = {0};
    qm_snprintf(asctime_buf, 26, "%.3s %.3s %02d %02d:%02d:%02d %04d\n",
            week+3*tm->tm_wday,
            month+3*tm->tm_mon,
            tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
            tm->tm_year+1900);
    return asctime_buf;
}

char *qm_asctime_r(const struct qm_tm *tm, char *buf)
{
    char *week = "SunMonTueWedThuFriSat";
    char *month = "JanFebMarAprMayJunJulAugSepOctNovDec";

    qm_snprintf(buf, 26, "%.3s %.3s %02d %02d:%02d:%02d %04d\n",
            week+3*tm->tm_wday,
            month+3*tm->tm_mon,
            tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec,
            tm->tm_year+1900);
    return buf;
}

char *qm_time_print(void)
{
    qm_time_t now = 0;
    now = qm_time(NULL);
    return qm_ctime(&now);
}

#endif
