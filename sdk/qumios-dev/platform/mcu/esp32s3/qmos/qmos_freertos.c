#include <qmos/qm.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <freertos/queue.h>
#include <freertos/timers.h>
#include <freertos/event_groups.h>
#include "esp_system.h"
#include "rom/ets_sys.h"
#include <sys/time.h>

#include "esp_timer.h"
#include "esp_idf_version.h"

#include "qm_config.h"

#define ms2tick(ms) ((ms)/portTICK_PERIOD_MS)

#define EXT_TASK_TEMP   (1) //临时使用，后续更改

static qm_mutex_t g_timer_info_lock = {0}; 

void qm_reboot(void)
{
    esp_restart();
}

const char *qm_version_get(void)
{
    return "V1.0.0";
}

#if CONFIG_QM_OS_SUPPORT

BaseType_t __attribute__((weak)) xTaskCreateRestrictedPinnedToCore(const TaskParameters_t *const pxTaskDefinition, TaskHandle_t *pxCreatedTask, const BaseType_t xCoreID)
{
    if (pxTaskDefinition == NULL || pxTaskDefinition->pvTaskCode == NULL) {
        return pdFAIL;
    }

    return xTaskCreatePinnedToCore(pxTaskDefinition->pvTaskCode,
                                   pxTaskDefinition->pcName,
                                   pxTaskDefinition->usStackDepth,
                                   pxTaskDefinition->pvParameters,
                                   pxTaskDefinition->uxPriority,
                                   pxCreatedTask,
                                   xCoreID);
}

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg,
                     int stack_size, int prio)
{
    TaskHandle_t xHandle = NULL;

    stack_size /= sizeof(StackType_t);
    xTaskCreate( fn, name, stack_size, arg, prio, &xHandle);
    task->hdl = xHandle;
    return xHandle ? 0 : -1; 
}

int qm_task_new_to_core(qm_task_t *task, const char *name, void (*fn)(void *), void *arg, int stack_size, int prio, int core_id)
{
    TaskHandle_t xHandle = NULL;
	stack_size /= sizeof(StackType_t);
    xTaskCreatePinnedToCore(fn, name, stack_size, arg, prio, &xHandle, core_id);
    task->hdl = xHandle;

    return xHandle ? 0 : -1;
}

qm_bool qm_task_is_current(const qm_task_t *task)
{
    return task != NULL && task->hdl != NULL &&
           (TaskHandle_t)task->hdl == xTaskGetCurrentTaskHandle() ?
           QM_TRUE : QM_FALSE;
}

int qm_task_exit(qm_task_t *task)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#if defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
    vTaskDelete(NULL);
    return 0;
#else
    if(task == NULL){
        vTaskDelete(NULL);
    }else{
        vTaskDelete(task->hdl);
        task->hdl = NULL;
    }
#endif
#else
    if(task == NULL){
        vTaskDelete(NULL);
    }else{
        vTaskDelete(task->hdl);
        task->hdl = NULL;
    }
#endif
    return 0;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#if defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
    SemaphoreHandle_t mux = xSemaphoreCreateMutexWithCaps(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    SemaphoreHandle_t mux = xSemaphoreCreateMutex();
#endif
#else
    SemaphoreHandle_t mux = xSemaphoreCreateMutex();
#endif
    mutex->hdl = mux;
    return mux != NULL ? 0 : -1;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    if (!mutex || !mutex->hdl) {
        return -1;
    }    

    vSemaphoreDelete(mutex->hdl);
    mutex->hdl = NULL;
    return 0;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    if (!mutex || !mutex->hdl) {
        return -1;
    }
    if (xSemaphoreTake(mutex->hdl, ms == QM_WAIT_FOREVER ? portMAX_DELAY : ms2tick(ms)) != pdTRUE) {
        return -1;
    }

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
    return mutex && mutex->hdl != NULL;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    if (sem == NULL) {
        return -1;
    }
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#if defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
    SemaphoreHandle_t s = xSemaphoreCreateCountingWithCaps(100, count, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    SemaphoreHandle_t s = xSemaphoreCreateCounting(100, count);
#endif
#else
    SemaphoreHandle_t s = xSemaphoreCreateCounting(100, count);
#endif
    sem->hdl = s;
    return s != NULL ? 0 : -1;
}

int qm_sem_free(qm_sem_t *sem)
{
    if (sem == NULL || !sem->hdl ) {
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

#if CONFIG_QM_OS_EVENT_SUPPORT
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
    if(event_group == NULL){
        return -1;
    }
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
    if(event_group == NULL){
        return -1;
    }

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
    if(event_group == NULL){
        return -1;
    }
    
    xEventGroupSetBits( event_group, (EventBits_t)event_bits );
    return 0;
}

int qm_event_clear(qm_event_t *event, unsigned int event_bits)
{
    EventGroupHandle_t event_group = (EventGroupHandle_t)event->hdl;
    if(event_group == NULL){
        return -1;
    }
    
    xEventGroupClearBits( event_group, (EventBits_t)event_bits );
    return 0;
}

#endif

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{

    QueueHandle_t q;
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#if defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
    q = xQueueCreateWithCaps(queue_len, queue_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    q = xQueueCreate(queue_len, queue_size);
#endif
#else
    q = xQueueCreate(queue_len, queue_size);
#endif
    if(q == NULL){
        return -1;
    }
    queue->hdl = q;
    return 0;
}

int qm_queue_free(qm_queue_t *queue)
{
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#if defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
    vQueueDeleteWithCaps(queue->hdl);
#else
    vQueueDelete(queue->hdl);
#endif
#else
    vQueueDelete(queue->hdl);
#endif
    queue->hdl = NULL;
    return 0;
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    return xQueueSend(queue->hdl, msg, 0) == pdPASS ? 0 : -1;
}

int qm_queue_send_from_isr(qm_queue_t *queue, void *msg, unsigned int size)
{
    return xQueueSendFromISR(queue->hdl, msg, NULL) == pdPASS ? 0 : -1;
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
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
    qm_mutex_lock(&g_timer_info_lock, QM_WAIT_FOREVER);
    for(i = 0; i < QM_TIMER_MAX_NUM; i++){
        if(!qm_timer_info[i].is_used){
            qm_timer_info[i].is_used = 1;
            qm_mutex_unlock(&g_timer_info_lock);
            return i;
        }
    }
    qm_mutex_unlock(&g_timer_info_lock);
    return -1;
}

static int qm_timer_info_reset(qm_timer_info_t *timer_info)
{
    if(timer_info == NULL) {
        return -QM_EINVAL;
    }
    qm_mutex_lock(&g_timer_info_lock, QM_WAIT_FOREVER);
    memset(timer_info, 0, sizeof(qm_timer_info_t));
    qm_mutex_unlock(&g_timer_info_lock);
    return QM_EOK;
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
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 1, 0)
#if defined(CONFIG_SPIRAM_BOOT_INIT) && (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY)
    StaticTimer_t *timer_buffer = (StaticTimer_t *)qm_calloc(1, sizeof(StaticTimer_t));
    qm_timer_info[index].timer = xTimerCreateStatic(NULL, ms/portTICK_PERIOD_MS, repeat, (void*)index, timer_callback, timer_buffer);
#else
    qm_timer_info[index].timer = xTimerCreate(NULL, ms/portTICK_PERIOD_MS, repeat, (void*)index, timer_callback);
#endif
#else
    qm_timer_info[index].timer = xTimerCreate(NULL, ms/portTICK_PERIOD_MS, repeat, (void*)index, timer_callback);
#endif
   
    if(qm_timer_info[index].timer == NULL){
        qm_timer_info_reset(&qm_timer_info[index]);
        return -1;
    }

    timer->hdl = qm_timer_info[index].timer;

    qm_timer_info[index].is_used = 1; 
    qm_timer_info[index].cb = cb;
    qm_timer_info[index].arg = arg;
    qm_timer_info[index].qm_timer = timer;

    return 0;
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return timer && timer->hdl != NULL;
}

int qm_timer_free(qm_timer_t *timer)
{
    int index = 0;
    if(timer == NULL || timer->hdl == NULL){
        return -1;
    }

    index = ( int32_t ) pvTimerGetTimerID( timer->hdl );
    
    qm_timer_info_reset(&qm_timer_info[index]);

    xTimerDelete(timer->hdl, 0);
    timer->hdl = NULL;
    
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

#endif

#if !CONFIG_QM_MTRACE_SUPPORT

void* qm_calloc(unsigned int nitems, unsigned int size)
{
    void *data =  NULL;
    #if CONFIG_SPIRAM_BOOT_INIT
        data = heap_caps_malloc(nitems * size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (data) {
            memset(data, 0, nitems * size);
        }
    #else
        data = calloc(nitems, size);
    #endif
        return data;
}

void* qm_aligned_alloc(unsigned int alignment, unsigned int size)
{
    return aligned_alloc(alignment, size);
}

void *qm_malloc(unsigned int size)
{
    void *data =  NULL;
    #if CONFIG_SPIRAM_BOOT_INIT
        data = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    #else
        data = malloc(size);
    #endif
        return data;
}

void *qm_realloc(void *mem, unsigned int size)
{
    void *p = NULL;

    #if CONFIG_SPIRAM_BOOT_INIT
        p = heap_caps_realloc(mem, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    #else
        p = heap_caps_realloc(mem, size, MALLOC_CAP_8BIT);
    #endif

        return p;
}

void qm_free(void *mem)
{
    free(mem);
}

#endif

void *qm_psram_malloc(unsigned int size)
{
#if CONFIG_SPIRAM_BOOT_INIT
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    (void)size;
    return NULL;
#endif
}

void *qm_psram_calloc(unsigned int nitems, unsigned int size)
{
    void *data = NULL;

#if CONFIG_SPIRAM_BOOT_INIT
    data = heap_caps_malloc(nitems * size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (data != NULL) {
        memset(data, 0, nitems * size);
    }
#else
    (void)nitems;
    (void)size;
#endif

    return data;
}

void *qm_psram_realloc(void *mem, unsigned int size)
{
#if CONFIG_SPIRAM_BOOT_INIT
    return heap_caps_realloc(mem, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    (void)mem;
    (void)size;
    return NULL;
#endif
}

uint32_t qm_free_mem_get(void)
{
    return esp_get_free_heap_size();
}

uint32_t qm_now_ms(void)
{
    return ((esp_timer_get_time() / 1000) & 0xFFFFFFFF);
}

int qm_msleep(int ms)
{
    vTaskDelay(ms/portTICK_PERIOD_MS);
    return 0;
}

int qm_usleep(int us)
{
    ets_delay_us(us);
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
    qm_mutex_new(&g_timer_info_lock);
}



