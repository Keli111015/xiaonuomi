#include "qm.h"

#include "qm_uart.h"
#include "qm_ringbuf.h"

#include "include.h"
#include "driver_gpio.h"
#include "driver_uart.h"
#include "driver_clkcal.h"

#define UART_SIZE (256)
#define UART_MAX_NUM (1)
#define RECV_TIMEOUT (20)

typedef struct
{
    UART_Sel_e port;
    qm_ringbuf_t ringbuf;
    qm_uart_dev_t *uart_dev;
    qm_uart_read_callback_t read;
    qm_timer_t recv_timeout;
    uint8_t buf[UART_SIZE];
    int rx_size;
} qm_uart_info_t;

static qm_uart_info_t g_uart_info[UART_MAX_NUM] = 
{
    [0] = {UART1, {0}, NULL, NULL, {0}, {0}},
};

ATTRIBUTE_ISR void UART1_Handler(void)
{
    uint8_t val = 0;
	uint8_t reg_val = uart_get_state(UART1);
    qm_uart_info_t *uart_info = &g_uart_info[0];
    
	if (reg_val&0x1)
	{
        val = uart_getchar(UART1);
        uart_info->rx_size += qm_ringbuf_push(&uart_info->ringbuf, &val, 1);
	}
}
int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    qm_uart_config_t *config = NULL;
    qm_uart_info_t *uart_info = NULL;

    if (uart == NULL || uart->port >= UART_MAX_NUM)
    {
        return -QM_EINVAL;
    }

    uart_info = &g_uart_info[uart->port];
    if (uart_info->uart_dev)
    {
        uart->priv = (void *)uart_info;
        return QM_EOK;
    }

    config = &uart->config;

    uart_info->uart_dev = uart;
    uart->priv = (void *)uart_info;
    uart_info->rx_size = 0;
    memset(uart_info->buf, 0, UART_SIZE);
    qm_ringbuf_init(&uart_info->ringbuf, uart_info->buf, UART_SIZE);

    uart_set_port(uart_info->port, config->pins.rx_pin, config->pins.tx_pin);
	uart_init(uart_info->port,config->baud_rate);
	uart_int_enable(uart_info->port, DISABLE, ENABLE);

    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    qm_uart_info_t *uart_info = NULL;

    if (uart == NULL || data == NULL || size == 0 || uart->priv == NULL)
    {
        return -QM_EINVAL;
    }
    uart_info = (qm_uart_info_t *)uart->priv;

    uart_send(uart_info->port, (uint8_t *)data, size);

    return QM_EOK;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    qm_err_t ret = QM_EOK;
    uint8_t isempty = 0;
    int32_t recv_len = 0;
    qm_uart_info_t *uart_info = NULL;

    if (uart == NULL || data == NULL || expect_size == 0 || uart->priv == NULL)
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
            ret = 0;
            break;
        }
    } while ((recv_len < expect_size));

    return (0 != recv_len) ? recv_len : ret;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    if (uart == NULL || uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

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


//暂不支持串口反初始化
int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    if (uart == NULL || uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

    uart->priv = NULL;
    qm_ringbuf_flush(&uart_info->ringbuf);

    return QM_EOK;
}