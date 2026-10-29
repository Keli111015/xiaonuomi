#ifndef QB_CORE_H
#define QB_CORE_H

#include "qm.h"
#include "qm_ble_common.h"
#include "qm_ble_service.h"
#include "qm_ble_transport.h"
#include "qm_ble_auth.h"
#include "qm_ble_bzopt.h"
#include "qm_ble_log.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Definition of qm advertising data
#define QB_MAX_ADV_DATA_LEN             (16)      // fixed manufacturer advertising data length
#define QB_MAX_LOCAL_NAME_LEN           (24)      // fixed scan response data length
#define QB_DEFAULT_LOCAL_NAME           "xjiang"  

#define QB_MANUFACT_SPEC_TYPE           (0xFF)    // fixed manufacturer adv data type

#ifndef QB_QM_COMPANY_ID
#define QB_QM_COMPANY_ID               (0xF018)     /* company id */
#endif

// Vendor ID octet
#define QB_SDK_VER_Pos                  (0)       // qm ble Version bit0-3
#define QB_SUB_TYPE_Pos                 (4)       // qm ble adv subtype, bit4-7, subtype 0-15
// qm ble subtype definition
#define QB_SUB_TYPE_BASIC               (0)       // qm ble basic mesh adv
#define QB_SUB_TYPE_SEC_BEACON          (1)       // qm ble security adv
#define QB_SUB_TYPE_VOICE               (2)       // qm ble voice adv
#define QB_SUB_TYPE_BLE_GATT            (3)       // qm ble gatt
// Function mack bit def
#define QB_FMSK_BLUETOOTH_VER_Pos       (0)       // FMSK bt version, bit 0-1
#define QB_FMSK_OTA_Pos                 (2)       // FMSK OTA flag, bit2, 0-qm ble OTA unsupport, 1-qm ble OTA supported
#define QB_FMSK_SECURITY_Pos            (3)       // FMSK sec flag, bit3, 0-no security, 1-security supported
#define QB_FMSK_SECRET_TYPE_Pos         (4)       // FMSK sec type, bit4, mandatory if bit3==1, 0-perproduct, 1-perdevice
#define QB_FMSK_SEC_ADV_Pos             (5)       // FMSK sec adv type, bit5, 0-not secure adv, 1-secure adv
#define QB_FMSK_BIND_STATE_Pos          (6)       // FMSK bind state, bit6, 0-not bind, 1-binded
// qm ble security type def
#define QB_SEC_TYPE_PRODUCT             (0)       // security type perproduct
#define QB_SEC_TYPE_DEVICE              (1)       // security type perdevice
// qm ble bind state def
#define QB_BIND_STATE_UNBIND            (0)       // unbind state
#define QB_BIND_STATE_BIND              (1)       // bind state

typedef struct {
    qm_ble_event_handler_t event_handler;
    uint8_t adv_data[QB_MAX_ADV_DATA_LEN];
    uint8_t local_name[QB_MAX_LOCAL_NAME_LEN+1];
    uint16_t adv_data_len;
    uint8_t  adv_mac[QB_BT_MAC_LEN];        // mac address filled in qm ble adv data(maybe bt addr or wifi mac)
    uint32_t product_id;
    uint8_t product_key[QB_DEV_PRODUCT_KEY_LEN];
    uint8_t product_key_len;
    uint8_t product_secret[QB_DEV_PRODUCT_SECRET_LEN];
    uint8_t product_secret_len;
    uint8_t device_id[QB_DEV_MAX_DEVICE_NAME_LEN];
    uint8_t device_id_len;
    uint8_t device_secret[QB_DEV_DEVICE_SECRET_LEN];
    uint8_t device_secret_len;
    uint8_t admin_checkin;                  // 0-not checkin, 1-checkin
    uint8_t guest_checkin;                  // 0-not checkin, 1-checkin
} qm_ble_core_t;

qm_ble_ret_code_t qm_ble_core_init(qm_ble_init_t const *p_init);
void qm_ble_core_reset(void);
void qm_ble_core_create_bz_adv_data(uint8_t sub_type, uint8_t sec_type, uint8_t bind_state);
qm_ble_ret_code_t qm_ble_core_get_bz_adv_data(uint8_t *p_data, uint16_t *length);
void qm_ble_core_event_notify(uint8_t evt_type, uint8_t *data, uint16_t length);
void qm_ble_core_handle_err(uint8_t src, uint8_t code);
char *ble_local_name_get(void);
void ble_local_name_set(char *local_name);

void qm_ble_core_pid_set(uint32_t product_id);
void qm_ble_core_device_id_set(uint8_t *device_id, uint8_t device_id_len);

#ifdef __cplusplus
}
#endif

#endif // QB_CORE_H
