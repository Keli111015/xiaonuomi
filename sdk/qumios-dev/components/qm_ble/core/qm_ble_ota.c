#include "qm_ble_bzopt.h"
#if QB_ENABLE_OTA
#include "qm_ble_hal_ota.h"
#include "qm_ble_ota.h"
#include "qm_utils_crc32.h"
#include "qm_ble_common.h"
#include "qm_ble_transport.h"
#include "qm_ble_utils.h"
#include "qm_utils_md5.h"
#include "qm_ble_async.h"
#include "qm_ble_core.h"

static qm_ble_ota_context_t g_qm_ble_ota;

static void qm_ble_ota_timer_callback(void *arg);
static uint32_t qm_ble_ota_timer_start(void);
static uint32_t qm_ble_ota_data_rsp(uint8_t msg_id, uint8_t com_code);

void qm_ble_ota_reset(void);


qm_ble_ota_context_t *qm_ble_ota_context_get(void)
{
    return &g_qm_ble_ota;
}

static void qm_ble_ota_status_set(qm_ble_ota_state_t status)
{
    g_qm_ble_ota.status = status;
}

static qm_ble_ota_state_t qm_ble_ota_status_get(void)
{
    return g_qm_ble_ota.status;
}

static uint8_t qm_ble_ota_next_seq_get(void)
{
    return g_qm_ble_ota.next_seq;
}

static void qm_ble_ota_next_seq_inc(void)
{
    g_qm_ble_ota.next_seq++;
}

static void qm_ble_ota_timeout_cnt_inc(void)
{
    g_qm_ble_ota.ota_timeout_cnt++;
}

static uint8_t qm_ble_ota_timeout_cnt_get(void)
{
    return g_qm_ble_ota.ota_timeout_cnt;
}

static void qm_ble_ota_timeout_cnt_clear(void)
{
    g_qm_ble_ota.ota_timeout_cnt = 0;
}

static uint32_t qm_ble_ota_download_size_get(void)
{
    return g_qm_ble_ota.ota_download_file_size;
}

static void qm_ble_ota_download_size_inc(uint32_t size)
{
    g_qm_ble_ota.ota_download_file_size += size;
}


static uint32_t qm_ble_ota_post_ack(uint8_t msg_id, uint8_t cmd, uint8_t err_code, uint8_t *buffer, uint16_t len)
{
    return qm_ble_generic_post(1, QB_TX_NOTIFICATION, &msg_id, cmd, &err_code, buffer, len);
}

static void qm_ble_ota_timer_callback(void *arg)
{
    uint8_t timeout_cnt = 0;
    qm_ble_ota_state_t status;

    qm_ble_ota_context_t *ota_context = NULL;

    ota_context = qm_ble_ota_context_get();
    if(!ota_context){
        return;
    }
    
    status = qm_ble_ota_status_get();

    if(status == QM_BLE_OTA_STATE_DATA){
        qm_ble_ota_status_set(QM_BLE_OTA_STATE_IDLE);
        return;
    }

    qm_ble_ota_data_rsp(ota_context->msg_id, QM_BLE_COM_NO_ERR);

    qm_ble_ota_timeout_cnt_inc();

    timeout_cnt = qm_ble_ota_timeout_cnt_get();

    QM_BLE_LOGE("reply in the timer, count: %d", timeout_cnt);

    if (timeout_cnt >= QM_BLE_OTA_MAX_RETRY_COUNT) {
        
        // inform the user ota failed because timeout
        qm_ble_core_handle_err(QM_ERROR_SRC_OTA_TIMER_0, QB_ETIMEOUT);

        qm_ble_ota_reset();
    }


}

static void qm_ble_ota_ver_padding(qm_ble_ver_t *ver, uint8_t *buffer)
{
    buffer[0] = ver->main_ver;
    buffer[1] = ver->sub_ver;
    buffer[2] = ver->fix_ver;
}

static uint32_t qm_ble_ota_ver_handle(uint8_t msg_id, uint8_t *data, uint16_t len)
{
    uint8_t query_type = 0;
    uint8_t ver_buf[6] = {0};
    uint8_t ver_offset = 0;
    uint8_t com_code = 0;
    uint32_t err_code = 0;
    qm_ble_ota_context_t *ota_context = NULL;

    if(!data){
        return QB_EINVALIDPARAM;
    }
    
    if(len > sizeof(uint8_t) || len == 0){
        return QB_EDATASIZE;
    }

    ota_context = qm_ble_ota_context_get();
    if(!ota_context){
        return QB_EINVALIDPARAM;
    }
    
    query_type = *data;
    if(query_type == QM_BLE_OTA_VER_BOTH){
        qm_ble_ota_ver_padding(&ota_context->ble_ver, ver_buf);
        qm_ble_ota_ver_padding(&ota_context->mcu_ver, ver_buf+3);
        ver_offset += 6;
    
    }else if(query_type == QM_BLE_OTA_VER_BLE){
        qm_ble_ota_ver_padding(&ota_context->ble_ver, ver_buf);
        ver_offset += 3;

    }else if(query_type == QM_BLE_OTA_VER_MCU){
        qm_ble_ota_ver_padding(&ota_context->mcu_ver, ver_buf);
        ver_offset += 3;
        
    }else{
        err_code = QB_EINVALIDDATA;
        com_code = QM_BLE_COM_ERR_INVALID_PARA;
    }
    if(com_code != 0){
        err_code = qm_ble_ota_post_ack(msg_id, QM_BLE_CMD_OTA_QUERY_VER, com_code, NULL, 0);
    }else{
        err_code = qm_ble_ota_post_ack(msg_id, QM_BLE_CMD_OTA_QUERY_VER, com_code, ver_buf, ver_offset);
    }
    
    return err_code;
}

static uint8_t is_valid_ota_type(uint8_t type)
{
    if(type == QM_BLE_OTA_TYPE_BLE ||
       type == QM_BLE_OTA_TYPE_MCU){
        return 1;
    }else{
        return 0;
    }
}


static uint8_t is_valid_hash_type(uint8_t type)
{
    if(type == QM_BLE_HASH_MD5  ||
       type == QM_BLE_HASH_SHA1 ||
       type == QM_BLE_HASH_CRC32){
        return 1;
    }else{
        return 0;
    }
}


static uint8_t hash_len_get(uint8_t type)
{
    if(type == QM_BLE_HASH_MD5){
        return QM_BLE_HASH_MD5_SIZE;
    }else if(type == QM_BLE_HASH_SHA1){
        return QM_BLE_HASH_SHA1_SIZE;
    }else if(type == QM_BLE_HASH_CRC32){
        return QM_BLE_HASH_CRC32_SIZE;
    
}else{
        return 0;
    }
}

static uint8_t is_valid_ota_reason(uint8_t reason)
{
    if(reason == QM_BLE_DEV_NORMAL  ||
       reason == QM_BLE_DEV_LOWBATTERY ||
       reason == QM_BLE_DEV_BUSY ||
       reason == QM_BLE_DEV_VER_ERR ){
        return 1;
    }else{
        return 0;
    }
}

static uint32_t qm_ble_ota_ver_check(qm_ble_ota_type_t ota_type, qm_ble_ver_t *ver, qm_ble_ota_reason_t *reason)
{
    qm_ble_ota_context_t *ota_context = NULL;
    
    ota_context = qm_ble_ota_context_get();
    if(!ota_context){
        return QB_EINVALIDPARAM;
    }

    if(ota_type == QM_BLE_OTA_TYPE_BLE){
        if(memcmp(&ota_context->ble_ver, ver, sizeof(qm_ble_ver_t))){
            *reason = QM_BLE_DEV_NORMAL;
        }else{
            *reason = QM_BLE_DEV_VER_ERR;
        }

    }else if(ota_type == QM_BLE_OTA_TYPE_MCU){
        if(memcmp(&ota_context->mcu_ver, ver, sizeof(qm_ble_ver_t))){
            *reason = QM_BLE_DEV_NORMAL;
        }else{
            *reason = QM_BLE_DEV_VER_ERR;
        }
    }
    
    return QB_SUCCESS;
}

static uint32_t qm_ble_ota_req_parse(uint8_t *data, uint16_t len, qm_ble_ota_file_t *ota_file)
{
    uint32_t err_code = 0;
    uint16_t offset = 0;
    uint8_t hash_len = 0;
    
    if(!data || len == 0){
        return QB_EINVALIDPARAM;
    }
    
    if(is_valid_ota_type(*(data+offset))){
        ota_file->ota_type = *(data+offset);
    }else{
        err_code = QB_EINVALIDPARAM;
        goto exit;
    }

    offset += 1;
    
    if(len - offset < 3){
        err_code = QB_EINVALIDPARAM;
        goto exit;
    }

    ota_file->ver.main_ver = *(data+offset+0);
    ota_file->ver.sub_ver = *(data+offset+1);
    ota_file->ver.fix_ver = *(data+offset+2);
    
    offset += 3;

    if(len - offset < 1){
        err_code = QB_EINVALIDPARAM;
        goto exit;
    }

    if(is_valid_hash_type(*(data+offset))){
        ota_file->hash = *(data+offset);
    }else{
        err_code = QB_EINVALIDPARAM;
        goto exit;
    }

    offset += 1;
    hash_len = hash_len_get(ota_file->hash);
    if(len - offset < hash_len){
        err_code = QB_EINVALIDPARAM;
        goto exit;
    }

    memcpy(ota_file->hash_value, data+offset, hash_len);
    ota_file->hash_len = hash_len;

    offset += hash_len;


    if(len - offset < sizeof(uint32_t)){
        err_code = QB_EINVALIDPARAM;
        goto exit;
    }


    memcpy(&ota_file->file_size, data+offset, sizeof(uint32_t));
    ota_file->file_size = QM_BLE_SWAP32(ota_file->file_size);

    offset += sizeof(uint32_t);
    if(offset != len){
        err_code = QB_EINVALIDPARAM;
        goto exit;
    }

    QM_BLE_LOGD(QM_BLE_LOG_OTA, "ota type: %d" ,ota_file->ota_type);
    QM_BLE_LOGD(QM_BLE_LOG_OTA, "file ver: %d.%d.%d" ,ota_file->ver.main_ver, ota_file->ver.sub_ver, ota_file->ver.fix_ver);
    QM_BLE_LOGD(QM_BLE_LOG_OTA, "file size: %d", ota_file->file_size);
    QM_BLE_LOGD(QM_BLE_LOG_OTA, "verify type:%d", ota_file->hash);
    
exit:

    return err_code;

}


static uint32_t qm_ble_ota_req_rsp(uint8_t msg_id, uint8_t com_code, qm_ble_ota_reason_t reason)
{
    uint8_t is_upgrade = 0;
    qm_ble_ota_req_rsp_t ota_req_rsp = {0};
    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();
    if(!ota_context){
        return QB_EINVALIDPARAM;
    }    

    if(com_code == QM_BLE_COM_NO_ERR){

        if(reason == QM_BLE_DEV_NORMAL){
            is_upgrade = 1;
            com_code = QM_BLE_COM_NO_ERR;    
        }else if(reason == QM_BLE_DEV_LOWBATTERY){
            is_upgrade = 0;
            com_code = QM_BLE_COM_ERR_LOW_BATTERY; 
        }else if(reason == QM_BLE_DEV_BUSY){
            is_upgrade = 0;
            com_code = QM_BLE_COM_ERR_BUSY; 
        }else if(reason == QM_BLE_DEV_VER_ERR){
            is_upgrade = 0;
            com_code = QM_BLE_COM_ERR_VER; 
        }else{
            is_upgrade = 0;
            com_code = QM_BLE_COM_ERR_SYSTEM; 
        }

    }

    ota_req_rsp.result = QM_BLE_OTA_ENABLE_SET(is_upgrade);
    ota_req_rsp.package_nums = QM_BLE_OTA_TOTAL_PACKAGES;
    ota_req_rsp.reboot_time = QM_BLE_OTA_REBOOT_TIME;
    ota_req_rsp.last_file_size = 0;

    return qm_ble_ota_post_ack(msg_id, QM_BLE_CMD_OTA_REQUEST, com_code, (uint8_t*)&ota_req_rsp, sizeof(qm_ble_ota_req_rsp_t));
}


static uint32_t qm_ble_ota_req_handle(uint8_t msg_id, uint8_t *data, uint16_t len)
{
    uint32_t err_code = 0;
    uint8_t com_code = 0;
    
#if QB_ENABLE_MCU_OTA 
    qm_ble_ota_notify_t mcu_ota_notify = {0};
#endif

    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();
    
    if(!data || len == 0){
        return QB_EINVALIDPARAM;
    }

    if(!ota_context){
        return QB_EINIT;
    }


    qm_ble_timer_stop(&ota_context->ota_timer);

    QM_BLE_LOGD(QM_BLE_LOG_OTA, "ble ota request parsed");

    err_code = qm_ble_ota_req_parse(data, len, &ota_context->ota_file);
    if(err_code != QB_SUCCESS){
        err_code = qm_ble_ota_req_rsp(msg_id, QM_BLE_COM_ERR_INVALID_PARA, ota_context->ota_reason);
        goto exit;
    }

    QM_BLE_LOGD(QM_BLE_LOG_OTA, "ble ota version check start");
    
    qm_ble_ota_ver_check(ota_context->ota_file.ota_type, &ota_context->ota_file.ver, &ota_context->ota_reason);
    if(ota_context->ota_reason == QM_BLE_DEV_NORMAL){
        QM_BLE_LOGD(QM_BLE_LOG_OTA, "ble ota version check success");
        
#if QB_ENABLE_MCU_OTA 
        if(ota_context->ota_file.ota_type == QM_BLE_OTA_TYPE_MCU){
            if(ota_context->ota_cb){

                mcu_ota_notify.file_size = ota_context->ota_file.file_size;
                mcu_ota_notify.hash = ota_context->ota_file.hash;
                mcu_ota_notify.hash_len = ota_context->ota_file.hash_len;
                memcpy(mcu_ota_notify.hash_value, ota_context->ota_file.hash_value, ota_context->ota_file.hash_len);

                ota_context->ota_reason = (qm_ble_ota_reason_t)ota_context->ota_cb(ota_context->ota_file.ota_type, QM_BLE_OTA_REQ, (uint8_t*)&mcu_ota_notify, sizeof(mcu_ota_notify));
            }
        }
#endif 

        if(ota_context->ota_file.ota_type == QM_BLE_OTA_TYPE_BLE){
            if(ota_context->ota_cb){
                ota_context->ota_reason = (qm_ble_ota_reason_t)ota_context->ota_cb(ota_context->ota_file.ota_type, QM_BLE_OTA_REQ, NULL, 0);
            }
        }

        QM_BLE_LOGD(QM_BLE_LOG_OTA, "ota reason: %d", ota_context->ota_reason);
             
    }else{
        QM_BLE_LOGD(QM_BLE_LOG_OTA, "ble ota version check failed");
    }

    err_code = qm_ble_ota_req_rsp(msg_id, com_code, ota_context->ota_reason);

exit:
    if(err_code != QB_SUCCESS){
        qm_ble_ota_status_set(QM_BLE_OTA_STATE_IDLE);
    }else{
        qm_ble_timer_start(&ota_context->ota_timer);
        qm_ble_ota_status_set(QM_BLE_OTA_STATE_DATA);
    }
    
    return err_code;
}

static uint32_t qm_ble_ota_data_rsp(uint8_t msg_id, uint8_t com_code)
{   
    uint32_t err_code = 0;

    qm_ble_ota_data_rsp_t data_rsp = {0};
    
    data_rsp.seq = msg_id;
    data_rsp.file_size = QM_BLE_SWAP32(qm_ble_ota_download_size_get());

    return qm_ble_ota_post_ack(msg_id, QM_BLE_CMD_OTA_DATA, com_code, (uint8_t*)&data_rsp, sizeof(qm_ble_ota_data_rsp_t));
}


uint32_t qm_ble_ota_data_async_rsp(void)
{
    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();

    if(!ota_context){
        return QB_EINIT;
    }
    qm_ble_ota_data_rsp(ota_context->msg_id, QM_BLE_COM_NO_ERR);

    return QB_SUCCESS;
}



static uint32_t qm_ble_ota_data_handle(uint8_t msg_id, uint8_t *data, uint16_t len)
{
    uint32_t err_code = 0;
    uint16_t deal_len = 0;
    uint32_t flash_offset = 0;
    qm_ble_ota_progress_t ota_progress = {0};
    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();

    if(!ota_context){
        return QB_EINIT;
    }

    QM_BLE_LOGD(QM_BLE_LOG_OTA, "qm ble ota msg id: %d", msg_id);
    
    if (msg_id == qm_ble_ota_next_seq_get()) {
        qm_ble_ota_status_set(QM_BLE_OTA_STATE_DATA);
        qm_ble_ota_next_seq_inc();
        qm_ble_ota_timeout_cnt_clear();

        // in the case that the device reply info missed, the timer reply info again
        // saved the last reply info and used in the time
        ota_context->msg_id = msg_id;
    
        if ((len + ota_context->ota_buf_offset) >= QM_BLE_OTA_BUF_SIZE) {
            deal_len = QM_BLE_OTA_BUF_SIZE - ota_context->ota_buf_offset;
            memcpy(ota_context->ota_data_buf + ota_context->ota_buf_offset, data, deal_len);
            ota_context->ota_buf_offset += QM_BLE_OTA_BUF_SIZE - ota_context->ota_buf_offset;
            qm_ble_ota_download_size_inc(deal_len);
        
#if QB_ENABLE_MCU_OTA
            if(ota_context->ota_file.ota_type == QM_BLE_OTA_TYPE_MCU){
                if(ota_context->ota_cb){
                    ota_context->ota_cb(ota_context->ota_file.ota_type, QM_BLE_OTA_DATA, ota_context->ota_data_buf, ota_context->ota_buf_offset);
                }
            }
#endif
            if(ota_context->ota_file.ota_type == QM_BLE_OTA_TYPE_BLE){
                if(0 != qm_ble_ota_flash_write(&flash_offset, ota_context->ota_data_buf, ota_context->ota_buf_offset)){
                    err_code = QB_EFLASH;
                    goto exit;
                }else{
                     
                    if(flash_offset != qm_ble_ota_download_size_get()){

                        QM_BLE_LOGD(QM_BLE_LOG_STATS, "flash offset %d downloaded %d", flash_offset, qm_ble_ota_download_size_get());
                        err_code = QB_EFLASH;
                        goto exit;
                    }
                } 
            }

            ota_context->ota_download_percent = qm_ble_ota_download_size_get()*100/ota_context->ota_file.file_size;
            ota_progress.percent = ota_context->ota_download_percent;
            if(ota_context->ota_cb){
                ota_context->ota_cb(ota_context->ota_file.ota_type, QM_BLE_OTA_DATA_PROGRESS, &ota_progress.percent, sizeof(qm_ble_ota_progress_t));
            }

            ota_context->ota_buf_offset = 0;
            memcpy(ota_context->ota_data_buf + ota_context->ota_buf_offset, data + deal_len, len - deal_len);
            ota_context->ota_buf_offset += len - deal_len;  

            qm_ble_ota_download_size_inc(len - deal_len);
            
        }else{
            memcpy(ota_context->ota_data_buf + ota_context->ota_buf_offset, data, len);
            ota_context->ota_buf_offset += len;
            qm_ble_ota_download_size_inc(len);
        }


        // reply the app if received the last package in the loop
        if (QM_BLE_OTA_TOTAL_PACKAGES == qm_ble_ota_next_seq_get()) {
            ota_context->next_seq = 0;
            QM_BLE_LOGD(QM_BLE_LOG_OTA, "reply loop");

#if QB_ENABLE_OTA_DATA_ASYNC
            if(ota_context->ota_cb){
                ota_context->ota_cb(ota_context->ota_file.ota_type, QM_BLE_OTA_DATA_LOOP, NULL, 0);
            }
#else
            qm_ble_ota_data_rsp(msg_id, QM_BLE_COM_NO_ERR);  
#endif
        }
        // if the last package, write to flash and reply the app
        if ( qm_ble_ota_download_size_get() >= ota_context->ota_file.file_size ) {
            QM_BLE_LOGD(QM_BLE_LOG_OTA, "received the last package");
            if(ota_context->ota_file.ota_type == QM_BLE_OTA_TYPE_BLE){
                
                if(ota_context->ota_buf_offset){
                
                    if(0 != qm_ble_ota_flash_write(&flash_offset, ota_context->ota_data_buf, ota_context->ota_buf_offset)){
                        err_code = QB_EFLASH;      
                        goto exit;
                    }else{
                        if(flash_offset != qm_ble_ota_download_size_get()){
                            err_code = QB_EFLASH;
                            goto exit;
                        }
                    }
                    
                }
    #if QB_ENABLE_MCU_OTA
                if(ota_context->ota_file.ota_type == QM_BLE_OTA_TYPE_MCU){
                    if(ota_context->ota_cb){
                        ota_context->ota_cb(ota_context->ota_file.ota_type, QM_BLE_OTA_DATA, ota_context->ota_data_buf, ota_context->ota_buf_offset);
                    }
                }
    #endif

                qm_ble_ota_data_rsp(msg_id, QM_BLE_COM_NO_ERR);  
            }

            qm_ble_ota_status_set(QM_BLE_OTA_STATE_FW_VERIFY);
        }
    
    }else{

        //qm_ble_timer_stop(&ota_context->ota_timer);
        QM_BLE_LOGE("unexpect seq %d, expect seq %d", msg_id, qm_ble_ota_next_seq_get());
        qm_ble_ota_status_set(QM_BLE_OTA_STATE_IDLE);
        qm_ble_ota_data_rsp(ota_context->msg_id, QM_BLE_COM_NO_ERR);
        
        qm_ble_timer_start(&ota_context->ota_timer);
    }


exit:
    if(err_code == QB_EFLASH){
        QM_BLE_LOGE("qm ble ota flash error");
        qm_ble_ota_status_set(QM_BLE_OTA_STATE_OFF);
        qm_ble_ota_data_rsp(msg_id, QM_BLE_COM_ERR_SYSTEM);
    }
    return err_code;

}


static uint32_t qm_ble_ota_crc32_get(uint32_t *crc32)
{
    uint32_t tmp_offset = 0;
    unsigned char tmp_buf[QM_BLE_OTA_BUF_SIZE];
    unsigned int  read_len = 0;
    uint32_t tmp_crc32 = 0;

    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();

    if(!ota_context){
        return QB_EINIT;
    }
    
    while(tmp_offset < ota_context->ota_file.file_size) {

        if(ota_context->ota_file.file_size - tmp_offset > sizeof(tmp_buf)) {
            read_len = sizeof(tmp_buf);
        }
        else {
            read_len = ota_context->ota_file.file_size - tmp_offset;
        }

        if(qm_ble_ota_flash_read(&tmp_offset, tmp_buf, read_len) != 0) {
            break;
        }

        tmp_crc32 = qm_utils_crc32(tmp_buf, read_len, tmp_crc32);
        
    }

    *crc32 = tmp_crc32;
    
    return QB_SUCCESS;
}


static uint32_t qm_ble_ota_md5_get(uint8_t *md5, uint16_t len)
{
    uint32_t tmp_offset = 0;
    uint8_t tmp_md5[16] = {0};
    unsigned char tmp_buf[QM_BLE_OTA_BUF_SIZE];
    unsigned int  read_len = 0;
    uint32_t tmp_crc32 = 0;

    qm_md5_context context = {0};

    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();

    if(!ota_context){
        return QB_EINIT;
    }

    qm_utils_md5_init(&context);                             
    qm_utils_md5_starts(&context); 
    
    while(tmp_offset < ota_context->ota_file.file_size) {

        if(ota_context->ota_file.file_size - tmp_offset > sizeof(tmp_buf)) {
            read_len = sizeof(tmp_buf);
        }
        else {
            read_len = ota_context->ota_file.file_size - tmp_offset;
        }

        if(qm_ble_ota_flash_read(&tmp_offset, tmp_buf, read_len) != 0) {
            break;
        }

         qm_utils_md5_update(&context, tmp_buf, read_len);       
    }

    qm_utils_md5_finish(&context, tmp_md5); 

    memcpy(md5, tmp_md5, 16);
    
    return QB_SUCCESS;
}


static uint32_t qm_ble_hash_check(uint8_t *is_valid)
{
    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();

    uint32_t crc32 = 0;
    uint32_t err_code = 0;
    uint8_t tmp_md5[16] = {0};

    if(!ota_context){
        return QB_EINIT;
    }


    switch(ota_context->ota_file.hash){

    case QM_BLE_HASH_MD5:

        qm_ble_ota_md5_get(tmp_md5, 16);

        if(0 == memcmp(tmp_md5, ota_context->ota_file.hash_value, ota_context->ota_file.hash_len)){
            *is_valid = 1;
        }else{
            *is_valid = 0;
        }

    break;

    case QM_BLE_HASH_SHA1:


    break;

    case QM_BLE_HASH_CRC32:
        
        qm_ble_ota_crc32_get(&crc32);

        crc32 = QM_BLE_SWAP32(crc32);

        QM_BLE_LOGD(QM_BLE_LOG_OTA, "crc32 calculated: %08X", crc32);
        
        if(0 == memcmp((uint8_t*)&crc32, ota_context->ota_file.hash_value, ota_context->ota_file.hash_len)){
            *is_valid = 1;
        }else{
            *is_valid = 0;
        }
        
    break;


    default:
        break;
    }

    return QB_SUCCESS;
    
}


static uint32_t qm_ble_ota_verify_rsp(uint8_t msg_id, uint8_t is_valid)
{   
    uint8_t com_code = 0;

    if(is_valid){
        com_code = QM_BLE_COM_NO_ERR;
    }else{
        com_code = QM_BLE_COM_ERR_VERIFY;
    }
     
    return qm_ble_ota_post_ack(msg_id, QM_BLE_CMD_OTA_VERIFY, com_code, NULL, 0);
}


static uint32_t qm_ble_ota_verify_handle(uint8_t msg_id, uint8_t *data, uint16_t len)
{
    uint32_t err_code = 0;
    uint8_t is_valid = 0;
    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();

    qm_ble_ota_reason_t ota_reason= {0};

    if(!ota_context){
        return QB_EINIT;
    }
    
    qm_ble_timer_stop(&ota_context->ota_timer);

    QM_BLE_LOGD(QM_BLE_LOG_OTA, "verify start");
    
#if QB_ENABLE_MCU_OTA
    if(ota_context->ota_file.ota_type == QM_BLE_OTA_TYPE_MCU){
        if(ota_context->ota_cb){
            ota_reason = (qm_ble_ota_reason_t)ota_context->ota_cb(ota_context->ota_file.ota_type, QM_BLE_OTA_VERIFY, NULL, 0);
            QM_BLE_LOGD(QM_BLE_LOG_OTA, "mcu dev verify result: %d", ota_reason);
            if(ota_reason == QM_BLE_DEV_NORMAL){
                qm_ble_ota_verify_rsp(msg_id, 1);
            }else{
                qm_ble_ota_verify_rsp(msg_id, 0);
            }
        }
    }
#endif

    if(ota_context->ota_file.ota_type == QM_BLE_OTA_TYPE_BLE){
        qm_ble_hash_check(&is_valid);
        QM_BLE_LOGD(QM_BLE_LOG_OTA, "ble dev verify result: %d", is_valid);
        qm_ble_ota_verify_rsp(msg_id, is_valid);
        qm_ble_ota_end(is_valid);
    }

    qm_ble_ota_reset();
  
    return QB_SUCCESS;

}



uint32_t qm_ble_ota_dispatcher(uint8_t msg_id, uint8_t cmd, uint8_t *data, uint16_t len)
{
    uint32_t err_code = 0;
    qm_ble_ver_t ver = {0};
    uint8_t com_code = 0;
    qm_ble_ota_state_t ota_state;

    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();
    if(!ota_context){
        return QB_EINVALIDPARAM;
    }

    ota_state = qm_ble_ota_status_get();
    
    switch(cmd){

    case QM_BLE_CMD_OTA_QUERY_VER:

        QM_BLE_LOGD(QM_BLE_LOG_OTA, "ble ota query version");


        err_code = qm_ble_ota_ver_handle(msg_id, data, len);

    break;

    case QM_BLE_CMD_OTA_REQUEST:

	    qm_ble_ota_reset();
        qm_ble_ota_start();
        
        err_code = qm_ble_ota_req_handle(msg_id, data, len);
          
    break;

    case QM_BLE_CMD_OTA_DATA:

        if(ota_state != QM_BLE_OTA_STATE_IDLE && ota_state != QM_BLE_OTA_STATE_DATA){
            goto exit;
        }

        err_code = qm_ble_ota_data_handle(msg_id, data, len);
        if(err_code != QB_SUCCESS){
            qm_ble_ota_status_set(QM_BLE_OTA_STATE_IDLE);
        }
    
    break;
    

    case QM_BLE_CMD_OTA_VERIFY:

        if(ota_state != QM_BLE_OTA_STATE_FW_VERIFY){
            goto exit;
        }

        qm_ble_timer_stop(&ota_context->ota_timer);
        
        err_code = qm_ble_ota_verify_handle(msg_id, data, len);
    
    break;
    
    default:
        break;

    }


 exit:
    
    return QB_SUCCESS;
    
}

#if QB_ENABLE_MCU_OTA

uint32_t qm_ble_mcu_ver_set(qm_ble_ver_t *mcu_ver)
{
    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();
    
    if(!mcu_ver){
        return QB_EINVALIDPARAM;   
    }
    
    if(!ota_context){
        return QB_EINIT;
    }

    ota_context->mcu_ver.main_ver = mcu_ver->main_ver;
    ota_context->mcu_ver.sub_ver = mcu_ver->sub_ver;
    ota_context->mcu_ver.fix_ver = mcu_ver->fix_ver;
    
    return QB_SUCCESS;
}

uint32_t qm_ble_mcu_reboot_time_set(uint8_t time_s)
{
    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();
    
    if(!ota_context){
        return QB_EINIT;
    }

    ota_context->reboot_time = time_s;
    return QB_SUCCESS;
}

#endif

uint32_t qm_ble_ota_init(qm_ble_ota_conf_t *ota_conf)
{
    void *ota_timer_hdl = NULL;
    if(!ota_conf){
        return QB_EINVALIDPARAM;
    }

    ota_timer_hdl = g_qm_ble_ota.ota_timer.hdl;
    memset(&g_qm_ble_ota, 0, sizeof(qm_ble_ota_context_t));

    g_qm_ble_ota.ota_timer.hdl = ota_timer_hdl;
    g_qm_ble_ota.reboot_time = QM_BLE_OTA_REBOOT_TIME;
    g_qm_ble_ota.status = QM_BLE_OTA_STATE_OFF;

    memcpy(&g_qm_ble_ota.ble_ver, &ota_conf->ble_ver, sizeof(ota_conf->ble_ver));
    memcpy(&g_qm_ble_ota.mcu_ver, &ota_conf->mcu_ver, sizeof(ota_conf->mcu_ver));
    
    g_qm_ble_ota.ota_cb = ota_conf->ota_cb;

    if(g_qm_ble_ota.ota_timer.hdl == NULL){
        qm_ble_timer_new(&g_qm_ble_ota.ota_timer, qm_ble_ota_timer_callback, &g_qm_ble_ota, QM_BLE_OTA_RETRY_TIMEOUT, 1);
    }
  
    return QB_SUCCESS;
    
}


void qm_ble_ota_reset(void)
{

    qm_ble_ota_context_t *ota_context = qm_ble_ota_context_get();

    if(!ota_context){
        return;
    }

    memset(&ota_context->ota_file, 0, sizeof(ota_context->ota_file));
    ota_context->ota_buf_offset = 0;
    ota_context->ota_timeout_cnt = 0;
    ota_context->ota_download_file_size = 0;
    ota_context->next_seq = 0;
    ota_context->status = QM_BLE_OTA_STATE_OFF;
    
    qm_ble_timer_stop(&ota_context->ota_timer);

    return;
}

void qm_ble_ota_relate_event(uint8_t event)
{

    switch (event) {

        case QB_EVENT_CONNECTED:
            

        break;

        case QB_EVENT_DISCONNECTED:

            qm_ble_ota_reset();
        
        break;
        
#if QB_ENABLE_AUTH
        
        case QB_EVENT_AUTHENTICATED:
            
        
        break;
        
#endif

        default:
            break;
    }
    
}



#endif
