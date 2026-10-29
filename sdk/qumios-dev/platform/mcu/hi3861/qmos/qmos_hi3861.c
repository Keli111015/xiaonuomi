#include "qmos/qm.h"
#include "qmos/qm_errno.h"
#include <sys/time.h>

#include "hi_time.h"
#include "hi_task.h"
#include "hi_mux.h"
#include "hi_sem.h"
#include "hi_mem.h"
#include "hi_reset.h"
#include "hi_timer.h"
#include "hi_time.h"
#include "hi_msg.h"

#define QM_DEFAULT_TASK_PRIO   20

void qm_reboot(void)
{
    hi_hard_reboot(HI_SYS_REBOOT_CAUSE_CMD);
}

const char *qm_version_get(void)
{
    return "V1.0.0";
}

typedef struct{
    hi_u32 id;
}hi_info_t;

int qm_task_new(qm_task_t *task, const char *name, void (*fn)(void *), void *arg, int stack_size, int prio)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_u32 task_id = 0;
    hi_task_attr task_attr = {0};
    hi_info_t *hi_info = NULL;
    if(task == NULL){
        return -QM_EINVAL;
    }
    task_attr.stack_size = stack_size;
    task_attr.task_prio = prio;
    task_attr.task_name = (hi_char*)name;

    hi_info = (hi_info_t*)hi_malloc(HI_MOD_ID_APP_COMMON, sizeof(hi_info_t));
    if(hi_info == NULL){
        err = -QM_ENOMEM;
        goto __exit;
    }
    
    ret = hi_task_create(&task_id, &task_attr, fn, arg);   
    if(ret == 0){
        hi_info->id = task_id;
        err = QM_EOK;
    }else{
        err = -QM_ERROR;
    }

    task->hdl = hi_info;
__exit:
    if(err != QM_EOK){
        if(hi_info){
            hi_free(HI_MOD_ID_APP_COMMON, hi_info);
        }
    }

    return err;
}

int qm_task_exit(qm_task_t *task)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = (hi_info_t*)task->hdl;
    if(task == NULL){
        return -QM_EINVAL;
    }
    ret = hi_task_delete(hi_info->id);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    hi_free(HI_MOD_ID_APP_COMMON, hi_info);
    return err;
}

int qm_mutex_new(qm_mutex_t *mutex)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = NULL;
    if(!mutex){
        return -QM_EINVAL;
    }
    hi_info = (hi_info_t*)hi_malloc(HI_MOD_ID_APP_COMMON, sizeof(hi_info_t));
    if(hi_info == NULL){
        err = -QM_ENOMEM;
        goto __exit;
    }

    ret = hi_mux_create(&hi_info->id);
    if (ret != 0) {
         err = -QM_ERROR;
         goto __exit;
    }

    mutex->hdl = hi_info;

__exit:
    if(err != QM_EOK){
        if(hi_info){
            hi_free(HI_MOD_ID_APP_COMMON, hi_info);
        }
    }
    return err;
}

int qm_mutex_free(qm_mutex_t *mutex)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    if (!mutex) {
        return -QM_EINVAL;
    }
    hi_info_t *hi_info = (hi_info_t*)mutex->hdl;
    ret = hi_mux_delete(hi_info->id);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    hi_free(HI_MOD_ID_APP_COMMON, hi_info);
    return err;
}

int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = (hi_info_t*)mutex->hdl;
    if (!mutex) {
        return -QM_EINVAL;
    }
    ret = hi_mux_pend(hi_info->id, ms);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    return err;
}

int qm_mutex_unlock(qm_mutex_t *mutex)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = (hi_info_t*)mutex->hdl;
    if (!mutex) {
        return -QM_EINVAL;
    }
    ret = hi_mux_post(hi_info->id);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    return err;
}

int qm_mutex_is_valid(qm_mutex_t *mutex)
{
    return (mutex != NULL && mutex->hdl != NULL) ? 1 : 0;
}

int qm_sem_new(qm_sem_t *sem, int count)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = NULL;
    if(!sem){
        return -QM_EINVAL;
    }
    hi_info = (hi_info_t*)hi_malloc(HI_MOD_ID_APP_COMMON, sizeof(hi_info_t));
    if(hi_info == NULL){
        err = -QM_ENOMEM;
        goto __exit;
    }

    ret = hi_sem_create(&hi_info->id, count);
    if (ret != 0) {
         err = -QM_ERROR;
         goto __exit;
    }

    sem->hdl = hi_info;

__exit:
    if(err != QM_EOK){
        if(hi_info){
            hi_free(HI_MOD_ID_APP_COMMON, hi_info);
        }
    }
    return err;
}

int qm_sem_free(qm_sem_t *sem)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    if (!sem) {
        return -QM_EINVAL;
    }
    hi_info_t *hi_info = (hi_info_t*)sem->hdl;
    ret = hi_sem_delete(hi_info->id);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    hi_free(HI_MOD_ID_APP_COMMON, hi_info);
    return err;
}

int qm_sem_wait(qm_sem_t *sem, unsigned int ms)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = (hi_info_t*)sem->hdl;
    if (!sem) {
        return -QM_EINVAL;
    }
    ret = hi_sem_wait(hi_info->id, ms);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    return err;
}

int qm_sem_signal(qm_sem_t *sem)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = (hi_info_t*)sem->hdl;
    if (!sem) {
        return -QM_EINVAL;
    }
    ret = hi_sem_signal(hi_info->id);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    return err;
}

int qm_sem_is_valid(qm_sem_t *sem)
{
    return (sem != NULL && sem->hdl != NULL) ? 1 : 0;
}

int qm_queue_new(qm_queue_t *queue, unsigned int queue_len, unsigned int queue_size)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = NULL;
    if(!queue){
        return -QM_EINVAL;
    }
    hi_info = (hi_info_t*)hi_malloc(HI_MOD_ID_APP_COMMON, sizeof(hi_info_t));
    if(hi_info == NULL){
        err = -QM_ENOMEM;
        goto __exit;
    }

    ret = hi_msg_queue_create(&hi_info->id, queue_len, queue_size);
    if (ret != 0) {
         err = -QM_ERROR;
         goto __exit;
    }

    queue->hdl = hi_info;

__exit:
    if(err != QM_EOK){
        if(hi_info){
            hi_free(HI_MOD_ID_APP_COMMON, hi_info);
        }
    }
    return err;
}

int qm_queue_free(qm_queue_t *queue)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    if (!queue) {
        return -QM_EINVAL;
    }
    hi_info_t *hi_info = (hi_info_t*)queue->hdl;
    ret = hi_msg_queue_delete(hi_info->id);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    hi_free(HI_MOD_ID_APP_COMMON, hi_info);
    return err;
}

int qm_queue_send(qm_queue_t *queue, void *msg, unsigned int size)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = (hi_info_t*)queue->hdl;
    if (!queue) {
        return -QM_EINVAL;
    }
    ret = hi_msg_queue_send(hi_info->id, msg, 0, size);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    return err;
}

int qm_queue_recv(qm_queue_t *queue, void *msg, unsigned int *size, unsigned int ms)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    hi_info_t *hi_info = (hi_info_t*)queue->hdl;
    if (!queue) {
        return -QM_EINVAL;
    }
    ret = hi_msg_queue_wait(hi_info->id, msg, ms, size);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    return err;
}


int qm_queue_is_valid(qm_queue_t *queue)
{
    return (queue != NULL && queue->hdl != NULL) ? 1 : 0;
}

typedef struct{
    hi_u32 timer_handle;
    qm_timer_t *timer;
    qm_timer_cb_t cb;
    int repeat;
    int ms;
    void *arg;
}qm_timer_info_t;

static hi_void hi_timer_callback(hi_u32 data)
{
    qm_timer_info_t *timer_info = (qm_timer_info_t*)data;
    if(timer_info->cb){
        timer_info->cb(timer_info->timer, timer_info->arg);
    }
}

int qm_timer_new(qm_timer_t *timer, qm_timer_cb_t cb,
                  void *arg, int ms, int repeat)
{
    int index = 0;
    hi_u32 ret = 0;
    qm_timer_info_t *timer_info = NULL;
    qm_err_t err = QM_EOK;

    index = qm_timer_unused_get();
    if(index < 0){
        return -1;
    }

    timer_info = (qm_timer_info_t*)hi_malloc(HI_MOD_ID_APP_COMMON, sizeof(qm_timer_info_t));
    if(timer_info == NULL){
        err = -QM_ENOMEM;
        goto __exit;
    }

    ret = hi_timer_creat(&timer_info->timer_handle);
    if (ret != 0) {
         err = -QM_ERROR;
         goto __exit;
    }

    timer->hdl = timer_info;

    timer_info->cb = cb;
    timer_info->arg = arg;
    timer_info->repeat = repeat;
    timer_info->ms = ms;
    timer_info->timer = timer;

__exit:
    if(err != QM_EOK){
        if(timer_info){
            hi_free(HI_MOD_ID_APP_COMMON, timer_info);
        }
    }
    return err;
}

int qm_timer_free(qm_timer_t *timer)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    qm_timer_info_t *timer_info = NULL;
    if (!timer) {
        return -QM_EINVAL;
    }
    timer_info = (hi_info_t*)timer->hdl;
    ret = hi_timer_delete(timer_info->timer_handle);
    if(ret == 0){
        err = QM_EOK;
    }else{
        err = -QM_EINVAL;
    }

    hi_free(HI_MOD_ID_APP_COMMON, timer_info);
    return err;
}


int qm_timer_start(qm_timer_t *timer)
{
    hi_u32 ret = QM_EOK;
    hi_timer_type type = 0;
    qm_timer_info_t *timer_info = NULL;
    if (!timer) {
        return -QM_EINVAL;
    }
    timer_info = (hi_info_t*)timer->hdl;

    if(timer_info->repeat){
        type = HI_TIMER_TYPE_PERIOD;
    }else{
        type = HI_TIMER_TYPE_ONCE;
    }

    ret = hi_timer_start(timer_info->timer_handle, type, timer_info->ms, hi_timer_callback, (hi_u32)timer_info);
    if(ret == 0){
        return QM_EOK;
    }else{
        return -QM_EINVAL;
    }
}

int qm_timer_stop(qm_timer_t *timer)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    qm_timer_info_t *timer_info = NULL;
    if (!timer) {
        return -QM_EINVAL;
    }
    timer_info = (hi_info_t*)timer->hdl;
    ret = hi_timer_stop(timer_info->timer_handle);
    if(ret == 0){
        return QM_EOK;
    }else{
        return -QM_EINVAL;
    }
}

int qm_timer_change(qm_timer_t *timer, int ms)
{
    hi_u32 ret = 0;
    qm_timer_info_t *timer_info = NULL;
    if(timer == NULL){
        return -1;
    };

    timer_info = (hi_info_t*)timer->hdl;

    ret = qm_timer_stop(timer);
    if(ret == 0){
        return QM_EOK;
    }else{
        return -QM_EINVAL;
    }

    timer_info->ms = ms;

    ret = qm_timer_start(timer);
    if(ret == 0){
        return QM_EOK;
    }else{
        return -QM_EINVAL;
    }
}

void *qm_malloc(unsigned int size)
{
    return hi_malloc(HI_MOD_ID_APP_COMMON, size);
}

void qm_free(void *mem)
{
    hi_free(HI_MOD_ID_APP_COMMON, mem);
}

long long qm_now_ms(void)
{
    return hi_get_milli_seconds();
}

int qm_msleep(int ms)
{
    hi_u32 ret = 0;
    qm_err_t err = QM_EOK;
    ret = hi_sleep(ms);
    if(ret == 0 ){
        return QM_EOK;
    }else{
        return QM_ERROR;
    }
}

void qm_init(void)
{
    
}


