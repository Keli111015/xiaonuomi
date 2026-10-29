#include "qmos/qm.h"
#include "os_sched.h"
#include "os_mem.h"
#include "os_task.h"
#include "os_timer.h"
#include "os_msg.h"
#include "os_sync.h"
#include "rtl876x_wdg.h"
#include "trace.h"
#include <stdarg.h>
#include <stdio.h>
void qm_reboot(void)
{
    WDG_SystemReset(RESET_ALL, UPPER_CMD_RESET);
}

const char *qm_version_get(void)
{
    return "V2.0.0";
}

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                     int stack_size, int prio)
{
   if(task == NULL || fn == NULL){
        return -QM_ERROR;
   }
   if(os_task_create(&task->hdl, name, fn, arg, stack_size, prio) == true){
       return QM_EOK;
   }else{
       return -QM_ERROR;
   }
}

int qm_task_exit(qm_task_t *task)
{
    if(task == NULL || task->hdl == NULL){
       return -QM_ERROR;
    }
    os_task_delete(task->hdl);
    return 0;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    if(mutex == NULL){
        return -QM_ERROR;
    }
    if (os_mutex_create(&mutex->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_ERROR;
    }
    if (os_mutex_delete(mutex->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_ERROR;
    }
    if (os_mutex_take(mutex->hdl, ms) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_ERROR;
    }
    if (os_mutex_give(mutex->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return mutex && mutex->hdl != NULL;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    if(sem == NULL){
        return -QM_ERROR;
    }
    
    if (os_sem_create(&sem->hdl, count, 100) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_sem_free(qm_sem_t *sem)
{
    if(sem == NULL || sem->hdl == NULL){
        return -QM_ERROR;
    }
    if (os_sem_delete(sem->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    if(sem == NULL || sem->hdl == NULL){
        return -QM_ERROR;
    }
    if (os_sem_take(sem->hdl, ms) == true){
        return QM_EOK;
    }else{
        return -QM_ETIMEOUT;
    }
}

int qm_sem_signal(qm_sem_t *sem)
{
    if(sem == NULL || sem->hdl == NULL){
        return -QM_ERROR;
    }
    if (os_sem_give(sem->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
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

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    if(queue == NULL){
        return -QM_ERROR;
    }
    if (os_msg_queue_create(&queue->hdl, queue_len, queue_size) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_queue_free(qm_queue_t *queue)
{
    if(queue == NULL || queue->hdl == NULL){
        return -QM_ERROR;
    }
    if (os_msg_queue_delete(queue->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    if(queue == NULL || queue->hdl == NULL){
        return -QM_ERROR;
    }
    if (os_msg_send(queue->hdl, msg, 0) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    if(queue == NULL || queue->hdl == NULL){
        return -QM_ERROR;
    }

    if (os_msg_recv(queue->hdl, msg, ms) == true){
        return QM_EOK;
    }else{
        return -QM_ETIMEOUT;
    }
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return queue && queue->hdl != NULL;
}

#include "qm_log.h"

#define QM_TIMER_MAX_NUM  20
#define QM_TIMER_INDEX    1000

typedef struct 
{
    qm_timer_t *timer;
    qm_timer_cb_t cb;
    void *arg;
    int is_used;
}qm_timer_info_t;


static qm_timer_info_t qm_timer_info[QM_TIMER_MAX_NUM] = {0};


static int qm_timer_unused_get(void)
{
    uint32_t i = 0;
    for(i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(!qm_timer_info[i].is_used){
            return i+QM_TIMER_INDEX;
        }
    }
    return -1;
}

static int qm_timer_id_get_by_handle(qm_timer_t *timer, uint32_t *p_timer_id)
{
    uint32_t i = 0;
    for(i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(qm_timer_info[i].timer == timer){
            *p_timer_id = i+QM_TIMER_INDEX;
            return 0;
        }
    }
    return -1;
}

static void timer_callback(void *p_handle)
{
    uint32_t index = 0;

    os_timer_id_get(&p_handle, &index);
    index = index - QM_TIMER_INDEX;
    
    if(qm_timer_info[index].cb){
        qm_timer_info[index].cb(qm_timer_info[index].timer, qm_timer_info[index].arg);
    }
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return timer && timer->hdl != NULL;
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb,
                  void *arg, int ms, int repeat)
{
    int index = 0;
    index = qm_timer_unused_get();
    if(index < 0){
        return -QM_ERROR;
    }
    index = index - QM_TIMER_INDEX;
    qm_timer_info[index].timer = timer;
    qm_timer_info[index].cb = cb;
    qm_timer_info[index].arg = arg;

    if(os_timer_create(&timer->hdl, "timer", (uint32_t)(index+QM_TIMER_INDEX), ms, repeat, timer_callback) == true){
        qm_timer_info[index].is_used = 1; 
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_timer_free(qm_timer_t *timer)
{
    uint32_t index = 0;
    int ret = 0;
    if(timer == NULL || timer->hdl == NULL){
        return -QM_ERROR;
    }
    
    ret = qm_timer_id_get_by_handle(timer, &index);
    if(ret != 0){
        return -QM_ERROR;
    }
    
    index = index - QM_TIMER_INDEX;
    
    qm_timer_info[index].is_used = 0; 

    if(os_timer_delete(&timer->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
    
    return 0;
}

int qm_timer_start(qm_timer_t *timer)
{
    if(timer == NULL || timer->hdl == NULL){
        return -QM_ERROR;
    }
    
    if(os_timer_start(&timer->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_timer_stop(qm_timer_t *timer)
{
    if(timer == NULL || timer->hdl == NULL){
        return -QM_ERROR;
    }

    if(os_timer_stop(&timer->hdl) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    if(timer == NULL || timer->hdl == NULL){
        return -QM_ERROR;
    }
    if(os_timer_restart(&timer->hdl, (uint32_t)ms) == true){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

void *qm_zalloc(unsigned int size)
{
    return os_mem_zalloc(RAM_TYPE_DATA_ON, size);
}

void *qm_malloc(unsigned int size)
{
    return os_mem_alloc(RAM_TYPE_DATA_ON, size);;
}

void *qm_realloc(void *mem, unsigned int size)
{
    return 0;
}

void qm_free(void *mem)
{
    os_mem_free(mem);
}

uint32_t qm_free_mem_get(void)
{
    return os_mem_peek(RAM_TYPE_DATA_ON);
}

uint32_t qm_now_ms(void)
{
    return os_sys_time_get() & 0xFFFFFFFF;
}

int qm_msleep(int ms)
{
    os_delay(ms);
    return 0;
}

#if !CONFIG_QM_SPRINTF_SUPPORT
int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int rc;

    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);

    return rc;
}
#endif

#if CONFIG_QM_LOG_SUPPORT 
#define LOG_BUFFER_MAX_LEN  512
static uint16_t buffer_len = 0;
static char buffer[LOG_BUFFER_MAX_LEN] = {0};

int qm_printf(const char * fmt, ...)
{
    uint16_t temp = 0;
    va_list ParamList;
    va_start(ParamList, fmt);
  	temp = vsnprintf (buffer + buffer_len,LOG_BUFFER_MAX_LEN - buffer_len, fmt, ParamList);   

	if(strstr(buffer, "\n") || buffer_len + temp>= LOG_BUFFER_MAX_LEN){
		temp = 0;
		buffer_len  = 0;
        log_direct(COMBINE_TRACE_INFO(TYPE_BEE2, SUBTYPE_DIRECT, 0, 0), (const char*)buffer);
		memset(buffer, 0, LOG_BUFFER_MAX_LEN);
	}
	buffer_len += temp;
    va_end(ParamList);
	return QM_EOK;
}
#endif

void qm_init(void)
{

}

int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
	return QM_EOK;
}
