/*
 * AC7911BX low-power policy adapter.
 *
 * The WL82 board power driver owns the actual sleep entry and wake-source
 * table. QUMI LPC is therefore used as the common policy/veto surface; the
 * explicit soft-poweroff transition is exposed by qm_lpc_enter_powerdown().
 */

#define BOOL_DEFINE_CONFLICT

#include "qm_errno.h"
#include "qm_lpc_hal.h"
#include "app_power_manage.h"
#include "asm/system_reset_reason.h"
#include "system/includes.h"

/* The SDK implementation exists, but its public header omits this symbol. */
extern void system_alarm_wakeup_clear(void);

int32_t qm_hal_lpc_init(void)
{
    return QM_EOK;
}

int32_t qm_hal_lpc_mode_set(qm_lpc_mode_t mode)
{
    switch (mode) {
    case QM_LPC_NO_SLEEP:
    case QM_LPC_POWERDOWN:
        return QM_EOK;
    case QM_LPC_LIGHT_SLEEP:
    case QM_LPC_DEEP_SLEEP:
        return -QM_ENOSYS;
    default:
        return -QM_EINVAL;
    }
}

int32_t qm_hal_lpc_wakeup_io_config(uint8_t pin, uint16_t type,
                                    lpc_callback_t cb, bool_t enable)
{
    (void)pin;
    (void)type;
    (void)cb;
    (void)enable;
    /* AC7911BX wake GPIOs are declared in board_7911B.c's static table. */
    return -QM_ENOSYS;
}

int32_t qm_hal_lpc_enter_powerdown(void)
{
    sys_power_poweroff();
    return -QM_EIO;
}

qm_lpc_wakeup_reason_t qm_hal_lpc_wakeup_reason_get(void)
{
    int reason = system_reset_reason_get();

    if (reason & SYS_RST_ALM_WKUP) {
        return QM_LPC_WAKEUP_REASON_RTC_ALARM;
    }
    if (reason & SYS_RST_PORT_WKUP) {
        return QM_LPC_WAKEUP_REASON_GPIO;
    }
    return QM_LPC_WAKEUP_REASON_NONE;
}

int32_t qm_hal_lpc_wakeup_reason_clear(void)
{
    int reason = system_reset_reason_get();

    if (reason & SYS_RST_ALM_WKUP) {
        system_alarm_wakeup_clear();
        return QM_EOK;
    }
    if (reason & SYS_RST_PORT_WKUP) {
        return -QM_ENOSYS;
    }

    return QM_EOK;
}

int32_t qm_hal_lpc_enable(void)
{
    /* Powerdown is entered explicitly; no autonomous platform mode to enable. */
    return QM_EOK;
}

int32_t qm_hal_lpc_disable(void)
{
    /* qm_lpc_check() enforces vetoes before the explicit powerdown entry. */
    return QM_EOK;
}
