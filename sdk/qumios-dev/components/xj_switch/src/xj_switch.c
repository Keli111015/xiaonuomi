#include "xj_switch.h"
#include "qm_types.h"

#include "xj_switch_event.h"
#include "qm_kv.h"

#include "qm_log.h"
// #include <sys/printk.h>


//按键操作包括：
//短按
//长按5s 松开 再长按5s 触发设备重置
//双击
//单击 长按3s
//单击 长按12s



static uint8_t _g_xj_switch_inited = 0;

static key_handle_cb key_cb = NULL;

//数组下标即为按键编号，值为对应的继电器，默认一一对应
static uint8_t _g_xj_key_map[XJ_SWITCH_MAX_KEY_NUM] = {0, 1, 2, 3};

static xj_switch_param_t g_xj_switch_param = {
    .is_led = XJ_SWITCH_LED_EXIST_DEFAULT,
    .is_save = XJ_SWITCH_RELAY_SAVE_DEFAULT, //默认保存
    .pwr_on_relay_status = XJ_SWITCH_PWRON_RELAY_LAST_STATUS,
    .led_switch = 1,
    .led_status = LED_FOLLOW_RELAY
};

static xj_switch_t g_xj_switch = {
    .switch_num = 0,
    .param = &g_xj_switch_param
};

static int xj_switch_pwr_status_set(void);
// static xj_switch_key_t _g_key[XJ_SWITCH_MAX_KEY_NUM] = {0};

// static xj_switch_relay_t _g_relay[XJ_SWITCH_MAX_KEY_NUM] = {0};

uint8_t xj_switch_is_inited(void)
{
    return _g_xj_switch_inited;
}

xj_switch_t *xj_switch_get_handle(void)
{
    if(!_g_xj_switch_inited)
    {
        return NULL;
    }

    return &g_xj_switch;
}


void xj_switch_relay_onoff(uint8_t index, uint8_t onoff)
{
    if(!_g_xj_switch_inited)
    {
        return;
    }

    for(uint8_t i = 0; i < g_xj_switch.switch_num; i++)
    {
        if(g_xj_switch.relay_list[i].id == index)
        {
            g_xj_switch.relay_list[i].ctrl_cb(onoff);
            break;
        }
    }

}

void xj_switch_led_onoff(uint8_t key_id, uint8_t index, uint8_t onoff)
{
    if(!_g_xj_switch_inited || g_xj_switch.param == NULL)
    {
        return;
    }
    //判断是否有指示灯
    if(g_xj_switch.param->is_led == 0)
    {
        return;
    }
    //有指示灯需要判断指示灯是是不是启用的
    if(g_xj_switch.param->led_switch == 0)
    {
        return;
    }
    for(uint8_t i = 0; i < g_xj_switch.switch_num; i++)
    {
        if(g_xj_switch.key_list[i].relay_idx == index)
        {
            //判断指示灯应该如何动作
            switch(g_xj_switch.param->led_status)
            {
                case LED_FOLLOW_RELAY: //跟随继电器
                    g_xj_switch.key_list[key_id].led_ctrl(onoff);
                    break;
                case LED_ALWAYS_OFF: //常闭
                    g_xj_switch.key_list[key_id].led_ctrl(SWITCH_OFF);
                    break;
                case LED_ALWAYS_ON: //常开
                    g_xj_switch.key_list[key_id].led_ctrl(SWITCH_ON);
                    break;
                case LED_OPPOSITE_RELAY:    //与继电器状态相反
                    if(onoff == SWITCH_ON)
                    {
                        g_xj_switch.key_list[key_id].led_ctrl(SWITCH_OFF);
                    }
                    else
                    {
                        g_xj_switch.key_list[key_id].led_ctrl(SWITCH_ON);
                    }
                    break;
                default:
                    break;;
            }
            break;
        }
    }
#ifdef ENABLE_QM_LOG
    QM_LOGD(ENABLE_QM_LOG,"led led_status = %d, key_id=%d, relay_idx=%d, onoff=%d\n", g_xj_switch.param->led_status, key_id, index, onoff);
#endif
}


void xj_switch_key_event_handler_register(key_handle_cb cb)
{
    key_cb = cb;
}

static void bt_attach_handler(void *handle)
{
    uint8_t idx = 0;//保存
    enum KEY_EVT event = KEY_NONE;
    uint8_t repeat = ((crazy_button_t *)handle)->repeat;
    crazy_button_t *btn = (crazy_button_t *)handle;

    for(uint8_t i = 0; i < g_xj_switch.switch_num; i++) {
        if (g_xj_switch.key_list[i].button_num == btn) {

            idx = i;
            break;
        }
    }

    switch (crazy_button_event_get(handle))
    {
    case PRESS_EVENT_REPEAT:
        if (repeat == 1)
        {
            printk("bt down SINGLE_CLICK\n");
            //通过地址匹配找到对应的映射项

            event = KEY_EVT_CLICK;

            //查看这个按键控制的继电器
            for(uint8_t j = 0; j < g_xj_switch.switch_num; j++)
            {
                if(g_xj_switch.relay_list[j].id == g_xj_switch.key_list[idx].relay_idx)
                {
                    //切换继电器状态
                    // uint8_t current_status = g_xj_switch.relay_list[j].status_get_cb();
                    // uint8_t new_status = !current_status; //取反
                    if(g_xj_switch.relay_list[j].current_status)
                    {
                        g_xj_switch.relay_list[j].current_status = 0;
                    }
                    else
                    {
                        g_xj_switch.relay_list[j].current_status = 1;
                    }

                    //直接控制，能加快响应速度
                    xj_switch_relay_onoff(g_xj_switch.relay_list[j].id, g_xj_switch.relay_list[j].current_status);
                    xj_switch_led_onoff(idx, g_xj_switch.key_list[idx].relay_idx, g_xj_switch.relay_list[j].current_status);

                    if(g_xj_switch.param->is_save)
                    {
                        xj_switch_send_param_save_event(CONFIG_COMMON_SWITCH_EVENTBUS_ID, false, XJ_SWITCH_EV_RELAY_SAVE_STATUS);
                    }
#ifdef ENABLE_QM_LOG
                    QM_LOGD(ENABLE_QM_LOG,"key idx = %d, relay idx = %d, new status = %d\n", idx, j, g_xj_switch.relay_list[j].current_status);
#endif 
                    break;
                }
            }

            
        }
        else if (repeat == 2)
        {
            event = KEY_EVT_TWO_CLICK;
            
        }
        else if(repeat == 3)
        {
            event = KEY_EVT_THREE_CLICK; 
        }
        break;
    case PRESS_EVENT_LONG_START: //1s
        event = KEY_EVT_LONG_PRESS_START;
        break;
    case PRESS_EVENT_LONG_HOLD: //1s执行一次
        event = KEY_EVT_LONG_PRESS_HOLD;
        break;
    default:
        break;
    } 
    
    if(key_cb != NULL && event != KEY_NONE)
    {
        key_cb(idx, event);
    }
}



int xj_switch_key_relay_register(xj_switch_key_t *key, uint8_t key_num, \
                    xj_switch_relay_t *relay, uint8_t relay_num)
{
    if(key == NULL || key_num == 0 || \
        relay == NULL || relay_num == 0)
    {
        return (-QM_EINVAL);
    }

    if(g_xj_switch.switch_num > XJ_SWITCH_MAX_KEY_NUM)
    {
        return (-QM_EINVAL);
    }

    if(key_num > XJ_SWITCH_MAX_KEY_NUM || relay_num > XJ_SWITCH_MAX_KEY_NUM)
    {
        return (-QM_EINVAL);
    }

    for(uint8_t i = 0; i < relay_num; i++)
    {
        uint8_t j = 0;
        for(j = 0; j < XJ_SWITCH_MAX_KEY_NUM; j++)
        {
            if(g_xj_switch.relay_list[j].is_used == 0)
            {
                g_xj_switch.relay_list[j] = relay[i];
                g_xj_switch.relay_list[j].is_used = 1;
                // xj_switch_relay_onoff(g_xj_switch.relay_list[j].id, g_xj_switch.relay_list[j].current_status);
                // xj_switch_led_onoff(i, g_xj_switch.key_list[j].relay_idx, g_xj_switch.relay_list[j].current_status);
                break;
            }
        }
        if(j == XJ_SWITCH_MAX_KEY_NUM)
        {
            return (-QM_EINVAL);
        }
    }


    for(uint8_t i = 0; i < key_num; i++)
    {
        uint8_t j = 0;
        for(j = 0; j < XJ_SWITCH_MAX_KEY_NUM; j++)
        {
            if(g_xj_switch.key_list[j].is_used == 0)
            {
                g_xj_switch.key_list[j] = key[i];
                if(g_xj_switch.key_list[i].button_num == NULL)
                {
                    continue;
                }
                g_xj_switch.key_list[j].is_used = 1;
                crazy_button_init(g_xj_switch.key_list[i].button_num, g_xj_switch.key_list[i].status_get_cb, \
                                    g_xj_switch.key_list[i].press_level);
                crazy_button_attach(g_xj_switch.key_list[i].button_num, bt_attach_handler);
                crazy_button_start(g_xj_switch.key_list[i].button_num);
                break;
            }
        }
        if(j == XJ_SWITCH_MAX_KEY_NUM)
        {
            return (-QM_EINVAL);
        }
    }

    g_xj_switch.switch_num += key_num;

    xj_switch_pwr_status_set();

    if(g_xj_switch.param->is_save)
    {
        xj_switch_send_param_save_event(CONFIG_COMMON_SWITCH_EVENTBUS_ID, false, XJ_SWITCH_EV_RELAY_SAVE_STATUS);
        xj_switch_send_param_save_event(CONFIG_COMMON_SWITCH_EVENTBUS_ID, false, XJ_SWITCH_EV_KEYLOAD_SAVE_STATUS);
    }

    // for(uint8_t i = 0; i < g_xj_switch.switch_num; i++)
    // {
    //     xj_switch_relay_onoff(g_xj_switch.relay_list[i].id, g_xj_switch.relay_list[i].current_status);
    //     xj_switch_led_onoff(i, g_xj_switch.key_list[i].relay_idx, g_xj_switch.relay_list[i].current_status);
    // }
    return QM_EOK;
}

//按键负载设置
//index: 按键编号(0-3)
//value: 负载值(xj_switch_relay_t结构体中id字段)
int xj_switch_key_setload_set(uint8_t index, uint8_t value)
{
    if(!_g_xj_switch_inited)
    {
        return (-QM_EINVAL);
    }

    if(index >= g_xj_switch.switch_num)
    {
        return (-QM_EINVAL);
    }

    g_xj_switch.key_list[index].relay_idx = value;
    if(g_xj_switch.param->is_save)
    {
        xj_switch_send_param_save_event(CONFIG_COMMON_SWITCH_EVENTBUS_ID, false, XJ_SWITCH_EV_KEYLOAD_SAVE_STATUS);
    }
    return QM_EOK;
}

//按键指示灯控制接口
//用于控制指示灯的打开和关闭或者状态设置
//led_onoff: 1-打开指示灯，0-关闭指示灯
//status: 指示灯状态，参照enum LED_STATUS定义
int xj_switch_led_status_set(uint8_t led_onoff, enum LED_STATUS status)
{
    if(!_g_xj_switch_inited || g_xj_switch.param == NULL)
    {
        return (-QM_EINVAL);
    }
    //判断是否有指示灯
    if(g_xj_switch.param->is_led == 0)
    {
        return (-QM_EINVAL);
    }

    g_xj_switch.param->led_switch = led_onoff;
    g_xj_switch.param->led_status = status;

    if(g_xj_switch.param->is_save)
    {
        xj_switch_send_param_save_event(CONFIG_COMMON_SWITCH_EVENTBUS_ID, false, XJ_SWITCH_EV_PARAM_SAVE_STATUS);
    }
    return QM_EOK;
}


uint8_t xj_switch_relay_status_get(uint8_t index)
{
    if(!_g_xj_switch_inited)
    {
        return NULL;
    }

    for(uint8_t i = 0; i < g_xj_switch.switch_num; i++)
    {
        if(g_xj_switch.relay_list[i].id == index)
        {
            return g_xj_switch.relay_list[i].current_status;
        }
    }

    return NULL;
}

//继电器上电状态设置接口
//status: 继电器上电状态，0-断开，1-闭合，2-保持上次状态
void xj_switch_relay_pwr_status_set(uint8_t status)
{
    if(!_g_xj_switch_inited)
    {
        return;
    }

    if(status >= XJ_SWITCH_PWRON_MAX_STATUS)
    {
        return;
    }

    g_xj_switch.param->pwr_on_relay_status = status;
    xj_switch_send_param_save_event(CONFIG_COMMON_SWITCH_EVENTBUS_ID, false, XJ_SWITCH_EV_PARAM_SAVE_STATUS);
}

// 

static int xj_switch_pwr_status_set(void)
{
    int ret = -1;
    uint8_t r_status[XJ_SWITCH_MAX_KEY_NUM] = {0};
    uint8_t k_load[XJ_SWITCH_MAX_KEY_NUM] = {0};
    ret |= qm_kv_get(XJ_SWITCH_DEFAULT_PARAM_KEY, &g_xj_switch_param, sizeof(g_xj_switch_param));
    ret |= qm_kv_get(XJ_SWITCH_DEFAULT_KEYLOAD_KEY, k_load, sizeof(k_load));
    ret |= qm_kv_get(XJ_SWITCH_DEFAULT_RELAY_STATUS_KEY, r_status, sizeof(r_status));
#ifdef ENABLE_QM_LOG
    QM_LOGD(ENABLE_QM_LOG,"xj_switch_pwr_status_set ret=%d\n", ret);
#endif
    if(ret == 0)
    {
        _g_xj_switch_inited = 1;
        g_xj_switch.param = &g_xj_switch_param;
        //恢复按键负载
        for(uint8_t i = 0; i < XJ_SWITCH_MAX_KEY_NUM; i++)
        {
            g_xj_switch.key_list[i].relay_idx = k_load[i];
        }

        //恢复继电器状态
        for(uint8_t i = 0; i < XJ_SWITCH_MAX_KEY_NUM; i++)
        {
            g_xj_switch.relay_list[i].current_status = r_status[i];
        }
#ifdef ENABLE_QM_LOG
        QM_LOGD(ENABLE_QM_LOG,"status = %d, %d, %d, %d\n", g_xj_switch.relay_list[0].current_status, \
               g_xj_switch.relay_list[1].current_status, \
               g_xj_switch.relay_list[2].current_status, \
               g_xj_switch.relay_list[3].current_status);

        QM_LOGD(ENABLE_QM_LOG, "xj_switch_pwr_status_set pwr_on_relay_status=%d\n", g_xj_switch.param->pwr_on_relay_status);
#endif
        //执行上电继电器状态设置
        for(uint8_t i = 0; i < XJ_SWITCH_MAX_KEY_NUM; i++)
        {
            if(g_xj_switch.param->pwr_on_relay_status == XJ_SWITCH_PWRON_RELAY_ON)
            {
                g_xj_switch.relay_list[i].current_status = SWITCH_ON;
            }
            else if(g_xj_switch.param->pwr_on_relay_status == XJ_SWITCH_PWRON_RELAY_OFF)
            {
                g_xj_switch.relay_list[i].current_status = SWITCH_OFF;
            }
            //保持上次状态则不做处理

            //执行继电器状态设置
            xj_switch_relay_onoff(g_xj_switch.relay_list[i].id, g_xj_switch.relay_list[i].current_status);

            for(uint8_t j = 0; j < g_xj_switch.switch_num; j++)
            {
                if(g_xj_switch.key_list[j].relay_idx == g_xj_switch.relay_list[i].id)
                {
                    xj_switch_led_onoff(j, g_xj_switch.key_list[j].relay_idx, g_xj_switch.relay_list[i].current_status);
                    break;
                }
            }
        }
        return QM_EOK;
    }
    ret = -1;
    return ret;
}

int xj_switch_init(xj_switch_param_t *param)
{
    int ret = -1;

    //存储初始化
    qm_kv_init();

    if(param != NULL)
    {
        if(param->is_save == 0)
        {
            g_xj_switch.param = param;
            memset(&g_xj_switch, 0, sizeof(xj_switch_t));
            g_xj_switch.switch_num = 0;
            _g_xj_switch_inited = 1;
            xj_switch_app_init();
            return QM_EOK;
        }
    }
    ret = xj_switch_pwr_status_set();
    if(ret != 0)//有问题
    {
        memset(&g_xj_switch, 0, sizeof(xj_switch_t));
        if(param == NULL)
        {
            g_xj_switch.param = &g_xj_switch_param;
        }
        else
        {
            g_xj_switch.param = param;
        }
        
        
        g_xj_switch.switch_num = 0;
    }
    
    _g_xj_switch_inited = 1;
    xj_switch_app_init();
    
    return QM_EOK;
}
