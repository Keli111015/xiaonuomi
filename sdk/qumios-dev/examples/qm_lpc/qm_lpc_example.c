#include "qm.h"
#include "qm_lpc.h"
#include "qm_log.h"

#define LOG_TAG "EXAMPLE"

static qm_task_t example_task = {0};

static int32_t iot_lpc_prepare(void) 
{

    return QM_EOK;
}

static int32_t iot_lpc_resume(void) 
{

    return QM_EOK;
}

static int32_t wake_up_cb(void)
{
    //setup rx

    return QM_EOK;
}

static int32_t iot_lpc_check(void) //If the return value is 0 means to disable sleep,others means enable.
{
    return QM_LPC_DEEP_SLEEP;
}

static void example_task_callback(void *arg)
{
	qm_lpc_init();
    qm_lpc_veto_add(1 << 0);
    qm_lpc_mode_set(QM_LPC_LIGHT_SLEEP);
    qm_lpc_check_register(iot_lpc_check);
    qm_lpc_hw_register(iot_lpc_prepare, iot_lpc_resume);

    qm_lpc_wakeup_io_config(0, QM_LPC_WAKEUP_POSEDGE, wake_up_cb , QM_TRUE);
    qm_lpc_wakeup_io_config(1, QM_LPC_WAKEUP_NEGEDGE, wake_up_cb , QM_TRUE);

    qm_msleep(5000);
    qm_lpc_mode_set(QM_LPC_DEEP_SLEEP);

    qm_msleep(5000);
    qm_lpc_mode_set(QM_LPC_POWERDOWN);

    qm_task_exit(&example_task);
}

void qm_application_start(void)
{
    qm_task_new(&example_task, "example", example_task_callback, NULL, 2048, 20);
}