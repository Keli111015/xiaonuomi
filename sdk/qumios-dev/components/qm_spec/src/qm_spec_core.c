#include "qm.h"
#include "qm_spec_api.h"
#include "qm_spec_core.h"

#ifndef CONFIG_CANCEL_QM_SPEC_CJSON_SUPPORT
#include "cJSON.h"
#endif

#include "qm_utils_string.h"

#define LOG_TAG "SPEC_CORE"

#define SPEC_JSON_KEY_PROPERTIES    "properties"

#define SPEC_JSON_KEY_DID    "did"
#define SPEC_JSON_KEY_SIID   "siid"
#define SPEC_JSON_KEY_PIID   "piid"
#define SPEC_JSON_KEY_CODE   "code"
#define SPEC_JSON_KEY_VALUE  "value"


#pragma pack(1)

typedef struct 
{
    uint8_t             siid;
    uint16_t            piid;
    uint16_t            len;
    uint8_t             format;
    uint8_t             value[];
}qm_spec_service_request_frame_t;

typedef struct 
{
    uint8_t             siid;
    uint16_t            piid;
    uint16_t            len;
    uint8_t             format;
    uint8_t             value[];
}qm_spec_set_request_frame_t;

typedef struct 
{
    uint8_t             siid;
    uint16_t            piid;
    int8_t              code;
}qm_spec_set_response_frame_t;

typedef struct 
{
    uint8_t             siid;
    uint16_t            piid;
}qm_spec_get_request_frame_t;

typedef struct 
{
    uint8_t             siid;
    uint16_t            piid;
    int8_t              code;
    uint16_t            len;
    uint8_t             format;
    uint8_t             value[];
}qm_spec_get_response_frame_t;

typedef struct 
{
    uint8_t             siid;
    uint16_t            piid;
    uint16_t            len;
    uint8_t             format;
    uint8_t             value[];
}qm_spec_report_frame_t;

#pragma pack()

typedef struct 
{
    qm_spec_data_type_t data_type;
    qm_spec_msg_type_t msg_type;
    int (*unpack)(uint8_t *data, int len, qm_spec_data_info_t *data_info);
    int (*pack)(qm_spec_data_info_t *event_info);
    int (*destroy)(qm_spec_data_info_t *data_info);
}qm_spec_core_data_handle_t;

#ifndef CONFIG_CANCEL_QM_SPEC_CJSON_SUPPORT

static int qm_spec_aws_json_set_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_aws_json_set_pack(qm_spec_data_info_t *event_info);
static int qm_spec_aws_json_set_destroy(qm_spec_data_info_t *data_info);
static int qm_spec_aws_json_report_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_aws_json_report_pack(qm_spec_data_info_t *event_info);
static int qm_spec_aws_json_report_destroy(qm_spec_data_info_t *data_info);


static int qm_spec_json_set_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_json_set_pack(qm_spec_data_info_t *event_info);
static int qm_spec_json_set_destroy(qm_spec_data_info_t *data_info);
static int qm_spec_json_report_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_json_report_pack(qm_spec_data_info_t *event_info);
static int qm_spec_json_report_destroy(qm_spec_data_info_t *data_info);

static int qm_spec_json_device_get_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_json_device_get_pack(qm_spec_data_info_t *event_info);
static int qm_spec_json_device_get_destroy(qm_spec_data_info_t *data_info);

static int qm_spec_json_cloud_get_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_json_cloud_get_pack(qm_spec_data_info_t *event_info);
static int qm_spec_json_cloud_get_destroy(qm_spec_data_info_t *data_info);

#endif

static int qm_spec_hex_set_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_hex_set_pack(qm_spec_data_info_t *event_info);
static int qm_spec_hex_set_destroy(qm_spec_data_info_t *data_info);

static int qm_spec_hex_get_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_hex_get_pack(qm_spec_data_info_t *event_info);
static int qm_spec_hex_get_destroy(qm_spec_data_info_t *data_info);

static int qm_spec_hex_report_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info);
static int qm_spec_hex_report_pack(qm_spec_data_info_t *event_info);
static int qm_spec_hex_report_destroy(qm_spec_data_info_t *data_info);

static qm_spec_core_data_handle_t  g_qm_spec_data_handle[] = {
#ifndef CONFIG_CANCEL_QM_SPEC_CJSON_SUPPORT
    {QM_SPEC_DATA_TYPE_AWS_JSON, QM_SPEC_MSG_TYPE_REPORT, qm_spec_aws_json_report_unpack, qm_spec_aws_json_report_pack, qm_spec_aws_json_report_destroy},
    {QM_SPEC_DATA_TYPE_AWS_JSON, QM_SPEC_MSG_TYPE_SET, qm_spec_aws_json_set_unpack, qm_spec_aws_json_set_pack, qm_spec_aws_json_set_destroy},

    {QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_REPORT, qm_spec_json_report_unpack, qm_spec_json_report_pack, qm_spec_json_report_destroy},
    {QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_SET, qm_spec_json_set_unpack, qm_spec_json_set_pack, qm_spec_json_set_destroy},

    {QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_CLOUD_GET, qm_spec_json_cloud_get_unpack, qm_spec_json_cloud_get_pack, qm_spec_json_cloud_get_destroy},
    {QM_SPEC_DATA_TYPE_JSON, QM_SPEC_MSG_TYPE_DEVICE_GET, qm_spec_json_device_get_unpack, qm_spec_json_device_get_pack, qm_spec_json_device_get_destroy},

#endif
    {QM_SPEC_DATA_TYPE_HEX, QM_SPEC_MSG_TYPE_SET, qm_spec_hex_set_unpack, qm_spec_hex_set_pack, qm_spec_hex_set_destroy},
    {QM_SPEC_DATA_TYPE_HEX, QM_SPEC_MSG_TYPE_GET, qm_spec_hex_get_unpack, qm_spec_hex_get_pack, qm_spec_hex_get_destroy},
    {QM_SPEC_DATA_TYPE_HEX, QM_SPEC_MSG_TYPE_REPORT, qm_spec_hex_report_unpack, qm_spec_hex_report_pack, qm_spec_hex_report_destroy},
    {QM_SPEC_DATA_TYPE_HEX, QM_SPEC_MSG_TYPE_SERVICE, qm_spec_hex_set_unpack, qm_spec_hex_set_pack, qm_spec_hex_set_destroy},
};

static qm_spec_core_data_handle_t *qm_spec_data_handle_get(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_qm_spec_data_handle); i++){
        if( msg_type == g_qm_spec_data_handle[i].msg_type &&
            data_type == g_qm_spec_data_handle[i].data_type){
            return &g_qm_spec_data_handle[i];
        }
    }
    return NULL;
}

#ifndef CONFIG_CANCEL_QM_SPEC_CJSON_SUPPORT
/*
    "1": {
        "properties": {
        "1": 20,
        "2": 0
        }
    }
*/
static int qm_spec_json_data_value_unpack(qm_spec_property_t *property, cJSON *object)
{
    char str_id[32] = {0};
    if (object == NULL) {
        QM_SPEC_LOGE(LOG_TAG, "object invalid");
        return -QM_EINVAL;
    }
    if (property == NULL) {
        QM_SPEC_LOGE(LOG_TAG, "property invalid");
        return -QM_EINVAL;
    }

    snprintf(str_id, 32, "%d", property->piid);
 
    switch ((property->value.property_format & 0xFF))
    {
        case QM_SPEC_PROPERTY_FORMAT_BOOL:
        {
            bool_t bool_vaule = 0;
            qm_spec_property_unpack_bool(property, &bool_vaule);
            cJSON_AddBoolToObject(object, str_id, (const cJSON_bool)bool_vaule);
        }
        break;

        case QM_SPEC_PROPERTY_FORMAT_NUMBER:
        {
            int int32_vaule = 0;
            qm_spec_property_unpack_number(property, &int32_vaule);
            cJSON_AddNumberToObject(object, str_id, (const double)int32_vaule);
        }
        break;

        case QM_SPEC_PROPERTY_FORMAT_INT8:
        {
            int8_t int8_vaule = 0;
            qm_spec_property_unpack_int8(property, &int8_vaule);
            cJSON_AddNumberToObject(object, str_id, (const double)int8_vaule);
        }
        break;
        
        case QM_SPEC_PROPERTY_FORMAT_UINT8:
        {
            uint8_t uint8_vaule = 0;
            qm_spec_property_unpack_uint8(property, &uint8_vaule);
            cJSON_AddNumberToObject(object, str_id, (const double)uint8_vaule);
        }
        break;

        case QM_SPEC_PROPERTY_FORMAT_INT16:
        {
            int16_t int16_vaule = 0;
            qm_spec_property_unpack_int16(property, &int16_vaule);
            cJSON_AddNumberToObject(object, str_id, (const double)int16_vaule);
        }
        break;

        case QM_SPEC_PROPERTY_FORMAT_UINT16:
        {
            uint16_t uint16_vaule = 0;
            qm_spec_property_unpack_uint16(property, &uint16_vaule);
            cJSON_AddNumberToObject(object, str_id, (const double)uint16_vaule);
        }
        break;

        case QM_SPEC_PROPERTY_FORMAT_UINT32:
        {
            uint32_t uint32_vaule = 0;
            qm_spec_property_unpack_uint32(property, &uint32_vaule);
            cJSON_AddNumberToObject(object, str_id, (const double)uint32_vaule);
        }
        break;

        case QM_SPEC_PROPERTY_FORMAT_INT32:
        {
            int32_t int32_vaule = 0;
            qm_spec_property_unpack_int32(property, &int32_vaule);
            cJSON_AddNumberToObject(object, str_id, (const double)int32_vaule);
        }
        break;

        case QM_SPEC_PROPERTY_FORMAT_STRING:
        {
            int str_size = 0;
            char *str_vaule = NULL;
            qm_spec_property_unpack_string_direct(property, &str_vaule, &str_size);
            cJSON_AddStringToObject(object, str_id, (const char * const)str_vaule);
        }
        break;

        default:

        break;
    }
    return QM_EOK;
}

static int qm_spec_json_data_value_pack(qm_spec_property_t *property, cJSON *object)
{

    if (object == NULL) {
        QM_SPEC_LOGE(LOG_TAG, "object invalid");
        return -QM_EINVAL;
    }
    if (property == NULL) {
        QM_SPEC_LOGE(LOG_TAG, "property invalid");
        return -QM_EINVAL;
    }

    switch ((object->type & 0xFF))
    {
        case cJSON_False:
            qm_spec_property_pack_bool(property, QM_FALSE);
        break;
        
        case cJSON_True:
            qm_spec_property_pack_bool(property, QM_TRUE);
        break;

        case cJSON_Number:
            qm_spec_property_pack_number(property, object->valueint);
        break;

        case cJSON_String:
            qm_spec_property_pack_string(property, object->valuestring, strlen(object->valuestring));
        break;

        default:

        break;
    }
    return QM_EOK;
}

static int qm_spec_aws_json_report_pack(qm_spec_data_info_t *event_info)
{
    int ret = QM_EOK;
    char *str_data = NULL;
    char str_id[32] = {0};
    cJSON *root = NULL;
    cJSON *json_siid = NULL;
    cJSON *json_piid = NULL;
    qm_spec_property_t *property = NULL;
    
    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM;
    }
    while(1)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        qm_snprintf(str_id, 32, "%d", property->siid);
        json_siid = cJSON_GetObjectItem(root, str_id);
        if(json_siid == NULL){
            json_siid = cJSON_AddObjectToObject(root, str_id);
            if(json_siid == NULL){
                break;
            }

            json_piid = cJSON_AddObjectToObject(json_siid, SPEC_JSON_KEY_PROPERTIES);
            if(json_piid == NULL){
                break;
            }
        }

        json_piid = cJSON_GetObjectItem(json_siid, SPEC_JSON_KEY_PROPERTIES);
        if(json_piid == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_piid type no object!!");
            break;
        }

        qm_spec_json_data_value_unpack(property, json_piid);

    }

    str_data = cJSON_PrintUnformatted(root);
    if(str_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    event_info->updata = (uint8_t *)str_data;
    event_info->updata_len = strlen(str_data);
    
__exit:

    if(root){
        cJSON_Delete(root);
        root = NULL;
    }

    return ret;
}

static int qm_spec_aws_json_report_destroy(qm_spec_data_info_t *data_info)
{
    int ret = QM_EOK;

    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return ret;
}

static int qm_spec_aws_json_set_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    int i = 0;
    int ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *json_siid = NULL;
    cJSON *json_piid = NULL;
    cJSON *current_element = NULL;
    qm_spec_property_t *property = NULL;
    data_info->operation.property_operation = qm_spec_property_operation_creat();
    
    if(data_info->operation.property_operation == NULL){
        return -QM_ENOMEM;
    }

    root = cJSON_Parse((const char *)data);
    if(root == NULL){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
        return -QM_ENOMEM;
    }

    for(i = 0; i < cJSON_GetArraySize(root); i++)
    {
        json_siid = cJSON_GetArrayItem(root, i);
        if(json_siid->type != cJSON_Object){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_siid type no object!!");
            break;
        }

        json_piid = cJSON_GetObjectItem(json_siid, SPEC_JSON_KEY_PROPERTIES);
        if(json_piid == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_piid type no object!!");
            break;
        }

        current_element = json_piid->child;
        while(current_element)
        {
            property = qm_spec_property_creat();
            if(property == NULL){
                QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack property_element no mem");
                break;  
            }

            property->siid = int_str_to_num(json_siid->string, strlen(json_siid->string));
            property->piid = int_str_to_num(current_element->string, strlen(current_element->string));
            
            qm_spec_json_data_value_pack(property, current_element);
            qm_spec_property_add(data_info->operation.property_operation, property);

            current_element = current_element->next;
        } 
    }

    cJSON_Delete(root);
    return ret;
}

static int qm_spec_aws_json_set_pack(qm_spec_data_info_t *event_info)
{
    return QM_EOK;
}

static int qm_spec_aws_json_set_destroy(qm_spec_data_info_t *data_info)
{
    int ret = QM_EOK;
    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return ret;
}

static int qm_spec_aws_json_report_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    return QM_EOK;
}

/*

[
{
    "did": "12345601",
    "siid": 1,
    "piid": 2,
    "value": "32132"
},
{
    "did": "12345601",
    "siid": 1,
    "piid": 2,
    "value": "32132"
}
]

*/

static int qm_spec_json_set_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    int i = 0;
    int ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *json_object = NULL;
    cJSON *json_did = NULL;
    cJSON *json_siid = NULL;
    cJSON *json_piid = NULL;
    cJSON *json_value = NULL;
    qm_spec_property_t *property = NULL;
    data_info->operation.property_operation = qm_spec_property_operation_creat();
    
    if(data_info->operation.property_operation == NULL){
        return -QM_ENOMEM;
    }

    root = cJSON_Parse((const char *)data);
    if(root == NULL){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
        return -QM_ENOMEM;
    }

    for(i = 0; i < cJSON_GetArraySize(root); i++)
    {
        json_object = cJSON_GetArrayItem(root, i);
        if(json_object->type != cJSON_Object){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_siid type no object!!");
            break;
        }

        json_did = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_DID);
        if(json_did == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_siid type no object!!");
            break;
        }
        

        json_siid = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_SIID);
        if(json_siid == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_siid type no object!!");
            break;
        }
        

        json_piid = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_PIID);
        if(json_piid == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_piid type no object!!");
            break;
        }

        json_value = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_VALUE);
        if(json_value == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_value type no object!!");
            break;
        }

        property = qm_spec_property_creat();
        if(property == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack property_element no mem");
            break;  
        }
        property->siid = json_siid->valueint;
        property->piid = json_piid->valueint;
        property->did = int_str_to_num(json_did->valuestring, strlen(json_did->valuestring));
        qm_spec_json_data_value_pack(property, json_value);
        qm_spec_property_add(data_info->operation.property_operation, property);
    }

    cJSON_Delete(root);
    return ret;
}

static int qm_spec_json_set_pack(qm_spec_data_info_t *event_info)
{
    return QM_EOK;
}

static int qm_spec_json_set_destroy(qm_spec_data_info_t *data_info)
{
    int ret = QM_EOK;
    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return ret;
}

static int qm_spec_json_report_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    return QM_EOK;
}

static int qm_spec_json_report_pack(qm_spec_data_info_t *event_info)
{
    int ret = QM_EOK;
    char *str_data = NULL;
    char str_id[32] = {0};
    cJSON *root = NULL;
    cJSON *object = NULL;
    qm_spec_property_t *property = NULL;
    
    root = cJSON_CreateArray();
    if(root == NULL){
        return -QM_ENOMEM;
    }
    while(1)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        object = cJSON_CreateObject();
        if(object == NULL){
            break;
        }

        qm_snprintf(str_id, 32, "%u", property->did);

        cJSON_AddItemToArray(root, object);
        cJSON_AddStringToObject(object, SPEC_JSON_KEY_DID, str_id);    // 字符串字段
        cJSON_AddNumberToObject(object, SPEC_JSON_KEY_SIID, property->siid);            // 数字字段（整数）
        cJSON_AddNumberToObject(object, SPEC_JSON_KEY_PIID, property->piid);            // 数字字段（整数）

        switch ((property->value.property_format & 0xFF))
        {
            case QM_SPEC_PROPERTY_FORMAT_BOOL:
            {
                bool_t bool_vaule = 0;
                qm_spec_property_unpack_bool(property, &bool_vaule);
                cJSON_AddBoolToObject(object, SPEC_JSON_KEY_VALUE, (const cJSON_bool)bool_vaule);
            }
            break;

            case QM_SPEC_PROPERTY_FORMAT_NUMBER:
            {
                int int32_vaule = 0;
                qm_spec_property_unpack_number(property, &int32_vaule);
                cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)int32_vaule);
            }
            break;

            case QM_SPEC_PROPERTY_FORMAT_INT8:
            {
                int8_t int8_vaule = 0;
                qm_spec_property_unpack_int8(property, &int8_vaule);
                cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)int8_vaule);
            }
            break;
            
            case QM_SPEC_PROPERTY_FORMAT_UINT8:
            {
                uint8_t uint8_vaule = 0;
                qm_spec_property_unpack_uint8(property, &uint8_vaule);
                cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)uint8_vaule);
            }
            break;

            case QM_SPEC_PROPERTY_FORMAT_INT16:
            {
                int16_t int16_vaule = 0;
                qm_spec_property_unpack_int16(property, &int16_vaule);
                cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)int16_vaule);
            }
            break;

            case QM_SPEC_PROPERTY_FORMAT_UINT16:
            {
                uint16_t uint16_vaule = 0;
                qm_spec_property_unpack_uint16(property, &uint16_vaule);
                cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)uint16_vaule);
            }
            break;

            case QM_SPEC_PROPERTY_FORMAT_UINT32:
            {
                uint32_t uint32_vaule = 0;
                qm_spec_property_unpack_uint32(property, &uint32_vaule);
                cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)uint32_vaule);
            }
            break;

            case QM_SPEC_PROPERTY_FORMAT_INT32:
            {
                int32_t int32_vaule = 0;
                qm_spec_property_unpack_int32(property, &int32_vaule);
                cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)int32_vaule);
            }
            break;

            case QM_SPEC_PROPERTY_FORMAT_STRING:
            {
                int str_size = 0;
                char *str_vaule = NULL;
                qm_spec_property_unpack_string_direct(property, &str_vaule, &str_size);
                cJSON_AddStringToObject(object, SPEC_JSON_KEY_VALUE, (const char * const)str_vaule);
            }
            break;

            default:

            break;
        }
        
    }

    str_data = cJSON_PrintUnformatted(root);
    if(str_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    event_info->updata = (uint8_t *)str_data;
    event_info->updata_len = strlen(str_data);
    
__exit:

    if(root){
        cJSON_Delete(root);
        root = NULL;
    }

    return ret;
}

static int qm_spec_json_report_destroy(qm_spec_data_info_t *data_info)
{
    int ret = QM_EOK;

    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return ret;
}

static int qm_spec_json_cloud_get_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    int i = 0;
    int ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *json_object = NULL;
    cJSON *json_did = NULL;
    cJSON *json_siid = NULL;
    cJSON *json_piid = NULL;
    qm_spec_property_t *property = NULL;
    data_info->operation.property_operation = qm_spec_property_operation_creat();
    
    if(data_info->operation.property_operation == NULL){
        return -QM_ENOMEM;
    }

    root = cJSON_Parse((const char *)data);
    if(root == NULL){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
        return -QM_ENOMEM;
    }

    for(i = 0; i < cJSON_GetArraySize(root); i++)
    {
        json_object = cJSON_GetArrayItem(root, i);
        if(json_object->type != cJSON_Object){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_object type no object!!");
            break;
        }

        property = qm_spec_property_creat();
        if(property == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack property_element no mem");
            break;  
        }

        json_did = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_DID);
        if(json_did == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_did type no object!!");
            break;
        }
        property->did = int_str_to_num(json_did->valuestring, strlen(json_did->valuestring));
        

        json_siid = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_SIID);
        if(json_siid == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_siid type no object!!");
            continue;
        }
        property->siid = json_siid->valueint;

        json_piid = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_PIID);
        if(json_piid == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_piid type no object!!");
        }else{
            property->piid = json_piid->valueint;
        }

        qm_spec_property_add(data_info->operation.property_operation, property);
    }

    cJSON_Delete(root);
    return ret;
}

static int qm_spec_json_cloud_get_pack(qm_spec_data_info_t *event_info)
{
    int ret = QM_EOK;
    char *str_data = NULL;
    char str_id[32] = {0};
    cJSON *root = NULL;
    cJSON *object = NULL;
    qm_spec_property_t *property = NULL;
    
    root = cJSON_CreateArray();
    if(root == NULL){
        return -QM_ENOMEM;
    }
    while(1)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        object = cJSON_CreateObject();
        if(object == NULL){
            break;
        }

        qm_snprintf(str_id, 32, "%d", property->did);

        cJSON_AddItemToArray(root, object);
        cJSON_AddStringToObject(object, SPEC_JSON_KEY_DID, str_id);    // 字符串字段
        cJSON_AddNumberToObject(object, SPEC_JSON_KEY_SIID, property->siid);            // 数字字段（整数）
        cJSON_AddNumberToObject(object, SPEC_JSON_KEY_PIID, property->piid);            // 数字字段（整数）
        cJSON_AddNumberToObject(object, SPEC_JSON_KEY_CODE, property->code);            // 数字字段（整数）

        if(property->value.len > 0){
            switch ((property->value.property_format & 0xFF))
            {
                case QM_SPEC_PROPERTY_FORMAT_BOOL:
                {
                    bool_t bool_vaule = 0;
                    qm_spec_property_unpack_bool(property, &bool_vaule);
                    cJSON_AddBoolToObject(object, SPEC_JSON_KEY_VALUE, (const cJSON_bool)bool_vaule);
                }
                break;

                case QM_SPEC_PROPERTY_FORMAT_NUMBER:
                {
                    int int32_vaule = 0;
                    qm_spec_property_unpack_number(property, &int32_vaule);
                    cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)int32_vaule);
                }
                break;

                case QM_SPEC_PROPERTY_FORMAT_INT8:
                {
                    int8_t int8_vaule = 0;
                    qm_spec_property_unpack_int8(property, &int8_vaule);
                    cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)int8_vaule);
                }
                break;
                
                case QM_SPEC_PROPERTY_FORMAT_UINT8:
                {
                    uint8_t uint8_vaule = 0;
                    qm_spec_property_unpack_uint8(property, &uint8_vaule);
                    cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)uint8_vaule);
                }
                break;

                case QM_SPEC_PROPERTY_FORMAT_INT16:
                {
                    int16_t int16_vaule = 0;
                    qm_spec_property_unpack_int16(property, &int16_vaule);
                    cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)int16_vaule);
                }
                break;

                case QM_SPEC_PROPERTY_FORMAT_UINT16:
                {
                    uint16_t uint16_vaule = 0;
                    qm_spec_property_unpack_uint16(property, &uint16_vaule);
                    cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)uint16_vaule);
                }
                break;

                case QM_SPEC_PROPERTY_FORMAT_UINT32:
                {
                    uint32_t uint32_vaule = 0;
                    qm_spec_property_unpack_uint32(property, &uint32_vaule);
                    cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)uint32_vaule);
                }
                break;

                case QM_SPEC_PROPERTY_FORMAT_INT32:
                {
                    int32_t int32_vaule = 0;
                    qm_spec_property_unpack_int32(property, &int32_vaule);
                    cJSON_AddNumberToObject(object, SPEC_JSON_KEY_VALUE, (const double)int32_vaule);
                }
                break;

                case QM_SPEC_PROPERTY_FORMAT_STRING:
                {
                    int str_size = 0;
                    char *str_vaule = NULL;
                    qm_spec_property_unpack_string_direct(property, &str_vaule, &str_size);
                    cJSON_AddStringToObject(object, SPEC_JSON_KEY_VALUE, (const char * const)str_vaule);
                }
                break;

                default:

                break;
            }
        }
    }

    str_data = cJSON_PrintUnformatted(root);
    if(str_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    event_info->updata = (uint8_t *)str_data;
    event_info->updata_len = strlen(str_data);
    
__exit:

    if(root){
        cJSON_Delete(root);
        root = NULL;
    }

    return ret;
}

static int qm_spec_json_cloud_get_destroy(qm_spec_data_info_t *data_info)
{
    int ret = QM_EOK;

    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return ret;
}

static int qm_spec_json_device_get_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    int i = 0;
    int ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *json_object = NULL;
    cJSON *json_did = NULL;
    cJSON *json_siid = NULL;
    cJSON *json_piid = NULL;
    cJSON *json_code = NULL;
    cJSON *json_value = NULL;
    qm_spec_property_t *property = NULL;
    data_info->operation.property_operation = qm_spec_property_operation_creat();
    
    if(data_info->operation.property_operation == NULL){
        return -QM_ENOMEM;
    }

    root = cJSON_Parse((const char *)data);
    if(root == NULL){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
        return -QM_ENOMEM;
    }

    for(i = 0; i < cJSON_GetArraySize(root); i++)
    {
        json_object = cJSON_GetArrayItem(root, i);
        if(json_object->type != cJSON_Object){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_siid type no object!!");
            break;
        }

        property = qm_spec_property_creat();
        if(property == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack property_element no mem");
            break;  
        }

        json_did = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_DID);
        if(json_did == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_siid type no object!!");
            break;
        }
        property->did = int_str_to_num(json_did->valuestring, strlen(json_did->valuestring));
        

        json_siid = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_SIID);
        if(json_siid == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_siid type no object!!");
            break;
        }
        property->siid = json_siid->valueint;

        json_piid = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_PIID);
        if(json_piid == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_piid type no object!!");
            break;
        }
        property->piid = json_piid->valueint;

        json_code = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_CODE);
        if(json_code == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack json_piid type no object!!");
            break;
        }
        property->code = json_code->valueint;

        json_value = cJSON_GetObjectItem(json_object, SPEC_JSON_KEY_VALUE);
        if(json_value){
            qm_spec_json_data_value_pack(property, json_value);
        }

        qm_spec_property_add(data_info->operation.property_operation, property);
    }

    cJSON_Delete(root);
    return ret;
}

static int qm_spec_json_device_get_pack(qm_spec_data_info_t *event_info)
{
    int ret = QM_EOK;
    char *str_data = NULL;
    char str_id[32] = {0};
    cJSON *root = NULL;
    cJSON *object = NULL;
    qm_spec_property_t *property = NULL;
    
    root = cJSON_CreateArray();
    if(root == NULL){
        return -QM_ENOMEM;
    }
    while(1)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        object = cJSON_CreateObject();
        if(object == NULL){
            break;
        }

        qm_snprintf(str_id, 32, "%d", property->did);

        cJSON_AddItemToArray(root, object);
        cJSON_AddStringToObject(object, SPEC_JSON_KEY_DID, str_id);    // 字符串字段

        if(property->siid){
            cJSON_AddNumberToObject(object, SPEC_JSON_KEY_SIID, property->siid);            // 数字字段（整数）
        }
        
        if(property->piid){
            cJSON_AddNumberToObject(object, SPEC_JSON_KEY_PIID, property->piid);            // 数字字段（整数）
        }
    }

    str_data = cJSON_PrintUnformatted(root);
    if(str_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    event_info->updata = (uint8_t *)str_data;
    event_info->updata_len = strlen(str_data);
    
__exit:

    if(root){
        cJSON_Delete(root);
        root = NULL;
    }

    return ret;
}

static int qm_spec_json_device_get_destroy(qm_spec_data_info_t *data_info)
{
    int ret = QM_EOK;

    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return ret;
}

#endif

static int qm_spec_hex_report_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    return QM_EOK;
}

static int qm_spec_hex_report_pack(qm_spec_data_info_t *event_info)
{
    int index = 0;
    int ret = QM_EOK;
    int len = 0;
    int malloc_len = 0;
    uint8_t prop_num = 0;
    uint8_t *malloc_buff = NULL;
    qm_spec_report_frame_t *report_frame = NULL;
    qm_spec_property_t *property = NULL;
    
    prop_num = event_info->operation.property_operation->element_num; // add element_num;
    malloc_len++;

    while(prop_num--)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        malloc_len += (sizeof(qm_spec_report_frame_t) + property->value.len);
    }
    
    malloc_buff = (uint8_t *)qm_malloc(malloc_len + 1); 
    if(malloc_buff == NULL){
        QM_SPEC_LOGE(LOG_TAG, "set pack data no mem (malloc len %d)", malloc_len + 1);
        return -QM_ENOMEM;
    }
    memset(malloc_buff, 0, malloc_len + 1);

    malloc_buff[index] = event_info->operation.property_operation->element_num; // add element_num;
    index++;

    property = NULL;
    while (1)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        report_frame = (qm_spec_report_frame_t *)&malloc_buff[index];

        report_frame->siid = property->siid;
        report_frame->piid = cpu_to_be16(property->piid);
        report_frame->format = property->value.property_format;
        
        qm_spec_property_unpack_bytes(property, report_frame->value, (int *)&len);
        
        report_frame->len = cpu_to_be16(len);
        index += (sizeof(qm_spec_report_frame_t) + len);
        
    }
    
    event_info->updata = malloc_buff;
    event_info->updata_len = malloc_len;

    return ret;
}

static int qm_spec_hex_report_destroy(qm_spec_data_info_t *data_info)
{
    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return QM_EOK;
}

static int qm_spec_hex_get_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    int index = 0;
    int ret = QM_EOK;
    uint8_t prop_num = 0;
    qm_spec_property_t *property = NULL;
    qm_spec_get_request_frame_t *request_frame = NULL;
    data_info->operation.property_operation = qm_spec_property_operation_creat();
    
    if(data_info->operation.property_operation == NULL){
        return -QM_ENOMEM;
    }

    prop_num = data[index]; 
    index++;

    while(prop_num--)
    {
        property = qm_spec_property_creat();
        if(property == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack property_element no mem");
            break;  
        }
        
        request_frame = (qm_spec_get_request_frame_t *)(data + index);
        
        property->siid = request_frame->siid;
        property->piid = request_frame->piid;
        
        qm_spec_property_add(data_info->operation.property_operation, property);
        index += (sizeof(qm_spec_get_request_frame_t));
    }
    
    return ret;
}

static int qm_spec_hex_get_pack(qm_spec_data_info_t *event_info)
{
    int index = 0;
    int ret = QM_EOK;
    int malloc_len = 0;
    uint8_t prop_num = 0;
    uint8_t *malloc_buff = NULL;
    qm_spec_get_response_frame_t *response_frame = NULL;
    qm_spec_property_t *property = NULL;
    
    prop_num = event_info->operation.property_operation->element_num; // add element_num;
    malloc_len++;

    while(prop_num--)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        malloc_len += (sizeof(qm_spec_get_response_frame_t) + property->value.len);
    }
    
    malloc_buff = (uint8_t *)qm_malloc(malloc_len + 1); 
    if(malloc_buff == NULL){
        QM_SPEC_LOGE(LOG_TAG, "set pack data no mem (malloc len %d)", malloc_len + 1);
        return -QM_ENOMEM;
    }
    memset(malloc_buff, 0, malloc_len + 1);

    malloc_buff[index] = event_info->operation.property_operation->element_num; // add element_num;
    index++;

    property = NULL;
    while (1)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        response_frame = (qm_spec_get_response_frame_t *)&malloc_buff[index];

        response_frame->siid = property->siid;
        response_frame->piid = cpu_to_be16(property->piid);
        response_frame->code = property->code;
        response_frame->format = property->value.property_format;

        qm_spec_property_unpack_bytes(property, response_frame->value, (int *)&response_frame->len);
        index += (sizeof(qm_spec_get_response_frame_t) + response_frame->len);

        response_frame->len = cpu_to_be16(response_frame->len);
        
    }
    
    event_info->updata = malloc_buff;
    event_info->updata_len = malloc_len;

    return ret;
}

static int qm_spec_hex_get_destroy(qm_spec_data_info_t *data_info)
{
    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return QM_EOK;
}

static int qm_spec_hex_set_unpack(uint8_t *data, int len, qm_spec_data_info_t *data_info)
{
    int index = 0;
    int ret = QM_EOK;
    uint16_t pro_len = 0;
    qm_spec_set_request_frame_t *request_frame = NULL;
    qm_spec_property_t *property = NULL;

    if(data_info->operation.property_operation == NULL){
        data_info->operation.property_operation = qm_spec_property_operation_creat();
    }
    
    if(data_info->operation.property_operation == NULL){
        return -QM_ENOMEM;
    }

    index++;

    while (index < len)
    {
        property = qm_spec_property_creat();
        if(property == NULL){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack property_element no mem");
            break;  
        }
        
        request_frame = (qm_spec_set_request_frame_t *)(data + index);
        
        if(request_frame->len == 0){
            QM_SPEC_LOGW(LOG_TAG, QM_SPEC_LOG_ERROR, "set unpack len 0 (SIID %d PIID %d)", request_frame->siid, request_frame->piid);
            break;
        }

        property->siid = request_frame->siid;
        property->value.property_format = request_frame->format;

        memcpy(&pro_len, &request_frame->len, sizeof(uint16_t));
        memcpy(&property->piid, &request_frame->piid, sizeof(uint16_t));

        pro_len = be16_to_cpu(pro_len);
        property->piid = be16_to_cpu(property->piid);
        
        qm_spec_property_pack_bytes(property, request_frame->value, pro_len);
        qm_spec_property_add(data_info->operation.property_operation, property);
        index += (sizeof(qm_spec_set_request_frame_t) + pro_len);
    }
    
    return ret;
}

static int qm_spec_hex_set_pack(qm_spec_data_info_t *event_info)
{
    int index = 0;
    int ret = QM_EOK;
    int malloc_len = 0;
    uint8_t *malloc_buff = NULL;
    qm_spec_set_response_frame_t *response_frame = NULL;
    qm_spec_property_t *property = NULL;
    
    malloc_len++;
    malloc_len = sizeof(qm_spec_set_response_frame_t) * event_info->operation.property_operation->element_num;

    malloc_buff = (uint8_t *)qm_malloc(malloc_len + 1); 
    if(malloc_buff == NULL){
        QM_SPEC_LOGE(LOG_TAG, "set pack data no mem (malloc len %d)", malloc_len + 1);
        return -QM_ENOMEM;
    }
    memset(malloc_buff, 0, malloc_len + 1);

    malloc_buff[0] = event_info->operation.property_operation->element_num; // add element_num;
    response_frame = (qm_spec_set_response_frame_t *)&malloc_buff[1];
    while (1)
    {
        property = qm_spec_property_next(event_info->operation.property_operation, property);
        if(property == NULL){
            break;
        }

        response_frame[index].code = property->code;
        response_frame[index].siid = property->siid;
        response_frame[index].piid = cpu_to_be16(property->piid);

        index++;
    }

    event_info->updata = malloc_buff;
    event_info->updata_len = malloc_len;
    
    return ret;
}

static int qm_spec_hex_set_destroy(qm_spec_data_info_t *data_info)
{
    if(data_info->updata){
        qm_free(data_info->updata);
        data_info->updata = NULL;
        data_info->updata_len = 0;
    }

    if(data_info->operation.property_operation){
        qm_spec_property_operation_delete(data_info->operation.property_operation);
        data_info->operation.property_operation = NULL;
    }
    return QM_EOK;
}

int qm_spec_data_unpack(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type, 
                        uint8_t *data, int len,  
                        qm_spec_data_info_t *data_info)
{
    qm_spec_core_data_handle_t *data_handle = NULL;
    
    if(data == NULL || len == 0 || data_info == NULL){
        QM_SPEC_LOGE(LOG_TAG, "unpack arguments invalid (data %p len %d info %p)", data, len, data_info);
        return -QM_EINVAL;
    }

    data_handle = qm_spec_data_handle_get(data_type, msg_type);
    if(data_handle == NULL){
        QM_SPEC_LOGE(LOG_TAG, "no unpack handler (data_type %d msg_type %d)", data_type, msg_type);
        return -QM_EINVAL;
    }

    if(data_handle->unpack == NULL){
        QM_SPEC_LOGE(LOG_TAG, "unpack handler is null (data_type %d msg_type %d)", data_type, msg_type);
        return -QM_EINVAL;
    }

    QM_SPEC_LOGD(LOG_TAG, QM_SPEC_LOG_DOWNLINK, "unpack data_type %d msg_type %d len %d", data_type, msg_type, len);
    return data_handle->unpack(data, len, data_info);
}


int qm_spec_data_pack(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type, qm_spec_data_info_t *data_info)
{
    qm_spec_core_data_handle_t *data_handle = NULL;
    
    if(data_info == NULL){
        QM_SPEC_LOGE(LOG_TAG, "pack data info is null");
        return -QM_EINVAL;
    }

    data_handle = qm_spec_data_handle_get(data_type, msg_type);
    if(data_handle == NULL){
        QM_SPEC_LOGE(LOG_TAG, "no pack handler (data_type %d msg_type %d)", data_type, msg_type);
        return -QM_EINVAL;
    }


    if(data_handle->pack == NULL){
        QM_SPEC_LOGE(LOG_TAG, "pack handler is null (data_type %d msg_type %d)", data_type, msg_type);
        return -QM_EINVAL;
    }

    QM_SPEC_LOGD(LOG_TAG, QM_SPEC_LOG_UPLINK, "pack data_type %d msg_type %d", data_type, msg_type);
    return data_handle->pack(data_info);
}

int qm_spec_data_destroy(qm_spec_data_type_t data_type, qm_spec_msg_type_t msg_type, qm_spec_data_info_t *data_info)
{
    qm_spec_core_data_handle_t *data_handle = NULL;
    
    if(data_info == NULL){
        QM_SPEC_LOGE(LOG_TAG, "destroy data info is null");
        return -QM_EINVAL;
    }

    data_handle = qm_spec_data_handle_get(data_type, msg_type);
    if(data_handle == NULL){
        QM_SPEC_LOGE(LOG_TAG, "no destroy handler (data_type %d msg_type %d)", data_type, msg_type);
        return -QM_EINVAL;
    }


    if(data_handle->destroy == NULL){
        QM_SPEC_LOGE(LOG_TAG, "destroy handler is null (data_type %d msg_type %d)", data_type, msg_type);
        return -QM_EINVAL;
    }

    return data_handle->destroy(data_info);
}

int qm_spec_standard_service_unpack(qm_spec_data_type_t data_type, qm_spec_property_operation_t *property_operation, uint8_t *msg, int len)
{
    qm_spec_data_info_t data_info = {
        .operation.property_operation = property_operation,
    };
    return qm_spec_data_unpack(data_type, QM_SPEC_MSG_TYPE_SERVICE, msg, len,  &data_info);
}
