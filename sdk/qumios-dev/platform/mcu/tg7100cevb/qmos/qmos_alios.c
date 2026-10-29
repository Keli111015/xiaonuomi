#include <qmos/qm.h>
#include <aos/aos.h>

void qm_reboot(void)
{
    aos_reboot();
}

const char *qm_version_get(void)
{
    return "WB40.C000.3208.01";
}

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                     int stack_size, int prio)
{
    int ret = 0;
    if(task == NULL){
        return -QM_EINVAL;
    }
    ret  = aos_task_new(name, fn, arg, stack_size);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_task_exit(qm_task_t *task)
{
    //code not use now
    aos_task_exit(0);
    return QM_EOK;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    int ret = 0;
    if(mutex == NULL){
        return -QM_EINVAL;
    }
    ret = aos_mutex_new((aos_mutex_t*)mutex);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }
    aos_mutex_free((aos_mutex_t*)mutex);
    return QM_EOK;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    int ret = 0;
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }
    ret = aos_mutex_lock((aos_mutex_t*)mutex, ms);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;
} 

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    int ret = 0;
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }
    ret = aos_mutex_unlock((aos_mutex_t*)mutex);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return mutex && mutex->hdl != NULL;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    int ret = 0;
    if(sem == NULL){
        return -QM_EINVAL;
    }
    ret = aos_sem_new((aos_sem_t*)sem, count);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_sem_free(qm_sem_t *sem)
{
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }
    aos_sem_free((aos_sem_t*)sem);
    return QM_EOK;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    int ret = 0;
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }

    ret = aos_sem_wait((aos_sem_t*)sem, ms);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_sem_signal(qm_sem_t *sem)
{
    int ret = 0;
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }

    aos_sem_signal((aos_sem_t*)sem);
    return QM_EOK;
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return sem && sem->hdl != NULL;
}

int qm_event_new(qm_event_t *event)
{
    return 0;
}

int qm_event_free(qm_event_t *event)
{
    return 0;
}

int qm_event_is_valid(qm_event_t *event)
{
    return (event != NULL && event->hdl != NULL) ? 1 : 0;
}

int qm_event_get(qm_event_t *event, unsigned int wait_event_bits, unsigned char opt, unsigned int *actl_event_bits, unsigned int timeout)
{
    if (!qm_event_is_valid(event) || actl_event_bits == NULL) {
        return -QM_EINVAL;
    }

    return 0;
}

int qm_event_set(qm_event_t *event, unsigned int event_bits)
{
    return 0;
}

typedef struct{
    aos_queue_t queue;
    uint8_t buff[1];
}queue_handle_t;

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    int ret = 0;
    uint32_t size = 0;
    queue_handle_t *queue_handle = NULL;
    if(queue == NULL || queue_len == 0 || queue_size == 0){
        return -QM_EINVAL;
    }

    size = queue_len * queue_size;

    queue_handle = (queue_handle_t*)qm_malloc(sizeof(queue_handle_t)-1 + size);
    if(queue_handle == NULL){
        return -QM_ENOMEM;
    }

    ret = aos_queue_new((aos_queue_t*)queue, queue_handle->buff, size, queue_size);
    if(ret != 0){
        qm_free(queue_handle);
        queue_handle = NULL;
        return -QM_ERROR;
    }

    queue->hdl = (void*)queue_handle;
    return QM_EOK;
}

int qm_queue_free(qm_queue_t *queue)
{
    queue_handle_t *queue_handle = NULL;
    if(queue == NULL || queue->hdl == NULL){
        return -QM_EINVAL;
    }

    queue_handle = (queue_handle_t*)queue->hdl;

    aos_queue_free(&queue_handle->queue);

    qm_free(queue_handle);
    queue_handle = NULL;

    return QM_EOK;
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    int ret = 0;
    queue_handle_t *queue_handle = NULL;
    if(queue == NULL || queue->hdl == NULL){
        return -QM_EINVAL;
    }

    queue_handle = (queue_handle_t*)queue->hdl;

    ret = aos_queue_send(&queue_handle->queue, msg, size);
    if(ret != 0){
        return -QM_ERROR;
    }

    return QM_EOK;
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    int ret = 0;
    queue_handle_t *queue_handle = NULL;
    if(queue == NULL || queue->hdl == NULL){
        return -QM_EINVAL;
    }

    queue_handle = (queue_handle_t*)queue->hdl;

    ret = aos_queue_recv(&queue_handle->queue, ms, msg, size);
    if(ret != 0){
        return -QM_ERROR;
    }

    return QM_EOK;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return queue && queue->hdl != NULL;
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb,
                  void *arg, int ms, int repeat)
{
    int ret = 0;
    if(timer == NULL || cb == NULL || ms == 0){
        return -QM_EINVAL;
    }
    ret = aos_timer_new_ext((aos_timer_t*)timer, cb, arg, ms, repeat, 0);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;;
}

int qm_timer_free(qm_timer_t *timer)
{
    int ret = 0;
    if(timer == NULL || timer ->hdl == NULL){
        return -QM_EINVAL;
    }
    aos_timer_free((aos_timer_t*)timer);
    return QM_EOK;;
}

int qm_timer_start(qm_timer_t *timer)
{
    int ret = 0;
    if(timer == NULL || timer ->hdl == NULL){
        return -QM_EINVAL;
    }
    ret = aos_timer_start((aos_timer_t*)timer);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;;
}

int qm_timer_stop(qm_timer_t *timer)
{
    int ret = 0;
    if(timer == NULL || timer ->hdl == NULL){
        return -QM_EINVAL;
    }
    ret = aos_timer_stop((aos_timer_t*)timer);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;;
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    int ret = 0;
    if(timer == NULL || timer ->hdl == NULL || ms == 0){
        return -QM_EINVAL;
    }
    ret = aos_timer_change((aos_timer_t*)timer, ms);
    if(ret != 0){
        return -QM_ERROR;
    }
    return QM_EOK;;
}

#if !CONFIG_QM_MTRACE

void *qm_zalloc(unsigned int size)
{
    return calloc(size, 1);
}

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void *qm_realloc(void *mem, unsigned int size)
{
    return realloc(mem, size);
}

void qm_free(void *mem)
{
    free(mem);
}

#endif

uint32_t qm_now_ms(void)
{
    uint64_t ms = aos_now_ms();
    return (uint32_t)(ms%0xFFFFFFFF);
}

uint32_t qm_free_mem_get(void)
{
    uint64_t ms = aos_now_ms();
    return (uint32_t)(ms%0xFFFFFFFF);
}

int qm_msleep(int ms)
{
    aos_msleep(ms);
    return QM_EOK;
}

int qm_printf(const char * fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);

    fflush(stdout);
}

int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int     rc;
    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);

    return rc;
}

static uint32_t next = 0;  
  
/* RAND_MAX assumed to be 32767 */  
static uint32_t hal_rand(void) {  
    next = next * 1103515245 + 12345;  
    return((uint32_t)(next/65536) % 32768);  
}  
  
void qm_srandom(uint32_t seed)
{
    next = seed; 
}

uint32_t qm_random_get(uint32_t region)
{
    return (region > 0) ? (hal_rand() % region) : 0;
}

void qm_init(void)
{
    
}


