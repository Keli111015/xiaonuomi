#include "prodtst_path.h"
#include "qm_types.h"
#include "qm_kernel.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "prodtst_channel.h"


#define LOG_TAG "prodtst path"

//path /box:1/uart:2
int prodtst_path_unpack(char *path, int len, prodtst_addr_t *addr)
{
    qm_err_t ret = QM_EOK;
    char str_main_type[PRODTST_MAIN_TYPE_MAX_LEN + 1] = {0};
    char str_hd_type[PRODTST_HD_TYPE_MAX_LEN + 1] = {0};
    int main_id = 0;
    int sub_id = 0;
    prodtst_main_type_t main_type = 0;
    prodtst_hd_type_t hd_type = 0;

    if(path == NULL || len == 0 || addr == NULL){
        return -QM_EINVAL;
    }

    memset(str_main_type, 0, PRODTST_MAIN_TYPE_MAX_LEN + 1);
    memset(str_hd_type, 0, PRODTST_HD_TYPE_MAX_LEN + 1);

    sscanf(path, "/%[^:]:%d/%[^:]:%d", str_main_type, &main_id, str_hd_type, &sub_id);

    main_type = prodtst_main_type_get(str_main_type);
    if(main_type == PRODTST_MAIN_TYPE_NONE){
        QM_LOGE(LOG_TAG, "prodtst main type get error");
        ret = -QM_EINVAL;
        goto __exit;
    }

    hd_type = prodtst_hd_type_get(str_hd_type);
    if(hd_type == PRODTST_HD_TYPE_NONE){
        QM_LOGE(LOG_TAG, "prodtst hd type get error");
        ret = -QM_EINVAL;
        goto __exit;
    }

    addr->main_type = main_type;
    addr->main_id = main_id;
    addr->hd_type = hd_type;
    addr->sub_id = sub_id;

    return QM_EOK;

__exit:
    return ret;
}


int prodtst_path_pack(prodtst_addr_t *addr, char *path, int *len)
{
    int path_len = 0;
    qm_err_t ret = QM_EOK;
    char *str_main_type = NULL;
    char *str_hd_type = NULL;

    if(path == NULL || len == 0 || addr == NULL){
        return -QM_EINVAL;
    }

    str_main_type = prodtst_main_type_str_get(addr->main_type);
    if(str_main_type == NULL){
        QM_LOGE(LOG_TAG, "prodtst main type str get error");
        ret =  -QM_EINVAL;
        goto __exit;
    }
    str_hd_type = prodtst_hd_type_str_get(addr->hd_type);
    if(str_main_type == NULL){
        QM_LOGE(LOG_TAG, "prodtst hd type str get error");
        ret =  -QM_EINVAL;
        goto __exit;
    }
    path_len = qm_snprintf(path, *len, "/%s:%d/%s:%d", str_main_type, addr->main_id, str_hd_type, addr->sub_id);
    *len = path_len;

__exit:
    return ret;
}

int prodtst_addr_copy(prodtst_addr_t *dest, prodtst_addr_t *src)
{
    if(dest == NULL || src == NULL){
        return -QM_EINVAL;
    }

    dest->main_type = src->main_type;
    dest->main_id = src->main_id;
    dest->hd_type = src->hd_type;
    dest->sub_id = src->sub_id;
    return QM_EOK;
}