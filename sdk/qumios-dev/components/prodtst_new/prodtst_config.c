#include "prodtst_config.h"
#include "qm_errno.h"
#include "prodtst_common.h"
#include "prodtst_path.h"
#include "prodtst_data.h"
#include "cJSON.h"
#include "qm_log.h"
#include "prodtst_data.h"
#include "prodtst_base.h"
#include "prodtst_at.h"

#define LOG_TAG "prodtst config"

extern prodtst_data_ctx_t g_prodtst_data_ctx;

static prodtst_test_type_map_t g_prodtst_test_type_map[] = {

    {PRODTST_TEST_TYPE_WIRED,       "wired"    },
    {PRODTST_TEST_TYPE_WIFI,        "wifi"     },
    {PRODTST_TEST_TYPE_BLE_GATT,    "bleGatt"  },
    {PRODTST_TEST_TYPE_BLE_ADV,     "bleAdv"   },
    {PRODTST_TEST_TYPE_BLE_MESH,    "bleMesh"  }
};

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

    char *dest = NULL;
    char *cfg = NULL;

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

        dest = cjson_str_get(json_config, PRODTST_JSON_KEY_DEST);
        if(dest == NULL){
            QM_LOGE(LOG_TAG, "json key get fail");
            return -QM_EINVAL;
        }

        ret = prodtst_path_unpack(dest, strlen(dest), &cfg_tmp->addr);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "prodtst path unpack fail");
            return ret;
        }

        cfg = cjson_str_get(json_config, PRODTST_JSON_KEY_CFG);
        if(cfg && strlen(cfg)){
            cfg_tmp->cfg = malloc_and_copy(cfg, strlen(cfg));
            if(cfg_tmp->cfg == NULL){
                return -QM_ENOMEM;
            }
        }else{
            cfg_tmp->cfg = NULL;
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
    if(config_info->config){
        qm_free(config_info->config);
        config_info->config = NULL;
    }
    return QM_EOK;
}

int prodtst_config_init(prodtst_config_info_t *config_info)
{
    int i = 0;
    qm_err_t ret = QM_EOK;
    int config_num = 0;
    prodtst_config_t *config = NULL;
    prodtst_recv_rule_at_t recv_rule;
    prodtst_at_rsp_data_t at_rsp_data = {0};

    config_num = config_info->config_num;

    for(i = 0; i < config_num; i++){

        config = config_info->config + i;

        if(config->addr.main_type == PRODTST_MAIN_TYPE_BOX){
            QM_LOGD(LOG_TAG, "config at inner exec start...");
            recv_rule.recv_size = CONFIG_PRODTST_INNER_AT_BUF_MAX_SIZE;
            recv_rule.wait_time = CONFIG_PRODTST_INNER_AT_WAIT_TIMEOUT;
            ret = prodtst_at_client_inner_exec_cmd(config->cfg, &recv_rule, &at_rsp_data);
            if(ret != QM_EOK){
                goto __exit;
            }
        }else{
            QM_LOGD(LOG_TAG, "config at ext exec start...");
            recv_rule.recv_size = CONFIG_PRODTST_EXT_CFG_AT_BUF_MAX_SIZE;
            recv_rule.wait_time = CONFIG_PRODTST_EXT_CFG_AT_WAIT_TIMEOUT;
            ret = prodtst_at_client_ext_exec_cfg_cmd(config->cfg, &recv_rule, &config->addr, &at_rsp_data);
            if(ret != QM_EOK){
                goto __exit;
            }
        }

        if(g_prodtst_data_ctx.data_cb){
            g_prodtst_data_ctx.data_cb(PRODTST_COMM_MSG_TYPE_CONFIG, PRODTST_DATA_NOTIFY_CONFIG_INFO, config);
        }
    }
    
__exit:
    return ret;
}
