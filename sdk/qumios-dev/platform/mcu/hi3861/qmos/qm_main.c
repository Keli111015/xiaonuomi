#include "qmos/qm_types.h"
#include "qm_uart.h"

qm_uart_dev_t dbg_uart = {0};

int32_t qm_dbg_write(void *data, uint32_t size, uint32_t timeout)
{
    return qm_uart_write(&dbg_uart, data, size, timeout);
}

int32_t qm_dbg_read(void *data, uint32_t expect_size, uint32_t timeout)
{
    return qm_uart_read(&dbg_uart, data, expect_size, timeout);
}

static void qm_dbg_init(void)
{
     //system already init uart0 as debug print
    dbg_uart.port = 0;
    dbg_uart.config.baud_rate = 115200;
    dbg_uart.config.stop_bits = QM_UART_STOP_BITS_1;
    dbg_uart.config.data_bits = QM_UART_DATA_8_BITS;
    dbg_uart.config.flow_ctrl = QM_UART_FLOW_CTRL_NONE;
    dbg_uart.config.parity = QM_UART_PARITY_NONE;
    dbg_uart.config.pins.tx_pin = 1;
    dbg_uart.config.pins.rx_pin = 3;

    qm_uart_init(&dbg_uart);

}

extern void qm_kernel_init(void);

void qm_main(void)
{
    qm_kernel_init();

    qm_dbg_init();

}