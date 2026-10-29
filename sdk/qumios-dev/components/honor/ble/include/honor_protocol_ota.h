/*
* Copyright (c) Honor Technologies Co.,Ltd. 2022-2023. All rights reserved.
* Description: BLE南向开放ota协议
*/
#ifndef __HONOR_PROTOCOL_OTA_H__
#define __HONOR_PROTOCOL_OTA_H__

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>




#define BLE_DEVICE_OTA_SERVICE 0x03
#define BLE_OTA_ERRCODE_TYPE 0x7F

#define SHA256_VALUE_LEN 64
#define BLE_OTA_PROTOCOL_TYPE_LEN 1
#define BLE_OTA_PROTOCOL_LENGTH_LEN 1
#define BLE_OTA_PROTOCOL_VALUE_LEN_ONE 1
#define BLE_OTA_PROTOCOL_VALUE_LEN_TWO 2
#define BATTERY_UPGRADE_THRESHOLD 40

enum ota_message_type
{
    BLE_OTA_MESSAGE_TYPE_1 = 1,
    BLE_OTA_MESSAGE_TYPE_2,
    BLE_OTA_MESSAGE_TYPE_3,
    BLE_OTA_MESSAGE_TYPE_4,
    BLE_OTA_MESSAGE_TYPE_5,
};

#define OTA_STATUS_UPGRADE_DONE 0x01

/* ota service cid */
enum ota_serive_cid
{
    BLE_DEVICE_OTA_GET_CURRENT_VERSION = 1,
    BLE_DEVICE_OTA_CHECK_UPGRADE,
    BLE_DEVICE_OTA_PARAMETER_NEGOTIATE,
    BLE_DEVICE_OTA_SEND_DATA ,
    BLE_DEVICE_OTA_CHECK_ACK,
    BLE_DEVICE_OTA_STOP_UPGRADE ,
    BLE_DEVICE_OTA_EXCEPTION_REPORT,
    BLE_DEVICE_OTA_STATUS_REPORT ,
};

#define OTA_PER_PACKAGE_SIZE 128
#define SHA256_CHECK_TIMER_LEN 3
#define OTA_RESTART_TIMER_LEN 20
#define OTA_PUSH_PACKAGE_IINTERVAL 25
#define FLASH_SEC_SIZE 256

typedef enum
{
    OTA_OK,
    OTA_CONTROL_DEVICE_UNKNOW_SID = 2,
    OTA_CONTROL_DEVICE_UNKNOW_CID,
    OTA_PACKAGE_TOO_LARGE = 1001,
    OTA_GET_FIRMVARE_VERSION_FAIL,
    OTA_GET_MCU_VERSION_FAIL,
    OTA_VERSION_NUM_INVALID,
    OTA_BATTERY_TOO_LOW,
    OTA_FIRMVARE_SHA256_CHECK_FAIL,
    OTA_DEVICE_PROCESS_FAIL,
    OTA_MCU_PREPARE_OTA_FAIL,
    OTA_SAVE_PACKAGE_FAIL,
    OTA_MCU_UPGRADE_FAIL,
} OtaErrCode;

typedef enum
{
    OTA_CID_GET_CUR_VER = 0x01,
    OTA_CID_UPGRADE_PREPARE,
    OTA_CID_PAR_NEGOTIATION,
    OTA_CID_PACKAGE_RECV,
    OTA_CID_PACKAGE_CHECK_ACK,
    OTA_CID_STOP_UPGRADE,
    OTA_CID_EXCEPTION_REPORT,
    OTA_OTA_STATUS_REPORT
} OtaServiceCid;

typedef enum
{
    DOWNLOAD_PACKAGE_FIRST,
    DOWNLOAD_PACKAGE_MIDDLE,
    DOWNLOAD_PACKAGE_LAST
} DownLoadPackageType;

typedef enum
{
    OTA_PACKAGE_PUSH,
    OTA_PACKAGE_PULL,
} OtaPackagePushType;

typedef enum
{
    OTA_UPGRADE_MUC_MODULE = 0x00,
    OTA_UPGRADE_MODULE,
    OTA_UPGRADE_MCU,
    OTA_UPGRADE_BUTT
} OtaUpgradeType;

typedef enum
{
    OTA_PACKAGE_CHECK_FAIL = 0x00,
    OTA_PACKAGE_CHECK_SUCCESS,
} OtaPackageCheckResult;

typedef struct {
    uint8_t sof;
    uint16_t len;
    uint8_t ctrl;
    uint8_t reqId;
    uint8_t fsn;
    uint8_t sid;
    uint8_t cid;
    uint8_t* head;
    uint16_t headLen; // 包括headLen本身
    uint8_t* data;
    uint16_t dataLen; // payload长度，不写入TLV
    bool needFree;
} OtaFrame;

int8_t ProcOtaAppData(void* frame);

/* 模组注册回调 */
typedef int8_t (*OtaGetCurFwVerFunc)(uint8_t **buf, uint8_t *len);
typedef uint32_t (*GetPkgSaveSizeFunc)();
typedef int8_t (*OtaProcInstallFunc)();
typedef int8_t (*OtaWritePkgFunc)(unsigned int offset, const unsigned char* pkgData, unsigned int pkgDataLen);
typedef uint8_t (*GetBatteryLevelFunc)();
void RegisterOtaGetCurFwVerFunc(OtaGetCurFwVerFunc otaGetCurFwVer);
void RegisterOtaWritePkgFunc(OtaWritePkgFunc otaWritePkgFunc);
void RegisterGetPkgSaveSizeFunc(GetPkgSaveSizeFunc getPkgSaveSizeFunc);
void RegisterOtaProcInstallFunc(OtaProcInstallFunc otaProcInstallFunc);
void RegisterGetBatteryLevelFunc(GetBatteryLevelFunc getBatteryLevelFunc);

/* MCU注册回调 */
typedef int8_t (*McuOtaEndFunc)(void);
typedef int8_t (*McuOtaStartFunc)(const char *version, uint8_t verLen, unsigned int pkgSize, const unsigned char* sha256,
    unsigned int sha256Len);
typedef int8_t (*OtaGetCurMcuVerFunc)(uint8_t **buf, uint8_t *len);
typedef int8_t (*McuOtaPackageRecvFunc)(unsigned int offset, const unsigned char* pkgData, unsigned int pkgDataLen);
typedef int8_t (*McuOtaProcInstallFunc)();
typedef void (*NotifyMcuOtaExceptionFunc)(int status, const char* str);
void RegisterMcuOtaEndFunc(McuOtaEndFunc mcuOtaEndFunc);
void RegisterMcuOtaStartFunc(McuOtaStartFunc mcuOtaStartFunc);
void RegisterOtaGetCurMcuVerFunc(OtaGetCurMcuVerFunc otaGetCurMcuVer);
void RegisterMcuOtaRecvPkgFunc(McuOtaPackageRecvFunc mcuOtaPackageRecvFunc);
void RegisterMcuOtaProcInstallFunc(McuOtaProcInstallFunc mcuOtaProcInstallFunc);
void RegisterNotifyMcuOtaExceptionFunc(NotifyMcuOtaExceptionFunc notifyMcuOtaExceptionFunc);

/* 协议注册回调 */
typedef int8_t (*SendFrame2AppFunc)(void *frame);
typedef void (*GetUint8ArrayFromTLVFunc)(const uint8_t *tlv, uint16_t *pos, uint8_t **buffer, uint16_t *bufLen);
void RegisterSendFrame2AppFunc(SendFrame2AppFunc sendFrame2AppFunc);
void RegisterGetUint8ArrayFromTLVFunc(GetUint8ArrayFromTLVFunc getUint8ArrayFromTLVFunc);
void ReportOtaError(OtaErrCode errCode);
#endif