

#ifndef __AIR_MATCH_HANDLE_H__
#define __AIR_MATCH_HANDLE_H__


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "ac_ir_dec.h"

#include "pwmout_api.h"
#include "timer_api.h"

#include "qm_kernel.h"

#define MAX_PATTERN_NUM 1024


#define AIR_TYPE_DATA_LEN (3*512)
#define MAX_AIR_TYPE_NUM (AIR_TYPE_DATA_LEN + 200)

#define AIR_LIGHT_PWM_PERIOD (800U)

#define GPIO_IR_SEND_PIN PA_23
#define GPIO_LIGHT_PWM_PIN PA_20
#define PERIODICAL_TIMER TIMER2

#define IR_QUEUE_LEN 1
#define IR_QUEUE_SIZE 4

//数组下标
#define PWM_IR_IDX      0
#define PWM_LIGHT_IDX   1

typedef struct ir_pwm_struct {
    uint8_t ir_pwm_is_init;
    uint8_t light_pwm_is_init;
    float ir_pwm_period;
    pwmout_t pwm_obj[2];
}ir_pwm_t;

typedef struct ir_timer_struct {
    gtimer_t ir_send_timer;
    uint8_t ir_timer_is_init;
} ir_timer_t;

typedef struct ir_queue_struct {
    uint8_t ir_send_queue_buf[4]; //0：功能码  1：临时码库（0）/匹配后的码库（1）
    qm_queue_t ir_send_queue;
} ir_queue_t;

typedef struct ir_data_struct {
    uint8_t ir_data_func_index;
    uint16_t ir_data_len;
    uint16_t ir_data_index;

    uint32_t branch_id;
    uint32_t remote_id;
    uint32_t air_type;
    
    ir_pwm_t ir_pwmout;
    ir_timer_t  ir_timer;
    ir_queue_t ir_queue;

    uint16_t air_type_data_len;
    uint16_t air_type_data_temp_len;
    uint8_t air_type_data[MAX_AIR_TYPE_NUM];
    uint8_t air_type_data_temp[MAX_AIR_TYPE_NUM];  //临时存储空调类型码，匹配成功后放入air_type_data
    uint32_t ir_data[MAX_PATTERN_NUM];
} ir_data_t;

enum {
    AIR_OPEN = 0,
    AIR_CLOSE = 1,




    AIR_HANDLE_MAX,
};


enum {
    AIR_OPEN_CLOSE_TAG = 0,
    AIR_MODE_TAG = 1,
    AIR_FAIL_TAG = 2,
    AIR_TEMP_TAG = 3,
    AIR_SWITCH_STATUS_CHANGE_TAG = 4,
    AIR_WIND_SPEED_TAG = 5,
    AIR_WIND_UPDOWN_TAG = 6,  //上下摆风
    AIR_WIND_DIRECT_TAG = 7,  //风向 action
    AIR_TIPS_LED_TAG = 8, //指示灯开关
    AIR_MATCH_STATUS_TAG = 9,

    AIR_BRAND_ID_TAG = 11,
    AIR_REMOTE_ID_TAG = 12,
    AIR_TYPE_TAG = 13,  //空调类型 1单一码  2组合码

    AIR_MATCH_TYPE_STRING1_TAG = 14,
    AIR_MATCH_TYPE_STRING2_TAG = 15,
    AIR_MATCH_TYPE_STRING3_TAG = 16,
    AIR_INFO_TAG = 17,
    AIR_MATCH_OPEN_TAG = 19,

    AIR_LIGHT_SWITCH_TAG = 20,
    AIR_LIGHT_BRIGHTNESS_TAG = 21,
    AIR_LIGHT_MODE_TAG = 22, //有无照明 0-无照明，1-有照明 2-rgb

    AIR_SET_ALL_STATUS_TAG = 23, //设置所有状态 开关,模式,温度,风速(true/false/,1,20,1  bool,uint8,uint8,uint8)
};

#define IR_TYPE_DATA_KEY   "IR_TYPE_DATA"  //空调红外类型码
#define IR_MATCH_STATUS_KEY  "IR_REG_STATUS" //匹配状态
#define AIR_STATUS_KEY     "AIR_STATUS"      //空调状态
#define IR_PERIOD_KEY      "IR_PERIOD"      //PWM频率

#define AIR_INFO_KEY       "AIR_INFO"      //空调信息(空调类型/空调品牌/遥控器id)
enum {
    AIR_TYPE = 0,
    AIR_BRAND,
    AIR_REMOTE_ID,
    AIR_INFO_MAX,
};

typedef void (*ir_send_func)(void *arg);
void pwm_start_ctrl(uint8_t flag);
int32_t ir_parse(int8_t *buf, uint8_t func, uint8_t flag);
void ir_send_task_init(void);
void ir_pwm_period_set(float period);
ir_data_t *ir_data_get(void);

uint32_t air_info_get(uint8_t index);
void air_info_set(uint8_t index, uint32_t value);

//1-100
void light_brightness_set(void);
void ir_pwm_init(float period);
void light_brightness_close(void);
#endif 

