#include "prodtst.h"
#include "prodtst_path.h"
#include "prodtst_data.h"
#include "prodtst_res.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "prodtst_common.h"
#include "qm_kernel.h"
#include "threadpool.h"
#include "prodtst_at.h"

#ifndef CONFIG_PRODTST_THREADPOOL_COUNT
#define CONFIG_PRODTST_THREADPOOL_COUNT   CONFIG_PRODTST_TEST_CHANNEL_MAX_NUM
#endif

#ifndef CONFIG_PRODTST_THREADPOOL_QUEUE_SIZE
#define CONFIG_PRODTST_THREADPOOL_QUEUE_SIZE  2
#endif

#define LOG_TAG "prodtst"

typedef struct{
    qm_task_t task;
    qm_queue_t queue;
    void *threadpool;
}prodtst_ctx_t;

static prodtst_ctx_t g_prodtst_ctx = {0};

static void prodtst_process_handle(void *arg)
{
    qm_err_t ret = QM_EOK;
    prodtst_data_info_t data_info = {0};
    prodtst_data_point_t *data_point  = (prodtst_data_point_t*)arg;
    if(data_point == NULL){
        return;
    }

    memset(&data_info, 0, sizeof(prodtst_data_info_t));

    data_info.code = data_point->code;

    ret = prodtst_data_unpack(data_point->msg_type, data_point->data, data_point->len, &data_info);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "prodtst data unpack error");
        goto __exit;
    }

    ret = prodtst_data_handle(&data_info);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "prodtst data handle error");
        goto __exit;
    }

    ret = prodtst_data_transmit(&data_info);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "prodtst data transmit error");
        goto __exit;
    }

__exit:
    ret = prodtst_data_destroy(&data_info);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "prodtst data destroy error");
    }

    if(data_point->data){
        qm_free(data_point->data);
        data_point->data = NULL;
    }

    qm_free(data_point);
}

static void prodtst_core_task(void *arg)
{
    int ret = QM_EOK;
    uint32_t size = 0;
    prodtst_data_point_t tmp_data_point = {0};
    prodtst_data_point_t *data_point = NULL;
    prodtst_ctx_t *prodtst_ctx = (prodtst_ctx_t*)arg;

    while(1){
        ret = qm_queue_recv(&prodtst_ctx->queue, &tmp_data_point, &size, QM_WAIT_FOREVER);
        if(ret != QM_EOK){
            break;
        }

        data_point = (prodtst_data_point_t*)qm_malloc(sizeof(prodtst_data_point_t));
        if(data_point == NULL){
            continue;
        }

        memcpy(data_point, &tmp_data_point, sizeof(prodtst_data_point_t));

        threadpool_add(prodtst_ctx->threadpool, prodtst_process_handle, (void*)data_point);
    }

    qm_task_exit(&prodtst_ctx->task);
}

int prodtst_data_notify(prodtst_comm_msg_type_t msg_type, int code, uint8_t *data, int len)
{
    prodtst_data_point_t data_point = {0};

    data_point.msg_type = msg_type;
    data_point.code = code;

    if(data != NULL && len != 0){
        data_point.data = (uint8_t*)qm_malloc(len);
        if(data_point.data == NULL){
            return -QM_ENOMEM;
        }

        memcpy(data_point.data, data, len);
        data_point.len = len;
    }

    qm_queue_send(&g_prodtst_ctx.queue, &data_point, sizeof(prodtst_data_point_t));

    return QM_EOK;
}

int prodtst_data_notify_from_isr(prodtst_comm_msg_type_t msg_type, int code, uint8_t *data, int len)
{
    prodtst_data_point_t data_point = {0};

    data_point.msg_type = msg_type;
    data_point.code = code;

    if(data != NULL && len != 0){
        data_point.data = (uint8_t*)qm_malloc(len);
        if(data_point.data == NULL){
            return -QM_ENOMEM;
        }

        memcpy(data_point.data, data, len);
        data_point.len = len;
    }

    qm_queue_send_from_isr(&g_prodtst_ctx.queue, &data_point, sizeof(prodtst_data_point_t));

    return QM_EOK;
}

int prodtst_result_report(uint8_t case_id, uint8_t mac[6], uint8_t *data, int len)
{
    prodtst_result_report_info_t result_report_info = {0};
    result_report_info.case_id = case_id;
    memcpy(result_report_info.unique.mac, mac, sizeof(result_report_info.unique.mac));

    if(data && len > 0){
        result_report_info.data = (uint8_t*)qm_malloc(len + 1);
        if(result_report_info.data == NULL){
            return -QM_ENOMEM;
        }
        memset(result_report_info.data, 0, len + 1);
        memcpy(result_report_info.data, data, len);
    }

    return prodtst_data_notify(PRODTST_COMM_MSG_TYPE_REPORT, PRODTST_COMM_CODE_SUCCESS, (uint8_t*)&result_report_info, sizeof(prodtst_result_report_info_t));
}

int prodtst_init(void)
{
    qm_err_t ret = QM_EOK;
    ret = qm_queue_new(&g_prodtst_ctx.queue, PRODTST_CORE_QUEUE_MAX_LEN, sizeof(prodtst_data_point_t)); 
    if(ret != QM_EOK){
        return ret;
    }

    g_prodtst_ctx.threadpool = threadpool_create(CONFIG_PRODTST_THREADPOOL_COUNT, CONFIG_PRODTST_THREADPOOL_QUEUE_SIZE);
    if(g_prodtst_ctx.threadpool == NULL){
        qm_queue_free(&g_prodtst_ctx.queue);
        return ret;
    }

    ret = qm_task_new(&g_prodtst_ctx.task, "prodtst core", prodtst_core_task, (void*)&g_prodtst_ctx, CONFIG_PRODTST_CORE_TASK_SIZE, CONFIG_PRODTST_CORE_TASK_PRIO);
    if(ret != QM_EOK){
        qm_queue_free(&g_prodtst_ctx.queue);
        threadpool_destroy(g_prodtst_ctx.threadpool, THREADPOOL_DESTORY_IMMEDIATE);
        return ret;
    }

    prodtst_at_init();

    return ret;
}


