#include "qm.h"

#include "malloc.h"
#include "miio_user_api.h"

#if CONFIG_QM_NOOS_SUPPORT
#include "qm_noos.h"
#endif

void qm_reboot(void)
{
    miio_system_reboot();
}

const char *qm_version_get(void)
{
    return "V1.0.0";
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

uint32_t qm_free_mem_get(void)
{
    struct mallinfo info;
    info = mallinfo();
    return info.fordblks;
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
    return QM_EOK;
}

int qm_usleep(int us)
{
    return QM_EOK;
}

void qm_init(void)
{
#if CONFIG_QM_NOOS_SUPPORT
    qm_noos_init();
#endif
}

int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int  rc = 0;
    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);

    return rc;
}

extern void vsyslog(int priority, FAR const IPTR char *fmt, va_list ap);

int qm_printf(const char * fmt, ...)
{      
    va_list ap;

    /* Let vsyslog do the work */

    va_start(ap, fmt);
    vsyslog(6, fmt, ap);
    va_end(ap);
    return 0;
}

#if !CONFIG_MULTI_TIMER_SUPPORT

typedef struct{
    struct wdog_s wdog;
    qm_timer_cb_t cb;
    int repeat;
    int is_active;
    uint32_t timeout;
    void *arg;
    qm_timer_t *qm_timer;
    struct mible_task_item post;
}qm_timer_handle_t;

static void timer_callback(void *param);

static void user_timer_expire(wdparm_t arg)
{
    qm_timer_handle_t *timer = (qm_timer_handle_t *)arg;

    mible_single_task_post(&timer->post, timer_callback, (void *)arg);
}

static void timer_callback(void *param)
{
    qm_timer_handle_t *timer_handle = (qm_timer_handle_t *)param;  

    if (!timer_handle->cb) {
        k_free(timer_handle);
        return;
    }

    if(timer_handle->repeat && timer_handle->timeout && timer_handle->is_active){
        wd_start(&timer_handle->wdog, timer_handle->timeout, user_timer_expire, (wdparm_t)timer_handle);
    }else{
        timer_handle->is_active = QM_FALSE;
    }

    if(timer_handle->cb){
        timer_handle->cb(timer_handle->qm_timer, timer_handle->arg);
    }
}

int qm_timer_is_valid(qm_timer_t *timer)
{
    return (timer && timer->hdl != NULL);
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb, void *arg, int ms, int repeat)
{          
    qm_timer_handle_t *timer_handle = NULL;  

    if(timer == NULL || cb == NULL || ms == 0){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)k_aligned_alloc(8, sizeof(qm_timer_handle_t));
    if(timer_handle == NULL){
        return -QM_ENOMEM;
    }
    memset(timer_handle, 0, sizeof(qm_timer_handle_t));

    timer_handle->arg = arg;
    timer_handle->cb = cb;
    timer_handle->timeout = K_MSEC((uint32_t)ms).ticks;
    timer_handle->repeat = repeat;
    timer_handle->qm_timer = timer;

    timer->hdl = (void*)timer_handle;

    return QM_EOK;
}

int qm_timer_free(qm_timer_t *timer)
{
    int ret = QM_EOK;
    qm_timer_handle_t *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }
    timer_handle = (qm_timer_handle_t *)timer->hdl;

    wd_cancel(&timer_handle->wdog);
    /* race condition, defer free */
    if (atomic_test_bit(&timer_handle->post.flags, MIBLE_TASK_USED)) {
        printk("timer pending, defer free\n");
        timer_handle->cb = NULL;
        return 0;
    }

    qm_free(timer->hdl);
    timer->hdl = NULL;

    return QM_EOK;
}

int qm_timer_start_from_isr(qm_timer_t *timer)
{
    return -QM_EINVAL;
}

int qm_timer_start(qm_timer_t *timer)
{
    qm_timer_handle_t *timer_handle = NULL;
 
    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;

    if(timer_handle->is_active){
        return -QM_EINVAL;
    }

    timer_handle->is_active = QM_TRUE;
    wd_start(&timer_handle->wdog, timer_handle->timeout, user_timer_expire, (wdparm_t)timer_handle);
    return QM_EOK;
}

int qm_timer_stop(qm_timer_t *timer)
{
    qm_timer_handle_t *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;

    if(!timer_handle->is_active){
        return -QM_EINVAL;
    }

    timer_handle->is_active = QM_FALSE;
    wd_cancel(&timer_handle->wdog);

    return QM_EOK;
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    qm_timer_handle_t *timer_handle = NULL;  

    if(timer == NULL || timer->hdl == NULL){
        return -QM_EINVAL;
    }

    timer_handle = (qm_timer_handle_t *)timer->hdl;

    timer_handle->timeout = K_MSEC((uint32_t)ms).ticks;

    if(timer_handle->is_active){
        wd_cancel(&timer_handle->wdog);
    }
    timer_handle->is_active = QM_TRUE;
    wd_start(&timer_handle->wdog, timer_handle->timeout, user_timer_expire, (wdparm_t)timer_handle);

    return QM_EOK;
}

#endif

