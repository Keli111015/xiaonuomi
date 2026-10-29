#include "qm.h"
#include "qm_log.h"
#include "qm_gpio.h"
#include "qm_errno.h"
#include "sleep.h"
#include "qm_rtc.h"

#define LOG_TAG "TEST"
#define PB09 0x19

#include "harmony_ble_comm.h"
#include "qm_ble_gap.h"


static int data_parse(harmony_ble_service_t *service)
{
    harmony_ble_data_t *data = NULL;
    QM_RETURN_ON_FALSE(service, NULL, LOG_TAG, "service inval");
    QM_RETURN_ON_FALSE(service->count, NULL, LOG_TAG, "count inval");

    data = harmony_ble_data_next(service, NULL);
    while (data)
    {        
        switch (data->type)
        {
            case HARMONY_BLE_PROFILE_DATA_TYPE_BOOL:
            {
                bool_t bool_value = QM_FALSE;
                harmony_ble_data_unpack_bool(data, &bool_value);
                QM_LOGD(LOG_TAG, "S:%s,D:%s V: %d", service->sid, data->data_id, bool_value);

            }
            break;
            case HARMONY_BLE_PROFILE_DATA_TYPE_NUMBER:
            {
                int int_value = 0;
                harmony_ble_data_unpack_number(data, &int_value);
                QM_LOGD(LOG_TAG, "S:%s,D:%s V: %d", service->sid, data->data_id, int_value);
            }
            break;
            case HARMONY_BLE_PROFILE_DATA_TYPE_STRING:
            {
                char sta_str[32] = {0};
                char *p_str = NULL;
                char *str = NULL;
                if(data->len >= 32){
                    p_str = (char *)qm_malloc(data->len + 1);
                    memset(p_str, 0, data->len + 1);
                    str = p_str;
                }else{
                    str = sta_str;
                }

                harmony_ble_data_unpack_string(data, str);

                QM_LOGD(LOG_TAG, "S:%s,D:%s V: %s", service->sid, data->data_id, str);

                if(p_str){
                    qm_free(p_str);
                }
            }
            break;
            default:
            break;
        }
        data = harmony_ble_data_next(service, data);
    }
    return QM_EOK;
}

static int harmony_put(harmony_ble_profile_t *profile)
{
    char *sid = NULL;
    harmony_ble_profile_t *report_profile = NULL;
    harmony_ble_service_t *report_srvice = NULL;
    harmony_ble_data_t *report_data = NULL;
    harmony_ble_service_t *service = NULL;
    QM_RETURN_ON_FALSE(profile, NULL, LOG_TAG, "service inval");
    QM_RETURN_ON_FALSE(profile->count, NULL, LOG_TAG, "count inval");

    service = harmony_ble_service_next(profile, NULL);
    while (service)
    {
        sid = harmony_ble_service_id_get(service);
        QM_LOGD(LOG_TAG, "service id :%d...", sid);
        if(!strncmp(sid, "manipulate", strlen(sid))){
            report_profile = harmony_ble_profile_creat();
            if(report_profile == NULL){
                QM_LOGE(LOG_TAG, "report_profile creat fail...");
                break;
            }
            report_srvice = harmony_ble_service_creat();
            if(report_profile == NULL){
                QM_LOGE(LOG_TAG, "report_profile creat fail...");
                break;
            }

            harmony_ble_service_id_set(report_srvice, "manipulate");
            
            report_data = harmony_ble_data_creat();
            if(report_profile == NULL){
                QM_LOGE(LOG_TAG, "report_profile creat fail...");
                break;
            }

            harmony_ble_data_id_set(report_data, "snapshot");
						
            harmony_ble_data_pack_string(report_data, "{\\\"on\\\":0,\\\"mode\\\":2,\\\"Chargingstatus\\\":0,\\\"BatteryLevel\\\":70,\\\"CleanRemind\\\":0}");

            harmony_ble_data_add(report_srvice, report_data);
            
            harmony_ble_service_add(report_profile, report_srvice);

            harmony_ble_comm_report(report_profile);
        }

        data_parse(service);
        service = harmony_ble_service_next(profile, service);
    }
    
}

void qm_application_start(void)
{
    harmony_ble_comm_param_t param = {
        .event_notify = NULL,
        .get = NULL,
        .put = harmony_put,
        .info = {
            .adv_name = "DN",
            .fw_ver = "1.1.1",
            .hw_ver = "1.1.1",
            .sw_ver = "1.1.1",
            .model = "DT-R207",
            .manu = "Shaver",
            .productid = "2NRD",
            .sub_pid = 0,
        },
        .pair_mode = HARMONY_BLE_PAIR_JUST_WORK,
    };
    QM_LOGD(LOG_TAG, "QM APP START !!!");
    harmony_ble_comm_init(&param);

}