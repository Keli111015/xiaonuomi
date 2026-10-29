#include "qm_uart.h"
#include "qm_errno.h"

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    (void)uart;
    return QM_EOK;
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    (void)uart;
    (void)cmd;
    (void)param;
    return QM_EOK;
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    (void)uart;
    (void)data;
    (void)timeout;
    return (int32_t)size;
}

int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    (void)uart;
    (void)data;
    (void)expect_size;
    (void)timeout;
    return 0;
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    (void)uart;
    return QM_EOK;
}
