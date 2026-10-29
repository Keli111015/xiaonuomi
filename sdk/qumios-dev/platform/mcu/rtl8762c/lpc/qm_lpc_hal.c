#include "qm_lpc.h"
#include "qm_lpc_hal.h"
#include "qm_log.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_utils_list.h"

#include "dlps.h"
#include <rtl876x_pinmux.h>
#include <rtl876x_io_dlps.h>

#define QM_LOG_TAG "LPC_HAL"

typedef struct 
{
    uint8_t pin;
    lpc_callback_t  wake_up;
    PAD_WAKEUP_POL_VAL type;
}io_wake_conf_t;

typedef struct{
    qm_timer_t lpc_timer;
    qm_list_t  io_wake_list;
    qm_mutex_t io_wake_mutex;
    lpc_callback_t lpc_delay_cb;
}qm_lpc_hal_ctx_t;

static qm_lpc_hal_ctx_t *g_lpc_ctx = NULL;


static void io_wakeup_delay_cb( qm_timer_t *timer, void *arg)
{
    qm_timer_stop(&g_lpc_ctx->lpc_timer);
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    io_wake_conf_t *lps_buf = NULL;
 
    self.direction = LIST_HEAD;
    self.next = g_lpc_ctx->io_wake_list.head;

    qm_mutex_lock(&g_lpc_ctx->io_wake_mutex, QM_WAIT_FOREVER);
    node = qm_list_iterator_next(&self);
    while (node)
    {
        lps_buf = (io_wake_conf_t *)node->val;

        if(System_WakeUpInterruptValue(lps_buf->pin)){
#ifdef QM_LOG_TAG
            QM_LOGD(QM_LOG_TAG, "WAKEUP || PIN %d", lps_buf->pin);
#endif
            if(lps_buf->wake_up){
                lps_buf->wake_up();
            }
             // 防止一些带有互斥锁的接口使模组死机
            GPIO_ClearINTPendingBit(GPIO_GetPin(lps_buf->pin));
            break;
        }
        node = qm_list_iterator_next(&self);
    }
    qm_mutex_unlock(&g_lpc_ctx->io_wake_mutex);
}

void System_Handler(void)
{
    NVIC_DisableIRQ(System_IRQn);
    qm_timer_change(&g_lpc_ctx->lpc_timer, 1); 
    // MUST clear debounce bit here. (NONE KNOWS WHAT CODE IS, WTF?)
    btaon_fast_write_safe(0x2b, (btaon_fast_read_safe(0x2b) | BIT7));

    NVIC_ClearPendingIRQ(System_IRQn);
}

static void app_enter_dlps_hook(void)
{
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    io_wake_conf_t *lps_buf = NULL;
 
    self.direction = LIST_HEAD;
    self.next = g_lpc_ctx->io_wake_list.head;

    node = qm_list_iterator_next(&self);

   while (node)
   {
        lps_buf = (io_wake_conf_t *)(node->val);      
        System_WakeUpPinEnable(lps_buf->pin, lps_buf->type, PAD_WK_DEBOUNCE_DISABLE);
        node = qm_list_iterator_next(&self);
   }

    qm_lpc_prepare();
}

static void app_exit_dlps_hook(void)
{
    qm_lpc_resume();
}


extern bool mi_mesh_otp_is_existed(void);

static bool app_mp_check(void)
{
    return mi_mesh_otp_is_existed();
}

static bool app_dlps_check_fn(void)
{
    return (app_mp_check() & qm_lpc_check());
}

int32_t qm_hal_lpc_init(void)
{
    g_lpc_ctx = (qm_lpc_hal_ctx_t*)qm_malloc(sizeof(qm_lpc_hal_ctx_t));
    if(g_lpc_ctx == NULL){
        return -QM_ENOMEM;
    } 
    
    int ret = QM_EOK;
    memset(g_lpc_ctx, 0, sizeof(qm_lpc_hal_ctx_t));
    g_lpc_ctx->io_wake_list.free = qm_free;

    ret = qm_mutex_new(&g_lpc_ctx->io_wake_mutex);
    if(ret != QM_EOK){
        goto _exit;
    }

    ret = qm_timer_new(&g_lpc_ctx->lpc_timer, io_wakeup_delay_cb, (void *)g_lpc_ctx->lpc_delay_cb, 10, 0);
    if(ret != QM_EOK){
        goto _exit;
    }

    if (QM_FALSE == dlps_check_cb_reg(app_dlps_check_fn)){
        ret =  -QM_EINVAL;
        goto _exit;
    }
    DLPS_IORegUserDlpsEnterCb(app_enter_dlps_hook);
    DLPS_IORegUserDlpsExitCb(app_exit_dlps_hook);
    DLPS_IORegister();
    return QM_EOK;

_exit:
    if(g_lpc_ctx){
        qm_free(g_lpc_ctx);
        g_lpc_ctx = NULL;
    }
    return ret;   
}

int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode)
{
    LPSMode lps_mode = LPM_ACTIVE_MODE;
    switch(mode){
        case QM_LPC_NO_SLEEP: 
            lps_mode = LPM_ACTIVE_MODE;
            break;
        case QM_LPC_LIGHT_SLEEP: 
            lps_mode = LPM_LPS_MODE;
            break;
        case QM_LPC_DEEP_SLEEP: 
            lps_mode = LPM_DLPS_MODE;
            break;
        default:break;
    }    
    lps_mode_set(lps_mode);
    return QM_EOK;
}


static  int32_t qm_hal_lpc_wakeup_io_enable(io_wake_conf_t *lps_buf)
{
    if(lps_buf == NULL){
        return -QM_EINVAL;
    }

#ifdef QM_LOG_TAG
    QM_LOGD(QM_LOG_TAG,"IO CONFIG PIN %d WAKE_TYPE %d", lps_buf->pin, lps_buf->type);
#endif  
    qm_list_node_t *node = qm_list_node_new((void *)lps_buf);
    if(node == NULL){
        return -QM_ENOMEM;
    }

    qm_mutex_lock(&g_lpc_ctx->io_wake_mutex, QM_WAIT_FOREVER);
    qm_list_rpush(&g_lpc_ctx->io_wake_list, node);
    qm_mutex_unlock(&g_lpc_ctx->io_wake_mutex);

    return QM_EOK;
}

static  int32_t qm_hal_lpc_wakeup_io_disable(io_wake_conf_t *lps_buf)
{
    if(lps_buf == NULL){
        return -QM_EINVAL;
    }
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    io_wake_conf_t *buf = NULL; 
    io_wake_conf_t *buf_match = (io_wake_conf_t *)lps_buf; 

    self.direction = LIST_HEAD;
    self.next = g_lpc_ctx->io_wake_list.head;

    qm_mutex_lock(&g_lpc_ctx->io_wake_mutex, QM_WAIT_FOREVER);
    node = qm_list_iterator_next(&self);
    
    while (node)
    {
        buf = (io_wake_conf_t *)node->val;
        if(buf->pin == buf_match->pin){
            System_WakeUpPinDisable(buf->pin);
            qm_list_remove(&g_lpc_ctx->io_wake_list, node);
            qm_list_node_destroy(node);
            break;
        }
        node = qm_list_iterator_next(&self);
    }
    qm_mutex_unlock(&g_lpc_ctx->io_wake_mutex);

    qm_free(lps_buf);
    lps_buf = NULL;
    return QM_EOK;
}

int32_t qm_hal_lpc_wakeup_io_config(uint8_t pin, uint16_t type, lpc_callback_t cb ,bool_t enable)
{

    int ret = QM_EOK;
    io_wake_conf_t *lps_buf = NULL;
    lps_buf = (io_wake_conf_t *)qm_malloc(sizeof(io_wake_conf_t));
    if(lps_buf == NULL){
        return -QM_ENOMEM;
    }

    PAD_WAKEUP_POL_VAL lps_type;
    if(type == QM_LPC_WAKEUP_LOW_LEVEL || type == QM_LPC_WAKEUP_NEGEDGE){
        lps_type = PAD_WAKEUP_POL_LOW;
    }else if (type == QM_LPC_WAKEUP_HIGH_LEVEL || type == QM_LPC_WAKEUP_POSEDGE){
        lps_type = PAD_WAKEUP_POL_HIGH;
    }

    lps_buf->pin      = pin; 
    lps_buf->type     = lps_type;

    if(cb){
        lps_buf->wake_up  =  cb;
    } 
 
    if(enable == QM_TRUE){
        ret = qm_hal_lpc_wakeup_io_enable(lps_buf);
    }else{
        ret = qm_hal_lpc_wakeup_io_disable(lps_buf);
    }
    return ret;
}