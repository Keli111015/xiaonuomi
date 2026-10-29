#include "qm_lpc.h"
#include "qm_lpc_hal.h"
#include "qm_log.h"

#include "qm.h"
#include "qm_utils_list.h"

#include "sleep.h"
#include "sys_stat.h"
#include "ls_soc_gpio.h"
#include "platform.h"
#include "ls_sys.h"

#define QM_LOG_TAG "LPC_HAL"

typedef struct 
{
    uint8_t pin;
    lpc_callback_t  wake_up;
}io_wake_conf_t;

static uint8_t lpc_init_ok = 0;
static volatile int lpc_check = 0;

static qm_list_t io_wake_list = {0};
static struct deep_sleep_wakeup hal_wakeup = {0};

int32_t qm_hal_lpc_init(void)
{
    lpc_init_ok = QM_TRUE;
    return QM_EOK;
}

void deep_sleep_prepare(void)
{
    if(!lpc_init_ok || lpc_check == QM_LPC_NO_SLEEP){
        return;
    }

    qm_lpc_prepare();
}

void deep_sleep_resume(void)
{
#if 0
    uint8_t pin = 0;
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    io_wake_conf_t *lps_buf = NULL;
 
    self.direction = LIST_HEAD;
    self.next = io_wake_list.head;

    if(!lpc_init_ok || lpc_check == QM_LPC_NO_SLEEP){
        return;
    }
    switch (get_wakeup_source())
    {
        case PB15_IO_WKUP:
            pin = PB15;
        break;

        case PA00_IO_WKUP:
            pin = PB15;
        break;

        case PA07_IO_WKUP:
            pin = PB15;
        break;

        case PB11_IO_WKUP:
            pin = PB15;
        break;
				
        default: goto _exit;
    }

    node = qm_list_iterator_next(&self);
    while (node)
    {
        lps_buf = (io_wake_conf_t *)node->val;
        if(lps_buf->pin == pin){
            if(lps_buf->wake_up){
            lps_buf->wake_up();
            }
            break;
        }
        node = qm_list_iterator_next(&self);
    }

_exit:
#endif
    qm_lpc_resume();
}

int32_t deep_sleep_check(void)
{
    lpc_check = qm_lpc_check();
    return lpc_check;
}

int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode)
{
    struct deep_sleep_wakeup wakeup_tmp = {0};
    memcpy(&wakeup_tmp, &hal_wakeup, sizeof(struct deep_sleep_wakeup));

    switch(mode){
        case QM_LPC_NO_SLEEP: 
            app_status_set(true);
        break;
        case QM_LPC_LIGHT_SLEEP: 
            app_status_set(false);
        break;
        case QM_LPC_DEEP_SLEEP: 
            app_status_set(false);
            wakeup_tmp.rtc = 1;                           //选择LP2模式
            enter_deep_sleep_mode_lvl2_lvl3(&wakeup_tmp);  //调用睡眠函数
        break;
        case QM_LPC_POWERDOWN: 
            app_status_set(false);
            wakeup_tmp.rtc = 0;                           //选择LP3模式
            enter_deep_sleep_mode_lvl2_lvl3(&wakeup_tmp);  //调用睡眠函数
        break;
        default:break;
    }
    return QM_EOK;
}

int32_t qm_hal_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t cb ,bool_t enable)
{
    uint8_t lps_type = 0;
    qm_list_node_t *node = NULL;
    io_wake_conf_t *wake_config = NULL;

    if(type == QM_LPC_WAKEUP_LOW_LEVEL || type == QM_LPC_WAKEUP_NEGEDGE){
        lps_type = 0;
        io_cfg_input(pin);                       
        io_pull_write(pin, IO_PULL_UP);         
    }else if (type == QM_LPC_WAKEUP_HIGH_LEVEL || type == QM_LPC_WAKEUP_POSEDGE){
        lps_type = 1;
        io_cfg_input(pin);                       
        io_pull_write(pin, IO_PULL_DOWN);         
    }

    switch (pin)
    {
        case PA00:
            hal_wakeup.pa00 = enable;
            hal_wakeup.pa00_rising_edge = lps_type;
        break;
        case PA07:
            hal_wakeup.pa07 = enable;
            hal_wakeup.pa07_rising_edge = lps_type;
        break;
        case PB11:
            hal_wakeup.pb11 = enable;
            hal_wakeup.pb11_rising_edge = lps_type;
        break;
        case PB15:
            hal_wakeup.pb15 = enable;
            hal_wakeup.pb15_rising_edge = lps_type;
        break;
        default:
        break;
    }    

    if(enable){
        node = qm_list_node_extra_new(sizeof(io_wake_conf_t));
        if(node == NULL){
            return -QM_ENOMEM;
        }
        wake_config = (io_wake_conf_t *)node->val;

        wake_config->pin = pin;
        wake_config->wake_up = cb;
        qm_list_rpush(&io_wake_list, node);
    }else{
        qm_list_iterator_t self;
        self.direction = LIST_HEAD;
        self.next = io_wake_list.head;
        while (node)
        {
            wake_config = (io_wake_conf_t *)node->val;
            if(wake_config->pin == pin){
                qm_list_remove(&io_wake_list, node);
                qm_list_node_destroy(node);
                break;
            }
            node = qm_list_iterator_next(&self);
        }
    }

    return QM_EOK;
}