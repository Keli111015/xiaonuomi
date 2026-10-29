#ifndef __MCU_PLATFORM_H__
#define __MCU_PLATFORM_H__

/**
 * @brief  把src所指内存区域的前count个字节设置成字符c
 * @param[out] {src} 待设置的内存首地址
 * @param[in] {ch} 设置的字符
 * @param[in] {count} 设置的内存长度
 * @return 待设置的内存首地址
 */
void *mcu_platform_memset(void *src,unsigned char ch,unsigned short count);

/**
 * @brief  内存拷贝
 * @param[out] {dest} 目标地址
 * @param[in] {src} 源地址
 * @param[in] {count} 拷贝数据个数
 * @return 数据处理完后的源地址
 */
void *mcu_platform_memcpy(void *dest, const void *src, unsigned short count);

/**
 * @brief  MCU初始化函数
 * @param  Null
 * @return Null
 * @note   1、mcu_system_init 初始化代码中调用该函数, 用于用户初始化串口等外设
 *         2、用户需将初始化函数填入该函数
 */
void  mcu_platform_init(void);

/**
 * @brief  串口接收多个字节数据暂存处理
 * @param[in] {value} 要接收的数据的源地址
 * @param[in] {data_len} 要接收的数据的数据长度
 * @return Null
 * @note   1、在MCU串口处理函数中调用该函数,并将接收到的数据作为参数传入
 *         2、数据由MCU_SDK处理,用户请勿再另行处理
 *         3、该接口内容已实现，请勿更改
 */
void mcu_platform_receive_buff_input(unsigned char *value, unsigned short data_len);

/**
 * @brief  串口发送数据
 * @param[in] {value} 串口要发送的数据的源地址
 * @param[in] {data_len} 要接收的数据的数据长度
 * @return Null
 * @note   1、用户需将发送函数填入该函数
 */
void mcu_platform_transmit_output(unsigned char *value, unsigned short data_len);

#endif
