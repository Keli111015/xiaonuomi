#include "qm_schedule_internal.h"

static int parse_two_digits(const char *text, int *value)
{
    if (!text || text[0] < '0' || text[0] > '9' ||
        text[1] < '0' || text[1] > '9') {
        return -QM_EINVAL;
    }
    *value = ((text[0] - '0') * 10) + (text[1] - '0');
    return QM_EOK;
}

static int parse_date(const char *text, int *year, int *month, int *day)
{
    int value;

    if (!text || strlen(text) != 10 || text[4] != '-' || text[7] != '-') {
        return -QM_EINVAL;
    }
    if (parse_two_digits(text + 5, month) != QM_EOK ||
        parse_two_digits(text + 8, day) != QM_EOK) {
        return -QM_EINVAL;
    }
    if (text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' ||
        text[2] < '0' || text[2] > '9' || text[3] < '0' || text[3] > '9') {
        return -QM_EINVAL;
    }
    value = ((text[0] - '0') * 1000) + ((text[1] - '0') * 100) +
        ((text[2] - '0') * 10) + (text[3] - '0');
    *year = value;
    if (*month < 1 || *month > 12 || *day < 1 || *day > 31) {
        return -QM_EINVAL;
    }
    {
        static const uint8_t days_in_month[] =
            {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        uint8_t max_day = days_in_month[*month - 1];
        bool leap = (*year % 4 == 0 && *year % 100 != 0) ||
            (*year % 400 == 0);
        if (*month < 1 || *month > 12 || *day < 1 ||
            *day > max_day + ((*month == 2 && leap) ? 1 : 0)) {
            return -QM_EINVAL;
        }
    }
    return QM_EOK;
}

static int64_t days_from_civil(int year, unsigned month, unsigned day)
{
    int adjusted_year = year - (month <= 2);
    int era = (adjusted_year >= 0 ? adjusted_year : adjusted_year - 399) / 400;
    unsigned year_of_era = (unsigned)(adjusted_year - era * 400);
    unsigned month_prime = (unsigned)((int)month + (month > 2 ? -3 : 9));
    unsigned day_of_year = (153 * month_prime + 2) / 5 +
        day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 -
        year_of_era / 100 + day_of_year;

    return (int64_t)era * 146097 + (int64_t)day_of_era - 719468;
}

static int timezone_offset_sec(const char *timezone, int *offset_sec)
{
    const char *pos = timezone;
    int sign = 1;
    int hour = 0;
    int minute = 0;

    if (!timezone || timezone[0] == '\0' || !offset_sec) {
        return -QM_EINVAL;
    }
    if (strcmp(timezone, "UTC") == 0 ||
        strcmp(timezone, "GMT") == 0 ||
        strcmp(timezone, "Z") == 0 ||
        strcmp(timezone, "Etc/UTC") == 0) {
        *offset_sec = 0;
        return QM_EOK;
    }

    if (strncmp(pos, "UTC", 3) == 0 || strncmp(pos, "GMT", 3) == 0) {
        pos += 3;
    }
    if (*pos == '+') {
        sign = 1;
        pos++;
    } else if (*pos == '-') {
        sign = -1;
        pos++;
    } else {
        return -QM_EINVAL;
    }

    if (parse_two_digits(pos, &hour) != QM_EOK) {
        return -QM_EINVAL;
    }
    pos += 2;
    if (*pos == ':') {
        pos++;
        if (parse_two_digits(pos, &minute) != QM_EOK) {
            return -QM_EINVAL;
        }
        pos += 2;
    } else if (pos[0] >= '0' && pos[0] <= '9') {
        if (parse_two_digits(pos, &minute) != QM_EOK) {
            return -QM_EINVAL;
        }
        pos += 2;
    }
    if (*pos != '\0' || hour > 14 || minute > 59 ||
        (hour == 14 && minute != 0)) {
        return -QM_EINVAL;
    }
    *offset_sec = sign * ((hour * 3600) + (minute * 60));
    return QM_EOK;
}

bool qm_schedule_timezone_supported(const char *timezone)
{
    int offset_sec = 0;

    return timezone_offset_sec(timezone, &offset_sec) == QM_EOK;
}

int qm_schedule_recurrence_date_to_utc(const char *date,
                                       const char *timezone,
                                       bool end_of_day,
                                       int64_t *utc)
{
    int year;
    int month;
    int day;
    int offset_sec = 0;

    if (!utc || parse_date(date, &year, &month, &day) != QM_EOK ||
        timezone_offset_sec(timezone, &offset_sec) != QM_EOK) {
        return -QM_EINVAL;
    }
    *utc = days_from_civil(year, (unsigned)month, (unsigned)day) *
        QM_SCHEDULE_SEC_PER_DAY + (end_of_day ? 86399 : 0) - offset_sec;
    return QM_EOK;
}

static int weekday_from_local_day(int64_t local_second)
{
    int64_t days_since_epoch = local_second / QM_SCHEDULE_SEC_PER_DAY;
    int weekday;

    if (local_second < 0 &&
        (local_second % QM_SCHEDULE_SEC_PER_DAY) != 0) {
        days_since_epoch--;
    }
    weekday = (int)((days_since_epoch + 4) % 7);
    if (weekday < 0) {
        weekday += 7;
    }
    return weekday;
}

static int next_by_recurrence(qm_schedule_recurrence_t recurrence,
                              uint8_t weekly_mask,
                              int64_t start_at,
                              int64_t until_at,
                              const char *timezone,
                              int64_t after_utc,
                              int64_t *next_utc)
{
    int offset_sec = 0;
    int64_t start_local;
    int64_t after_local;
    int64_t candidate;
    int i;

    if (!next_utc || start_at <= 0 ||
        timezone_offset_sec(timezone, &offset_sec) != QM_EOK) {
        return -QM_EINVAL;
    }
    start_local = start_at + offset_sec;
    after_local = after_utc + offset_sec;

    if (recurrence == QM_SCHEDULE_RECURRENCE_ONCE) {
        if (start_at > after_utc &&
            (until_at == 0 || start_at <= until_at)) {
            *next_utc = start_at;
            return QM_EOK;
        }
        return -QM_ENOENT;
    }

    if (recurrence == QM_SCHEDULE_RECURRENCE_DAILY) {
        candidate = start_local;
        if (candidate <= after_local) {
            int64_t diff = after_local - candidate;
            int64_t steps = (diff / QM_SCHEDULE_SEC_PER_DAY) + 1;
            candidate += steps * QM_SCHEDULE_SEC_PER_DAY;
        }
        candidate -= offset_sec;
        if (until_at != 0 && candidate > until_at) {
            return -QM_ENOENT;
        }
        *next_utc = candidate;
        return QM_EOK;
    }

    if (recurrence != QM_SCHEDULE_RECURRENCE_WEEKLY || weekly_mask == 0) {
        return -QM_EINVAL;
    }

    candidate = start_local;
    if (candidate <= after_local) {
        int64_t diff = after_local - candidate;
        int64_t weeks = diff / QM_SCHEDULE_SEC_PER_WEEK;
        candidate += weeks * QM_SCHEDULE_SEC_PER_WEEK;
    }

    for (i = 0; i < 14; i++) {
        int64_t local_day = candidate + ((int64_t)i * QM_SCHEDULE_SEC_PER_DAY);
        int64_t day = local_day - offset_sec;
        int weekday = weekday_from_local_day(local_day);

        if (day <= after_utc) {
            continue;
        }
        if (((weekly_mask >> weekday) & 0x01u) == 0) {
            continue;
        }
        if (until_at != 0 && day > until_at) {
            return -QM_ENOENT;
        }
        *next_utc = day;
        return QM_EOK;
    }

    return -QM_ENOENT;
}

int qm_schedule_recurrence_next(const qm_schedule_item_t *item,
                                int64_t after_utc,
                                int64_t *next_utc)
{
    if (!item) {
        return -QM_EINVAL;
    }

    return next_by_recurrence(item->recurrence, item->weekly_mask,
                              item->start_at, item->recurrence_until,
                              item->timezone, after_utc, next_utc);
}

int qm_schedule_recurrence_next_record(const qm_schedule_item_record_t *item,
                                       int64_t after_utc,
                                       int64_t *next_utc)
{
    if (!item || !item->used) {
        return -QM_EINVAL;
    }

    return next_by_recurrence(item->recurrence, item->weekly_mask,
                              item->start_at, item->recurrence_until,
                              item->timezone, after_utc, next_utc);
}
