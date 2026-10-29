#ifndef __AIR_FUNC_HANDLE_H__
#define __AIR_FUNC_HANDLE_H__


#include "qm.h"
#include "qm_at.h"
#include "qm_log.h"
#include "qm_kernel.h"
#include "qm_kv.h"

#define AIR_FUNC_DATA_KEY "AIR_FUNC_DATA"

#define DEFAULT_FUNC_TEMP 26
#define DEFAULT_FUNC_MODE 0   //模式默认制冷

enum {
    AIR_FUNC_SWITCH = 0,
    AIR_FUNC_TEMP,
    AIR_FUNC_MODE,
    AIR_FUNC_WIND_SPEED,
    AIR_FUNC_WIND_DIRECTION_SWITCH,
    AIR_FUNC_WIND_DIRECTION,
    AIR_FUNC_TIPS_LED,
    AIR_FUNC_SWITCH_STATUS,
    AIR_FUNC_LIGHT_SWITCH,
    AIR_FUNC_LIGHT_BRIGHTNESS,
    AIR_FUNC_LIGHT_MODE,
    AIR_FUNC_REG_STATUS,
    AIR_FUNC_MAX,
};


// 对应用户使用物理遥控器当前按压的按键id[电源 1，模式 2，温度+ 3，温度- 4，风量 5，扫风 6，定向风 7，如果上下风向只有一组码，则切换上下风向时传7]，
enum {
    AIR_KEY_DEFAULT = 0,
    AIR_KEY_SWITCH,
    AIR_KEY_MODE,
    AIR_KEY_TEMP_ADD,
    AIR_KEY_TEMP_SUB,
    AIR_KEY_WIND_SPEED, 
    AIR_KEY_SWEPT_WING, 
    AIR_KEY_WIND_DIRECTION,
    AIR_KEY_ONE_WIND_DIRECTION,
    AIR_KEY_LIGHT_SWITCH,
    AIR_KEY_LIGHT_BRIGHTNESS,
    AIR_KEY_LIGHT_MODE,
    AIR_KEY_MAX,
};

void air_func_set(uint8_t index, int8_t value);
int8_t air_func_get(uint8_t index);
int8_t *air_func_get_buf(void);


#endif
