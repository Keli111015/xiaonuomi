#include "qm.h"
#include "prodtst_client.h"
#include "prodtst_client_at.h"
#include "prodtst_client_common.h"

#if CONFIG_PRODTST_CLIENT_GEN_CMD_SUPPORT

#include "qm_utils_string.h"
#if CONFIG_QM_SPRINTF_SUPPORT
#include "qm_sprintf.h"
#endif

extern prodtst_client_generic_cmd_t *prodtst_client_gen_cmd_handle_get(void);

qm_at_result_t qm_prodtst_client_fwname_verify(void *channel, const char *args)
{
    int ret = QM_AT_RESULT_OK;
    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();  
    if(client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    ret = client_ctx->fwname_verify((char *)(args + 2), strlen(args) - 5);//delete '=' + '\"' + '\"' + "\r\n"

    if(ret != QM_AT_RESULT_OK){
        return QM_AT_RESULT_FAILE;
    }

    return QM_AT_RESULT_OK;
}

qm_at_result_t qm_prodtst_client_fwname_query(void *channel)
{
    char buff[65] = {0};
    int ret = QM_AT_RESULT_OK;
    prodtst_client_generic_cmd_t *client_ctx = NULL;

    if(channel == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    client_ctx = prodtst_client_gen_cmd_handle_get();
    if(client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

   
    ret = client_ctx->fwname_get(buff, sizeof(buff));
    if(ret != QM_AT_RESULT_OK){
        return ret;
    }
    qm_at_server_printfln(channel, "+FW_NAME:\"%s\"", buff);
    return ret;
}

#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
qm_at_result_t mi_pid_model_setup(void *channel, const char *args)
{
    int cnt = 0;
    uint32_t pid = 0;
    char model[65] = {0};
    int ret = QM_AT_RESULT_OK;
    int pid_len = 0;

    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();  
    if(client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    for (pid_len = 0; pid_len < strlen(args); pid_len++)
    {
        if(args[pid_len] == ','){
            break;
        }
    }
    pid = int_str_to_num(args + 1, pid_len - 1);
    memcpy(model, args + pid_len + 2, strlen(args + pid_len + 2) - 3);//delete  "\"\r\n"
    ret = client_ctx->mi_pid_model_set(pid, model);

    if(ret != QM_AT_RESULT_OK){
        return QM_AT_RESULT_FAILE;
    }

    return QM_AT_RESULT_OK;
}

qm_at_result_t mi_pid_model_query(void *channel)
{
    int ret = QM_AT_RESULT_OK;

    int pid = 0;
    uint8_t is_use = 0;
    char model[65] = {0};
    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();  
    if(client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    is_use = client_ctx->mi_is_use_get();

    pid = client_ctx->mi_pid_get();
    if(pid <= 0){
        goto __error;
    }

    ret = client_ctx->mi_model_get(model);
    if(ret != QM_AT_RESULT_OK){
        goto __error;
    }

    qm_at_server_printfln(channel, "+MI_PI:%u,\"%s\",%d", pid, model, is_use);
    return QM_AT_RESULT_OK;

__error:
    qm_at_server_printfln(channel, "+MI_PI:,,0");
    return QM_AT_RESULT_OK;
}
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT
qm_at_result_t baidu_cert_combo_query(void *channel)
{
    uint32_t product_id = 0;
    char device_name[33] = {0};
    char device_secret[65] = {0};
    int ret = QM_AT_RESULT_OK;
    
    if(channel == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();  
    if(client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    ret = client_ctx->baidu_cert_get(&product_id, device_name, device_secret);
    if(ret != QM_AT_RESULT_OK){
        return QM_AT_RESULT_FAILE;
    }
           
    qm_at_server_printfln(channel, "+CERT_BD:\"%d\",\"%s\",\"%s\"", 
                                    product_id, device_name, device_secret);
    return ret;
}


qm_at_result_t baidu_cert_combo_set_up(void *channel, const char *args)
{
    int ret = QM_AT_RESULT_OK;
    uint32_t pid = 0;
    char product_id[11] = {0};
    char device_name[33] = {0};
    char device_secret[65] = {0};

    const char *buff = args;
    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();

    if(channel == NULL || client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    sscanf(buff, "%*[^\"]\"%[^\"]\"", product_id);
    buff += strlen(product_id) + 2 + 1;
    sscanf(buff, "%*[^\"]\"%[^\"]\"", device_name);

    buff += strlen(device_name) + 2 + 1;
    sscanf(buff, "%*[^\"]\"%[^\"]\"", device_secret);

    pid = int_str_to_num(product_id, strlen(product_id));
    ret = client_ctx->baidu_cert_set(pid, device_name, device_secret);
    if(ret != QM_AT_RESULT_OK){
        return QM_AT_RESULT_FAILE;
    }

    return QM_AT_RESULT_OK;
}
#endif

#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT

#if CONFIG_PRODTST_CLIENT_CHIP_COMBO
qm_at_result_t ali_cert_combo_query(void *channel)
{
    int ret = QM_AT_RESULT_OK;

    char product_id[9] = {0};
    char device_name[33] = {0};
    char product_key[21] = {0};
    char device_secret[65] = {0};
    char product_secret[65] = {0};

    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();

    if(channel == NULL || client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    ret = client_ctx->ali_cert_get( product_id, 
                                    product_key, 
                                    product_secret,
                                    device_name,
                                    device_secret);

    if(ret != QM_AT_RESULT_OK){
        return QM_AT_RESULT_FAILE;
    }
           
    qm_at_server_printf(channel, "+CERT_ALI_COMBO:");
    qm_at_server_printf(channel, "\"%s\"", product_id);    
    qm_at_server_printf(channel, ",\"%s\"", product_key);    
    qm_at_server_printf(channel, ",\"%s\"", product_secret);    
    qm_at_server_printf(channel, ",\"%s\"", device_name);      
    qm_at_server_printf(channel, ",\"%s\"", device_secret);      
    qm_at_server_printf(channel,"\r\n");

    return ret;
}

qm_at_result_t ali_cert_combo_set_up(void *channel, const char *args)
{
    int ret = QM_AT_RESULT_OK;

    char product_id[9] = {0};
    char device_name[33] = {0};
    char product_key[21] = {0};
    char device_secret[65] = {0};
    char product_secret[65] = {0};

    const char *buff = args;
    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();

    if(channel == NULL || client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    sscanf(buff, "%*[^\"]\"%[^\"]\"", product_id);

    buff += strlen(product_id) + 2 + 1;
    sscanf(buff, "%*[^\"]\"%[^\"]\"", product_key);

    buff += strlen(product_key) + 2 + 1;
    sscanf(buff, "%*[^\"]\"%[^\"]\"", product_secret);

    buff += strlen(product_secret) + 2 + 1;
    sscanf(buff, "%*[^\"]\"%[^\"]\"", device_name);

    buff += strlen(device_name) + 2 + 1;
    sscanf(buff, "%*[^\"]\"%[^\"]\"", device_secret);


    ret = client_ctx->ali_cert_set( product_id, 
                                    product_key, 
                                    product_secret,
                                    device_name,
                                    device_secret);

    if(ret != QM_AT_RESULT_OK){
        return QM_AT_RESULT_FAILE;
    }

    return ret;
}
#endif

#endif

#if CONFIG_PRODTST_CLIENT_AT_WIFI 
qm_at_result_t qm_prodtst_client_macsta_query(void *channel)
{
    if(channel == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    int ret = QM_AT_RESULT_OK;
    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();
    if(client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    uint8_t mac[6] = {0};
    char buff[20] = {0};
    ret = client_ctx->wifi_mac_get(mac);
    if(ret != QM_AT_RESULT_OK){
        return QM_AT_RESULT_FAILE;
    }
    qm_snprintf(buff, 20, "%02x:%02x:%02x:%02x:%02x:%02x", 
            mac[0], mac[1],mac[2],mac[3],mac[4],mac[5]);
    qm_at_server_printfln(channel, "+MAC_STA:\"%s\"", buff);
    return ret;
}
#endif

#if CONFIG_PRODTST_CLIENT_AT_BLE 
qm_at_result_t qm_prodtst_client_macble_query(void *channel)
{
    if(channel == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    int ret = QM_AT_RESULT_OK;
    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();
    if(client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    uint8_t mac[6] = {0};
    char buff[20] = {0};
    ret = client_ctx->ble_mac_get(mac);
    if(ret != QM_AT_RESULT_OK){
        return ret;
    }
    qm_snprintf(buff, 20, "%02x:%02x:%02x:%02x:%02x:%02x", 
            mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    qm_at_server_printfln(channel, "+MAC_BLE:\"%s\"", buff);
    return ret;
}

#if CONFIG_PRODTST_CLIENT_AT_BLE_ADV
qm_at_result_t qm_prodtst_client_ble_adv_set_up(void *channel, const char *args)
{
    int ret = QM_AT_RESULT_OK;

    const char *rsp_expr = "=%d,%d,\"%[^\"]\"";
    
    int interval = 0;
    int duration = 0;
    char parse_adv_data[64 + 1] = {0}; 
    uint8_t adv_raw_data[32] = {0}; // esp32 adv_data less than 32

    const char *buff = args + 1;
    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();

    if(channel == NULL || args == NULL || client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    ret = sscanf(args, rsp_expr, &interval, &duration, parse_adv_data);
    if(ret != 3){
        return QM_AT_RESULT_FAILE;
    }

    hex_str_to_nums((char *)parse_adv_data, 
                    strlen((const char *)parse_adv_data), 
                    adv_raw_data);

    ret = client_ctx->ble_adv_set(interval, 
                                  duration, 
                                  adv_raw_data, 
                                  strlen((const char *)parse_adv_data) / 2);
    if(ret != QM_AT_RESULT_OK){
        return QM_AT_RESULT_FAILE;
    }
    return ret;
}
#endif
#endif

#if CONFIG_PRODTST_CLIENT_AT_WIFI_RF       
qm_at_result_t qm_prodtst_client_rf_wifi_setup(void *channel, const char *args)
{
    if(channel == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    int ret = QM_AT_RESULT_OK;
    prodtst_client_generic_cmd_t *client_ctx = NULL;
    client_ctx = prodtst_client_gen_cmd_handle_get();
    if(client_ctx == NULL){
        return QM_AT_RESULT_CHANNEL_DOWN;
    }

    int8_t rssi = 0;
    const char *buff = args + 1;
    ret = client_ctx->wifi_rf_test(buff, &rssi);
    if(ret != QM_AT_RESULT_OK){
        return ret;
    }
    qm_at_server_printfln(channel, "+RF_W:%d", rssi);
    return ret;
}
#endif

#endif
