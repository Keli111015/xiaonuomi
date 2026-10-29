#include "qm.h"

#ifdef CONFIG_QM_UART_SUPPORT

#include "qm_uart.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"
#include "qm_kernel.h"

#include "log.h"

#include "uart.h"

#define UART_SIZE (128)
#define RECV_TIMEOUT (20)

typedef struct
{
    UART_INDEX_e uart_index;
    
    uint8_t *buf;
    uint16_t rx_size;
    qm_ringbuf_t ringbuf;
    qm_uart_dev_t *uart_dev;
    qm_timer_t recv_timeout;
    volatile int rx_full_flag;
   
    uint8_t recv_data;

    qm_uart_read_callback_t read;
    uart_Hdl_t  evt_handler;
} qm_uart_info_t;


static void uart0_irq_handle(uart_Evt_t* pev);
static void uart1_irq_handle(uart_Evt_t* pev);

static qm_uart_info_t g_uart_info[] = {
    {UART0, NULL, 0, {0}, NULL, {0}, 0, 0, NULL, uart0_irq_handle},
    {UART1, NULL, 0, {0}, NULL, {0}, 0, 0, NULL, uart1_irq_handle},
};

static void uart0_irq_handle(uart_Evt_t* pev)
{
    qm_uart_info_t *uart_info = NULL;
    uart_info = &g_uart_info[0];
    
    if (uart_info->buf == NULL)
    {
        return;
    }
    switch(pev->type)
    {
        case UART_EVT_TYPE_RX_DATA:
        case UART_EVT_TYPE_RX_DATA_TO: //case rx data of uart RX timeout
        {
            qm_timer_change(&uart_info->recv_timeout, RECV_TIMEOUT);
            uart_info->rx_size += qm_ringbuf_push(&uart_info->ringbuf, pev->data, pev->len);
            break;
        }
        case UART_EVT_TYPE_TX_COMPLETED:
        {
            break;
        }
        default:
            break;
    }
}

static void uart1_irq_handle(uart_Evt_t* pev)
{
    qm_uart_info_t *uart_info = NULL;
    uart_info = &g_uart_info[1];
    
    if (uart_info->buf == NULL)
    {
        return;
    }
    switch(pev->type)
    {
        case UART_EVT_TYPE_RX_DATA:
        case UART_EVT_TYPE_RX_DATA_TO: //case rx data of uart RX timeout
        {   
            qm_timer_change(&uart_info->recv_timeout, RECV_TIMEOUT);
            uart_info->rx_size += qm_ringbuf_push(&uart_info->ringbuf, pev->data, pev->len);
            
            if (uart_info->read)
            {
                uart_info->read(uart_info->uart_dev, uart_info->rx_size);
            }
            break;
        }
        case UART_EVT_TYPE_TX_COMPLETED:
        {
            break;
        }
        default:
            break;
    }
}

static void rx_timerout_cb(qm_timer_t *timer, void *arg)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)arg;
    if (uart_info->read)
    {
        uart_info->read(uart_info->uart_dev, uart_info->rx_size);
    }
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    int ret = 0;
    qm_uart_info_t *uart_info = NULL;
    qm_uart_config_t uart_cfg = {0};

    if (uart == NULL)
    {
        return -QM_EINVAL;
    }

    if (uart->port >= QM_ARRAY_SIZE(g_uart_info))
    {
        return -QM_EINVAL;
    }

    uart_info = &g_uart_info[uart->port];

    if (uart_info->uart_dev)
    {
        uart->priv = (void *)uart_info;
        return QM_EOK;
    }
    
    uart_cfg = uart->config;
    
    uart_Cfg_t cfg =
    {
        .tx_pin = uart_cfg.pins.tx_pin,
        .rx_pin = uart_cfg.pins.rx_pin,
        .rts_pin = GPIO_DUMMY,
        .cts_pin = GPIO_DUMMY,
        .baudrate = uart_cfg.baud_rate,
        .use_fifo = TRUE,
        .hw_fwctrl = uart_cfg.flow_ctrl,//FALSE,
        .use_tx_buf = FALSE,
        .parity     = uart_cfg.parity,//FALSE,
        .evt_handler = uart_info->evt_handler,
    };
    
    hal_uart_init(cfg, uart_info->uart_index);
    ret = qm_timer_new(&uart_info->recv_timeout, rx_timerout_cb, (void *)uart_info, RECV_TIMEOUT, 0);
    if (ret != 0)
    {
        return -QM_EINVAL;
    }

    uart_info->buf = (uint8_t *)qm_malloc(UART_SIZE);
    if (uart_info->buf == NULL)
    {
        return -QM_ENOMEM;
    }
    memset(uart_info->buf, 0, UART_SIZE);
    qm_ringbuf_init(&uart_info->ringbuf, uart_info->buf, UART_SIZE);

    uart_info->uart_dev = uart;
    uart->priv = (void *)uart_info;
    return QM_EOK;
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
     if (uart == NULL || uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    
     qm_ringbuf_flush(&uart_info->ringbuf);
    if (uart_info->buf)
    {
        qm_free(uart_info->buf);
        uart_info->buf = NULL;
    }

    if (uart_info->recv_timeout.hdl != NULL)
    {
        qm_timer_free(&uart_info->recv_timeout);
        uart_info->recv_timeout.hdl = NULL;
    }

    uart->priv = NULL;
    return hal_uart_deinit(uart_info->uart_index);;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    qm_uart_info_t *uart_info = NULL;
    if (uart == NULL || data == NULL || size == 0)
    {
        return -QM_EINVAL;
    }

    if (uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

    uart_info = (qm_uart_info_t *)uart->priv;
    
    return hal_uart_send_buff(uart_info->uart_index, (uint8_t *)data, size);
}


int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int wait = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_left = 0;
    uint8_t isempty = 0;
    int32_t recv_len = 0;

    qm_uart_info_t *uart_info = NULL;

    if (uart == NULL || data == NULL || expect_size == 0 || uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

    if (uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

    uart_info = (qm_uart_info_t *)uart->priv;
    
    do
    {
        isempty = qm_ringbuf_isempty(&uart_info->ringbuf);

        if (!isempty)
        {

            len = qm_ringbuf_pop(&uart_info->ringbuf, (uint8_t *)data + recv_len, expect_size - recv_len);
            if (len > 0)
            {
                recv_len += len;
                uart_info->rx_size -= len;
            }
            else
            {
                ret = 0;
                break;
            }
        }
        else
        {

            if (t_left == 0)
            {
                ret = 0;
                break;
            }
        }
    } while ((recv_len < expect_size));

    return (0 != recv_len) ? recv_len : ret;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    qm_uart_info_t *uart_info = NULL;

    if (uart == NULL || param == NULL)
    {
        return -QM_EINVAL;
    }

    if (uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

    uart_info = (qm_uart_info_t *)uart->priv;

    switch (cmd)
    {
    case QM_UART_CTRL_CMD_RX_CLEAR:
        qm_ringbuf_flush(&uart_info->ringbuf);
        break;
    case QM_UART_CTRL_CMD_READ_CALLBACK:
        uart_info->read = (qm_uart_read_callback_t)param;
        break;
    case QM_UART_CTRL_CMD_WRITE_CALLBACK:
        break;
    default:
        break;
    }
    return QM_EOK;
}



#endif