#ifndef __HARMONY_BLE_DATA_H__
#define __HARMONY_BLE_DATA_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"
#include "harmony_ble_comm.h"
#include "harmony_ble_trans.h"

#define HARMONY_SERVICE_NETCFGVER	    "netCfgVer"
#define HARMONY_SERVICE_DEVICEINFO	    "deviceInfo"
#define HARMONY_SERVICE_AUTHSETUP	    "authSetup"
#define HARMONY_SERVICE_CREATSESS	    "createSession"
#define HARMONY_SERVICE_CUSTOMSECDATA	"customSecData"




#define HARMONY_BODY_ERRCODE	       "errcode"

#define HARMONY_BODY_KET_VENDOR        "vendor"

#define HARMONY_BODY_NAME_VER           "ver"

#define HARMONY_BODY_NAME_PRODUCTID     "productId"
#define HARMONY_BODY_NAME_SN            "sn"
#define HARMONY_BODY_NAME_DEVID         "devId"
#define HARMONY_BODY_NAME_MODEL         "model"
#define HARMONY_BODY_NAME_MANU          "manu"
#define HARMONY_BODY_NAME_PRODID        "prodId"
#define HARMONY_BODY_NAME_MAC           "mac"
#define HARMONY_BODY_NAME_BLEMAC        "bleMac"
#define HARMONY_BODY_NAME_FWR           "fwv"
#define HARMONY_BODY_NAME_HWR           "hwv"
#define HARMONY_BODY_NAME_PROTT         "prot_t"

#define HARMONY_BODY_NAME_SEQ           "seq"
#define HARMONY_BODY_NAME_SN1           "sn1"
#define HARMONY_BODY_NAME_SN2           "sn2"
#define HARMONY_BODY_NAME_UUID          "uuid"
#define HARMONY_BODY_NAME_SESSID        "sessId"

#define HARMONY_BODY_NAME_AUTHCODE       "authCode"
#define HARMONY_BODY_NAME_AUTHCODEID     "authCodeId"

#define HARMONY_BODY_NAME_SID           "sid"
#define HARMONY_BODY_NAME_DATA          "data"
#define HARMONY_BODY_NAME_ALLSERVICE    "allservices"


typedef enum{
    HARMONY_BLE_SERVICE_TYPE_NONE,
    HARMONY_BLE_SERVICE_TYPE_NETCFGVER, 
    HARMONY_BLE_SERVICE_TYPE_DEVICEINFO, 
    HARMONY_BLE_SERVICE_TYPE_AUTHSETUP, 
    HARMONY_BLE_SERVICE_TYPE_CREATSESS, 
    HARMONY_BLE_SERVICE_TYPE_CUSTOMSECDATA,
    HARMONY_BLE_SERVICE_TYPE_MAX
}harmony_ble_service_type_t;

typedef struct 
{
	uint8_t version; //协议版本号
}harmony_ble_netcfgver_info_t;

typedef struct 
{
	char *sn; //mac地址
    char *model; //设备型号
    char *manu;  //厂商名称
    char *productid;  //华为给厂商分配的产品唯一标识
    char *fw_ver;   //固件版本号
    char *hw_ver;   //硬件版本号
}harmony_ble_device_info_t;

typedef struct 
{
	int seq;
    char uuid[64]; 
    uint8_t sn1[8];
    uint8_t sn2[8]; 
    uint8_t sessid[32]; 
    uint8_t authcodeid[16]; 
}harmony_ble_session_info_t;

typedef struct 
{
	int seq;
    int error_code;
    int get_allservice;
    harmony_ble_opt_type_t opt;

    uint8_t iv[HARMONY_BLE_CIPHER_IV_LEN];

    char *productid;
    uint8_t *hmac; 
    uint8_t *sessid; 
    uint8_t *sesskey; 
    harmony_ble_profile_t profile;

}harmony_ble_custom_info_t;

typedef struct 
{
    uint8_t msg_id;
	harmony_ble_service_type_t service_type;
    union{
        harmony_ble_netcfgver_info_t netcfgver_info;
        harmony_ble_device_info_t device_info;
        harmony_ble_session_info_t session_info;
        harmony_ble_custom_info_t custom_info;
    }data;
    char *up_data;
    int up_data_len;
    void *arg;
}harmony_ble_data_info_t;

typedef int (*harmony_ble_data_callback_t)(harmony_ble_service_type_t msg_type, void *arg);

harmony_ble_service_type_t harmony_ble_service_type_get(char *service_name, int len);

int harmony_ble_data_handle_register(harmony_ble_data_callback_t data_cb, harmony_ble_comm_param_t *param);

int harmony_ble_data_unpack(harmony_ble_service_type_t msg_type, harmony_ble_trans_info_t *rx_info, harmony_ble_data_info_t *data_info);
int harmony_ble_data_handle(harmony_ble_data_info_t *data_info);
int harmony_ble_data_pack_transmit(harmony_ble_data_info_t *data_info);
int harmony_ble_data_destroy(harmony_ble_data_info_t *data_info);

#ifdef __cplusplus
}
#endif

#endif