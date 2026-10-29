#include "qm.h"
#include "qm_log.h"
#include "qm_kv.h"
#include "qm_utils_string.h"
#include "harmony_ble_hal.h"
#include "harmony_ble_comm.h"
#include "harmony_ble_core.h"
#include "harmony_ble_data.h"
#include "harmony_ble_cipher.h"

#include "qm_ble_gap.h"

#include "cJSON.h"

#define LOG_TAG "harmony"

#define STR_DATA_MAX_LEN    (32)

static int harmony_data_netcfgver_handle(harmony_ble_data_info_t *data_info);
static int harmony_data_netcfgver_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info);
static int harmony_data_netcfgver_transmit(harmony_ble_data_info_t *data_info);
static int harmony_data_netcfgver_destroy(harmony_ble_data_info_t *data_info);

static int harmony_data_deviceInfo_handle(harmony_ble_data_info_t *data_info);
static int harmony_data_deviceInfo_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info);
static int harmony_data_deviceInfo_transmit(harmony_ble_data_info_t *data_info);
static int harmony_data_deviceInfo_destroy(harmony_ble_data_info_t *data_info);

static int harmony_data_authsetup_handle(harmony_ble_data_info_t *data_info);
static int harmony_data_authsetup_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info);
static int harmony_data_authsetup_transmit(harmony_ble_data_info_t *data_info);
static int harmony_data_authsetup_destroy(harmony_ble_data_info_t *data_info);

static int harmony_data_creatsession_handle(harmony_ble_data_info_t *data_info);
static int harmony_data_creatsession_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info);
static int harmony_data_creatsession_transmit(harmony_ble_data_info_t *data_info);
static int harmony_data_creatsession_destroy(harmony_ble_data_info_t *data_info);

static int harmony_data_customsecdata_handle(harmony_ble_data_info_t *data_info);
static int harmony_data_customsecdata_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info);
static int harmony_data_customsecdata_transmit(harmony_ble_data_info_t *data_info);
static int harmony_data_customsecdata_destroy(harmony_ble_data_info_t *data_info);

typedef struct 
{
    char *service_name;
    harmony_ble_service_type_t msg_type;
    int (*handle)(harmony_ble_data_info_t *data_info);
    int (*unpack)(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info);
    int (*pack_transmit)(harmony_ble_data_info_t *data_info);
    int (*destroy)(harmony_ble_data_info_t *data_info);
}harmony_ble_json_data_handle_t;

typedef struct 
{
    harmony_ble_comm_param_t *param;
    harmony_ble_data_callback_t data_cb;
}harmony_ble_json_data_ctx_t;

static harmony_ble_json_data_ctx_t g_harmony_ble_ctx = {0};

static harmony_ble_json_data_handle_t  g_harmony_json_data_handle[] = {
    {HARMONY_SERVICE_NETCFGVER, HARMONY_BLE_SERVICE_TYPE_NETCFGVER, harmony_data_netcfgver_handle, harmony_data_netcfgver_unpack, harmony_data_netcfgver_transmit, harmony_data_netcfgver_destroy},
    {HARMONY_SERVICE_DEVICEINFO, HARMONY_BLE_SERVICE_TYPE_DEVICEINFO, harmony_data_deviceInfo_handle, harmony_data_deviceInfo_unpack, harmony_data_deviceInfo_transmit, harmony_data_deviceInfo_destroy},
    {HARMONY_SERVICE_AUTHSETUP, HARMONY_BLE_SERVICE_TYPE_AUTHSETUP, harmony_data_authsetup_handle, harmony_data_authsetup_unpack, harmony_data_authsetup_transmit, harmony_data_authsetup_destroy},
    {HARMONY_SERVICE_CREATSESS, HARMONY_BLE_SERVICE_TYPE_CREATSESS, harmony_data_creatsession_handle, harmony_data_creatsession_unpack, harmony_data_creatsession_transmit, harmony_data_creatsession_destroy},
    {HARMONY_SERVICE_CUSTOMSECDATA, HARMONY_BLE_SERVICE_TYPE_CUSTOMSECDATA, harmony_data_customsecdata_handle, harmony_data_customsecdata_unpack, harmony_data_customsecdata_transmit, harmony_data_customsecdata_destroy},
};

static char *cjson_str_get(cJSON *cjson, char *key)
{
    cJSON *tmp = NULL;
    tmp = cJSON_GetObjectItem(cjson, key);
    if(tmp == NULL){
        return NULL;
    }
    return tmp->valuestring;
}

static int cjson_int_get(cJSON *cjson, char *key, int *data)
{
    cJSON *tmp = NULL;
    tmp = cJSON_GetObjectItem(cjson, key);
    if(tmp == NULL){
        return -QM_EINVAL;
    }

    *data = tmp->valueint;
    return QM_EOK;
}

static char *cjson_name_get(cJSON *cjson)
{
    return cjson->string;
}

static int cjson_type_get(cJSON *cjson)
{
    return cjson->type;
}

static harmony_ble_json_data_handle_t *harmony_ble_data_handle_get(harmony_ble_service_type_t msg_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_harmony_json_data_handle); i++){
        if(msg_type == g_harmony_json_data_handle[i].msg_type){
            return &g_harmony_json_data_handle[i];
        }
    }
    return NULL;
}

int harmony_ble_data_handle_register(harmony_ble_data_callback_t data_cb, harmony_ble_comm_param_t *param)
{
    g_harmony_ble_ctx.param = param;
    g_harmony_ble_ctx.data_cb = data_cb;
    return QM_EOK;
}

harmony_ble_service_type_t harmony_ble_service_type_get(char *service_name, int len)
{
    int i = 0;
    for ( i = 0; i < QM_ARRAY_SIZE(g_harmony_json_data_handle); i++)
    {
        if(!strncmp(service_name, g_harmony_json_data_handle[i].service_name, len)){
            return g_harmony_json_data_handle[i].msg_type;
        }
    }
    return HARMONY_BLE_SERVICE_TYPE_NONE;
}


int harmony_ble_data_unpack(harmony_ble_service_type_t msg_type, 
                            harmony_ble_trans_info_t *rx_info, 
                            harmony_ble_data_info_t *data_info)
{
    harmony_ble_json_data_handle_t *data_handle = NULL;
    
    QM_RETURN_ON_FALSE(rx_info, -QM_EINVAL, LOG_TAG, "rx_info inval");
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_handle = harmony_ble_data_handle_get(msg_type);
    if(data_handle == NULL){
        return -QM_EINVAL;
    }

    if(data_handle->unpack == NULL){
        return -QM_EINVAL;
    }

    data_info->service_type = msg_type;

    return data_handle->unpack(rx_info, data_info);
}

int harmony_ble_data_handle(harmony_ble_data_info_t *data_info)
{
    harmony_ble_json_data_handle_t *data_handle = NULL;

    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_handle = harmony_ble_data_handle_get(data_info->service_type);
    if(data_handle == NULL){
        return -QM_EINVAL;
    }

    if(data_handle->unpack == NULL){
        return -QM_EINVAL;
    }

    return data_handle->handle(data_info);
}

int harmony_ble_data_pack_transmit(harmony_ble_data_info_t *data_info)
{
    harmony_ble_json_data_handle_t *data_handle = NULL;

    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_handle = harmony_ble_data_handle_get(data_info->service_type);
    if(data_handle == NULL){
        return -QM_EINVAL;
    }

    if(data_handle->unpack == NULL){
        return -QM_EINVAL;
    }

    return data_handle->pack_transmit(data_info);
}

int harmony_ble_data_destroy(harmony_ble_data_info_t *data_info)
{
    harmony_ble_json_data_handle_t *data_handle = NULL;

    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_handle = harmony_ble_data_handle_get(data_info->service_type);
    if(data_handle == NULL){
        return -QM_EINVAL;
    }

    if(data_handle->unpack == NULL){
        return -QM_EINVAL;
    }

    return data_handle->destroy(data_info);
}

static int harmony_data_netcfgver_handle(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(g_harmony_ble_ctx.data_cb){
        g_harmony_ble_ctx.data_cb(data_info->service_type, &data_info->data.netcfgver_info);
    }

    return QM_EOK;
}

static int harmony_data_netcfgver_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(rx_info, -QM_EINVAL, LOG_TAG, "rx_info inval");
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_info->msg_id = rx_info->msg_id;

    return QM_EOK;
}

static int harmony_data_netcfgver_transmit(harmony_ble_data_info_t *data_info)
{
    cJSON *root = NULL;
    harmony_ble_trans_info_t tx_info = {0};
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");
    
    tx_info.msg_id = data_info->msg_id;
    tx_info.cmd = HARMONY_BLE_CMD_RESPONSE;
    tx_info.data_type = HARMONY_BLE_DATA_TYPE_JSON;
    tx_info.data.json_data.service_name = HARMONY_SERVICE_NETCFGVER;
    tx_info.data.json_data.service_length = strlen(HARMONY_SERVICE_NETCFGVER);


    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM; 
    }

    cJSON_AddNumberToObject(root, HARMONY_BODY_NAME_VER, HARMONY_BLE_COMM_VERSION);

    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        cJSON_Delete(root);
        return -QM_ENOMEM;    
    }
    data_info->up_data_len = strlen(data_info->up_data);

    tx_info.data.json_data.body_data = data_info->up_data;
    tx_info.data.json_data.body_length = data_info->up_data_len;

    cJSON_Delete(root);
    QM_LOGD(LOG_TAG, "data netCfgVer reply: %.*s...", data_info->up_data_len, data_info->up_data);

    harmony_ble_transport_tx(&tx_info);
    
    return QM_EOK;
}

static int harmony_data_netcfgver_destroy(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(data_info->up_data){   
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }

    return QM_EOK;
}

static int harmony_data_deviceInfo_handle(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(g_harmony_ble_ctx.data_cb){
        g_harmony_ble_ctx.data_cb(data_info->service_type, &data_info->data.device_info);
    }

    return QM_EOK;
}

static int harmony_data_deviceInfo_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(rx_info, -QM_EINVAL, LOG_TAG, "rx_info inval");
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_info->msg_id = rx_info->msg_id;

    return QM_EOK;
}

static int harmony_data_deviceInfo_transmit(harmony_ble_data_info_t *data_info)
{
    cJSON *root = NULL;
    cJSON *vendor_object = NULL;
    cJSON *info_object = NULL;
    uint8_t mac[6] = {0};
    char str_mac[20] = {0};
    harmony_ble_trans_info_t tx_info = {0};
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");
    
    tx_info.msg_id = data_info->msg_id;
    tx_info.cmd = HARMONY_BLE_CMD_RESPONSE;
    tx_info.data_type = HARMONY_BLE_DATA_TYPE_JSON;
    tx_info.data.json_data.service_name = HARMONY_SERVICE_DEVICEINFO;
    tx_info.data.json_data.service_length = strlen(HARMONY_SERVICE_DEVICEINFO);

    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM; 
    }

    cJSON_AddStringToObject(root, HARMONY_BODY_NAME_PRODUCTID, data_info->data.device_info.productid);
    cJSON_AddStringToObject(root, HARMONY_BODY_NAME_SN, data_info->data.device_info.sn);

    vendor_object = cJSON_AddObjectToObject(root, HARMONY_BODY_KET_VENDOR);
    if(vendor_object == NULL){
        cJSON_Delete(root);
        return -QM_ENOMEM; 
    }
    cJSON_AddStringToObject(vendor_object, HARMONY_BODY_NAME_DEVID, "");

    info_object = cJSON_AddObjectToObject(vendor_object, HARMONY_SERVICE_DEVICEINFO);
    if(info_object == NULL){
        cJSON_Delete(root);
        return -QM_ENOMEM; 
    }
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_SN, data_info->data.device_info.sn);
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_MODEL, data_info->data.device_info.model);
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_MANU, data_info->data.device_info.manu);
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_PRODID, data_info->data.device_info.productid);

    qm_ble_gap_get_local_addr(mac);
    qm_snprintf(str_mac, 20, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_MAC, str_mac);
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_BLEMAC, str_mac);
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_FWR, data_info->data.device_info.fw_ver);
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_HWR, data_info->data.device_info.hw_ver);
    cJSON_AddStringToObject(info_object, HARMONY_BODY_NAME_PROTT, HARMONY_BLE_COMM_PROT_T);


    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        cJSON_Delete(root);
        return -QM_ENOMEM;    
    }
    data_info->up_data_len = strlen(data_info->up_data);

    tx_info.data.json_data.body_data = data_info->up_data;
    tx_info.data.json_data.body_length = data_info->up_data_len;

    cJSON_Delete(root);

    QM_LOGD(LOG_TAG, "data deviceInfo reply: %.*s...", data_info->up_data_len, data_info->up_data);

    harmony_ble_transport_tx(&tx_info);
    
    return QM_EOK;
}

static int harmony_data_deviceInfo_destroy(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }

    return QM_EOK;
}

static int harmony_data_authsetup_handle(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");
    if(g_harmony_ble_ctx.data_cb){
        g_harmony_ble_ctx.data_cb(data_info->service_type, NULL);
    }
    return QM_EOK;
}

static int harmony_data_authsetup_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info)
{
    char *str = NULL;
    cJSON *root = NULL;
    uint8_t hex_authcode[16] = {0};
    uint8_t hex_authcodeId[16] = {0};

    QM_RETURN_ON_FALSE(rx_info, -QM_EINVAL, LOG_TAG, "rx_info inval");
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_info->msg_id = rx_info->msg_id;

    root = cJSON_Parse((const char*)rx_info->data.json_data.body_data);
    if(root == NULL){
        return -QM_EINVAL;
    }

    str = cjson_str_get(root, HARMONY_BODY_NAME_AUTHCODE);
    hex_str_to_nums(str, strlen(str),hex_authcode);

    str = cjson_str_get(root, HARMONY_BODY_NAME_AUTHCODEID);
    hex_str_to_nums(str, strlen(str),hex_authcodeId);
      
    harmony_ble_core_store_set(1, hex_authcode, hex_authcodeId);

    cJSON_Delete(root);

    return QM_EOK;
}

static int harmony_data_authsetup_transmit(harmony_ble_data_info_t *data_info)
{
    cJSON *root = NULL;
    char str_code[4] = {0};
    harmony_ble_trans_info_t tx_info = {0};
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");
    
    tx_info.msg_id = data_info->msg_id;
    tx_info.cmd = HARMONY_BLE_CMD_RESPONSE;
    tx_info.data_type = HARMONY_BLE_DATA_TYPE_JSON;
    tx_info.data.json_data.service_name = HARMONY_SERVICE_AUTHSETUP;
    tx_info.data.json_data.service_length = strlen(HARMONY_SERVICE_AUTHSETUP);

    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM; 
    }

    qm_snprintf(str_code, 4, "%d", HARMONY_BLE_EOK);
    cJSON_AddStringToObject(root, HARMONY_BODY_ERRCODE, str_code);

    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        cJSON_Delete(root);
        return -QM_ENOMEM;    
    }
    data_info->up_data_len = strlen(data_info->up_data);

    tx_info.data.json_data.body_data = data_info->up_data;
    tx_info.data.json_data.body_length = data_info->up_data_len;

    cJSON_Delete(root);

    QM_LOGD(LOG_TAG, "data authSetup reply: %.*s...", data_info->up_data_len, data_info->up_data);

    harmony_ble_transport_tx(&tx_info);
    
    if(g_harmony_ble_ctx.param->event_notify){
        g_harmony_ble_ctx.param->event_notify(HARMONY_BLE_EVENT_DEVICE_REGISTERED, NULL);
    }

    return QM_EOK;
}

static int harmony_data_authsetup_destroy(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }

    return QM_EOK;
}

static int harmony_data_creatsession_handle(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(g_harmony_ble_ctx.data_cb){
        g_harmony_ble_ctx.data_cb(data_info->service_type, &data_info->data.session_info);
    }

    return QM_EOK;
}

static int harmony_data_creatsession_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info)
{
    int ret = QM_EOK;
    char *str = NULL;
    cJSON *root = NULL;
    harmony_ble_session_info_t *session_info = &data_info->data.session_info;

    QM_RETURN_ON_FALSE(rx_info, -QM_EINVAL, LOG_TAG, "rx_info inval");
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_info->msg_id = rx_info->msg_id;

    root = cJSON_Parse((const char*)rx_info->data.json_data.body_data);
    if(root == NULL){
        return -QM_EINVAL;
    }

    ret = cjson_int_get(root, HARMONY_BODY_NAME_SEQ, &session_info->seq);
    if(ret != QM_EOK){
        ret = -QM_ERROR;
        goto __exit;
    }

    str = cjson_str_get(root, HARMONY_BODY_NAME_SN1);
    if(str == NULL){
        ret = -QM_ERROR;
        goto __exit;

    }
    hex_str_to_nums(str, strlen(str), session_info->sn1);

    str = cjson_str_get(root, HARMONY_BODY_NAME_UUID);
    if(str == NULL){
        ret = -QM_ERROR;
        goto __exit;
    }
    memcpy(session_info->uuid, str, strlen(str));

__exit:
    cJSON_Delete(root);
    return ret;
}

static int harmony_data_creatsession_transmit(harmony_ble_data_info_t *data_info)
{
    cJSON *root = NULL;
    char str_data[64 + 1] = {0};
    harmony_ble_trans_info_t tx_info = {0};
    harmony_ble_session_info_t *session_info = &data_info->data.session_info;

    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");
    
    tx_info.msg_id = data_info->msg_id;
    tx_info.cmd = HARMONY_BLE_CMD_RESPONSE;
    tx_info.data_type = HARMONY_BLE_DATA_TYPE_JSON;
    tx_info.data.json_data.service_name = HARMONY_SERVICE_CREATSESS;
    tx_info.data.json_data.service_length = strlen(HARMONY_SERVICE_CREATSESS);

    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM; 
    }

    cJSON_AddNumberToObject(root, HARMONY_BODY_NAME_SEQ, session_info->seq);
    cJSON_AddStringToObject(root, HARMONY_BODY_NAME_UUID, session_info->uuid);

    hex_to_strs(session_info->sessid, 32, str_data);
    cJSON_AddStringToObject(root, HARMONY_BODY_NAME_SESSID, str_data);

    memset(str_data, 0, 64 + 1);
    hex_to_strs(session_info->sn2, 8, str_data);    
    cJSON_AddStringToObject(root, HARMONY_BODY_NAME_SN2, str_data);

    memset(str_data, 0, 64 + 1);
    hex_to_strs(session_info->authcodeid, 16, str_data);    
    cJSON_AddStringToObject(root, HARMONY_BODY_NAME_AUTHCODEID, str_data);

    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        cJSON_Delete(root);
        return -QM_ENOMEM;    
    }
    data_info->up_data_len = strlen(data_info->up_data);

    tx_info.data.json_data.body_data = data_info->up_data;
    tx_info.data.json_data.body_length = data_info->up_data_len;

    cJSON_Delete(root);

    QM_LOGD(LOG_TAG, "data creatSession reply: %.*s...", data_info->up_data_len, data_info->up_data);

    harmony_ble_transport_tx(&tx_info);


    if(g_harmony_ble_ctx.param->event_notify){
        g_harmony_ble_ctx.param->event_notify(HARMONY_BLE_EVENT_CONNECT_APP, NULL);
    }

    return QM_EOK;
}

static int harmony_data_creatsession_destroy(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }

    return QM_EOK;
}

static int harmony_data_customsecdata_handle(harmony_ble_data_info_t *data_info)
{
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(g_harmony_ble_ctx.data_cb){
        g_harmony_ble_ctx.data_cb(data_info->service_type, &data_info->data.custom_info);
    }

    return QM_EOK;
}

static int haromy_data_value_add(cJSON *object,harmony_ble_data_t *data)
{
    int type = cJSON_Invalid;
    cJSON *data_item = NULL;

    QM_RETURN_ON_FALSE(object, -QM_EINVAL, LOG_TAG, "object inval");
    QM_RETURN_ON_FALSE(data, -QM_EINVAL, LOG_TAG, "data inval");

    data_item = cJSON_GetObjectItem(object, data->data_id);
    if(data_item == NULL){
        return -QM_EINVAL;
    }

    type = cjson_type_get(data_item);
    QM_LOGD(LOG_TAG, "cjson type %d...", type);

    switch ((type & 0xFF))
    {
        case cJSON_False:
            harmony_ble_data_pack_bool(data, QM_FALSE);
            QM_LOGD(LOG_TAG, "HARMONY BLE data False...");
        break;
        
        case cJSON_True:
            harmony_ble_data_pack_bool(data, true);
            QM_LOGD(LOG_TAG, "HARMONY BLE data True...");
        break;

        case cJSON_Number:
            harmony_ble_data_pack_number(data, data_item->valueint);
            QM_LOGD(LOG_TAG, "HARMONY BLE data %d...", object->valueint);
        break;

        case cJSON_String:
            harmony_ble_data_pack_string(data, data_item->valuestring);
            QM_LOGD(LOG_TAG, "HARMONY BLE data %s...", data_item->valuestring);
        break;

        default:

        break;
    }
    return QM_EOK;
}

static int haromy_data_parse_create(cJSON *operation_object, harmony_ble_service_t *service)
{
    char *str = NULL;
    int size = 0;
    cJSON *params_data = NULL;
    harmony_ble_data_t *data = NULL;

    QM_RETURN_ON_FALSE(operation_object, -QM_EINVAL, LOG_TAG, "rx_info inval");
    QM_RETURN_ON_FALSE(service, -QM_EINVAL, LOG_TAG, "data info inval");

    size =  cJSON_GetArraySize(operation_object);
    if(size == 0){
        QM_LOGW(LOG_TAG, "data num get 0");
        goto __exit;
    }
    QM_LOGD(LOG_TAG, "data num %d", size);

    for (int i = 0; i < size; i++)
    {        
        params_data = cJSON_GetArrayItem(operation_object, i);
        if(params_data == NULL){
            break;
        }

        data = harmony_ble_data_creat();
        if(data == NULL){
            QM_LOGW(LOG_TAG, "data creat fail");
            break;
        }

        str = cjson_name_get(params_data);
        if(str == NULL){
            break;
        }
        QM_LOGE(LOG_TAG, "data name: %s", str);
        harmony_ble_data_id_set(data, str);
        haromy_data_value_add(operation_object, data);
        harmony_ble_data_add(service, data);
    }

__exit:
    return QM_EOK;
}

static int harmony_data_custom_put_unpack(cJSON *params_object,harmony_ble_custom_info_t *custom_info)
{
    int ret = QM_EOK;
    char *str = NULL;
    cJSON *params_data = NULL;
    harmony_ble_service_t *service = NULL;
    
    service = harmony_ble_service_creat();
    if(service == NULL){
        QM_LOGE(LOG_TAG, "services creat NULL"); 
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cjson_str_get(params_object, HARMONY_BODY_NAME_SID);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "str NULL");
        ret = -QM_EINVAL;
        goto __exit;
    }
    harmony_ble_service_id_set(service, str);

    params_data = cJSON_GetObjectItem(params_object, HARMONY_BODY_NAME_DATA);
    if(params_data == NULL){
        QM_LOGE(LOG_TAG, "json data key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }
    haromy_data_parse_create(params_data, service);
    harmony_ble_service_add(&custom_info->profile, service);
    
    
__exit:
    return ret;
}
#if 0
static int harmony_data_custom_get_unpack(cJSON *params_object,harmony_ble_custom_info_t *custom_info)
{
    int ret = QM_EOK;
    char *str = NULL;
    cJSON *params_data = NULL;
    harmony_ble_service_t *service = NULL;

    str = cjson_str_get(params_object, HARMONY_BODY_NAME_SID);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "str NULL");
        ret = -QM_EINVAL;
        goto __exit;
    }

    if(!strncmp(HARMONY_BODY_NAME_ALLSERVICE, str, strlen(str))){
        QM_LOGD(LOG_TAG, "get all services info");
        ret = QM_EOK;
        custom_info->get_allservice = QM_TRUE;
        goto __exit;
    }

    service = harmony_ble_service_creat();
    if(service == NULL){
        QM_LOGE(LOG_TAG, "services creat NULL"); 
        ret = -QM_EINVAL;
        goto __exit;
    }
    harmony_ble_service_id_set(service, str);
    harmony_ble_service_add(&custom_info->profile, service);

__exit:
    return ret;
}
#endif
static int harmony_data_customsecdata_unpack(harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info)
{
    int ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    harmony_ble_custom_info_t *custom_info = &data_info->data.custom_info;

    QM_RETURN_ON_FALSE(rx_info, -QM_EINVAL, LOG_TAG, "rx_info inval");
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    data_info->msg_id = rx_info->msg_id;
    root = cJSON_Parse((const char*)rx_info->data.json_data.body_data);
    if(root == NULL){
        return -QM_EINVAL;
    }

    ret = cjson_int_get(root, HARMONY_BODY_NAME_SEQ, &custom_info->seq);
    if(ret != QM_EOK){
        ret = -QM_ERROR;
        goto __exit;
    }

    params_object = cJSON_GetObjectItem(root, HARMONY_BODY_KET_VENDOR);
    if(params_object == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    custom_info->opt = rx_info->opt;

    switch (rx_info->opt)
    {
        case HARMONY_BLE_OPT_TYPE_PUT:
            harmony_data_custom_put_unpack(params_object, custom_info);
        break;
    #if 0
        case HARMONY_BLE_OPT_TYPE_GET:
            harmony_data_custom_get_unpack(params_object, custom_info);
        break;
    #endif
        default:
        break;
    }

__exit:
    cJSON_Delete(root);
    return ret;
}

static int harmony_data_custom_put_pack_link(harmony_ble_trans_info_t *tx_info, harmony_ble_data_info_t *data_info)
{
    int ret = QM_EOK;
    cJSON *root = NULL;
    harmony_ble_custom_info_t *custom_info = &data_info->data.custom_info;
    
    QM_RETURN_ON_FALSE(tx_info, -QM_EINVAL, LOG_TAG, "root inval");
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "custom_info inval");

    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM; 
    }
    cJSON_AddNumberToObject(root, HARMONY_BODY_ERRCODE, custom_info->error_code);

    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        cJSON_Delete(root);
        return -QM_ENOMEM;    
    }
    data_info->up_data_len = strlen(data_info->up_data);

    tx_info->data.json_data.body_data = data_info->up_data;
    tx_info->data.json_data.body_length = data_info->up_data_len;

    cJSON_Delete(root);

    QM_LOGD(LOG_TAG, "data customSecData reply: %.*s...", data_info->up_data_len, data_info->up_data);
    harmony_ble_transport_encrypt_tx(tx_info, 
                                    custom_info->productid,
                                    strlen(custom_info->productid),
                                    custom_info->iv,
                                    custom_info->sesskey,
                                    custom_info->sessid,
                                    custom_info->hmac);

    if(g_harmony_ble_ctx.param->put){
        g_harmony_ble_ctx.param->put(&custom_info->profile);
    }

    return ret;
}

#if 0
static int harmony_data_custom_get_pack_link(harmony_ble_trans_info_t *tx_info, harmony_ble_data_info_t *data_info)
{
    int ret = QM_EOK;
    harmony_ble_custom_info_t *custom_info = &data_info->data.custom_info;

    QM_RETURN_ON_FALSE(tx_info, -QM_EINVAL, LOG_TAG, "root inval");
    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "custom_info inval");
    
    if(g_harmony_ble_ctx.param->get){
        ret = g_harmony_ble_ctx.param->get(custom_info->get_allservice, &custom_info->profile);
    }

    return ret;
}
#endif

static int harmony_data_customsecdata_transmit(harmony_ble_data_info_t *data_info)
{
    harmony_ble_trans_info_t tx_info = {0};
    harmony_ble_custom_info_t *custom_info = &data_info->data.custom_info;

    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");
    		
    tx_info.encrypted = HARMONY_BLE_ENCY_SESSION; //Session Key加密	
    tx_info.msg_id = data_info->msg_id;
    tx_info.cmd = HARMONY_BLE_CMD_RESPONSE;
    tx_info.data_type = HARMONY_BLE_DATA_TYPE_JSON;
    tx_info.data.json_data.service_name = HARMONY_SERVICE_CUSTOMSECDATA;
    tx_info.data.json_data.service_length = strlen(HARMONY_SERVICE_CUSTOMSECDATA);

    switch (custom_info->opt)
    {
        case HARMONY_BLE_OPT_TYPE_PUT:
            harmony_data_custom_put_pack_link(&tx_info, data_info);
        break;
    #if 0 
        case HARMONY_BLE_OPT_TYPE_GET:
            harmony_data_custom_get_pack_link(&tx_info, data_info);
        break;
    #endif
        default:
        break;
    }

    return QM_EOK;
}

static int harmony_data_customsecdata_destroy(harmony_ble_data_info_t *data_info)
{
    harmony_ble_service_t *service = NULL;
    harmony_ble_service_t *tmp_service = NULL;
    harmony_ble_custom_info_t *custom_info = &data_info->data.custom_info;

    QM_RETURN_ON_FALSE(data_info, -QM_EINVAL, LOG_TAG, "data info inval");

    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }

    service = harmony_ble_service_next(&custom_info->profile, NULL);
    while (service)
    {
        tmp_service = service;
        service = harmony_ble_service_next(&custom_info->profile, service);
        harmony_ble_service_delete(tmp_service);
    }
    

    return QM_EOK;
}