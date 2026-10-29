#include "prodtst_path.h"
#include "qm_types.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "prodtst_res.h"


#define LOG_TAG "prodtst path"

//path /box:uart:1/hub:uart:2
qm_list_t *prodtst_path_unpack(char *path, int len)
{
    char *str = path;
    int offset = 0;
    char tmp = 0;
    prodtst_addr_t *addr = NULL;
    char str_main_type[PRODTST_MAIN_TYPE_MAX_LEN + 1] = {0};
    char str_hd_type[PRODTST_HD_TYPE_MAX_LEN + 1] = {0};
    int id = 0;
    prodtst_main_type_t main_type = 0;
    prodtst_hd_type_t hd_type = 0;
    qm_list_t *addr_list = NULL;
    qm_list_node_t *node = NULL;

    if(path == NULL || len == 0){
        return NULL;
    }

    addr_list = qm_list_new();
    if(addr_list == NULL){
        return NULL;
    }

    while(str && offset < len ){
        tmp = *(str + offset);
        if(tmp == '/'){
            node = qm_list_node_extra_new(sizeof(prodtst_addr_t));
            if(node == NULL){
                goto __exit;
            }

            qm_list_rpush(addr_list, node);

            addr = (prodtst_addr_t*)node->val;

            memset(str_main_type, 0, PRODTST_MAIN_TYPE_MAX_LEN + 1);
            memset(str_hd_type, 0, PRODTST_HD_TYPE_MAX_LEN + 1);

            sscanf(str + offset, "/%[^:]:%[^:]:%d", str_main_type, str_hd_type, &id);

            QM_LOGD(LOG_TAG, "main type: %s", str_main_type);
            QM_LOGD(LOG_TAG, "hd type:   %s", str_hd_type);
            QM_LOGD(LOG_TAG, "id:        %d", id);

            main_type = prodtst_main_type_get(str_main_type);
            if(main_type == PRODTST_MAIN_TYPE_NONE){
                QM_LOGE(LOG_TAG, "prodtst main type get error");
                goto __exit;
            }

            hd_type = prodtst_hd_type_get(str_hd_type);
            if(hd_type == PRODTST_HD_TYPE_NONE){
                QM_LOGE(LOG_TAG, "prodtst hd type get error");
                goto __exit;
            }

            addr->main_type = main_type;
            addr->hd_type = hd_type;
            addr->id = id;
        }
        offset += 1;
    }
    
    return addr_list;

__exit:
    if(addr_list){
        qm_list_destroy(addr_list);
    }
    return NULL;
}


int prodtst_path_pack(qm_list_t *addr_list, char *path, int *len)
{
    int offset = 0;
    int path_len = 0;
    qm_err_t ret = QM_EOK;
    prodtst_addr_t *addr = NULL;
    char *str_main_type = NULL;
    char *str_hd_type = NULL;
    qm_list_node_t *node = NULL;
    qm_list_iterator_t *self = NULL;

    if(addr_list == NULL || len == NULL){
        return -QM_EINVAL;
    }

    self = qm_list_iterator_new(addr_list, LIST_HEAD);
    if(self == NULL){
        return -QM_ENOMEM;
    }

    node = qm_list_iterator_next(self);

    while(node){
       addr = (prodtst_addr_t*)node->val;

       str_main_type = prodtst_main_type_str_get(addr->main_type);
        if(str_main_type == NULL){
            QM_LOGE(LOG_TAG, "prodtst main type str get error");
            ret =  -QM_EINVAL;
            goto __exit;
        }
        path_len += strlen(str_main_type);
        str_hd_type = prodtst_hd_type_str_get(addr->hd_type);
        if(str_hd_type == NULL){

            QM_LOGE(LOG_TAG, "prodtst hd type str get error");
            ret =  -QM_EINVAL;
            goto __exit;
        }
        path_len += strlen(str_hd_type);
        //include /,:,:id
        path_len += 4;
        if(path){
            offset += qm_snprintf(path + offset, *len - offset, "/%s:%s:%d", str_main_type, str_hd_type, addr->id);
        }

        node = qm_list_iterator_next(self);
    }

    *len = path_len + 1;
    
__exit:
    if(self){
        qm_list_iterator_destroy(self);
        self = NULL;
    }
    return ret;
}

int prodtst_path_destroy(qm_list_t *addr_list)
{
    if(addr_list == NULL){
        return -QM_EINVAL;
    }
    qm_list_destroy(addr_list);
    return QM_EOK;
}

prodtst_addr_t *prodtst_addr_first_get(qm_list_t *addr_list)
{
    prodtst_addr_t *addr = NULL;
    qm_list_node_t *node = NULL;
    qm_list_iterator_t *self = NULL;
    self = qm_list_iterator_new(addr_list, LIST_HEAD);
    if(self == NULL){
        return NULL;
    }
    node = qm_list_iterator_next(self);
    addr = (prodtst_addr_t*)node->val;
    qm_list_iterator_destroy(self);

    return addr;
}

prodtst_addr_t *prodtst_addr_last_get(qm_list_t *addr_list)
{
    prodtst_addr_t *addr = NULL;
    qm_list_node_t *node = NULL;
    qm_list_iterator_t *self = NULL;
    self = qm_list_iterator_new(addr_list, LIST_TAIL);
    if(self == NULL){
        return NULL;
    }
    node = qm_list_iterator_next(self);
    addr = (prodtst_addr_t*)node->val;
    qm_list_iterator_destroy(self);

    return addr;
}

int prodtst_addr_is_dest(qm_list_t *addr_list)
{
    return qm_list_len_get(addr_list) == 1;
}

int prodtst_addr_list_compare(qm_list_t *a, qm_list_t *b)
{
    int ret = QM_EOK;
    char *a_path = NULL;
    int a_len = 0;
    
    char *b_path = NULL;
    int b_len = 0;

    if(a == NULL || b == NULL){
        return -QM_EINVAL;
    }

    if(a->len != b->len){
        return -QM_ERROR;
    }

    ret = prodtst_path_pack(a, NULL, &a_len);
    if(ret != QM_EOK){
        return ret;
    }

    ret = prodtst_path_pack(b, NULL, &b_len);
    if(ret != QM_EOK){
        return ret;
    }

    if(a_len != b_len){
        return -QM_ERROR;
    }

    a_path = (char*)qm_malloc(a_len);
    if(a_path == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    memset(a_path, 0, a_len);

    b_path = (char*)qm_malloc(b_len);
    if(b_path == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    memset(b_path, 0, b_len);

    ret = prodtst_path_pack(a, a_path, &a_len);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = prodtst_path_pack(b, b_path, &b_len);
    if(ret != QM_EOK){
        goto __exit;
    }

    if(0 == strcmp(a_path, b_path)){
        ret = QM_EOK;
    }else{
        ret = -QM_ERROR;
    }

__exit:
    if(a_path){
        qm_free(a_path);
        a_path = NULL;
    }
    if(b_path){
        qm_free(b_path);
        b_path = NULL;
    }
    return ret;
}

int prodtst_addr_compare(prodtst_addr_t *a, prodtst_addr_t *b)
{
    if(a == NULL || b == NULL){
        return -QM_EINVAL;
    }

    if(a->main_type == b->main_type || a->hd_type == b->hd_type || a->id == b->id){
        return QM_EOK;
    }else{
        return -QM_ERROR;
    }
}

qm_list_t *prodtst_addr_list_copy(qm_list_t *src)
{
    qm_list_t *dest = NULL;
    prodtst_addr_t *addr = NULL;
    qm_list_node_t *node = NULL;
    qm_list_node_t *dest_node = NULL;
    prodtst_addr_t *dest_addr = NULL;
    qm_list_iterator_t *self = NULL;

    dest = qm_list_new();
    if(dest == NULL){
        return NULL;
    }

    self = qm_list_iterator_new(src, LIST_HEAD);
    if(self == NULL){
        goto __exit;
    }
    node = qm_list_iterator_next(self);
    while(node){

        addr = (prodtst_addr_t*)qm_list_node_val_get(node);

        dest_node = qm_list_node_extra_new(sizeof(prodtst_addr_t));
        if(dest_node == NULL){
            goto __exit;
        }

        dest_addr = (prodtst_addr_t*)qm_list_node_val_get(dest_node);

        prodtst_addr_copy(dest_addr, addr);

        qm_list_rpush(dest, dest_node);

        node = qm_list_iterator_next(self);
    }

    qm_list_iterator_destroy(self);
    self = NULL;

    return dest;

__exit:
    if(self){
        qm_list_iterator_destroy(self); 
    }
    if(dest){
        qm_list_destroy(dest);
    }
    return NULL;
}

int prodtst_addr_copy(prodtst_addr_t *dest, prodtst_addr_t *src)
{
    if(dest == NULL || src == NULL){
        return -QM_EINVAL;
    }

    dest->main_type = src->main_type;
    dest->hd_type = src->hd_type;
    dest->id = src->id;
    return QM_EOK;
}

int prodtst_path_dyn_add(qm_list_t *addr_list, prodtst_addr_t *addr)
{   
    qm_list_node_t *node = NULL;
    if(addr_list == NULL || addr == NULL){
        return -QM_EINVAL;
    }

    node = qm_list_node_extra_new(sizeof(prodtst_addr_t));
    if(node == NULL){
       return -QM_ENOMEM;
    }

    qm_list_rpush(addr_list, node);
    return QM_EOK;
}