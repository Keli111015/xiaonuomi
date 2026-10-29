#include "qm_uart.h"
#include "qm.h"

#if 1
#include "qm_log.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"

#include "applet.h"
#include "util.h"
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>

#define LOG_TAG "uart"

#define ESP32C3_STOPBIT1            0
#define ESP32C3_STOPBIT2            1

#define QM_UART1_RECV_MAX_LEN       (128)

typedef struct 
{
    int is_use;
    int fd;
    const char *data;
    int type;
    void *arg;
}qm_uart_info_t;

typedef struct 
{
    qm_sem_t sem;
    qm_mutex_t lock;
    qm_uart_dev_t *uart;
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
    qm_ringbuf_t ringbuf;
    uint8_t recv_buff[QM_UART1_RECV_MAX_LEN];
    uint8_t ringbuff_data[QM_UART1_RECV_MAX_LEN];
}qm_uart_recv_ctx_t;

static qm_uart_info_t  uart_config[] = {
    {0,0,"/dev/ttyp0",O_RDWR | O_NONBLOCK, NULL},
    {0,0,"/dev/ttyS1",O_RDWR, NULL},
};

static int32_t qm_uart0_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout);
static int32_t qm_uart1_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout);

void uart_recv_thread(uint32_t recv_timeout)
{
    int len = 0;
    int recv_len = 0;
    uint32_t t_left = 0;
    struct pollfd fdp = {0};
    qm_utils_time_t time = {0};
    qm_uart_info_t *uart_info = (qm_uart_info_t *)&uart_config[1];
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;

    //防止数据超时时间过长导致后续的multi_timer无法调度
    if(recv_timeout >= 20){
        recv_timeout = 20;
    }

    if(uart_info->arg == NULL){
        qm_msleep(recv_timeout);
        return;
    }

    fdp.events = POLLIN;
    fdp.fd = uart_info->fd;

    t_left = recv_timeout;
    qm_utils_time_countdown_ms(&time, recv_timeout);  

    do{
        recv_len = poll((struct pollfd*)&fdp, 1, t_left);
        if (recv_len < 0) {
            t_left = qm_utils_time_left(&time);
            continue;
        }

        if (fdp.revents == POLLIN){
            recv_len = read(uart_info->fd, recv_ctx->recv_buff + len, QM_UART1_RECV_MAX_LEN);
            if(recv_len > 0){
                len += recv_len;
            }
        }    
        t_left = qm_utils_time_left(&time);       
    }while ((len < QM_UART1_RECV_MAX_LEN) && (t_left > 0));

    if(len > 0){
        qm_mutex_lock(&recv_ctx->lock, QM_WAIT_FOREVER);
        qm_ringbuf_push(&recv_ctx->ringbuf, recv_ctx->recv_buff, len);
        qm_mutex_unlock(&recv_ctx->lock);
        if(recv_ctx->read){
            recv_ctx->read(recv_ctx->uart, len);
        }else{
            qm_sem_signal(&recv_ctx->sem);
        }
    }
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    int ret = 0;
    serial_config_t config = {0};
    qm_uart_info_t *uart_info = NULL;
    qm_uart_recv_ctx_t *recv_ctx = NULL;

    if(uart == NULL){
        return -QM_EINVAL;
    }

    if(uart->port > (QM_ARRAY_SIZE(uart_config) - 1)){
        return -QM_EINVAL;
    }

    uart_info = &uart_config[uart->port];
    if(uart_info->is_use){
        goto __next;
    }

    config.id   = uart->port;
    config.baud = uart->config.baud_rate;
    config.tx   = uart->config.pins.tx_pin;
    config.rx   = uart->config.pins.rx_pin;

    if( uart->config.stop_bits == QM_UART_STOP_BITS_1){
         config.stopbit = ESP32C3_STOPBIT1;
    }else if (uart->config.stop_bits == QM_UART_STOP_BITS_2){
         config.stopbit = ESP32C3_STOPBIT2;
    }
    
    if(uart->port){
        ret = boardctl(APP_BOARDIOC_SERIAL, &config);
        if(ret != 0){
            return ret;
        }
    }

    int fd = open(uart_info->data, uart_info->type);
    if (fd < 0) {
        return fd;
    } 
    uart_info->fd = fd;
    uart_info->is_use = QM_TRUE;
__next:
    uart->priv = (void*)uart_info;

    if(uart->port){
        recv_ctx = (qm_uart_recv_ctx_t *)qm_malloc(sizeof(qm_uart_recv_ctx_t));
        if(recv_ctx == NULL){
            return -QM_ENOMEM;
        }
        
        memset(recv_ctx, 0, sizeof(qm_uart_recv_ctx_t));
        qm_mutex_new(&recv_ctx->lock);
        qm_ringbuf_init(&recv_ctx->ringbuf, recv_ctx->ringbuff_data, QM_UART1_RECV_MAX_LEN);
        qm_sem_new(&recv_ctx->sem, 0);
        recv_ctx->uart = uart;

        uart_info->arg = (void *)recv_ctx;
    }

    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    qm_uart_recv_ctx_t *recv_ctx = NULL;
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;
    if(recv_ctx == NULL){
        return -QM_EINVAL;
    }

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_mutex_lock(&recv_ctx->lock, QM_WAIT_FOREVER);
            qm_ringbuf_flush(&recv_ctx->ringbuf);
            qm_mutex_unlock(&recv_ctx->lock);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            recv_ctx->read = (qm_uart_read_callback_t)param;
            if(qm_sem_is_valid(&recv_ctx->sem)){
                qm_sem_free(&recv_ctx->sem); //节省资源,注册回调时无需使用sem
            }
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            recv_ctx->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    int ret = QM_EOK;
    if(uart == NULL || data == NULL || size == 0){
        return -QM_EINVAL;
    }

    if(uart->priv == NULL){
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = NULL;

    uart_info = (qm_uart_info_t*)uart->priv;
    struct pollfd fdp = {
        .fd = uart_info->fd,
        .events = POLLOUT,
    };

    ret = poll((struct pollfd*)&fdp, 1, timeout);
    if (ret >= 0) {
        if ((fdp.revents & POLLOUT) != 0) {
            ret = write(uart_info->fd, data, size);
        }
    }

    return ret;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int ret  = QM_EOK;
    if(uart == NULL || data == NULL || expect_size == 0){
        return 0;
    }

    if(uart->port){
        ret = qm_uart1_read(uart, data, expect_size, timeout);
    }else{
        ret = qm_uart0_read(uart, data, expect_size, timeout);
    }

    return (0 <= ret) ? ret : 0;
}

static int32_t qm_uart0_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int ret = QM_EOK;
    qm_uart_info_t *uart_info = NULL;

    if(uart == NULL || data == NULL || expect_size == 0){
        return -QM_EINVAL;
    }

    if(uart->priv == NULL){
        return -QM_EINVAL;
    }

    uart_info = (qm_uart_info_t*)uart->priv;
    struct pollfd fdp = {
        .fd = uart_info->fd,
        .events = POLLIN,
    };

    ret = poll((struct pollfd*)&fdp, 1, timeout);
    if (ret >= 0) {
        if ((fdp.revents & POLLIN) != 0) {
            ret = read(uart_info->fd, data, expect_size);
        }
    }
    return ret;
}

static int32_t qm_uart1_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_left = 0;
    uint8_t isempty = 0;
    qm_utils_time_t time = {0};
    qm_uart_info_t *uart_info = NULL;
    qm_uart_recv_ctx_t *recv_ctx = NULL;
    
    if(uart == NULL || data == NULL || expect_size == 0){
        return -QM_EINVAL;
    }

    if(uart->priv == NULL){
        return -QM_EINVAL;
    }

    if(timeout >= (UINT32_MAX / 2)){
        timeout = (UINT32_MAX / 2);
    }

    uart_info = (qm_uart_info_t*)uart->priv;
    recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;
    if(recv_ctx == NULL){
        return -QM_EINVAL;
    }

    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);  

    do{
        t_left = qm_utils_time_left(&time);
        
        qm_mutex_lock(&recv_ctx->lock, QM_WAIT_FOREVER);
        isempty = qm_ringbuf_isempty(&recv_ctx->ringbuf);
        qm_mutex_unlock(&recv_ctx->lock);

        if(!isempty){
            qm_mutex_lock(&recv_ctx->lock, QM_WAIT_FOREVER);
            len = qm_ringbuf_pop(&recv_ctx->ringbuf, (uint8_t *)data + recv_len, expect_size-recv_len);
            qm_mutex_unlock(&recv_ctx->lock);

            if(len > 0){
                recv_len += len;
            }else{
                ret = 0;
                break;
            }
        }else{

            if(t_left == 0 || recv_ctx->read){
                ret = 0;
                break;
            }

            ret = qm_sem_wait(&recv_ctx->sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }            
    }while ((recv_len < expect_size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret; 
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    qm_uart_recv_ctx_t *recv_ctx = (qm_uart_recv_ctx_t *)uart_info->arg;

    if(uart == NULL || uart->priv == NULL || recv_ctx == NULL){
        return -QM_EINVAL;
    }

    if(qm_sem_is_valid(&recv_ctx->sem)){
        qm_sem_free(&recv_ctx->sem); //节省资源,注册回调时无需使用sem
    }

    qm_free(recv_ctx);

    uart_info->arg = NULL;
    uart->priv = NULL;
    return QM_EOK;
}

#endif
