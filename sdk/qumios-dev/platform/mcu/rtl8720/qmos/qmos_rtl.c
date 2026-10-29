#include "qm.h"

#include "osdep_service.h"
#include "FreeRTOSConfig.h"
#include "hal_wdt.h"
#include "hal_syson.h"
#include "hal_timer.h"
#include "sys_api.h"

#define ms2tick(ms) ((ms)/portTICK_PERIOD_MS)
#define portIsInIsr()				 (__get_IPSR() != 0)

void qm_reboot(void)
{
    hal_sys_set_fast_boot(NULL, 0);
	hal_misc_rst_by_wdt();
}

const char *qm_version_get(void)
{
    return "V2.0.0";
}

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg, int stack_size, int prio)
{
    TaskHandle_t xHandle;
	stack_size /= sizeof(StackType_t);
	xTaskCreate( fn, name, stack_size, arg, prio, &xHandle );

    task->hdl = xHandle;

    return xHandle ? QM_EOK : -QM_ERROR;
}

int qm_task_exit(qm_task_t *task)
{
    if(task == NULL){
        vTaskDelete(NULL);
    }else{
        vTaskDelete(task->hdl);
        task->hdl = NULL;
    }
    return QM_EOK;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    if(mutex == NULL || mutex->hdl){
        return -QM_EINVAL;
    }

    SemaphoreHandle_t mux = xSemaphoreCreateMutex();

    mutex->hdl = mux;
    
    return mux != NULL ? QM_EOK : -QM_ERROR;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }
    vSemaphoreDelete(mutex->hdl);
    mutex->hdl = NULL;
    return QM_EOK;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }

    int ret = QM_EOK;
	if(portIsInIsr()){
		portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
		ret = xSemaphoreTakeFromISR(mutex->hdl, &xHigherPriorityTaskWoken);
		portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
	}else {
        ret = xSemaphoreTake(mutex->hdl, ms == QM_WAIT_FOREVER ? portMAX_DELAY : ms2tick(ms));
	}
    return ret == pdTRUE ? QM_EOK : -QM_ERROR;
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    if(mutex == NULL || mutex->hdl == NULL){
        return -QM_EINVAL;
    }
    
    int ret = QM_EOK;

	if (portIsInIsr()) {
		portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
		ret = xSemaphoreGiveFromISR(mutex->hdl, &xHigherPriorityTaskWoken);
		portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
	}else {
		ret = xSemaphoreGive(mutex->hdl);
	}
    return ret == pdTRUE ? QM_EOK : -QM_ERROR;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return (mutex != NULL && mutex->hdl != NULL) ? 1 : 0;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    if(sem == NULL || sem->hdl){
        return -QM_EINVAL;
    }  
    SemaphoreHandle_t s = xSemaphoreCreateCounting(100, count);

    sem->hdl = s;
    return s != NULL ? QM_EOK : -QM_ERROR;
}

int qm_sem_free(qm_sem_t *sem)
{
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }  

    vSemaphoreDelete(sem->hdl);
    sem->hdl = NULL;
    return QM_EOK;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }  

    int ret = QM_EOK;
	if(portIsInIsr()){
		portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
		ret = xSemaphoreTakeFromISR(sem->hdl, &xHigherPriorityTaskWoken);
		portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
	}else {
        ret = xSemaphoreTake(sem->hdl, ms == QM_WAIT_FOREVER ? portMAX_DELAY : ms2tick(ms));
	}
    return ret == pdTRUE ? QM_EOK : -QM_ERROR;
}

int qm_sem_signal(qm_sem_t *sem)
{
    if(sem == NULL || sem->hdl == NULL){
        return -QM_EINVAL;
    }  
    
    int ret = QM_EOK;

	if (portIsInIsr()) {
		portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
		ret = xSemaphoreGiveFromISR(sem->hdl, &xHigherPriorityTaskWoken);
		portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
	}else {
		ret = xSemaphoreGive(sem->hdl);
	}
    return ret == pdTRUE ? QM_EOK : -QM_ERROR;
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return (sem != NULL && sem->hdl != NULL) ? 1 : 0;
}

int qm_event_new(qm_event_t *event)
{
    return -QM_EINVAL;
}

int qm_event_free(qm_event_t *event)
{
    return -QM_EINVAL;
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

    return -QM_EINVAL;
}

int qm_event_set(qm_event_t *event, unsigned int event_bits)
{
    return -QM_EINVAL;
}

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    if(queue == NULL || queue_len == 0 || queue_size == 0){
        return -QM_EINVAL;
    }

    xQueueHandle q = xQueueCreate(queue_len, queue_size);
    queue->hdl = q;
    return queue->hdl != NULL ? QM_EOK : -QM_ERROR;
}

int qm_queue_free(qm_queue_t *queue)
{
    if(queue == NULL || queue->hdl == NULL){
        return -QM_EINVAL;
    }  

    vQueueDelete(queue->hdl);
    queue->hdl = NULL;
    return 0;
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    if( queue == NULL || queue->hdl == NULL || 
        msg == NULL   || size == 0){
        return -QM_EINVAL;
    }  

    int ret = QM_EOK;
    if(portIsInIsr()) {
		portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
		ret = xQueueSendToBackFromISR(queue->hdl, msg, &xHigherPriorityTaskWoken);
		portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
	}else {
		ret = xQueueSendToBack(queue->hdl, msg, portMAX_DELAY);
	}
    return ret == pdTRUE ? QM_EOK : -QM_ERROR;
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
     if( queue == NULL || queue->hdl == NULL || 
        msg == NULL    || size == NULL){
        return -QM_EINVAL;
    }     
    
    int ret = QM_EOK;
    if(portIsInIsr()) {
		portBASE_TYPE xHigherPriorityTaskWoken = pdFALSE;
		ret = xQueueReceiveFromISR(queue->hdl, msg, &xHigherPriorityTaskWoken);
		portEND_SWITCHING_ISR(xHigherPriorityTaskWoken);
	}else {
		ret = xQueueReceive(queue->hdl, msg, ms == QM_WAIT_FOREVER ? portMAX_DELAY : ms2tick(ms));
	}
    return ret == pdTRUE ? QM_EOK : -QM_ERROR;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return (queue != NULL && queue->hdl != NULL) ? 1 : 0;
}

#define QM_TIMER_MAX_NUM  10

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
    int i = 0;
    for(i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(!qm_timer_info[i].is_used){
            return i;
        }
    }
    return -1;
}

static void timer_callback(TimerHandle_t timer)
{
    int index = 0;
    if(timer == NULL){
        return;
    }
    index = ( int32_t ) pvTimerGetTimerID( timer );
    if(qm_timer_info[index].cb){
        qm_timer_info[index].cb(qm_timer_info[index].timer, qm_timer_info[index].arg);
    }
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb,
                  void *arg, int ms, int repeat)
{
    int index = 0;

    index = qm_timer_unused_get();
    if(index < 0){
        return -1;
    }

    qm_timer_info[index].timer = xTimerCreate(NULL, ms/portTICK_PERIOD_MS, repeat, (void*)index, timer_callback);
    if(qm_timer_info[index].timer == NULL){
        return -1;
    }

    timer->hdl = qm_timer_info[index].timer;

    qm_timer_info[index].is_used = 1; 
    qm_timer_info[index].cb = cb;
    qm_timer_info[index].arg = arg;

    return 0;
}

int qm_timer_free(qm_timer_t *timer)
{
    int index = 0;
    if(timer == NULL || timer->hdl == NULL){
        return -1;
    }

    index = ( int32_t ) pvTimerGetTimerID( timer->hdl );
    qm_timer_info[index].is_used = 0; 

    xTimerDelete(timer->hdl, 0);

    return 0;
}

int qm_timer_start(qm_timer_t *timer)
{
    if(timer == NULL || timer->hdl == NULL){
        return -1;
    }

    if(portIsInIsr()) 
    {
        xTimerStartFromISR(timer->hdl, 0);
    }
    else
    {
        xTimerStart(timer->hdl, 0);
    } 

    return 0;
}

int qm_timer_stop(qm_timer_t *timer)
{
    if(timer == NULL || timer->hdl == NULL){
        return -1;
    }

    if(portIsInIsr()) 
    {
        xTimerStopFromISR(timer->hdl, 0);
    }
    else
    {
        xTimerStop(timer->hdl, 0);
    } 
    
    return 0;
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    if(timer == NULL || timer->hdl == NULL){
        return -1;
    };

    if(portIsInIsr()) 
    {
         xTimerChangePeriodFromISR(timer->hdl, ms / portTICK_PERIOD_MS, 0);
    }
    else
    {
        xTimerChangePeriod(timer->hdl, ms / portTICK_PERIOD_MS, 0);
    } 
    return  0;
}

#if !CONFIG_QM_MTRACE_SUPPORT

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

uint32_t qm_free_mem_get(void)
{
    return rtw_getFreeHeapSize();
}

uint32_t qm_now_ms(void)
{
    return ((hal_read_systime_us() / 1000) & 0xFFFFFFFF);
}

int qm_msleep(int ms)
{
    rtw_msleep_os(ms);
    return QM_EOK;
}

int qm_usleep(int us)
{
    rtw_usleep_os(us);
    return QM_EOK;
}

int qm_printf(const char * fmt, ...)
{
    va_list args;
    int ret;
    va_start(args, fmt);
    ret = vprintf(fmt, args);
    va_end(args);
    return ret;
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

void qm_init(void)
{

}
