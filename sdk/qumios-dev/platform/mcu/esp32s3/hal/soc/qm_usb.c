#include "qm.h"


#if CONFIG_QM_USB_SUPPORT
#include "tinyusb.h"
#include "tusb_cdc_acm.h"
#include "qm_errno.h"
#include "esp_err.h"
static qm_usb_cdcacm_config_t qm_usb_cdcacm_config = {0};

static void tinyusb_cdc_rx_callback(int itf, cdcacm_event_t *event)
{
    size_t rx_size = 0;
    qm_usb_cdcacm_event_t cdcacm_event = {0};
    if(event->type == CDC_EVENT_RX){
        cdcacm_event.type = QM_USB_CDC_EVENT_RX;
        qm_usb_cdcacm_config.callback_rx((qm_usb_cdcacm_port_t)itf, &cdcacm_event);
    }
}

int qm_usb_cdcacm_init(qm_usb_cdcacm_config_t *cfg)
{
    esp_err_t err = ESP_OK;
    tinyusb_config_t tusb_cfg = {}; 
    err = tinyusb_driver_install(&tusb_cfg);
    if(err != ESP_OK){
        return -QM_ERROR;
    }

    tinyusb_config_cdcacm_t acm_cfg = {
        .usb_dev = cfg->usb_port,
        .cdc_port = cfg->cdc_port,
        .rx_unread_buf_sz = cfg->rx_buf_sz,
        .callback_rx = &tinyusb_cdc_rx_callback, 
        .callback_rx_wanted_char = NULL,
        .callback_line_state_changed = NULL,
        .callback_line_coding_changed = NULL
    };

    err = tusb_cdc_acm_init(&acm_cfg);
    if(err != ESP_OK){
        return -QM_ERROR;
    }

    memcpy(&qm_usb_cdcacm_config, cfg, sizeof(qm_usb_cdcacm_config_t)); 
    return QM_EOK;
}

int qm_usb_cdcacm_register_callback(qm_usb_cdcacm_port_t port, qm_usb_cdcacm_event_type_t event_type, qm_usb_cdcacm_callback_t callback)
{
    return QM_EOK;
}

int qm_usb_cdcacm_write(qm_usb_cdcacm_port_t port, const uint8_t *in_buf, int in_size, uint32_t timeout)
{
    esp_err_t err = ESP_OK;
    size_t write_len = 0;
    if(port != QM_USB_CDCACM_0 || in_buf == NULL || in_size == 0){
        return -QM_EINVAL;
    }
    write_len = tinyusb_cdcacm_write_queue((tinyusb_cdcacm_itf_t)port, in_buf, in_size);
    err = tinyusb_cdcacm_write_flush((tinyusb_cdcacm_itf_t)port, timeout);
    if(err == ESP_OK){
        return QM_EOK;
    }else if(err == ESP_ERR_TIMEOUT){
        return -QM_ETIMEOUT;
    }else{
        return -QM_ERROR;
    }
}

int qm_usb_cdcacm_read(qm_usb_cdcacm_port_t port, uint8_t *out_buf, int out_size)
{   
    size_t rx_size = 0;
    esp_err_t err = ESP_OK;
    if(port != QM_USB_CDCACM_0 || out_buf == NULL || out_size == 0){
        return -QM_EINVAL;
    }

    err = tinyusb_cdcacm_read((tinyusb_cdcacm_itf_t)port, out_buf, out_size, &rx_size);
    if (err == ESP_OK) {
        return (int)rx_size;
    }else{
        return -QM_ERROR;
    }
}

#endif
