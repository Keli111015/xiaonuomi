#include "qm_errno.h"
#include "qm_log.h"
#include "qm_kernel.h"

#include "log.h"
#include "qm_gpio.h"
#include "rtc_xx1302.h"

#define LOG_TAG  "rtc_xx1302"

#ifdef CONFIG_RTC_XX1302_SUPPORT

typedef void (*rtc_xx1302_delay_cb)(void);

typedef struct{
    qm_gpio_dev_t qm_gpio_ce;
    qm_gpio_dev_t qm_gpio_sck;
    qm_gpio_dev_t qm_gpio_sio;
} rtc_xx1302_handle_t;

static rtc_xx1302_handle_t rtc_xx1302_handle;
static rtc_xx1302_TimeData_t rtc_xx1302_TimeData = {0};
static rtc_xx1302_delay_cb rtc_xx1302_delay_handle = NULL;

// 延时函数
static inline void rtc_xx1302_delay(void)
{
    if (rtc_xx1302_delay_handle)
    {
        rtc_xx1302_delay_handle();
    }
}
// 使能引脚
static inline void rtc_xx1302_ce_set(void)
{
    qm_gpio_set_level(&rtc_xx1302_handle.qm_gpio_ce, 1);
}
static inline void rtc_xx1302_ce_clr(void)
{
    qm_gpio_set_level(&rtc_xx1302_handle.qm_gpio_ce, 0);
}
// SCK引脚
static inline void rtc_xx1302_sck_set(void)
{
    qm_gpio_set_level(&rtc_xx1302_handle.qm_gpio_sck, 1);
}
static inline void rtc_xx1302_sck_clr(void)
{
    qm_gpio_set_level(&rtc_xx1302_handle.qm_gpio_sck, 0);
}
// SIO引脚
static inline void rtc_xx1302_sio_set(void)
{
    qm_gpio_set_level(&rtc_xx1302_handle.qm_gpio_sio, 1);
}
static inline void rtc_xx1302_sio_clr(void)
{
    qm_gpio_set_level(&rtc_xx1302_handle.qm_gpio_sio, 0);
}
static inline uint8_t rtc_xx1302_sio_get(void)
{
    return qm_gpio_get_level(&rtc_xx1302_handle.qm_gpio_sio);
}
// SIO 方向控制
static inline void rtc_xx1302_sio_output(void)
{
    rtc_xx1302_handle.qm_gpio_sio.config.mode = QM_GPIO_MODE_OUTPUT;
    qm_gpio_init(&(rtc_xx1302_handle.qm_gpio_sio));
}
static inline void rtc_xx1302_sio_input(void)
{
    rtc_xx1302_handle.qm_gpio_sio.config.mode = QM_GPIO_MODE_INPUT;
    qm_gpio_init(&(rtc_xx1302_handle.qm_gpio_sio));
}

/*************************************************************************/ // RTC XX1302 driver functions
/**
 * @brief 发送一个字节到RTC XX1302
 * @param data 数据
*/
static void rtc_xx1302_write_byte(uint8_t data)
{
    uint8_t temp = data;
    for (int i = 0; i < 8; i++)
    {
        if (temp & 0x01)
        {
            rtc_xx1302_sio_set();
        }
        else
        {
            rtc_xx1302_sio_clr();
        }
        temp >>= 1;
        rtc_xx1302_delay();
        rtc_xx1302_sck_set();
        rtc_xx1302_delay();
        rtc_xx1302_sck_clr();
    }
    rtc_xx1302_delay(); // 连续写间隔
}
/**
 * @brief 从RTC XX1302读取一个字节
 * @return 读取到的数据
*/
static uint8_t rtc_xx1302_read_byte(void)
{
    rtc_xx1302_delay();
    uint8_t recv = 0;
    for (int i = 0; i < 8; i++)
    {
        recv >>= 1;  // step r/w bit
        if (rtc_xx1302_sio_get())
        {
            recv |= 0x80;
        }
        else
        {
            recv &= 0x7F;
        }
        rtc_xx1302_sck_set();
        rtc_xx1302_delay();
        rtc_xx1302_sck_clr();
        rtc_xx1302_delay();
    }
    rtc_xx1302_delay();
    return BCD_TO_DEC(recv); // 返回BCD转DEC后的数据
}

void rtc_xx1302_write_reg(uint8_t addr, uint8_t data)
{
    rtc_xx1302_sio_output();
    rtc_xx1302_ce_set();
    rtc_xx1302_write_byte(addr | RTC_REG_WRITE);
    rtc_xx1302_write_byte(data | RTC_REG_WRITE);
    rtc_xx1302_sio_clr();
    rtc_xx1302_ce_clr();
}
void rtc_xx1302_read_reg(uint8_t addr, uint8_t *data)
{
    rtc_xx1302_sio_output();
    rtc_xx1302_ce_set();
    rtc_xx1302_write_byte(addr | RTC_REG_READ);
    rtc_xx1302_sio_input();
    *data = rtc_xx1302_read_byte();
    rtc_xx1302_ce_clr();
}

void rtc_xx1302_set_time(rtc_xx1302_TimeData_t *time_data)
{
    rtc_xx1302_write_reg(RTC_XX1302_REG_CONTROL, 0x00); // 关闭写保护
    // rtc_xx1302_write_reg(RTC_XX1302_REG_RAM, 0xA5); // 开启内部充电
    rtc_xx1302_write_reg(RTC_XX1302_REG_SECOND, time_data->second);
    rtc_xx1302_write_reg(RTC_XX1302_REG_MINUTE, time_data->minute);
    rtc_xx1302_write_reg(RTC_XX1302_REG_HOUR, time_data->hour);
    rtc_xx1302_write_reg(RTC_XX1302_REG_WEEK, time_data->week);
    rtc_xx1302_write_reg(RTC_XX1302_REG_DAY, time_data->day);
    rtc_xx1302_write_reg(RTC_XX1302_REG_MONTH, time_data->month);
    rtc_xx1302_write_reg(RTC_XX1302_REG_YEAR, time_data->year);
    rtc_xx1302_write_reg(RTC_XX1302_REG_CONTROL, 0x80); // 重新开启写保护
}

rtc_xx1302_TimeData_t *rtc_xx1302_get_time(void)
{
    rtc_xx1302_read_reg(RTC_XX1302_REG_SECOND, &rtc_xx1302_TimeData.second);
    rtc_xx1302_read_reg(RTC_XX1302_REG_MINUTE, &rtc_xx1302_TimeData.minute);
    rtc_xx1302_read_reg(RTC_XX1302_REG_HOUR, &rtc_xx1302_TimeData.hour);
    rtc_xx1302_read_reg(RTC_XX1302_REG_WEEK, &rtc_xx1302_TimeData.week);
    rtc_xx1302_read_reg(RTC_XX1302_REG_DAY, &rtc_xx1302_TimeData.day);
    rtc_xx1302_read_reg(RTC_XX1302_REG_MONTH, &rtc_xx1302_TimeData.month);
    rtc_xx1302_read_reg(RTC_XX1302_REG_YEAR, &rtc_xx1302_TimeData.year);

    if (rtc_xx1302_TimeData.second == 80)
    {
#ifdef LOG_TAG
        LOG(LOG_TAG"rtc_xx1302_get_time second %d", rtc_xx1302_TimeData.second);
#endif
        rtc_xx1302_write_reg(RTC_XX1302_REG_CONTROL, 0x00); // 关闭写保护
        // rtc_xx1302_write_reg(RTC_XX1302_REG_RAM, 0xA5); // 开启内部充电
        rtc_xx1302_write_reg(RTC_XX1302_REG_SECOND, rtc_xx1302_TimeData.second & 0x7F);
        rtc_xx1302_write_reg(RTC_XX1302_REG_CONTROL, 0x80); // 重新开启写保护
    }
    return &rtc_xx1302_TimeData;
}

rtc_xx1302_TimeData_t *rtc_xx1302_time_addr_get(void)
{
    return &rtc_xx1302_TimeData;
}

/*************************************************************************/
qm_err_t rtc_xx1302_init(int ce, int sck, int sio, void *delay_handle)
{
    if(sck < 0 || sio < 0 || ce < 0 || (sck == sio)){
        return QM_EINVAL;
    }

    // if (delay_handle == NULL) {
    //     return QM_EINVAL;
    // }
    
    rtc_xx1302_delay_handle = (rtc_xx1302_delay_cb)delay_handle;

    rtc_xx1302_handle.qm_gpio_ce.port = ce;
    rtc_xx1302_handle.qm_gpio_sck.config.mode = QM_GPIO_MODE_OUTPUT;
    rtc_xx1302_handle.qm_gpio_sck.config.pull_en = QM_GPIO_PULLUP_ONLY;

    rtc_xx1302_handle.qm_gpio_sck.port = sck;
    rtc_xx1302_handle.qm_gpio_sck.config.mode = QM_GPIO_MODE_OUTPUT;
    rtc_xx1302_handle.qm_gpio_sck.config.pull_en = QM_GPIO_PULLUP_ONLY;

    rtc_xx1302_handle.qm_gpio_sio.port = sio;
    rtc_xx1302_handle.qm_gpio_sio.config.mode = QM_GPIO_MODE_OUTPUT;
    rtc_xx1302_handle.qm_gpio_sio.config.pull_en = QM_GPIO_PULLUP_ONLY;

    if(qm_gpio_init(&(rtc_xx1302_handle.qm_gpio_ce)) != QM_EOK){
        return QM_EINVAL;
    }
    if(qm_gpio_init(&(rtc_xx1302_handle.qm_gpio_sck)) != QM_EOK){
        return QM_EINVAL;
    }

    if(qm_gpio_init(&(rtc_xx1302_handle.qm_gpio_sio)) != QM_EOK){
        return QM_EINVAL;
    }
    rtc_xx1302_ce_clr(); // 初始化拉低CE引脚
    rtc_xx1302_sio_clr(); // 初始化拉低SIO引脚

    rtc_xx1302_delay();
    rtc_xx1302_get_time();

    rtc_xx1302_write_reg(RTC_XX1302_REG_CONTROL, 0x00); // 关闭写保护
    // rtc_xx1302_write_reg(RTC_XX1302_REG_RAM, 0xA5); // 开启内部充电
    rtc_xx1302_write_reg(RTC_XX1302_REG_SECOND, rtc_xx1302_TimeData.second & 0x7F);
    rtc_xx1302_write_reg(RTC_XX1302_REG_CONTROL, 0x80); // 重新开启写保护

    return QM_EOK;
}

qm_err_t rtc_xx1302_deinit(void)
{
    qm_gpio_deinit(&(rtc_xx1302_handle.qm_gpio_ce));
    qm_gpio_deinit(&(rtc_xx1302_handle.qm_gpio_sck));
    qm_gpio_deinit(&(rtc_xx1302_handle.qm_gpio_sio));

    rtc_xx1302_delay_handle = NULL;
    return QM_EOK;
}

#endif // RTC_XX1302_SUPPORT
