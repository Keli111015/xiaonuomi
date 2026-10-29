#ifndef QB_AUTH_H
#define QB_AUTH_H

#include "qm_ble_common.h"
#include "qm_ble_hal_os.h"

#ifdef __cplusplus
extern "C"
{
#endif

#if QB_ENABLE_AUTH

#define RANDOM_SEQ_LEN       16
#define QB_PID_STR_LEN        8
#define QB_DID_STR_LEN        12
#define MAX_OKM_LEN          16
#define MAX_IKM_LEN          (RANDOM_SEQ_LEN + QB_DEV_PRODUCT_ID_LEN + QB_DID_STR_LEN + QB_DEV_DEVICE_SECRET_LEN +3 + 1)

enum {
    QB_AUTH_STATE_IDLE,                                // Auth idle state
    QB_AUTH_STATE_CONNECTED,                           // BLE link connected
    QB_AUTH_STATE_SVC_ENABLED,                         // QM service enabled
    QB_AUTH_STATE_VERIFY,                              // Auth verify from peer
    QB_AUTH_STATE_DONE,                                // Auth rx ok from peer
    QB_AUTH_STATE_FAILED,                              // Auth failed
};

enum {
    QB_AUTH_TYPE_NONE,
    QB_AUTH_TYPE_PER_PK,
    QB_AUTH_TYPE_PER_DEV,
};

typedef struct auth_s {
    uint8_t state;                                     // Auth state
    qm_ble_tx_func_t tx_func;                                 // Auth data send, use indication
    qm_ble_timer_t timer;                                  // Auth timeout timer, start from send random
    uint8_t ikm[MAX_IKM_LEN];                          // Auth sign calc input buffer
    uint16_t ikm_len;                                  // Auth sign calc input buffer length
    uint8_t okm[MAX_OKM_LEN];                          // Auth key output
    uint8_t *p_product_key;
    uint8_t product_key_len;
    uint8_t *p_product_secret;
    uint8_t product_secret_len;
    uint8_t *p_device_name;
    uint8_t device_name_len;
    uint8_t *p_device_secret;
    uint8_t device_secret_len;
    uint8_t auth_type;
} qm_ble_auth_t;

qm_ble_ret_code_t qm_ble_auth_deinit(void);
qm_ble_ret_code_t qm_ble_auth_init(qm_ble_init_t const *p_init, qm_ble_tx_func_t tx_func);
void qm_ble_auth_reset(void);
void qm_ble_auth_rx_command(uint8_t cmd, uint8_t *p_data, uint16_t length);
void qm_ble_auth_connected(void);
void qm_ble_auth_service_enabled(void);
void qm_ble_auth_tx_done(void);
bool qm_ble_auth_is_authdone(void);

int auth_calc_adv_sign(uint32_t seq, uint8_t *sign);

#ifdef __cplusplus
}
#endif


#endif //QB_ENABLE_AUTH


#endif // QB_AUTH_H

