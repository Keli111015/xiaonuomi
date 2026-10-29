#include "backoff_algorithm.h"
#include "qm_kernel.h"

backoff_algorithm_status_t backoff_algorithm_get_next_delay(backoff_algorithm_ctx_t *ctx, uint16_t *next_backoff)
{
    backoff_algorithm_status_t status = BACKOFF_ALGORITHM_SUCCESS;

    /* If maxRetryAttempts state of the context is set to the maximum, retry forever. */
    if( ( ctx->max_attempts == BACKOFF_ALGORITHM_RETRY_FOREVER ) ||
        ( ctx->attempts_num < ctx->max_attempts ) )
    {
        /* The next backoff value is a random value between 0 and the maximum jitter value
         * for the retry attempt. */
        qm_srandom((uint32_t)qm_now_ms());

        /* Choose a random value for back-off time between 0 and the max jitter value. */
        *next_backoff = ( uint16_t ) ( qm_random_get(UINT32_MAX) % ( ctx->next_backoff + ( uint32_t ) 1U ) );

        /* Increment the retry attempt. */
        ctx->attempts_num++;

        /* Double the max jitter value for the next retry attempt, only
         * if the new value will be less than the max backoff time value. */
        if( ctx->next_backoff < ( ctx->max_backoff / 2U ) )
        {
            ctx->next_backoff += ctx->next_backoff;
        }
        else
        {
            ctx->next_backoff = ctx->max_backoff;
        }
    }
    else
    {
        /* When max retry attempts are exhausted, let application know by
         * returning BackoffAlgorithmRetriesExhausted. Application may choose to
         * restart the retry process after calling BackoffAlgorithm_InitializeParams(). */
        status = BACKOFF_ALGORITHM_RETRY_EXHAUSTED;
    }

    return status;
}

/*-----------------------------------------------------------*/
void backoff_algorithm_init(backoff_algorithm_ctx_t *ctx, uint16_t backoff_base, uint16_t max_backoff, uint32_t max_attempts)
{

    /* Initialize the context with parameters used in calculating the backoff
     * value for the next retry attempt. */
    ctx->next_backoff = backoff_base;
    ctx->max_backoff = max_backoff;
    ctx->max_attempts = max_attempts;

    /* The total number of retry attempts is zero at initialization. */
    ctx->attempts_num = 0;
}
