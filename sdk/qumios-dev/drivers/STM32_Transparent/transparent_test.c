#include "transparent.h"

#include "qm.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "qm_uart.h"

#define LOG_TAG "transparent"

static qm_uart_dev_t uart1_dev = {
        .port = 1,
        .config = {
            .baud_rate = QM_UART_BAUDRATE_115200,
            .data_bits = QM_UART_DATA_8_BITS,
            .stop_bits = QM_UART_STOP_BITS_1,
            .parity    = QM_UART_PARITY_NONE,
            .flow_ctrl = QM_UART_FLOW_CTRL_NONE,
            .pins = {
                .tx_pin = 33,
                .rx_pin = 32,
            }
        }
    };


static int32_t transparent_test_write(void *data, uint32_t len)
{
    return qm_uart_write(&uart1_dev, data, len, 0);
}

static int32_t transparent_test_readtimeout(void *data, uint32_t len, uint32_t timeout)
{
    return qm_uart_read(&uart1_dev, data, len, timeout);
}

static transparent_param_t  transparent_param = {
    .write = transparent_test_write,
    .readtimeout = transparent_test_readtimeout,
};

static transparent_port_t temp_port = {
    .channel = {
        .port1 = 1,
        .port2 = 1,
        .port3 = 7,
        .port4 = 7,
        .port5 = 7,
        .inval = 0,
    }
};

static transparent_port_t port = {
    .channel = {
        .port1 = 1,
        .port2 = 1,
        .port3 = 7,
        .port4 = 7,
        .port5 = 7,
        .inval = 0,
    }
};

static void *handle = NULL;
static void transparent_test_thread(void *arg)
{
    #define TEST_DATA_LEN 64
    uint8_t send_data[] = "JUST FOR TEST";

    uint8_t data[TEST_DATA_LEN] = {0};
    while (1)
    {        
        memset(data, 0, TEST_DATA_LEN);
        transparent_write_cfg(handle, port, send_data, strlen("JUST FOR TEST"));
        transparent_read(handle, port,data, strlen("JUST FOR TEST"), 10000);
        if(0 == strcmp((char*)data, "JUST FOR TEST")){
            QM_LOGI(LOG_TAG, "transparent cfg pass");
        }else{
            QM_LOGI(LOG_TAG, "transparent cfg  fail");
        }

        memset(data, 0, TEST_DATA_LEN);
        transparent_write(handle, port, send_data, strlen("JUST FOR TEST"));
        transparent_read(handle, port,data, strlen("JUST FOR TEST"), 1000);
        if(0 == strcmp((char*)data, "JUST FOR TEST")){
            QM_LOGI(LOG_TAG, "transparent PORT %02X pass", port);
        }else{
            QM_LOGI(LOG_TAG, "transparent PORT %02X  fail", port);
        }

        if(port.channel.port1++ >= 5){
            if(port.channel.port2++ >= 5){
                memcpy(&port, &temp_port, sizeof(transparent_port_t));
            }
        }
    }
}

int32_t transparent_demo(void)
{
  
    qm_task_t test_task;

    qm_uart_init(&uart1_dev);
    handle = transparent_init(&transparent_param);
    if (handle == NULL){
        QM_LOGE(LOG_TAG, "transparent_demo handle NULL");
    }
    qm_task_new(&test_task, "trans tx", transparent_test_thread, NULL, 4096, 20);

    
    return QM_EOK;
} 