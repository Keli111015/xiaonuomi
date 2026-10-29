/*
* Copyright (c) Honor Technologies Co.,Ltd. 2022-2023. All rights reserved.
* Description: BLE南向开放协议（包括广播、链接、配对、绑定、注册、信息读写、主动上报等）
*/
#ifndef __HONOR_PROTOCOL_H__
#define __HONOR_PROTOCOL_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "honor_protocol_ota.h"

//这里面包含firmware version
#include "generic_serial_factor.h"

#define BLE_ADV_DATA_LEN 23


#define BLE_DEVICE_CONNECT_SERVICE 0x01
#define BLE_DEVICE_CONTROL_SERVICE 0x02
#define BLE_DEVICE_OTA_SERVICE 0x03

#define BLE_DEVICE_CONNECT_PARAMETER_NEGOTIATE 0x01
#define BLE_DEVICE_CONNECT_CERTIFICATE_APPROVED 0x02
#define BLE_DEVICE_CONNECT_SET_CONFIG 0x03
#define BLE_DEVICE_CONNECT_GET_CHALLENGE_CODE 0x04
#define BLE_DEVICE_CONNECT_REGISTER 0x05
#define BLE_DEVICE_CONNECT_DELETE 0x06
#define BLE_DEVICE_CONTROL_READ 0x01
#define BLE_DEVICE_CONTROL_WRITE 0x02

enum ota_service_info
{
    BLE_DEVICE_GET_VERSION_INFO = 0x01, //获取ota版本
    BLE_DEVICE_GET_CHECK_IS_OTA,        //查询是否可升级
    BLE_DEVICE_GET_OTA_PARAM,           //升级参数协商
    BLE_DEVICE_GET_OTA_PACKAGE,         //升级包发送
    BLE_DEVICE_REPORT_OTA_PACKAGE_CHECKSUM, //升级包校验
    BLE_DEVICE_GET_OTA_STOP,                //停止设备升级下发
    BLE_DEVICE_REPORT_OTA_ABNORAML,         //升级异常上报
    BLE_DEVICE_REPORT_OTA_STATUS            //升级状态上报

};

#define BLE_DEVICE_CONTROL_REPORT_PROPERTY 0x03
#define BLE_DEVICE_CONTROL_REPORT_EVENT 0x04

#define BLE_MESSAGE_TYPE_1 0x01
#define BLE_MESSAGE_TYPE_2 0x02
#define BLE_MESSAGE_TYPE_3 0x03
#define BLE_MESSAGE_TYPE_4 0x04
#define BLE_MESSAGE_TYPE_5 0x05
#define BLE_MESSAGE_TYPE_6 0x06
#define BLE_MESSAGE_TYPE_7 0x07
#define BLE_MESSAGE_TYPE_8 0x08
#define BLE_MESSAGE_TYPE_9 0x09
#define BLE_MESSAGE_TYPE_10 0x0A

#define BLE_PIN_LEN 6
#define BLE_RANDOM_LEN 8
#define BLE_AUTH_CODE_LEN 6
#define BLE_HMAC_SHA256_LEN 32
#define BLE_CALCULATE_PIN_MAC_LEN 2

#define BLE_PROTOCOL_TYPE_LEN 1
#define BLE_PROTOCOL_LENGTH_LEN 1
#define BLE_PROTOCOL_VALUE_LEN_ONE 1
#define BLE_PROTOCOL_VALUE_LEN_TWO 2

#define BLE_READ_PROPERTY_MAX_COUNT 10

#define BLE_SN_LEN 8
#define BLE_MAC_LEN 6
#define BLE_SALT_LEN 4

#define BLE_PRODUCT_ID_LEN 15
#define BLE_PRODUCT_KEY_LEN 32
#define BLE_PRODUCT_SECRET_LEN 32

#define BLE_SUB_PRODUCT_ID_LEN 1
#define BLE_MANUFACTURE_INFO_LEN 1
#define BLE_MANUFACTURE_UUID_LEN 2

#define DEV_NAME_MAX_LEN 25

#define BLE_LICENSE_KEY_LEN 40
#define BLE_LICENSE_SECRET_LEN 64

#define BLE_BOND_REQ 0x01
#define BLE_CONNECT_REQ 0x02

#define BLE_ERRCODE_LEN 4
#define BLE_ERRCODE_TYPE 0x7F

#define BLE_AUTH_TYPE_HMACSHA256 0x01 // hmacsha256

#define BLE_DEVICE_LOGIN 0x00
#define BLE_DEVICE_DELETE 0x01
#define BLE_DEVICE_REGISTER_SUCCEED 0x01

#define BLE_LENGTH_ONE_BIT_MAX 0x7F // 单字节最大值126，最高位为表示后一字节是否依然表示长度
#define BLE_BIT_MAX_VALUE_UINT8 0xFF
#define BLE_BIT_MAX_VALUE_UINT16_LOW 0x00FF
#define BLE_BIT_MAX_VALUE_UINT16_HIGH 0xFF00

#define BLE_PROTOCOL_VERSION 0x01 // protocol version

#define TRANS_PROTO_SOF 0xA5
#define SOF_SIZE 1
#define LEN_SIZE 2
#define CTRL_SIZE 1
#define SID_SIZE 1
#define CID_SIZE 1
#define FSN_SIZE 1
#define REQID_SIZE 2

typedef enum {
    BLE_NORMAL_SEND,
    BLE_HONOR_SEND,
    BLE_SEND_BUTT,
} BLE_SEND_TYPE;

typedef enum {
    BLE_PRODUCT_VERIFY,
    BLE_LICENSE_VERIFY,
} BLE_VERIFY_METHOD;

typedef enum {
    BLE_DEVICE_REPORT_PROPERTY = 0x03,
    BLE_DEVICE_REPORT_EVENT,
} BLE_DEVICE_REPORT_TYPE;



#define FIRMWARE_VERSION    XJIANG_FIRMWARE_VERSION_HONOR_TEMP
#define MCU_VERSION         "0.1"

#define HARDWARE_VERSION    "1.0.0"

#define MANU_NAME_LEN 50
#define DEV_MODEL_LEN 25
#define SN_LEN  12
#define MAC_STR_LEN 17
#define FIRMWARE_VER_LEN 10
#define HARDWARE_VER_LEN 10
#define MCU_VER_LEN 10

typedef struct
{
    uint8_t manu_name[MANU_NAME_LEN + 1];//厂商名称
    uint8_t dev_model[DEV_MODEL_LEN + 1];//产品型号
    uint8_t sn[SN_LEN + 1];
    uint8_t mac_str[MAC_STR_LEN + 1]; //11:11:11:11:11:11
    uint8_t firmware_ver[FIRMWARE_VER_LEN + 1];
    uint8_t hardware_ver[HARDWARE_VER_LEN + 1];
    uint8_t mcu_ver[MCU_VER_LEN + 1];
}product_info_t;


typedef void (*BleSendFunc)(const uint8_t *data, uint16_t dataLen);
typedef void (*GenerateRandom)(uint8_t *data, uint16_t dataLen);
typedef int8_t (*ReadAuthCode)(uint8_t *data, uint16_t dataLen);
typedef int8_t (*WriteAuthCode)(const uint8_t *data, uint16_t dataLen);
typedef int8_t (*GetProperty)(uint8_t service, uint8_t property, uint8_t **data, uint16_t *dataLen);
typedef int8_t (*SetProperty)(uint8_t service, uint8_t property, const uint8_t *data, uint16_t dataLen);
typedef int8_t (*AckWriteReportProperty)(uint8_t service, uint8_t property, const uint8_t *data, uint16_t dataLen);
typedef void (*CfgAdvData)(uint8_t *advData, uint16_t advDataLen);
typedef void (*RmvBondDev)();



typedef struct {
    const char *checkSalt;
    const char *calculateSalt;
    const char *prdId;
    const char *prdKey;
    const char *prdSecret;
    const char *licenseKey;
    const char *licenseSecret;
    const char *subPrdId;
    const uint8_t *macAddress;
    uint16_t macAddressLen;
    uint8_t authCodeId;
    uint16_t mtu;
    uint16_t maxFrameSize;
    uint16_t interval;
    BLE_VERIFY_METHOD verifyMethod;
} BleInitParam;


typedef struct 
{
    char prdId[BLE_PRODUCT_ID_LEN + 1];
    char prdKey[BLE_PRODUCT_KEY_LEN + 1];
    char prdSecret[BLE_PRODUCT_SECRET_LEN + 1];
    char licenseKey[BLE_LICENSE_KEY_LEN + 1];
    char licenseSecret[BLE_LICENSE_SECRET_LEN + 1];
    char subPrdId[BLE_SUB_PRODUCT_ID_LEN + 1];
    char devName[DEV_NAME_MAX_LEN + 1];
}honor_product_param_t;


typedef struct {
    BleSendFunc bleHonorSendFunc;
    BleSendFunc bleNormalSendFunc;
    GenerateRandom genRandomFunc;
    ReadAuthCode readAuthCodeFunc;
    WriteAuthCode writeAuthCodeFunc;
    GetProperty getPropertyFunc;
    SetProperty setPropertyFunc;
    AckWriteReportProperty AckWriteReportPropertyFunc;
    CfgAdvData cfgAdvDataFunc;
    RmvBondDev rmvBondDevFunc;
} BleInitCallback;

typedef struct {
    OtaGetCurFwVerFunc otaGetCurFwVer;
    OtaProcInstallFunc otaProcInstallFunc;
    GetPkgSaveSizeFunc getPkgSaveSizeFunc;
    GetBatteryLevelFunc getBatteryLevelFunc;
    OtaWritePkgFunc otaWritePkgFunc;
    McuOtaEndFunc mcuOtaEndFunc;
    McuOtaStartFunc mcuOtaStartFunc;
    OtaGetCurMcuVerFunc otaGetCurMcuVer;
    McuOtaPackageRecvFunc mcuOtaPackageRecvFunc;
    McuOtaProcInstallFunc mcuOtaProcInstallFunc;
    NotifyMcuOtaExceptionFunc notifyMcuOtaExceptionFunc;
} BleInitOtaCallback;


typedef struct _honor_property_operation
{
    uint8_t            siid;
    uint8_t            prop_idx;
    uint8_t            type_id;
    uint8_t            attr_id;
    uint8_t            *value;
    uint8_t            value_len;
} property_operation_t;

enum
{
    REGISTERED_STATUS = 0,
    UNREGISTERED_STATUS,
};


int8_t HonorBleInit(const BleInitParam *param, const BleInitCallback *callback, const BleInitOtaCallback *otaCallback);
void HonorBleDeinit();
uint8_t dev_register_status_get(void);
// 设备主动上报property或者event，用倒数第一个参数区分，指定serviceId，serviceLen为需要上报数据的总长度（propertyTLV）
void dev_report_value(uint8_t serviceId, uint16_t serviceLen, const uint8_t* value, BLE_DEVICE_REPORT_TYPE reportType);
void ProcessFrame(BLE_SEND_TYPE sendType, const uint8_t* data, uint16_t dataLen);
uint8_t register_status(void);
uint8_t honor_link_status_get(void);

void SetAdvData();

#endif // __PROTOCOL_H__
