#include "qm_gpio.h"
#include "qm_kernel.h"
#include "ch423s.h"
#include "qm_errno.h"
#include "qm_log.h"

#define NOT_COMPLETE 0
#define LOG_TAG "ch423"
#if NOT_COMPLETE
#define QM_CH423_IRQ_NUM_MAX    (8)
#define QM_CH423_QUEUE_SIZE     (30)
#define QM_CH423_TASK_PRO       (20)
#define QM_CH423_TASK_SIZE      (4096)

typedef struct 
{
    void *arg;
    uint8_t irq_used;
    uint8_t irq_gpio;
    uint8_t irq_value;
    qm_ch423s_irq_handler_t fn;
    qm_ch423s_irq_type_t irq_type;
}qm_ch423s_ext_irq_handler_t;

typedef struct 
{
    void *handle;
    uint32_t irq_total_value;
    qm_queue_t  ch423s_queue;
    qm_ch423s_ext_irq_handler_t irq_info[QM_CH423_IRQ_NUM_MAX];
}qm_ch423s_ext_irq_info_t;

static qm_ch423s_ext_irq_info_t ch423s_info = {0};

#endif
static inline void ch423_sda_set(qm_gpio_dev_t *gpio_sda)
{
    qm_gpio_set_level(gpio_sda, 1);
}
static inline void ch423_sda_clr(qm_gpio_dev_t *gpio_sda)
{
    qm_gpio_set_level(gpio_sda, 0);
}

static inline void ch423_scl_set(qm_gpio_dev_t *gpio_scl)
{
    qm_gpio_set_level(gpio_scl, 1);
}
static inline void ch423_scl_clr(qm_gpio_dev_t *gpio_scl)
{
    qm_gpio_set_level(gpio_scl, 0);
}

static inline int ch423_sda_in(qm_gpio_dev_t *gpio_sda)
{
    return qm_gpio_get_level(gpio_sda);
}

static void ch423_i2c_start(ch423s_handle_t *ch423s_handle)    // 操作起始
{
    ch423_sda_set(&(ch423s_handle->gpio_sda));
    ch423_scl_set(&(ch423s_handle->gpio_scl));
    qm_usleep(1);
    ch423_sda_clr(&(ch423s_handle->gpio_sda));
    qm_usleep(1);
    ch423_scl_clr(&(ch423s_handle->gpio_scl));
}

static void ch423_i2c_stop(ch423s_handle_t *ch423s_handle)    // 操作结束
{
    ch423_sda_clr(&(ch423s_handle->gpio_sda));
    ch423_scl_clr(&(ch423s_handle->gpio_scl));
    qm_usleep(1);
    ch423_sda_set(&(ch423s_handle->gpio_sda));
    ch423_scl_set(&(ch423s_handle->gpio_scl));
    qm_usleep(1);
}

static void ch423_i2c_ack(ch423s_handle_t *ch423s_handle)
{
    ch423_scl_clr(&(ch423s_handle->gpio_scl));
    ch423_sda_clr(&(ch423s_handle->gpio_sda));
    qm_usleep(1);
    ch423_scl_set(&(ch423s_handle->gpio_scl));
    qm_usleep(1);
    ch423_scl_clr(&(ch423s_handle->gpio_scl));
}

static void ch423_i2c_nack(ch423s_handle_t *ch423s_handle)
{
    ch423_scl_clr(&(ch423s_handle->gpio_scl));
    ch423_sda_set(&(ch423s_handle->gpio_sda));
    qm_usleep(1);
    ch423_scl_set(&(ch423s_handle->gpio_scl));
    qm_usleep(1);
    ch423_scl_clr(&(ch423s_handle->gpio_scl));
}

static void ch423_i2c_write_byte(ch423s_handle_t *ch423s_handle,unsigned char dat)    // 写一个字节数据
{
    unsigned char i;

    ch423_scl_clr(&(ch423s_handle->gpio_scl));
    for( i = 0; i < 8; i++ )
    {
        if( (dat&0x80) >> 7 ){
            ch423_sda_set(&(ch423s_handle->gpio_sda));
        }else{
            ch423_sda_clr(&(ch423s_handle->gpio_sda));
        }
        dat <<= 1;
        qm_usleep(1);
        ch423_scl_set(&(ch423s_handle->gpio_scl));
        qm_usleep(1);
        ch423_scl_clr(&(ch423s_handle->gpio_scl));
        qm_usleep(1);
    }
}

static unsigned char ch423_i2c_read_byte(ch423s_handle_t *ch423s_handle,unsigned char ack)    // 读一个字节数据
{
    unsigned char dat = 0;
    unsigned char i = 0;

    ch423_sda_set(&(ch423s_handle->gpio_sda));
    for( i = 0; i < 8; i++ )
    {
        ch423_scl_clr(&(ch423s_handle->gpio_scl));
        qm_usleep(1);
        ch423_scl_set(&(ch423s_handle->gpio_scl));
        qm_usleep(1);
        dat <<= 1;
        if(ch423_sda_in(&(ch423s_handle->gpio_sda))){
            dat++;
        }
        qm_usleep(1);
    }
    if(!ack){
        ch423_i2c_nack(ch423s_handle);//发送nACK
    }
    else{
        ch423_i2c_ack(ch423s_handle);//发送ACK   
    }
    return dat;
}

static void ch423_config(ch423s_handle_t *ch423s_handle, unsigned char data)    
{
    ch423_i2c_start(ch423s_handle);    // 启动总线
    ch423_i2c_write_byte(ch423s_handle, CH423_SYS_CMD);
    ch423_i2c_ack(ch423s_handle);

    ch423_i2c_write_byte(ch423s_handle, data);    // 发送数据
    ch423_i2c_ack(ch423s_handle);
    ch423_i2c_stop(ch423s_handle);    // 结束总线  
}

static void ch423_write(ch423s_handle_t *ch423s_handle, unsigned char cmd, unsigned char data)    
{
    ch423_i2c_start(ch423s_handle);    // 启动总线
    ch423_i2c_write_byte(ch423s_handle, cmd);
    ch423_i2c_ack(ch423s_handle);

    ch423_i2c_write_byte(ch423s_handle, data);    // 发送数据
    ch423_i2c_ack(ch423s_handle);
    ch423_i2c_stop(ch423s_handle);    // 结束总线  
}

static unsigned char ch423_read(ch423s_handle_t *ch423s_handle)    // 读取数据
{
    unsigned char din;
    ch423_i2c_start(ch423s_handle);    // 启动总线
    ch423_i2c_write_byte(ch423s_handle, CH423_RD_IO_CMD);    // 此值为0x4D
    ch423_i2c_ack(ch423s_handle);
    din = ch423_i2c_read_byte(ch423s_handle, 1);    // 读取数据
    ch423_i2c_stop(ch423s_handle);    // 结束总线
    return( din );
}

int qm_ch423s_set(void *handle, uint32_t gpio_pin, uint8_t value)
{
    uint8_t cmd = 0;
    uint8_t data = 0;
    uint8_t low_gpio = (uint8_t)gpio_pin;
    uint8_t high_gpio = (uint8_t)(gpio_pin>>8);
    ch423s_handle_t *ch423s_handle = (ch423s_handle_t*)handle;

    if(ch423s_handle == NULL){
        return -QM_EINVAL;
    }

    if(low_gpio){
        cmd = CH423_OC_L_CMD;
        if(value){
            ch423s_handle->ch423s_gpio_ext.gpio_ext_low_oc |= low_gpio;
        }else{
            ch423s_handle->ch423s_gpio_ext.gpio_ext_low_oc &= ~low_gpio;
        }
        data = ch423s_handle->ch423s_gpio_ext.gpio_ext_low_oc;
        ch423_write(ch423s_handle, cmd, data);
    }
    if(high_gpio){
        cmd = CH423_OC_H_CMD;
        if(value){
            ch423s_handle->ch423s_gpio_ext.gpio_ext_high_oc |= high_gpio;
        }else{
            ch423s_handle->ch423s_gpio_ext.gpio_ext_high_oc &= ~high_gpio;
        }
        data = ch423s_handle->ch423s_gpio_ext.gpio_ext_high_oc;

        ch423_write(ch423s_handle, cmd, data);
    }
    return QM_EOK;
}

#if NOT_COMPLETE

static void ext_isr_handler(void *arg)
{
    qm_ch423s_ext_irq_info_t *handle = (qm_ch423s_ext_irq_info_t *)arg;
    qm_queue_send_from_isr(&ch423s_info.ch423s_queue, (void *)handle, sizeof(int32_t));
}

static void ch423_irq_thread(void *arg)
{
    int32_t index = 0;
    uint32_t recv_len = 0;  
    uint8_t temp_vaule = 0;
    uint8_t ch423_irq_vaule = 0;   
    ch423s_handle_t *handle  = NULL; 
    qm_ch423s_ext_irq_info_t irq_info = {0};
    qm_ch423s_ext_irq_handler_t *irq_handle = NULL;
    while(1){ 
        if(qm_queue_recv(&ch423s_info.ch423s_queue, &irq_info, &recv_len, QM_WAIT_FOREVER) < 0){
            continue;
        }
        
        handle = (ch423s_handle_t *)irq_info.handle;
        qm_gpio_disable_irq(&handle->gpio_isr);
        ch423_irq_vaule = ch423_read(handle);

        QM_LOGD(LOG_TAG, "CH423_irq_vaule %02X", ch423_irq_vaule);

        for(index = 0; index < QM_CH423_IRQ_NUM_MAX; index++){

            irq_handle = (qm_ch423s_ext_irq_handler_t *)&irq_info.irq_info[index];
            temp_vaule = (ch423_irq_vaule >> index) & 1;
            QM_LOGD(LOG_TAG, "CH423_irq_IO %d VAULE %02X", irq_handle->irq_gpio, temp_vaule);
            irq_handle->irq_value = temp_vaule;

            if(!irq_handle->fn  ||  (temp_vaule == irq_handle->irq_value)){
                continue;
            }

            if(((irq_handle->irq_type == QM_CH423S_INTR_POSEDGE) ||  
                (irq_handle->irq_type == QM_CH423S_INTR_HIGH_LEVEL))  && (temp_vaule != 1)){
                continue;
            }

            if(((irq_handle->irq_type == QM_CH423S_INTR_LOW_LEVEL) || 
                (irq_handle->irq_type == QM_CH423S_INTR_NEGEDGE))  && (temp_vaule != 0)){
                continue;
            }

            irq_handle->fn(irq_handle->arg);            
        }

        ch423_write(handle, CH423_SET_IO_CMD, ch423_irq_vaule);

        qm_gpio_enable_irq(&handle->gpio_isr, QM_GPIO_INTR_NEGEDGE, ext_isr_handler, (void*)&irq_info);

    }
    qm_task_exit(NULL);
}

int qm_ch423s_get(void *handle, uint32_t gpio_pin)
{
    ch423s_handle_t *ch423s_handle = (ch423s_handle_t *)handle;
    if(ch423s_handle == NULL){
        return -QM_EINVAL;
    }
    uint8_t read_value = ch423_read(ch423s_handle);
    return  ((read_value & (uint8_t)gpio_pin) ? 1 : 0);
}

static int qm_ch423_irq_info_unused_get(void)
{
    int i = 0;
    for(i = 0; i < QM_CH423_IRQ_NUM_MAX; i++){
        if(!ch423s_info.irq_info[i].irq_used){
            return i;
        }
    }
    return -QM_EINVAL;
}

int qm_ch423s_irq_deregister(void *handle, uint32_t gpio_pin)
{
    int index = 0;
    int ret = -QM_ERROR;
    qm_ch423s_ext_irq_handler_t *irq_handle = NULL;
    ch423s_handle_t *ch423s_handle = (ch423s_handle_t *)handle;
    if(ch423s_handle == NULL){
        return -QM_EINVAL;
    }
    
    qm_gpio_disable_irq(&ch423s_handle->gpio_isr);

    for(index= 0; index < QM_CH423_IRQ_NUM_MAX; index++){
        if(!ch423s_info.irq_info[index].irq_used){
            continue;
        }

        if(ch423s_info.irq_info[index].irq_gpio == gpio_pin){
            irq_handle = &ch423s_info.irq_info[index];
            memset(irq_handle, 0, sizeof(qm_ch423s_ext_irq_handler_t));
            ret = QM_EOK;
            goto _exit;
        }
    }

_exit:
    qm_gpio_enable_irq(&ch423s_handle->gpio_isr, QM_GPIO_INTR_NEGEDGE, ext_isr_handler, (void*)&ch423s_info);
    return -ret;    
}

int qm_ch423s_irq_register(void *handle, qm_ch423s_irq_type_t intr_type, uint32_t gpio_pin, qm_ch423s_irq_handler_t handler, void *arg)
{
    int i = 0;
    int index = 0;
    uint32_t temp_value = 0;
    qm_ch423s_ext_irq_handler_t *irq_handle = NULL;
    ch423s_handle_t *ch423s_handle = (ch423s_handle_t *)handle;
    if(ch423s_handle == NULL){
        return -QM_EINVAL;
    }

    qm_gpio_disable_irq(&ch423s_handle->gpio_isr);

    for(i = 0; i < 8; i++){
        if(!(gpio_pin & (1 << i))){
            continue;
        }
        index = qm_ch423_irq_info_unused_get();
        if(index < 0){
            return -QM_EINVAL;
        }
        irq_handle = &ch423s_info.irq_info[index];
        irq_handle->irq_used = QM_TRUE;
        irq_handle->irq_gpio = i;
        irq_handle->irq_type = intr_type;
        irq_handle->fn = handler;
        irq_handle->arg = arg;        
    }

    switch (intr_type)
    {
        case QM_CH423S_INTR_POSEDGE:
        case QM_CH423S_INTR_HIGH_LEVEL:
            irq_handle->irq_value = 0;
            temp_value &= ~(irq_handle->irq_value << i);
        break;

        case QM_CH423S_INTR_NEGEDGE:
        case QM_CH423S_INTR_ANYEDGE:
        case QM_CH423S_INTR_LOW_LEVEL:
            irq_handle->irq_value = 1;
            temp_value |= (irq_handle->irq_value << i);
        break;

        default:
            memset(irq_handle, 0, sizeof(qm_ch423s_ext_irq_handler_t));
            qm_gpio_enable_irq(&ch423s_handle->gpio_isr, QM_GPIO_INTR_NEGEDGE, ext_isr_handler, (void*)&ch423s_info);
            return -QM_EINVAL;
        break;
    }
    
    qm_gpio_enable_irq(&ch423s_handle->gpio_isr, QM_GPIO_INTR_NEGEDGE, ext_isr_handler, (void*)&ch423s_info);
    return QM_EOK;
}

#endif

void* qm_ch423s_init(int scl, int sda, int isr)
{
#if NOT_COMPLETE
    qm_task_t ch423_irq_task = {0};
#endif
    ch423s_handle_t *ch423s_handle = NULL;
    ch423s_handle = (ch423s_handle_t*)qm_malloc(sizeof(ch423s_handle_t));
    if(ch423s_handle == NULL){
        return NULL;
    }
    memset(ch423s_handle, 0, sizeof(ch423s_handle_t));

    ch423s_handle->gpio_scl.port = scl;
    ch423s_handle->gpio_scl.config.mode = QM_GPIO_MODE_OUTPUT;
    ch423s_handle->gpio_scl.config.pull_en = QM_GPIO_PULLUP_ONLY;

    ch423s_handle->gpio_sda.port = sda;
    ch423s_handle->gpio_sda.config.mode = QM_GPIO_MODE_INPUT_OUTPUT;
    ch423s_handle->gpio_sda.config.pull_en = QM_GPIO_PULLUP_ONLY;

    ch423s_handle->gpio_isr.port = isr;
    ch423s_handle->gpio_isr.config.mode = QM_GPIO_MODE_INPUT;
    ch423s_handle->gpio_isr.config.pull_en = QM_GPIO_PULLUP_ONLY;

    qm_gpio_init(&ch423s_handle->gpio_scl);
    qm_gpio_init(&ch423s_handle->gpio_sda);
    
#if NOT_COMPLETE
    ch423_config(ch423s_handle, BIT_X_INT);
    ch423s_info.handle = ch423s_handle;
    ch423s_info.irq_total_value = 0xFF;
    qm_gpio_init(&ch423s_handle->gpio_isr);
    ch423_write(ch423s_handle, CH423_SET_IO_CMD, 0xFF);
    qm_gpio_enable_irq(&ch423s_handle->gpio_isr, QM_GPIO_INTR_NEGEDGE, ext_isr_handler, (void *)&ch423s_info);
    qm_queue_new(&ch423s_info.ch423s_queue, sizeof(qm_ch423s_ext_irq_info_t), QM_CH423_QUEUE_SIZE);
    qm_task_new(&ch423_irq_task, "ch423 irq_task", ch423_irq_thread, NULL, QM_CH423_TASK_SIZE, QM_CH423_TASK_PRO);
#endif
    return (void*)ch423s_handle;
}

int qm_ch423s_deinit(void *handle)
{
    if(handle == NULL){
        return -QM_EINVAL;
    }
    qm_free(handle);
    return QM_EOK;
}