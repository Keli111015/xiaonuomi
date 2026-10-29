#include "qm.h"

#if 1

#include "qm_timer.h"

#include "cpu.h"
#include "user_timer.h"
#include "system/includes.h"

typedef struct qm_timer_info{
    user_tmr_id_e timer_id;
    hal_timer_cb_t  cb;             /**< timer handle when expired */
    void           *arg;            /**< timer handle args */
}qm_timer_info_t;

static qm_timer_info_t g_qm_tim_hd = {0};

AT_VOLATILE_RAM_CODE
static void usr_timer_callback(user_tmr_id_e idx)
{
    g_qm_tim_hd.cb(g_qm_tim_hd.arg);
}

int32_t qm_timer_dev_init(qm_timer_dev_t *tim)
{
    bool one_shot = false;

    if(tim == NULL || tim->port != 0){
        return -QM_EINVAL;
    }

    g_qm_tim_hd.timer_id = USER_TIMER_0;
    g_qm_tim_hd.cb = tim->config.cb;
    g_qm_tim_hd.arg = tim->config.arg;
    one_shot = (tim->config.reload_mode == TIMER_RELOAD_AUTO) ? false : true;

    user_timer_init(&g_qm_tim_hd.timer_id, tim->config.period, one_shot, usr_timer_callback);

    tim->priv = (void *)&g_qm_tim_hd;

    return QM_EOK;


}

int32_t qm_timer_dev_start(qm_timer_dev_t *tim)
{
    qm_timer_info_t *time_info = NULL;
    if(tim == NULL || tim->port != 0){
        return -QM_EINVAL;
    }
    time_info = (qm_timer_info_t *)tim->priv;

    user_timer_start(time_info->timer_id);
    return QM_EOK;
}

void qm_timer_dev_stop(qm_timer_dev_t *tim)
{
    qm_timer_info_t *time_info = NULL;
    if(tim == NULL || tim->port != 0){
        return;
    }
    time_info = (qm_timer_info_t *)tim->priv;

    user_timer_pause(time_info->timer_id);
}

int32_t qm_timer_dev_para_change(qm_timer_dev_t *tim, qm_timer_dev_config_t para)
{
    return -QM_EINVAL;
}

int32_t qm_timer_dev_deinit(qm_timer_dev_t *tim)
{
    qm_timer_info_t *time_info = NULL;
    if(tim == NULL || tim->port != 0){
        return -QM_EINVAL;
    }
    time_info = (qm_timer_info_t *)tim->priv;

    user_timer_pause(time_info->timer_id);

    return QM_EOK;
}
#endif