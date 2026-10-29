#include "qm.h"
#include "tm1651.h"
#include "qm_log.h"
#include "qm_gpio.h"

#define CODE_E          (0x79)  //字符E 显示编码
#define CODE__          (0x00)  //无字符

#define TM1651_COMMAND_SET_DATA			(0x40)
#define TM1651_COMMAND_SET_ADDRESS		(0xC0)
#define TM1651_COMMAND_SET_DISPLAY		(0x8F)

#define TM1651_USLEEP_10   (10)
#define TM1651_USLEEP_20   (20)

typedef struct{
    qm_gpio_dev_t qm_gpio_scl;
    qm_gpio_dev_t qm_gpio_sda;
}tm1651_handle_t;

static const uint8_t show_code[10] = { 0x3f, 0x06, 0x5b, 0x4f, 0x66, 
                                       0x6d, 0x7d, 0x07, 0x7f, 0x6f};//数字显示编码0-9

static inline void tm1651_sda_set(qm_gpio_dev_t *qm_gpio_sda)
{
    qm_gpio_set_level(qm_gpio_sda, 1);
}
static inline void tm1651_sda_clr(qm_gpio_dev_t *qm_gpio_sda)
{
    qm_gpio_set_level(qm_gpio_sda, 0);
}

static inline void tm1651_scl_set(qm_gpio_dev_t *qm_gpio_scl)
{
    qm_gpio_set_level(qm_gpio_scl, 1);
}
static inline void tm1651_scl_clr(qm_gpio_dev_t *qm_gpio_scl)
{
    qm_gpio_set_level(qm_gpio_scl, 0);
}

static void tm1651_i2c_start(tm1651_handle_t *tm1651_handle)    // 操作起始
{
    tm1651_scl_set(&(tm1651_handle->qm_gpio_scl));
    tm1651_sda_set(&(tm1651_handle->qm_gpio_sda));
    qm_usleep(TM1651_USLEEP_20);
    tm1651_sda_clr(&(tm1651_handle->qm_gpio_sda));
    qm_usleep(TM1651_USLEEP_20);
}

static void tm1651_i2c_stop(tm1651_handle_t *tm1651_handle)    // 操作结束
{
    tm1651_sda_clr(&(tm1651_handle->qm_gpio_sda));
    qm_usleep(TM1651_USLEEP_20);
    tm1651_scl_set(&(tm1651_handle->qm_gpio_scl));
    qm_usleep(TM1651_USLEEP_20);
    tm1651_sda_set(&(tm1651_handle->qm_gpio_sda));
    qm_usleep(TM1651_USLEEP_20);
}

static void tm1651_i2c_ack(tm1651_handle_t *tm1651_handle)
{
    tm1651_scl_clr(&(tm1651_handle->qm_gpio_scl));
    qm_usleep(TM1651_USLEEP_10);
    tm1651_sda_set(&(tm1651_handle->qm_gpio_sda));
    qm_usleep(TM1651_USLEEP_10);
    tm1651_scl_set(&(tm1651_handle->qm_gpio_scl));
    qm_usleep(TM1651_USLEEP_20);
    tm1651_scl_clr(&(tm1651_handle->qm_gpio_scl));
    qm_usleep(TM1651_USLEEP_20);
}

static void tm1651_i2c_write_byte(tm1651_handle_t *tm1651_handle, uint8_t byte)    // 写一个字节数据
{
    for(int i = 0; i < 8; i++)
    {
        tm1651_scl_clr(&(tm1651_handle->qm_gpio_scl));
        qm_usleep(TM1651_USLEEP_20);
        if(byte & 0x01){
            tm1651_sda_set(&(tm1651_handle->qm_gpio_sda));
        }else{
            tm1651_sda_clr(&(tm1651_handle->qm_gpio_sda));
        }
        qm_usleep(TM1651_USLEEP_20);
        byte = byte >> 1;
        tm1651_scl_set(&(tm1651_handle->qm_gpio_scl));
        qm_usleep(TM1651_USLEEP_20);
    }
}

static qm_err_t tm1651_display_num(void* handle, uint8_t num1, uint8_t num2)
{
    tm1651_handle_t *tm1651_handle = (tm1651_handle_t*)handle;
    if((num1 > 9) || (num2 > 9) || (tm1651_handle == NULL)){
        return -QM_EINVAL;
    }

    tm1651_i2c_start(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, TM1651_COMMAND_SET_DATA);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_stop(tm1651_handle);

    tm1651_i2c_start(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, TM1651_COMMAND_SET_ADDRESS);
    tm1651_i2c_ack(tm1651_handle);

    tm1651_i2c_write_byte(tm1651_handle, show_code[num1]);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, show_code[num1]);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, show_code[num2]);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, show_code[num2]);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_stop(tm1651_handle);

    tm1651_i2c_start(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, TM1651_COMMAND_SET_DISPLAY);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_stop(tm1651_handle);

    return QM_EOK;
}

qm_err_t tm1651_display_code(void* handle, uint8_t code1, uint8_t code2)
{
    tm1651_handle_t *tm1651_handle = (tm1651_handle_t*)handle;
    if(tm1651_handle == NULL){
        return -QM_EINVAL;
    }

    tm1651_i2c_start(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, TM1651_COMMAND_SET_DATA);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_stop(tm1651_handle);

    tm1651_i2c_start(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, TM1651_COMMAND_SET_ADDRESS);
    tm1651_i2c_ack(tm1651_handle);

    tm1651_i2c_write_byte(tm1651_handle, code1);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, code1);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, code2);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, code2);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_stop(tm1651_handle);

    tm1651_i2c_start(tm1651_handle);
    tm1651_i2c_write_byte(tm1651_handle, TM1651_COMMAND_SET_DISPLAY);
    tm1651_i2c_ack(tm1651_handle);
    tm1651_i2c_stop(tm1651_handle);

    return QM_EOK;
}

static void* tm1651_init(int scl, int sda)
{
    tm1651_handle_t *tm1651_handle = NULL;

    if(scl < 0 || sda < 0 || (scl == sda)){
        return NULL;
    }

    tm1651_handle = (tm1651_handle_t*)qm_malloc(sizeof(tm1651_handle_t));
    if(tm1651_handle == NULL){
        return NULL;
    }
    memset(tm1651_handle, 0, sizeof(tm1651_handle_t));

    tm1651_handle->qm_gpio_scl.port = scl;
    tm1651_handle->qm_gpio_scl.config.mode = QM_GPIO_MODE_OUTPUT;
    tm1651_handle->qm_gpio_scl.config.pull_en = QM_GPIO_PULLUP_ONLY;

    tm1651_handle->qm_gpio_sda.port = sda;
    tm1651_handle->qm_gpio_sda.config.mode = QM_GPIO_MODE_OUTPUT;
    tm1651_handle->qm_gpio_sda.config.pull_en = QM_GPIO_PULLUP_ONLY;

    if(qm_gpio_init(&(tm1651_handle->qm_gpio_scl)) != QM_EOK){
        qm_free(tm1651_handle);
        return NULL;
    }

    if(qm_gpio_init(&(tm1651_handle->qm_gpio_sda)) != QM_EOK){
        qm_gpio_deinit(&(tm1651_handle->qm_gpio_scl));
        qm_free(tm1651_handle);
        return NULL;
    }
    return (void*)tm1651_handle;
}

qm_err_t tm1651_deinit(void *handle)
{
    tm1651_handle_t *tm1651_handle = (tm1651_handle_t *)handle;
    if(handle == NULL){
        return -QM_EINVAL;
    }

    qm_gpio_deinit(&(tm1651_handle->qm_gpio_scl));
    qm_gpio_deinit(&(tm1651_handle->qm_gpio_sda));

    qm_free(tm1651_handle);
    tm1651_handle = NULL;
    return QM_EOK;
}

qm_err_t aupu_display_num(void *handle, uint8_t num)
{
    return tm1651_display_num(handle, num / 10, num % 10);
}

qm_err_t tm1651_display_off(void *handle)
{
    return tm1651_display_code(handle, CODE__, CODE__);
}

qm_err_t aupu_display_errcode(void *handle, uint8_t num)
{
    return tm1651_display_code(handle, CODE_E, show_code[num]);
}