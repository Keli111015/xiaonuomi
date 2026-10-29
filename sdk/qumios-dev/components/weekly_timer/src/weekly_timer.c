#include "weekly_timer.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_types.h"
#include "qm_log.h"
#include "qm_time.h"

#define LOG_TAG "weekly timer"

static volatile uint32_t g_weekly_timer_log_flags = WEEKLY_TIMER_KCONFIG_LOG_FLAGS;

uint32_t weekly_timer_get_log_flags(void)
{
    return g_weekly_timer_log_flags;
}

int weekly_timer_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)WEEKLY_TIMER_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_weekly_timer_log_flags = log_flags;
    return QM_EOK;
}

typedef struct {
    bool_t weekly_loop;               /**< whether the timer would loop weekly */
    weekday_mask_t week_mark;
    uint32_t time_num;               /**< how many event_time this timer should contain */
    uint32_t nearest_tm;      
    uint32_t nearest_idx;
    qm_timer_t timer;
    event_time_t time_group[1];
} event_timer_loop_t;


#define SEC_PER_DAY           86400
#define TIMER_NEAREST_INF     2147483647

static uint32_t weekly_timer_loop_get_nearest(event_timer_loop_t* timer_loop)
{
    struct qm_tm timeinfo = {0};
    uint32_t old_idx = timer_loop->nearest_idx;
    uint32_t time_loc;
    uint32_t g_now = 0;

    g_now = (uint32_t)qm_time(NULL);
    qm_localtime_r((qm_time_t*)&g_now, &timeinfo);

    if (timer_loop->week_mark.day.enable == 0) {
        return TIMER_NEAREST_INF;
    }
    timer_loop->nearest_tm = TIMER_NEAREST_INF;

    for (int i = 0; i < timer_loop->time_num; i++) {
        if (timer_loop->time_group[i].en) {
            event_time_t ev_tm = timer_loop->time_group[i];
            timeinfo.tm_hour = ev_tm.hour;
            timeinfo.tm_min = ev_tm.minute;
            timeinfo.tm_sec = ev_tm.second;
            time_loc = (uint32_t)qm_mktime(&timeinfo);
            if (timer_loop->weekly_loop) {
                int week_day = timeinfo.tm_wday;
                int day_diff = 0;
                if (i == old_idx) {
                    week_day = (week_day + 1) % 7;
                    day_diff++;
                }
                while (day_diff <= 7) {
                    if ((timer_loop->week_mark.en >> week_day) & 0x01) {
                        if ((time_loc + SEC_PER_DAY * day_diff) > g_now) {
                            time_loc += (SEC_PER_DAY * day_diff);
                            break;
                        }
                    }
                    week_day = (week_day + 1) % 7;
                    day_diff++;
                }
            } else {
                time_loc = time_loc > g_now ? time_loc : (time_loc + SEC_PER_DAY);
            }
            if (time_loc < timer_loop->nearest_tm) {
                timer_loop->nearest_tm = time_loc;
                timer_loop->nearest_idx = i;
            }
        }
    }
    return timer_loop->nearest_tm;
}

static int weekly_timer_update(event_timer_loop_t *tm_loop)
{
    uint32_t g_now = 0, tm_loc = 0;
    g_now = (uint32_t)qm_time(NULL);

    tm_loc = weekly_timer_loop_get_nearest(tm_loop);
    qm_timer_stop(&tm_loop->timer);
    if (tm_loc != TIMER_NEAREST_INF && (tm_loc - g_now) > 0) {
        qm_timer_change(&tm_loop->timer, (tm_loc - g_now) * 1000);
        qm_timer_start(&tm_loop->timer);
        WEEKLY_TIMER_LOGD(LOG_TAG, WEEKLY_TIMER_LOG_CONTROL,
                          "the timer at %p will trigger in %ld seconds", tm_loop, tm_loc-g_now);
    }
    return QM_EOK;
}

static void weekly_timer_cb(qm_timer_t *timer, void *arg)
{
    event_timer_loop_t* tm_loop = (event_timer_loop_t*)arg;
    event_time_t* event_tm = &tm_loop->time_group[tm_loop->nearest_idx];
    if (tm_loop->weekly_loop == false) {
        event_tm->en = false;
    }
    event_tm->tm_cb(event_tm->arg);
    weekly_timer_update(tm_loop);
}

weekly_timer_handle_t weekly_timer_add(bool_t weekly_loop, weekday_mask_t week_mark, uint32_t time_num, const event_time_t *time_group)
{
    struct qm_tm timeinfo = {0};
    qm_time_t g_now = 0;
    event_timer_loop_t* new_loop = NULL;
    if(time_group == NULL){
        return NULL;
    }

    new_loop = (event_timer_loop_t*)qm_malloc(sizeof(event_timer_loop_t) + (time_num-1) * sizeof(event_time_t));
    if(new_loop == NULL){
        return NULL;
    }
    new_loop->weekly_loop = weekly_loop;
    new_loop->week_mark = week_mark;
    new_loop->time_num = time_num;
    new_loop->nearest_tm = TIMER_NEAREST_INF;
    new_loop->nearest_idx = TIMER_NEAREST_INF;
    memcpy(new_loop->time_group, time_group, time_num * sizeof(event_time_t));

    qm_timer_new(&new_loop->timer, weekly_timer_cb, new_loop, QM_WAIT_FOREVER, 0);

    g_now = qm_time(NULL);
    qm_localtime_r(&g_now, &timeinfo);

    if (timeinfo.tm_year >= 2023 - 1900) {
        weekly_timer_update(new_loop);
    }
    return (weekly_timer_handle_t)new_loop;
}

int weekly_timer_delete(weekly_timer_handle_t timer_handle)
{
    event_timer_loop_t* tm_loop = (event_timer_loop_t*) timer_handle;
    if(timer_handle == NULL){
        return -QM_EINVAL;
    }
    qm_timer_stop(&tm_loop->timer);
    qm_timer_free(&tm_loop->timer);
    qm_free(tm_loop);
    return QM_EOK;
}

int weekly_timer_start(weekly_timer_handle_t timer_handle)
{
    int i = 0;
    event_timer_loop_t *tm_loop = (event_timer_loop_t*)timer_handle;
    if(timer_handle == NULL){
        return -QM_EINVAL;
    }
    tm_loop->week_mark.day.enable = 1;
    tm_loop->nearest_tm = TIMER_NEAREST_INF;
    tm_loop->nearest_idx = TIMER_NEAREST_INF;
    for(i = 0; i < tm_loop->time_num; i++) {
        tm_loop->time_group[i].en = true;
    }
    weekly_timer_update(tm_loop);
    return QM_EOK;
}

int weekly_timer_stop(weekly_timer_handle_t timer_handle)
{
    int i = 0;
    event_timer_loop_t *tm_loop = (event_timer_loop_t*)timer_handle;
    if(timer_handle == NULL){
        return -QM_EINVAL;
    }
    tm_loop->week_mark.day.enable = 0;
    tm_loop->nearest_tm = TIMER_NEAREST_INF;
    tm_loop->nearest_idx = TIMER_NEAREST_INF;
    for(i = 0; i < tm_loop->time_num; i++) {
        tm_loop->time_group[i].en = false;
    }
    weekly_timer_update(tm_loop);
    return QM_EOK;
}
