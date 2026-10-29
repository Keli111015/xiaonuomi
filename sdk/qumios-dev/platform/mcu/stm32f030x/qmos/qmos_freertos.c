#include "qm_kernel.h"
#include <FreeRTOS.h>
#include <task.h>
#include <semphr.h>
#include <queue.h>
#include <timers.h>
#include <event_groups.h>
#include "stm32f0xx.h"
#include "portable.h"
#include "delay.h"

#include "qm.h"

#define ms2tick(ms) ((ms)/portTICK_PERIOD_MS)

void qm_reboot(void)
{
    NVIC_SystemReset();
}

const char *qm_version_get(void)
{
    return "V1.0.0";
}

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                     int stack_size, int prio)
{
    TaskHandle_t xHandle = NULL;
	stack_size /= sizeof(StackType_t);
	xTaskCreate( fn, name, stack_size, arg, prio, &xHandle );

    task->hdl = xHandle;

    return xHandle ? 0 : -1;
}

int qm_task_exit(qm_task_t *task)
{
    if(task == NULL){
        vTaskDelete(NULL);
    }else{
        vTaskDelete(task->hdl);
        task->hdl = NULL;
    }
    return 0;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    SemaphoreHandle_t mux = xSemaphoreCreateMutex();
    mutex->hdl = mux;
    return mux != NULL ? 0 : -1;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    vSemaphoreDelete(mutex->hdl);
    mutex->hdl = NULL;
    return 0;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    if (!mutex || !mutex->hdl) {
        return -1;
    }
    xSemaphoreTake(mutex->hdl, ms == QM_WAIT_FOREVER ? portMAX_DELAY : ms2tick(ms));

    return 0;
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    if (!mutex || !mutex->hdl) {
        return -1;
    }
    xSemaphoreGive(mutex->hdl);

    return 0;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return mutex->hdl != NULL;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    SemaphoreHandle_t s = xSemaphoreCreateCounting(100, count);
    sem->hdl = s;
    return 0;
}

int qm_sem_free(qm_sem_t *sem)
{
    if (sem == NULL || !sem->hdl) {
        return -1;
    }

    vSemaphoreDelete(sem->hdl);
    sem->hdl = NULL;
    return 0;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    if (sem == NULL || !sem->hdl) {
        return -1;
    }

    int ret = xSemaphoreTake(sem->hdl, ms == QM_WAIT_FOREVER ? portMAX_DELAY : ms2tick(ms));
    return ret == pdPASS ? 0 : -1;
}

int qm_sem_signal(qm_sem_t *sem)
{
    if (sem == NULL || !sem->hdl) {
        return -1;
    }
    xSemaphoreGive(sem->hdl);
    return 0;
}

int qm_sem_signal_from_isr(qm_sem_t *sem)
{
    if (sem == NULL || !sem->hdl) {
        return -1;
    }
    xSemaphoreGiveFromISR(sem->hdl, NULL);
    return 0;
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return sem && sem->hdl != NULL;
}

int qm_event_new(qm_event_t *event)
{
    EventGroupHandle_t event_group = NULL;
    event_group = xEventGroupCreate();
    if(event_group == NULL){
        return -1;
    }

    event->hdl = (void*)event_group;
    return 0;
}

int qm_event_free(qm_event_t *event)
{
    EventGroupHandle_t event_group = (EventGroupHandle_t)event->hdl;
    vEventGroupDelete(event_group);
    event->hdl = NULL;
    return 0;
}

int qm_event_is_valid(qm_event_t *event)
{
    return event != NULL && event->hdl != NULL;
}

int qm_event_get(qm_event_t *event, unsigned int wait_event_bits, unsigned char opt, unsigned int *actl_event_bits, unsigned int timeout)
{
    EventBits_t event_bits = 0;

    if (!qm_event_is_valid(event) || actl_event_bits == NULL) {
        return -QM_EINVAL;
    }

    EventGroupHandle_t event_group = (EventGroupHandle_t)event->hdl;
    if(opt == QM_EVENT_OR){
        event_bits = xEventGroupWaitBits(event_group, (EventBits_t)wait_event_bits, 0, 0, timeout/portTICK_PERIOD_MS);

    }else if(opt == QM_EVENT_OR_CLEAR){
        event_bits = xEventGroupWaitBits(event_group, (EventBits_t)wait_event_bits, 1, 0, timeout/portTICK_PERIOD_MS);

    }else if(opt == QM_EVENT_AND){
        event_bits = xEventGroupWaitBits(event_group, (EventBits_t)wait_event_bits, 0, 1, timeout/portTICK_PERIOD_MS);
        
    }else if(opt == QM_EVENT_AND_CLEAR){
        event_bits = xEventGroupWaitBits(event_group, (EventBits_t)wait_event_bits, 1, 1, timeout/portTICK_PERIOD_MS);
    }else{
        return -1;
    }

    *actl_event_bits = event_bits;

    return 0;
}

int qm_event_set(qm_event_t *event, unsigned int event_bits)
{
    EventGroupHandle_t event_group = (EventGroupHandle_t)event->hdl;
    xEventGroupSetBits( event_group, (EventBits_t)event_bits );
    return 0;
}

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    xQueueHandle q;
    q = xQueueCreate(queue_len, queue_size);
    if(q == NULL){
        return -1;
    }
    queue->hdl = q;
    return 0;
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
    if(queue == NULL || msg == NULL || size == 0){
        return -QM_EINVAL;
    }

    if(queue->hdl == NULL){
        return -QM_EINVAL;
    }

    return xQueueSend(queue->hdl, msg, 0) == pdPASS ? 0 : -1;
}

int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
    if(queue == NULL || msg == NULL || size == 0){
        return -QM_EINVAL;
    }

    if(queue->hdl == NULL){
        return -QM_EINVAL;
    }

    return xQueueSendFromISR(queue->hdl, msg, NULL) == pdPASS ? 0 : -1;
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{

    if(queue == NULL || msg == NULL || size == 0){
        return -QM_EINVAL;
    }

    if(queue->hdl == NULL){
        return -QM_EINVAL;
    }
    
    return xQueueReceive(queue->hdl, msg, ms == QM_WAIT_FOREVER ? portMAX_DELAY : ms2tick(ms))
           == pdPASS ? 0 : -1;
}

int qm_queue_is_valid(qm_queue_t *queue)
{
    return queue && queue->hdl != NULL;
}

#define QM_TIMER_MAX_NUM  20

typedef struct 
{
    TimerHandle_t timer;
    qm_timer_t *qm_timer;
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
        qm_timer_info[index].cb(qm_timer_info[index].qm_timer, qm_timer_info[index].arg);
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
    qm_timer_info[index].qm_timer = timer;

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

    xTimerStart(timer->hdl, 0);

    return 0;
}

int qm_timer_start_from_isr(qm_timer_t *timer)
{
    if(timer == NULL || timer->hdl == NULL){
        return -1;
    }
    xTimerStartFromISR(timer->hdl, 0);

    return 0;
}

int qm_timer_stop(qm_timer_t *timer)
{
    if(timer == NULL || timer->hdl == NULL){
        return -1;
    }

    xTimerStop(timer->hdl, 0);
    return 0;
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    if(timer == NULL || timer->hdl == NULL){
        return -1;
    };

    xTimerChangePeriod(timer->hdl, ms/portTICK_PERIOD_MS, 0);

    return  0;
}

#if !CONFIG_QM_MTRACE_SUPPORT

void *qm_malloc(unsigned int size)
{
    return pvPortMalloc(size);
}

void *qm_realloc(void *mem, unsigned int size)
{
    return realloc(mem, size);
}

void qm_free(void *mem)
{
    vPortFree(mem);
}

#endif

uint32_t qm_free_mem_get(void)
{
    return xPortGetFreeHeapSize();
}

uint32_t qm_now_ms(void)
{
    return xTaskGetTickCount();
}

int qm_msleep(int ms)
{
    vTaskDelay(ms/portTICK_RATE_MS);
    return 0;
}

int qm_usleep(int us)
{
    delay_us(us);
    return 0;
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



