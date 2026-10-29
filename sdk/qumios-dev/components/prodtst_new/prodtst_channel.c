#include "prodtst_channel.h"
#include "qm_errno.h"
#include "cJSON.h"
#include "qm_kernel.h"
#include "prodtst_base.h"
#include "prodtst_path.h"
#include "qm_utils_list.h"
#include "qm_log.h"
#include "prodtst_common.h"

#define LOG_TAG "prodtst res"

typedef struct {
    prodtst_channel_type_t channel_type;
    prodtst_channel_handle_t channel_handle;
}prodtst_channle_ctx_t;

typedef struct {
    prodtst_hd_type_t hd_type;
    prodtst_channel_type_t channel_type;
}prodtst_channel_map_t;

static prodtst_channel_map_t g_prodtst_channel_map[] = {

    { PRODTST_HD_TYPE_BLE_MESH,     PRODTST_CHANNEL_TYPE_BLE_MESH },
    { PRODTST_HD_TYPE_BLE_CLIENT,   PRODTST_CHANNEL_TYPE_BLE_CLIENT },
    { PRODTST_HD_TYPE_UART,         PRODTST_CHANNEL_TYPE_UART },
};

static prodtst_hd_type_map_t g_prodtst_hd_type_map[] = {

    {PRODTST_HD_TYPE_UART, "uart"},
    {PRODTST_HD_TYPE_RS232, "rs232"},
    {PRODTST_HD_TYPE_WIFI_AP, "wifiAp"},
    {PRODTST_HD_TYPE_RELAY, "relay"},
    {PRODTST_HD_TYPE_IO_PIN, "ioPin"},
    {PRODTST_HD_TYPE_BLE_CLIENT, "bleClient"},
    {PRODTST_HD_TYPE_BLE_MESH, "bleMesh"},
    {PRODTST_HD_TYPE_BLE_SCAN, "bleScan"},
    {PRODTST_HD_TYPE_BLE_ADV, "bleAdv"},
    {PRODTST_HD_TYPE_BUZZER, "buzzer"},
    {PRODTST_HD_TYPE_LED, "led"},
    {PRODTST_HD_TYPE_VOLT, "volt"},
    {PRODTST_HD_TYPE_AMPERE, "ampere"},
    {PRODTST_HD_TYPE_VOICE, "voice"}
};

static prodtst_main_type_map_t g_prodtst_main_type_map[] = {
    { PRODTST_MAIN_TYPE_BOX, "box"},
    { PRODTST_MAIN_TYPE_HUB, "hub"},
    { PRODTST_MAIN_TYPE_VOLTAMMETER, "voltammeter"},
    { PRODTST_MAIN_TYPE_IO, "io"},
    { PRODTST_MAIN_TYPE_RELAY, "relay"},
    { PRODTST_MAIN_TYPE_LED, "led"},
    { PRODTST_MAIN_TYPE_VOICE, "voice"}
};

static prodtst_channle_ctx_t g_prodtst_channel_ctx[PRODTST_CHANNEL_TYPE_MAX] = {0};

int prodtst_channel_open(prodtst_addr_t *addr, void *arg)
{
    int i = 0;
    qm_err_t ret = QM_EOK;
    for(i = 0; i < PRODTST_CHANNEL_TYPE_MAX; i++){
        ret = g_prodtst_channel_ctx[i].channel_handle.open();
        if(ret != QM_EOK){
            return ret;
        }
    }
    return ret;
}

int prodtst_channel_read(prodtst_addr_t *addr, void *data, int len, int timeout, void *arg)
{
    prodtst_channel_handle_t *channel_handle = NULL;
    prodtst_channel_type_t channel_type;

    if(addr->main_type == PRODTST_MAIN_TYPE_BOX){
        channel_type = prodtst_channel_type_get(addr->hd_type);
        channel_handle = prodtst_channel_handle_get(channel_type);
        if(channel_handle == NULL){
            return -QM_EINVAL;
        }
        if(addr->sub_id == 0){
            return channel_handle->read_cfg(addr, data, len, timeout, arg);
        }
    }else{
        channel_handle = prodtst_channel_handle_get(PRODTST_CHANNEL_TYPE_BUS);
    }

    return channel_handle->read(addr, data, len, timeout, arg);
}

int prodtst_channel_write(prodtst_addr_t *addr, void *data, int len, void *arg)
{
    prodtst_channel_handle_t *channel_handle = NULL;
    prodtst_channel_type_t channel_type;

    if(addr->main_type == PRODTST_MAIN_TYPE_BOX){
        channel_type = prodtst_channel_type_get(addr->hd_type);
        channel_handle = prodtst_channel_handle_get(channel_type);
        if(channel_handle == NULL){
            return -QM_EINVAL;
        }
    }else{
        channel_handle = prodtst_channel_handle_get(PRODTST_CHANNEL_TYPE_BUS);
    }

    return channel_handle->write(addr, data, len, arg);
}

int prodtst_channel_read_cfg(prodtst_addr_t *addr, void *data, int len, int timeout, void *arg)
{
    prodtst_channel_handle_t *channel_handle = NULL;
    prodtst_channel_type_t channel_type;

    if(addr->main_type == PRODTST_MAIN_TYPE_BOX){
        channel_type = prodtst_channel_type_get(addr->hd_type);
        channel_handle = prodtst_channel_handle_get(channel_type);
        if(channel_handle == NULL){
            return -QM_EINVAL;
        }
    }else{
        channel_handle = prodtst_channel_handle_get(PRODTST_CHANNEL_TYPE_BUS);
    }
    return channel_handle->read_cfg(addr, data, len, timeout, arg);
}

int prodtst_channel_write_cfg(prodtst_addr_t *addr, void *data, int len, void *arg)
{
    prodtst_channel_handle_t *channel_handle = NULL;
    prodtst_channel_type_t channel_type;

    if(addr->main_type == PRODTST_MAIN_TYPE_BOX){
        channel_type = prodtst_channel_type_get(addr->hd_type);
        channel_handle = prodtst_channel_handle_get(channel_type);
        if(channel_handle == NULL){
            return -QM_EINVAL;
        }
    }else{
        channel_handle = prodtst_channel_handle_get(PRODTST_CHANNEL_TYPE_BUS);
    }

    return channel_handle->write_cfg(addr, data, len, arg);
}

int prodtst_channel_ctrl(prodtst_addr_t *addr, prodtst_channel_cmd_t cmd)
{
    prodtst_channel_handle_t *channel_handle = NULL;
    prodtst_channel_type_t channel_type;

    if(addr->main_type == PRODTST_MAIN_TYPE_BOX){
        channel_type = prodtst_channel_type_get(addr->hd_type);
        channel_handle = prodtst_channel_handle_get(channel_type);
        if(channel_handle == NULL){
            return -QM_EINVAL;
        }
    }else{
        channel_handle = prodtst_channel_handle_get(PRODTST_CHANNEL_TYPE_BUS);
    }
    return channel_handle->ctrl(addr, cmd);
}

int prodtst_channel_register(prodtst_channel_type_t channel_type, prodtst_channel_handle_t *channel_handle)
{
    prodtst_channel_handle_t *m_channel_handle = NULL;
    if(channel_handle == NULL){
        return -QM_EINVAL;
    }

    if(channel_type <= PRODTST_CHANNEL_TYPE_NONE ||  channel_type >= PRODTST_CHANNEL_TYPE_MAX){
        return -QM_EINVAL;
    }
    
    m_channel_handle = &g_prodtst_channel_ctx[channel_type - 1].channel_handle;
    memcpy(m_channel_handle, channel_handle, sizeof(prodtst_channel_handle_t));

    return QM_EOK;
}

prodtst_channel_type_t prodtst_channel_type_get(prodtst_hd_type_t hd_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_channel_map); i++){
        if(g_prodtst_channel_map[i].hd_type == hd_type){
            return g_prodtst_channel_map[i].channel_type;
        }
    }
    return PRODTST_CHANNEL_TYPE_NONE;
}

prodtst_channel_handle_t *prodtst_channel_handle_get(prodtst_channel_type_t channel_type)
{
    if(channel_type <= PRODTST_CHANNEL_TYPE_NONE ||  channel_type >= PRODTST_CHANNEL_TYPE_MAX){
        return NULL;
    }
    return &g_prodtst_channel_ctx[channel_type - 1].channel_handle;
}

prodtst_hd_type_t prodtst_hd_type_get(char *str_hd_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_hd_type_map); i++){
        if(0 == strcmp(g_prodtst_hd_type_map[i].str_hd_type, str_hd_type)){
            return g_prodtst_hd_type_map[i].hd_type;
        }
    }
    return PRODTST_HD_TYPE_NONE;
}

char *prodtst_hd_type_str_get(prodtst_hd_type_t hd_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_hd_type_map); i++){
        if(hd_type == g_prodtst_hd_type_map[i].hd_type){
            return g_prodtst_hd_type_map[i].str_hd_type;
        }
    }
    return NULL;
}

prodtst_main_type_t prodtst_main_type_get(char *str_main_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_main_type_map); i++){
        if(0 == strcmp(g_prodtst_main_type_map[i].str_main_type, str_main_type)){
            return g_prodtst_main_type_map[i].main_type;
        }
    }
    return PRODTST_MAIN_TYPE_NONE;
}

char *prodtst_main_type_str_get(prodtst_main_type_t main_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_main_type_map); i++){
        if(main_type == g_prodtst_main_type_map[i].main_type){
            return g_prodtst_main_type_map[i].str_main_type;
        }
    }
    return NULL;
}