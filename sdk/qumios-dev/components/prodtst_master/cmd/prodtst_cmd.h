#ifndef __PRODTST_CMD_H__
#define __PRODTST_CMD_H__

#include "prodtst_core.h"

#define PRODTST_FW_VER_KEY             "fw_ver"
#define PRODTST_FW_VER_MAX_LEN           16
#define PRODTST_FW_VER_CMD             "AT+FW_VER?"

#define PRODTST_FW_NAME_KEY             "fw_name"
#define PRODTST_FW_NAME_MAX_LEN          16
#define PRODTST_FW_NAME_CMD             "AT+FW_NAME?"

#define PRODTST_WIFI_STA_MAC_KEY        "wifi_sta_mac"
#define PRODTST_WIFI_STA_MAC_CMD        "AT+MAC_STA?"
#define PRODTST_WIFI_AP_MAC_KEY         "wifi_ap_mac"
#define PRODTST_WIFI_AP_MAC_CMD         "AT+MAC_AP?"
#define PRODTST_MAC_MAX_LEN              24

#define PRODTST_AP_RSSI_KEY             "ap_rssi"
#define PRODTST_AP_RSSI_MAX_LEN          8
#define PRODTST_AP_RSSI_CMD              "AT+RF_W="%s""
#define PRODTST_AP_RSSI_CMD_MAX_LEN      64

#define PRODTST_HW_CONFIG_KEY           "hw_cfg"
#define PRODTST_HW_CONFIG_CMD           "AT+HW_CFG?"
#define PRODTST_HW_CONFIG_MAX_LEN        4096

#define PRODTST_HW_TOKEN_KEY            "hw_token"
#define PRODTST_HW_TOKEN_CMD            "AT+HW_TOKEN?"
#define PRODTST_HW_TOKEN_MAX_LEN         196

#define PRODTST_FUN_GET_KEY                 "get"
#define PRODTST_FUN_GET_CMD                 "AT+FUN"
#define PRODTST_FUN_GET_MAX_LEN              512

#define PRODTST_FUN_SET_KEY                 "set"
#define PRODTST_FUN_SET_CMD                 "AT+FUN"
#define PRODTST_FUN_SET_MAX_LEN              128

#define PRODTST_NFC_KEY                 ""
#define PRODTST_NFC_CMD                 "AT+NFC?"
#define PRODTST_NFC_MAX_LEN              128

extern prodtst_cmd_t prodtst_fun_cmd[];
extern prodtst_cmd_t prodtst_wifi_generic_cmd[];
extern prodtst_cmd_t prodtst_hw_wifi_specific_cmd[];



prodtst_data_t *prodtst_data_creat(int num);
int prodtst_data_destroy(prodtst_data_t *prodtst_data, int num);
int prodtst_name_copy(prodtst_data_t *prodtst_data, char *name, int len);
int prodtst_data_copy(prodtst_data_t *prodtst_data, char *data, int len);
int prodtst_data_zalloc(prodtst_data_t *prodtst_data, int len);

prodtst_cmd_t *prodtst_fun_cmd_get(char *type, char *method);
prodtst_cmd_t *prodtst_misc_cmd_get(char *key);

#endif



