#include "qm_types.h"
#include "qm_uart.h"
#include "qm.h"
#include "qm_config.h"

#if CONFIG_QM_LOG_SUPPORT == 1
#include "SEGGER_RTT.h"
extern qm_mutex_t g_log_mutex;

int fputc(int ch, FILE *f)
{
    SEGGER_RTT_PutChar(0, ch);
	return ch;
}

static void qm_dbg_init(void)
{
   SEGGER_RTT_ConfigUpBuffer(1, NULL, NULL, 0, SEGGER_RTT_MODE_NO_BLOCK_SKIP);
}

#endif

void qm_main(void)
{
    #if CONFIG_QM_LOG_SUPPORT == 1
    qm_mutex_new(&g_log_mutex);
    qm_dbg_init();    
    #endif
    
    qm_kernel_init();

    qm_application_start();
}
