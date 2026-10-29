#include "prodtst_cloud.h"
#include "qm_errno.h"
#include "cJSON.h"
#include "prodtst_config.h"

int prodtst_generic_pack(prodtst_data_table_t *prodtst_tbl, char **data, int *len)
{
    qm_err_t ret = QM_EOK;
    int i = 0, j = 0;
    cJSON *root = NULL;
    char *json = NULL;
    char *out = NULL;
    prodtst_data_t *prodtst_data = NULL;
    
    if(prodtst_tbl == NULL){
        return -QM_EINVAL;
    }
    root = cJSON_CreateObject();
    if(root == NULL){
        return -QM_ENOMEM;
    }

    for(i = 0; i < CONFIG_QM_PRODTST_DATA_TABLE_NUM; i++){
        prodtst_data = prodtst_tbl->data[i];
        if(prodtst_data == NULL){
            continue;
        }
                
        for(j = 0; j < prodtst_tbl->num[i]; j++){
            prodtst_data = (prodtst_data+j);  
            if(prodtst_data->name && prodtst_data->data && prodtst_data->type == PRODTST_DATA_STRING){
                cJSON_AddStringToObject(root, prodtst_data->name, (char*)prodtst_data->data);
            }

            if(prodtst_data->name && prodtst_data->type == PRODTST_DATA_INT){
                cJSON_AddNumberToObject(root, prodtst_data->name, prodtst_data->int_data);
            }
            //TODO need free json item
            if(prodtst_data->name && prodtst_data->type == PRODTST_DATA_JSON){
                cJSON_AddItemToObject(root, prodtst_data->name, (cJSON*)prodtst_data->data);\
            }
        }
    }

    json = cJSON_PrintUnformatted(root);
    if(json == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    out = (char*)qm_malloc(strlen(json)+1);
    if(out == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    memset(out, 0, strlen(json)+1);
    memcpy(out, json, strlen(json));

    *data = out;
    *len = strlen(out);

__exit:
    if(json){
        cJSON_free(json);
    }
    cJSON_Delete(root);

    return ret;
}








