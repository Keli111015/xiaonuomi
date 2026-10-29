#ifndef _XJ_SWITCH_EVENT_H_
#define _XJ_SWITCH_EVENT_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "qm_config.h"
#include "qm_types.h"
#include "qm_eventbus_static.h"
#include "qm_errno.h"

#ifndef CONFIG_COMMON_SWITCH_EVENTBUS_ID
#define CONFIG_COMMON_SWITCH_EVENTBUS_ID 0
#endif

#ifndef CONFIG_COMMON_SWITCH_EVENTBUFFER_LEN
#define CONFIG_COMMON_SWITCH_EVENTBUFFER_LEN 512
#endif

#ifndef XJ_SWITCH_DEFAULT_PARAM_KEY 
#define XJ_SWITCH_DEFAULT_PARAM_KEY "param"
#endif 

#ifndef XJ_SWITCH_DEFAULT_KEYLOAD_KEY 
#define XJ_SWITCH_DEFAULT_KEYLOAD_KEY "keyload"
#endif

#ifndef XJ_SWITCH_DEFAULT_RELAY_STATUS_KEY 
#define XJ_SWITCH_DEFAULT_RELAY_STATUS_KEY "relay_status"
#endif

enum  
{
    XJ_SWITCH_MODULE_RELAY_ONOFF = 1,
    XJ_SWITCH_MODULE_LED_ONOFF,
    XJ_SWITCH_MODULE_SAVE_STATUS,
    XJ_SWITCH_MODULE_RESET_DEVICE,


    XJ_SWITCH_EV_RELAY_ON = 0,
    XJ_SWITCH_EV_RELAY_OFF,
    XJ_SWITCH_EV_LED_ON = 0,
    XJ_SWITCH_EV_LED_OFF,
    XJ_SWITCH_EV_RELAY_SAVE_STATUS = 0,
    XJ_SWITCH_EV_KEYLOAD_SAVE_STATUS,
    XJ_SWITCH_EV_PARAM_SAVE_STATUS,
    XJ_SWITCH_EV_RESET_DEVICE = 0,
};

typedef struct
{
    qm_eventbus_event_t base_ev;
    uint8_t status; //状态
    uint8_t id;     //对应哪个继电器或led
}xj_switch_led_relay_ev_t;

void xj_switch_event_init(void);

/**
 * 继电器开关事件发送
 * busid: 事件总线ID，默认为0
 * emergency: 是否为紧急事件，紧急事件会优先处理，暂未生效
 * index: 继电器索引（参照初始化注册时候的参数）
 * onoff: 继电器状态，1-打开，0-关闭
 * 
**/
int xj_switch_send_relay_onoff_event(int busid, bool emergency, int index, int onoff);


/**
 * led亮灭事件发送
 * busid: 事件总线ID，默认为0
 * emergency: 是否为紧急事件，紧急事件会优先处理，暂未生效
 * index: led索引（参照初始化注册时候的参数）
 * onoff: led状态，1-打开，0-关闭
**/
int xj_switch_send_led_onoff_event(int busid, bool emergency, int index, int onoff);


int xj_switch_send_param_save_event(int busid, bool emergency, uint8_t save_type);

#ifdef __cplusplus
}
#endif

#endif
