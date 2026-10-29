#include "qm.h"
#include "multi_timer.h"
#include "qm_log.h"

#define LOG_TAG  "multi_timer"

static qm_task_t timer_task = {0};

static multi_timer_t timer1;
static multi_timer_t timer2;
static multi_timer_t timer3;

static void timer_yield(void *arg)
{
    uint32_t delay = 0;
    uint32_t total = 0;
    int test = 1;

    while(1){
        delay = multi_timer_yield();
        if(delay){
            qm_msleep(delay);
        }else{
            qm_msleep(50);
        }
        total += delay;
        if(total > 10000 && test){
            test = 0;
            QM_LOGD(LOG_TAG, "timer1 delete");
            multi_timer_delete(&timer1);
            QM_LOGD(LOG_TAG, "timer2 stop");
            multi_timer_stop(&timer2);
            QM_LOGD(LOG_TAG, "timer3 change");
            multi_timer_change(&timer3, 3000);
        }
    }
} 

static void timer1_callback(multi_timer_t *timer, void *arg)
{
    QM_LOGD(LOG_TAG, "timer1 expires");
}

static void timer2_callback(multi_timer_t *timer, void *arg)
{
    QM_LOGD(LOG_TAG, "timer2 expires");
}

static void timer3_callback(multi_timer_t *timer, void *arg)
{
    QM_LOGD(LOG_TAG, "timer3 expires");
}

void qm_application_start(void)
{
    multi_timer_init();

    multi_timer_creat(&timer1, timer1_callback, NULL, 500, 0);
    multi_timer_creat(&timer2, timer2_callback, NULL, 1000, 1);
    multi_timer_creat(&timer3, timer3_callback, NULL, 2000, 1);

    multi_timer_start(&timer1);
    multi_timer_start(&timer2);
    multi_timer_start(&timer3);
    
    qm_task_new(&timer_task, "timer", timer_yield, NULL, 4*1024, 20);

}