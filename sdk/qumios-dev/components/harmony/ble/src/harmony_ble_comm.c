#include "qm.h"
#include "qm_log.h"

#include "qm_ble_gap.h"

#include "harmony_ble_comm.h"
#include "harmony_ble_core.h"
#include "harmony_ble_ota.h"
#include "harmony_ble_data.h"
#include "harmony_ble_trans.h"
#include "harmony_ble_cipher.h"


#include "cJSON.h"

#define LOG_TAG "harmony comm"

#define TEMP_BUFF_LEN   (64)

typedef struct 
{
    char str_mac[13];
    uint8_t salt[16];
    int registered;
    uint8_t authcode[16];
    uint8_t authcodeid[16];
    uint8_t session_id[HARMONY_BLE_CIPHER_SESSID_LEN];
    uint8_t hmac_key[HARMONY_BLE_CIPHER_HMACKEY_LEN];
    uint8_t session_key[HARMONY_BLE_CIPHER_SESSIONKEY_LEN];
    harmony_ble_comm_param_t param;
    uint32_t report_id;   
}harmony_ble_comm_ctx_t;

static harmony_ble_comm_ctx_t  g_harmony_comm_ctx = {0};

static void random_num_get(uint8_t *data, int len)
{
    for (int i = 0; i < len; i++)
    {
        data[i] = (qm_random_get(256) & 0xFF);
    }
}


static void harmony_ble_decrypt_body_data(uint8_t *encrypt_data, uint16_t *length)
{
    uint8_t *p_str_data = NULL;
    uint8_t str_sta_data[TEMP_BUFF_LEN + 1] = {0};
    int data_len = *length - HARMONY_BLE_CIPHER_IV_LEN - HARMONY_BLE_CIPHER_TAG_LEN - HARMONY_BLE_CIPHER_SESSID_LEN;

    if (data_len > TEMP_BUFF_LEN){
        p_str_data = (uint8_t *)qm_malloc(data_len);
        memset(p_str_data, 0, data_len);
    }else{
        p_str_data = str_sta_data;
    }
    
    harmony_ble_cipher_info_t cipher_info = {0};
    cipher_info.iv = &encrypt_data[0]; // iv length 12 bytes, index : 0
    QM_HEX_LOGD(LOG_TAG, "iv body: ", cipher_info.iv, HARMONY_BLE_CIPHER_IV_LEN);

    cipher_info.in_data = &encrypt_data[HARMONY_BLE_CIPHER_IV_LEN];
    QM_HEX_LOGD(LOG_TAG, "in_data body: ", cipher_info.in_data, data_len);

    cipher_info.data_len = data_len;
    cipher_info.tag = &encrypt_data[HARMONY_BLE_CIPHER_IV_LEN + data_len];

    QM_HEX_LOGD(LOG_TAG, "tag body: ", cipher_info.tag, HARMONY_BLE_CIPHER_TAG_LEN);

    cipher_info.session_key = g_harmony_comm_ctx.session_key;
    QM_HEX_LOGD(LOG_TAG, "session_key body: ", cipher_info.session_key, HARMONY_BLE_CIPHER_SESSIONKEY_LEN);

    cipher_info.prod_id = (uint8_t *)g_harmony_comm_ctx.param.info.productid;
    cipher_info.prod_length = strlen(g_harmony_comm_ctx.param.info.productid);
    QM_HEX_LOGD(LOG_TAG, "prod_id body: ", cipher_info.prod_id, cipher_info.prod_length);

    cipher_info.out_data = p_str_data;
    harmony_ble_cipher_decrypt_data(&cipher_info);

    memset(encrypt_data, 0, *length);
    memcpy(encrypt_data, p_str_data, data_len);
    *length = data_len;

    if(data_len > TEMP_BUFF_LEN){
        qm_free(p_str_data);
        p_str_data = NULL;
    }
}

static void harmony_ble_trans_rx_callback(harmony_ble_trans_info_t *rx_info)
{
    harmony_ble_data_info_t data_info = {0};
    harmony_ble_service_type_t msg_type = HARMONY_BLE_SERVICE_TYPE_NONE;
    QM_LOGD(LOG_TAG, "ble service: %.*s",  rx_info->data.json_data.service_length,  rx_info->data.json_data.service_name);

    msg_type =  harmony_ble_service_type_get(rx_info->data.json_data.service_name, rx_info->data.json_data.service_length);
    if(msg_type <= HARMONY_BLE_SERVICE_TYPE_NONE || msg_type >= HARMONY_BLE_SERVICE_TYPE_MAX){
        return;
    }

    if(rx_info->encrypted){
        harmony_ble_decrypt_body_data((uint8_t *)rx_info->data.json_data.body_data, &rx_info->data.json_data.body_length);
    }

    QM_LOGD(LOG_TAG, "Ble body: %.*s", rx_info->data.json_data.body_length, rx_info->data.json_data.body_data);


    harmony_ble_data_unpack(msg_type, rx_info, &data_info);

    harmony_ble_data_handle(&data_info);

    harmony_ble_data_pack_transmit(&data_info);

    harmony_ble_data_destroy(&data_info);
}

static int harmony_ble_comm_device_info_handler(void *arg)
{
    harmony_ble_comm_param_t *comm_param = &g_harmony_comm_ctx.param;
    harmony_ble_device_info_t *info = (harmony_ble_device_info_t *)arg;
    QM_RETURN_ON_FALSE(arg, -QM_EINVAL, LOG_TAG, "arg inval");

    uint8_t xj_mac[6] = {0};
    qm_ble_gap_get_local_addr(xj_mac);
    qm_snprintf(g_harmony_comm_ctx.str_mac, 13, "%02X%02X%02X%02X%02X%02X", xj_mac[0], xj_mac[1], xj_mac[2], xj_mac[3], xj_mac[4], xj_mac[5]);

    info->sn = g_harmony_comm_ctx.str_mac;
    info->model = comm_param->info.model;
    info->manu = comm_param->info.manu;
    info->productid = comm_param->info.productid;
    info->fw_ver = comm_param->info.fw_ver;
    info->hw_ver = comm_param->info.hw_ver;

    return QM_EOK;
}

static int harmony_ble_comm_create_session_info_handler(void *arg)
{
    harmony_ble_comm_param_t *param = &g_harmony_comm_ctx.param;
    harmony_ble_session_info_t *info = (harmony_ble_session_info_t *)arg;
    QM_RETURN_ON_FALSE(arg, -QM_EINVAL, LOG_TAG, "arg inval");

    random_num_get(info->sn2, sizeof(info->sn2));
    random_num_get(info->sessid, sizeof(info->sessid));
    memcpy(info->authcodeid, g_harmony_comm_ctx.authcodeid, 16); 

    memcpy(g_harmony_comm_ctx.salt, info->sn1, 8);
    memcpy(g_harmony_comm_ctx.salt + 8, info->sn2, 8);
    memcpy(g_harmony_comm_ctx.session_id, info->sessid, sizeof(info->sessid));

    harmony_ble_cipher_flush(g_harmony_comm_ctx.salt, g_harmony_comm_ctx.authcode, 
                            g_harmony_comm_ctx.session_key, g_harmony_comm_ctx.hmac_key);

    QM_HEX_LOGD(LOG_TAG, "salt body: ", g_harmony_comm_ctx.salt, 16);
    QM_HEX_LOGD(LOG_TAG, "hmac_key body: ", g_harmony_comm_ctx.hmac_key, 32);
    QM_HEX_LOGD(LOG_TAG, "session_key body: ", g_harmony_comm_ctx.session_key, 16);

    return QM_EOK;
}

static int harmony_ble_comm_customsecdata_info_handler(void *arg)
{
    harmony_ble_custom_info_t *info = (harmony_ble_custom_info_t *)arg;
    QM_RETURN_ON_FALSE(arg, -QM_EINVAL, LOG_TAG, "arg inval");

    random_num_get(info->iv, HARMONY_BLE_CIPHER_IV_LEN);

    QM_HEX_LOGD(LOG_TAG, "iv body: ", info->iv, HARMONY_BLE_CIPHER_IV_LEN);
    QM_HEX_LOGD(LOG_TAG, "session_id body: ", g_harmony_comm_ctx.session_id, HARMONY_BLE_CIPHER_SESSID_LEN);
    QM_HEX_LOGD(LOG_TAG, "hmac_key body: ", g_harmony_comm_ctx.hmac_key, HARMONY_BLE_CIPHER_HMACKEY_LEN);
    QM_HEX_LOGD(LOG_TAG, "productid body: ", g_harmony_comm_ctx.param.info.productid, strlen(g_harmony_comm_ctx.param.info.productid));

    info->sesskey = g_harmony_comm_ctx.session_key;
    info->sessid = g_harmony_comm_ctx.session_id;
    info->hmac = g_harmony_comm_ctx.hmac_key;
    info->productid = g_harmony_comm_ctx.param.info.productid;
    
    return QM_EOK;
}

static int harmony_ble_comm_authup_info_handler(void *arg)
{
    harmony_ble_comm_param_t *param = &g_harmony_comm_ctx.param;
    harmony_ble_core_store_get(&g_harmony_comm_ctx.registered, g_harmony_comm_ctx.authcode, g_harmony_comm_ctx.authcodeid);

    return QM_EOK;
}

static int harmony_ble_comm_netcfgver_info_handler(void *arg)
{
    if(g_harmony_comm_ctx.param.event_notify && !g_harmony_comm_ctx.registered){
        g_harmony_comm_ctx.param.event_notify(HARMONY_BLE_EVENT_PAIRING, NULL);
    }
    return QM_EOK;
}

static int harmony_ble_data_callback(harmony_ble_service_type_t msg_type, void *arg)
{   
    switch (msg_type)
    {
        case HARMONY_BLE_SERVICE_TYPE_NETCFGVER:
            harmony_ble_comm_netcfgver_info_handler(arg);
        break;

        case HARMONY_BLE_SERVICE_TYPE_DEVICEINFO:
            harmony_ble_comm_device_info_handler(arg);
        break;
        
        case HARMONY_BLE_SERVICE_TYPE_AUTHSETUP:
            harmony_ble_comm_authup_info_handler(arg);
        break;

        case HARMONY_BLE_SERVICE_TYPE_CREATSESS:
            harmony_ble_comm_create_session_info_handler(arg);
        break;

        case HARMONY_BLE_SERVICE_TYPE_CUSTOMSECDATA:
            harmony_ble_comm_customsecdata_info_handler(arg);
        break;

        default:
        break;
    }
    return QM_EOK;
}

int harmony_ble_comm_init(harmony_ble_comm_param_t *param)
{
    QM_RETURN_ON_FALSE(param, -QM_EINVAL, LOG_TAG, "param inval");

    memcpy(&g_harmony_comm_ctx.param, param, sizeof(harmony_ble_comm_param_t));

    harmony_ble_core_init(&g_harmony_comm_ctx.param);

    harmony_ble_trans_init(harmony_ble_trans_rx_callback);

    harmony_ble_ota_register(&g_harmony_comm_ctx.param);

    harmony_ble_data_handle_register(harmony_ble_data_callback, &g_harmony_comm_ctx.param);

    harmony_ble_core_store_get(&g_harmony_comm_ctx.registered, g_harmony_comm_ctx.authcode, g_harmony_comm_ctx.authcodeid);
    return QM_EOK;
}

static uint32_t get_report_seq(void) {
	g_harmony_comm_ctx.report_id += 1;
	if(g_harmony_comm_ctx.report_id == 0){
        g_harmony_comm_ctx.report_id = 1;
    }
	return g_harmony_comm_ctx.report_id;
}

static cJSON *data_item_parse_creat(harmony_ble_service_t *service)
{
    cJSON *item = NULL;
    cJSON *datat_item = NULL;
    harmony_ble_data_t *data = NULL;
    QM_RETURN_ON_FALSE(service, NULL, LOG_TAG, "service inval");
    QM_RETURN_ON_FALSE(service->count, NULL, LOG_TAG, "count inval");

    datat_item = cJSON_CreateObject();
    if(datat_item == NULL){
        return NULL;
    }

    data = harmony_ble_data_next(service, NULL);
    while (data)
    {

        switch (data->type)
        {
            case HARMONY_BLE_PROFILE_DATA_TYPE_BOOL:
            {
                bool_t bool_value = QM_FALSE;
                harmony_ble_data_unpack_bool(data, &bool_value);
                cJSON_AddBoolToObject(datat_item, data->data_id, (cJSON_bool)bool_value);
            }
            break;
            case HARMONY_BLE_PROFILE_DATA_TYPE_NUMBER:
            {
                int int_value = 0;
                harmony_ble_data_unpack_number(data, &int_value);
                cJSON_AddNumberToObject(datat_item, data->data_id, int_value);
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
                cJSON_AddStringToObject(datat_item, data->data_id, str);
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

    return datat_item;
}

int harmony_ble_comm_report(harmony_ble_profile_t *profile)
{
    int ret = QM_EOK;
    int connect_status = 0;
    cJSON *root = NULL;
    cJSON *data_item = NULL;
    cJSON *vendor_item = NULL;
    cJSON *service_item = NULL;
    harmony_ble_service_t *service = NULL;
    harmony_ble_trans_info_t tx_info = {0};

    uint8_t iv[HARMONY_BLE_CIPHER_IV_LEN] = {0};

    QM_RETURN_ON_FALSE(profile, -QM_EINVAL, LOG_TAG, "profile inval");

    connect_status =  harmony_ble_core_conn_status_get();
    if(!connect_status){
        harmony_ble_profile_delete(profile);
        return -QM_EINVAL;
    }


    tx_info.encrypted = HARMONY_BLE_ENCY_SESSION;
    tx_info.msg_id = 0;
    tx_info.cmd = HARMONY_BLE_CMD_REPORT;
    tx_info.data_type = HARMONY_BLE_DATA_TYPE_JSON;
    tx_info.data.json_data.service_name = HARMONY_SERVICE_CUSTOMSECDATA;
    tx_info.data.json_data.service_length = strlen(HARMONY_SERVICE_CUSTOMSECDATA);

    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM; 
    }
    cJSON_AddNumberToObject(root, HARMONY_BODY_NAME_SEQ, get_report_seq());

    if(profile->count > 1){
        vendor_item = cJSON_CreateArray(); 
    }
    
    service = harmony_ble_service_next(profile, NULL);
    while (service)
    {
        service_item = cJSON_CreateObject();
        if(service_item == NULL){
            break;
        }

        data_item = data_item_parse_creat(service);
        if(service_item == NULL){
            cJSON_Delete(service_item);
            break;
        }

        cJSON_AddStringToObject(service_item, HARMONY_BODY_NAME_SID, service->sid);
        cJSON_AddItemToObject(service_item, HARMONY_BODY_NAME_DATA, data_item);

        if(vendor_item){
            cJSON_AddItemToArray(vendor_item, service_item);
        }else{
            vendor_item = service_item;
        }
        
        service = harmony_ble_service_next(profile, service);
    }

    cJSON_AddItemToObject(root, HARMONY_BODY_KET_VENDOR,vendor_item);
    
    tx_info.data.json_data.body_data = cJSON_PrintUnformatted(root);
    if(tx_info.data.json_data.body_data == NULL){
        cJSON_Delete(root);
        return -QM_ENOMEM;    
    }
    tx_info.data.json_data.body_length = strlen(tx_info.data.json_data.body_data);

    harmony_ble_profile_delete(profile);

    cJSON_Delete(root);
    
    QM_LOGD(LOG_TAG, "data report reply: %.*s...", tx_info.data.json_data.body_length, tx_info.data.json_data.body_data);

    random_num_get(iv, HARMONY_BLE_CIPHER_IV_LEN);

    harmony_ble_transport_encrypt_tx(&tx_info, 
                                    g_harmony_comm_ctx.param.info.productid,
                                    strlen(g_harmony_comm_ctx.param.info.productid),
                                    iv,
                                    g_harmony_comm_ctx.session_key,
                                    g_harmony_comm_ctx.session_id,
                                    g_harmony_comm_ctx.hmac_key);  

    cJSON_free(tx_info.data.json_data.body_data);
    
    return ret;
}

int harmony_ble_comm_adv_start(uint8_t is_near)
{
    return harmony_ble_core_adv_start(is_near);
}

int harmony_ble_comm_adv_stop(void)
{
    return harmony_ble_core_adv_stop();
}

int harmony_ble_comm_reset(uint8_t is_near)
{
    g_harmony_comm_ctx.registered = QM_FALSE;
    return harmony_ble_core_unregisterd(is_near);
}