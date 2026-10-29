#include "qm.h"
#include "prodtst_client.h"

#if CONFIG_PRODTST_CLIENT_UART_SUPPORT

#include "qm_at.h"
#include "qm_uart.h"

#if !CONFIG_QM_AT_SERVER_OS_SUPPORT
char *recv_buff = NULL;
#endif

static void *prodtst_client_uart_init(void);
static int prodtst_client_uart_deinit(void *handle);
static int prodtst_client_uart_send(void *handle, char *buf, int size);
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
static int prodtst_client_uart_recv(void *handle, char *buf, int size, int timeout);
#else
extern void *prodtst_client_handle_get(prodtst_client_channel_t channel);
#endif

prodtst_client_link_t prodtst_client_uart_link_ctx = 
{
    .init   = prodtst_client_uart_init,
    .write  = prodtst_client_uart_send,
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    .read   = prodtst_client_uart_recv,
#endif
    .deinit = prodtst_client_uart_deinit,
};

static qm_uart_dev_t uart_dev = {
    .config = {
        .baud_rate = CONFIG_PRODTST_CLIENT_UART_BAUDRATE,
        .data_bits = QM_UART_DATA_8_BITS,
        .parity    = QM_UART_PARITY_NONE,
        .pins.tx_pin = CONFIG_PRODTST_CLIENT_UART_TX_PIN,
        .pins.rx_pin = CONFIG_PRODTST_CLIENT_UART_RX_PIN,
        .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = CONFIG_PRODTST_CLIENT_UART_PORT,
};

#if !CONFIG_QM_AT_SERVER_OS_SUPPORT
static void prodtst_channel_read_callback(qm_uart_dev_t *uart, uint32_t size)
{
    int recv_len = 0;
    void *handle = prodtst_client_handle_get(PRODTST_CLIENT_CHANNEL_TYPE_UART);

    memset(recv_buff, 0, CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN);
    recv_len = qm_uart_read(uart, recv_buff, size, 0);
    if(recv_len > 0){
        prodtst_client_data_push(handle, recv_buff, recv_len);
    }
}
#endif


void *prodtst_client_uart_init(void)
{
    int ret = QM_EOK;
    ret = qm_uart_init(&uart_dev);
    if (ret != QM_EOK){
        return NULL;
    }
#if !CONFIG_QM_AT_SERVER_OS_SUPPORT
    recv_buff = (char *)qm_malloc(CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN);
    if(recv_buff == NULL){
        qm_uart_deinit(&uart_dev);
        return NULL;
    }
    memset(recv_buff, 0, CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN);
    qm_uart_ctrl(&uart_dev, QM_UART_CTRL_CMD_READ_CALLBACK, prodtst_channel_read_callback);
#endif
    return (void *)&uart_dev; 
}

static int prodtst_client_uart_send(void *handle, char *buf, int size)
{
    qm_uart_dev_t *uart = (qm_uart_dev_t *)handle;
    if(handle == NULL || buf == NULL || size == 0){
        return -QM_EINVAL;
    }

    return qm_uart_write(uart, buf, (uint32_t)size, 0);
}

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
static int prodtst_client_uart_recv(void *handle, char *buf, int size, int timeout)
{
    qm_uart_dev_t *uart = (qm_uart_dev_t *)handle;
    if(handle == NULL || buf == NULL || size == 0){
        return 0;
    }
    return qm_uart_read(uart, buf, (uint32_t)size, (uint32_t)timeout);
}
#endif

int prodtst_client_uart_deinit(void *handle)
{
    qm_uart_dev_t *uart = (qm_uart_dev_t *)handle;

    if(handle == NULL){
        return -QM_EINVAL;
    }
    return  qm_uart_deinit(uart);
}

#endif
