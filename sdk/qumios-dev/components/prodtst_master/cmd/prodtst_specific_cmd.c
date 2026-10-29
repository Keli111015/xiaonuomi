#include "prodtst_cmd.h"
#include "qm_at.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "prodtst_core.h"
#include "prodtst_config.h"

#define LOG_TAG "specific cmd"

static int hw_token_parse(void *rsp, prodtst_data_t **data, int *num);
static int hw_cfg_parse(void *rsp, prodtst_data_t **data, int *num);

prodtst_cmd_t prodtst_hw_wifi_specific_cmd[] = {
    {"hw_cfg",    "AT+HW_CFG?",  PRODTST_HW_CONFIG_MAX_LEN, CONFIG_QM_PRODTST_CMD_ACK_HW_CFG_TIMEOUT, NULL, hw_cfg_parse },
    {"hw_token", "AT+HW_TOKEN?", PRODTST_HW_TOKEN_MAX_LEN,  CONFIG_QM_PRODTST_CMD_ACK_DEFAULT_TIMEOUT, NULL, hw_token_parse },
};

//TODO: 
static int hw_token_parse(void *rsp, prodtst_data_t **data, int *num)
{
    int ret = 0;
    int data_num = 1;
    #define AP_RSSI_DATA_MAX_LEN 8
    prodtst_data_t *prodtst_data = NULL;
    qm_at_response_t *at_resp = (qm_at_response_t*)rsp;
    const char *rsp_expr = "%*[^\"]\"%[^\"]\"";

    prodtst_data = prodtst_data_creat(data_num);
    if(prodtst_data == NULL){
        return -QM_ENOMEM;
    }   

    ret = prodtst_data_zalloc(prodtst_data, PRODTST_HW_TOKEN_MAX_LEN);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_at_client_resp_parse_line_args(at_resp, 1, rsp_expr, prodtst_data->data);
    if(ret <= 0){
        ret = -QM_EINVAL;
        QM_LOGE(LOG_TAG, "hw token parse error");
        goto __exit;
    }

    QM_LOGI(LOG_TAG, "hw token: %s", prodtst_data->data);

    prodtst_name_copy(prodtst_data, PRODTST_HW_TOKEN_KEY, strlen(PRODTST_HW_TOKEN_KEY));
    
    *data = prodtst_data; 
    *num = data_num;

    return QM_EOK;

__exit:
    prodtst_data_destroy(prodtst_data, data_num);
    return ret;
}

static int hw_cfg_parse(void *rsp, prodtst_data_t **data, int *num)
{
    int ret = 0;
    char *cfg = NULL;
    int data_num = 1;
    prodtst_data_t *prodtst_data = NULL;
    qm_at_response_t *at_resp = (qm_at_response_t*)rsp;
    char *kw = "+HW_CFG:\"";

    prodtst_data = prodtst_data_creat(data_num);
    if(prodtst_data == NULL){
        return -QM_ENOMEM;
    }   

    cfg = qm_at_client_resp_get_line_by_kw(at_resp, kw);
    if(cfg == NULL){
        ret = -QM_EINVAL;
        QM_LOGE(LOG_TAG, "hw cfg kw not found");
        goto __exit;
    }

    ret = prodtst_data_zalloc(prodtst_data, strlen(cfg));
    if(ret != QM_EOK){
        goto __exit;
    }

    memcpy(prodtst_data->data, cfg+strlen(kw), strlen(cfg)-2-strlen(kw));

    QM_LOGI(LOG_TAG, "hw cfg: %s", prodtst_data->data);

    prodtst_name_copy(prodtst_data, PRODTST_HW_CONFIG_KEY, strlen(PRODTST_HW_CONFIG_KEY));
    
    *data = prodtst_data; 
    *num = data_num;

    return QM_EOK;

__exit:
    prodtst_data_destroy(prodtst_data, data_num);
    return ret;
}


