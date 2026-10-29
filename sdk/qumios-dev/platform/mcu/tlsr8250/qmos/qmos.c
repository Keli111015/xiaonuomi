#include "qm.h"
#include "qm_log.h"
#include <stdarg.h>
#include "qm_rtc.h"
#include "timer.h"

#include "myprintf.h"

extern int my_printf_uart(const char *format,va_list args);

int qm_printf(const char * fmt, ...)
{
	va_list list;
	va_start( list, fmt);
	my_printf_uart(fmt,list);
    return 0;

}

uint32_t qm_now_ms(void)
{
    // return clock_time() / CLOCK_16M_SYS_TIMER_CLK_1MS;
    return clock_time_ms();
}

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void qm_free(void *mem)
{
    free(mem);
}

int32_t qm_rtc_get_timestamp(qm_rtc_dev_t *rtc, uint32_t *timestamp)
{
    //TODO: implement this function
    *timestamp = qm_now_ms() / 1000;
    return QM_EOK;
}

void qm_init(void)
{
}

int qm_msleep(int ms)
{
    sleep_ms(ms);
}

// int qm_mutex_new(qm_mutex_t *mutex)
// {
//     return QM_EOK;
//     // return -QM_EINVAL;

// }

// int qm_mutex_free(qm_mutex_t *mutex)
// {
//     return QM_EOK;
//     // return -QM_EINVAL;
// }

// int qm_mutex_lock(qm_mutex_t *mutex, unsigned int ms)
// {
//     return QM_EOK;
//     // return -QM_EINVAL;
// }

// int qm_mutex_unlock(qm_mutex_t *mutex)
// {
//     return QM_EOK;
//     // return -QM_EINVAL;
// }

// int qm_mutex_is_valid(qm_mutex_t *mutex)
// {
//     return QM_EOK;
//     // return -QM_EINVAL;
// }
