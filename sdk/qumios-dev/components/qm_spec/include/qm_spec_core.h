/* Define to prevent recursive inclusion -------------------------------------*/

#ifndef __QM_SPEC_CORE_H__
#define __QM_SPEC_CORE_H__

/* Includes ------------------------------------------------------------------*/
#include "qm.h"
#include "qm_spec_api.h"
#include "qm_spec_log.h"

typedef enum 
{
    QM_SPEC_DATA_TYPE_HEX           = 0,
#ifndef CONFIG_CANCEL_QM_SPEC_CJSON_SUPPORT
    QM_SPEC_DATA_TYPE_JSON          = 1,
    QM_SPEC_DATA_TYPE_AWS_JSON      = 2,
#endif
    QM_SPEC_DATA_TYPE_MAX,
}qm_spec_data_type_t;

typedef enum 
{
    QM_SPEC_MSG_TYPE_GET            = 0,
    QM_SPEC_MSG_TYPE_SET            = 1,
    QM_SPEC_MSG_TYPE_REPORT         = 2,
    QM_SPEC_MSG_TYPE_ACTION         = 3,
    QM_SPEC_MSG_TYPE_EVENT          = 4,
    QM_SPEC_MSG_TYPE_SERVICE        = 5,    //标准服务

    QM_SPEC_MSG_TYPE_DEVICE_GET     = 6,
    QM_SPEC_MSG_TYPE_CLOUD_GET      = 7,

    QM_SPEC_MSG_TYPE_MAX,
}qm_spec_msg_type_t;

typedef struct 
{   
    int updata_len;
    uint8_t *updata;
    union 
    {    
        qm_spec_event_operation_t           *event_operation;
        qm_spec_action_operation_t          *action_operation;
        qm_spec_property_operation_t        *property_operation;
    }operation;
}qm_spec_data_info_t;

int qm_spec_data_unpack(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type, uint8_t *data, int len,  qm_spec_data_info_t *data_info);
int qm_spec_data_pack(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type, qm_spec_data_info_t *data_info);
int qm_spec_data_destroy(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type, qm_spec_data_info_t *data_info);

int qm_spec_standard_service_unpack(qm_spec_data_type_t data_type, qm_spec_property_operation_t *property_operation, uint8_t *msg, int len);

#endif
