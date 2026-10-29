#ifndef _QM_MODEM_H_
#define _QM_MODEM_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_config.h"

#if CONFIG_QM_MODEM_SUPPORT

typedef enum {
    QM_MODEM_EVENT_CONN,            /*!< Modem Net Connected */
    QM_MODEM_EVENT_DISCONN,         /*!< Modem Net Disconnected */
    QM_MODEM_EVENT_RSSI,            /*!< Modem rssi */
    QM_MODEM_EVENT_LBS,             /*!< Modem lbs */
    QM_MODEM_EVENT_SIMCARD_CONN,    /*!< Modem SIM CARD Connected */
    QM_MODEM_EVENT_SIMCARD_DISCONN, /*!< Modem SIM CARD Disconnected */
    QM_MODEM_EVENT_RESTART,         /*!< Modem DTE Reset to restart */
    QM_MODEM_EVENT_RESTART_DONE,    /*!< Modem DTE Restart done */
} qm_modem_event_t;

typedef enum {
    QM_MODEM_CHANNLE_USB,           
    QM_MODEM_CHANNLE_UART,
    QM_MODEM_CHANNLE_4G,           
} qm_modem_channel_t;

typedef struct {
    int rssi;                      
} qm_modem_rssi_t;

typedef struct {
    char longitude[32];             
    char latitude[32];            
} qm_modem_lbs_t;

typedef struct{
    uint32_t rx_buffer_size;
    uint32_t tx_buffer_size;
    qm_modem_channel_t channel;
    void *arg;
    void (*event_handler)(qm_modem_event_t event, void *data, int len, void *arg);
}qm_modem_config_t;

typedef void* qm_modem_t;

/**
 * @brief Create and initialize Modem object
 *
 * @param config configuration of QM Modem object
 * @return qm_modem_t*
 *      - Modem DTE object
 */
qm_modem_t qm_modem_init(const qm_modem_config_t *config);

/**
 * @brief deinitialize Modem object
 *
 * @param modem Modem object
 * @return int
 *      - QM_EOK: succeed
 *      - others: fail
 */
int qm_modem_deinit(qm_modem_t modem);

/**
 * @brief get lbs
 *
 * @param modem Modem object
 * @return
 *     - QM_EOK: succeed
 *     - others: fail
 */
int qm_modem_get_lbs(qm_modem_t modem);


/**
 * @brief get imsi of sim
 *
 * @param imsi buffer of imsi
 * @param imsi_len len of imsi
 * @return
 *     - QM_EOK: succeed
 *     - others: fail
 */
int qm_modem_get_imsi(qm_modem_t modem, char *imsi, int imsi_len);

/**
 * @brief Setup PPP Session
 *
 * @param modem Modem object
 * @param timeout wait time
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
 */
int qm_modem_start_ppp(qm_modem_t modem, uint32_t timeout);

/**
 * @brief reset PPP Session
 *
 * @param modem Modem object
 * @param timeout wait time
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
 */
int qm_modem_restart_ppp(qm_modem_t modem, uint32_t timeout);

/**
 * @brief Exit PPP Session
 *
 * @param dte Modem Object
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
 */
int qm_modem_stop_ppp(qm_modem_t modem, uint32_t timeout);

#endif

#ifdef __cplusplus
}
#endif

#endif
