#include "qm.h"
#include "comm_base.h"
#include "qm_log.h"
#include "qm_usb.h"
#include "qm_work.h"

#define LOG_TAG "comm base"

int comm_base_send(uint8_t *data, int len)
{
    qm_usb_cdcacm_write(QM_USB_CDCACM_0, data, len, 100);
    QM_HEX_LOGD(LOG_TAG, "usb send: ", data, len);
    return len;
}

/*
 *   -------------------------------------------------------------------------
 *  |  HEAD  |  VER  |  CMD   |   LENGTH |    ID    |  PAYLOAD    |  CHKSUM  |
 *   ------------------------------------------------------------------------
 *    2Bytes   1Byte    1Byte     2Bytes    1Byte     n Byte        1Byte
 */

int comm_base_id_get(uint8_t *data, int len)
{
    return (int)*(data + 6);
}

int comm_base_cmd_get(uint8_t *data, int len)
{
    return (int)*(data + 3);
}

int comm_base_check(uint8_t *data, int len)
{
    uint16_t i;
    uint8_t check_sum = 0;
    for (i = 0; i < len - 1; i++){
        check_sum += data[i];
    }
    if(check_sum == *(data + len - 1)){
        QM_LOGD(LOG_TAG, "check success");
        return 0;
    }else{
        QM_LOGD(LOG_TAG, "check fail");
        return 1;
    }
}

int comm_base_notify(comm_base_event_info_t *event_info)
{
    if(event_info->event == COMM_BASE_EVENT_RESEND){
        QM_HEX_LOGD(LOG_TAG, "resend: ", event_info->info.data, event_info->info.len);
    }else if(event_info->event == COMM_BASE_EVENT_NO_ACK){
        QM_LOGD(LOG_TAG, "no ack, cmd: %d, id: %d", event_info->info.cmd, event_info->info.id);
    }
    return QM_EOK;
}

static uint8_t buffer[64] = {0};

static void usb_cdcacm_callback(qm_usb_cdcacm_port_t cdcacm_port, qm_usb_cdcacm_event_t *event)
{
    int recv_len = 0;
    if(event->type == QM_USB_CDC_EVENT_RX){
        recv_len = qm_usb_cdcacm_read(cdcacm_port, buffer, 64);
        if(recv_len){
            buffer[recv_len] = '\0';
            QM_HEX_LOGD(LOG_TAG, "usb recv: ", buffer, recv_len);
            comm_base_data_push(buffer, recv_len);
        }
    }
}

static int set_unpack(uint8_t *data, int len)
{
    QM_LOGD(LOG_TAG, "set unpack");
    return QM_EOK;
}

static int set_pack(uint8_t *data, int *len, void *arg)
{
    char set[] = {0xA5, 0xA5, 0x03, 0x01, 0x00, 0x04, 0x01, 0x00, 0x01, 0x00, 0x54};
    QM_LOGD(LOG_TAG, "set pack");
    if(data == NULL){
        *len = sizeof(set);
        return QM_EOK;
    }
    memcpy(data, set, *len);
    return QM_EOK;
}

static int report_unpack(uint8_t *data, int len)
{
    QM_LOGD(LOG_TAG, "report unpack");
    return QM_EOK;
}

static int report_pack(uint8_t *data, int *len, void *arg)
{
    char report_ack[] = {0xA5, 0xA5, 0x03, 0x10, 0x00, 0x00, 0x5D};
    /* ack: A5 A5 03 01 00 02 01 00 51*/
    QM_LOGD(LOG_TAG, "report ack pack");
    if(data == NULL){
        *len = sizeof(report_ack);
        return QM_EOK;
    }
    memcpy(data, report_ack, *len);
    return QM_EOK;
}

int recv_len_get_1(void)
{
    QM_LOGD(LOG_TAG, "step 1");
    return 1;
}

int recv_check_1(uint8_t *data, int len)
{
    if(*(data) == 0xA5){
        QM_LOGD(LOG_TAG, "step 1 check success");
        return QM_EOK;
    }else{
        QM_LOGD(LOG_TAG, "step 1 check fail");
        return -QM_ERROR;
    }
}

int recv_len_get_2(void)
{
    QM_LOGD(LOG_TAG, "step 2");
    return 1;
}

int recv_check_2(uint8_t *data, int len)
{
    if(*(data) == 0xA5){
        QM_LOGD(LOG_TAG, "step 2 check success");
        return QM_EOK;
    }else{
        QM_LOGD(LOG_TAG, "step 2 check fail");
        return -QM_ERROR;
    }
}

int recv_len_get_3(void)
{
    QM_LOGD(LOG_TAG, "step 3");
    return 1;
}

int recv_check_3(uint8_t *data, int len)
{
    if(*(data) == 0x03){
        QM_LOGD(LOG_TAG, "step 3 check success");
        return QM_EOK;
    }else{
        QM_LOGD(LOG_TAG, "step 3 check fail");
        return -QM_ERROR;
    }
}

int recv_len_get_4(void)
{
    QM_LOGD(LOG_TAG, "step 4");
    return 3;
}

static int recv_len = 0;

int recv_check_4(uint8_t *data, int len)
{
   recv_len = *(data + 2);
   QM_LOGD(LOG_TAG, "step 4 check success");
   return QM_EOK;
}

int recv_len_get_5(void)
{
    QM_LOGD(LOG_TAG, "step 5");
    return recv_len + 1;
}

int recv_check_5(uint8_t *data, int len)
{
    QM_LOGD(LOG_TAG, "step 5 check success");
    return QM_EOK;
}

static void *handle = NULL;

static void timeout_callback(void *arg)
{
    comm_base_cmd_send(0x01, 1, 500, NULL);
    qm_post_delayed_action(&handle, timeout_callback, NULL, 1000);
}

void qm_application_start(void)
{
    qm_err_t ret = QM_EOK;
    comm_base_param_t param = {
        .recv_timeout = 200,
        .send = comm_base_send,
        .id_get = comm_base_id_get,
        .cmd_get = comm_base_cmd_get,
        .check = comm_base_check,
        .notify = comm_base_notify
    };

    qm_usb_cdcacm_config_t cfg = {
        .usb_port = QM_USB_0,
        .cdc_port = QM_USB_CDCACM_0,
        .rx_buf_sz = 64,
        .callback_rx = usb_cdcacm_callback,
    };

    ret = qm_usb_cdcacm_init(&cfg);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "usb cacacm init fail");
        return;
    }

    ret = comm_base_init(&param);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "comm base init");
        return;
    }

    comm_base_cmd_register(0x01, COMM_BASE_DIR_DOWN, set_pack, set_unpack);
    comm_base_cmd_register(0x10, COMM_BASE_DIR_UP, report_pack, report_unpack);

    comm_base_recv_register(1, recv_len_get_1, recv_check_1);
    comm_base_recv_register(2, recv_len_get_2, recv_check_2);
    comm_base_recv_register(3, recv_len_get_3, recv_check_3);
    comm_base_recv_register(4, recv_len_get_4, recv_check_4);
    comm_base_recv_register(5, recv_len_get_5, recv_check_5);

    qm_msleep(6000);

    qm_post_delayed_action(&handle, timeout_callback, NULL, 1000);
   
}
