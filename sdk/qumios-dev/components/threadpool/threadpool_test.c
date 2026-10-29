#include "threadpool.h"
#include "qm_kernel.h"
#include "qm_log.h"
#include "qm_errno.h"

#define LOG_TAG "threadpool"


static void threadpool_test_1(void *arg)
{
    int i = 0;
    QM_LOGD(LOG_TAG, "threadpool test 1: %d", (int)arg);
    while(1){
        QM_LOGD(LOG_TAG, "test 1");
        qm_msleep(1000);
        if(++i == 2){
            break;
        }
    }
}

static void threadpool_test_2(void *arg)
{
    QM_LOGD(LOG_TAG, "threadpool test 2: %d", (int)arg);
}

static void threadpool_test_3(void *arg)
{
    int i = 0;
    QM_LOGD(LOG_TAG, "threadpool test 3: %d", (int)arg);
    while(1){
        QM_LOGD(LOG_TAG, "test 3");
        qm_msleep(2000);
        if(++i == 1){
            break;
        }
    }
}

void threadpool_test(void)
{
    int ret = 0;
    void *handle = NULL;
    int test = 10;
    handle = threadpool_create(2, 1);
    if(handle == NULL){
        QM_LOGE(LOG_TAG, "threadpool create fail");
        return;
    }

    ret = threadpool_add(handle, threadpool_test_1, (void*)test);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "threadpool add fail");
        return;
    }

    ret = threadpool_add(handle, threadpool_test_2, (void*)test);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "threadpool add fail");
        return;
    }

    ret = threadpool_add(handle, threadpool_test_3, (void*)test);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "threadpool add fail");
        return;
    }

    qm_msleep(5000);

    threadpool_destroy(handle, THREADPOOL_DESTORY_IMMEDIATE);

}