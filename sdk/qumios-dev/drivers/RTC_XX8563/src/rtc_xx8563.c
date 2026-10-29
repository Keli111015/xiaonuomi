#include "qm_errno.h"
#include "qm_log.h"
#include "qm_kernel.h"

#include "log.h"
#include "qm_gpio.h"
#include "rtc_xx8563.h"

#define LOG_TAG  "rtc_xx8563"

#ifdef CONFIG_RTC_XX8563_SUPPORT

static rtc_xx8563_TimeData_t rtc_xx8563_TimeData = {0};
static uint8_t reg_ctrl1_default[] = {RTC_XX8563_REG_CONTROL1, 0x00};
static uint8_t reg_ctrl2_default[] = {RTC_XX8563_REG_CONTROL2, 0x00};
static uint8_t reg_clkout_default[] = {RTC_XX8563_REG_CLKOUT, 0x00};
static uint8_t reg_time_ctrl_default[] = {RTC_XX8563_REG_TIME_CONTROL, 0x00};
static uint8_t reg_time_default[] = {RTC_XX8563_REG_TIME, 0x00};
static uint8_t reg_cfg_default[] = {RTC_XX8563_REG_CONTROL1,
                                    0x00, 
                                    // RTC_XX8563_REG_CONTROL2,
                                    0x00, 
                                    // RTC_XX8563_REG_SECOND,          
                                    // 0x22,
                                    // RTC_XX8563_REG_MINUTE,          
                                    // 0x14,
                                    // RTC_XX8563_REG_HOUR,            
                                    // 0x13,
                                    // RTC_XX8563_REG_DAY,             
                                    // 0x20,
                                    // RTC_XX8563_REG_WEEK,            
                                    // 0x02,
                                    // RTC_XX8563_REG_MONTH,           
                                    // 0x05,
                                    // RTC_XX8563_REG_YEAR,            
                                    // 0x25,
                                    // RTC_XX8563_REG_ALARM_MINUTE,   
                                    // 0x00,
                                    // RTC_XX8563_REG_ALARM_HOUR,      
                                    // 0x00,
                                    // RTC_XX8563_REG_ALARM_DAY,       
                                    // 0x00,
                                    // RTC_XX8563_REG_ALARM_WEEK,      
                                    // 0x00,
                                    // RTC_XX8563_REG_CLKOUT,          
                                    // 0x00, 
                                    // RTC_XX8563_REG_TIME_CONTROL,    
                                    // 0x00, 
                                    // RTC_XX8563_REG_TIME,            
                                    // 0x00
                                };

static uint8_t reg_time_read_default[] = {RTC_XX8563_REG_SECOND};
qm_soft_i2c_t *p_dev;

qm_err_t rtc_xx8563_init(qm_soft_i2c_t *i2c)
{
    p_dev = i2c;
    int ret = 0;
    // 清中断
    uint8_t read_data[7] = {0};
    ret = qm_soft_i2c_write(i2c, reg_cfg_default, sizeof(reg_cfg_default));

    // ret = qm_soft_i2c_write(i2c, reg_ctrl1_default, sizeof(reg_ctrl1_default));
    // ret = qm_soft_i2c_write(i2c, reg_ctrl1_default, 1);
    // ret = qm_soft_i2c_read(i2c, read_data, 1);
    // LOG("ret %d ctrl1 %02X\n", ret, read_data[0]);
    // ret = qm_soft_i2c_write(i2c, reg_ctrl2_default, sizeof(reg_ctrl2_default));
    // ret = qm_soft_i2c_write(i2c, reg_ctrl2_default, 1);
    // ret = qm_soft_i2c_read(i2c, read_data, 1);
    // LOG("ret %d ctrl2 %02X\n", ret, read_data[0]);
    // LOG("reg_cfg_default %02X-%02X-%02X %02X-%02X-%02XX\n", reg_cfg_default[i], reg_data[i+1], reg_data[i+2], reg_data[i+3]);

    // uint8_t reg_data[16] = {0};
    // ret = qm_soft_i2c_write(i2c, reg_data, 1);
    // ret = qm_soft_i2c_read(i2c, reg_data, 16);
    // LOG("reg_data %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n", reg_data[0], reg_data[1], reg_data[2], reg_data[3], reg_data[4], reg_data[5], reg_data[6], reg_data[7], reg_data[8], reg_data[9], reg_data[10], reg_data[11], reg_data[12], reg_data[13], reg_data[14], reg_data[15]);
}

void rtc_xx8563_set_time(rtc_xx8563_TimeData_t *time_data)
{
    uint8_t _data[8] = {0};
    _data[0] = RTC_XX8563_REG_SECOND;
    _data[1] = time_data->second;
    _data[2] = time_data->minute;
    _data[3] = time_data->hour;
    _data[4] = time_data->day;
    _data[5] = time_data->week;
    _data[6] = time_data->month;
    _data[7] = time_data->year;
    // LOG("set reg data %02X %02X %02X %02X %02X %02X %02X\n", _data[0], _data[1], _data[2], _data[3], _data[4], _data[5], _data[6]);
    qm_soft_i2c_write(p_dev, _data, sizeof(_data));
}

rtc_xx8563_TimeData_t *rtc_xx8563_get_time(void)
{
    uint8_t _data[7] = {0};
    qm_soft_i2c_write(p_dev, reg_time_read_default, 1);
    qm_soft_i2c_read(p_dev, _data, sizeof(_data));
    // LOG("reg_data %02X %02X %02X %02X %02X %02X %02X\n", _data[0], _data[1], _data[2], _data[3], _data[4], _data[5], _data[6]);
    rtc_xx8563_TimeData.second = _data[0];
    rtc_xx8563_TimeData.minute = _data[1];
    rtc_xx8563_TimeData.hour = _data[2];
    rtc_xx8563_TimeData.day = _data[3];
    rtc_xx8563_TimeData.week = _data[4];
    rtc_xx8563_TimeData.month = _data[5];
    rtc_xx8563_TimeData.year = _data[6];
    return &rtc_xx8563_TimeData;
}

rtc_xx8563_TimeData_t *rtc_xx8563_time_addr_get(void)
{
    return &rtc_xx8563_TimeData;
}
#endif // RTC_XX8563_SUPPORT
