#include "xj_switch_app.h"
#include "qm_config.h"
#include "xj_switch_event.h"
#include "qm_types.h"

#include "qm_eventbus_static.h"
#include "xj_switch.h"

#include "qm_kv.h"
#include "qm_log.h"
// #include <sys/printk.h>
// #include <string.h>

// #ifdef ENABLE_QM_LOG



/**
 * 需要的事件：
 * 开关继电器
 * 开关指示灯
 * 保存继电器状态
 * 重置设备
 * 
 * 状态上报：继电器状态/指示灯状态/上电状态/按键负载情况（即按键可以选择控制任意继电器）
 * 
 * **/
static int xj_switch_relay_onoff_ev_hook(int busid, qm_eventbus_event_t *ev);
static int xj_switch_led_onoff_ev_hook(int busid, qm_eventbus_event_t *ev);
static int xj_switch_save_ev_hook(int busid, qm_eventbus_event_t *ev);
static int xj_switch_device_reset_ev_hook(int busid, qm_eventbus_event_t *ev);

extern xj_switch_t *xj_switch_get_handle(void);

static uint8_t _event_buffer[CONFIG_COMMON_SWITCH_EVENTBUFFER_LEN];
static qm_eventbus_event_table_t light_event_table[] = {
    {{XJ_SWITCH_MODULE_RELAY_ONOFF,         XJ_SWITCH_EV_RELAY_ON}, xj_switch_relay_onoff_ev_hook}, 
    {{XJ_SWITCH_MODULE_RELAY_ONOFF,         XJ_SWITCH_EV_RELAY_OFF}, xj_switch_relay_onoff_ev_hook},

    {{XJ_SWITCH_MODULE_LED_ONOFF,           XJ_SWITCH_EV_LED_ON}, xj_switch_led_onoff_ev_hook},
    {{XJ_SWITCH_MODULE_LED_ONOFF,           XJ_SWITCH_EV_LED_OFF}, xj_switch_led_onoff_ev_hook},

    {{XJ_SWITCH_MODULE_SAVE_STATUS,     XJ_SWITCH_EV_PARAM_SAVE_STATUS}, xj_switch_save_ev_hook},//
    {{XJ_SWITCH_MODULE_SAVE_STATUS,     XJ_SWITCH_EV_RELAY_SAVE_STATUS}, xj_switch_save_ev_hook},//
    {{XJ_SWITCH_MODULE_SAVE_STATUS,     XJ_SWITCH_EV_KEYLOAD_SAVE_STATUS}, xj_switch_save_ev_hook},
    {{XJ_SWITCH_MODULE_RESET_DEVICE,        XJ_SWITCH_EV_RESET_DEVICE}, xj_switch_device_reset_ev_hook},
};


//继电器开关事件处理
static int xj_switch_relay_onoff_ev_hook(int busid, qm_eventbus_event_t *ev)
{
    int ret;
    xj_switch_led_relay_ev_t relay_event;
#ifdef ENABLE_QM_LOG
    QM_LOGD(ENABLE_QM_LOG, "xj_switch_relay_onoff_ev_hook: event_id=%d\n", ev->event_id);
#endif
    ret = qm_eventbus_peek_event_data(busid, ev, &relay_event, sizeof(xj_switch_led_relay_ev_t));
    if (ret <= 0)
    {
        return ret;
    }

  
    return 0;
}

//指示灯开关事件处理
static int xj_switch_led_onoff_ev_hook(int busid, qm_eventbus_event_t *ev)
{
    int ret;
    xj_switch_led_relay_ev_t led_event;
#ifdef ENABLE_QM_LOG
    QM_LOGD(ENABLE_QM_LOG,"xj_switch_led_onoff_ev_hook: event_id=%d\n", ev->event_id);
#endif
    ret = qm_eventbus_peek_event_data(busid, ev, &led_event, sizeof(led_event));
    if (ret <= 0)
    {
        return ret;
    }

    return 0;
}

//继电器状态保存事件处理
static int xj_switch_save_ev_hook(int busid, qm_eventbus_event_t *ev)
{
    int ret;
    uint8_t save_event;
    ret = qm_eventbus_peek_event_data(busid, ev, &save_event, sizeof(uint8_t));
    if (ret <= 0)
    {
        return ret;
    }
    xj_switch_t *_xj_switch = xj_switch_get_handle();
    if(_xj_switch == NULL)
    {
        return -1;
    }
    if (EVENT_ID(ev->event_id) == XJ_SWITCH_EV_PARAM_SAVE_STATUS)
    { 
       qm_kv_set(XJ_SWITCH_DEFAULT_PARAM_KEY, (const void *)_xj_switch->param, sizeof(xj_switch_param_t), 1);

    }
    else if(EVENT_ID(ev->event_id) == XJ_SWITCH_EV_RELAY_SAVE_STATUS)
    {
        uint8_t status[XJ_SWITCH_MAX_KEY_NUM] = {0};
        for(uint8_t i = 0; i < _xj_switch->switch_num; i++)
        {
            status[i] = _xj_switch->relay_list[i].current_status;
        }
        qm_kv_set(XJ_SWITCH_DEFAULT_RELAY_STATUS_KEY, (const void *)status, sizeof(status), 1);
#ifdef ENABLE_QM_LOG
        QM_LOGD(ENABLE_QM_LOG,"Saved relay status:%d %d %d %d\n", status[0], status[1], status[2], status[3]);
#endif
    }
    else if(EVENT_ID(ev->event_id) == XJ_SWITCH_EV_KEYLOAD_SAVE_STATUS)
    {
        uint8_t status[XJ_SWITCH_MAX_KEY_NUM] = {0};
        for(uint8_t i = 0; i < _xj_switch->switch_num; i++)
        {
            status[i] = _xj_switch->key_list[i].relay_idx;
        }
        qm_kv_set(XJ_SWITCH_DEFAULT_KEYLOAD_KEY, (const void *)status, sizeof(status), 1);
#ifdef ENABLE_QM_LOG
        QM_LOGD(ENABLE_QM_LOG, "Saved keyload status:%d %d %d %d\n", status[0], status[1], status[2], status[3]);
#endif
    }
    
    return 0;
}


//设备重置事件处理
static int xj_switch_device_reset_ev_hook(int busid, qm_eventbus_event_t *ev)
{
    int ret;
    uint8_t reset_event;
    ret = qm_eventbus_peek_event_data(busid, ev, &reset_event, sizeof(uint8_t));
    if (ret <= 0)
    {
        return ret;
    }
    if (EVENT_ID(ev->event_id) == XJ_SWITCH_EV_RESET_DEVICE)
    {
        
       // TODO: 处理设备重置逻辑
    }
    return 0;
}

int xj_switch_send_relay_onoff_event(int busid, bool emergency, int index, int onoff)
{
    int ret;
    xj_switch_led_relay_ev_t ev;
    uint8_t event_id;
    if (onoff)
    {
        event_id = XJ_SWITCH_EV_RELAY_ON;
    }
    else
    {
        event_id = XJ_SWITCH_EV_RELAY_OFF;
    }
    ev.base_ev.module_id = XJ_SWITCH_MODULE_RELAY_ONOFF;
    ev.base_ev.event_id = event_id;
    ev.id = index; // index 为继电器索引
    ev.status = onoff;
    ret = qm_eventbus_post_event(busid, (qm_eventbus_event_t*)&ev, sizeof(ev));
#ifdef ENABLE_QM_LOG
    QM_LOGD(ENABLE_QM_LOG,"r: id=%d, onoff=%d, ret=%d\n", index, onoff, ret);
#endif
    return ret;
}

int xj_switch_send_send_led_onoff_event(int busid, bool emergency, int index, int onoff)
{
    int ret;
    xj_switch_led_relay_ev_t ev;
    uint8_t event_id;
    if (onoff)
    {

        event_id = emergency ? EMERGENCY_EVENT(XJ_SWITCH_EV_LED_ON) : XJ_SWITCH_EV_LED_ON;

        
    }
    else
    {
        event_id = emergency ? EMERGENCY_EVENT(XJ_SWITCH_EV_LED_OFF) : XJ_SWITCH_EV_LED_OFF;

    }
    ev.base_ev.module_id = XJ_SWITCH_MODULE_LED_ONOFF;
    ev.base_ev.event_id = event_id;
    ev.id = index; // index 为 LED 索引
    ev.status = onoff;
    ret = qm_eventbus_post_event(busid, (qm_eventbus_event_t*)&ev, sizeof(ev));
#ifdef ENABLE_QM_LOG
    QM_LOGD(ENABLE_QM_LOG,"l: id=%d, onoff=%d, ret=%d\n", index, onoff, ret);
#endif
    return ret;
}

int xj_switch_send_param_save_event(int busid, bool emergency, uint8_t save_type)
{
    int ret;
    uint8_t event_id;
    qm_eventbus_event_t ev;
    event_id = save_type;

    ev.module_id = XJ_SWITCH_MODULE_SAVE_STATUS;
    ev.event_id = event_id;
    ret = qm_eventbus_post_event(busid, (qm_eventbus_event_t*)&ev, sizeof(ev));
#ifdef ENABLE_QM_LOG
    QM_LOGD(ENABLE_QM_LOG,"s: ret=%d\n", ret);
#endif
    return ret;
}


void xj_switch_event_init(void)
{
    //事件初始化
    int err = qm_eventbus_init(CONFIG_COMMON_SWITCH_EVENTBUS_ID, _event_buffer, sizeof(_event_buffer), light_event_table, 0);
#ifdef ENABLE_QM_LOG
    QM_LOGD(ENABLE_QM_LOG,"xj_switch_event_init: err=%d\n", err);
#endif
}

