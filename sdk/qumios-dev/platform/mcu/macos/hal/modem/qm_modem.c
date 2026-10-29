#include "qm_config.h"

#if CONFIG_QM_MODEM_SUPPORT

#include "qm_modem.h"
#include "qm_errno.h"

qm_modem_t qm_modem_init(const qm_modem_config_t *config)
{
    (void)config;
    return NULL;
}
int qm_modem_deinit(qm_modem_t modem) { (void)modem; return -QM_ENOSYS; }
int qm_modem_get_lbs(qm_modem_t modem) { (void)modem; return -QM_ENOSYS; }
int qm_modem_get_imsi(qm_modem_t modem, char *imsi, int imsi_len)
{
    (void)modem; (void)imsi; (void)imsi_len; return -QM_ENOSYS;
}
int qm_modem_start_ppp(qm_modem_t modem, uint32_t timeout) { (void)modem; (void)timeout; return -QM_ENOSYS; }
int qm_modem_restart_ppp(qm_modem_t modem, uint32_t timeout) { (void)modem; (void)timeout; return -QM_ENOSYS; }
int qm_modem_stop_ppp(qm_modem_t modem, uint32_t timeout) { (void)modem; (void)timeout; return -QM_ENOSYS; }

#endif

