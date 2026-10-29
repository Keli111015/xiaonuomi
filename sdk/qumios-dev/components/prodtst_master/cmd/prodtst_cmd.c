#include "prodtst_cmd.h"
#include "qm_errno.h"
#include "qm_kernel.h"

prodtst_data_t *prodtst_data_creat(int num)
{
    prodtst_data_t *prodtst_data = NULL;
    if(num <= 0){
        return -QM_EINVAL;
    }
    prodtst_data = (prodtst_data_t*)qm_malloc(sizeof(prodtst_data_t)*num);
    if(prodtst_data == NULL){
        return -QM_ENOMEM;
    }  
    memset(prodtst_data, 0, sizeof(prodtst_data_t));
    return prodtst_data;
}

int prodtst_data_destroy(prodtst_data_t *prodtst_data, int num)
{
    prodtst_data_t *m_prodtst_data = NULL;
    if(prodtst_data == NULL || num <= 0){
        return -QM_EINVAL;
    }
    for(int i = 0; i < num; i++){
        m_prodtst_data = (prodtst_data+i);
        if(m_prodtst_data->data){
            qm_free(m_prodtst_data->data);
        }      
        if(m_prodtst_data->name){
            qm_free(m_prodtst_data->name);
        }
    }
    qm_free(prodtst_data);
    return QM_EOK;
}


int prodtst_name_copy(prodtst_data_t *prodtst_data, char *name, int len)
{
    qm_err_t ret = QM_EOK;

    if(prodtst_data == NULL || name == NULL || len <= 0){
        return -QM_EINVAL;
    }
    prodtst_data->name = (char*)qm_malloc(len+1);
    if(prodtst_data->name == NULL){
        return -QM_ENOMEM;
    }

    memset(prodtst_data->name, 0, len+1);
    memcpy(prodtst_data->name, name, len);
    return QM_EOK;
}

int prodtst_data_copy(prodtst_data_t *prodtst_data, char *data, int len)
{
    qm_err_t ret = QM_EOK;
    if(prodtst_data == NULL || data == NULL || len <= 0){
        return -QM_EINVAL;
    }
    prodtst_data->data = qm_malloc(len+1);
    if(prodtst_data->data == NULL){
        return -QM_ENOMEM;
    }

    memset(prodtst_data->data, 0, len+1);
    memcpy(prodtst_data->data, data, len);
    return QM_EOK;
}

int prodtst_data_zalloc(prodtst_data_t *prodtst_data, int len)
{
    qm_err_t ret = QM_EOK;
    if(prodtst_data == NULL || len <= 0){
        return -QM_EINVAL;
    }
    prodtst_data->data = qm_malloc(len+1);
    if(prodtst_data->data == NULL){
        return -QM_ENOMEM;
    }
    memset(prodtst_data->data, 0, len+1);
    return QM_EOK;
}