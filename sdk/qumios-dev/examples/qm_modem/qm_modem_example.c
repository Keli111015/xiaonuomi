#include "qm.h"
#include "qm_modem.h"
#include "qm_log.h"

#define LOG_TAG "qm modem"

static void event_handler(qm_modem_event_t event, void *arg)
{
    switch(event){

        case QM_MODEM_EVENT_CONN:
            QM_LOGD(LOG_TAG, "modem event conn");
        break;

        case QM_MODEM_EVENT_DISCONN:
            QM_LOGD(LOG_TAG, "modem event disconn");
        break;

        case QM_MODEM_EVENT_SIMCARD_CONN:
            QM_LOGD(LOG_TAG, "modem event simcard conn");
        break;

        case QM_MODEM_EVENT_SIMCARD_DISCONN:
            QM_LOGD(LOG_TAG, "modem event simcard disconn");
        break;

        case QM_MODEM_EVENT_RESTART:
            QM_LOGD(LOG_TAG, "modem event restart");
        break;

        case QM_MODEM_EVENT_RESTART_DONE:
            QM_LOGD(LOG_TAG, "modem event restart done");
        break;

        default:
            break;
    }

}

void qm_application_start(void)
{
    qm_err_t ret = QM_EOK;
    qm_modem_t modem = NULL;
    qm_modem_config_t cfg = {
        .rx_buffer_size = 5*1024,
        .tx_buffer_size = 5*1024,
        .channel = QM_MODEM_CHANNLE_USB,
        .event_handler = event_handler,
        .arg = NULL,
    };

    modem = qm_modem_init(&cfg);

    ret = qm_modem_start_ppp(modem, 20000);
    QM_LOGD(LOG_TAG, "qm modem start ppp :%d", ret);
}



