#include "prodtst_client.h"
#include "prodtst_client_at.h"
#include "qm.h"
#include "qm_at.h"
#include "qm_log.h"

/** Production-test client event base used by event-loop APIs. */
QM_EVENT_DEFINE_BASE(QM_EVENT_PRODTST_CLIENT);

typedef struct{
    void *handle;
    qm_at_server_t *at_handle;
    prodtst_client_link_t *link_handle;
}prodtst_client_ctx_t;

typedef struct 
{
    prodtst_client_event_cb event_fn;
    prodtst_client_generic_cmd_t  *cmd_ctx;
}prodtst_client_handle_t;

#if CONFIG_PRODTST_CLIENT_UART_SUPPORT
extern prodtst_client_link_t prodtst_client_uart_link_ctx;
#endif

#if CONFIG_PRODTST_CLIENT_BLE_MESH_SUPPORT
extern prodtst_client_link_t prodtst_client_ble_mesh_link_ctx;
#endif

#if CONFIG_PRODTST_CLIENT_BLE_GATT_SUPPORT
extern prodtst_client_link_t prodtst_client_ble_gatt_link_ctx;
#endif

static prodtst_client_handle_t g_prodtst_client_link_ctx = {0};
static prodtst_client_ctx_t g_prodtst_client_handle[PRODTST_CLIENT_CHANNEL_TYPE_MAX] = {0};

#if CONFIG_PRODTST_CLIENT_GEN_CMD_SUPPORT
static qm_at_cmd_t prodtst_client_common_cmd[] = {
    {"AT+FW_NAME", NULL, NULL, qm_prodtst_client_fwname_query, qm_prodtst_client_fwname_verify, NULL},
#if CONFIG_IOT_GENERIC_PLATFORM_MI_SUPPORT
    {"AT+MI_PI", NULL, NULL, mi_pid_model_query, mi_pid_model_setup, NULL},
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_BAIDU_SUPPORT
    {"AT+CERT_BD", NULL, NULL, baidu_cert_combo_query, baidu_cert_combo_set_up, NULL},
#endif
#if CONFIG_IOT_GENERIC_PLATFORM_ALI_SUPPORT
#if CONFIG_PRODTST_CLIENT_CHIP_COMBO
    {"AT+CERT_ALI_COMBO", NULL, NULL, ali_cert_combo_query, ali_cert_combo_set_up, NULL},
#endif
#endif
#if CONFIG_PRODTST_CLIENT_AT_BLE 
    {"AT+MAC_BLE", NULL, NULL, qm_prodtst_client_macble_query, NULL, NULL},
#if CONFIG_PRODTST_CLIENT_AT_BLE_ADV
    {"AT+BLE_ADV", NULL, NULL, NULL, qm_prodtst_client_ble_adv_set_up, NULL},
#endif
#endif
#if CONFIG_PRODTST_CLIENT_AT_WIFI       
    {"AT+MAC_STA", NULL, NULL, qm_prodtst_client_macsta_query, NULL, NULL},
#if CONFIG_PRODTST_CLIENT_AT_WIFI_RF       
    {"AT+RF_W", NULL, NULL, NULL, qm_prodtst_client_rf_wifi_setup, NULL},
#endif
#endif
};
#endif

void *prodtst_client_handle_get(prodtst_client_channel_t channel)
{
    if (channel >= PRODTST_CLIENT_CHANNEL_TYPE_MAX){
        return NULL;
    }

    return (void *)&g_prodtst_client_handle[channel - 1];
}

prodtst_client_generic_cmd_t *prodtst_client_gen_cmd_handle_get(void)
{
    return g_prodtst_client_link_ctx.cmd_ctx;
}

int prodtst_client_event_notify_register(prodtst_client_event_cb event_fn)
{
    if(event_fn == NULL){
        return -QM_EINVAL;
    }    
    
    g_prodtst_client_link_ctx.event_fn = event_fn;
    return QM_EOK;
}

int prodtst_client_generic_cmd_register(prodtst_client_generic_cmd_t *eneric_hal)
{
    if(eneric_hal == NULL){
        return -QM_EINVAL;
    }

    g_prodtst_client_link_ctx.cmd_ctx = eneric_hal;

    return QM_EOK;
}

int prodtst_client_event_post(prodtst_client_event_t event)
{
    if(g_prodtst_client_link_ctx.event_fn == NULL){
        return -QM_EINVAL;
    }    
    
    g_prodtst_client_link_ctx.event_fn(event);
    return QM_EOK;
}

#if CONFIG_PRODTST_CLIENT_EXT_LINK_SUPPORT
int prodtst_client_channel_link_register(prodtst_client_channel_t channel, prodtst_client_link_t *link_fn)
{
    if (channel >= PRODTST_CLIENT_CHANNEL_TYPE_MAX){
        return -QM_EINVAL;
    }

    g_prodtst_client_handle[channel - 1].link_handle = link_fn;
    return QM_EOK;
}
#else
static prodtst_client_link_t *prodtst_client_link_register(prodtst_client_channel_t  channel)
{
    prodtst_client_link_t *link = NULL;

    if (channel >= PRODTST_CLIENT_CHANNEL_TYPE_MAX){
        return NULL;
    }

    switch (channel)
    {
    #if CONFIG_PRODTST_CLIENT_UART_SUPPORT
        case PRODTST_CLIENT_CHANNEL_TYPE_UART:{
            link = &prodtst_client_uart_link_ctx;
        }break;
    #endif    
    #if CONFIG_PRODTST_CLIENT_BLE_MESH_SUPPORT
        case PRODTST_CLIENT_CHANNEL_TYPE_BLE_MESH:{
            link = &prodtst_client_ble_mesh_link_ctx;
        }break;
    #endif    
    #if CONFIG_PRODTST_CLIENT_BLE_GATT_SUPPORT
        case PRODTST_CLIENT_CHANNEL_TYPE_BLE_GATT:{
            link = &prodtst_client_ble_gatt_link_ctx;
        }break;
    #endif
        default:
        break;
    }

    return link;
}
#endif


#if !CONFIG_QM_AT_SERVER_OS_SUPPORT
int prodtst_client_data_push(void *handle, char *buf, uint32_t size)
{
    prodtst_client_ctx_t *client_ctx = (prodtst_client_ctx_t *)handle;
    if(handle == NULL || buf == NULL || size == 0){
        return -QM_EINVAL;
    }
    return qm_at_server_data_push(client_ctx->at_handle, buf, size);
}
#endif


void *prodtst_client_init(prodtst_client_channel_t  channel, void *prodtst_func_cmd , uint16_t cmd_num)
{
    qm_at_server_param_t param = {0};
    prodtst_client_ctx_t *client_ctx = NULL;
    prodtst_client_link_t *link_handle = NULL;

    if (channel >= PRODTST_CLIENT_CHANNEL_TYPE_MAX){
        return NULL;
    }

    client_ctx = &g_prodtst_client_handle[channel - 1];
    
#if !CONFIG_PRODTST_CLIENT_EXT_LINK_SUPPORT
    memset(client_ctx, 0, sizeof(prodtst_client_ctx_t));
    client_ctx->link_handle = prodtst_client_link_register(channel);
#endif

    link_handle = client_ctx->link_handle;
    client_ctx->handle = link_handle->init();

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    param.recv = link_handle->read;
#endif
    param.send = link_handle->write;
    param.handle = client_ctx->handle;
    param.channel_type = channel;
    param.recv_buf_len = CONFIG_PRODTST_CLIENT_RECV_BUFF_LEN;

    client_ctx->at_handle = qm_at_server_init(&param);

#if CONFIG_PRODTST_CLIENT_GEN_CMD_SUPPORT
    if(prodtst_client_gen_cmd_handle_get() != NULL){
        qm_at_register_commands(client_ctx->at_handle, prodtst_client_common_cmd, QM_ARRAY_SIZE(prodtst_client_common_cmd));
    }
#endif

    if(prodtst_func_cmd && cmd_num){
        qm_at_register_commands(client_ctx->at_handle, (qm_at_cmd_t *)prodtst_func_cmd, cmd_num);
    }

    return (void *)client_ctx;
}

int prodtst_client_deinit(void *handle)
{
    int ret = QM_EOK;
    prodtst_client_link_t *link_handle = NULL;
    prodtst_client_ctx_t *client_ctx = (prodtst_client_ctx_t *)handle;
    if(handle == NULL || client_ctx->link_handle == NULL){
        return QM_AT_RESULT_FAILE;
    }
 
    ret =  qm_at_server_deinit(client_ctx->at_handle);
    if(ret != QM_EOK){
        goto _error;
    } 

    link_handle = client_ctx->link_handle;
    ret = link_handle->deinit(client_ctx->handle);
    if(ret != QM_EOK){
        goto _error;
    }

    if(client_ctx){
        client_ctx = NULL;
    }

    return QM_EOK;

_error:
    return ret;
}
