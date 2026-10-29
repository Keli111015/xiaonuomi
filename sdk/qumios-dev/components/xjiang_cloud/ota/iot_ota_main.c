#include "iot_ota.h"
#include "cloud_common.h"
#include "qm_utils_string.h"
#include "iot_event.h"

#if CONFIG_OTA_SUPPORT
int ota_loop(void** handle, void *pcontext)
{
     int rc = 0;
     static int last_percent = 0;
     int percent = 0;
     int len, size_downloaded, size_file;
     uint32_t firmware_valid;
     uint8_t md5[16] = {0};
     int err_code;
#ifdef MCU_OTA_SUPPORT 
     int event_value = 0;
     void *callback = NULL;     
#endif

     cloud_context_t *context=(cloud_context_t*)pcontext;

     ota_struct_pt h_ota = (ota_struct_pt) (*handle);
     if(h_ota == NULL){
        return -1;
     }

    if (iot_ota_isfetching(h_ota))
    {
        
        context->loop_timeout = FAST_LOOP_TIME;
        
        len = iot_ota_fetchyield(h_ota, h_ota->ota_buf, OTA_BUF_LEN, 10);
        if (len > 0) {

            if(h_ota->ota_type == IOT_OTA_TYPE_DEVICE){
                hal_firmware_upgrade_write(h_ota->ota_buf, len);
            }

#ifdef MCU_OTA_SUPPORT 
            if(h_ota->ota_type == IOT_OTA_TYPE_MCU){

                event_value = EVENT_VALUE_OTA_DATA;
                callback = iot_event_callback(IOT_EVENT_NOTIFY);
                if (callback) {
                    ((int (*)(const int, const int, const char*, const int))callback)(IOT_MCU_DEVID, event_value, (char*)h_ota->ota_buf, len);         
                }
            }
#endif     

        } else {
            err_code = IOT_OTAP_FETCH_FAILED;
            iot_ota_reportprogress(h_ota, err_code, NULL, h_ota->ota_type);
            OTA_LOG_ERROR("ota fetch fail");
            rc =- 1;
            goto exit;
        }

        /* get OTA information */

        iot_ota_ioctl(h_ota, IOT_OTAG_FETCHED_SIZE, &size_downloaded, 4);
        iot_ota_ioctl(h_ota, IOT_OTAG_FILE_SIZE, &size_file, 4);

        percent = (size_downloaded * 100) / size_file;
        if(percent % 10 == 0 && last_percent != percent)
        {
            last_percent = percent;
            iot_ota_reportprogress(h_ota, percent, NULL, h_ota->ota_type);
        }
    }
    else if(iot_ota_isfetch_finsh(h_ota))
    {
        iot_ota_ioctl(h_ota, IOT_OTAG_CHECK_FIRMWARE, &firmware_valid, 4);
        if (0 == firmware_valid) {
            OTA_LOG_DEBUG("The firmware is invalid");
            err_code = IOT_OTAP_CHECK_FALIED;
            iot_ota_reportprogress(h_ota, IOT_OTAP_CHECK_FALIED, NULL, h_ota->ota_type);
        } else {
            OTA_LOG_DEBUG("The firmware is valid");
            err_code = IOT_OTAP_SUCCESS;
            hex_str_to_nums(h_ota->md5sum, 32, md5);
            //hal_firmware_upgrade_write((char*)md5, 16);
            context->loop_timeout = NORMAL_LOOP_TIME;
        }   
        goto exit;
    }

     return rc;

exit:

    #ifdef MCU_OTA_SUPPORT 
    if(h_ota->ota_type == IOT_OTA_TYPE_MCU){

        event_value = EVENT_VALUE_OTA_VERIFY;
        
        callback = iot_event_callback(IOT_EVENT_NOTIFY);
        if (callback) {
            ((int (*)(const int, const int, const char*, const int))callback)(IOT_MCU_DEVID, event_value, (char*)&err_code, sizeof(int));         
        }
    }
#endif   

    if(h_ota->ota_type == IOT_OTA_TYPE_DEVICE){
        if(err_code == IOT_OTAP_SUCCESS){
            hal_firmware_upgrade_finish( 1 );
        }else{
            hal_firmware_upgrade_finish( 0 );
        }
    }

    
    if (NULL != h_ota) {
        iot_ota_deinit(h_ota);
    }

    (*handle)=iot_ota_init(context->product_info.product_id, context->product_info.device_id, context->pclient);
    if((*handle)==NULL)
    {
        rc = -1;
        iot_log_err("initialize OTA failed");
    }

    context->loop_timeout=NORMAL_LOOP_TIME;

    return rc;

 }

#endif

