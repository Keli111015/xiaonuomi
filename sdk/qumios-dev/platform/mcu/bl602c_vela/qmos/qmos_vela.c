
#define _GNU_SOURCE

#include <qm.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <fcntl.h>           /* For O_* constants */
#include <sys/stat.h>        /* For mode constants */
#include <mqueue.h>
#include <errno.h>
#include <string.h>
#include <limits.h>
#include <malloc.h>
#include <ctype.h>

#include "arch_os.h"
#include "arch_dbg.h"
#include "miio_util.h"

#include <sys/time.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <dirent.h>

#include <nuttx/mqueue.h>

#define MIIO_QUEUE_NAME_PATH  "/xj_queue"
#define QUEUE_MAX_NAMESIZE    32
#define QUEUE_MAX_NUM         32

#ifdef CONFIG_BOARDCTL_RESET

#include <sys/boardctl.h>

void qm_reboot(void)
{
  boardctl(BOARDIOC_RESET, EXIT_SUCCESS);
}
#else
void qm_reboot(void)
{
  system("reboot");
}
#endif

const char *qm_version_get(void)
{
    return "V1.0.0";
}

typedef struct{
    pthread_t pthread;
    void (*fn)(void *);
    void *arg;
}task_handle_t;

static void* fuc_thread(void* arg)
{
    task_handle_t *task_handle = (task_handle_t*)arg;
    task_handle->fn(task_handle->arg);
    return NULL;
}


int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                     int stack_size, int prio)
{
    int ret = QM_EOK;
    pthread_attr_t attr = {0};
    struct sched_param param = {0};
    task_handle_t *task_handle = NULL;
    if(task == NULL || name == NULL || fn == NULL){
        return -QM_EINVAL;
    }

    task_handle = (task_handle_t *)qm_malloc(sizeof(task_handle_t));
    if(task_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(task_handle, 0, sizeof(task_handle_t));

    ret = pthread_attr_init(&attr);
    if (ret < 0)
        return MIIO_ERROR;

    if (stack_size > PTHREAD_STACK_MIN) {
        ret = pthread_attr_setstacksize(&attr, stack_size);
        if (ret < 0)
        goto bail;
    }

    ret = pthread_attr_getschedparam(&attr, &param);
    if (ret < 0)
        goto bail;

    param.sched_priority = prio;

    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret < 0)
        goto bail;

    task_handle->fn = fn;
    task_handle->arg = arg;

    ret = pthread_create(&task_handle->pthread, &attr, fuc_thread, task_handle);
    if (ret == 0) {
        pthread_setname_np(task_handle->pthread, name);
        pthread_detach(task_handle->pthread);
    }

bail:
    if(ret != 0){
        qm_free(task_handle);
        task_handle = NULL;
    }

    pthread_attr_destroy(&attr);
    task->hdl = (void*)task_handle;

    return ret;
}

int qm_task_exit(qm_task_t *task)
{
    int ret = QM_EOK;

    if(task == NULL){
        pthread_exit(0);
    }else{
        task_handle_t *task_handle = (task_handle_t*)task->hdl;
        if(task_handle == NULL){
            return -QM_EINVAL;
        }
        ret = pthread_cancel(task_handle->pthread);
        if(ret !=0){
            return -QM_ERROR;
        }

        qm_free(task_handle);
        task->hdl = NULL;
    }

    return ret;
}


// pthread_mutex_t pthread_mutex;
typedef struct{
    pthread_mutex_t pthread_mutex;
}mutex_handle_t;

int qm_mutex_new(qm_mutex_t *mutex)
{
    int ret = QM_EOK;
    pthread_mutexattr_t attr = {0};
    mutex_handle_t *pthread_mutex_handle = NULL;
    if(mutex == NULL){
        return -QM_EINVAL;
    }

    pthread_mutex_handle  = (mutex_handle_t*)qm_malloc(sizeof(mutex_handle_t));
    if(pthread_mutex_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(pthread_mutex_handle, 0, sizeof(mutex_handle_t));

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    ret = pthread_mutex_init(&pthread_mutex_handle->pthread_mutex, &attr);
    if (ret != 0){
        ret = -QM_ERROR;
        goto bail;
    }
    mutex->hdl = (void *)pthread_mutex_handle;
    
bail:
    if(ret != QM_EOK){
        qm_free(pthread_mutex_handle);
        pthread_mutex_handle = NULL;
    }
    pthread_mutexattr_destroy(&attr);
    return ret;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    int ret = QM_EOK;
    mutex_handle_t *pthread_mutex_handle = NULL;
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }

    pthread_mutex_handle = (mutex_handle_t*)mutex->hdl;

    ret = pthread_mutex_destroy(&pthread_mutex_handle->pthread_mutex);

    qm_free(pthread_mutex_handle);
    mutex->hdl = NULL;
    return ret;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    struct timespec abstime = {0};
    mutex_handle_t *pthread_mutex_handle = NULL;
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }

    pthread_mutex_handle = (mutex_handle_t*)mutex->hdl;

    if (ms == QM_WAIT_FOREVER){
        return pthread_mutex_lock(&pthread_mutex_handle->pthread_mutex);
    }

    clock_gettime(CLOCK_REALTIME, &abstime);
    abstime.tv_sec += ms / 1000;
    abstime.tv_nsec += (ms % 1000) * 1000 * 1000;
    if (abstime.tv_nsec >= (1000 * 1000000)) {
        abstime.tv_sec += 1;
        abstime.tv_nsec -= (1000 * 1000000);
    }

    return pthread_mutex_timedlock(&pthread_mutex_handle->pthread_mutex, &abstime);
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    mutex_handle_t *pthread_mutex_handle = NULL;
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }

    pthread_mutex_handle = (mutex_handle_t*)mutex->hdl;

    return pthread_mutex_unlock(&pthread_mutex_handle->pthread_mutex);
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return (mutex && mutex->hdl != NULL);
}

typedef struct{
    sem_t sema;
}sem_handle_t;

int qm_sem_new(qm_sem_t *sem, int count)
{
    int ret = QM_EOK;
    sem_handle_t *sem_handle = NULL;

    if(sem == NULL){
        return -QM_EINVAL;
    }

    sem_handle = (sem_handle_t*)qm_malloc(sizeof(sem_handle_t));
    if(sem_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(sem_handle, 0, sizeof(sem_handle_t));

    ret = sem_init(&sem_handle->sema, 0, count);
    if(ret != 0){
        goto __exit;
    }

    sem->hdl = (void *)sem_handle;
    return ret;

__exit:
    qm_free(sem_handle);
    sem_handle = NULL;
    return ret;
}

int qm_sem_free(qm_sem_t *sem)
{
    int ret = QM_EOK;
    sem_handle_t *sem_handle = NULL;
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }

    sem_handle = (sem_handle_t *)sem->hdl;

    sem_destroy(&sem_handle->sema);

__exit:
    qm_free(sem_handle);
    sem->hdl = NULL;
    return ret;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    struct timespec abstime = {0};
    sem_handle_t *sem_handle = NULL;
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }

    sem_handle = (sem_handle_t *)sem->hdl;

    if (ms == 0){
        ms = 1;
    }
            
    if (ms == QM_WAIT_FOREVER){
        return sem_wait(&sem_handle->sema);
    }

    clock_gettime(CLOCK_REALTIME, &abstime);

    abstime.tv_sec += ms / 1000;
    abstime.tv_nsec += (ms % 1000) * 1000 * 1000;
    if (abstime.tv_nsec >= (1000 * 1000000)) {
        abstime.tv_sec += 1;
        abstime.tv_nsec -= (1000 * 1000000);
    }

    return sem_timedwait(&sem_handle->sema, &abstime);
}

int qm_sem_signal(qm_sem_t *sem)
{
    sem_handle_t *sem_handle = NULL;
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }

    sem_handle = (sem_handle_t *)sem->hdl;

    return sem_post(&sem_handle->sema);
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return (sem && sem->hdl != NULL);
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

typedef struct {
  struct file mqdes;
  size_t msgsz;
}queue_handle_t;

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    qm_err_t ret = QM_EOK;
    struct mq_attr attr = {0};
    queue_handle_t *queue_handle = NULL;
    char msgname[QUEUE_MAX_NAMESIZE] = {0};

    if(queue == NULL || queue_len == 0 || queue_size == 0){
        return -QM_EINVAL;
    }

    queue_handle = (queue_handle_t*)qm_malloc(sizeof(queue_handle_t));
    if(queue_handle == NULL){
        return -QM_ENOMEM;
    }

    memset(queue_handle, 0, sizeof(queue_handle_t));

    snprintf(msgname, sizeof(msgname), "%s%d%d", MIIO_QUEUE_NAME_PATH, getpid(), (intptr_t)queue_handle);

    attr.mq_maxmsg = queue_len;
    attr.mq_msgsize = queue_size;

    ret = file_mq_open(&queue_handle->mqdes, msgname, O_RDWR|O_CREAT, 0600, &attr);
    if(ret < 0){
        ret = -QM_ERROR;
        goto __exit;
    }

    file_mq_unlink(msgname);
    queue_handle->msgsz = queue_size;

    queue->hdl = (void *)queue_handle;
    return ret;

__exit:
    qm_free(queue_handle);
    queue_handle = NULL;

    return ret;
}

int qm_queue_free(qm_queue_t *queue)
{
    queue_handle_t *queue_handle = NULL;
    
    if(queue == NULL || queue->hdl == NULL){
        return -QM_EINVAL;
    }
    queue_handle = (queue_handle_t *)queue->hdl;

    file_mq_close(&queue_handle->mqdes);

    qm_free(queue_handle);
    queue->hdl = NULL;

    return QM_EOK;
}

//node+msg
int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    int ret = QM_EOK;
    queue_handle_t *queue_handle = NULL;
    if(queue == NULL || queue->hdl == NULL){
        return -QM_EINVAL;
    }

    queue_handle = (queue_handle_t *)queue->hdl;
    if(queue_handle->msgsz != size){
        return -QM_EINVAL;
    }

    ret = file_mq_send(&queue_handle->mqdes, msg, queue_handle->msgsz, 0);

    return (ret < 0) ? -QM_ERROR : QM_EOK;
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    qm_err_t ret = QM_EOK;
    struct timespec ts = {0};
    queue_handle_t *queue_handle = NULL;
    if(queue == NULL || queue->hdl == NULL ||  msg == NULL || size == NULL){
        return -QM_EINVAL;
    }

    queue_handle = (queue_handle_t *)queue->hdl;

    if (ms == ARCH_OS_WAIT_FOREVER){
        ret = file_mq_receive(&queue_handle->mqdes, msg, queue_handle->msgsz, NULL);
    }else{
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += ms / 1000;
        ts.tv_nsec += (ms % 1000) * 1000 * 1000;
        if (ts.tv_nsec >= (1000 * 1000000)) {
            ts.tv_sec += 1;
            ts.tv_nsec -= (1000 * 1000000);
        }
        ret = file_mq_timedreceive(&queue_handle->mqdes, msg, queue_handle->msgsz, NULL, &ts);
    }

    return (ret < 0) ? -QM_ETIMEOUT : QM_EOK;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return (queue && queue->hdl != NULL);
}

typedef struct{
  int                   repeat;
  timer_t                timer;
  int                       ms;
  void                    *arg;
  qm_timer_cb_t             cb;
  qm_timer_t         *qm_timer;
}qm_timer_handle_t;

static void timer_callback(union sigval value)
{
    qm_timer_handle_t *timer_handle = (qm_timer_handle_t *)value.sival_ptr;  
    if(timer_handle == NULL){
        return;
    }
    timer_handle->cb(timer_handle->qm_timer, timer_handle->arg);
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return (timer && timer->hdl != NULL);
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb, void *arg, int ms, int repeat)
{      
    int ret = QM_EOK;
    struct sigevent notify = {0};    
    qm_timer_handle_t *timer_handle = NULL;  
    if(timer == NULL || cb == NULL || ms == 0){
        return -QM_EINVAL;
    }           

    timer_handle = (qm_timer_handle_t *)qm_malloc(sizeof(qm_timer_handle_t));
    if(timer_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(timer_handle, 0, sizeof(qm_timer_handle_t));

    notify.sigev_notify            = SIGEV_THREAD;
    notify.sigev_value.sival_ptr   = timer_handle;
    notify.sigev_notify_function   = timer_callback;
    notify.sigev_notify_attributes = NULL;

    ret = timer_create(CLOCK_REALTIME, &notify, &timer_handle->timer);
    if (ret < 0){
        goto bail;
    }

    timer_handle->ms = ms;
    timer_handle->arg = arg;
    timer_handle->cb = cb;
    timer_handle->repeat = repeat;
    timer_handle->qm_timer = timer;

    timer->hdl = (void*)timer_handle;

bail:
    if (ret < 0){
        qm_free(timer_handle);
    }
        
    return ret;
}

int qm_timer_free(qm_timer_t *timer)
{
    int ret = QM_EOK;
    qm_timer_handle_t *timer_handle = NULL;  
    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;

    timer_delete(timer_handle->timer);

    qm_free(timer->hdl);
    timer->hdl = NULL;

    return QM_EOK;
}

int qm_timer_start(qm_timer_t *timer)
{
    struct itimerspec itp = {0};
    qm_timer_handle_t *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;

    itp.it_value.tv_sec = timer_handle->ms / 1000;
    itp.it_value.tv_nsec = (timer_handle->ms % 1000) * 1000 * 1000;

    if(timer_handle->repeat == QM_TRUE) {
        itp.it_interval.tv_sec = itp.it_value.tv_sec;
        itp.it_interval.tv_nsec = itp.it_value.tv_nsec;
    }

    return timer_settime(timer_handle->timer, 0, &itp, NULL);
}

int qm_timer_stop(qm_timer_t *timer)
{
    struct itimerspec itp = {0};
    qm_timer_handle_t *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;
    return timer_settime(timer_handle->timer, 0, &itp, NULL);
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    struct itimerspec itp = {0};
    qm_timer_handle_t *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;

    /* Start the timer */
    itp.it_value.tv_sec = ms / 1000;
    itp.it_value.tv_nsec = (ms % 1000) * 1000000;
 
    itp.it_interval.tv_sec = 0;
    itp.it_interval.tv_nsec = 0;

    return timer_settime(timer_handle->timer, 0, &itp, NULL);
}

#if !CONFIG_QM_MTRACE

void *qm_zalloc(unsigned int size)
{
    return calloc(1, size);
}

void *qm_malloc(unsigned int size)
{
    void *ptr = malloc(size);
    if(ptr == NULL){
        qm_reboot();
    }
    return ptr;
}

void *qm_realloc(void *mem, unsigned int size)
{
    return realloc(mem, size);
}

void qm_free(void *mem)
{
    free(mem);
}

uint32_t qm_free_mem_get(void)
{
  FILE *fd = NULL;
  int mem_size = -1;
  char name[32] = {0};
  char mem_str[16] = {0};
  char line_buff[256] = {0};

  fd = fopen("/proc/meminfo", "r");
  if (fd == NULL){
    goto try_mallinfo;
  }

  while (fgets(line_buff, sizeof(line_buff), fd) != NULL)
  {
    memset(name, 0, sizeof(name));
    memset(mem_str, 0, sizeof(mem_str));

    sscanf(line_buff, "%s %s", name, mem_str);

    name[strlen(name) - 1] = '\0';
    if (strncmp(name, "MemFree", strlen("MemFree")) == 0) {
      if (strlen(mem_str) < 10){
        mem_size = atoi(mem_str);
      }
      break;
    }
    memset(line_buff, 0, sizeof(line_buff));
  }

  fclose(fd);

try_mallinfo:
  if (mem_size == -1){
    mem_size = mallinfo().fordblks;
  }
  return mem_size;
}
#endif


uint32_t qm_now_ms(void)
{
    uint64_t ms = 0;
    struct timespec time = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &time);
    ms = time.tv_nsec/1000000 + time.tv_sec*1000;
    return (uint32_t)(ms & 0xFFFFFFFF);
}


int qm_msleep(int ms)
{ 
    usleep(ms*1000);

    return QM_EOK;
}

int qm_usleep(int us)
{
    volatile uint16_t b=0;
    volatile uint8_t delay1 = 0;
    b = us * 10;
    while (b--)
    {
		delay1++;
    }

    return QM_EOK;
}

void qm_init(void)
{

}

#if !CONFIG_QM_SPRINTF_SUPPORT
int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int  rc = 0;
    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);

    return rc;
}
#endif

int qm_printf(const char * fmt, ...)
{      
    uint16_t temp = 0;
    va_list ParamList;

    arch_printf_lock();

    va_start(ParamList, fmt);
    vsyslog(0, fmt, ParamList);
    va_end(ParamList);  

    arch_printf_unlock();

    return QM_EOK;
}



