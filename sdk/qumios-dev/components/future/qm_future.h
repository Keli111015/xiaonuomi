#ifndef _QM_FUTURE_H_
#define _QM_FUTURE_H_

#ifdef __cplusplus
extern "C" {
#endif
    
#include "qm_types.h"

/**
 * @brief           Constructs a new future object
 * @return          Returns NULL on failure.
 */
void *qm_future_new(void *arg);

/**
 * @brief           Constructs a new future_t object with an immediate |value|,No waiting will
 *                  occur in the call to |qm_future_await| because the value is already present.
 * @param[in]       value: pointer of value
 * @return          Returns NULL on failure.
 *
 */
void *qm_future_new_immediate(void *value, void *arg);

/**
 * @brief           Signals that the |future| is ready, passing |value| back to the context
 *                  waiting for the result. Must only be called once for every future. |future| may not be NULL.
 * @param[in]       future: future object
 * @param[in]       value: pointer of value
 * @return          - QM_EOK : success
 *                  - other  : failed
 */
int qm_future_ready(void *future, void *value);

/**
 * @brief           get arg.
 * @param[in]       future: future object
 * @param[in]       arg: pointer of arg
 * @return          - QM_EOK : success
 *                  - other  : failed
 */
int qm_future_arg_get(void *future, void **arg);

/**
 * @brief           Signals that the |future| is ready, passing |value| back to the context
 *                  waiting for the result. Must only be called once for every future. |future| may not be NULL.
 * @param[in]       future: future object
 * @param[in]       value: pointer of value
 * @param[in]       timeout: wait time
 * @return          - QM_EOK : success
 *                  - other  : failed
 *
 */
int qm_future_wait(void *future, void **value, uint32_t timeout);

/**
 * @brief           Free the future if this "future" is not used
 * @param[in]       future: future object
 * @return          - QM_EOK : success
 *                  - other  : failed
 */
int qm_future_free(void *future);


#ifdef __cplusplus
}
#endif


#endif /* _QM_FUTURE_H_ */
