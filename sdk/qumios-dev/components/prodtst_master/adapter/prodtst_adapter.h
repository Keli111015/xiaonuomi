#ifndef __PRODTST_ADAPTER_H__
#define __PRODTST_ADAPTER_H__

#include "qm_types.h"


typedef struct{

    void *(*init)(void *param);
    int (*deinit)(void *handle);
    void *(*establish)(void *param);
    int (*destroy)(void *handle);
    int (*send)(void *handle, char *buf, int size);
    int (*recv)(void *handle, char *buf, int size, int timeout);
}prodtst_adapter_t;
 

extern prodtst_adapter_t wifi_prodtst_adapter;


int prodtst_wifi_ap_event_handler(uint32_t ip);
int prodtst_wifi_ap_name_get(char ssid[32]);

#endif

