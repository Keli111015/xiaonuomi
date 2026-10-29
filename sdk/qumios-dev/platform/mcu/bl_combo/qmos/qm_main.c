#include "qm.h"
#include "qm_uart.h"

#if CONFIG_QM_LOG_SUPPORT == 1
extern qm_mutex_t g_log_mutex;

#if CONFIG_QM_LOG_UART == 1
static qm_uart_dev_t dbg_uart = {0};

#ifndef CONFIG_QM_DEBUG_UART_TX_PIN
#define CONFIG_QM_DEBUG_UART_TX_PIN (1)
#endif

#ifndef CONFIG_QM_DEBUG_UART_RX_PIN
#define CONFIG_QM_DEBUG_UART_RX_PIN (3)
#endif


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

    /* Disable buffering on stdin */
    // setvbuf(stdin, NULL, _IONBF, 0);

     //system already init uart0 as debug print
    dbg_uart.port = CONFIG_QM_DEBUG_UART_PORT;

    dbg_uart.config.baud_rate = CONFIG_QM_DEBUG_UART_BRAUDRATE;
    dbg_uart.config.stop_bits = QM_UART_STOP_BITS_1;
    dbg_uart.config.data_bits = QM_UART_DATA_8_BITS;
    dbg_uart.config.flow_ctrl = QM_UART_FLOW_CTRL_NONE;
    dbg_uart.config.parity = QM_UART_PARITY_NONE;
    dbg_uart.config.pins.tx_pin = CONFIG_QM_DEBUG_UART_TX_PIN;
    dbg_uart.config.pins.rx_pin = CONFIG_QM_DEBUG_UART_RX_PIN;

    qm_uart_init(&dbg_uart);

}
#endif
#endif

void qm_main(void)
{
    #if CONFIG_QM_LOG_SUPPORT
    qm_mutex_new(&g_log_mutex);
    #if CONFIG_QM_LOG_UART == 1
    qm_dbg_init();    
    #endif
    #endif

    qm_kernel_init();
    qm_application_start();
}