#ifndef BACKOFF_ALGORITHM_H_
#define BACKOFF_ALGORITHM_H_

#include "qm_types.h"

/* *INDENT-OFF* */
#ifdef __cplusplus
    extern "C" {
#endif
/* *INDENT-ON* */

/**
 * @ingroup backoff_algorithm_constants
 * @brief Constant to represent unlimited number of retry attempts.
 */
#define BACKOFF_ALGORITHM_RETRY_FOREVER    ( UINT32_MAX )

/**
 * @ingroup backoff_algorithm_enum_types
 * @brief Status for @ref backoff_algorithm_get_next_delay.
 */
typedef enum 
{ 
    BACKOFF_ALGORITHM_SUCCESS = 0,      /**< @brief The function successfully calculated the next back-off value. */
    BACKOFF_ALGORITHM_RETRY_EXHAUSTED,  /**< @brief The function exhausted all retry attempts. */
} backoff_algorithm_status_t;

/**
 * @ingroup backoff_algorithm_struct_types
 * @brief Represents parameters required for calculating the back-off delay for the
 * next retry attempt.
 */
typedef struct
{
    /**
     * @brief The maximum backoff delay (in milliseconds) between consecutive retry attempts.
     */
    uint16_t max_backoff;

    /**
     * @brief The total number of retry attempts completed.
     * This value is incremented on every call to #backoff_algorithm_get_next_delay API.
     */
    uint32_t attempts_num;

    /**
     * @brief The maximum backoff value (in milliseconds) for the next retry attempt.
     */
    uint16_t next_backoff;

    /**
     * @brief The maximum number of retry attempts.
     */
    uint32_t max_attempts;
} backoff_algorithm_ctx_t;

/**
 * @brief Initializes the context for using backoff algorithm. The parameters
 * are required for calculating the next retry backoff delay.
 * This function must be called by the application before the first new retry attempt.
 *
 * @param[out] ctx The context to initialize with parameters required
 * for the next backoff delay calculation function.
 * @param[in] backoff_base The maximum backoff delay (in milliseconds) between
 * consecutive retry attempts.
 * @param[in] max_backoff The base value (in milliseconds) of backoff delay to
 * use in the exponential backoff and jitter model.
 * @param[in] max_attempts The maximum number of retry attempts. Set the value to
 * #BACKOFF_ALGORITHM_RETRY_FOREVER to retry for ever.
 */
void backoff_algorithm_init(backoff_algorithm_ctx_t *ctx, uint16_t backoff_base, uint16_t max_backoff, uint32_t max_attempts);

/**
 * @brief Simple exponential backoff and jitter function that provides the
 * delay value for the next retry attempt.
 * After a failure of an operation that needs to be retried, the application
 * should use this function to obtain the backoff delay value for the next retry,
 * and then wait for the backoff time period before retrying the operation.
 *
 * @param[in, out] ctx Structure containing parameters for the next backoff
 * value calculation.
 * @param[out] next_backoff This will be populated with the backoff value (in milliseconds)
 * for the next retry attempt. The value does not exceed the maximum backoff delay
 * configured in the context.
 *
 * @note For generating a random number, it is recommended to use a Random Number Generator
 * that is seeded with a device-specific entropy source so that possibility of collisions
 * between multiple devices retrying the network operations can be mitigated.
 *
 * @return #BACKOFF_ALGORITHM_SUCCESS after a successful sleep;
 * #BACKOFF_ALGORITHM_RETRY_EXHAUSTED when all attempts are exhausted.
 */
backoff_algorithm_status_t backoff_algorithm_get_next_delay(backoff_algorithm_ctx_t *ctx, uint16_t *next_backoff);

#ifdef __cplusplus
    }
#endif

#endif /* ifndef BACKOFF_ALGORITHM_H_ */
