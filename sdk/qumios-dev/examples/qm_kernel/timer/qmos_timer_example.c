#include "qm.h"
#define LOG_TAG "TEST"

qm_timer_t timer_loop = {0};
qm_timer_t timer_test[8] = {0};


static void qmos_timer_test_callback(qm_timer_t *timer, void *arg)
{
	QM_LOGD(LOG_TAG,"HELLO_WORLD ID :%d !!!\r\n", (int)arg);
}

static void qmos_timer_test_config(uint8_t run)
{
    int id = 0;
    int ret = 0;

    for(id = 0; id < 8; id++)
    {
        if(run){
            qm_timer_start(&timer_test[id]);
        }else{
            qm_timer_stop(&timer_test[id]);
        }
    }
}


static void qmos_timer_loop_callback(qm_timer_t *timer, void *arg)
{
    static uint8_t timer_run_or_stop = 0;
    if(timer_run_or_stop){
        QM_LOGD(LOG_TAG,"START QMOS TIMER !!!\r\n");
        timer_run_or_stop = 0;
        qmos_timer_test_config(1);
        qm_timer_change(timer, 10000);

    }else{
        QM_LOGD(LOG_TAG,"STOP QMOS TIMER !!!\r\n");
        timer_run_or_stop = 1;
        qmos_timer_test_config(0);
        qm_timer_change(timer, 5000);
    }
	
}

static int qmos_test_timer(void)
{
    int ret = 0;
	int id = 0;

    for(id = 0; id < 8; id++)
    {
        ret = qm_timer_new(&timer_test[id],qmos_timer_test_callback,(void *)id, 5 + (id * 100) , 0);
        if(ret != 0){
            QM_LOGE(LOG_TAG, "==========qm_timer_new test error!======%d======\r\n",ret);  
        }
        qm_timer_start(&timer_test[id]);
    }

    ret = qm_timer_new(&timer_loop,qmos_timer_loop_callback, NULL, 10 * 1000 , 0);
    if(ret != 0){
        QM_LOGE(LOG_TAG, "==========qm_timer_new loop error!======%d======\r\n",ret);  
    }
    qm_timer_start(&timer_loop);
    
    return 0;
}


void qm_application_start(void)
{

    QM_LOGD(LOG_TAG, "==========qm_application_start!============\r\n" );  

    qmos_test_timer();
}
