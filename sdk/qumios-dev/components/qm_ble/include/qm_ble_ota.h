#ifndef _QM_BLE_OTA_H_
#define _QM_BLE_OTA_H_

#include "qm_ble_bzopt.h"
#include "qm_ble_export.h"
#include "qm_ble_hal_os.h"


#define QM_BLE_OTA_REBOOT_TIME          10
#define QM_BLE_OTA_TOTAL_PACKAGES      0x10  // the total package numbers in a loop
#define QM_BLE_OTA_BUF_SIZE           (256)
#define QM_BLE_OTA_RETRY_TIMEOUT      (3000) 
#define QM_BLE_OTA_MAX_RETRY_COUNT    (2)   // disconnect if retry times more than QM_BLE_OTA_MAX_RETRY_COUNT

// ota feature
#define QM_BLE_OTA_ENABLE_SET(x)           ((x) << 0)
#define QM_BLE_OTA_RESUME_ENABLE_SET(x)    ((x) << 1)


typedef struct{

    qm_ble_ver_t ble_ver;
    qm_ble_ver_t mcu_ver;
    qm_ble_ota_cb ota_cb;
    
}qm_ble_ota_conf_t;

typedef struct{

    uint8_t result;
    uint8_t package_nums;
    uint8_t reboot_time;
    uint32_t last_file_size;

}__attribute__((packed)) qm_ble_ota_req_rsp_t;

enum{
    QM_BLE_OTA_VER_BOTH = 0,
    QM_BLE_OTA_VER_BLE = 1,
    QM_BLE_OTA_VER_MCU = 2,
};


typedef struct{

    uint8_t seq;
    uint32_t file_size;

}__attribute__((packed)) qm_ble_ota_data_rsp_t;


typedef enum
{
    QM_BLE_OTA_STATE_OFF,            
    QM_BLE_OTA_STATE_IDLE,
    QM_BLE_OTA_STATE_DATA,      
    QM_BLE_OTA_STATE_WRITE_SETTINGS,
    QM_BLE_OTA_STATE_FW_VERIFY,   
    
} qm_ble_ota_state_t;


typedef struct {
    qm_ble_ota_type_t ota_type;
    qm_ble_hash_t hash;
    uint8_t hash_len;
    uint8_t hash_value[QM_BLE_HASH_MAX_SIZE];
    uint32_t file_size;
    qm_ble_ver_t ver;
} qm_ble_ota_file_t;

// ota info saved in flash if support resuming
typedef struct{
    uint32_t          last_file_size;  // the file size already write in flash
    uint32_t          last_address;    // the address file saved
    qm_ble_ota_file_t download_file_info;
    uint32_t          record_crc32;
} qm_ble_ota_record_t;


typedef struct{

    qm_ble_ota_file_t ota_file;

    qm_ble_ota_reason_t ota_reason;
     
    qm_ble_ver_t mcu_ver;
    uint8_t reboot_time;
    uint8_t msg_id;
    uint8_t loop_msg_id;
    
    qm_ble_ver_t ble_ver;
    uint8_t ota_data_buf[QM_BLE_OTA_BUF_SIZE];  // storage ota data and write to the flash at once
    uint32_t ota_buf_offset;                    // the data size in the buffer
    uint32_t ota_download_file_size;            // the data size download from the server
    uint8_t  ota_download_percent;              // the percent of file had download
    
    qm_ble_ota_state_t status;
    uint8_t next_seq;

    uint8_t ota_timeout_cnt;         // count the number of no data times
    qm_ble_timer_t ota_timer;

    qm_ble_ota_cb ota_cb;          // user callback
}qm_ble_ota_context_t;


uint32_t qm_ble_ota_init(qm_ble_ota_conf_t *ota_conf);

qm_ble_ota_context_t *qm_ble_ota_context_get(void);

uint32_t qm_ble_ota_dispatcher(uint8_t msg_id, uint8_t cmd, uint8_t *data, uint16_t len);

void qm_ble_ota_relate_event(uint8_t event);

#endif


