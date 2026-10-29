#ifndef _RTX_XX1302_H
#define _RTX_XX1302_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

#define RTC_REG_WRITE   0
#define RTC_REG_READ    1

#define BCD_TO_DEC(x)   (((x) & 0x0F) + (((x) >> 4) & 0x0F) * 10)
#define DEC_TO_BCD(x)   ((((x) / 10) << 4) | ((x) % 10))

typedef enum
{
    RTC_XX1302_REG_SECOND = 0x80,  // 秒寄存器
    RTC_XX1302_REG_MINUTE = 0x82,  // 分寄存器
    RTC_XX1302_REG_HOUR = 0x84,    // 时寄存器
    RTC_XX1302_REG_WEEK = 0x86,    // 星期寄存器
    RTC_XX1302_REG_DAY = 0x88,     // 日寄存器
    RTC_XX1302_REG_MONTH = 0x8A,   // 月寄存器
    RTC_XX1302_REG_YEAR = 0x8C,    // 年寄存器
    RTC_XX1302_REG_CONTROL = 0x8E, // 控制寄存器
    RTC_XX1302_REG_RAM = 0x90       // RAM寄存器
} rtc_xx1302_RegAddr_e;

typedef struct
{
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t week;
} rtc_xx1302_TimeData_t;

/**
 * @brief RTC XX1302 driver initialization.
 * @param ce  ce(ds1302)/rst(jsm1302) Chip Enable pin number.
 * @param sck Serial Clock Line pin number.
 * @param sio Serial Data Line pin number.
*/
qm_err_t rtc_xx1302_init(int ce, int sck, int sio, void *delay_handle);
qm_err_t rtc_xx1302_deinit(void);

/**
 * @brief 写入RTC XX1302寄存器
 * @param addr 寄存器地址
 * @param data 要写入的数据
*/
void rtc_xx1302_write_reg(uint8_t addr, uint8_t data);
/**
 * @brief 读取RTC XX1302寄存器
 * @param addr 寄存器地址
 * @param *data 数据存储地址
*/
void rtc_xx1302_read_reg(uint8_t addr, uint8_t *data);

/**
 * @brief 设置RTC XX1302时间
 * @param time_data 时间数据结构体指针
*/
void rtc_xx1302_set_time(rtc_xx1302_TimeData_t *time_data);

/**
 * @brief 获取当前时间数据
 * @return rtc_xx1302_TimeData_t 当前时间数据结构体
*/
rtc_xx1302_TimeData_t *rtc_xx1302_get_time(void);

/**
 * @brief 获取当前时间数据存储地址
 * @return rtc_xx1302_TimeData_t 当前时间数据结构体
*/
rtc_xx1302_TimeData_t *rtc_xx1302_time_addr_get(void);


#ifdef __cplusplus
}
#endif

#endif // end of _RTX_XX1302_H
