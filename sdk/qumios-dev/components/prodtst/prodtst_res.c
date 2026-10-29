#include "prodtst_res.h"
#include "qm_errno.h"
#include "cJSON.h"
#include "qm_kernel.h"
#include "prodtst_base.h"
#include "prodtst_path.h"
#include "qm_utils_list.h"
#include "qm_log.h"
#include "prodtst_common.h"

#define LOG_TAG "prodtst res"

static prodtst_res_dist_t g_prodtst_res_dist[] = {

    {PRODTST_HD_TYPE_UART, PRODTST_RES_LOCATION_EXTERNAL},
    {PRODTST_HD_TYPE_RS232, PRODTST_RES_LOCATION_EXTERNAL},
    {PRODTST_HD_TYPE_IO_PIN, PRODTST_RES_LOCATION_EXTERNAL},
    {PRODTST_HD_TYPE_BUZZER, PRODTST_RES_LOCATION_EXTERNAL},
    {PRODTST_HD_TYPE_BLE_CLIENT, PRODTST_RES_LOCATION_EXTERNAL},
    {PRODTST_HD_TYPE_BLE_MESH, PRODTST_RES_LOCATION_EXTERNAL},
    {PRODTST_HD_TYPE_WIFI_AP, PRODTST_RES_LOCATION_INNER},
    {PRODTST_HD_TYPE_BLE_SCAN, PRODTST_RES_LOCATION_INNER},
    {PRODTST_HD_TYPE_BLE_ADV, PRODTST_RES_LOCATION_INNER},
    {PRODTST_HD_TYPE_RELAY, PRODTST_RES_LOCATION_EXTERNAL},
    {PRODTST_HD_TYPE_LED, PRODTST_RES_LOCATION_EXTERNAL},
};


typedef struct {
    prodtst_res_t res[PRODTST_HD_TYPE_MAX];
}prodtst_config_ctx_t;

static prodtst_config_ctx_t g_prodtst_config_ctx = {0};

static prodtst_res_t *prodtst_res_get(prodtst_hd_type_t hd_type);

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
    {PRODTST_HD_TYPE_LED, "led"}
};

static prodtst_channel_type_map_t g_prodtst_channel_type_map[] = {
    {PRODTST_CHANNEL_TYPE_INNER, "inner"},
    {PRODTST_CHANNEL_TYPE_DEVICE, "device"},
    {PRODTST_CHANNEL_TYPE_TEST, "test"},
    {PRODTST_CHANNEL_TYPE_CUSTOM, "custom"}
};

static prodtst_main_type_map_t g_prodtst_main_type_map[] = {
    { PRODTST_MAIN_TYPE_BOX, "box"},
    { PRODTST_MAIN_TYPE_HUB, "hub"}
};

prodtst_res_dist_t *prodtst_res_dist_get(prodtst_hd_type_t hd_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_res_dist); i++){
        if(hd_type == g_prodtst_res_dist[i].hd_type){
            return &g_prodtst_res_dist[i];
        }
    }
    return NULL;
}

int prodtst_res_open(prodtst_hd_type_t hd_type, void *addr, void *arg)
{
    qm_err_t ret = QM_EOK;
    prodtst_res_t *res = NULL;

    res = prodtst_res_get(hd_type);
    if(res == NULL){
        QM_LOGE(LOG_TAG, "res get fail");
        return -QM_EINVAL;
    }

    if(res->res_handle.open == NULL){
        QM_LOGE(LOG_TAG, "res_handle open null");
        return -QM_EINVAL;
    }

    ret = res->res_handle.open(addr, arg);
    if(ret != QM_EOK){
        return ret;
    }
    return ret;
}

int prodtst_res_read(prodtst_hd_type_t hd_type, void *addr, void *data, int len, void *arg)
{
    prodtst_res_t *res = NULL;
    res = prodtst_res_get(hd_type);
    if(res == NULL){
        return -QM_EINVAL;
    }
    if(res->res_handle.read == NULL){
        return -QM_EINVAL;
    }
    return res->res_handle.read(addr, data, len, arg);
}

int prodtst_res_readtimeout(prodtst_hd_type_t hd_type, void *addr, void *data, int len, int timeout, void *arg)
{
    prodtst_res_t *res = NULL;
    res = prodtst_res_get(hd_type);
    if(res == NULL){
        return -QM_EINVAL;
    }
    if(res->res_handle.readtimeout == NULL){
        return -QM_EINVAL;
    }
    return res->res_handle.readtimeout(addr, data, len, timeout, arg);
}

int prodtst_res_write(prodtst_hd_type_t hd_type, void *addr, void *data, int len, void *arg)
{
    prodtst_res_t *res = NULL;
    res = prodtst_res_get(hd_type);
    if(res == NULL){
        return -QM_EINVAL;
    }
    if(res->res_handle.write == NULL){
        return -QM_EINVAL;
    }
    return res->res_handle.write(addr, data, len, arg);
}

int prodtst_res_read_cfg_timeout(prodtst_hd_type_t hd_type, void *addr, void *data, int len, int timeout)
{
    prodtst_res_t *res = NULL;
    res = prodtst_res_get(hd_type);
    if(res == NULL){
        return -QM_EINVAL;
    }
    if(res->res_handle.read_cfg_timeout == NULL){
        return -QM_EINVAL;
    }
    return res->res_handle.read_cfg_timeout(addr, data, len, timeout);
}

int prodtst_res_write_cfg(prodtst_hd_type_t hd_type, void *addr, void *data, int len)
{
    prodtst_res_t *res = NULL;
    res = prodtst_res_get(hd_type);
    if(res == NULL){
        return -QM_EINVAL;
    }
    if(res->res_handle.write_cfg == NULL){
        return -QM_EINVAL;
    }
    return res->res_handle.write_cfg(addr, data, len);
}

int prodtst_res_ctrl(prodtst_hd_type_t hd_type, void *addr, int cmd, void *arg)
{
    prodtst_res_t *res = NULL;
    res = prodtst_res_get(hd_type);
    if(res == NULL){
        return -QM_EINVAL;
    }
    if(res->res_handle.ctrl == NULL){
        return -QM_EINVAL;
    }
    return res->res_handle.ctrl(addr, cmd, arg);
}

int prodtst_res_close(prodtst_hd_type_t hd_type, void *addr)
{
    qm_err_t ret = QM_EOK;
    prodtst_res_t *res = NULL;
    res = prodtst_res_get(hd_type);
    if(res == NULL){
        return -QM_EINVAL;
    }
    if(res->res_handle.close == NULL){
        return -QM_EINVAL;
    }
    ret = res->res_handle.close(addr);
    if(ret != QM_EOK){
        return ret;
    }
    return ret;
}

int prodtst_res_register(prodtst_hd_type_t hd_type, prodtst_res_handle_t *res_handle)
{
    prodtst_res_t *res = NULL;
    if(res_handle == NULL){
        return -QM_EINVAL;
    }

    if(hd_type <= PRODTST_HD_TYPE_NONE ||  hd_type >= PRODTST_HD_TYPE_MAX){
        return -QM_EINVAL;
    }
    
    res = &g_prodtst_config_ctx.res[hd_type];

    memcpy(&res->res_handle, res_handle, sizeof(prodtst_res_handle_t));
    res->hd_type = hd_type;
    return QM_EOK;
}

prodtst_res_t *prodtst_res_get(prodtst_hd_type_t hd_type)
{
    if(hd_type <= PRODTST_HD_TYPE_NONE ||  hd_type >= PRODTST_HD_TYPE_MAX){
        return NULL;
    }

    return &g_prodtst_config_ctx.res[hd_type];
}

prodtst_channel_type_t prodtst_channel_type_get(char *str_channel_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_channel_type_map); i++){
        if(0 == strcmp(g_prodtst_channel_type_map[i].str_channel_type, str_channel_type)){
            return g_prodtst_channel_type_map[i].channel_type;
        }
    }
    return PRODTST_CHANNEL_TYPE_NONE;
}

char *prodtst_channel_type_str_get(prodtst_channel_type_t channel_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_channel_type_map); i++){
        if(channel_type == g_prodtst_channel_type_map[i].channel_type){
            return g_prodtst_channel_type_map[i].str_channel_type;
        }
    }
    return NULL;
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