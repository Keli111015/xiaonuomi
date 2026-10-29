#include "qm.h"

#include "gpio.h"
#include "qm_ringbuf.h"
#include "hosal_timer.h"
#include "qm_utils_timer.h"

typedef enum
{
    COM_START_BIT,
    COM_D0_BIT,
    COM_D1_BIT,
    COM_D2_BIT,
    COM_D3_BIT,
    COM_D4_BIT,
    COM_D5_BIT,
    COM_D6_BIT,
    COM_D7_BIT,
    COM_STOP_BIT,
}simulate_uart_bit_t;

typedef struct {
    gpio_dev_t tx_gpio;
    gpio_dev_t rx_gpio;
    qm_sem_t rx_sem;
    qm_mutex_t rx_mutex;
    uint8_t  data[128];
    qm_ringbuf_t ringbuff;
    hosal_timer_dev_t recv_timer;
    hosal_timer_dev_t idle_timer;
    simulate_uart_bit_t recv_state;
}simulate_uart_handle_t;

static void timer_callback(void *arg);
static void gpio_irq_handler(void *arg);
static void idle_timer_callback(void *arg);
static void simulate_uart_byte_send(simulate_uart_handle_t *handle, uint8_t byte);

void *simulate_uart_init(uint8_t tx_pin, uint8_t rx_pin)
{
    qm_err_t ret = QM_EOK;
    simulate_uart_handle_t * uart_handle = NULL;
    uart_handle = (simulate_uart_handle_t *)qm_malloc(sizeof(simulate_uart_handle_t));
    if(uart_handle == NULL){
        return -QM_ENOMEM;
    }

    memset(uart_handle, 0, sizeof(simulate_uart_handle_t));

    ret = qm_mutex_new(&uart_handle->rx_mutex);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_sem_new(&uart_handle->rx_sem, 0);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_ringbuf_init(&uart_handle->ringbuff, uart_handle->data, sizeof(uart_handle->data));
    if(ret != QM_EOK){
        goto __exit;
    }

    uart_handle->tx_gpio.port = tx_pin;
    uart_handle->tx_gpio.config = OUTPUT_PUSH_PULL;
    ret = hal_gpio_init(&uart_handle->tx_gpio);
    
    if(ret != QM_EOK){
        goto __exit;
    }

    hal_gpio_output_high(&uart_handle->tx_gpio);

    uart_handle->rx_gpio.port = rx_pin;
    uart_handle->rx_gpio.config = INPUT_PULL_UP;
    ret = hal_gpio_init(&uart_handle->rx_gpio);  
    if(ret != QM_EOK){
        hal_gpio_finalize(&uart_handle->tx_gpio);
        goto __exit;
    }

    ret =  hal_gpio_enable_irq(&uart_handle->rx_gpio, IRQ_TRIGGER_FALLING_EDGE, gpio_irq_handler, (void *)uart_handle);  
    if(ret != QM_EOK){
        hal_gpio_finalize(&uart_handle->tx_gpio);
        hal_gpio_finalize(&uart_handle->rx_gpio);
        goto __exit;
    }

    uart_handle->recv_timer.port = 0;
    uart_handle->recv_timer.config.period = 208; /* 4800 baudrate */
    uart_handle->recv_timer.config.reload_mode = TIMER_RELOAD_PERIODIC;
    uart_handle->recv_timer.config.cb = timer_callback;
    uart_handle->recv_timer.config.arg = (void *)uart_handle;

    hosal_timer_init(&uart_handle->recv_timer);

    uart_handle->idle_timer.port = 1;
    uart_handle->idle_timer.config.period = 3744; /* 2075 */
    uart_handle->idle_timer.config.reload_mode = TIMER_RELOAD_ONCE;
    uart_handle->idle_timer.config.cb = idle_timer_callback;
    uart_handle->idle_timer.config.arg = (void *)uart_handle;
    hosal_timer_init(&uart_handle->idle_timer);

    uart_handle->recv_state = COM_STOP_BIT;
    return (void *)uart_handle;

__exit:

    if(qm_mutex_is_valid(&uart_handle->rx_mutex)){
        qm_mutex_free(&uart_handle->rx_mutex);
    }

    if(qm_sem_is_valid(&uart_handle->rx_sem)){
        qm_sem_free(&uart_handle->rx_sem);
    }

    qm_free(uart_handle);
    uart_handle = NULL;
    return NULL;    
}

void simulate_uart_send(void *handle, uint8_t *data, uint16_t len)
{
    uint16_t i=0;
    simulate_uart_handle_t *uart_handle = (simulate_uart_handle_t *)handle;
    if(handle == NULL || data == NULL || len == 0){
        return -QM_EINVAL;
    }

    irq_lock();
    for(i=0; i<len; i++){
        simulate_uart_byte_send(uart_handle, *(data+i));
    }
    irq_unlock();
}

int32_t simulate_uart_recv(void *handle, void *data, uint32_t expect_size, uint32_t timeout)
{
    int isempty = 0;
    int temp_len = 0;
    int recv_len = 0;
    uint32_t t_left = 0;
    qm_err_t ret = QM_EOK;
    uint16_t ext_port = 0;
    qm_utils_time_t time = {0};
    simulate_uart_handle_t *uart_handle = (simulate_uart_handle_t *)handle;

    if((handle == NULL || data == NULL) || (expect_size == 0)){
        return -QM_EINVAL;
    }

    if(timeout >= (UINT32_MAX / 2)){
        timeout = (UINT32_MAX / 2);
    }

    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);   

    do{
        t_left = qm_utils_time_left(&time);

        irq_lock();
        isempty = qm_ringbuf_isempty(&uart_handle->ringbuff);
        irq_unlock();


        if(!isempty){
            irq_lock();
            temp_len = qm_ringbuf_pop(&uart_handle->ringbuff, data + recv_len, expect_size - recv_len);
            irq_unlock();

            if(temp_len > 0){
                recv_len += temp_len;
            }else{
                ret = -QM_ERROR;
                break;
            }
        }else{
            if(t_left == 0){
                break;
            }
            ret = qm_sem_wait(&uart_handle->rx_sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }            
    }while ((recv_len < expect_size) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret;
}

static void gpio_irq_handler(void *arg)
{
    simulate_uart_handle_t *uart_handle = (simulate_uart_handle_t *)arg;
    if(arg == NULL){
        return;
    }

    if (uart_handle->recv_state == COM_STOP_BIT)
    {
        hal_gpio_disable_irq(&uart_handle->rx_gpio);
        uart_handle->recv_state = COM_START_BIT;
        hosal_timer_stop(&uart_handle->idle_timer);
        hosal_timer_start(&uart_handle->recv_timer);
    }    
}


static void idle_timer_callback(void *arg)
{
    simulate_uart_handle_t *uart_handle = (simulate_uart_handle_t *)arg;
    if(arg == NULL){
        return;
    }

    if(uart_handle->recv_state == COM_STOP_BIT){
        aos_sem_signal(&uart_handle->rx_sem);
    }
}

static void timer_callback(void *arg)
{
    uint32_t value = 0;
    static uint8_t chr = 0;
    simulate_uart_handle_t *uart_handle = (simulate_uart_handle_t *)arg;
    if(arg == NULL){
        return;
    }    

    uart_handle->recv_state++;
    hal_gpio_input_get(&uart_handle->rx_gpio, &value);

    if(uart_handle->recv_state == COM_D7_BIT){
        hal_gpio_enable_irq(&uart_handle->rx_gpio, IRQ_TRIGGER_FALLING_EDGE, gpio_irq_handler, (void *)uart_handle);
    }else if (uart_handle->recv_state == COM_STOP_BIT){
        hosal_timer_stop(&uart_handle->recv_timer);
        qm_ringbuf_push(&uart_handle->ringbuff, &chr, 1);
        hosal_timer_start(&uart_handle->idle_timer);
        
        chr = 0;
        return;
    }

    if (value){
        chr |= (1 << (uart_handle->recv_state - 1));
    }else{
        chr &= ~(1 << (uart_handle->recv_state - 1));
    }
}

static void simulate_uart_byte_send(simulate_uart_handle_t *handle, uint8_t byte)
{
	uint8_t i = 0;
    if(handle == NULL){
        return;
    }

	hal_gpio_output_low(&handle->tx_gpio);
	TG7100C_Delay_US(208);
	for(i = 0; i < 8; i++) 
	{
        if(byte & 0x01)
            hal_gpio_output_high(&handle->tx_gpio);
        else
            hal_gpio_output_low(&handle->tx_gpio);
        TG7100C_Delay_US(208);
        byte >>= 1;
	}
	hal_gpio_output_high(&handle->tx_gpio);
	TG7100C_Delay_US(208); 
}