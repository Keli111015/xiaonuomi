#include "qm.h"

#include <stdarg.h>
#include <string.h>
#include <stdint.h>
#include "config.h"
#include "log_dbg.h"
#include "driver_gpio.h"
#include "driver_uart.h"

extern uint32_t loop_counter;
extern void dbg_print_string_raw(const char *format, va_list vaArgP);

void qm_reboot(void)
{
    
}

const char *qm_version_get(void)
{
    return  NULL;
}

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void qm_free(void *p)
{
    free(p);
}

uint32_t qm_now_ms(void)
{
    return (loop_counter & 0xFFFFFFFF);
}

int qm_msleep(int ms)
{
    
    return 0;
}

int qm_usleep(int us)
{
    
    return 0;
}

int qm_printf(const char * fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    dbg_print_string_raw(fmt, args);
    va_end(args);
    return 0;
}

int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int     rc;
    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);
    return rc;
}

void qm_init(void)
{

}



