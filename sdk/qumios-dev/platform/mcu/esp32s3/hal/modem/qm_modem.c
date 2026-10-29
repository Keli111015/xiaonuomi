#include "qm_modem.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_log.h"

#include <arpa/inet.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "usbh_modem_board.h"
#include "usbh_modem_wifi.h"
#include "esp_modem_dce_common_commands.h"

#define LOG_TAG "modem"

static qm_modem_config_t g_qm_modem_config = {0};

static modem_config_t modem_config = MODEM_DEFAULT_CONFIG();

static void on_modem_event(void *arg, esp_event_base_t event_base,
                           int32_t event_id, void *event_data)
{
    if (event_base == MODEM_BOARD_EVENT) {
        if ( event_id == MODEM_EVENT_SIMCARD_DISCONN) {
            QM_LOGD(LOG_TAG, "Modem Board Event: SIM Card disconnected");
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_SIMCARD_DISCONN, NULL, 0, g_qm_modem_config.arg);
        } else if ( event_id == MODEM_EVENT_SIMCARD_CONN) {
            QM_LOGD(LOG_TAG, "Modem Board Event: SIM Card Connected");
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_SIMCARD_CONN, NULL, 0, g_qm_modem_config.arg);
        } else if ( event_id == MODEM_EVENT_DTE_DISCONN) {
            QM_LOGD(LOG_TAG, "Modem Board Event: USB disconnected");
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_DISCONN, NULL, 0, g_qm_modem_config.arg);
        } else if ( event_id == MODEM_EVENT_DTE_CONN) {
            QM_LOGD(LOG_TAG, "Modem Board Event: USB connected");
            
        } else if ( event_id == MODEM_EVENT_DTE_RESTART) {
            QM_LOGD(LOG_TAG, "Modem Board Event: Hardware restart");
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_RESTART, NULL, 0, g_qm_modem_config.arg);
        } else if ( event_id == MODEM_EVENT_DTE_RESTART_DONE) {
            QM_LOGD(LOG_TAG, "Modem Board Event: Hardware restart done");
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_RESTART_DONE, NULL, 0, g_qm_modem_config.arg);
        } else if ( event_id == MODEM_EVENT_NET_CONN) {
            QM_LOGD(LOG_TAG, "Modem Board Event: Network connected");
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_CONN, NULL, 0, g_qm_modem_config.arg);
        } else if ( event_id == MODEM_EVENT_NET_DISCONN) {
            QM_LOGD(LOG_TAG, "Modem Board Event: Network disconnected");
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_DISCONN, NULL, 0, g_qm_modem_config.arg);
        }else if ( event_id == MODEM_EVENT_CSQ) {
            QM_LOGD(LOG_TAG, "Modem Board Event: csq");
            esp_modem_dce_csq_ctx_t *csq = (esp_modem_dce_csq_ctx_t*)event_data;
            qm_modem_rssi_t modem_rssi;
            modem_rssi.rssi = csq->rssi * 2 - 113;
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_RSSI, &modem_rssi, sizeof(qm_modem_rssi_t), g_qm_modem_config.arg);
        }else if ( event_id == MODEM_EVENT_LBS) {
            QM_LOGD(LOG_TAG, "Modem Board Event: lbs");
            esp_modem_dce_lbs_ctx_t *lbs = (esp_modem_dce_lbs_ctx_t*)event_data;
            qm_modem_lbs_t modem_lbs;
            modem_lbs.longitude = lbs->longitude;
            modem_lbs.latitude = lbs->latitude;
            g_qm_modem_config.event_handler(QM_MODEM_EVENT_LBS, &modem_lbs, sizeof(qm_modem_lbs_t), g_qm_modem_config.arg);
        }
    }
}

qm_modem_t qm_modem_init(const qm_modem_config_t *config)
{
    if(config == NULL){
        return NULL;
    }
    if(config->event_handler == NULL){
        return NULL;
    }

    memcpy(&g_qm_modem_config, config, sizeof(qm_modem_config_t));

     /* Initialize default TCP/IP stack */
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /* Waiting for modem powerup */
    QM_LOGD(LOG_TAG, "4G Cat.1 modem init");

    /* Initialize modem board. Dial-up internet */
    modem_config.rx_buffer_size = config->rx_buffer_size;
    modem_config.tx_buffer_size = config->tx_buffer_size;
    /* Modem init flag, used to control init process */
    /* if Not enter ppp, modem will enter command mode after init */
    modem_config.flags |= MODEM_FLAGS_INIT_NOT_ENTER_PPP;
    /* if Not waiting for modem ready, just return after modem init */
    modem_config.flags |= MODEM_FLAGS_INIT_NOT_BLOCK;

    modem_config.handler = on_modem_event;
    modem_board_init(&modem_config);

    return (void*)config;
}

int qm_modem_start_ppp(qm_modem_t modem, uint32_t timeout)
{
    esp_err_t err = ESP_OK;
    err = modem_board_ppp_start(timeout);
    if(err != ESP_OK){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_modem_restart_ppp(qm_modem_t modem, uint32_t timeout)
{
    esp_err_t err = ESP_OK;
    err = modem_board_ppp_restart(timeout);
    if(err != ESP_OK){
        return -QM_ERROR;
    }
    return QM_EOK;
}

int qm_modem_stop_ppp(qm_modem_t modem, uint32_t timeout)
{
    esp_err_t err = ESP_OK;
    err = modem_board_ppp_stop(timeout);
    if(err != ESP_OK){
        return -QM_ERROR;
    }
    return QM_EOK;
}