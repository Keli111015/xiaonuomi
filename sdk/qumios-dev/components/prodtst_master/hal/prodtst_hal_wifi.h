#ifndef __PRODTST_HAL_WIFI_H__
#define __PRODTST_HAL_WIFI_H__

#include "qm_types.h"


typedef enum{
    PRODTST_WIFI_EVENT_AP_STACONNECTED,         
    PRODTST_WIFI_EVENT_AP_STADISCONNECTED,  
    PRODTST_WIFI_EVENT_AP_STAIPASSIGNED,
}prodtst_wifi_event_type_t;

typedef struct 
{
    uint16_t aid;
    uint8_t mac[6]; 
}prodtst_wifi_event_ap_stadisconnected_t;

typedef struct 
{
    uint16_t aid;
    uint8_t mac[6]; 
}prodtst_wifi_event_ap_staconnected_t;

typedef struct {
    uint32_t ip_addr;
}prodtst_wifi_event_ap_staipassigned_t;

typedef union{
    prodtst_wifi_event_ap_staconnected_t sta_connected;
    prodtst_wifi_event_ap_stadisconnected_t sta_disconnected;
    prodtst_wifi_event_ap_staipassigned_t ap_staipassigned;
}prodtst_wifi_event_info_t;

typedef struct 
{
    prodtst_wifi_event_type_t type;
    prodtst_wifi_event_info_t event_info;

}prodtst_wifi_event_t;


typedef void (*prodtst_wifi_event_cb)(prodtst_wifi_event_t *event, void *arg);


int prodtst_wifi_deauth_sta(uint16_t aid);

int prodtst_wifi_ap_init(char *ssid, char *pwd, int channel, int max_conn);

int prodtst_wifi_ap_deinit(void);

int prodtst_wifi_sta_mac_get(uint8_t mac[6]);

int prodtst_wifi_ap_event_register(prodtst_wifi_event_cb cb, void *arg);

int prodtst_wifi_ap_event_set(prodtst_wifi_event_t *event);

#endif


