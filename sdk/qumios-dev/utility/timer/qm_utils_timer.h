#ifndef _QM_UTILS_TIMER_H_
#define _QM_UTILS_TIMER_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

typedef struct {
    uint32_t time;
} qm_utils_time_t;

#define utils_get_timems  qm_now_ms

void qm_utils_time_start(qm_utils_time_t *timer);

uint32_t qm_utils_time_spend(qm_utils_time_t *start);

uint32_t qm_utils_time_left(qm_utils_time_t *end);

uint32_t qm_utils_time_is_expired(qm_utils_time_t *timer);

void qm_utils_time_init(qm_utils_time_t *timer);

void qm_utils_time_countdown_ms(qm_utils_time_t *timer, uint32_t millisecond);

#ifdef __cplusplus
}
#endif

#endif /* _IOTX_COMMON_TIMER_H_ */
