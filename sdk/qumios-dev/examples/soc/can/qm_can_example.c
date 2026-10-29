#include "qm.h"
#include "qm_log.h"
#include "qm_can.h"
#include "qm_platform.h"

#include "main.h"

#define LOG_TAG "app"

#define CAN_TX_PIN  (QM_GPIO_PIN_47)
#define CAN_RX_PIN  (QM_GPIO_PIN_48)

qm_task_t can_trans_task = {0};
qm_task_t can_recv_task = {0};

static qm_can_dev_t can_dev = {
    .port = 0,
    .filter_config = QM_CAN_FILTER_CONFIG_ACCEPT_ALL(),
    .can_config = QM_CAN_CONFIG_DEFAULT(QM_CAN_MODE_NORMAL, CAN_TX_PIN, CAN_RX_PIN, QM_CAN_CLOCK_1MHZ),
};


static void can_trans_yield(void *arg)
{
    int ret = 0;
    while (1)
    {
        qm_can_message_t can_messag = {0};
        ret = qm_can_read(&can_dev, &can_messag, QM_WAIT_FOREVER);
        if(ret != QM_EOK){
            continue;
        }
        QM_LOGD(LOG_TAG, "FD CAN RX ID %04x", can_messag.identifier);
        QM_HEX_LOGD(LOG_TAG, "FD CAN RX", can_messag.data, can_messag.data_length);
    }
    
}

static void can_recv_yield(void *arg)
{
    int ret = 0;
    qm_can_message_t can_messag = {
      .data = {1,2,3,4,5,6,7,8},
      .data_length = 8,
      .identifier = 0x101,
    };
    while (1)
    {
        qm_msleep(1000);
        QM_HEX_LOGD(LOG_TAG, "FD CAN TX", can_messag.data, can_messag.data_length);
        qm_can_write(&can_dev, &can_messag, 0);
    }
    
}

void qm_application_start(void)
{
    int ret = QM_EOK;
    QM_LOGD(LOG_TAG, "hello world\n");
    
    ret = qm_can_init(&can_dev);
    if (ret != QM_EOK){
        QM_LOGE(LOG_TAG, "can register init failed");
        return;
    }

    qm_task_new(&can_trans_task, "can_trans", can_trans_yield, NULL, 4096, 20);
    qm_task_new(&can_recv_task, "can_recv", can_recv_yield, NULL, 4*1024, 4);
}

