
#ifndef _QM_NOOS_H_
#define _QM_NOOS_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_config.h"
#include "qm_types.h"

#if CONFIG_QM_NOOS_SUPPORT


#define QM_NOOS_QUEUE_TYPE_BASE         0
#define QM_NOOS_QUEUE_TYPE_SEMAPHORE    1

int qm_noos_init(void);
int qm_noos_generic_queue_new(void **queue, uint8_t type, uint32_t queue_len, uint32_t queue_size);
int qm_noos_generic_queue_free(void *queue);
int qm_noos_generic_queue_send(void *queue, void *msg, uint32_t size);
int qm_noos_generic_queue_recv(void *queue, void *msg, uint32_t *size);
int qm_noos_generic_queue_is_valid(void *queue);
bool_t qm_noos_generic_queue_is_empty(void *queue);

int qm_noos_task_new(void **task, const char *name, void (*fn)(void *), void *arg, int prio);
int qm_noos_task_exit(void *task);


#endif

#ifdef __cplusplus
}
#endif

#endif /* QM_NOOS_H */

