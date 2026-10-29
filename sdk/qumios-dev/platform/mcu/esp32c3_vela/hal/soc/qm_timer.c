#include "qm_timer.h"
#include "qm.h"

#if CONFIG_QM_TIMER_SUPPORT

#include <applet.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <nuttx/signal.h>
#include <nuttx/timers/timer.h>
#include <sys/ioctl.h>
#include <timer.h>

#define TIMER_IRQ_ID 100

typedef struct qm_timer_info{
    int tim_fd;
    qm_timer_dev_config_t tim_cvalue;
    void *pre;
}qm_timer_info_t;

qm_timer_info_t *qm_tim_hd = NULL;

static void timer_callback(union sigval value)
{
    int *tim_t = NULL;

    qm_timer_dev_t *tim = NULL;

    if(qm_tim_hd == NULL){
        return;
    } 

    tim = (qm_timer_dev_t *)qm_tim_hd->pre;
    if(tim == NULL)return;

    if(qm_tim_hd->tim_cvalue.reload_mode != TIMER_RELOAD_AUTO)
    {
        qm_timer_dev_stop(tim);
    }

    if(qm_tim_hd->tim_cvalue.cb != NULL)
    {
        qm_tim_hd->tim_cvalue.cb(qm_tim_hd->tim_cvalue.arg);
    }
    
}

static int32_t qm_timer_port_check(qm_timer_dev_t *tim)
{
    if(tim->port != 0)
    {
        return -QM_ERROR;
    }

    return QM_EOK;
}

int32_t qm_timer_dev_init(qm_timer_dev_t *tim)
{
    int ret,tim_fd;

    if(qm_timer_port_check(tim))
    {
        return -QM_ERROR;
    }


    /*********************** 初始化timer ********************/
    timer_config_t config = {
        .id = 1
    };

    /* adc config */
    ret = boardctl(APP_BOARDIOC_TIMER, &config);
    if(ret != 0){
        return -QM_ERROR;
    }


    tim_fd = open("/dev/timer1", O_RDONLY);
    if (tim_fd < 0) {
        return -QM_ERROR;
    }

    qm_tim_hd = (qm_timer_info_t *)qm_malloc(sizeof(qm_timer_info_t));

    if(qm_tim_hd == NULL){
        close(tim_fd);
        return -QM_ENOMEM;
    }

    memset(qm_tim_hd, 0, sizeof(qm_timer_info_t));

    memcpy(&qm_tim_hd->tim_cvalue,&tim->config,sizeof(qm_timer_dev_config_t));
    qm_tim_hd->tim_fd = tim_fd;

    // tim->priv = (void *)&fd;

    return QM_EOK;


}

int32_t qm_timer_dev_start(qm_timer_dev_t *tim)
{
    int ret;
    struct timer_notify_s notify = { 0 };
    long long  interval; //1= 1us


    if(qm_tim_hd == NULL){
        return -QM_ENOMEM;
    }

    if (qm_tim_hd->tim_fd < 0) {
        return -QM_ERROR;
    }

    memset(&qm_tim_hd->tim_cvalue, 0, sizeof(qm_timer_dev_config_t));
    memcpy(&qm_tim_hd->tim_cvalue,&tim->config,sizeof(qm_timer_dev_config_t));
    qm_tim_hd->pre = (void *)tim;

    // interval = qm_tim_hd->tim_cvalue.period;
    if(tim->config.period <= 0){
        interval = 50; 
    }else{
        interval = tim->config.period * 1000; 
    }
    

    /* Set the timer interval */

    ret = ioctl(qm_tim_hd->tim_fd, TCIOC_SETTIMEOUT, interval);
    if (ret < 0) {
        return -QM_ERROR;
    }

    int arg = qm_tim_hd->tim_cvalue.reload_mode;

    notify.irq = TIMER_IRQ_ID;
    notify.event.sigev_notify = SIGEV_THREAD;
    notify.event.sigev_signo  = 17;
    notify.event.sigev_value.sival_int = arg;
    notify.event.sigev_notify_function = timer_callback;

    ret = ioctl(qm_tim_hd->tim_fd, TCIOC_NOTIFICATION, (unsigned long)(&notify));
    if (ret < 0) {
        return -QM_ERROR;
    }

    /* Start the timer */
    // syslog(LOG_INFO,"Start the timer\n");

    ret = ioctl(qm_tim_hd->tim_fd, TCIOC_START, 0);
    if (ret < 0) {
        return -QM_ERROR;
    }
    return QM_EOK;
}

void qm_timer_dev_stop(qm_timer_dev_t *tim)
{
    int ret = 0;
    if(qm_timer_port_check(tim))
    {
        return;
    }

    if(qm_tim_hd == NULL){
        return;
    }

    if (qm_tim_hd->tim_fd < 0) {
        return;
    }

    ret = ioctl(qm_tim_hd->tim_fd, TCIOC_STOP, TIMER_IRQ_ID);
    if (ret < 0) {
        return;
    }

}

int32_t qm_timer_dev_para_change(qm_timer_dev_t *tim, qm_timer_dev_config_t para)
{
    // int ret;

    if(qm_timer_port_check(tim))
    {
        return -QM_ERROR;
    }

    qm_timer_dev_stop(tim);

    memset(&tim->config,0,sizeof(qm_timer_dev_config_t));

    memcpy(&tim->config,&para,sizeof(qm_timer_dev_config_t));

    memcpy(&qm_tim_hd->tim_cvalue,&para,sizeof(qm_timer_dev_config_t));


    qm_timer_dev_start(tim);

    return QM_EOK;


}

int32_t qm_timer_dev_deinit(qm_timer_dev_t *tim)
{
    if(qm_timer_port_check(tim))
    {
        return -QM_ERROR;
    }

    if(qm_tim_hd->tim_fd > 0)
    {
        close(qm_tim_hd->tim_fd);
        return QM_EOK;
    }

    if(qm_tim_hd != NULL)
    {
        qm_free(qm_tim_hd);
        qm_tim_hd = NULL;
    }

    return QM_EOK;
}
#endif