#include "qmos/qm.h"
#include "miio_user_api.h"
#include "mible_log.h"
#include <stdarg.h>
#include <stdio.h>
#include "localtime.h"
#include "time.h"

#include "qm_log.h"
#include "efr32_api.h"

#include "qm_rtc.h"

void qm_reboot(void)
{
    miio_system_reboot();
}

const char *qm_version_get(void)
{
    return "V2.0.0";
}

#if 0
void *qm_zalloc(unsigned int size)
{
    return NULL;
}

void *qm_realloc(void *mem, unsigned int size)
{
    return NULL;
}
#endif


extern char __HeapLimit;  /**< Defined by the linker */
extern volatile char       *_g_heap_end;

uint32_t qm_free_mem_get(void)
{
    uint32_t mfree = 0;
    char  *prev_heap_end = _g_heap_end;
    mfree = (char*)&__HeapLimit - prev_heap_end;
    return mfree;
}

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void qm_free(void *mem)
{
    free(mem);
}

uint32_t qm_now_ms(void)
{
    return (uint32_t)(mible_mesh_get_exact_systicks() & 0xFFFFFFFF);
}

int qm_msleep(int ms)
{
    return 0;
}

extern int SEGGER_RTT_vprintf(unsigned BufferIndex, const char * sFormat,
                                va_list * pParamList);
int qm_printf(const char * sFormat, ...)
{
    va_list ParamList;
    va_start(ParamList, sFormat);
    SEGGER_RTT_vprintf(0, sFormat, &ParamList);
    va_end(ParamList);
    return 0;
}

#if 0
int qm_snprintf(char *str, const int len, const char *fmt, ...)
{
    va_list args;
    int     rc;
    va_start(args, fmt);
    rc = vsnprintf(str, len, fmt, args);
    va_end(args);

    return rc;
}
#endif
void qm_init(void)
{
    qm_rtc_dev_t g_rtc_dev = {0};
    qm_rtc_init(&g_rtc_dev);
}

int RETARGET_ReadChar(void)
{
    return 0;
}

int RETARGET_WriteChar(char c)
{
    return 1;
}
