#include "qm_ble_core.h"
#include "qm_ble_transport.h"
#include "qm_ble_auth.h"
#include "qm_ble_common.h"
#include "qm_ble_service.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_bzopt.h"

#include "qm_ble_utils.h"
#if QB_ENABLE_SECURE_ADV
#include "qm_utils_sha256.h"
#endif

qm_ble_core_t  g_qm_core;

#if QB_ENABLE_AUTH
extern qm_ble_auth_t  g_am_auth;
#endif

#if QB_ENABLE_SECURE_ADV
#define QM_SEQ_KV_KEY      "qms_adv_seq"
#define QM_SEQ_UPDATE_FREQ (1 * 60 * 60) /* in second uint */
static uint32_t g_seq = 0;
static qm_ble_timer_t g_secadv_timer;
#endif

void qm_ble_core_pid_set(uint32_t product_id)
{
    g_qm_core.product_id = product_id;
}

void qm_ble_core_device_id_set(uint8_t *device_id, uint8_t device_id_len)
{
    if(device_id == NULL || device_id_len >= QB_DEV_MAX_DEVICE_NAME_LEN){
        return;
    }
    
    g_qm_core.device_id_len = device_id_len;
    memcpy(g_qm_core.device_id, device_id, device_id_len);
}


void qm_ble_core_event_notify(uint8_t event_type, uint8_t *data, uint16_t length)
{
    qm_ble_event_info_t event;

    event.type = event_type;
    event.rx_data.p_data = data;
    event.rx_data.length = length;
     g_qm_core.event_handler(&event);
}

#if QB_ENABLE_AUTH
static uint32_t tx_func_indicate(uint8_t cmd, uint8_t *p_data, uint16_t length)
{
    uint8_t msg_id = 0;
    return qm_ble_transport_tx(QB_TX_INDICATION, &msg_id, cmd, p_data, length);
}
#endif

static uint32_t qms_init(qm_ble_init_t const *p_init)
{
    qm_ble_service_init_t init_qms;

    memset(&init_qms, 0, sizeof(qm_ble_service_init_t));
    init_qms.mtu = p_init->max_mtu;
    return qm_ble_service_init(&init_qms);
}

#if QB_ENABLE_SECURE_ADV
static void update_seq(void *arg1, void *arg2)
{
    qm_kv_set(QM_SEQ_KV_KEY, &g_seq, sizeof(g_seq), 1);
    qm_ble_timer_start(&g_secadv_timer);
}

static void init_seq_number(uint32_t *seq)
{
    int len = sizeof(uint32_t);

    if (!seq)
        return;

    if (qm_kv_get(QM_SEQ_KV_KEY, seq, &len) != 0) {
        *seq = 0;
        len  = sizeof(uint32_t);
        qm_kv_set(QM_SEQ_KV_KEY, seq, len, 1);
    }

    qm_ble_timer_new(&g_secadv_timer, update_seq, NULL, QM_SEQ_UPDATE_FREQ, 0);
    qm_ble_timer_start(&g_secadv_timer);
}

void set_adv_sequence(uint32_t seq)
{
    g_seq = seq;
    qm_kv_set(QM_SEQ_KV_KEY, &g_seq, sizeof(g_seq), 1);
}
#endif

qm_ble_ret_code_t qm_ble_core_init(qm_ble_init_t const *p_init)
{
    // qm core base infomation init
    memset(& g_qm_core, 0, sizeof(qm_ble_core_t));
     g_qm_core.event_handler = p_init->event_handler;
    memcpy( g_qm_core.adv_mac, p_init->adv_mac, QB_BT_MAC_LEN);
     g_qm_core.product_id = p_init->product_id;
    // core device info init
    if((p_init->product_key.p_data != NULL) && (p_init->product_key.length > 0)) {
         g_qm_core.product_key_len = p_init->product_key.length;
        memcpy( g_qm_core.product_key, p_init->product_key.p_data,  g_qm_core.product_key_len);
    }
    if((p_init->product_secret.p_data != NULL) && (p_init->product_secret.length > 0)) {
         g_qm_core.product_secret_len = p_init->product_secret.length;
        memcpy( g_qm_core.product_secret, p_init->product_secret.p_data,  g_qm_core.product_secret_len);
    }
    if((p_init->device_id.p_data != NULL) && (p_init->device_id.length > 0)) {
         g_qm_core.device_id_len = p_init->device_id.length;
        memcpy( g_qm_core.device_id, p_init->device_id.p_data,  g_qm_core.device_id_len);
    }
    if((p_init->device_secret.p_data != NULL) && (p_init->device_secret.length > 0)) {
         g_qm_core.device_secret_len = p_init->device_secret.length;
        memcpy( g_qm_core.device_secret, p_init->device_secret.p_data,  g_qm_core.device_secret_len);
    }

#if QB_ENABLE_SECURE_ADV
    init_seq_number(&g_seq);
#endif

    qms_init(p_init);
    qm_ble_transport_init(p_init);

#if QB_ENABLE_AUTH
    qm_ble_auth_init(p_init, tx_func_indicate);
#endif

    return QB_SUCCESS;
}


void qm_ble_core_reset(void)
{
#if QB_ENABLE_AUTH
    qm_ble_auth_reset();
#endif
    qm_ble_transport_reset();
     g_qm_core.admin_checkin = 0;
     g_qm_core.guest_checkin = 0;
}

void qm_ble_core_handle_err(uint8_t src, uint8_t code)
{
    QM_BLE_LOGE("err at 0x%04x, code 0x%04x", src, code);
    switch (src & QB_ERR_MASK) {
        case QB_TRANS_ERR:
            if (code != QB_EINTERNAL) {
                if (src == QM_ERROR_SRC_TRANSPORT_FW_DATA_DISC) {
                    qm_ble_core_event_notify(QB_EVENT_DISCONNECTED, NULL, 0);
                }
                if(src == QM_ERROR_SRC_TRANSPORT_TX_TIMER){
                    qm_ble_core_event_notify(QB_EVENT_TX_DONE_TIMEOUT, NULL, 0);
                }
            }
            break;
#if QB_ENABLE_AUTH
        case QB_AUTH_ERR:
            QM_BLE_LOGE("QM_BLE_AUTH_ERR");
            qm_ble_auth_reset();
            if (code == QB_ETIMEOUT) {
                qm_ble_disconnect(QMS_BT_REASON_REMOTE_USER_TERM_CONN);
            }
            break;
#endif
        case QB_EXTCMD_ERR:
            QM_BLE_LOGE("QB_EXTCMD_ERR");
            break;
#if QB_ENABLE_OTA

        case QB_OTA_ERR:
            QM_BLE_LOGE("QB_OTA_ERR");

            break;
#endif

        default:
            QM_BLE_LOGE("unknow bz err\r\n");
            break;
    }
}


char *ble_local_name_get(void)
{
    return  (char *)g_qm_core.local_name;
}

void ble_local_name_set(char *local_name)
{
    memcpy( g_qm_core.local_name, local_name, strlen(local_name));
}

void qm_ble_core_create_bz_adv_data(uint8_t sub_type, uint8_t sec_type, uint8_t bind_state)
{
    uint16_t idx;
    // uint8_t version = 0;
    uint8_t fmsk = 0;
    uint8_t i = 0;

    QM_BLE_SET_U16_LE( g_qm_core.adv_data, QB_QM_COMPANY_ID);
    idx = sizeof(uint16_t);

	g_qm_core.adv_data[idx++] = (QB_VERSION<<QB_SDK_VER_Pos) | (sub_type<<QB_SUB_TYPE_Pos);

    // FMSK byte
    fmsk = QB_BLUETOOTH_VER << QB_FMSK_BLUETOOTH_VER_Pos;
#if QB_ENABLE_OTA
    fmsk |= 1 << QB_FMSK_OTA_Pos;
#endif
#if QB_ENABLE_AUTH
    if(sec_type != QB_AUTH_TYPE_NONE){
        fmsk |= 1 << QB_FMSK_SECURITY_Pos;
    }
    
    if(sec_type == QB_AUTH_TYPE_PER_DEV) {
        QM_BLE_LOGD(QM_BLE_LOG_CONTROL, "qm ble adv per device");
        fmsk |= 1 << QB_FMSK_SECRET_TYPE_Pos;
         g_am_auth.auth_type = QB_AUTH_TYPE_PER_DEV;
    } else if (sec_type == QB_AUTH_TYPE_PER_PK) {
        QM_BLE_LOGD(QM_BLE_LOG_CONTROL, "qm ble adv per product");
        fmsk &= ~(1 << QB_FMSK_SECRET_TYPE_Pos);
         g_am_auth.auth_type = QB_AUTH_TYPE_PER_PK;
    } else {
        QM_BLE_LOGE("qm ble adv none sec");
         g_am_auth.auth_type = QB_AUTH_TYPE_NONE;
    }
#endif
#if QB_ENABLE_SECURE_ADV
    fmsk |= 1 << QB_FMSK_SEC_ADV_Pos;
#endif
    // if(bind_state && (version >= 6)){
    if(bind_state){
        QM_BLE_LOGD(QM_BLE_LOG_CONTROL, "qm ble binded");
        fmsk |= 1 << QB_FMSK_BIND_STATE_Pos;
    } else{
        QM_BLE_LOGD(QM_BLE_LOG_CONTROL, "qm ble unbind");
        fmsk &= ~(1 << QB_FMSK_BIND_STATE_Pos);
    }   

     g_qm_core.adv_data[idx++] = fmsk;

    QM_BLE_SET_U32_LE( g_qm_core.adv_data + idx,  g_qm_core.product_id);
    idx += sizeof(uint32_t);
    
    for(i = 0; i < 6; i++){
        g_qm_core.adv_data[idx+i] = g_qm_core.device_id[5-i];
    }  
    
    idx += 6;
    g_qm_core.adv_data_len = idx;
}


qm_ble_ret_code_t qm_ble_core_get_bz_adv_data(uint8_t *p_data, uint16_t *length)
{
#if QB_ENABLE_SECURE_ADV
    if (*length < ( g_qm_core.adv_data_len + 4 + 4)) {
#else
    if (*length <  g_qm_core.adv_data_len) {
#endif
        return QB_ENOMEM;
    }

#if QB_ENABLE_SECURE_ADV
    uint8_t  sign[4];
    uint32_t seq;

    seq = (++g_seq);
#if QB_ENABLE_AUTH
    auth_calc_adv_sign(seq, sign);
#endif
    memcpy(p_data,  g_qm_core.adv_data,  g_qm_core.adv_data_len);
    memcpy(p_data +  g_qm_core.adv_data_len, sign, 4);
    memcpy(p_data +  g_qm_core.adv_data_len + 4, &seq, 4);
    *length =  g_qm_core.adv_data_len + 4 + 4;
#else
    memcpy(p_data,  g_qm_core.adv_data,  g_qm_core.adv_data_len);
    *length =  g_qm_core.adv_data_len;
#endif

    return QB_SUCCESS;
}

