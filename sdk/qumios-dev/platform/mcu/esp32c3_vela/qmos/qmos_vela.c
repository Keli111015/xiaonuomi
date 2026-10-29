#include <qm.h>
#include <applet.h>
#include <pthread.h>
#include "qm_errno.h"
#include "signal.h"
#include <fcntl.h>
#include <nuttx/timers/timer.h>
#include <mqueue.h>
#include <nuttx/signal.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include "qm_utils_list.h"
#include "qm_utils_timer.h"
#include "qm_timer.h"
#include "qm_types.h"
#include "qm_uart.h"
#include "miio_loop.h"

#include "qm_config.h"

#include "qm_log.h"
#include "syslog.h"
#include "qm_sprintf.h"
#include "multi_timer.h"

void qm_reboot(void)
{
    void *channel = miio_command_channel_get();
    miio_uart_send_str_wait_ack(channel, "reboot\r", 1000);
}

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

    task_handle_t *task_handle = (task_handle_t*)qm_malloc(sizeof(task_handle_t));
    if(task_handle == NULL){
        return -QM_ENOMEM;
    }

    memset(task_handle, 0, sizeof(task_handle_t));

    task_handle->fn = fn;
    task_handle->arg = arg;

    ret = pthread_create(&task_handle->pthread, NULL, fuc_thread, (void*)task_handle);
    if(ret != 0){
        qm_free(task_handle);
        return -QM_ERROR;
    }

    task->hdl = (void*)task_handle;
    pthread_detach(task_handle->pthread);
    return QM_EOK;
}

int qm_task_exit(qm_task_t *task)
{
    int ret = QM_EOK;
    task_handle_t *task_handle = (task_handle_t*)task->hdl;
    if(task_handle == NULL){
        return -QM_EINVAL;
    }

    pthread_exit(0);

    qm_free(task_handle);
    task_handle = NULL;
    return ret;
}


// pthread_mutex_t pthread_mutex;
typedef struct{
    pthread_mutex_t pthread_mutex;
}mutex_handle_t;


int qm_mutex_new(qm_mutex_t *mutex)
{
    int ret = QM_EOK;
    mutex_handle_t *pthread_mutex_handle  = (mutex_handle_t*)qm_malloc(sizeof(mutex_handle_t));

    if(pthread_mutex_handle == NULL){
        return -QM_ENOMEM;
    }

    memset(pthread_mutex_handle, 0, sizeof(mutex_handle_t));

    ret = pthread_mutex_init(&pthread_mutex_handle->pthread_mutex, NULL);

    mutex->hdl = (void *)pthread_mutex_handle;

    return QM_EOK;
}

int qm_mutex_free(qm_mutex_t *mutex)
{

    int ret = QM_EOK;

    mutex_handle_t *pthread_mutex_handle = (mutex_handle_t*)mutex->hdl;

    if(pthread_mutex_handle == NULL){
        return -QM_EINVAL;
    }

    ret = pthread_mutex_destroy(&pthread_mutex_handle->pthread_mutex);
    if(ret != 0){
        ret = -QM_ERROR;
        goto __exit;
    }
    return ret;
__exit:
    qm_free(pthread_mutex_handle);
    pthread_mutex_handle = NULL;
    return ret;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    int ret = QM_EOK;

    mutex_handle_t *pthread_mutex_handle = (mutex_handle_t*)mutex->hdl;

    if(pthread_mutex_handle == NULL){
        return -QM_EINVAL;
    }

    ret =  pthread_mutex_lock(&pthread_mutex_handle->pthread_mutex);
    if(ret != 0){
        ret = -QM_ERROR;
    }

    return ret;
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    int ret = QM_EOK;

    mutex_handle_t *pthread_mutex_handle = (mutex_handle_t*)mutex->hdl;

    if(pthread_mutex_handle == NULL){
        return -QM_EINVAL;
    }

    ret =  pthread_mutex_unlock(&pthread_mutex_handle->pthread_mutex);
    if(ret != 0){
        ret = -QM_ERROR;
    }

    return ret;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return mutex->hdl != NULL;
}

typedef struct{
    int count;
    pthread_cond_t cond;
    pthread_mutex_t mutex;
}sem_handle_t;

int qm_sem_new(qm_sem_t *sem, int count)
{
    int ret = QM_EOK;

    sem_handle_t *sem_handle = (sem_handle_t*)qm_malloc(sizeof(sem_handle_t));
    if(sem_handle == NULL){
        return -QM_ENOMEM;
    }

    memset(sem_handle, 0, sizeof(sem_handle_t));

    ret =  pthread_cond_init(&sem_handle->cond, NULL);
    if(ret != 0){
        ret = -QM_ERROR;
        goto __exit;
    }

    ret = pthread_mutex_init(&sem_handle->mutex, NULL);
    if(ret != 0){
        ret = -QM_ERROR;
        goto __exit;
    }

    sem_handle->count = count;

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

    sem_handle_t *sem_handle = (sem_handle_t *)sem->hdl;
    if(sem_handle == NULL){
        return -QM_EINVAL;
    }

    ret = pthread_cond_destroy(&sem_handle->cond);
    if(ret != 0){
        ret = -QM_ERROR;
        goto __exit;
    }

    ret = pthread_mutex_destroy(&sem_handle->mutex);
    if(ret != 0){
        ret = -QM_ERROR;
        goto __exit;
    }

__exit:
    qm_free(sem_handle);
    sem_handle = NULL;
    return ret;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    int ret = QM_EOK;

    sem_handle_t *sem_handle = (sem_handle_t *)sem->hdl;
    if(sem_handle == NULL){
        return -QM_EINVAL;
    }
    pthread_mutex_lock(&sem_handle->mutex);
    if(sem_handle->count == 0){
        if(ms == QM_WAIT_FOREVER){
            ret = pthread_cond_wait(&sem_handle->cond, &sem_handle->mutex);
        }else{
            ret = pthread_cond_timedwait(&sem_handle->cond, &sem_handle->mutex, ms*1000);
        }
        if(ret != 0){
            ret = -QM_ERROR;
        }
    }else{
        sem_handle->count -= 1;
    }
    pthread_mutex_unlock(&sem_handle->mutex);
    return ret;
}

int qm_sem_signal(qm_sem_t *sem)
{
    int ret = QM_EOK;

    sem_handle_t *sem_handle = (sem_handle_t *)sem->hdl;
    if(sem_handle == NULL){
        return -QM_EINVAL;
    }

    pthread_mutex_lock(&sem_handle->mutex);

    if (sem_handle->count == 0){
        ret = pthread_cond_signal(&sem_handle->cond);
        if(ret != 0){
            ret = -QM_ERROR;
        }
    }

    sem_handle->count += 1;

    pthread_mutex_unlock(&sem_handle->mutex);

    return ret;
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return sem->hdl != NULL;
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
    qm_sem_t sem;
    qm_mutex_t mutex;
    qm_list_t *list;
    uint32_t queue_len;
    uint32_t queue_size;
}queue_handle_t;

typedef struct 
{
    uint32_t size;
    uint8_t payload[1];
}queue_data_handle_t;

bool_t qm_queue_is_empty(qm_queue_t *queue)
{    
    uint32_t queue_len = 0;
    if(queue == NULL || queue->hdl == NULL){
        return QM_FALSE;
    }
 
    queue_handle_t *queue_handle = (queue_handle_t *)queue->hdl;

    qm_mutex_lock(&queue_handle->mutex, QM_WAIT_FOREVER);
    queue_len = queue_handle->list->len;
    qm_mutex_unlock(&queue_handle->mutex);

    return (queue_len >= 0 ? QM_FALSE : QM_TRUE);
}

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    qm_err_t ret = QM_EOK;
    queue_handle_t *queue_handle = NULL;

    if(queue == NULL || queue_len == 0 || queue_size == 0){
        return -QM_EINVAL;
    }

    queue_handle = (queue_handle_t*)qm_malloc(sizeof(queue_handle_t));
    if(queue_handle == NULL){
        return -QM_ENOMEM;
    }

    memset(queue_handle, 0, sizeof(queue_handle_t));

    ret = qm_sem_new(&queue_handle->sem, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_mutex_new(&queue_handle->mutex);
    if(ret != QM_EOK){
        goto __exit;
    }

    queue_handle->list = qm_list_new();
    if(queue_handle->list == NULL){
        goto __exit;
    }

    queue_handle->queue_len = queue_len;
    queue_handle->queue_size = queue_size;

    queue->hdl = (void *)queue_handle;
    return ret;
__exit:

    if(queue_handle->list){
        qm_list_destroy(queue_handle->list);
        queue_handle->list = NULL;
    }

    qm_free(queue_handle);
    queue_handle = NULL;

    return ret;
}

int qm_queue_free(qm_queue_t *queue)
{
    if(queue == NULL || queue->hdl == NULL){
        return -QM_EINVAL;
    }
    queue_handle_t *queue_handle = (queue_handle_t *)queue->hdl;

    qm_sem_free(&queue_handle->sem);
    qm_mutex_free(&queue_handle->mutex);
    qm_list_destroy(queue_handle->list);
    qm_free(queue_handle);
    queue_handle = NULL;

    return QM_EOK;
}

//node+msg
int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    uint32_t queue_len = 0;
    qm_list_node_t *node = NULL;
    queue_data_handle_t *info = NULL;
    if(queue == NULL || queue->hdl == NULL || msg == NULL){
        return -QM_EINVAL;
    }
    queue_handle_t *queue_handle = (queue_handle_t *)queue->hdl;

    if(size > queue_handle->queue_size){
        return -QM_EINVAL;
    }

    qm_mutex_lock(&queue_handle->mutex, QM_WAIT_FOREVER);
    queue_len = queue_handle->list->len;
    qm_mutex_unlock(&queue_handle->mutex);

    if(queue_len >= queue_handle->queue_len){
        return -QM_EFULL;
    }

    node = qm_list_node_extra_new(sizeof(queue_data_handle_t) + size - 1);
    if(node == NULL){
        return -QM_ENOMEM;
    }

    info  = (queue_data_handle_t *)node->val;

    info->size = size;
    memcpy(&info->payload[0], msg, size);

    qm_mutex_lock(&queue_handle->mutex, QM_WAIT_FOREVER);
    qm_list_lpush(queue_handle->list, node);
    qm_mutex_unlock(&queue_handle->mutex);

    qm_sem_signal(&queue_handle->sem);

    return QM_EOK;
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    qm_err_t ret = QM_EOK;
    qm_list_node_t *node = NULL;
    queue_data_handle_t *info = NULL;
    if(queue == NULL || queue->hdl == NULL ||  msg == NULL || size == NULL){
        return -QM_EINVAL;
    }
    queue_handle_t *queue_handle = (queue_handle_t *)queue->hdl;

    ret = qm_sem_wait(&queue_handle->sem, ms);
    if(ret != QM_EOK){
        return ret;
    }

    qm_mutex_lock(&queue_handle->mutex, QM_WAIT_FOREVER);
    node = qm_list_rpop(queue_handle->list);
    qm_mutex_unlock(&queue_handle->mutex);
    if(node == NULL){
        return -QM_EEMPTY;
    }

    info = (queue_data_handle_t *)node->val;

    *size = info->size;
    memcpy(msg, &info->payload[0], info->size);
    
    qm_free(node);
    
    return QM_EOK;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return queue && queue->hdl != NULL;
}


#if 0
typedef struct{
    int start;
    int period;
    qm_utils_time_t timeout;
    qm_timer_t *timer;
    int repeat;
    void *arg;
    qm_timer_cb_t cb;
}qm_timer_handle;

#define QM_TIMER_MAX_NUM  5
static qm_timer_handle g_timer_handle[QM_TIMER_MAX_NUM] = {0};

static void dev_timer_callback(union sigval value);

static int32_t dev_timer_init(void)
{
    int ret = QM_EOK;
    struct timer_notify_s notify = { 0 };
    timer_config_t config = {
        .id = 1
    };

    ret = boardctl(APP_BOARDIOC_TIMER, &config);
    if(ret != 0){
        return -QM_ERROR;
    } 

    int tim_fd = open("/dev/timer1", O_RDONLY);
    if (tim_fd < 0) {
        return -QM_ERROR;
    }
    /* Set the timer interval */

    ret = ioctl(tim_fd, TCIOC_SETTIMEOUT, 50000);
    if (ret < 0) {
        return -QM_ERROR;
    }

    notify.irq = 2;
    notify.event.sigev_notify = SIGEV_THREAD;
    notify.event.sigev_signo  = 17;
    notify.event.sigev_value.sival_ptr = (void *)&g_timer_handle;
    notify.event.sigev_notify_function = dev_timer_callback;

    ret = ioctl(tim_fd, TCIOC_NOTIFICATION, (unsigned long)(&notify));
    if (ret < 0) {
        return -QM_ERROR;
    }

    /* Start the timer */
    ret = ioctl(tim_fd, TCIOC_START, 0);
    if (ret < 0) {
        return -QM_ERROR;
    }
    return QM_EOK; 
}

static qm_timer_handle *timer_handle_unused_get(void)
{
    for(int i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(!g_timer_handle[i].timer){
            return &g_timer_handle[i];  
        }
    }
    return NULL;
}

static qm_timer_handle *timer_handle_get(qm_timer_t *timer)
{
    for(int i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(g_timer_handle[i].timer == timer){
            return &g_timer_handle[i];  
        }
    }
    return NULL;
}

static int timer_start(qm_timer_handle *timer_handle)
{

    if(timer_handle->start){
        return QM_EOK;
    }

    timer_handle->start = 1;
    qm_utils_time_init(&timer_handle->timeout);
    qm_utils_time_countdown_ms(&timer_handle->timeout, timer_handle->period);
    return QM_EOK; 
}

static int timer_stop(qm_timer_handle *timer_handle)
{
    if(!timer_handle->start){
        return QM_EOK;
    }
    timer_handle->start = 0;
    return QM_EOK; 
}

static void dev_timer_callback(union sigval value)
{
    for(int i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(!g_timer_handle[i].start){
            continue;
        }

        if(!qm_utils_time_is_expired(&g_timer_handle[i].timeout)){
            continue;
        }

        qm_utils_time_countdown_ms(&g_timer_handle[i].timeout, g_timer_handle[i].period);

        if(g_timer_handle[i].cb){
            g_timer_handle[i].cb(g_timer_handle[i].timer, g_timer_handle[i].arg);
        }   

        if(!g_timer_handle[i].repeat){
            timer_stop(&g_timer_handle[i]);
        }
    }
}

#endif

typedef struct{
    multi_timer_t timer;
    qm_timer_t *qm_timer;
    void *arg;
    qm_timer_cb_t cb;
}qm_timer_handle;

static void timer_callback(multi_timer_t* timer, void* arg)
{
    qm_timer_handle *timer_handle = (void* )arg;  
    if(timer == NULL || arg == NULL){
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
    qm_timer_handle *timer_handle = NULL;  
    if(timer == NULL || cb == NULL || ms == 0){
        return -QM_EINVAL;
    }           

    timer_handle = (qm_timer_handle *)qm_malloc(sizeof(qm_timer_handle));
    if(timer_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(timer_handle, 0, sizeof(qm_timer_handle));

    timer_handle->arg = arg;
    timer_handle->cb = cb;
    timer_handle->qm_timer = timer;

    timer->hdl = (void*)timer_handle;

    return multi_timer_creat(&timer_handle->timer, timer_callback, (void *)timer_handle, (uint32_t)ms, repeat);
#if 0
    qm_timer_handle *timer_handle = NULL;  
    timer_handle = timer_handle_unused_get();
    if(timer_handle == NULL){
        return -QM_EFULL;
    }  

    timer_handle->timer = timer;

    timer_handle->arg = arg;
    timer_handle->cb = cb;
    timer_handle->period = (uint32_t)ms;
   
    timer_handle->repeat = repeat;

    timer->hdl = (void*)timer_handle;
    return QM_EOK;
#endif
}

int qm_timer_free(qm_timer_t *timer)
{
    int ret = QM_EOK;
    qm_timer_handle *timer_handle = NULL;  
    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle *)timer->hdl;
    ret = multi_timer_delete(&timer_handle->timer);
    if(ret != QM_EOK){
        return ret;
    }

    qm_free(timer->hdl);
    timer->hdl = NULL;

    return QM_EOK;
}

int qm_timer_start(qm_timer_t *timer)
{
    qm_timer_handle *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle *)timer->hdl;
    return multi_timer_start(&timer_handle->timer);
#if 0
    timer_handle = timer_handle_get(timer);
    if(timer_handle == NULL){
        return -QM_EINVAL;
    }


    timer_start(timer_handle);  

    return QM_EOK;
#endif
}

int qm_timer_stop(qm_timer_t *timer)
{
    qm_timer_handle *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle *)timer->hdl;
    return multi_timer_stop(&timer_handle->timer);
#if 0
    timer_handle = timer_handle_get(timer);
    if(timer_handle == NULL){
        return -QM_EINVAL;
    }

    timer_stop(timer_handle);
     
    return QM_EOK;
#endif
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    qm_timer_handle *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle *)timer->hdl;
    return multi_timer_change(&timer_handle->timer, (uint32_t)ms);
}

#if !CONFIG_QM_MTRACE

void *qm_zalloc(unsigned int size)
{
    return calloc(size, 1);
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
    qm_err_t ret = QM_EOK;

    usleep(ms*1000);

    return ret;
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

#if CONFIG_QM_UART_SUPPORT
extern void uart_recv_thread(uint32_t recv_timeout);
#endif

static void* qmos_callback(void* arg)
{
    uint32_t delay = 20;
    multi_timer_init();

    while(1) 
    {
        delay = multi_timer_yield();
        if(delay <= 20){
            delay = 20;
        }
    #if CONFIG_QM_UART_SUPPORT
        uart_recv_thread(delay);
    #else   
        qm_msleep(delay);
    #endif
    }

    pthread_exit(NULL);
    return NULL;
}


void qm_init(void)
{
#if 0
    dev_timer_init();
#endif
    pthread_t tid1;
    pthread_create(&tid1, NULL, qmos_callback, NULL);
    pthread_detach(tid1);
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

#define LOG_BUFFER_MAX_LEN  256
static uint16_t buffer_len = 0;
static uint8_t buffer[LOG_BUFFER_MAX_LEN] = {0};

void nx_syslog(int priority, const char *str);

int qm_printf(const char * fmt, ...)
{      
    uint16_t temp = 0;
    va_list ParamList;
    va_start(ParamList, fmt);
#if !CONFIG_QM_SPRINTF_SUPPORT
  	temp = vsnprintf((char *)(buffer + buffer_len),LOG_BUFFER_MAX_LEN - buffer_len, fmt, ParamList); 
#else
    temp = qm_vsnprintf((char *)(buffer + buffer_len),LOG_BUFFER_MAX_LEN - buffer_len, fmt, ParamList); 
#endif
	if(strstr((const char *)(buffer + buffer_len), "\n") || buffer_len + temp >= LOG_BUFFER_MAX_LEN - 1){
		temp = 0;
		buffer_len  = 0;
        nx_syslog(0, (const char *)buffer);
		memset(buffer, 0, LOG_BUFFER_MAX_LEN);
	}
    buffer_len += temp;
    va_end(ParamList);  
    return QM_EOK;
}



