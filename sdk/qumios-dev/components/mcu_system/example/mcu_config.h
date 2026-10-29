#ifndef _MCU_CONFIG_H_
#define _MCU_CONFIG_H_

#define CONFIG_MCU_MAIN_VER 1         //用户的软件主版本号,用于MCU固件升级,MCU升级版本需修改
#define CONFIG_MCU_SUB_VER  0         //用户的软件子版本号,用于MCU固件升级,MCU升级版本需修改
#define CONFIG_MCU_FIX_VER  0         //用户的软件修复版本号,用于MCU固件升级,MCU升级版本需修改

#define CONFIG_MCU_RECV_BUF_LMT          4096              //数据接收缓存区大小,如MCU的RAM不够,可缩小
#define CONFIG_MCU_SEND_BUF_LMT          2048              //数据发送缓存区,必须大于48
#define CONFIG_MCU_DATA_PROCESS_LMT      4096              //数据处理缓存区大小,根据用户数据大小量定,必须大于24

/******************************************************************************
                        1:小匠产品功能协议对照表
          **后续为自动生成代码,如在开发平台有相关修改请重新下载MCU_SDK**         
******************************************************************************/
//网关(可下发可上报)
//备注:
#define ATTR_ID_ONOFF 0
//模式(可下发可上报)
//备注:
#define ATTR_ID_MODE  1

//产品功能表(必要，将相关属性ID填入该宏，需加{})
//备注:
#define CONFIG_MCU_ATTR_TABLE   {ATTR_ID_ONOFF, ATTR_ID_MODE}

/******************************************************************************
                        2:小匠产品设备扩展信息(不包含MCU版本号)
          **后续为自动生成代码,如在开发平台有相关修改请重新下载MCU_SDK**         
******************************************************************************/
//设备扩展信息(必要，将设备扩展信息填入该宏，需加{})
//备注:
#define CONFIG_MCU_DEVINFO   {/*0x06 ,0xF7 ,0x00 ,0x00 ,0x40 ,0xA2 ,0x0B*/}

#endif
