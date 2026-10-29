#include "qm_ble_auth.h"
#include "qm_ble_common.h"
#include "qm_ble_bzopt.h"

#if QB_ENABLE_AUTH

#include "qm_utils_sha256.h"
#include "qm_ble_core.h"
#include "qm_ble_utils.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_sec.h"

extern qm_ble_core_t  g_qm_core;
extern qm_ble_transport_t  g_qm_transport;

qm_ble_auth_t g_am_auth = {0};

static void on_timeout(void *arg)
{
    qm_ble_core_handle_err(QM_ERROR_SRC_AUTH_PROC_TIMER_2, QB_ETIMEOUT);
}

static void update_aes_key(bool use_device_key)
{
    qm_sha256_context context;
    #define SHA256_BLOCK_SIZE 32
    uint8_t okm[SHA256_BLOCK_SIZE];

    qm_utils_sha256_init(&context);
    qm_utils_sha256_starts(&context);
    qm_utils_sha256_update(&context,  g_am_auth.ikm,  g_am_auth.ikm_len);
    qm_utils_sha256_finish(&context, okm);
    qm_utils_sha256_free(&context);
    memcpy( g_am_auth.okm, okm, MAX_OKM_LEN);
    
    qm_ble_transport_update_key( g_am_auth.okm);
}

qm_ble_ret_code_t qm_ble_auth_deinit(void)
{
    qm_ble_timer_free(& g_am_auth.timer);

    return QB_SUCCESS;
}

qm_ble_ret_code_t qm_ble_auth_init(qm_ble_init_t const *p_init, qm_ble_tx_func_t tx_func)
{
    int len;
    char tmp_ds[QB_DEV_DEVICE_SECRET_LEN];
    qm_ble_ret_code_t ret = QB_SUCCESS;

    len = sizeof(tmp_ds);
    memset(tmp_ds, 0, len);
    memset(& g_am_auth, 0, sizeof(qm_ble_auth_t));
     g_am_auth.state = QB_AUTH_STATE_IDLE;
     g_am_auth.tx_func = tx_func;

    if ((p_init->product_key.p_data != NULL) && (p_init->product_key.length > 0)) {
         g_am_auth.p_product_key =  g_qm_core.product_key;
         g_am_auth.product_key_len =  g_qm_core.product_key_len;
    }
    if ((p_init->device_id.p_data != NULL) && (p_init->device_id.length > 0)) {
         g_am_auth.p_device_name =  g_qm_core.device_id;
         g_am_auth.device_name_len =  g_qm_core.device_id_len;
    }
    if ((p_init->device_secret.p_data != NULL) && (p_init->device_secret.length > 0)) {
         g_am_auth.p_device_secret =  g_qm_core.device_secret;
         g_am_auth.device_secret_len =  g_qm_core.device_secret_len;
    }
    if ((p_init->product_secret.p_data != NULL) && (p_init->product_secret.length > 0)) {
         g_am_auth.p_product_secret =  g_qm_core.product_secret;
         g_am_auth.product_secret_len =  g_qm_core.product_secret_len;
    }
    
    ret = qm_ble_timer_new(& g_am_auth.timer, on_timeout, & g_am_auth, QB_AUTH_TIMEOUT, 0);
    return ret;
}

void qm_ble_auth_reset(void)
{
    g_am_auth.state = QB_AUTH_STATE_IDLE;
    qm_ble_timer_stop(& g_am_auth.timer);
}

void qm_ble_auth_rx_command(uint8_t cmd, uint8_t *p_data, uint16_t length)
{
    uint32_t err_code;
    uint8_t str_pid[QB_PID_STR_LEN+1] = {0};
    uint8_t m_pid[4] = {0};
    uint8_t encrypt_data[16] = {0};
#if (QB_VERSION == 3)
    char str_did[32+1] = {0};
#endif
    if (length == 0) {
        return;
    }

    QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                "auth rx state=%u cmd=0x%02x len=%u",
                (unsigned)g_am_auth.state, cmd, (unsigned)length);

    switch ( g_am_auth.state) {
        case QB_AUTH_STATE_SVC_ENABLED:
            if(cmd == QM_BLE_CMD_SET_RAND){

                if(length == RANDOM_SEQ_LEN){
                    memcpy( g_am_auth.ikm +  g_am_auth.ikm_len, p_data, length);
                     g_am_auth.ikm_len += length;
                     g_am_auth.ikm[ g_am_auth.ikm_len++] = ',';
                    QM_BLE_LOGD(QM_BLE_LOG_AUTH, "[BZ auth]: random received");
                }else{
                    QM_BLE_LOGE("[BZ auth]: rand len err");
                    return;
                }

                 g_am_auth.state = QB_AUTH_STATE_VERIFY;

                QM_BLE_SET_U32_BE(m_pid,  g_qm_core.product_id);
                qm_ble_hex2string(m_pid, sizeof(m_pid), str_pid);
    
                memcpy( g_am_auth.ikm +  g_am_auth.ikm_len, str_pid, QB_PID_STR_LEN);
                g_am_auth.ikm_len += QB_PID_STR_LEN;
                g_am_auth.ikm[ g_am_auth.ikm_len++] = ',';

                if(g_am_auth.auth_type == QB_AUTH_TYPE_PER_DEV){
                #if (QB_VERSION == 3)
                    qm_ble_hex2string(g_qm_core.device_id, g_qm_core.device_id_len * 2, (uint8_t *)str_did);
                    memcpy( g_am_auth.ikm +  g_am_auth.ikm_len,  str_did, strlen(str_did));
                    g_am_auth.ikm_len +=  (g_qm_core.device_id_len * 2);
                #else
                    memcpy( g_am_auth.ikm +  g_am_auth.ikm_len,  g_qm_core.device_id, g_qm_core.device_id_len);
                    g_am_auth.ikm_len +=  g_qm_core.device_id_len;
                #endif     
                    g_am_auth.ikm[ g_am_auth.ikm_len++] = ',';
                   
                    memcpy( g_am_auth.ikm +  g_am_auth.ikm_len,  g_qm_core.device_secret,  g_qm_core.device_secret_len);
                    g_am_auth.ikm_len +=  g_qm_core.device_secret_len;
                    g_am_auth.ikm[ g_am_auth.ikm_len] = '\0';

                }else{
                    memcpy( g_am_auth.ikm +  g_am_auth.ikm_len,  g_qm_core.product_secret,  g_qm_core.product_secret_len);
                    g_am_auth.ikm_len +=  g_qm_core.product_secret_len;
                    g_am_auth.ikm[ g_am_auth.ikm_len] = '\0';
                }

                QM_BLE_LOGD(QM_BLE_LOG_AUTH, "[BZ auth]: calculate session key");

                update_aes_key(true);


                qm_ble_aes128_cbc_encrypt( g_qm_transport.p_aes_ctx, p_data, length>>4, encrypt_data);

                err_code =  g_am_auth.tx_func(QM_BLE_CMD_AUTH, encrypt_data, length);
                QM_BLE_LOGD(QM_BLE_LOG_AUTH, "[BZ auth]: cipher send %s", err_code ? "failed": "succeeded");
                if (err_code != QB_SUCCESS) {
                    qm_ble_core_handle_err(QM_ERROR_AUTH_CIPHER_RPT, err_code);
                    return;
                }
                
            }else{
                 g_am_auth.state = QB_AUTH_STATE_FAILED;
            }
            
            break;


         case QB_AUTH_STATE_VERIFY:

            if (cmd == QM_BLE_CMD_AUTH) {
                uint8_t m_auth_result = 0;
                if (length == 1) {
                    m_auth_result = p_data[0];
                    QM_BLE_LOGD(QM_BLE_LOG_AUTH, "[BZ auth]: authentication result %s", m_auth_result ? "failed" : "succeeded");
                    if (m_auth_result == 0) {
                          g_am_auth.state = QB_AUTH_STATE_DONE;
                    }else{
                         g_am_auth.state = QB_AUTH_STATE_FAILED;
                    }
                }
            }
   
            break;

        default:
            break;
    }

    if ( g_am_auth.state == QB_AUTH_STATE_DONE) {
        qm_ble_core_event_notify(QB_EVENT_AUTHENTICATED, NULL, 0);
        qm_ble_timer_stop(& g_am_auth.timer);
    } else if ( g_am_auth.state == QB_AUTH_STATE_FAILED) {
        qm_ble_timer_stop(& g_am_auth.timer);
        qm_ble_disconnect(QMS_BT_REASON_REMOTE_USER_TERM_CONN);
    }
}
void qm_ble_auth_connected(void)
{
    uint32_t err_code;

    err_code = qm_ble_timer_start(& g_am_auth.timer);
    if (err_code != QB_SUCCESS) {
        qm_ble_core_handle_err(QM_ERROR_SRC_AUTH_PROC_TIMER_0, err_code);
        return;
    }

     g_am_auth.state = QB_AUTH_STATE_CONNECTED;
}

void qm_ble_auth_service_enabled(void)
{
    g_am_auth.state = QB_AUTH_STATE_SVC_ENABLED;
    memset( g_am_auth.ikm, 0, sizeof( g_am_auth.ikm));
    g_am_auth.ikm_len = 0;
    QM_BLE_LOGD(QM_BLE_LOG_AUTH, "[BZ auth]: start");
}

void qm_ble_auth_tx_done(void)
{
    
}

bool qm_ble_auth_is_authdone(void)
{
    return (bool)( g_am_auth.state == QB_AUTH_STATE_DONE);
}


#endif
