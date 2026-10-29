#ifndef QM_BLE_OPT_H
#define QM_BLE_OPT_H

#include "qm.h"
#include "qm_ble_log.h"

#ifdef CONFIG_QB_QM_COMPANY_ID
#define QB_QM_COMPANY_ID CONFIG_QB_QM_COMPANY_ID     /* company id */
#endif

#ifdef CONFIG_BLE_UUID_QMS_SERVICE
#define BLE_UUID_QMS_SERVICE CONFIG_BLE_UUID_QMS_SERVICE /* The UUID of the xiaojiang IOT Service. */
#endif

#ifdef CONFIG_BLE_UUID_QMS_RC
#define BLE_UUID_QMS_RC CONFIG_BLE_UUID_QMS_RC /* The UUID of the "Read Characteristics" Characteristic. */
#endif

#ifdef CONFIG_BLE_UUID_QMS_WC
#define BLE_UUID_QMS_WC CONFIG_BLE_UUID_QMS_WC /* The UUID of the "Write Characteristics" Characteristic. */
#endif

#ifdef CONFIG_BLE_UUID_QMS_IC
#define BLE_UUID_QMS_IC CONFIG_BLE_UUID_QMS_IC /* The UUID of the "Indicate Characteristics" Characteristic. */
#endif

#ifdef CONFIG_BLE_UUID_QMS_WWNRC
#define BLE_UUID_QMS_WWNRC CONFIG_BLE_UUID_QMS_WWNRC /* The UUID of the "Write WithNoRsp Characteristics" Characteristic. */
#endif

#ifdef CONFIG_BLE_UUID_QMS_NC
#define BLE_UUID_QMS_NC  CONFIG_BLE_UUID_QMS_NC /* The UUID of the "Notify Characteristics" Characteristic. */
#endif

#ifdef CONFIG_QM_BLE_COMBO_NET_SUPPORT
#define   EN_QM_COMBO_NET
#endif

#ifdef CONFIG_QM_BLE_LONG_MTU_SUPPORT
#define   EN_LONG_MTU
#endif

#ifdef CONFIG_QM_BLE_VER_4_2_SUPPORT
#define   BLE_4_2
#endif

#ifdef CONFIG_QM_BLE_OTA_SUPPORT
#define  EN_QM_BLE_OTA
#endif

#ifdef CONFIG_QM_BLE_MCU_OTA_SUPPORT
#define  EN_QM_BLE_MCU_OTA
#endif

#ifdef CONFIG_QM_BLE_AUTH_SUPPORT
#define  EN_QM_BLE_AUTH
#endif

#ifdef CONFIG_QM_BLE_MAX_FRAME_NUMEBER
#define QB_MAX_FRAME_NUMBER           CONFIG_QM_BLE_MAX_FRAME_NUMEBER     // user payload max frame number
#else
#define QB_MAX_FRAME_NUMBER           16     // user payload max frame number
#endif

#ifdef CONFIG_QM_BLE_VESION
#define QB_VERSION                      CONFIG_QM_BLE_VESION
#else
#define QB_VERSION                      (2)
#endif

#define EN_QM_BLE_PRODTST

#define  EN_QM_BLE_POST_ASYNC

#define QB_AUTH_TIMEOUT               60000  // not allowed to be 0

#define QB_TRANSPORT_TIMEOUT          10000


#ifdef EN_LONG_MTU
#define QB_TRANSPORT_VER              0
#define EN_QM_BLE_LONG_MTU            1
#else
#define QB_TRANSPORT_VER              0
#endif

#define QB_ATT_HDR_SIZE               3
#define QB_FRAME_HDR_SIZE             4
#define QB_ENCRY_BLOCK_LENGTH         16     // aes 128, 16 byte a block
#define QB_GATT_MTU_SIZE_DEFAULT      23     // connect init before mtu exchange, use default mtu size
#define QB_GATT_MTU_SIZE_MAX          247    // when use long mtu, adjust this max mtu value with BLE stack config
#define QB_GATT_MTU_SIZE_LIMIT        103

/* Larger MTU size consumes more memory. If out of memory, you need to reduce the MTU size */
#define QB_FRAME_SIZE_DEFAULT         (QB_GATT_MTU_SIZE_DEFAULT - QB_ATT_HDR_SIZE - QB_FRAME_HDR_SIZE)
#define QB_FRAME_SIZE_MAX             (QB_GATT_MTU_SIZE_MAX - QB_ATT_HDR_SIZE - QB_FRAME_HDR_SIZE)
#define QB_FRAME_SIZE_LIMIT           (QB_GATT_MTU_SIZE_LIMIT - QB_ATT_HDR_SIZE - QB_FRAME_HDR_SIZE)

#ifdef EN_QM_BLE_LONG_MTU
#define QB_MAX_PAYLOAD_SIZE           (QB_FRAME_SIZE_MAX * QB_MAX_FRAME_NUMBER)
#else
#define QB_MAX_PAYLOAD_SIZE           (QB_FRAME_SIZE_DEFAULT * QB_MAX_FRAME_NUMBER)
#endif

#ifdef  EN_QM_BLE_SECURE_ADV
#define QB_ENABLE_SECURE_ADV 1
#else
#define QB_ENABLE_SECURE_ADV 0
#endif

#ifdef  EN_QM_BLE_AUTH
#define QB_ENABLE_AUTH 1
#else
#define QB_ENABLE_AUTH 0
#endif

#ifdef  EN_QM_COMBO_NET
#define QB_ENABLE_COMBO_NET 1
#else 
#define QB_ENABLE_COMBO_NET 0
#endif

#ifdef EN_QM_BLE_POST_ASYNC
#define QB_ENABLE_POST_ASYNC  1
#else
#define QB_ENABLE_POST_ASYNC  0
#endif

#ifdef EN_QM_BLE_OTA
#define QB_ENABLE_OTA  1
#else
#define QB_ENABLE_OTA  0
#endif

#ifdef EN_QM_BLE_MCU_OTA
#define QB_ENABLE_MCU_OTA  1
#else
#define QB_ENABLE_MCU_OTA  0
#endif

#ifdef EN_QM_BLE_OTA_DATA_ASYNC
#define QB_ENABLE_OTA_DATA_ASYNC  1
#else
#define QB_ENABLE_OTA_DATA_ASYNC  0
#endif

#ifdef EN_QM_BLE_PRODTST
#define QB_ENABLE_PRODTST  1
#else
#define QB_ENABLE_PRODTST  0
#endif



#if defined(BLE_4_0)
#define QB_BLUETOOTH_VER 0x00
#define QB_MAX_SUPPORTED_MTU 23
#elif defined(BLE_4_2)
#define QB_BLUETOOTH_VER 0x01
#define QB_MAX_SUPPORTED_MTU 247
#elif defined(BLE_5_0)
#define QB_BLUETOOTH_VER 0x10
#define QB_MAX_SUPPORTED_MTU 247
#else
#define QB_BLUETOOTH_VER 0x00
#define QB_MAX_SUPPORTED_MTU 23
#endif

#endif  // QM_BLE_OPT_H
