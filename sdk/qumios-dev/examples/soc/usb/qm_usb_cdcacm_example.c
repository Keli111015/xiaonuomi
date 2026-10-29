#include "qm_usb.h"
#include "qm_log.h"
#include "qm_errno.h"

#define LOG_TAG "usb"

static uint8_t buffer[64] = {0};

static void usb_cdcacm_callback(qm_usb_cdcacm_port_t cdcacm_port, qm_usb_cdcacm_event_t *event)
{
    int recv_len = 0;
    if(event->type == QM_USB_CDC_EVENT_RX){
        recv_len = qm_usb_cdcacm_read(cdcacm_port, buffer, 64);
        if(recv_len){
            buffer[recv_len] = '\0';
            QM_LOGD(LOG_TAG, "usb recv: %s", buffer);
            qm_usb_cdcacm_write(cdcacm_port, buffer, recv_len, 100);
        }
    }
}

void qm_application_start(void)
{
    qm_err_t ret = QM_EOK;
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
}