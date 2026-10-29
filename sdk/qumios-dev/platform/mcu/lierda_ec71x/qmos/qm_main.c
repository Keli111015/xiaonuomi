#include "qm_types.h"
#include "qm_uart.h"
#include "qm.h"
#include "qm_config.h"

#if CONFIG_QM_LOG_SUPPORT == 1
extern qm_mutex_t g_log_mutex;
#if CONFIG_QM_LOG_UART == 1
static qm_uart_dev_t dbg_uart = {0};

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
    dbg_uart.port = CONFIG_QM_DEBUG_UART_PORT;

    dbg_uart.config.baud_rate = CONFIG_QM_DEBUG_UART_BRAUDRATE;
    dbg_uart.config.stop_bits = QM_UART_STOP_BITS_1;
    dbg_uart.config.data_bits = QM_UART_DATA_8_BITS;
    dbg_uart.config.flow_ctrl = QM_UART_FLOW_CTRL_NONE;
    dbg_uart.config.parity = QM_UART_PARITY_NONE;

    qm_uart_init(&dbg_uart);

}

#endif

#endif

void qm_main(void)
{
    #if CONFIG_QM_LOG_SUPPORT
    qm_mutex_new(&g_log_mutex);
    #if CONFIG_QM_LOG_UART == 1 || CONFIG_QM_LOG_USB_CDC == 1
    qm_dbg_init();    
    #endif
    #endif

    qm_kernel_init();
    qm_application_start();
}