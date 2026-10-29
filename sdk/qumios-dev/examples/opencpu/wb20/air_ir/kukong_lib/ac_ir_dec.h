#ifndef _AC_IR_DEC_H_
#define _AC_IR_DEC_H_

#ifdef _MSC_VER
#ifdef DLL_EXPORTS
#define SO_PUBLIC __declspec(dllexport)
#elif defined(DLL_IMPORTS)
#define SO_PUBLIC __declspec(dllimport)
#else
#define SO_PUBLIC
#endif
#else
#ifdef _HIDE_PRIVATE_FUNC_
#define SO_PUBLIC __attribute__ ((visibility("default")))
#else
#define SO_PUBLIC
#endif
#endif

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//本头文件支持的功能
//获取酷控有状态空调码库支持的功能以及根据指定的状态获取对应的红外码
//注意所有指针返回参数的内存空间需要调用方分配好

//about this header file
//Obtain the functions supported by kookong stateful air conditioner remote controller codeset and obtain the corresponding infrared code according to the specified state
//Note: the memory space of all pointer return parameters need to be allocated by the caller

// get_ac_info
// 得到状态码空调遥控器(码值由当前的状态动态组合而成)状态支持情况，支持的模式，风向，每个模式支持的温度和风速范围
// data遥控器数据首地址, data_len遥控器数据总长度
// mode_flags 长度至少包含5个uint16_t元素，0-4一共5个uint16_t分别对应制冷，制热，自动，送风，除湿模式的信息
// mode_flags的每个uint16_t包含信息的规则为：
// 为0则表示不支持该模式，从低到高0-3bit存储该模式支持的温度最小值与16的差(制冷模式为例：(mode_flags[0]&0x0F)+16),
// 从低到高4-7bit存储该模式支持的温度最大值与16的差(制冷模式为例：((mode_flags[0]>>4)&0x0F)+16)
// 最小最大值都为0则表示不支持温度调节(制冷模式为例：(mode_flags[0]&0xFF)==0)
// 从低到高8-11bit每bit分别对应风速自动，低，中，高，为1则表示支持该风速，为0则表示不支持
// (制冷模式为例：((mode_flags[0]>>8)&1) 自动， ((mode_flags[0]>>9)&1) 低，((mode_flags[0]>>10)&1) 中，((mode_flags[0]>>11)&1) 高)
// wind_direct_flag 一个字节长度，返回遥控器上下风向支持情况，*wind_direct_flag = 0 表示不支持上下风向调节(get_ac_state_pulse接口wind_direct传-1），
// *wind_direct_flag = 1表示上下风向只有一组码，发这组码空调的上下风向在2-n个状态之间来回切换(get_ac_state_pulse接口wind_direct传1），
// 除此之外则表示空调上下风向有多组码（码值组数根据码组序号最大值和最小值确定），每组码对应空调上下风向的一个状态，0-3bit返回风向码组序号的最小值（(*wind_direct_flag) & 0x0F,
// 从0开始，0表示发该组码空调进入扫风状态，大于0的值表示一个固定的上下风向，可能停在当前位置或者停到一个预设的位置），
// 4-7bit返回风向码组序号的最大值（((*wind_direct_flag)>>4) & 0x0F),(get_ac_state_pulse接口wind_direct传想发送的码组序号即可，比如传0返回扫风的码组）
//
// 返回0成功，-1 data指向的数据不合法

// get_ac_info
// Obtain the functions supported by kookong stateful air conditioner remote controller codeset(supported modes, and in each mode, wind direction, temperature and wind speed range supported by the mode)
// data: ac remote controller data
// data_len: total length of ac remote control data
// mode_flags: should have at least 5 uint16_t element，element with position 0 contains information of cool mode
// and element with position 1 contains information of heat mode, and 2 for auto mode, 3 for air supply mode, 4 for dry mode
// rules of element in mode_flags:
// if it is equal to 0, the corresponding mode is not supported, else
// bit 0-3:the minimum temperature supported by this mode minus 16(take cool mode as example:(mode_flags[0]&0x0F)+16) to get the minimum temperature supported by cool mode,
// bit 4-7:the maximum temperature supported by this mode minus 16(take cool mode as example:((mode_flags[0]>>4)&0x0F)+16) to get the maximum temperature supported by cool mode
// if the minimum and maximum temperature are both 0, it means that the temperature can not be adjusted (take cool mode as example: (mode_flags[0]&0xff)==0)
// bit 8-11:each bit corresponds to the wind speed: auto, low, medium and high. if the bit is 1, the corresponding wind speed is supported, if the bit is 0, the corresponding wind speed is not supported,
// take cool mode as example: ((mode_flags[0]>>8)&1) auto wind speed, ((mode_flags[0]>>9)&1) low wind speed, ((mode_flags[0]>>10)&1) medium wind speed, ((mode_flags[0]>>11)&1) high wind speed)
// wind_direct_flag: return the info of up and down wind direction
// if *wind_direct_flag = 0, it means that up and down wind direction adjustment is not supported(in this case, pass -1 to parameter wind_direct of get_ac_state_pulse api)
// if *wind_direct_flag = 1, it means that there is only one ir code for up and down wind direction, when this code emitted, the state of up down wind direction will loop between several States(in this case, pass 1 to parameter wind_direct of get_ac_state_pulse api)
// else, it means there are several code for up and down wind direction, ervery code for one specified wind direction state
// bit 0-3: the minimum code position (*wind_direct_flag) & 0x0F,
// bit 4-7: the maximum code position ((*wind_direct_flag)>>4) & 0x0F),
// pass the value between minimum code position and maximum code position to parameter wind_direct of get_ac_state_pulse api to get the ir code for specified up and down wind direction state
// in this case, pass 0 to start swing, other value to stop swing or switch to a fixed direction and so on
//
// return 0 success，-1 ac remote controller data is illegal
SO_PUBLIC int32_t get_ac_info(const uint8_t *data, uint16_t data_len,
    uint16_t *mode_flags, uint8_t *wind_direct_flag);

// 得到指定状态的红外脉冲数据, data遥控器数据缓冲区首地址, data_len data缓冲区中有效字节数,
// power 开关机[0 开机， 1 关机], mode 模式[0-4对应制冷，制热，自动，送风，除湿],
// temperature 温度[16-30]，不支持调节温度传-1，wind_speed 风速[0-3对应自动，低，中，高]，不支持调节风量传-1，
// wind_direct 上下风向状态值，请参考get_ac_info接口中wind_direct_flag参数的说明
// pressed_fid 对应用户使用物理遥控器当前按压的按键id[电源 1，模式 2，温度+ 3，温度- 4，风量 5，扫风 6，定向风 7，如果上下风向只有一组码，则切换上下风向时传7]，frequency返回频率比如38000，
// out 调用方准备好的返回高低电平缓冲区（单位：脉冲个数），out_max_num out允许存放的最大元素个数(out指向的可用内存空间/sizeof(uint16_t))，
// ret_num 编码完成后在out内存空间中实际写了多少个uint16_t元素，frame_count 通过out返回的高低电平数组（一帧）需要重复的次数
//
// 返回值 0 成功，-1 data指向的数据不合法，-2 power 参数取值不在[01]范围内, -3 mode 参数取值不在[0-4]范围内
// -99 out缓冲区空间不足
// 注意： temperature和win_speed参数会自动使用最近的有效值，比如temperature传入15则会当16处理，传入31则会当30处理
// wind_speed传入4会当3处理

// obtain the corresponding infrared code according to the specified state
// data: ac remote controller data
// data_len: total length of ac remote control data
// power: power state[0 power on, 1 power off]
// mode: mode state[0 cool, 1 heat, 2 auto, 3 air supply, 4 dry]
// temperature: temperature[16-30], pass -1 if the temperature can not be adjusted for the current mode
// wind_speed: wind_speed[0 auto, 1 low, 2 medium, 3 high], pass -1 if the wind speed can not be adjusted for the current mode
// wind_direct: up and down wind direction, please refer to comments of parameter wind_direct_flag of get_ac_info api
// pressed_fid: the key pressed on physical remote controller [power 1, mode 2, temperature+ 3, temperature- 4, wind speed 5, swing wind 6, fixed wind 7, if there is only one ir code for up and down wind direction, pass 7]
// you'd better to pass a determined value, if you are not sure, pass 0, but some ac remote controller may have problem
// frequency: return the ir frequency, for example 38000
// out: return alternating on/off pattern(unit: number of ir pulse, you could multiple it with 1000000/frequency to get the microseconds)
// out_max_num: maximum number of elements can be stored in out(sizeof(out)/sizeof(uint16_t))
// ret_num: elements actually written in the out memory space
// frame_count: the time returned alternating on/off pattern needs to be repeated, you need to take care of this return parameter
// if it is greater than 1, then you need to send the returned alternating on/off pattern more than 1 time or refer to demo to do memcpy or memmove
// to get a big pattern to send
//
// return 0 success, -1 ac remote controller data is illegal, -2 power parameter is out of range, -3 mode parameter out out of range
// -99 space of out parameter is overflow
// note: if pass illegal value to parameter temperature or wind_speed, the nearest legal value will be used
// for example, if pass 15 to temperature, it will be processed as 16, and if pass 31 to temperature, it will be processed as 30,
// if pass 4 to wind_speed, it will be processed as 3
SO_PUBLIC int32_t get_ac_state_pulse(const uint8_t *data, uint16_t data_len,
    uint8_t power, uint8_t mode, int8_t temperature, int8_t wind_speed,
    int8_t wind_direct, int8_t pressed_fid, uint16_t *frequency, uint16_t *out,
    uint16_t out_max_num, uint16_t *ret_num, uint8_t *frame_count);

// 返回版本号，递增的整数，从1开始

// returns the version number, an increasing integer, starting from 1
SO_PUBLIC int32_t get_version();

#ifdef __cplusplus
}
#endif

#endif // _AC_IR_DEC_H_
