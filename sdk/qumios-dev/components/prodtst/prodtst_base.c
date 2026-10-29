#include "prodtst_base.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_utils_string.h"

char *cjson_str_get(cJSON *cjson, char *key)
{
    cJSON *tmp = NULL;
    tmp = cJSON_GetObjectItem(cjson, key);
    if(tmp == NULL){
        return NULL;
    }
    return tmp->valuestring;
}

int cjson_int_get(cJSON *cjson, char *key, int *data)
{
    cJSON *tmp = NULL;
    tmp = cJSON_GetObjectItem(cjson, key);
    if(tmp == NULL){
        return -QM_EINVAL;
    }

    *data = tmp->valueint;
    return QM_EOK;
}

void *malloc_and_copy(void *data, int len)
{
    void *tmp = NULL;
    tmp = (char*)qm_malloc(len + 1);
    if(tmp == NULL){
        return NULL;
    }
    memset(tmp, 0 , len + 1);
    memcpy(tmp, data, len);
    
    return tmp;
}

int hexstrs_to_hex(char *str, int len, uint8_t **hex, int *hex_len)
{   
    uint8_t *tmp_hex = NULL;
    int tmp_hex_len = 0;
    if(str == NULL || len == 0){
        return -QM_EINVAL;
    }
    if(len % 2 != 0){
        return -QM_EINVAL;
    }

    tmp_hex_len = len / 2;
    tmp_hex = (uint8_t*)qm_malloc(tmp_hex_len);
    if(tmp_hex == NULL){
        return -QM_ENOMEM;
    }

    hex_str_to_nums(str, len, tmp_hex);
    *hex = tmp_hex;
    *hex_len = tmp_hex_len;
    return QM_EOK;
}

int hex_to_hexstrs(uint8_t *hex, int hex_len, char **str, int *len)
{
    char *lstr = NULL;
    if(hex == NULL || hex_len == 0 || *str == NULL || len == NULL){
        return -QM_EINVAL;
    }

    lstr = (char*)qm_malloc(hex_len *2 + 1);
    if(lstr == NULL){
        return -QM_ENOMEM;
    }
    memset(lstr, 0, hex_len *2 + 1);
    hex_to_strs(hex, hex_len, lstr);
    *str = lstr;
    *len = hex_len*2;
    return QM_EOK;
}