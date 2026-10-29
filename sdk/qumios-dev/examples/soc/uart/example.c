#include "qm.h"
#include "qm_uart.h"
#include "qm_platform.h"

#define LOG_TAG "TEST"

#define USE_TEST_TASK_UART          (0)  //建立任务测试 串口read阻塞
#define USE_TEST_UART_CALLBACK      (1)  //测试串口callback 

#if USE_TEST_TASK_UART
static qm_task_t read_task = {0};
#endif


static qm_uart_dev_t uart_dev = {
    .config = {
        .baud_rate = 9600,
        .data_bits = QM_UART_DATA_8_BITS,
        .parity    = QM_UART_PARITY_NONE,
        .pins.tx_pin = QM_GPIO_PIN_21,
        .pins.rx_pin = QM_GPIO_PIN_20,
        .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = 1,
};

#if USE_TEST_TASK_UART
static void uart_test_read_thread(void *arg)
{
    int recv_len = 0;
    while (1)
    {
        memset(recv_buff, 0, 256);
        recv_len = qm_uart_read(&uart_dev, recv_buff, 256, 100);
        if(recv_len > 0){
            qm_uart_write(&uart_dev, recv_buff, recv_len, 0);
        }
    }
    
}
#endif

#if USE_TEST_UART_CALLBACK
static void uart_test_read_callback(qm_uart_dev_t *uart, uint32_t size)
{
    int recv_len = 0;
    static uint8_t recv_buff[256] = {0};

    if(uart == NULL || size == 0){
        return;
    }

    memset(recv_buff, 0, 256);
    recv_len = qm_uart_read(uart, recv_buff, size, 0);

    if(recv_len > 0){
        qm_uart_write(uart, recv_buff, recv_len, 0);
    }
}
#endif

void qm_application_start(void)
{
    int ret = QM_EOK;
    QM_LOGD(LOG_TAG, "start");

    ret = qm_uart_init(&uart_dev);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "INIT FAILED!!");
        while(1);
    }
#if USE_TEST_UART_CALLBACK
    ret = qm_uart_ctrl(&uart_dev, QM_UART_CTRL_CMD_READ_CALLBACK, uart_test_read_callback);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "qm_uart_ctrl FAILED!!");
        while(1);
    }
#endif
#if USE_TEST_TASK_UART
    qm_task_new(&read_task, "selftest_thread", uart_test_read_thread, NULL, 1*1024, 20);
#endif
}