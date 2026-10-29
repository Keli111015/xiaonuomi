#ifndef _MCU_PROTOCOL_H_
#define _MCU_PROTOCOL_H_

#include "mcu_system.h"

typedef enum{
    MCU_HEARTBEAT_RESET  = 0x00,
    MCU_HEARTBEAT_COMMON = 0x01,
    MCU_HEARTBEAT_OTA    = 0x02,
}mcu_heartbeat_type_t;

typedef struct 
{
    unsigned char code;     //为是否获取时间成功标志，为 0 表示失败，为 1表示成功
    signed char timezone;   //为时区，时区，比如：8，为东八区，-8为西八区
    unsigned char year;     //为年份，0x00 表示 2000 年
    unsigned char month;    //为月份，从 1 开始到12 结束
    unsigned char day;      //为日期，从 1 开始到31 结束
    unsigned char wday;     //为星期，从 1 开始到7 结束，7表示周日
    unsigned char hour;     //为时钟，从 0 开始到23 结束
    unsigned char minute;   //为分钟，从 0 开始到59 结束
    unsigned char second;   //为秒钟，从 0 开始到59 结束
}mcu_protocol_time_t;

/*
 * @brief  模组状态通知
 * @param[in] {module_status} 模组状态，详见mcu_system_module_status_t
 * @return Null
 * @note   MCU必须实现该函数内不同网络状态的相关动作
 * 
 */
void mcu_protocol_module_status_notify(mcu_system_module_status_t module_status);

/*
 * @brief  获取MCU心跳状态
 * @param[in/out] {protocol_msg} response数据，详见mcu_protocol_msg_t
 * @return Null
 * @note   MCU必须实现该函数内设备信息上传功能, 接口触发时用户需往protocol_msg传入参数
 */
mcu_heartbeat_type_t mcu_protocol_get_heartbeat_status(void);

/*
 * @brief  获取MCU属性,实现APP和muc数据同步
 * @param[in/out] {protocol_msg} response数据，详见mcu_protocol_msg_t
 * @return Null
 * @note   MCU必须实现该函数内设备信息上传功能, 接口触发时用户需往protocol_msg传入参数
 */
void mcu_protocol_get_attr(mcu_system_spec_msg_t *spec_msg);

/**
 * @brief  下发处理函数
 * @param[in/out] {protocol_msg} set数据，详见mcu_protocol_msg_t
 * @param[in] {left_msg_len}  剩余set数据的数量
 * @return 处理结果
 * -           -1(ERROR): 失败
 * -           0(SUCCESS): 成功
 */
int mcu_protocol_set_handler(mcu_system_spec_msg_t *spec_msg, unsigned int left_msg_num);

/**
 * @brief  获取到的时间数据
 * @param[in] {time} 获取到的时间数据
 * @return Null
 * @note   MCU需要自行实现该功能
 *   time[0] 为是否获取时间成功标志，为 0 表示失败，为 1表示成功
 *   time[1] 为时区，时区，比如：8，为东八区，-8为西八区
 *   time[2] 为年份，0x00 表示 2000 年
 *   time[3] 为月份，从 1 开始到12 结束
 *   time[4] 为日期，从 1 开始到31 结束
 *   time[5] 为时钟，从 0 开始到23 结束
 *   time[6] 为分钟，从 0 开始到59 结束
 *   time[7] 为秒钟，从 0 开始到59 结束
 */
void mcu_get_time_notify(mcu_protocol_time_t *time);

#endif
