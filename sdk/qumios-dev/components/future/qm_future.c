#include "qm_future.h"
#include "qm_kernel.h"
#include "qm_types.h"
#include "qm_log.h"
#include "qm_errno.h"

typedef struct {
    int ready_can_be_called;
    qm_sem_t sem;             
    void *result;
    void *arg;
}qm_future_t;

void *qm_future_new(void *arg)
{
    qm_err_t ret = QM_EOK;
    qm_future_t *future = (qm_future_t*)qm_malloc(sizeof(qm_future_t));
    if (future == NULL) {
        goto __exit;
    }
    memset(future, 0, sizeof(qm_future_t));

    ret = qm_sem_new(&future->sem, 0);
    if(ret != QM_EOK){
        goto __exit;
    }
    future->arg = arg;
    future->ready_can_be_called = 1;
    return (void*)future;

__exit:
    if(future){
        qm_free(future);
    }
    return NULL;
}

void *qm_future_new_immediate(void *value, void *arg)
{
    qm_future_t *future = (qm_future_t*)qm_malloc(sizeof(qm_future_t));
    if (future == NULL) {
        return NULL;
    }
    memset(future, 0, sizeof(qm_future_t));

    future->result = value;
    future->arg = arg;
    future->ready_can_be_called = 0;
    return (void*)future;
}

int qm_future_arg_get(void *future, void **arg)
{   
    qm_future_t *lfuture = (qm_future_t*)future;
    if(future == NULL || arg == NULL){
        return -QM_EINVAL;
    }
    *arg = lfuture->arg;
    return QM_EOK;
}

int qm_future_ready(void *future, void *value)
{
    qm_future_t *lfuture = (qm_future_t*)future;

    if(future == NULL){
        return -QM_EINVAL;
    }
    if(!lfuture->ready_can_be_called){
        return -QM_EBUSY;
    }

    lfuture->ready_can_be_called = 0;
    lfuture->result = value;
    qm_sem_signal(&lfuture->sem);
    return QM_EOK;
}

int qm_future_wait(void *future, void **value, uint32_t timeout)
{
    qm_err_t ret = QM_EOK;
    qm_future_t *lfuture = (qm_future_t*)future;
    if(future == NULL|| value == NULL){
        return -QM_EINVAL;
    }

    // If the future is immediate, it will not have a semaphore
    if (qm_sem_is_valid(&lfuture->sem)) {
        ret = qm_sem_wait(&lfuture->sem, timeout);
    }
    *value = lfuture->result;
    qm_future_free(lfuture);
    return ret;
}

int qm_future_free(void *future)
{
    qm_future_t *lfuture = (qm_future_t*)future;
    if(future == NULL){
        return -QM_EINVAL;
    }

    if (qm_sem_is_valid(&lfuture->sem)) {
        qm_sem_free(&lfuture->sem);
    }
    qm_free(lfuture);
    return QM_EOK;
}
