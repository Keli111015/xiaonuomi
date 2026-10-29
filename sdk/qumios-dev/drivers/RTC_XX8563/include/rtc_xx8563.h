#ifndef _RTX_XX8563_H
#define _RTX_XX8563_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"
#include "qm_gpio.h"
#include "qm_errno.h"
#include "qm_soft_i2c.h"

#define RTC_REG_WRITE   0
#define RTC_REG_READ    1

#define BCD_TO_DEC(x)   (((x) & 0x0F) + (((x) >> 4) & 0x0F) * 10)
#define DEC_TO_BCD(x)   ((((x) / 10) << 4) | ((x) % 10))

typedef enum
{
    RTC_XX8563_REG_CONTROL1         = 0x00, // 控制寄存器1
    RTC_XX8563_REG_CONTROL2         = 0x01, // 控制寄存器2
    RTC_XX8563_REG_SECOND           = 0x02, // 秒寄存器 seconds 00 to 59 coded in BCD
    RTC_XX8563_REG_MINUTE           = 0x03, // 分寄存器 minutes 00 to 59 coded in BCD
    RTC_XX8563_REG_HOUR             = 0x04, // 时寄存器 hours 00 to 23 coded in BCD
    RTC_XX8563_REG_DAY              = 0x05, // 日寄存器 days 01 to 31 coded in BCD
    RTC_XX8563_REG_WEEK             = 0x06, // 星期寄存器 weekdays 0 to 6 in BCD
    RTC_XX8563_REG_MONTH            = 0x07, // 月寄存器 months 01 to 12 coded in BCD
    RTC_XX8563_REG_YEAR             = 0x08, // 年寄存器 years 00 to 99 coded in BCD
    RTC_XX8563_REG_ALARM_MINUTE     = 0x09, // 闹钟，分钟寄存器 minute alarm 00 to 59 coded in BCD
    RTC_XX8563_REG_ALARM_HOUR       = 0x0A, // 闹钟，小时寄存器 hour alarm 00 to 23 coded in BCD
    RTC_XX8563_REG_ALARM_DAY        = 0x0B, // 闹钟，日寄存器 day alarm 01 to 31 coded in BCD
    RTC_XX8563_REG_ALARM_WEEK       = 0x0C, // 闹钟，星期寄存器 weekday alarm 0 to 6 in BCD
    RTC_XX8563_REG_CLKOUT           = 0x0D, // 时钟输出设置
    RTC_XX8563_REG_TIME_CONTROL     = 0x0E, // 定时器设置
    RTC_XX8563_REG_TIME             = 0x0F, // 定时器倒计数值
} rtc_xx8563_RegAddr_e;

typedef struct
{
    uint8_t second;
    uint8_t minute;
    uint8_t hour;
    uint8_t day;
    uint8_t week;
    uint8_t month;
    uint8_t year;
} rtc_xx8563_TimeData_t;

/**
 * @brief RTC XX8563 driver initialization.
 * @param ce  ce(ds8563)/rst(jsm8563) Chip Enable pin number.
 * @param sck Serial Clock Line pin number.
 * @param sio Serial Data Line pin number.
*/
qm_err_t rtc_xx8563_init(qm_soft_i2c_t *i2c);

/**
 * @brief 设置RTC XX8563时间
 * @param time_data 时间数据结构体指针
*/
void rtc_xx8563_set_time(rtc_xx8563_TimeData_t *time_data);

/**
 * @brief 获取当前时间数据
 * @return rtc_xx8563_TimeData_t 当前时间数据结构体
*/
rtc_xx8563_TimeData_t *rtc_xx8563_get_time(void);

/**
 * @brief 获取当前时间数据存储地址
 * @return rtc_xx8563_TimeData_t 当前时间数据结构体
*/
rtc_xx8563_TimeData_t *rtc_xx8563_time_addr_get(void);

#ifdef __cplusplus
}
#endif

#endif // end of _RTX_XX8563_H
