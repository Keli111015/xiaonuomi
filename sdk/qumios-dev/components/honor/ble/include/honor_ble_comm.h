#ifndef __HONOR_BLE_COMM_H__
#define __HONOR_BLE_COMM_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"
#include "honor_ble_profile.h"

#define HONOR_BLE_RX_EVENT		(0x0001)
#define HONOR_BLE_TX_EVENT		(0x0002)

#define HONOR_BLE_COMM_VERSION      (100)
#define HONOR_BLE_COMM_PROT_T       ("4")

#define HONOR_BLE_EOK          (0)               /**< There is no error */
#define HONOR_BLE_ERESET       (500)             /**< Device is reset */
#define HONOR_BLE_EPUT         (600)             /**< Device control failed*/
#define HONOR_BLE_ESEQ         (601)             /**< The status query failed */
#define HONOR_BLE_ENOMEM       (602)             /**< Out of memory */
#define HONOR_BLE_ERROR        (603)             /**< The device is abnormal */
#define HONOR_BLE_ESTACK       (604)             /**< The BLE protocol stack is abnormal */

#define HONOR_BLE_DEFAULT_ADV_TIMEOUT     (60 * 1000)

#define HONOR_BLE_DEFAULT_MMP_INTERVAL_S  (60)

#define HONOR_BLE_DEFAULT_MTU_SIZE    (251)

typedef enum{
    HONOR_BLE_PAIR_NONE,
    HONOR_BLE_PAIR_JUST_WORK,
    HONOR_BLE_PAIR_MAX,
}honor_ble_comm_pair_t;

typedef enum{
    HONOR_BLE_EVENT_CONFIG,               //配网状态
    HONOR_BLE_EVENT_CONFIG_FAIL,          //配网失败
    HONOR_BLE_EVENT_PAIRING,              //配对绑定中
    HONOR_BLE_EVENT_DEVICE_REGISTERED,    //设备被绑定
    HONOR_BLE_EVENT_DEVICE_UNREGISTER,    //设备已解绑
    HONOR_BLE_EVENT_CONNECT_APP,          //连接APP
    HONOR_BLE_EVENT_DISCONNECT_APP,       //断连APP
    HONOR_BLE_EVENT_DEVICE_OTA_START,     //OTA开始
    HONOR_BLE_EVENT_DEVICE_OTA_COMPLETE,  //OTA完成
    HONOR_BLE_EVENT_DEVICE_OTA_FAIL,      //OTA失败
    HONOR_BLE_EVENT_MAX,
}honor_ble_comm_event_t;

typedef struct 
{
    int8_t tx_power;   //发射功率
    uint8_t sub_pid; //华为给厂商分配的产品唯一标识
    char productid[5]; //华为给厂商分配的产品唯一标识
    char fw_ver[6];    //固件版本号 : 如 1.1.1
    char hw_ver[6];    //硬件版本号 : 如 1.1.1
    char sw_ver[6];    //软件版本号 : 如 1.1.1
    char model[16];     //设备型号
    char adv_name[11]; //广播名字
    char manu[11];     //厂商名称
}honor_ble_comm_info_t;

typedef struct 
{
    uint8_t pair_mode;
    honor_ble_comm_info_t info;
    int (*put)(honor_ble_profile_t *profile);
    int (*get)(uint8_t allservices, honor_ble_profile_t *profile); 
    void (*event_notify)(honor_ble_comm_event_t event, void *param);
}honor_ble_comm_param_t;

int honor_ble_comm_init(honor_ble_comm_param_t *param);
int honor_ble_comm_deinit(void);

int honor_ble_comm_reset(uint8_t is_near);
int honor_ble_comm_report(honor_ble_profile_t *profile); //调用该接口内部会自行释放profile指针,因此后续无需在使用free接口

int honor_ble_comm_adv_start(uint8_t is_near);
int honor_ble_comm_adv_stop(void);


#ifdef __cplusplus
}
#endif

#endif