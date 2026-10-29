#ifndef _QM_TIME_H_
#define _QM_TIME_H_

#include "qm_types.h"
#include "qm_config.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_QM_TIME_SUPPORT

typedef uint64_t qm_time_t;

typedef struct qm_timeval{
    qm_time_t tv_sec;  /* Seconds. */
    qm_time_t tv_usec;  /* Microseconds. */
}qm_timeval_t;

typedef struct qm_timezone{
    int minuteswest;/* minutes west of Greenwich */
}qm_timezone_t;

struct qm_tm
{
  int tm_sec;			/* Seconds.	[0-60] (1 leap second) */
  int tm_min;			/* Minutes.	[0-59] */
  int tm_hour;			/* Hours.	[0-23] */
  int tm_mday;			/* Day.		[1-31] */
  int tm_mon;			/* Month.	[0-11] */
  int tm_year;			/* Year	- 1900.  */
  int tm_wday;			/* Day of week.	[0-6] */
  int tm_yday;			/* Days in year.[0-365]	*/
};

/* init time */
int qm_time_init(void);

/* Return the current time and put it in *TIMER if TIMER is not NULL.  */
qm_time_t qm_time(qm_time_t *time);

/* Get the current time of day and timezone information */
int qm_gettimeofday(struct qm_timeval *tv, struct qm_timezone *tz);

/* Set the current time of day and timezone information */
int qm_settimeofday(const struct qm_timeval *tv, const struct qm_timezone *tz);

/* Set timezone information */
int qm_settimezone(const struct qm_timezone *tz);

/* Return the `struct tm' representation
   of *TIMER in the local timezone.  */
struct qm_tm *qm_localtime(const qm_time_t *time);

/* Return the `struct tm' representation of *TIMER in local time,
   using *TP to store the result.  */
struct qm_tm *qm_localtime_r(const qm_time_t *time, struct qm_tm *tm);

/* Return the `struct tm' representation of *TIMER
   in Universal Coordinated Time (aka Greenwich Mean Time).  */
struct qm_tm *qm_gmtime(const qm_time_t *time);

/* Return the `struct tm' representation of *TIMER in UTC,
   using *TP to store the result.  */
struct qm_tm *qm_gmtime_r(const qm_time_t *time, struct qm_tm *tm);

/* Return the `time_t' representation of TP and normalize TP.  */
qm_time_t qm_mktime (struct qm_tm *tm);

/* Return a string of the form "Day Mon dd hh:mm:ss yyyy\n"
   that is the representation of TP in this format.  */
char *qm_asctime(const struct qm_tm *tm);

/* Equivalent to `asctime (localtime (timer))'.  */
char *qm_ctime(const qm_time_t *time);

/* Return in BUF a string of the form "Weekday Day Mon dd hh:mm:ss yyyy\n"
   that is the representation of TP in this format.  */
char *qm_asctime_r(const struct qm_tm *tm, char *buf);

/* Equivalent to `asctime_r (localtime_r (timer, *TMP*), buf)'.  */
char *qm_ctime_r(const qm_time_t *time, char *buf);

char *qm_time_print(void);

#ifdef __cplusplus
}
#endif

#endif

#endif

