#include "qm_utils_timer.h"

void qm_utils_time_start(qm_utils_time_t *timer)
{
    if (!timer) {
        return;
    }

    timer->time = utils_get_timems();
}

uint32_t qm_utils_time_spend(qm_utils_time_t *start)
{
    uint32_t now, res;

    if (!start) {
        return 0;
    }

    now = utils_get_timems();
    res = now - start->time;
    return res;
}

uint32_t qm_utils_time_left(qm_utils_time_t *end)
{
    uint32_t now, res;

    if (!end) {
        return 0;
    }

    now = utils_get_timems();
    if(now - end->time < (UINT32_MAX/2)){
      return 0;
    }
    else{
      res=end->time-now;
      return res;
    }

}


uint32_t qm_utils_time_is_expired(qm_utils_time_t *timer)
{
    uint32_t cur_time;

    if (!timer) {
        return 1;
    }

    cur_time = utils_get_timems();
    /*
     *  WARNING: Do NOT change the following code until you know exactly what it do!
     *
     *  check whether it reach destination time or not.
     */
    if ((cur_time - timer->time) < (UINT32_MAX / 2)) {
        return 1;
    } else {
        return 0;
    }
}

void qm_utils_time_init(qm_utils_time_t *timer)
{
    if (!timer) {
        return;
    }

    timer->time = 0;
}

void qm_utils_time_countdown_ms(qm_utils_time_t *timer, uint32_t millisecond)
{
    if (!timer) {
        return;
    }

    timer->time = utils_get_timems() + millisecond;
}

