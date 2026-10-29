#include "qm.h"
#include "qm_log.h"
#include "qm_config.h"
#include "qm_at.h"
#include "qm_uart.h"
#include "qm_usb.h"

#define LOG_TAG "at server"

static qm_at_result_t name_query(void *channel);
static qm_at_result_t name_setup(void *channel, const char *args);

static qm_at_server_t *at_server = NULL;

#if CONFIG_EXAMPLE_QM_AT_SERVER_OS_SUPPORT

static qm_uart_dev_t uart = {
    .config = {
            .baud_rate = CONFIG_EXAMPLE_UART_BAUDRATE,
            .data_bits = QM_UART_DATA_8_BITS,
            .parity    = QM_UART_PARITY_NONE,
            .pins.tx_pin = CONFIG_EXAMPLE_UART_TX_PIN,
            .pins.rx_pin = CONFIG_EXAMPLE_UART_RX_PIN,
            .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = CONFIG_EXAMPLE_UART_PORT,
};

#endif

static qm_at_cmd_t at_server_cmd[] = {
    {"AT+NAME", NULL, NULL, name_query, name_setup, NULL},

};

static qm_at_result_t name_query(void *channel)
{
    qm_at_server_printfln(channel, "+NAME: test");
    return QM_AT_RESULT_OK;
}

static qm_at_result_t name_setup(void *channel, const char *args)
{
    char name[16] = {0};
    const char *rsp_expr = "=\"%s\"";
    sscanf(args, rsp_expr, name);
    QM_LOGD(LOG_TAG, "set name: %s");
    return QM_AT_RESULT_OK;
}

#if CONFIG_EXAMPLE_QM_AT_SERVER_OS_SUPPORT

static int32_t channel_send(void *handle, char *buf, int size)
{
    qm_uart_dev_t *uart = (qm_uart_dev_t*)handle;
    return qm_uart_write(uart, buf, (uint32_t)size, 0);
}

static int32_t channel_recv(void *handle, char *buf, int size, int timeout)
{
    qm_uart_dev_t *uart = (qm_uart_dev_t*)handle;
    return qm_uart_read(uart, buf, (uint32_t)size, timeout);
}

#else

static uint8_t buffer[64] = {0};

static int32_t channel_send(void *handle, char *buf, int size)
{
    qm_usb_cdcacm_port_t port = (qm_usb_cdcacm_port_t)handle;
    return qm_usb_cdcacm_write(port, (const uint8_t*)buf, size, 0);
}

static void usb_cdcacm_callback(qm_usb_cdcacm_port_t cdcacm_port, qm_usb_cdcacm_event_t *event)
{
    int recv_len = 0;
    if(event->type == QM_USB_CDC_EVENT_RX){
        recv_len = qm_usb_cdcacm_read(cdcacm_port, buffer, 64);
        if(recv_len){
            buffer[recv_len] = '\0';
            QM_LOGD(LOG_TAG, "usb recv: %s", buffer);
            qm_at_server_data_push(at_server, (char*)buffer, recv_len);
        }
    }
}

#endif

void qm_application_start(void)
{
    qm_at_server_param_t param = {
        .echo_mode = 0,
        .channel_type = AT_CHANNEL_UART,
        .send = channel_send,
        .recv_buf_len = CONFIG_EXAMPLE_RECV_BUF_SIZE,
    #if CONFIG_EXAMPLE_QM_AT_SERVER_OS_SUPPORT
        .recv = channel_recv,
        .handle = &uart
    #else
        .handle = (void*)QM_USB_CDCACM_0 
    #endif
    };

#if CONFIG_EXAMPLE_QM_AT_SERVER_OS_SUPPORT

    qm_uart_init(&uart);   
#else
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
#endif

    at_server = qm_at_server_init(&param);
    qm_at_register_commands(at_server, at_server_cmd, QM_ARRAY_SIZE(at_server_cmd));

}


