#ifndef QM_KERNEL_H
#define QM_KERNEL_H


#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_config.h"

#if CONFIG_QM_MTRACE_SUPPORT
#include "mtrace.h"
#endif

#define QM_WAIT_FOREVER    0xffffffffu
#define QM_NO_WAIT         0x0

#define QM_EVENT_OR               0x00u
#define QM_EVENT_OR_CLEAR         0x01u
#define QM_EVENT_AND              0x02u
#define QM_EVENT_AND_CLEAR        0x03u


typedef struct {
    void *hdl;
} qm_hdl_t;

typedef qm_hdl_t qm_task_t;
typedef qm_hdl_t qm_mutex_t;
typedef qm_hdl_t qm_sem_t;
typedef qm_hdl_t qm_event_t;
typedef qm_hdl_t qm_queue_t;
typedef qm_hdl_t qm_timer_t;


/**
 * Defines the prototype to which timer callback functions must conform.
 */
typedef void (*qm_timer_cb_t)( qm_timer_t *timer, void *arg);


/**
 * Reboot QMOS.
 */
void qm_reboot(void);

/**
 * Get kernel version.
 *
 * @return  SYSINFO_KERNEL_VERSION.
 */
const char *qm_version_get(void);

#if CONFIG_QM_OS_TASK_SUPPORT
/**
 * Create a task.
 *
 * @param[in]  task        handle.
 * @param[in]  name        task name.
 * @param[in]  fn          task function.
 * @param[in]  arg         argument of the function..
 * @param[in]  stack_buf   stack-buf: if stack_buf==NULL, provided by kernel.
 * @param[in]  stack_size  stack-size in bytes.
 * @param[in]  prio        priority value, the max is RHINO_CONFIG_USER_PRI_MAX(default 60).
 *
 * @return  0: success.
 */
int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg, int stack_size, int prio);


/**
 * Create a task.
 *
 * @param[in]  task        handle.
 * @param[in]  name        task name.
 * @param[in]  fn          task function.
 * @param[in]  arg         argument of the function..
 * @param[in]  stack_buf   stack-buf: if stack_buf==NULL, provided by kernel.
 * @param[in]  stack_size  stack-size in bytes.
 * @param[in]  prio        priority value, the max is RHINO_CONFIG_USER_PRI_MAX(default 60).
 * @param[in]  core_id     The core to which the task is pinned to, or tskNO_AFFINITY if the task has no core affinity.
 *
 * @return  0: success.
 */
int qm_task_new_to_core(qm_task_t *task, const char *name, void (*fn)(void *), void *arg, int stack_size, int prio, int core_id);

/**
 * Exit a task.
 *
 * @param[in]  task  handle.
 * 
 * @return  0: success.
 */
int qm_task_exit(qm_task_t *task);

/**
 * @brief Test whether a live task handle identifies the calling task.
 * @param[in] task Borrowed handle; creation/deletion must not race this call.
 * @return QM_TRUE for the current task, otherwise QM_FALSE, including NULL.
 */
qm_bool qm_task_is_current(const qm_task_t *task);

#endif

#if CONFIG_QM_OS_MUTEX_SUPPORT
/**
 * Alloc a mutex.
 *
 * @param[in]  mutex  pointer of mutex object, mutex object must be alloced,
 *                    hdl pointer in qm_mutex_t will refer a kernel obj internally.
 *
 * @return  0: success.
 */
int qm_mutex_new(qm_mutex_t *mutex);

/**
 * Free a mutex.
 *
 * @param[in]  mutex  mutex object, mem refered by hdl pointer in qm_mutex_t will
 *                    be freed internally.
 * @return  0: success.
 */
int qm_mutex_free(qm_mutex_t *mutex);

/**
 * Lock a mutex.
 *
 * @param[in]  mutex    mutex object, it contains kernel obj pointer which qm_mutex_new alloced.
 * @param[in]  timeout  waiting until timeout in milliseconds.
 *
 * @return  0: success.
 */
int qm_mutex_lock(qm_mutex_t *mutex, unsigned int timeout);

/**
 * Unlock a mutex.
 *
 * @param[in]  mutex  mutex object, it contains kernel obj pointer which oc_mutex_new alloced.
 *
 * @return  0: success.
 */
int qm_mutex_unlock(qm_mutex_t *mutex);

/**
 * This function will check if mutex is valid.
 *
 * @param[in]  mutex  pointer to the mutex.
 *
 * @return  1: valid, 0: invalid. / 1: 有效，0: 无效。
 */
int qm_mutex_is_valid(qm_mutex_t *mutex);

#endif

#if CONFIG_QM_OS_SEM_SUPPORT
/**
 * Alloc a semaphore.
 *
 * @param[out]  sem    pointer of semaphore object, semaphore object must be alloced,
 *                     hdl pointer in qm_sem_t will refer a kernel obj internally.
 * @param[in]   count  the init count of the semaphore
 *
 * @return  0:success.
 */
int qm_sem_new(qm_sem_t *sem, int count);

/**
 * Destroy a semaphore.
 *
 * @param[in]  sem  pointer of semaphore object, mem refered by hdl pointer
 *                  in qm_sem_t will be freed internally.
 * @return  0: success.
 */
int qm_sem_free(qm_sem_t *sem);

/**
 * Acquire a semaphore.
 *
 * @param[in]  sem      semaphore object, it contains kernel obj pointer which qm_sem_new alloced.
 * @param[in]  timeout  waiting until timeout in milliseconds.
 *
 * @return  0: success.
 */
int qm_sem_wait(qm_sem_t *sem, unsigned int timeout);

/**
 * Release a semaphore.
 *
 * @param[in]  sem  semaphore object, it contains kernel obj pointer which qm_sem_new alloced.
 * 
 * @return  0: success.
 */
int qm_sem_signal(qm_sem_t *sem);

/**
 * Release a semaphore from interrupt.
 *
 * @param[in]  sem  semaphore object, it contains kernel obj pointer which qm_sem_new alloced.
 * 
 * @return  0: success.
 */
int qm_sem_signal_from_isr(qm_sem_t *sem);

/**
 * This function will check if semaphore is valid.
 *
 * @param[in]  sem  pointer to the semaphore.
 * 
 * @return  1: valid, 0: invalid. / 1: 有效，0: 无效。
 */
int qm_sem_is_valid(qm_sem_t *sem);

#endif

#if CONFIG_QM_OS_EVENT_SUPPORT

/**
 * This function will create an event with an initialization flag set.
 * This function should not be called from interrupt context.
 *
 * @param[in]  event    event object pointer.
 *
 * @return  0: success.
 */

int qm_event_new(qm_event_t *event);

/**
 * This function will free an event.
 * This function shoud not be called from interrupt context.
 *
 * @param[in]  event    memory refered by hdl pointer in event will be freed.
 *
 * @return  0: success.
 */

int qm_event_free(qm_event_t *event);

/**
 * This function checks whether an event object is valid. / 检查事件对象是否有效。
 *
 * @param[in]  event  pointer to the event.
 *
 * @return  1: valid, 0: invalid. / 1: 有效，0: 无效。
 */
int qm_event_is_valid(qm_event_t *event);

/**
 * This function will try to get flag set from given event, if the request flag
 * set is satisfied, it will return immediately, if the request flag set is not
 * satisfied with timeout(RHINO_WAIT_FOREVER,0xFFFFFFFF), the caller task will be
 * pended on event until the flag is satisfied, if the request flag is not 
 * satisfied with timeout(RHINO_NO_WAIT, 0x0), it will also return immediately.
 * Note, this function should not be called from interrupt context because it has
 * possible to lead context switch and an interrupt has no TCB to save context.
 *
 * @param[in]  event        event object pointer.
 * @param[in]  wait_event_bits        request flag set.
 * @param[in]  opt          operation type, such as AND,OR,AND_CLEAR,OR_CLEAR.
 * @param[out] actl_event_bits   the internal flags value hold by event.
 * @param[in]  timeout      max wait time in millisecond.
 *
 * @return  0: success; -QM_EINVAL for an invalid event or NULL output pointer.
 *          / 0: 成功；事件无效或输出指针为 NULL 时返回 -QM_EINVAL。
 */

int qm_event_get(qm_event_t *event, unsigned int wait_event_bits, unsigned char opt,
                       unsigned int *actl_event_bits, unsigned int timeout);

/**
* This function will set flag set to given event, and it will check if any task
* which is pending on the event should be waken up. 
*
* @param[in]  event    event object pointer.
* @param[in]  flags    flag set to be set into event.
* @param[in]  opt      operation type, such as AND,OR.
*
* @return  0: success.
*/

int qm_event_set(qm_event_t *event, unsigned int event_bits);

/**
* 
*
* @param[in]  event    event object pointer.
* @param[in]  flags    flag set to be set into event.
* @param[in]  opt      operation type, such as AND,OR.
*
* @return  0: success.
*/

int qm_event_clear(qm_event_t *event, unsigned int event_bits);

#endif

#if CONFIG_QM_OS_QUEUE_SUPPORT

/**
 * This function will create a queue.
 *
 * @param[in]  queue    pointer to the queue(the space is provided by user).
 * @param[in]  queue_len      queue length
 * @param[in]  queue_size     Size of each message in the message queue
 * 
 * @return  0: success.
 */

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size);

/**
 * This function will delete a queue.
 *
 * @param[in]  queue  pointer to the queue.
 * @return  0: success.
 */
int qm_queue_free(qm_queue_t *queue);

/**
 * This function will send a msg to the front of a queue.
 *
 * @param[in]  queue  pointer to the queue.
 * @param[in]  msg    msg to send.
 * @param[in]  size   size of the msg.
 *
 * @return  0: success.
 */
int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size);

/**
 * This function will send a msg to the front of a queue from interrupt.
 *
 * @param[in]  queue  pointer to the queue.
 * @param[in]  msg    msg to send.
 * @param[in]  size   size of the msg.
 *
 * @return  0: success.
 */
int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size);

/**
 * This function will receive msg from a queue.
 *
 * @param[in]   queue  pointer to the queue.
 * @param[out]  msg    buf to save msg.
 * @param[out]  size   size of the msg.
 * @param[in]   ms     ms to wait before receive.
 *
 * @return  0: success.
 */
int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms);

/**
 * This function will check if queue is valid.
 *
 * @param[in]  queue  pointer to the queue.
 *
 * @return  1: valid, 0: invalid. / 1: 有效，0: 无效。
 */
int qm_queue_is_valid(qm_queue_t *queue);

/**
 * This function will check if queue is empty.
 *
 * @param[in]  queue  pointer to the queue.
 *
 * @return  0: empty, -1: not empty.
 */
bool_t qm_queue_is_empty(qm_queue_t *queue);

#endif

#if CONFIG_QM_OS_TIMER_SUPPORT
/**
 * This function will create a timer.
 *
 * @param[in]  timer   pointer to the timer.
 * @param[in]  cb      callbak of the timer.
 * @param[in]  arg     the argument of the callback.
 * @param[in]  ms      ms of the normal timer triger.
 * @param[in]  repeat  repeat or not when the timer is created.
 *
 * @return  0: success.
 */
int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb,
                  void *arg, int ms, int repeat);


/**
 * This function will check if timer is valid.
 *
 * @param[in]  timer  pointer to the timer.
 * 
 * @return  1: valid, 0: invalid. / 1: 有效，0: 无效。
 */
int qm_timer_is_valid(qm_timer_t *timer);

/**
 * This function will create a timer.
 *
 * @param[in]  timer   pointer to the timer.
 * @param[in]  cb      callbak of the timer.
 * @param[in]  arg     the argument of the callback.
 * @param[in]  ms      ms of the normal timer triger.
 * @param[in]  repeat  repeat or not when the timer is created.
 * @param[in]  auto_run  run auto or not when the timer is created.
 *
 * @return  0: success.
 */
int qm_timer_new_ext(qm_timer_t *timer, qm_timer_cb_t cb,
                  void *arg, int ms, int repeat, unsigned char auto_run);

/**
 * This function will delete a timer.
 *
 * @param[in]  timer  pointer to a timer.
 *  
 * @return  0: success.
 */
int qm_timer_free(qm_timer_t *timer);

/**
 * This function will start a timer.
 *
 * @param[in]  timer  pointer to the timer.
 *
 * @return  0: success.
 */
int qm_timer_start(qm_timer_t *timer);

/**
 * This function will start a timer from ISR.
 *
 * @param[in]  timer  pointer to the timer.
 *
 * @return  0: success.
 */
int qm_timer_start_from_isr(qm_timer_t *timer);

/**
 * This function will stop a timer.
 *
 * @param[in]  timer  pointer to the timer.
 *
 * @return  0: success.
 */
int qm_timer_stop(qm_timer_t *timer);

/**
 * This function will change attributes of a timer.
 *
 * @param[in]  timer  pointer to the timer.
 * @param[in]  ms     ms of the timer triger.
 *
 * @return  0: success.
 */
int qm_timer_change(qm_timer_t *timer, int ms);

#endif


/**
 * Alloc memory and Set the allocated memory to zero.
 *
 * @param[in]  nitems The number of elements to be assigned.
 * 
 * @param[in]  size  size of the mem to malloc.
 *
 * @return  NULL: error.
 */
void* qm_calloc(unsigned int nitems, unsigned int size);

/**
 * Allocating aligned memory blocks.
 *
 * @param[in]  alignment Specifies the alignment. The value of is a power of two. alignment.
 * 
 * @param[in]  size  Number of bytes to allocate. The value of is an integral multiple of alignment.
 *
 * @return  NULL: error.
 */
void* qm_aligned_alloc(unsigned int alignment, unsigned int size);

/**
 * Alloc memory.
 *
 * @param[in]  size  size of the mem to malloc.
 *
 * @return  NULL: error.
 */
#if CONFIG_QM_MTRACE_SUPPORT
#define qm_malloc(size) \
    ({void *ptr = malloc(size); \
    allocate_memory(size, ptr, __LINE__, __FILE__);\
    qm_printf("malloc ptr: %p, line: %d, file: %s\n",ptr, __LINE__, __FILE__);\
    ptr;})
#else
void *qm_malloc(unsigned int size);

#endif

/**
 * Realloc memory.
 *
 * @param[in]  mem   current memory address point.
 * @param[in]  size  new size of the mem to remalloc.
 *
 * @return  NULL: error.
 */
#if CONFIG_QM_MTRACE_SUPPORT
#define qm_realloc(mem, size) \
    ({void *ptr = realloc(mem, size); \
    if(mem != ptr){deallocate_memory(mem, __LINE__, __FILE__);\
    allocate_memory(size, ptr, __LINE__, __FILE__);}\
    ptr;})
#else
void *qm_realloc(void *mem, unsigned int size);
#endif

/**
 * Alloc memory from PSRAM.
 *
 * @param[in]  size  size of the mem to malloc.
 *
 * @return  NULL: error or PSRAM unsupported.
 */
void *qm_psram_malloc(unsigned int size);

/**
 * Alloc memory from PSRAM and Set the allocated memory to zero.
 *
 * @param[in]  nitems The number of elements to be assigned.
 * @param[in]  size   size of the mem to malloc.
 *
 * @return  NULL: error or PSRAM unsupported.
 */
void *qm_psram_calloc(unsigned int nitems, unsigned int size);

/**
 * Realloc PSRAM memory.
 *
 * @param[in]  mem   current memory address point.
 * @param[in]  size  new size of the mem to remalloc.
 *
 * @return  NULL: error or PSRAM unsupported.
 */
void *qm_psram_realloc(void *mem, unsigned int size);

/**
 * Free memory.
 *
 * @param[in]  ptr  address point of the mem.
 */
#if CONFIG_QM_MTRACE_SUPPORT
#define qm_free(mem) \
    ({deallocate_memory(mem, __LINE__, __FILE__);\
    qm_printf("free ptr: %p, line: %d, file: %s\n",mem, __LINE__, __FILE__);\
    free(mem);})
#else
void qm_free(void *mem);
#endif

/**
 * get free memory size.
 *
 * @return  memory size
 * */
uint32_t qm_free_mem_get(void);

/**
 * Get current time in mini seconds.
 *
 * @return  elapsed time in mini seconds from system starting.
 */
uint32_t qm_now_ms(void);

/**
 * Msleep.
 *
 * @param[in]  ms  sleep time in milliseconds.
 * 
 * @return  0: success.
 */
int qm_msleep(int ms);

/**
 * Usleep.
 *
 * @param[in]  ms  sleep time in microsecond.
 * 
 * @return  0: success.
 */
int qm_usleep(int us);

/**
 * @brief Writes formatted data to stream.
 *
 * @param [in] fmt: @n String that contains the text to be written, it can optionally contain embedded format specifiers
     that specifies how subsequent arguments are converted for output.
 * @param [in] ...: @n the variable argument list, for formatted and inserted in the resulting string replacing their respective specifiers.

 * @return  0: success.
 */

int qm_printf(const char * fmt, ...);

/**
 * @brief Writes formatted data to string.
 *
 * @param [out] str: @n String that holds written text.
 * @param [in] len: @n Maximum length of character will be written
 * @param [in] fmt: @n Format that contains the text to be written, it can optionally contain embedded format specifiers
     that specifies how subsequent arguments are converted for output.
 * @param [in] ...: @n the variable argument list, for formatted and inserted in the resulting string replacing their respective specifiers.
 * @return bytes of character successfully written into string.
 * @see None.
 * @note None.
 */
int qm_snprintf(char *str, const int len, const char *fmt, ...);

/**
 * @brief Set seed for a sequence of pseudo-random integers, which will be returned by qm_random_get()
 *
 * @param [in] seed @n A start point for the random number sequence
 * @return None.
 * @see None.
 * @note None.
 */
void qm_srandom(uint32_t seed);

/**
 * @brief Get a random integer
 *
 * @param [in] region @n Range of generated random numbers
 * @return Random number
 * @see None.
 * @note None.
 */
uint32_t qm_random_get(uint32_t region);

/**
 * Initialize system
 */
void qm_init(void);


#ifdef __cplusplus
}
#endif

#endif /* QM_KERNEL_H */
