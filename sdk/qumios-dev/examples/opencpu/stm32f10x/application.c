#include "qm.h"

#include "qm_gpio.h"
#include "qm_uart.h"
#include "qm_log.h"
#define LOG_TAG "TEST"

static uint8_t recv_buff[256] = {0};

// #if CONFIG_BOARD_PLATFORM_STM32F10X

#include "qm_platform.h"


static qm_gpio_dev_t gpio_out =
{
    .port = QM_GPIO_PIN_22,
    .config = {
        .mode = QM_GPIO_MODE_OUTPUT,
        .pull_en = QM_GPIO_PULLUP_ONLY,
    }
};


	static qm_uart_dev_t uart_dev1 = {
			.config = {
					.baud_rate = QM_UART_BAUDRATE_115200,
					.data_bits = QM_UART_DATA_8_BITS,
					.parity    = QM_UART_PARITY_NONE,
					.pins.tx_pin = QM_GPIO_PIN_9,
					.pins.rx_pin = QM_GPIO_PIN_10,
					.stop_bits = QM_UART_STOP_BITS_1 ,
			},
			.port = QM_UART_PORT_0,
	};

static qm_uart_dev_t uart_dev2 = {
    .config = {
        .baud_rate = QM_UART_BAUDRATE_115200,
        .data_bits = QM_UART_DATA_8_BITS,
        .parity    = QM_UART_PARITY_NONE,
        .pins.tx_pin = QM_GPIO_PIN_2,
        .pins.rx_pin = QM_GPIO_PIN_3,
        .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = QM_UART_PORT_1,
};

static qm_uart_dev_t uart_dev3 = {
    .config = {
        .baud_rate = QM_UART_BAUDRATE_115200,
        .data_bits = QM_UART_DATA_8_BITS,
        .parity    = QM_UART_PARITY_NONE,
        .pins.tx_pin = QM_GPIO_PIN_26,
        .pins.rx_pin = QM_GPIO_PIN_27,
        .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = QM_UART_PORT_2,
};

static qm_uart_dev_t uart_dev4 = {
    .config = {
        .baud_rate = QM_UART_BAUDRATE_115200,
        .data_bits = QM_UART_DATA_8_BITS,
        .parity    = QM_UART_PARITY_NONE,
        .pins.tx_pin = QM_GPIO_PIN_42,
        .pins.rx_pin = QM_GPIO_PIN_43,
        .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = QM_UART_PORT_3,
};

static qm_uart_dev_t uart_dev5 = {
    .config = {
        .baud_rate = QM_UART_BAUDRATE_115200,
        .data_bits = QM_UART_DATA_8_BITS,
        .parity    = QM_UART_PARITY_NONE,
        .pins.tx_pin = QM_GPIO_PIN_44,
        .pins.rx_pin = QM_GPIO_PIN_50,
        .stop_bits = QM_UART_STOP_BITS_1 ,
    },
    .port = QM_UART_PORT_4,
};
// #endif

static void uart_test_read_callback(qm_uart_dev_t *uart, uint32_t size)
{
    int recv_len = 0;
    if(uart == NULL || size == 0){
        return;
    }
    
    memset(recv_buff, 0, 256);
    recv_len = qm_uart_read(uart, recv_buff, size, 0);
    if(recv_len > 0){
        qm_uart_write(uart, recv_buff, recv_len, 0);
    }
}

void qm_application_start(void)
{
    int ret = QM_EOK;
	QM_LOGD(LOG_TAG, "qm_application_start!!");

    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 0);

    gpio_out.port = QM_GPIO_PIN_21;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 0);

    gpio_out.port = QM_GPIO_PIN_20;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 0);

    gpio_out.port = QM_GPIO_PIN_19;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 0);

    gpio_out.port = QM_GPIO_PIN_25;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 1);

    gpio_out.port = QM_GPIO_PIN_23;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 0);

    gpio_out.port = QM_GPIO_PIN_8;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 1);

    gpio_out.port = QM_GPIO_PIN_41;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 1);

    gpio_out.port = QM_GPIO_PIN_40;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 1);

    gpio_out.port = QM_GPIO_PIN_39;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 1);

    gpio_out.port = QM_GPIO_PIN_38;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 1);

    gpio_out.port = QM_GPIO_PIN_31;
    qm_gpio_init(&gpio_out);
    qm_gpio_set_level(&gpio_out, 1);

// #if CONFIG_BOARD_PLATFORM_STM32F10X
    ret = qm_uart_init(&uart_dev1);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "INIT UART1 FAILED!!");
        while(1);
    }

    ret = qm_uart_ctrl(&uart_dev1, QM_UART_CTRL_CMD_READ_CALLBACK, uart_test_read_callback);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "reg read cb FAILED!!");
        while(1);
    }

    ret = qm_uart_init(&uart_dev2);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "INIT UART2 FAILED!!");
        while(1);
    }

    ret = qm_uart_ctrl(&uart_dev2, QM_UART_CTRL_CMD_READ_CALLBACK, uart_test_read_callback);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "reg read cb FAILED!!");
        while(1);
    }

    ret = qm_uart_init(&uart_dev3);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "INIT UART3 FAILED!!");
        while(1);
    }

    ret = qm_uart_ctrl(&uart_dev3, QM_UART_CTRL_CMD_READ_CALLBACK, uart_test_read_callback);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "reg read cb FAILED!!");
        while(1);
    }

    ret = qm_uart_init(&uart_dev4);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "INIT UART4 FAILED!!");
        while(1);
    }

    ret = qm_uart_ctrl(&uart_dev4, QM_UART_CTRL_CMD_READ_CALLBACK, uart_test_read_callback);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "reg read cb FAILED!!");
        while(1);
    }

    ret = qm_uart_init(&uart_dev5);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "INIT UART4 FAILED!!");
        while(1);
    }

    ret = qm_uart_ctrl(&uart_dev5, QM_UART_CTRL_CMD_READ_CALLBACK, uart_test_read_callback);
    if(ret != QM_EOK){
        QM_LOGD(LOG_TAG, "reg read cb FAILED!!");
        while(1);
    }

// #endif

}
