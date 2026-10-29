#include "qm_uart.h"
#include "qm_ringbuf.h"
#include "qm.h"
#include "qm_ringbuf.h"
#include "qm_log.h"
#include "qm_utils_timer.h"

#include "em_device.h"
#include "em_chip.h"
#include "em_emu.h"
#include "em_cmu.h"
#include "em_gpio.h"
#include "em_usart.h"
#include "em_eusart.h"
#include "native_gecko.h"
#include "em_ldma.h"
#include "em_timer.h"
#include "sleep.h"
#include "event_id.h"

#define QM_UART_PORT_NUM (0)
#define QM_UART_PORT     (EUART0)
#define QM_UART_IRQ      EUART0_RX_IRQHandler

#if (QM_UART_PORT_NUM != 0) && (QM_UART_PORT_NUM != 1)
#error "QM_UART_PORT_NUM must be 0 or 1"
#endif

#define RECV_BUFF_SIZE  (256)
#define RECV_TIMEOUT    (10)

typedef struct
{
    uint8_t is_use;
    uint32_t rx_size;
    qm_uart_dev_t *uart_dev;
    qm_ringbuf_t rx_ringbuf;
    uint8_t rx_buff[RECV_BUFF_SIZE];
    qm_uart_read_callback_t read;
    qm_uart_write_callback_t write;
    qm_timer_t recv_timeout;
}qm_uart_info_t;

static qm_uart_info_t g_uart_info = {0};

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    qm_uart_info_t *uart_info = &g_uart_info;

    if(uart == NULL){
        return -QM_EINVAL;
    }

    if(uart_info->is_use){
        goto _next;
    }

    int tx_gpio_port = (uart->config.pins.tx_pin >> 4) & 0xF;
    int tx_gpio_pin  = (uart->config.pins.tx_pin & 0xF);
    int rx_gpio_port = (uart->config.pins.rx_pin >> 4) & 0xF;
    int rx_gpio_pin  = (uart->config.pins.rx_pin & 0xF);
    //1. enable clock
    CMU_ClockEnable(cmuClock_PCLK, true);
    CMU_ClockEnable(cmuClock_GPIO, true);
      //use euart0
    CMU_ClockSelectSet(cmuClock_EUART0, cmuSelect_EM23GRPACLK);
    CMU_ClockEnable(cmuClock_EUART0, true);


    //3. init uart
    //use euart
    EUSART_UartInit_TypeDef init = EUSART_UART_INIT_DEFAULT_LF;
    EUSART_AdvancedInit_TypeDef advance_init = EUSART_ADVANCED_INIT_DEFAULT;

    init.baudrate = uart->config.baud_rate;
    init.advancedSettings = &advance_init;
    init.advancedSettings->txAutoTristate = true;
    init.enable = eusartDisable;
    EUSART_UartInitLf(QM_UART_PORT, &init);


    // Route EUSART0 TX to the board controller TX pin
    GPIO->EUARTROUTE[0].TXROUTE = (tx_gpio_port << _GPIO_EUART_TXROUTE_PORT_SHIFT)
        | (tx_gpio_pin << _GPIO_EUART_TXROUTE_PIN_SHIFT);
    GPIO->EUARTROUTE[0].RXROUTE = (tx_gpio_port << _GPIO_EUART_RXROUTE_PORT_SHIFT) 
        | (rx_gpio_pin << _GPIO_EUART_RXROUTE_PIN_SHIFT);

    // Enable TX signal now that it has been routed
    GPIO->EUARTROUTE[0].ROUTEEN = GPIO_EUART_ROUTEEN_TXPEN; 


    EUSART_Enable(QM_UART_PORT, eusartEnable);

    //2. enable gpio pinmux
    GPIO_PinModeSet(tx_gpio_port, tx_gpio_pin, gpioModePushPull, 1);
    GPIO_PinModeSet(rx_gpio_port, rx_gpio_pin, gpioModeInput, 0);

    // 4. Configure and enable USART and enable interrupt
    EUSART_IntClear(QM_UART_PORT, EUSART_IEN_RXFLIEN);
    EUSART_IntEnable(QM_UART_PORT, EUSART_IEN_RXFLIEN);
    NVIC_ClearPendingIRQ(EUART0_RX_IRQn);
    NVIC_EnableIRQ(EUART0_RX_IRQn);

    //init ringbuffer
    qm_ringbuf_init(&uart_info->rx_ringbuf, &uart_info->rx_buff[0], RECV_BUFF_SIZE);

_next:
    qm_ringbuf_flush(&uart_info->rx_ringbuf);
    uart_info->is_use = QM_TRUE;
    uart_info->uart_dev = uart;
    uart->priv = (void *)uart_info;
    return QM_EOK;
}

static void rx_timerout_cb(qm_timer_t *timer, void *arg)
{
    qm_uart_info_t *uart_info = &g_uart_info;
    if(uart_info->read && uart_info->rx_size > 0){
        uart_info->read(uart_info->uart_dev, uart_info->rx_size);
    }
}

void QM_UART_IRQ(void)
{
    uint8_t data = QM_UART_PORT->RXDATA;
    qm_uart_info_t *uart_info = &g_uart_info;
    EUSART_IntClear(QM_UART_PORT, EUSART_IF_RXFLIF);
    qm_ringbuf_push(&uart_info->rx_ringbuf, &data, 1);
    uart_info->rx_size += 1;
    // TODO: overflow 
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t size, uint32_t timeout)
{
    
    // ignore timeout, send blocking
    if ((NULL == data) || (NULL == uart) || uart->priv == NULL || (size == 0)) {
        return 0;
    }
    
    uint8_t *p = (uint8_t*)data;
    for (int i = 0; i < size; i++) {
        EUSART_Tx(QM_UART_PORT, p[i]);
    }

    return size;
}


int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int32_t len = 0;
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    if ((NULL == data) || (NULL == uart) || uart->priv == NULL || (expect_size == 0)) {
        return 0;
    }

    if (qm_ringbuf_isempty(&uart_info->rx_ringbuf)) {
        return 0;
    }

    len = qm_ringbuf_pop(&uart_info->rx_ringbuf, data, expect_size);
    uart_info->rx_size -= len;
    
    return (len > 0 ? len : 0); 
}

int32_t qm_uart_ctrl(qm_uart_dev_t *uart, qm_uart_ctrl_cmd_t cmd, void *param)
{
    if(uart == NULL || uart->priv == NULL || param == NULL){
        return -QM_EINVAL;
    }

    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;

    switch (cmd)
    {
        case QM_UART_CTRL_CMD_RX_CLEAR:
            qm_ringbuf_flush(&uart_info->rx_ringbuf);
        break;
        case QM_UART_CTRL_CMD_READ_CALLBACK:
            uart_info->read = (qm_uart_read_callback_t)param;
            qm_timer_new(&uart_info->recv_timeout, rx_timerout_cb, NULL, RECV_TIMEOUT, 1);
            qm_timer_start(&uart_info->recv_timeout);
        break;
        case QM_UART_CTRL_CMD_WRITE_CALLBACK:
            uart_info->write = (qm_uart_write_callback_t)param;
        break;       
        default:
            break;
    }

    return QM_EOK;
}

int32_t qm_uart_deinit(qm_uart_dev_t *uart)
{
    qm_uart_info_t *uart_info = (qm_uart_info_t *)uart->priv;
    if(uart == NULL || uart->priv == NULL){
        return -QM_EINVAL;
    }

    qm_ringbuf_flush(&uart_info->rx_ringbuf);
    uart_info->uart_dev = NULL;
    uart->priv = (void *)NULL;
    return QM_EOK;
}
