#ifndef __HARMONY_BLE_CORE_H__
#define __HARMONY_BLE_CORE_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"
#include "harmony_ble_comm.h"

#define HARMONY_BLE_COMPANY_ID          (0xEEFD)  // company id
#define HARMONY_BLE_UNREG_NAME          ("Hi-")  
#define HARMONY_BLE_REG_NAME            ("HI-")  
#define HARMONY_BLE_VERSION             (1)
#define HARMONY_BLE_BASIC               (1)
#define HARMONY_BLE_EXTENSION           (0x07) //半模态
#define HARMONY_BLE_EXTENSION_FA        (0x0D) //FA
#define HARMONY_BLE_TYPE_SUB_PID        (0x04) //产品子型号类型
#define HARMONY_BLE_TYPE_TX_POWER       (0x11) //设备广播的实际发射功率，用于接收方测距
#define HARMONY_BLE_TYPE_PRODID         (0x12) //产品型号
#define HARMONY_BLE_TYPE_PROTOCOID      (0x00) //蓝牙协议ID
#define HARMONY_BLE_TYPE_SN             (0x02) //设备SN后两位
#define HARMONY_BLE_SEPARATOR           (0xFF) //分隔符

#define HARMONY_BLE_NEAR_ADV_TIMEOUT    (60 * 1000)
#define HARMONY_BLE_NEAR_ADV_INTERVAL   (200)      //靠近发现广播间隔, unit: ms
#define HARMONY_BLE_COMMON_ADV_INTERVAL (1000)      //常态广播间隔, unit: ms

#define HARMONY_BLE_ADV_NAME_TYPE             (0x09)
#define HARMONY_BLE_UNREG_PAYLODE_DATA_LEN    (12)
#define HARMONY_BLE_UNREG_HEAD_DATA_LEN       (5)
#define HARMONY_BLE_REG_PAYLODE_DATA_LEN      (15)
#define HARMONY_BLE_REG_HEAD_DATA_LEN         (5)

#define HARMONY_BLE_DEFAULT_REG_PAYLODE_DATA(PRODID, SN, MPP, INTERVAL)  { \
            '-', '1', \
            PRODID[0], PRODID[1], PRODID[2], PRODID[3], \
            SN[8], SN[9], SN[10], SN[11], \
            MPP, (INTERVAL & 0xFF), (INTERVAL >> 8) \
        }

#define HARMONY_BLE_DEFAULT_REG_HEAD_DATA(LEN)  { \
            LEN, HARMONY_BLE_ADV_NAME_TYPE, \
            'H', 'I', '-'   \
        }

#define HARMONY_BLE_DEFAULT_UNREG_PAYLODE_DATA(PRODID, SN)  { \
            '-', '1', \
            PRODID[0], PRODID[1], PRODID[2], PRODID[3], \
            '0', '0',\
            SN[8], SN[9], SN[10], SN[11], \
        }

#define HARMONY_BLE_DEFAULT_UNREG_HEAD_DATA(LEN)  { \
            LEN, HARMONY_BLE_ADV_NAME_TYPE, \
            'H', 'i', '-'   \
        }

#define HARMONY_BLE_1NEAR_ADV_DATA(SUB_PID, TX_POWER, PRODID, SN)  { \
            0x02, 0x01, 0x06, \
            0x15, 0x16, (HARMONY_BLE_COMPANY_ID >> 8), (HARMONY_BLE_COMPANY_ID & 0x00FF), \
            HARMONY_BLE_VERSION, HARMONY_BLE_BASIC, HARMONY_BLE_EXTENSION, \
            HARMONY_BLE_TYPE_SUB_PID, SUB_PID, \
            HARMONY_BLE_TYPE_TX_POWER, TX_POWER, \
            HARMONY_BLE_TYPE_PRODID, PRODID[0],PRODID[1], PRODID[2],PRODID[3], \
            HARMONY_BLE_SEPARATOR, \
            HARMONY_BLE_TYPE_PROTOCOID, 0x15, \
            HARMONY_BLE_TYPE_SN, SN[10], SN[11] \
        } 

#define HARMONY_BLE_2NEAR_ADV_DATA(SUB_PID, TX_POWER, PRODID, SN)  { \
            0x02, 0x01, 0x06, \
            0x15, 0x16, (HARMONY_BLE_COMPANY_ID >> 8), (HARMONY_BLE_COMPANY_ID & 0x00FF), \
            HARMONY_BLE_VERSION, HARMONY_BLE_BASIC, HARMONY_BLE_EXTENSION, \
            HARMONY_BLE_TYPE_SUB_PID, SUB_PID, \
            HARMONY_BLE_TYPE_TX_POWER, TX_POWER, \
            HARMONY_BLE_TYPE_PRODID, PRODID[0],PRODID[1], PRODID[2],PRODID[3], \
            HARMONY_BLE_SEPARATOR, \
            HARMONY_BLE_TYPE_PROTOCOID, 0x01, \
            HARMONY_BLE_TYPE_SN, SN[10], SN[11] \
        }


int harmony_ble_core_deinit(void);

int harmony_ble_core_init(harmony_ble_comm_param_t *param);

int harmony_ble_core_event_notify(uint8_t evt_type, uint8_t *data, uint16_t length);

int harmony_ble_core_unregisterd(uint8_t near_mode);

int harmony_ble_core_adv_start(uint8_t near_mode);
int harmony_ble_core_adv_stop(void);


int harmony_ble_core_conn_status_get(void);

int harmony_ble_core_store_set(int registered, uint8_t authCode[16], uint8_t authCodeId[16]);
int harmony_ble_core_store_get(int *registered, uint8_t authCode[16], uint8_t authCodeId[16]);

#ifdef __cplusplus
}
#endif

#endif