#include "prodtst_config.h"
#include "qm_errno.h"
#include "prodtst_common.h"
#include "prodtst_path.h"
#include "prodtst_data.h"
#include "cJSON.h"
#include "qm_log.h"
#include "prodtst_data.h"
#include "prodtst_base.h"

#define LOG_TAG "prodtst config"

extern prodtst_data_ctx_t g_prodtst_data_ctx;

static prodtst_test_type_map_t g_prodtst_test_type_map[] = {

    {PRODTST_TEST_TYPE_WIRED,       "wired"    },
    {PRODTST_TEST_TYPE_WIFI,        "wifi"     },
    {PRODTST_TEST_TYPE_BLE_GATT,    "bleGatt"  },
    {PRODTST_TEST_TYPE_BLE_ADV,     "bleAdv"   },
    {PRODTST_TEST_TYPE_BLE_MESH,    "bleMesh"  }
};

static prodtst_config_handle_t *prodtst_config_handle_get(prodtst_hd_type_t hd_type);

static int prodtst_config_generic_init(prodtst_config_t *config);
static int prodtst_config_generic_deinit(prodtst_config_t *config);

//config specific unpack
static int prodtst_uart_unpack(void *json_object, prodtst_config_t *config);
static int prodtst_uart_destroy(prodtst_config_t *config);

static int prodtst_wifi_ap_unpack(void *json_object, prodtst_config_t *config);
static int prodtst_wifi_ap_destroy(prodtst_config_t *config);

static int prodtst_ble_client_unpack(void *json_object, prodtst_config_t *config);
static int prodtst_ble_client_destroy(prodtst_config_t *config);

static int prodtst_ble_mesh_unpack(void *json_object, prodtst_config_t *config);
static int prodtst_ble_mesh_destroy(prodtst_config_t *config);


static prodtst_config_handle_t g_prodtst_config_handle[] = {
    { PRODTST_HD_TYPE_UART,           prodtst_uart_unpack,         NULL,     prodtst_uart_destroy,               prodtst_config_generic_init,    prodtst_config_generic_deinit    },
    { PRODTST_HD_TYPE_BLE_CLIENT,     prodtst_ble_client_unpack,   NULL,     prodtst_ble_client_destroy,         prodtst_config_generic_init,    prodtst_config_generic_deinit    },
    { PRODTST_HD_TYPE_WIFI_AP,        prodtst_wifi_ap_unpack,      NULL,     prodtst_wifi_ap_destroy,            prodtst_config_generic_init,    prodtst_config_generic_deinit    },
    { PRODTST_HD_TYPE_BLE_MESH,       prodtst_ble_mesh_unpack,     NULL,     prodtst_ble_mesh_destroy,           prodtst_config_generic_init,    prodtst_config_generic_deinit    },
    { PRODTST_HD_TYPE_BLE_ADV,       prodtst_ble_mesh_unpack,     NULL,     prodtst_ble_mesh_destroy,           prodtst_config_generic_init,    prodtst_config_generic_deinit    },

};

static prodtst_config_handle_t *prodtst_config_handle_get(prodtst_hd_type_t hd_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_config_handle); i++){
        if(hd_type == g_prodtst_config_handle[i].hd_type){
            return &g_prodtst_config_handle[i];
        }
    }
    return NULL;
}

static prodtst_test_type_t prodtst_test_type_get(char *test_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_test_type_map); i++){
        if(strlen(test_type) == strlen(g_prodtst_test_type_map[i].str_test_type) &&
           0 == memcmp(test_type, g_prodtst_test_type_map[i].str_test_type, strlen(test_type))){
             return g_prodtst_test_type_map[i].test_type;
           }
    }
    return PRODTST_TEST_TYPE_NONE;
}

int prodtst_config_unpack(void *json_object, prodtst_config_info_t *config_info)
{   
    int size = 0;
    int i = 0;
    int channel_num = 0;
    qm_err_t ret = QM_EOK;
    char *str = NULL;
    cJSON *params_config = (cJSON*)json_object;
    cJSON *array_config = NULL;
    cJSON *json_config = NULL;
    prodtst_config_t *config = NULL;
    prodtst_config_t *cfg_tmp = NULL;
    prodtst_config_handle_t *config_handle = NULL;

    char *str_hd_type = NULL;
    char *dest = NULL;

    str = cjson_str_get(params_config, PRODTST_JSON_KEY_TEST_TYPE);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }
    if(strlen(str) > PRODTST_TEST_TYPE_MAX_LEN){
        ret = -QM_EINVAL;
        goto __exit;
    }

    config_info->test_type = prodtst_test_type_get(str);
    if(config_info->test_type == PRODTST_TEST_TYPE_NONE){
        return -QM_EINVAL;
    }
    str = cjson_str_get(params_config, PRODTST_JSON_KEY_BUTTON_START);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    if(0 == strcmp(str, "enable")){
        config_info->button_enable = 1;
    }else{
        config_info->button_enable = 0;
    }
    QM_LOGD(LOG_TAG, "config button_enable: %d", config_info->button_enable);

    ret = cjson_int_get(params_config, PRODTST_JSON_KEY_CHANNEL_NUM, &channel_num);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;   
    }
    config_info->channel_num = channel_num;
    QM_LOGD(LOG_TAG, "config channel_num: %d", config_info->channel_num);


    array_config = cJSON_GetObjectItem(params_config, PRODTST_JSON_KEY_CONFIG);
    if(array_config == NULL){
        QM_LOGE(LOG_TAG, "json config object null");
        ret = -QM_EINVAL;
        goto __exit;
    }

    size = cJSON_GetArraySize(array_config);
    if(size == 0){
        QM_LOGE(LOG_TAG, "json array size null");
        ret = -QM_EINVAL;
        goto __exit;
    }

    QM_LOGD(LOG_TAG, "config num: %d", size);
    config_info->config_num = size;

    config = (prodtst_config_t*)qm_malloc(sizeof(prodtst_config_t) * size);
    if(config == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    memset(config, 0, sizeof(prodtst_config_t) * size);

    cfg_tmp = config;
    config_info->config = config;
    
    for(i = 0; i < size; i++){

        json_config = cJSON_GetArrayItem(array_config, i);
        if(json_config == NULL){
            QM_LOGE(LOG_TAG, "json get array fail");
            ret = -QM_EINVAL;
            goto __exit;
        }

        str_hd_type = cjson_str_get(json_config, PRODTST_JSON_KEY_HD_TYPE);
        if(str_hd_type == NULL){
            QM_LOGE(LOG_TAG, "json key get fail");
            return -QM_EINVAL;
        }

        dest = cjson_str_get(json_config, PRODTST_JSON_KEY_DEST);
        if(dest == NULL){
            QM_LOGE(LOG_TAG, "json key get fail");
            return -QM_EINVAL;
        }

        cfg_tmp->addr_list = prodtst_path_unpack(dest, strlen(dest));
        if(cfg_tmp->addr_list == NULL){
            QM_LOGE(LOG_TAG, "prodtst path unpack fail");
            return -QM_EINVAL;
        }

        cfg_tmp->hd_type = prodtst_hd_type_get(str_hd_type);

        config_handle = prodtst_config_handle_get(cfg_tmp->hd_type);
        if(config_handle == NULL){
            ret = -QM_EINVAL;
            goto __exit;
        }

        ret = config_handle->unpack(cJSON_GetObjectItem(json_config, PRODTST_JSON_KEY_CFG), cfg_tmp);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "prodtst specific config unpack fail");
            goto __exit;
        }
        cfg_tmp = cfg_tmp + 1;
    }

__exit:
    return ret;
}

int prodtst_config_pack(prodtst_config_info_t *config_info, void *json_object)
{
    return QM_EOK;
}

int prodtst_config_destroy(prodtst_config_info_t *config_info)
{
    int i = 0;
    qm_err_t ret = QM_EOK;
    int config_num = 0;
    prodtst_config_t *config = NULL;
    prodtst_config_handle_t *config_handle = NULL;

    config_num = config_info->config_num;

    for(i = 0; i < config_num; i++){

        config = config_info->config + i;

        config_handle = prodtst_config_handle_get(config->hd_type);
        if(config_handle == NULL){
            QM_LOGE(LOG_TAG, "handle get fail");
            ret = -QM_EINVAL;
            goto __exit;
        }

        if(config_handle->destroy == NULL){
            ret = -QM_ERROR;
            goto __exit;
        }

        ret = config_handle->destroy(config);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "config destroy fail");
            goto __exit;
        }
    }
    
__exit:
    if(config_info->config){
        qm_free(config_info->config);
        config_info->config = NULL;
    }

    return ret;
}

int prodtst_config_init(prodtst_config_info_t *config_info)
{
    int i = 0;
    qm_err_t ret = QM_EOK;
    int config_num = 0;
    prodtst_config_t *config = NULL;
    prodtst_config_handle_t *config_handle = NULL;

    config_num = config_info->config_num;

    for(i = 0; i < config_num; i++){

        config = config_info->config + i;

        config_handle = prodtst_config_handle_get(config->hd_type);
        if(config_handle == NULL){
            QM_LOGE(LOG_TAG, "handle get fail");
            ret = -QM_EINVAL;
            goto __exit;
        }

        if(config_handle->init == NULL){
            ret = -QM_ERROR;
            goto __exit;
        }

        ret = config_handle->init(config);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "config init fail");
            goto __exit;
        }

        if(g_prodtst_data_ctx.data_cb){
            g_prodtst_data_ctx.data_cb(PRODTST_COMM_MSG_TYPE_CONFIG, PRODTST_DATA_NOTIFY_CONFIG_INFO, config);
        }
    }
    
__exit:
    return ret;
}

int prodtst_config_deinit(prodtst_config_info_t *config_info)
{
    int i = 0;
    qm_err_t ret = QM_EOK;
    int config_num = 0;
    prodtst_config_t *config = NULL;
    prodtst_config_handle_t *config_handle = NULL;

    config_num = config_info->config_num;

    for(i = 0; i < config_num; i++){

        config = config_info->config + i;

        config_handle = prodtst_config_handle_get(config->hd_type);
        if(config_handle == NULL){
            QM_LOGE(LOG_TAG, "handle get fail");
            ret = -QM_EINVAL;
            goto __exit;
        }

        if(config_handle->init == NULL){
            ret = -QM_ERROR;
            goto __exit;
        }

        ret = config_handle->deinit(config);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "config deinit fail");
            goto __exit;
        }
    }
    
__exit:
    return ret;
}

int prodtst_config_active_deinit(void)
{
    return QM_EOK;
}


//config specific unpack
static int prodtst_uart_unpack(void *json_object, prodtst_config_t *config)
{
    int ret = 0;
    int data = 0;
    cJSON *root = (cJSON*)json_object;
    char *str = NULL;
    int channel_id = 0;
    prodtst_uart_config_t *uart_cfg = &config->cfg.uart_config;

    if(json_object == NULL || config == NULL){
        QM_LOGE(LOG_TAG, "prodtst_uart_unpack error");
        return -QM_EINVAL;
    }

    str = cjson_str_get(root, PRODTST_JSON_KEY_CHANNEL_TYPE);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        return -QM_EINVAL;
    }

    uart_cfg->channel_type = prodtst_channel_type_get(str);
    if(uart_cfg->channel_type == PRODTST_CHANNEL_TYPE_NONE){
        QM_LOGE(LOG_TAG, "prodtst channel type get");
        return -QM_EINVAL;
    }

    ret = cjson_int_get(root, PRODTST_JSON_KEY_CHANNEL_ID, &channel_id);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "json key get fail");
        return ret;
    }
    uart_cfg->channel_id = channel_id;

    ret = cjson_int_get(root, PRODTST_JSON_KEY_BRAUDRATE, &data);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "json key get fail");
        return ret;
    }
    uart_cfg->baudrate = data;

    ret = cjson_int_get(root, PRODTST_JSON_KEY_DATABITS, &data);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "json key get fail");
        return ret;
    }
    uart_cfg->databits = data;

    ret = cjson_int_get(root, PRODTST_JSON_KEY_STOPBITS, &data);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "json key get fail");
        return ret;
    }
    uart_cfg->stopbits = data;

    str = cjson_str_get(root, PRODTST_JSON_KEY_PARITY);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        return -QM_EINVAL;
    }

    if(0 == strcmp(str, "none")){
        uart_cfg->parity = PRODTST_UART_PARITY_NONE;
    }else if(0 == strcmp(str, "odd")){
        uart_cfg->parity = PRODTST_UART_PARITY_ODD;
    }else if(0 == strcmp(str, "even")){
        uart_cfg->parity = PRODTST_UART_PARITY_EVEN;
    }

    return QM_EOK;
}

static int prodtst_uart_destroy(prodtst_config_t *config)
{
    return QM_EOK;
}


static int prodtst_wifi_ap_unpack(void *json_object, prodtst_config_t *config)
{


    return QM_EOK;
}

static int prodtst_wifi_ap_destroy(prodtst_config_t *config)
{

    return QM_EOK;
}

static int prodtst_ble_mesh_unpack(void *json_object, prodtst_config_t *config)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = (cJSON*)json_object;
    int pid = 0;
    prodtst_ble_mesh_config_t *mesh_config = &config->cfg.ble_mesh_config;

    ret = cjson_int_get(root, PRODTST_JSON_KEY_BLE_MESH_PID, &pid);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "json key get fail");
        return ret;
    }
    
    mesh_config->pid = (uint16_t)pid;
    QM_LOGD(LOG_TAG, "ble_mesh_unpack PID %d", mesh_config->pid);
    return QM_EOK;
}

static int prodtst_ble_mesh_destroy(prodtst_config_t *config)
{
    return QM_EOK;
}


static int prodtst_ble_client_unpack(void *json_object, prodtst_config_t *config)
{
    return QM_EOK;
}

static int prodtst_ble_client_destroy(prodtst_config_t *config)
{
    return QM_EOK;
}

static int prodtst_config_generic_init(prodtst_config_t *config)
{
    return prodtst_res_open(config->hd_type, (void*)config->addr_list, (void*)&config->cfg);
}

static int prodtst_config_generic_deinit(prodtst_config_t *config)
{
    prodtst_addr_t *addr = NULL;

    addr = prodtst_addr_first_get(config->addr_list);
    if(addr == NULL){
        return -QM_EINVAL;
    }

    return prodtst_res_close(addr->hd_type, (void*)config->addr_list);
}
