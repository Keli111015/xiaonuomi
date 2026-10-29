/**
* @file  ble_remoter.h
* @brief 小匠蓝牙广播协议规范V3.0协议组件
* @date  2025-1-13
*
* @copyright Copyright (C) 2025 小匠物联. All rights reserved.
*
* @details
*
* 该模块基于qmos组件快速对接小匠蓝牙广播协议规范V3.0, API使用流程如下:
*
* 1. 配置qm_config
*
* 2. 配置 @ref ble_remoter_cmd_table_t 可参考[ble_remoter_example.c]
*
* 3. 调用 @ref ble_remoter_init 初始化模块
*    + ble_remoter_params_t     初始化参数 见 @ref ble_remoter_params_t
*    + ble_remoter_cmd_table_t  遥控器的键值映射关系 见 @ref ble_remoter_cmd_table_t
*    + cmd_num    遥控器的键值映射关系数量
*
* 4. 当设备接收到遥控器相关键值并满足条件时，则触发用户注册的handler
*
*/
#ifndef _BLE_REMOTER_H_
#define _BLE_REMOTER_H_

#include "qm.h"

#ifdef __cplusplus
extern "C" {
#endif

//note: 当前ble addr 比对通过 contont里面的mac部分

///使用大端模式的PID(后续废弃)
#ifndef CONFIG_BLE_REMOTER_USE_BIGENDIAN_PID       
#define CONFIG_BLE_REMOTER_USE_BIGENDIAN_PID        (0)
#endif

///支持多遥控器控制
#ifndef CONFIG_BLE_REMOTER_MULTI_CONTROL_SUPPORT       
#define CONFIG_BLE_REMOTER_MULTI_CONTROL_SUPPORT    (0)
#endif

///最大过滤器数量
#ifndef CONFIG_BLE_REMOTER_FILTER_MAX_NUM       
#define CONFIG_BLE_REMOTER_FILTER_MAX_NUM           (10)
#endif

///最大支持的cmd table 数量
#ifndef CONFIG_BLE_REMOTER_CMD_TABLE_MAX_NUM       
#define CONFIG_BLE_REMOTER_CMD_TABLE_MAX_NUM        (20)
#endif

///内部启用扫描单元转换，兼容以前内部不自行转换的芯片
#ifndef CONFIG_BLE_REMOTER_NOT_USE_SCAN_UNIT      
#define CONFIG_BLE_REMOTER_NOT_USE_SCAN_UNIT        (0)
#endif

///扫描间隔，单位ms
#ifndef CONFIG_BLE_REMOTER_SCAN_INTERVAL     
#define CONFIG_BLE_REMOTER_SCAN_INTERVAL            (40)
#endif

///扫描窗口，单位ms
#ifndef CONFIG_BLE_REMOTER_SCAN_WINDOW     
#define CONFIG_BLE_REMOTER_SCAN_WINDOW              (20)
#endif

///mac地址长度
#define BLE_REMOTER_ADRR_LEN                        (6)   

///最小接收单元
#define BLE_REMOTER_RECV_DATA_MIN_NUM               (17)
///最大接收单元
#define BLE_REMOTER_RECV_DATA_MAX_NUM               (31)   
///PAYLOAD最大接收单元
#define BLE_REMOTER_PARAM_MAX_LEN                   (BLE_REMOTER_RECV_DATA_MAX_NUM - BLE_REMOTER_RECV_DATA_MIN_NUM)   

/**
 * @brief BLE Remoter模块映射关系类型
 */
typedef enum 
{
    /**
     * @brief 配对指令类型
     */
    BLE_REMOTE_CMD_TYPE_PAIR,           
    /**
     * @brief 通用控制类指令类型
     */
    BLE_REMOTE_CMD_TYPE_COMM,
    /**
     * @brief 工厂类指令类型
     */
    BLE_REMOTE_CMD_TYPE_FACTORY,
    BLE_REMOTE_CMD_TYPE_MAX,
}ble_remoter_cmd_type_t;

/**
 * @brief BLE Remoter模块触发接收报文时, 通知用户的参数类型
 */
typedef struct 
{
    /**
     * @brief 报文内容所对应的BLE ADDR
     */
    uint8_t ble_addr[BLE_REMOTER_ADRR_LEN];
    /**
     * @brief 组播地址
     */
    uint8_t group_id;
    /**
     * @brief 可选参数长度
     */
    uint8_t param_len;
    /**
     * @brief 可选参数数据内容
     */
    uint8_t param[BLE_REMOTER_PARAM_MAX_LEN];
}ble_remoter_payload_t;

/**
 * @brief BLE Remoter模块触发接收报文所需的映射关系结构体
 */
typedef struct 
{
    /**
     * @brief BLE Remoter模块映射关系类型, 详见 @ref ble_remoter_cmd_type_t
     */
    ble_remoter_cmd_type_t cmd_type;
    /**
     * @brief 键值
     * 
     * @note  opcode 为 0x00 则不进行键值过滤
     *
     */
    uint8_t opcode; 
    /**
     * @brief 用于通知接收报文的数据回调函数
     */
    void (*recv)(ble_remoter_cmd_type_t cmd_type, uint8_t opcode, ble_remoter_payload_t *payload);
}ble_remoter_cmd_table_t;

/**
 * @brief BLE Remoter模块配对模式初始化结构体
 */
typedef struct {
    /**
     * @brief 初始化自动使能配对功能
     */
    uint8_t  enable;   
    /**
     * @brief 配置配对窗口，单位(ms)
     */
    uint16_t pair_window;
}ble_remoter_pair_t;

/**
 * @brief BLE Remoter模块工厂模式初始化结构体
 */
typedef struct {
    /**
     * @brief 初始化自动使能工厂功能
     */
    uint8_t  enable;   
    /**
     * @brief 工厂模式的特殊mac地址
     * 
     * @note 该部分不能与正常使用的mac地址一样
     * 
     */
    uint8_t fac_addr[BLE_REMOTER_ADRR_LEN];
}ble_remoter_factory_t;

/**
 * @brief BLE Remoter模块通用项初始化结构体
 */
typedef struct {
    /**
     * @brief 蓝牙遥控器产品id
     */
    uint16_t    pid;
    /**
     * @brief 支持的功能掩码
     */
    uint8_t     fmsk;
    /**
     * @brief Subtype类型
     */
    uint8_t     subtype;
}ble_remoter_comm_t;

/**
 * @brief BLE Remoter模块初始化结构体
 */
typedef struct 
{
    /**
     * @brief BLE Remoter模块通用项初始化结构体
     */
    ble_remoter_comm_t comm_param;
    /**
     * @brief BLE Remoter模块配对模式初始化结构体
     */
    ble_remoter_pair_t pair_param;
    /**
     * @brief BLE Remoter模块工厂模式初始化结构体
     */
    ble_remoter_factory_t factory_param;
}ble_remoter_params_t;

/**
 * @brief 反初始化模块
 *
 * @return int32_t
 * @retval QM_EOK 反初始化成功
 * @retval Other   反初始化失败
 *
 */
int32_t ble_remoter_deinit(void);

/**
 * @brief 初始化模块
 * 
 * @param[in] params 初始化参数, 更多信息请参考 @ref ble_remoter_params_t
 * @param[in] cmd_params 映射关系选项, 更多信息请参考 @ref ble_remoter_cmd_table_t
 * @param[in] table_num   映射关系数量
 *
 * @return int32_t
 * @retval QM_EOK 反初始化成功
 * @retval Other   初始化失败, 一般是内存分配失败导致
 *
 */
int32_t ble_remoter_init(ble_remoter_params_t *params, ble_remoter_cmd_table_t *cmd_params, int table_num);


/**
 * @brief 重置模块并清除已绑定的设备
 * 
 *
 * @return int32_t
 * @retval QM_EOK 执行成功
 * @retval Other  执行失败, 一般是内存分配失败导致
 *
 */
int32_t ble_remoter_reset(void);


/**
 * @brief 使能配对功能
 * 
 * @return int32_t
 * @retval QM_EOK 使能成功
 * @retval Other   使能失败
 *
 */
int32_t ble_remoter_pair_enable(void);

/**
 * @brief 失能配对功能
 * 
 * @return int32_t
 * @retval QM_EOK 失能成功
 * @retval Other   失能失败
 *
 */
int32_t ble_remoter_pair_disable(void);

/**
 * @brief 使能工厂测试功能
 * 
 * @return int32_t
 * @retval QM_EOK 使能成功
 * @retval Other   使能失败
 *
 */
int32_t ble_remoter_factory_enable(void);

/**
 * @brief 失能工厂测试功能
 * 
 * @return int32_t
 * @retval QM_EOK 失能成功
 * @retval Other   失能失败
 *
 */
int32_t ble_remoter_factory_disable(void);

#ifdef __cplusplus
}
#endif  

#endif /* GENERIC_SERIAL_H */
