#include <qm.h>
#include "qm_utils_list.h"
#include "qm_types.h"

#include "log.h"
#include "clock.h"
#include "OSAL.h"
#include "OSAL_Memory.h"
#include "qm_osal_task.h"

void qm_reboot(void)
{
	hal_system_soft_reset();
}

const char *qm_version_get(void)
{
    return "V1.0.0";
}




int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                     int stack_size, int prio)
{
    int ret = QM_EOK;

    return QM_EOK;
}

int qm_task_exit(qm_task_t *task)
{
    int ret = QM_EOK;

    return ret;
}



int qm_mutex_new(qm_mutex_t *mutex)
{
    int ret = QM_EOK;

    return QM_EOK;
}

int qm_mutex_free(qm_mutex_t *mutex)
{

    int ret = QM_EOK;

    return ret;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    int ret = QM_EOK;

    return ret;
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    int ret = QM_EOK;

    return ret;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return mutex->hdl != NULL;
}


int qm_sem_new(qm_sem_t *sem, int count)
{
    int ret = QM_EOK;

    return ret;
}

int qm_sem_free(qm_sem_t *sem)
{
    int ret = QM_EOK;

    return ret;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    int ret = QM_EOK;

    return ret;
}

int qm_sem_signal(qm_sem_t *sem)
{
    int ret = QM_EOK;



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

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    qm_err_t ret = QM_EOK;

    return ret;
}

int qm_queue_free(qm_queue_t *queue)
{

    return QM_EOK;
}

//node+msg
int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{

    return QM_EOK;
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{

    return QM_EOK;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return queue && queue->hdl != NULL;
}


#define QM_TIMER_MAX_NUM  16

typedef struct 
{
    qm_timer_t *timer;
    qm_timer_cb_t cb;
    void *arg;
    int is_used;
    uint32_t ms;
    int repeat;
}qm_timer_info_t;

static qm_timer_info_t qm_timer_info[QM_TIMER_MAX_NUM] = {0};    

static int qm_timer_unused_get(void)
{
    uint32_t i = 0;
    for(i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(!qm_timer_info[i].is_used){
            return i;
        }
    }
    return -1;
}

static int qm_timer_id_get_by_handle(qm_timer_t *timer, uint32_t *p_timer_id)
{
    uint32_t i = 0;
    for(i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(qm_timer_info[i].timer == timer){
            *p_timer_id = i;
            return 0;
        }
    }
    return -1;
}

uint16_t QM_APPS_TIMER_ProcessEvent(uint8 task_id, uint16 events)
{
    for(int index = 0; index < QM_TIMER_MAX_NUM; index++){
        if(events & 1 << index){
            if(qm_timer_info[index].cb){
                qm_timer_info[index].cb(qm_timer_info[index].timer, qm_timer_info[index].arg);
            }
            return events ^ (1 << index);
        }
    }
    return events;
}


int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb, void *arg, int ms, int repeat)
{         
    int index = 0;
    index = qm_timer_unused_get();
    if(index < 0){
        return -QM_ERROR;
    }
    qm_timer_info[index].repeat = repeat;
    qm_timer_info[index].timer = timer;
    qm_timer_info[index].arg = arg;
    qm_timer_info[index].cb = cb;
    qm_timer_info[index].ms = (uint32_t)ms;

    qm_timer_info[index].is_used = QM_TRUE;
    return QM_EOK;
}

int qm_timer_free(qm_timer_t *timer)
{
    if(timer == NULL ){
        return -QM_ERROR;
    }

    int ret = 0;    
    uint32_t index = 0;
    ret = qm_timer_id_get_by_handle(timer, &index);
    if(ret != 0){
        return -QM_ERROR;
    }
     
    qm_timer_info[index].is_used = QM_FALSE; 

    return 0;
}

int qm_timer_start(qm_timer_t *timer)
{
    if(timer == NULL){
        return -QM_ERROR;
    }

    int ret = 0;    
    uint32_t index = 0;
    ret = qm_timer_id_get_by_handle(timer, &index);
    if(ret != 0){
        return -QM_ERROR;
    }

    if(!qm_timer_info[index].repeat){
       return osal_start_timerEx(QM_APPS_TIMER_TASKID, (1 << index), qm_timer_info[index].ms);
    }else{
       return osal_start_reload_timer(QM_APPS_TIMER_TASKID, (1 << index), qm_timer_info[index].ms);
    }
		return 0;
}

int qm_timer_stop(qm_timer_t *timer)
{
    if(timer == NULL){
        return -QM_ERROR;
    }

    int ret = 0;    
    uint32_t index = 0;
    ret = qm_timer_id_get_by_handle(timer, &index);
    if(ret != 0){
        return -QM_ERROR;
    }
    return osal_stop_timerEx(QM_APPS_TIMER_TASKID, (1 << index));
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    if(timer == NULL){
        return -QM_ERROR;
    }
    
    int ret = 0;    
    uint32_t index = 0;
    ret = qm_timer_id_get_by_handle(timer, &index);
    LOG("");
    if(ret != 0){
        return -QM_ERROR;
    }
    qm_timer_info[index].ms =  ms;
    ret = osal_stop_timerEx(QM_APPS_TIMER_TASKID, (1 << index));    

    return qm_timer_start(timer);
}


#if !CONFIG_QM_MTRACE

void *qm_zalloc(unsigned int size)
{
    return NULL;
}

void *qm_malloc(unsigned int size)
{
    return osal_mem_alloc((uint16_t)size);
}

void *qm_realloc(void *mem, unsigned int size)
{
    return NULL;
}

void qm_free(void *mem)
{
    osal_mem_free(mem);
}

#endif


uint32_t qm_now_ms(void)
{
    return (rtc_get_counter() & 0xFFFFFFFF);
}


int qm_msleep(int ms)
{ 
    qm_err_t ret = QM_EOK;
    WaitMs((uint32_t)ms);
    return ret;
}
int qm_usleep(int us)
{
    qm_err_t ret = QM_EOK;
    WaitUs((uint32_t)us);

    return ret;
}
void qm_init(void)
{

}

extern void _uart_putc(char* data, uint16_t size);

int qm_printf(const char * fmt, ...)
{      
    va_list args;
    va_start(args, fmt);
    log_vsprintf(_uart_putc, fmt, args);
    va_end(args);
    return QM_EOK;
}



