/**********************************Copyright (c)**********************************
**                       版权所有 (C), 2024-2026, 
**
**                             
**
*********************************************************************************/
/**
 * @file    mcu_protocol.c
 * @author  
 * @version v2.5.6
 * @date    2020.12.16
 * @brief                
 *                       *******非常重要，一定要看哦！！！********
 *          1. 用户在此文件中实现数据下发/上报功能
 *          2. DP的ID/TYPE及数据处理函数都需要用户按照实际定义实现
 *          3. 当开始某些宏定义后需要用户实现代码的函数内部有#err提示,完成函数后请删除该#err
 */

/****************************** 免责声明 ！！！ *******************************
由于MCU类型和编译环境多种多样，所以此代码仅供参考，用户请自行把控最终代码质量，
小匠不对MCU功能结果负责。
******************************************************************************/

/******************************************************************************
                                移植须知:
1:MCU必须在while中直接调用mcu_system.c内的mcu_system_yield()函数
2:程序正常初始化完成后,建议不进行关串口中断,如必须关中断,关中断时间必须短,关中断会引起串口数据包丢失
3:请勿在中断/定时器中断内调用上报函数
******************************************************************************/

#include "mcu_config.h"
#include "mcu_protocol.h"

/******************************************************************************
                              第一步:初始化
******************************************************************************/
/*
 * @brief  模组状态通知
 * @param[in] {module_status} 模组状态，详见mcu_system_module_status_t
 * @return Null
 * @note   MCU必须实现该函数内不同网络状态的相关动作
 * 
 */
void mcu_protocol_module_status_notify(mcu_system_module_status_t module_status)
{

}

/*
 * @brief  设备快照数据上传,实现APP和muc数据同步
 * @param[in/out] {protocol_msg} response数据，详见mcu_protocol_msg_t
 * @return Null
 * @note   MCU必须实现该函数内设备信息上传功能, 接口触发时用户需往protocol_msg传入参数
 */
void mcu_protocol_get_attr(mcu_system_spec_msg_t *spec_msg)
{
    switch(spec_msg->attr_id) {
        case ATTR_ID_ONOFF:
            //开关数据组包接口
            mcu_system_spec_msg_new_boolean(spec_msg, 0);
        break;
        case ATTR_ID_MODE:
            //模式处理函数
            mcu_system_spec_msg_new_int8(spec_msg, -1);
        break;
        default:
        break;
    }
}

/**
 * @brief  下发处理函数
 * @param[in/out] {protocol_msg} set数据，详见mcu_protocol_msg_t
 * @param[in] {left_msg_len}  剩余set数据的数量
 * @return 处理结果
 * -           -1(ERROR): 失败
 * -           0(SUCCESS): 成功
 */
int mcu_protocol_set_handler(mcu_system_spec_msg_t *spec_msg, unsigned int left_msg_num)
{
    /*********************************
    当前函数处理可下发                  
    具体函数内需要实现下发数据处理
    ***********************************/
    unsigned char ret;
    switch(spec_msg->attr_id) {
        case ATTR_ID_ONOFF:
            //开关处理函数
        break;
        case ATTR_ID_MODE:
            //模式处理函数
        break;  
        default:
        break;
    }

    /*********************************
    当left_msg_num为0时，用户可调用上报数据  
    ***********************************/
   if(left_msg_num == 0){
        //上报处理函数
   }

    return ret;
}
