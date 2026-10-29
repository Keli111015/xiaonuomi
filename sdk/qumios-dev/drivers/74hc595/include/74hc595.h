#ifndef _HC595_H_
#define _HC595_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm.h"

typedef void* hc595_handle_t;

typedef enum {
    HC595_STATUS_LOW = 0,
    HC595_STATUS_HIGH,
} hc595_status_t;

typedef struct {
    int stcp; //存储寄存器时钟输入引脚。上升沿时，数据从移位寄存器转存到存储寄存器
    int shcp; //移位寄存器时钟引脚，上升沿时，移位寄存器中的bit 数据整体后移，并接受新的bit（从ser输入）
    int oe;   //输出使能控制脚，它是低电才使能输出; 不用时 oe置为-1, 硬件接GND即可
    int ds_ser;   //串行数据输入引脚
    int mr;   //MR低电平时，清空移位寄存器中已有的bit数据; 不用时 MRe置为-1,硬件接高电平即可
}hc595_io_t;

/**
  * @brief create hc595 object.
  *
  * @param hc595_io gpio number(s) of hc595
  * @param chip_num  芯片数量
  *
  * @return hc595_handle_t the handle of the hc595 created 
  */
hc595_handle_t hc595_create(hc595_io_t *hc595_io, int chip_num);

/**
  * @brief 设置芯片对应BIT位的电平信号，使用后需要调用 hc595_data_flush进行数据更新
  *
  * @param  hc595_handle
  * @param  channel 输出的IO端口通道号: 从第一级芯片的Q1端口开始计算,
  *                 如 第一级芯片的Q1端口号为0,Q2为1。第二级芯片的Q1端口号为8,Q2端口号为9,以此推类
  * @param  status IO端口的电平信号状态  
  * 
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t hc595_status_write(hc595_handle_t hc595_handle, uint8_t channel, hc595_status_t status);

/**
  * @brief 设置芯片对应BIT位的电平信号，使用后需要调用 hc595_data_flush进行数据更新
  *
  * @param  hc595_handle
  * @param  channel 输出的IO端口通道号: 从第一级芯片的Q1端口开始计算,
  *                 如 第一级芯片的Q1端口号为0,Q2为1。第二级芯片的Q1端口号为8,Q2端口号为9,以此推类
  * @param  out_level 输出的电平信号
  * 
  * @return state of the hc595 channel
  */
hc595_status_t hc595_status_read(hc595_handle_t hc595_handle, uint8_t channel);

/**
  * @brief free the memory of hc595
  *
  * @param  hc595_handle
  *
  * @return
  *     - QM_EOK: succeed
  *     - others: fail
  */
qm_err_t hc595_delete(hc595_handle_t hc595_handle);

#ifdef __cplusplus
}
#endif

#endif
