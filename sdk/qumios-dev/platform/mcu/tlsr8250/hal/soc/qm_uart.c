#include "qm.h"
#include "qm_uart.h"
#include "qm_log.h"
#include "qm_ringbuf.h"
#include "qm_utils_timer.h"

#include "drivers.h"

// #define TX_DMA_EN 1

#define UART_DMA_LEN 256
#define UART_DATA_LEN 256

typedef struct{
	unsigned int dma_len;        // dma len must be 4 byte
	unsigned char data[UART_DMA_LEN];
}uart_data_t;

// volatile unsigned char uart_dmairq_tx_cnt=0;
static _attribute_data_retention_ qm_utils_time_t _g_time;
static _attribute_data_retention_ qm_ringbuf_t _uart_ringbuf;
static _attribute_data_retention_ uart_data_t _uart_recv_dma_buf = {0,{0x00}};
#if TX_DMA_EN
static uart_data_t _uart_send_dma_buf = {0,{0x00}};
#endif
static _attribute_data_retention_ uint8_t _uart_ringbuf_data[UART_DATA_LEN];

int qm_uart_dma_isr(void)
{
    // QM_LOGD("uart", "uart_dma_isr\n");
    if (_uart_recv_dma_buf.dma_len)
    {
        qm_ringbuf_push(&_uart_ringbuf, _uart_recv_dma_buf.data, _uart_recv_dma_buf.dma_len);
        _uart_recv_dma_buf.dma_len = 0;
        qm_utils_time_countdown_ms(&_g_time, 500);
    }

    return 0;
}

bool qm_uart_is_busy(void)
{
    bool busy = qm_utils_time_is_expired(&_g_time) ? false : true;

    return busy;
}

int32_t qm_uart_hw_init(void)
{
    gpio_setup_up_down_resistor(GPIO_PB1, PM_PIN_PULLUP_10K);
    uart_gpio_set(UART_TX_PB1, UART_RX_PA0);// uart tx/rx pin set
    uart_recbuff_init(&_uart_recv_dma_buf, sizeof(_uart_recv_dma_buf));


    uart_reset();
    // baud rate: 9600
    #if (CLOCK_SYS_CLOCK_HZ == 16000000)
    uart_init(118, 13, PARITY_NONE, STOP_BIT_ONE);
    // uart_init(9, 13, PARITY_NONE, STOP_BIT_ONE);
    #elif (CLOCK_SYS_CLOCK_HZ == 24000000)
    uart_init(249, 9, PARITY_NONE, STOP_BIT_ONE);
    #endif

#if TX_DMA_EN
    uart_dma_enable(1, 1); 	//uart data in hardware buffer moved by dma, so we need enable them first
#else
    uart_dma_enable(1, 0); 	//uart data in hardware buffer moved by dma, so we need enable them first
#endif

    irq_set_mask(FLD_IRQ_DMA_EN);
#if TX_DMA_EN
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX | FLD_DMA_CHN_UART_TX, 1);   	//uart Rx/Tx dma irq enable
#else
    dma_chn_irq_enable(FLD_DMA_CHN_UART_RX, 1);   	//uart Rx/Tx dma irq enable
#endif

    uart_irq_enable(0, 0);

    return 0;
}

int32_t qm_uart_init(qm_uart_dev_t *uart)
{
    //825x 只有一个串口，引脚默认为PB1/PA0. 而且引脚编号以及超过uint8，所以不需要配置引脚
    if ((NULL == uart) || (uart->port != 0))
    {
        return -QM_EINVAL;
    }

    //init ringbuff
    qm_ringbuf_init(&_uart_ringbuf, _uart_ringbuf_data, sizeof(_uart_ringbuf_data));
    qm_utils_time_init(&_g_time);

    return qm_uart_hw_init();
}

int32_t qm_uart_write(qm_uart_dev_t *uart, const void *data, uint32_t len, uint32_t timeout)
{
    if ((NULL == uart) || (NULL == data) || (0 == len))
    {
        return -QM_EINVAL;
    }

    //如果长度超过dma长度，则只发送dma长度的数据
    if(len > UART_DMA_LEN){
        len = UART_DMA_LEN;
    }

#if TX_DMA_EN
    //TODO:disable PM, when write
    _uart_send_dma_buf.dma_len = len;
    memcpy(_uart_send_dma_buf.data, data, len);
    uart_dma_send((unsigned short*)&_uart_send_dma_buf);
    qm_utils_time_countdown_ms(&_g_time, 500);
#else
    for (int i = 0; i < len; i++)
    {
        uart_ndma_send_byte(((unsigned char *)data)[i]);
    }
    qm_utils_time_countdown_ms(&_g_time, 500);
#endif
    
    QM_LOGD("uart", "send data len:%d", len);

    return len;
}


int32_t qm_uart_read(qm_uart_dev_t *uart, void *data, uint32_t expect_size, uint32_t timeout)
{
    int len = 0;
    int recv_len = 0;
    qm_err_t ret = QM_EOK;
    uint32_t t_left = 0;
    uint8_t isempty = 0;
    qm_utils_time_t time = {0};
    
    if(uart == NULL || data == NULL || expect_size == 0){
        return -QM_EINVAL;
    }

    if(timeout >= (UINT32_MAX / 2)){
        timeout = (UINT32_MAX / 2);
    }

    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);  

    do {
        t_left = qm_utils_time_left(&time);
        
        isempty = qm_ringbuf_isempty(&_uart_ringbuf);

        if (!isempty)
        {
            len = qm_ringbuf_pop(&_uart_ringbuf, (uint8_t *)data + recv_len, expect_size-recv_len);
            if(len > 0){
                recv_len += len;
            }else{
                ret = 0;
                break;
            }
        }
        else
        {

            //没有数据，直接退出
            // if (t_left == 0 || recv_ctx->read)
            {
                ret = 0;
                break;
            }

            // ret = qm_sem_wait(&recv_ctx->sem, t_left);
            // if(ret != QM_EOK){
            //     ret = 0;
            //     break;
            // }
        }            
    }
    while ((recv_len < expect_size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret; 
}
