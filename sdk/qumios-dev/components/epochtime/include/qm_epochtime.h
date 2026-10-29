
#ifndef _QM_EPOCH_TIME_H_
#define _QM_EPOCH_TIME_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_config.h"
#include "qm_epochtime_log.h"

#if CONFIG_QM_EPOCHTIME_SUPPORT

struct timeval_t {
    uint32_t tv_sec;
    uint32_t tv_usec;
};

/**
 * @brief Get epoch time from the NTP.
 *        The type of the epoch time is millisecond.
 *
 * @param time
 *
 * @return 0, failed to get epoch time; OTHERS, the actual value of epoch time
 */
int qm_get_epoch_time_from_ntp(struct timeval_t *time);

#endif

#ifdef __cplusplus
}
#endif
#endif 
