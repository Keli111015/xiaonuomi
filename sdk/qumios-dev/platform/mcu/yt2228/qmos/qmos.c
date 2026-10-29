#include "qm.h"
#include "multi_timer.h"
#include "os/os_api.h"
#include "timer.h"

#include "jiffies.h"

#include "qm_utils_list.h"
#include "qm_utils_timer.h"


#define ms2tick(ms) ((ms)/portTICK_PERIOD_MS)

OS_MUTEX qm_noos_mutex = {0};
static int xjian_thread_handler = 0;

void qm_reboot(void)
{
    p33_soft_reset();
}

const char *qm_version_get(void)
{
    return "1.0.0";
}
#if 0
typedef struct{
    qm_timer_t *qm_timer;
    void *arg;
    qm_timer_cb_t cb;
    int ms;
    int repeat;
    int id;
}qm_timer_handle;

static void timer_callback(void* arg)
{
    qm_timer_handle *timer_handle = (qm_timer_handle* )arg;  
    if(timer_handle == NULL){
        return;
    }

    os_mutex_pend(&qm_noos_mutex, 0);
    if(!timer_handle->repeat){
        sys_timer_del(timer_handle->id);
        timer_handle->id = 0;
    }

    timer_handle->cb(timer_handle->qm_timer, timer_handle->arg);
    os_mutex_post(&qm_noos_mutex);
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

    timer_handle->ms = ms;
    timer_handle->repeat = repeat;

    timer_handle->arg = arg;
    timer_handle->cb = cb;
    timer_handle->qm_timer = timer;

    timer->hdl = (void*)timer_handle;

    return QM_EOK;
}

int qm_timer_free(qm_timer_t *timer)
{
    int ret = QM_EOK;
    qm_timer_handle *timer_handle = NULL;  
    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle *)timer->hdl;

    if(timer_handle->id){
        sys_timer_del(timer_handle->id);
        timer_handle->id = 0;
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
    if(timer_handle->id == 0){
        timer_handle->id = sys_timer_add(timer_handle, timer_callback, timer_handle->ms);
    }

    return QM_EOK;
}

int qm_timer_stop(qm_timer_t *timer)
{
    qm_timer_handle *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle *)timer->hdl;

    if(timer_handle->id){
        sys_timer_del(timer_handle->id);
        timer_handle->id = 0;
    }
    
    return QM_EOK;
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    qm_timer_handle *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle *)timer->hdl;

    timer_handle->ms = ms;

    if(timer_handle->id){
        sys_timer_modify(timer_handle->id, ms);
        sys_timer_re_run(timer_handle->id);
    }else{
        timer_handle->id = sys_timer_add(timer_handle, timer_callback, timer_handle->ms);
    }

    return QM_EOK;
}
#endif

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

uint32_t qm_free_mem_get(void)
{
    return xPortGetFreeHeapSize();
}

uint32_t qm_now_ms(void)
{
    return (uint32_t)(timer_get_ms() & 0xFFFFFFFF);
}

int qm_msleep(int ms)
{ 
    qm_err_t ret = QM_EOK;

    if(ms < 10){
        ms = 10;
    }
    os_time_dly(ms / 10);

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

#define LOG_BUFFER_MAX_LEN  256

int qm_printf(const char * fmt, ...)
{
    char buffer[LOG_BUFFER_MAX_LEN] = {0};
    uint16_t temp = 0;
    va_list ParamList;
    va_start(ParamList, fmt);
    vsnprintf(buffer, LOG_BUFFER_MAX_LEN, fmt, ParamList);
    va_end(ParamList);    

    log_print(__LOG_INFO,NULL,"%s", buffer);
    return QM_EOK;
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



static void queue_callback(void* arg)
{
    os_mutex_pend(&qm_noos_mutex, 0);
    qm_os_yield();
    os_mutex_post(&qm_noos_mutex);
}

void qm_init(void)
{
    os_mutex_create(&qm_noos_mutex);
    xjian_thread_handler = sys_timer_add(NULL, queue_callback, 50);
}


