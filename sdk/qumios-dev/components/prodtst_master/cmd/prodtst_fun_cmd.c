#include "prodtst_cmd.h"
#include "qm_at.h"
#include "qm_errno.h"
#include "qm_log.h"

#define LOG_TAG "fun cmd"


//static int fun_set_parse(void *rsp, prodtst_data_t **data, int *num);
static int fun_get_parse(void *rsp, prodtst_data_t **data, int *num);


prodtst_cmd_t prodtst_fun_cmd[] = {
    {"serial_get", "AT+FUN", PRODTST_FUN_GET_MAX_LEN, CONFIG_QM_PRODTST_CMD_ACK_DEFAULT_TIMEOUT, NULL, fun_get_parse },
    {"serial_set", "AT+FUN", PRODTST_FUN_SET_MAX_LEN, CONFIG_QM_PRODTST_CMD_ACK_DEFAULT_TIMEOUT, NULL, NULL },
};

#define PRODTST_FUN_CMD_NUM   (sizeof(prodtst_fun_cmd)/sizeof(prodtst_fun_cmd[0]))

prodtst_cmd_t *prodtst_fun_cmd_get(char *type, char *method)
{
    int i = 0;
    char key[32] = {0};
    if(type == NULL || method == NULL){
        return NULL;
    }

    snprintf(key, 32, "%s_%s", type, method);

    for(i = 0; i < PRODTST_FUN_CMD_NUM; i++){
        if((strlen(key) == strlen(prodtst_fun_cmd[i].name)) && 
        (0 == strcmp(key, prodtst_fun_cmd[i].name))){
            return &prodtst_fun_cmd[i];
        }
    }
    return NULL;
}


static int fun_get_parse(void *rsp, prodtst_data_t **data, int *num)
{
    qm_err_t ret = QM_EOK;
    int data_num = 1;
    prodtst_data_t *prodtst_data = NULL;
    qm_at_response_t *at_resp = (qm_at_response_t*)rsp;
    const char *rsp_expr = "%*[^\"]\"%[^\"]\"";

    prodtst_data = prodtst_data_creat(data_num);
    if(prodtst_data == NULL){
        return -QM_ENOMEM;
    }   

    ret = prodtst_data_zalloc(prodtst_data, PRODTST_FUN_GET_MAX_LEN);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_at_client_resp_parse_line_args(at_resp, 1, rsp_expr, prodtst_data->data);
    if(ret <= 0){
        ret = -QM_EINVAL;
        goto __exit;
    }

    QM_LOGI(LOG_TAG, "get data: %s", prodtst_data->data);

    prodtst_name_copy(prodtst_data, "data", strlen("data"));

    *data = prodtst_data; 
    *num = data_num;

    return QM_EOK;

__exit:
    ret = prodtst_data_destroy(prodtst_data, data_num);
    return ret;
}
 