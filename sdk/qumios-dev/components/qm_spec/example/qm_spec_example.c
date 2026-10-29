#include "qm.h"
#include "qm_work.h"
#include "qm_spec_api.h"
#include "qm_spec_hal.h"
#include "qm_spec_core.h"

#define LOG_TAG "SPEC_EXAMPLE"

static qm_work_t work_action = {0};

static int spec_hex_send(qm_spec_msg_type_t msg_type, uint8_t *data, int len)
{
    int ret = QM_EOK;
    switch (msg_type)
    {
        case QM_SPEC_MSG_TYPE_GET:
            
        break;
        
        case QM_SPEC_MSG_TYPE_SET:
            
        break;

        case QM_SPEC_MSG_TYPE_REPORT:
           
        break;

        default:
        break;
    }
    return ret;
}

static int spec_json_send(qm_spec_msg_type_t msg_type, uint8_t *data, int len)
{
    int ret = QM_EOK;
    switch (msg_type)
    {
        case QM_SPEC_MSG_TYPE_GET:
            
        break;
        
        case QM_SPEC_MSG_TYPE_SET:
            
        break;

        case QM_SPEC_MSG_TYPE_REPORT:
           
        break;

        default:
        break;
    }
    return ret;
}

static int spec_hal_send(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type, uint8_t *data, int len)
{
    int ret = QM_EOK;
    switch (data_type)
    {
        case QM_SPEC_DATA_TYPE_HEX:
            ret = spec_hex_send(msg_type, data, len);
        break;
        
        case QM_SPEC_DATA_TYPE_AWS_JSON:
            ret = spec_json_send(msg_type, data, len);
        break;

        default:
        break;
    }

    return ret;
}

static int spec_event_notify(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type, qm_spec_data_info_t *data_info)
{
    qm_spec_property_t  *property = NULL;
    qm_spec_property_operation_t  *property_operation = NULL;
    if(data_type == QM_SPEC_DATA_TYPE_HEX){
        QM_LOGD(LOG_TAG, "QM_SPEC_DATA_TYPE_HEX");
    }else if(data_type == QM_SPEC_DATA_TYPE_AWS_JSON){
        QM_LOGD(LOG_TAG, "QM_SPEC_DATA_TYPE_AWS_JSON");
    }

    switch (msg_type)
    {
        case QM_SPEC_MSG_TYPE_GET:
        {
            property_operation = &data_info->property_operation;
            while (1)
            {
                property = qm_spec_property_next(property_operation, property);
                if(property == NULL){
                    break;
                }
                switch (property->value.property_format)
                {
                    case QM_SPEC_PROPERTY_FORMAT_BOOL:
                        qm_spec_property_pack_bool(property, QM_TRUE);
                    break;

                    case QM_SPEC_PROPERTY_FORMAT_INT32:
                        qm_spec_property_pack_int32(property, 5600);
                    break;

                    case QM_SPEC_PROPERTY_FORMAT_STRING:
                        qm_spec_property_pack_string(property, "HELLO WROLD!!");
                    break;

                    default:
                    break;
                }
            }
        }
        break;
        
        case QM_SPEC_MSG_TYPE_SET:
            
        break;

        default:
        break;
    }
}

static void qm_spec_report_timer(void *arg)
{
    qm_spec_property_t  *property = NULL;
    qm_spec_property_operation_t  *property_operation = qm_spec_property_operation_creat();
    if(property_operation == NULL){
        return;
    }

    property = qm_spec_property_creat();
    qm_spec_propety_pack_xiid(property, 2, 1);
    qm_spec_property_pack_bool(property, QM_TRUE);
    qm_spec_property_add(property_operation, property);

    property = qm_spec_property_creat();
    qm_spec_propety_pack_xiid(property, 2, 2);
    qm_spec_property_pack_bool(property, QM_TRUE);
    qm_spec_property_add(property_operation, property);


    qm_spec_properties_changed(QM_SPEC_DATA_TYPE_AWS_JSON, property_operation);  //内部自动free内存块

    qm_post_delayed_action(&work_action, qm_spec_report_timer, NULL, 60 * 1000);
}

void qm_application_start(void)
{   
    QM_LOGD(LOG_TAG, "QM APP START !!!");

    qm_spec_hal_t generic_hal =
    {
        .hal_send = spec_hal_send,
    };

    qm_spec_init_param_t init_param = {
        .event_notify = spec_event_notify,
    };
    qm_spec_hal_register(&generic_hal);

    qm_spec_init(&init_param);

    qm_post_delayed_action(&work_action, qm_spec_report_timer, NULL, 60 * 1000);
}

