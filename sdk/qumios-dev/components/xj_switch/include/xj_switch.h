#ifndef _XJ_SWITCH_H_
#define _XJ_SWITCH_H_

#ifdef __cplusplus
extern "C" {
#endif


#include "qm_config.h"
#include "xj_switch_app.h"
#include "qm_types.h"
#include "crazy_button.h"

#include "qm_errno.h"

// 继电器上电状态，不设置默认上电继电器状态为断开
enum {
    XJ_SWITCH_PWRON_RELAY_OFF = 0,
    XJ_SWITCH_PWRON_RELAY_ON  = 1,
    XJ_SWITCH_PWRON_RELAY_LAST_STATUS = 2,
    XJ_SWITCH_PWRON_MAX_STATUS
};

#ifndef XJ_SWITCH_LED_EXIST_DEFAULT
#define XJ_SWITCH_LED_EXIST_DEFAULT          1
#endif

#ifndef XJ_SWITCH_RELAY_SAVE_DEFAULT
#define XJ_SWITCH_RELAY_SAVE_DEFAULT         1
#endif

#ifndef XJ_SWITCH_MAX_KEY_NUM
#define XJ_SWITCH_MAX_KEY_NUM                4
#endif

typedef uint8_t (*key_status_get_cb)(void);
typedef uint8_t (*relay_status_get_cb)(void);
typedef void    (*relay_ctrl_cb)(uint8_t level);
typedef void (*led_ctrl_cb)(uint8_t onff);

//继电器上报
typedef int (*iot_relay_status_report_cb)(uint8_t index, uint8_t status);

//按键负载上报
typedef int (*iot_key_setload_report_cb)(uint8_t index, uint8_t status);

//重置
// typedef int (*iot_key_setload_report_cb)(uint8_t index, uint8_t status);

enum {
    SWITCH_OFF,
    SWITCH_ON,


};

enum LED_STATUS {
    LED_FOLLOW_RELAY,
    LED_ALWAYS_OFF,
    LED_ALWAYS_ON,
    LED_OPPOSITE_RELAY,
};

typedef struct 
{
    uint8_t is_led; //是否有led指示灯
    uint8_t led_switch;
    enum LED_STATUS led_status; //有指示灯的话，且指示灯打开的话，指示灯状态？--跟随继电器状态？保持常灭？保持常亮？和继电器状态相反？
    uint8_t is_save; //所有参数是否保存
    uint8_t pwr_on_relay_status; //上电继电器状态
}xj_switch_param_t;


//按键维护结构体
typedef struct 
{
    crazy_button_t *button_num;         //按键对象,请务必保证指针有效不会被释放
    uint8_t is_used;                    //无需关注，随意赋值
    uint8_t press_level;                //按键按下电平
    uint8_t relay_idx;                  //控制的继电器编号,必须要和继电器id对应
    uint8_t led_status;               //无需关注，随意赋值
    led_ctrl_cb led_ctrl;                    //led控制函数
    key_status_get_cb status_get_cb;    //按键gpio状态获取
}xj_switch_key_t;

//继电器维护结构体
typedef struct 
{
    uint8_t id;
    uint8_t is_used;                            //无需关注，随意赋值
    uint8_t open_level;                         //继电器打开电平
    uint8_t current_status;                      //当前继电器状态
    relay_ctrl_cb ctrl_cb;          //继电器控制函数
}xj_switch_relay_t;

// 指示灯维护结构体
// typedef struct 
// {
//     uint8_t 
// }xj_switch_led_t;

typedef struct 
{
    uint8_t switch_num; //按键数量
    xj_switch_param_t *param;
    xj_switch_key_t key_list[XJ_SWITCH_MAX_KEY_NUM];
    xj_switch_relay_t relay_list[XJ_SWITCH_MAX_KEY_NUM];
}xj_switch_t;



//iot相关：主要是上报
typedef struct 
{

    iot_relay_status_report_cb  iot_relay_report; //继电器状态上报
    iot_key_setload_report_cb   iot_key_setload_report; //按键负载上报

}xj_switch_iot_t;

enum KEY_EVT {
    KEY_NONE = 0,
    KEY_EVT_CLICK,
    KEY_EVT_TWO_CLICK,
    KEY_EVT_THREE_CLICK,
    KEY_EVT_LONG_PRESS_START,
    KEY_EVT_LONG_PRESS_HOLD,
    
};


typedef void (*key_handle_cb)(uint8_t index, enum KEY_EVT event);

//按键操作回调
void xj_switch_key_event_handler_register(key_handle_cb cb);


//初始化函数
int xj_switch_init(xj_switch_param_t *param);
int xj_switch_key_relay_register(xj_switch_key_t *key, uint8_t key_num, \
                    xj_switch_relay_t *relay, uint8_t relay_num);

uint8_t xj_switch_is_inited(void);

//继电器led开关控制接口
//对外开放
void xj_switch_relay_onoff(uint8_t index, uint8_t onoff);

//按键负载设置
//index: 按键编号(0-3)
//value: 负载值(xj_switch_relay_t结构体中id字段)
int xj_switch_key_setload_set(uint8_t index, uint8_t value);

//按键指示灯控制接口
//用于控制指示灯的打开和关闭或者状态设置
//led_onoff: 1-打开指示灯，0-关闭指示灯
//status: 指示灯状态，参照enum LED_STATUS定义
int xj_switch_led_status_set(uint8_t led_onoff, enum LED_STATUS status);


//获取继电器状态接口
//index: 继电器编号,xj_switch_relay_t的id字段
uint8_t xj_switch_relay_status_get(uint8_t index);

void xj_switch_relay_pwr_status_set(uint8_t status);

#ifdef __cplusplus
}
#endif

#endif
