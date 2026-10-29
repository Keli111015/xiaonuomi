#include "product.h"
#include "qm_iot_import.h"
#include "cloudapi.h"
#include "iot_device.h"

#define QM_KV_PRODUCT_SUPPORT

#ifndef QM_KV_PRODUCT_SUPPORT
static uint32_t PRODUCT_ID = 158689;
static char DEVICE_ID[DEVICE_ID_LEN+1] = "cE5ww5";
static char DEVICE_SECRET[DEVICE_SECRET_LEN+1] = "80cec92c26da40cf85f716668feeb4c4";

#ifdef CONFIG_DYN_REG_SUPPORT
static char PRODUCT_SECRET[PRODUCT_SECRET_LEN+1] = "b81c7d2aa2e04a4391a8129e9af2517c";
#endif

#endif


int hal_get_product_id(unsigned int *product_id)
{
    int len = sizeof(uint32_t);
    unsigned int pid = 0;
    if(product_id == NULL){
        return -1;
    }

#ifdef QM_KV_PRODUCT_SUPPORT
    if (0 != hal_kv_get("product_id", &pid, &len)) {
        return -1;
    }
    *product_id = pid;
    return 0;
#else
    *product_id = PRODUCT_ID; 
    return 0;
#endif

}

int hal_set_product_id(unsigned int product_id)
{
    if(product_id == 0){
        return -1;
    }

#ifdef QM_KV_PRODUCT_SUPPORT
    if (0 != hal_kv_set("product_id", &product_id, sizeof(uint32_t), 1)) {
        return -1;
    }
    return 0;
#else
    PRODUCT_ID = product_id; 
    return 0;
#endif

}


int hal_get_device_secret(char *device_secret, int *len)
{
    if(!device_secret || !len){
        return -1;
    }
    if(*len < DEVICE_SECRET_LEN){
        return -1;
    }
    
#ifdef QM_KV_PRODUCT_SUPPORT
    int tmp_len = DEVICE_SECRET_LEN;
    if (0 != hal_kv_get("device_secret", device_secret, &tmp_len)) {
        return -1;
    }

    *len = tmp_len;
    return 0;
#else
    strncpy(device_secret, DEVICE_SECRET, strlen(DEVICE_SECRET));
    *len = strlen(device_secret);
    return 0;
#endif

}

int hal_set_device_secret(char *device_secret, int len)
{
    if(!device_secret || len > DEVICE_SECRET_LEN){
        return -1;
    }

#ifdef QM_KV_PRODUCT_SUPPORT
    if (0 != hal_kv_set("device_secret", device_secret, len, 1)) {
        return -1;
    }
    return 0;
#else
    memset(DEVICE_SECRET, 0, DEVICE_SECRET_LEN);
    strncpy(DEVICE_SECRET, device_secret, strlen(device_secret));
    return 0;
#endif

}



int hal_get_device_id(char *device_id, int *len)
{
    if(!device_id || !len){
        return -1;
    }
    if(*len < DEVICE_ID_LEN){
        return -1;
    }
    
#ifdef QM_KV_PRODUCT_SUPPORT
    int tmp_len = DEVICE_ID_LEN;
    if (0 != hal_kv_get("device_id", device_id, &tmp_len)) {
        return -1;
    }

    *len = tmp_len;
    return 0;
#else
    strncpy(device_id, DEVICE_ID, strlen(DEVICE_ID));
    *len = strlen(device_id);
    return 0;
#endif

}


int hal_set_device_id(char *device_id, int len)
{    

    if(!device_id || len > DEVICE_ID_LEN){
        return -1;
    }
    
#ifdef QM_KV_PRODUCT_SUPPORT
    if (0 != hal_kv_set("device_id", device_id, len, 1)) {
        return -1;
    }
    return 0;
#else
    memset(DEVICE_ID, 0, DEVICE_ID_LEN);
    strncpy(DEVICE_ID, device_id, strlen(device_id));
    return 0;
#endif

}



#ifdef CONFIG_DYN_REG_SUPPORT

int hal_get_product_secret(char *product_secret, int *len)
{
    if(!product_secret || !len){
        return -1;
    }
    if(*len < PRODUCT_SECRET_LEN){
        return -1;
    }
    
#ifdef QM_KV_PRODUCT_SUPPORT
    int tmp_len = PRODUCT_SECRET_LEN;
    if (0 != hal_kv_get("product_secret", product_secret, &tmp_len)) {
        return -1;
    }

    *len = tmp_len;
    return 0;
#else
    strncpy(product_secret, PRODUCT_SECRET, strlen(PRODUCT_SECRET));
    *len = strlen(product_secret);
    return 0;
#endif


}

int hal_set_product_secret(char *product_secret, int len)
{

    if(!product_secret || len > DEVICE_SECRET_LEN){
        return -1;
    }

#ifdef QM_KV_PRODUCT_SUPPORT
    if (0 != hal_kv_set("product_secret", product_secret, len, 1)) {
        return -1;
    }
    return 0;
#else
    memset(PRODUCT_SECRET, 0, PRODUCT_SECRET_LEN);
    strncpy(PRODUCT_SECRET, product_secret, strlen(product_secret));
    return 0;
#endif

}

#endif
