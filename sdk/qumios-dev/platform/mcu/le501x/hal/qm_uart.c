#include "qm.h"

#ifdef CONFIG_QM_UART_SUPPORT

#include "qm_uart.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"
#include "qm_platform.h"
#include "qm_kernel.h"

#include "sleep.h"
#include "platform.h"
#include "ls_hal_uart.h"
#include "ls_soc_gpio.h"
#include "sw_timer.h"
#include "log.h"
#include "ls_sys.h"

#define UART_SIZE (256)
#define RECV_TIMEOUT (20)

static void rx_timerout_cb(qm_timer_t *timer, void *arg);

typedef struct
{
    reg_uart_t *UARTX;

    uint8_t *buf;
    uint16_t rx_size;
    qm_ringbuf_t ringbuf;
    qm_uart_dev_t *uart_dev;
    qm_timer_t recv_timeout;
    volatile int rx_full_flag;
    UART_HandleTypeDef uart_config;

    uint8_t recv_data;

    void (*uart_init_func)(uint8_t tx_pin, uint8_t rx_pin);
    void (*uart_deinit_func)(void);

    qm_uart_read_callback_t read;
} qm_uart_info_t;

static qm_uart_info_t g_uart_info[] = {
    {UART1, NULL, 0, {0}, NULL, {0}, 0, {0}, 0, pinmux_uart1_init, pinmux_uart1_deinit, NULL},
    {UART2, NULL, 0, {0}, NULL, {0}, 0, {0}, 0, pinmux_uart2_init, pinmux_uart2_deinit, NULL},
    {UART3, NULL, 0, {0}, NULL, {0}, 0, {0}, 0, pinmux_uart3_init, pinmux_uart3_deinit, NULL},
};

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    int ret = 0;
    HAL_StatusTypeDef hal_ret = HAL_OK;
    qm_uart_info_t *uart_info = NULL;
    UART_HandleTypeDef *UART_Config = NULL;
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

    io_pull_write(uart_cfg.pins.rx_pin, IO_PULL_UP);
    io_pull_write(uart_cfg.pins.tx_pin, IO_PULL_UP);

    uart_info->uart_init_func(uart_cfg.pins.tx_pin, uart_cfg.pins.rx_pin);
    UART_Config = &uart_info->uart_config;
    UART_Config->UARTX = uart_info->UARTX;

    switch (uart_cfg.baud_rate)
    {
    case 1200:
        UART_Config->Init.BaudRate = UART_BAUDRATE_1200;
        break;
    case 2400:
        UART_Config->Init.BaudRate = UART_BAUDRATE_2400;
        break;
    case 4800:
        UART_Config->Init.BaudRate = UART_BAUDRATE_4800;
        break;
    case 9600:
        UART_Config->Init.BaudRate = UART_BAUDRATE_9600;
        break;
    case 14400:
        UART_Config->Init.BaudRate = UART_BAUDRATE_14400;
        break;
    case 19200:
        UART_Config->Init.BaudRate = UART_BAUDRATE_19200;
        break;
    case 28800:
        UART_Config->Init.BaudRate = UART_BAUDRATE_28800;
        break;
    case 38400:
        UART_Config->Init.BaudRate = UART_BAUDRATE_38400;
        break;
    case 57600:
        UART_Config->Init.BaudRate = UART_BAUDRATE_57600;
        break;
    case 76800:
        UART_Config->Init.BaudRate = UART_BAUDRATE_76800;
        break;
    case 115200:
        UART_Config->Init.BaudRate = UART_BAUDRATE_115200;
        break;
    case 230400:
        UART_Config->Init.BaudRate = UART_BAUDRATE_230400;
        break;
    case 250000:
        UART_Config->Init.BaudRate = UART_BAUDRATE_250000;
        break;
    case 500000:
        UART_Config->Init.BaudRate = UART_BAUDRATE_500000;
        break;
    case 460800:
        UART_Config->Init.BaudRate = UART_BAUDRATE_460800;
        break;
    case 750000:
        UART_Config->Init.BaudRate = UART_BAUDRATE_750000;
        break;
    case 921600:
        UART_Config->Init.BaudRate = UART_BAUDRATE_921600;
        break;
    case 1000000:
        UART_Config->Init.BaudRate = UART_BAUDRATE_1000000;
        break;
    case 2000000:
        UART_Config->Init.BaudRate = UART_BAUDRATE_2000000;
        break;
    default:
        ret = -QM_ERROR;
        break;
    }

    UART_Config->Init.MSBEN = 0;

    switch (uart_cfg.parity)
    {
    case QM_UART_PARITY_NONE:
        UART_Config->Init.Parity = UART_NOPARITY;
        break;
    case QM_UART_PARITY_EVEN:
        UART_Config->Init.Parity = UART_EVENPARITY;
        break;
    case QM_UART_PARITY_ODD:
        UART_Config->Init.Parity = UART_ODDPARITY;
        break;
    default:
        ret = -QM_ERROR;
        break;
    }

    switch (uart_cfg.stop_bits)
    {
    case QM_UART_STOP_BITS_1:
        UART_Config->Init.StopBits = UART_STOPBITS1;
        break;
    case QM_UART_STOP_BITS_2:
        UART_Config->Init.StopBits = UART_STOPBITS2;
        break;
    default:
        ret = -QM_ERROR;
        break;
    }

    switch (uart_cfg.data_bits)
    {
    case QM_UART_DATA_8_BITS:
        UART_Config->Init.WordLength = UART_BYTESIZE8;
        break;
    case QM_UART_DATA_7_BITS:
        UART_Config->Init.WordLength = UART_BYTESIZE7;
        break;
    case QM_UART_DATA_6_BITS:
        UART_Config->Init.WordLength = UART_BYTESIZE6;
        break;
    case QM_UART_DATA_5_BITS:
        UART_Config->Init.WordLength = UART_BYTESIZE5;
        break;

    default:
        ret = -QM_ERROR;
        break;
    }

    hal_ret = HAL_UART_Init(UART_Config);
    if (hal_ret != HAL_OK)
    {
        return -QM_EINVAL;
    }
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

    HAL_UART_Receive_IT(&uart_info->uart_config, &uart_info->recv_data, 1);

    return QM_EOK;
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    HAL_StatusTypeDef ret = HAL_OK;
    if (uart == NULL || uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    ret = HAL_UART_DeInit(&(uart_info->uart_config));

    if (uart_info->uart_deinit_func)
    {
        uart_info->uart_deinit_func();
    }

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

    return QM_EOK;
}
int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    qm_uart_info_t *uart_info = NULL;
    qm_utils_time_t time = {0};
    uint32_t t_left = 0;

    if (uart == NULL || data == NULL || size == 0)
    {
        return -QM_EINVAL;
    }

    if (uart->priv == NULL)
    {
        return -QM_EINVAL;
    }

    uart_info = (qm_uart_info_t *)uart->priv;

    HAL_UART_Transmit(&(uart_info->uart_config), (uint8_t *)data, (uint16_t)size, 1000);

    return QM_EOK;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int wait = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_left = 0;
    uint8_t isempty = 0;
    int32_t recv_len = 0;
#if CONFIG_QM_OS_SUPPORT
    qm_utils_time_t time = {0};
#endif
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

#if CONFIG_QM_OS_SUPPORT
    if (timeout == QM_WAIT_FOREVER)
    {
        wait = 1;
        t_left = QM_WAIT_FOREVER;
    }
    else
    {
        qm_utils_time_init(&time);
        qm_utils_time_countdown_ms(&time, timeout);
    }
#endif

    do
    {
#if CONFIG_QM_OS_SUPPORT
        if (!wait)
        {
            t_left = qm_utils_time_left(&time);
        }
#endif
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

#if CONFIG_QM_OS_SUPPORT
            ret = qm_sem_wait(&uart_info->rx_sem, t_left);
            if (ret != QM_EOK)
            {
                ret = 0;
                break;
            }
#endif
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

static void rx_timerout_cb(qm_timer_t *timer, void *arg)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)arg;
    if (uart_info->read)
    {
        uart_info->read(uart_info->uart_dev, uart_info->rx_size);
    }
}

static void timer_isr_handler(void *param)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)param;

    if (uart_info == NULL)
    {
        return;
    }

    qm_timer_change(&uart_info->recv_timeout, RECV_TIMEOUT);
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    qm_uart_info_t *uart_info = NULL;
    if (huart->UARTX == UART1)
    {
        uart_info = &g_uart_info[0];
    }
    else if (huart->UARTX == UART2)
    {
        uart_info = &g_uart_info[1];
    }
    else if (huart->UARTX == UART3)
    {
        uart_info = &g_uart_info[2];
    }
    else
    {
        return;
    }

    if (uart_info->buf == NULL)
    {
        return;
    }
    if (uart_info->read)
    {
        func_post(timer_isr_handler, (void *)uart_info);
    }

    uart_info->rx_size += qm_ringbuf_push(&uart_info->ringbuf, &uart_info->recv_data, 1);
    HAL_UART_Receive_IT(&(uart_info->uart_config), &uart_info->recv_data, 1);
}

#endif