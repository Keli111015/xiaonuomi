#include "mcu_config.h"
#include "mcu_protocol.h"

#include "qm_uart.h"

/**
 * @brief  MCU初始化函数
 * @param  Null
 * @return Null
 * @note   1、mcu_system_init 初始化代码中调用该函数, 用于用户初始化串口等外设
 *         2、用户需将初始化函数填入该函数
 */
void  mcu_platform_init(void)
{
    
}

/**
 * @brief  串口发送数据
 * @param[in] {value} 串口要发送的数据的源地址
 * @param[in] {data_len} 要接收的数据的数据长度
 * @return Null
 * @note   1、用户需将发送函数填入该函数
 */
void mcu_platform_transmit_output(unsigned char *value, unsigned short data_len)
{
    
}