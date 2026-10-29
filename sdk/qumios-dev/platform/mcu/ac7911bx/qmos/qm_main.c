#include "qm_types.h"
#include "qm.h"
#include "qm_config.h"
#include "qm_lpc.h"
#include "qm_uart.h"
#include "qm_usb.h"

#include "app_config.h"

static volatile int s_debug_transport_ready;

int qm_debug_transport_is_ready(void)
{
    return s_debug_transport_ready;
}

#if CONFIG_QM_LOG_SUPPORT == 1
extern qm_mutex_t g_log_mutex;

#if CONFIG_QM_UART_SUPPORT && TCFG_DEBUG_UART1_ENABLE
static qm_uart_dev_t s_debug_uart;

int32_t qm_dbg_write(void *data, uint32_t size, uint32_t timeout)
{
    if (s_debug_uart.priv == NULL) {
        return -QM_EINIT;
    }
    return qm_uart_write(&s_debug_uart, data, size, timeout);
}

int32_t qm_dbg_read(void *data, uint32_t expect_size, uint32_t timeout)
{
    if (s_debug_uart.priv == NULL) {
        return -QM_EINIT;
    }
    return qm_uart_read(&s_debug_uart, data, expect_size, timeout);
}

static void qm_dbg_init(void)
{
    int ret;

    s_debug_uart.port = TCFG_QM_DEBUG_UART_PORT;
    s_debug_uart.config.baud_rate = TCFG_DEBUG_BAUDRATE;
    s_debug_uart.config.data_bits = QM_UART_DATA_8_BITS;
    s_debug_uart.config.parity = QM_UART_PARITY_NONE;
    s_debug_uart.config.stop_bits = QM_UART_STOP_BITS_1;
    s_debug_uart.config.flow_ctrl = QM_UART_FLOW_CTRL_NONE;
    s_debug_uart.config.mode = QM_UART_MODE_UART;

    ret = qm_uart_init(&s_debug_uart);
    if (ret != QM_EOK) {
        qm_printf("[QUMI] debug uart init failed ret=%d\n", ret);
        return;
    }
    s_debug_transport_ready = 1;
}
#elif CONFIG_QM_USB_SUPPORT && TCFG_QM_USB_CDC_ENABLE
static qm_sem_t s_debug_usb_sem;

static void qm_debug_usb_rx_callback(qm_usb_cdcacm_port_t port,
                                     qm_usb_cdcacm_event_t *event)
{
    (void)port;
    if (event != NULL && event->type == QM_USB_CDC_EVENT_RX) {
        qm_sem_signal_from_isr(&s_debug_usb_sem);
    }
}

int32_t qm_dbg_write(void *data, uint32_t size, uint32_t timeout)
{
    return qm_usb_cdcacm_write(QM_USB_CDCACM_0, (const uint8_t *)data,
                               (int)size, timeout);
}

int32_t qm_dbg_read(void *data, uint32_t expect_size, uint32_t timeout)
{
    int32_t ret;

    ret = qm_usb_cdcacm_read(QM_USB_CDCACM_0, (uint8_t *)data,
                             (int)expect_size);
    if (ret != 0 || timeout == QM_NO_WAIT) {
        return ret;
    }

    ret = qm_sem_wait(&s_debug_usb_sem, timeout);
    if (ret != QM_EOK) {
        return 0;
    }
    return qm_usb_cdcacm_read(QM_USB_CDCACM_0, (uint8_t *)data,
                               (int)expect_size);
}

static void qm_dbg_init(void)
{
    qm_usb_cdcacm_config_t config = {
        .usb_port = QM_USB_0,
        .cdc_port = QM_USB_CDCACM_0,
        .rx_buf_sz = TCFG_QM_USB_CDC_RX_BUFFER_SIZE,
        .callback_rx = qm_debug_usb_rx_callback,
    };
    int ret;

    ret = qm_sem_new(&s_debug_usb_sem, 0);
    if (ret != QM_EOK) {
        qm_printf("[QUMI] debug usb semaphore init failed ret=%d\n", ret);
        return;
    }

    ret = qm_usb_cdcacm_init(&config);
    if (ret != QM_EOK) {
        qm_printf("[QUMI] debug usb init failed ret=%d\n", ret);
        return;
    }
    s_debug_transport_ready = 1;
}
#endif

#endif

int qm_debug_transport_write(const void *data, uint32_t size, uint32_t timeout)
{
#if CONFIG_QM_LOG_SUPPORT == 1 && \
    ((CONFIG_QM_UART_SUPPORT && TCFG_DEBUG_UART1_ENABLE) || \
     (CONFIG_QM_USB_SUPPORT && TCFG_QM_USB_CDC_ENABLE))
    if (!s_debug_transport_ready) {
        return -QM_EINIT;
    }
    return qm_dbg_write((void *)data, size, timeout);
#else
    (void)data;
    (void)size;
    (void)timeout;
    return -QM_ENOSYS;
#endif
}

void qm_main(void)
{

#if CONFIG_QM_LOG_SUPPORT == 1
    qm_mutex_new(&g_log_mutex);
#endif

    // Initialize the Qumi scheduler/kernel (tasks, queues, timers etc)
    qm_kernel_init();

#if CONFIG_QM_LOG_SUPPORT == 1 && \
    ((CONFIG_QM_UART_SUPPORT && TCFG_DEBUG_UART1_ENABLE) || \
     (CONFIG_QM_USB_SUPPORT && TCFG_QM_USB_CDC_ENABLE))
    qm_dbg_init();
#endif

#if CONFIG_QM_LPC_SUPPORT
    qm_lpc_init();
#endif

    // Start the Qumi application layer (network, BLE, etc.)
    qm_application_start();
}
