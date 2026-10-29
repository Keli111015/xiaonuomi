#ifndef _CH423S_H_
#define _CH423S_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_gpio.h"

#if 0
typedef union{
	struct{
		uint16_t OC0:1;
		uint16_t OC1:1;
		uint16_t OC2:1;
		uint16_t OC3:1;
		uint16_t OC4:1;
		uint16_t OC5:1;
		uint16_t OC6:1;
		uint16_t OC7:1;
		uint16_t OC8:1;
		uint16_t OC9:1;
		uint16_t OC10:1;
		uint16_t OC11:1;
		uint16_t OC12:1;
		uint16_t OC13:1;
		uint16_t OC14:1;
		uint16_t OC15:1;
	}state;
	uint16_t v;
}ch423_output_t;

typedef union{
	struct{
		uint16_t IO0:1;
		uint16_t IO1:1;
		uint16_t IO2:1;
		uint16_t IO3:1;
		uint16_t IO4:1;
		uint16_t IO5:1;
		uint16_t IO6:1;
		uint16_t IO7:1;
	}state;
	uint8_t v;
}ch423_input_t;
#endif

// CH423接口定义
#define     CH423_I2C_ADDR1     0x40         // CH423的地址
#define     CH423_I2C_MASK      0x3E         // CH423的高字节命令掩码

/*  设置系统参数命令 */ 

#define CH423_SYS_CMD     0x48       // 设置系统参数命令，默认方式
#define BIT_SLEEP         0x40       //低功耗睡眠控制
#define BIT_INTENS        0x20       //动态显示驱动亮度控制
#define BIT_OD_EN         0x10		 // 输出引脚 OC15～OC0 开漏输出使能
#define BIT_X_INT         0x08       // 使能输入电平变化中断，为0禁止输入电平变化中断；为1并且DEC_H为0允许输出电平变化中断
#define BIT_DEC_H         0x04       // 控制开漏输出引脚高8位的片选译码
#define BIT_DEC_L         0x02       // 控制开漏输出引脚低8位的片选译码
#define BIT_IO_OE         0x01       // 控制双向输入输出引脚的三态输出，为1允许输出

/*  设置低8位开漏输出命令 */

#define CH423_OC_L_CMD    0x44       // 设置低8位开漏输出命令，默认方式
#define BIT_OC0_L_DAT     0x01       // OC0为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC1_L_DAT     0x02       // OC1为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC2_L_DAT     0x04       // OC2为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC3_L_DAT     0x08       // OC3为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC4_L_DAT     0x10       // OC4为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC5_L_DAT     0x20       // OC5为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC6_L_DAT     0x40       // OC6为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC7_L_DAT     0x80       // OC7为0则使引脚输出低电平，为1则引脚不输出

/*  设置高8位开漏输出命令 */

#define CH423_OC_H_CMD    0x46        // 设置低8位开漏输出命令，默认方式
#define BIT_OC8_L_DAT     0x01        // OC8为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC9_L_DAT     0x02        // OC9为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC10_L_DAT    0x04        // OC10为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC11_L_DAT    0x08        // OC11为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC12_L_DAT    0x10        // OC12为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC13_L_DAT    0x20        // OC13为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC14_L_DAT    0x40        // OC14为0则使引脚输出低电平，为1则引脚不输出
#define BIT_OC15_L_DAT    0x80        // OC15为0则使引脚输出低电平，为1则引脚不输出

/* 设置双向输入输出命令 */

#define CH423_SET_IO_CMD   0x60      // 设置双向输入输出命令，默认方式
#define BIT_IO0_DAT        0x01      // 写入双向输入输出引脚的输出寄存器，当IO_OE=1,IO0为0输出低电平，为1输出高电平
#define BIT_IO1_DAT        0x02      // 写入双向输入输出引脚的输出寄存器，当IO_OE=1,IO1为0输出低电平，为1输出高电平
#define BIT_IO2_DAT        0x04      // 写入双向输入输出引脚的输出寄存器，当IO_OE=1,IO2为0输出低电平，为1输出高电平
#define BIT_IO3_DAT        0x08      // 写入双向输入输出引脚的输出寄存器，当IO_OE=1,IO3为0输出低电平，为1输出高电平
#define BIT_IO4_DAT        0x10      // 写入双向输入输出引脚的输出寄存器，当IO_OE=1,IO4为0输出低电平，为1输出高电平
#define BIT_IO5_DAT        0x20      // 写入双向输入输出引脚的输出寄存器，当IO_OE=1,IO5为0输出低电平，为1输出高电平
#define BIT_IO6_DAT        0x40      // 写入双向输入输出引脚的输出寄存器，当IO_OE=1,IO6为0输出低电平，为1输出高电平
#define BIT_IO7_DAT        0x80      // 写入双向输入输出引脚的输出寄存器，当IO_OE=1,IO7为0输出低电平，为1输出高电平

/* 读取双向输入输出命令 */

#define CH423_RD_IO_CMD		0x4D	// 输入I/O引脚当前状态



#define GPIO_EXT_OC_Pin_0                 ((uint32_t)0x00000001)  /*!< Pin 0 selected */
#define GPIO_EXT_OC_Pin_1                 ((uint32_t)0x00000002)  /*!< Pin 1 selected */
#define GPIO_EXT_OC_Pin_2                 ((uint32_t)0x00000004)  /*!< Pin 2 selected */
#define GPIO_EXT_OC_Pin_3                 ((uint32_t)0x00000008)  /*!< Pin 3 selected */
#define GPIO_EXT_OC_Pin_4                 ((uint32_t)0x00000010)  /*!< Pin 4 selected */
#define GPIO_EXT_OC_Pin_5                 ((uint32_t)0x00000020)  /*!< Pin 5 selected */
#define GPIO_EXT_OC_Pin_6                 ((uint32_t)0x00000040)  /*!< Pin 6 selected */
#define GPIO_EXT_OC_Pin_7                 ((uint32_t)0x00000080)  /*!< Pin 7 selected */
#define GPIO_EXT_OC_Pin_8                 ((uint32_t)0x00000100)  /*!< Pin 8 selected */
#define GPIO_EXT_OC_Pin_9                 ((uint32_t)0x00000200)  /*!< Pin 9 selected */
#define GPIO_EXT_OC_Pin_10                ((uint32_t)0x00000400)  /*!< Pin 10 selected */
#define GPIO_EXT_OC_Pin_11                ((uint32_t)0x00000800)  /*!< Pin 11 selected */
#define GPIO_EXT_OC_Pin_12                ((uint32_t)0x00001000)  /*!< Pin 12 selected */
#define GPIO_EXT_OC_Pin_13                ((uint32_t)0x00002000)  /*!< Pin 13 selected */
#define GPIO_EXT_OC_Pin_14                ((uint32_t)0x00004000)  /*!< Pin 14 selected */
#define GPIO_EXT_OC_Pin_15                ((uint32_t)0x00008000)  /*!< Pin 15 selected */

#define GPIO_EXT_IO_Pin_0                 ((uint32_t)0x00000001)  /*!< Pin 0 selected */
#define GPIO_EXT_IO_Pin_1                 ((uint32_t)0x00000002)  /*!< Pin 1 selected */
#define GPIO_EXT_IO_Pin_2                 ((uint32_t)0x00000004)  /*!< Pin 2 selected */
#define GPIO_EXT_IO_Pin_3                 ((uint32_t)0x00000008)  /*!< Pin 3 selected */
#define GPIO_EXT_IO_Pin_4                 ((uint32_t)0x00000010)  /*!< Pin 4 selected */
#define GPIO_EXT_IO_Pin_5                 ((uint32_t)0x00000020)  /*!< Pin 5 selected */
#define GPIO_EXT_IO_Pin_6                 ((uint32_t)0x00000040)  /*!< Pin 6 selected */
#define GPIO_EXT_IO_Pin_7                 ((uint32_t)0x00000080)  /*!< Pin 7 selected */


#define IS_GPIO_EXT_OC_L_PIN(PIN) (((PIN) == GPIO_EXT_OC_Pin_0) || \
                              ((PIN) == GPIO_EXT_OC_Pin_1) || \
                              ((PIN) == GPIO_EXT_OC_Pin_2) || \
                              ((PIN) == GPIO_EXT_OC_Pin_3) || \
                              ((PIN) == GPIO_EXT_OC_Pin_4) || \
                              ((PIN) == GPIO_EXT_OC_Pin_5) || \
                              ((PIN) == GPIO_EXT_OC_Pin_6) || \
                              ((PIN) == GPIO_EXT_OC_Pin_7))


#define IS_GPIO_EXT_OC_H_PIN(PIN) (((PIN) == GPIO_EXT_OC_Pin_8) || \
                              ((PIN) == GPIO_EXT_OC_Pin_9) || \
                              ((PIN) == GPIO_EXT_OC_Pin_10) || \
                              ((PIN) == GPIO_EXT_OC_Pin_11) || \
                              ((PIN) == GPIO_EXT_OC_Pin_12) || \
                              ((PIN) == GPIO_EXT_OC_Pin_13) || \
                              ((PIN) == GPIO_EXT_OC_Pin_14) || \
                              ((PIN) == GPIO_EXT_OC_Pin_15))


#define IS_GPIO_EXT_IO_PIN(PIN) (((PIN) == GPIO_EXT_IO_Pin_0) || \
                              ((PIN) == GPIO_EXT_IO_Pin_1) || \
                              ((PIN) == GPIO_EXT_IO_Pin_2) || \
                              ((PIN) == GPIO_EXT_IO_Pin_3) || \
                              ((PIN) == GPIO_EXT_IO_Pin_4) || \
                              ((PIN) == GPIO_EXT_IO_Pin_5) || \
                              ((PIN) == GPIO_EXT_IO_Pin_6) || \
                              ((PIN) == GPIO_EXT_IO_Pin_7))

typedef struct{
    uint8_t gpio_ext_low_oc;
    uint8_t gpio_ext_high_oc;
    uint8_t gpio_ext_io;
}ch423s_gpio_ext_t;

typedef struct{
    qm_gpio_dev_t gpio_scl;
    qm_gpio_dev_t gpio_sda;
    qm_gpio_dev_t gpio_isr;
    ch423s_gpio_ext_t ch423s_gpio_ext;
}ch423s_handle_t;

typedef enum
{
    QM_CH423S_INTR_DISABLE = 0,     /*!< Disable GPIO interrupt                             */
    QM_CH423S_INTR_POSEDGE = 1,     /*!< GPIO interrupt type : rising edge                  */
    QM_CH423S_INTR_NEGEDGE = 2,     /*!< GPIO interrupt type : falling edge                 */
    QM_CH423S_INTR_ANYEDGE = 3,     /*!< GPIO interrupt type : both rising and falling edge */
    QM_CH423S_INTR_LOW_LEVEL = 4,   /*!< GPIO interrupt type : input low level trigger      */
    QM_CH423S_INTR_HIGH_LEVEL = 5,  /*!< GPIO interrupt type : input high level trigger     */
    QM_CH423S_INTR_MAX,	
}qm_ch423s_irq_type_t;

typedef void (*qm_ch423s_irq_handler_t)(void *arg);

int qm_ch423s_get(void *handle, uint32_t gpio_pin);
int qm_ch423s_irq_deregister(void *handle, uint32_t gpio_pin);
int qm_ch423s_irq_register(void *handle, qm_ch423s_irq_type_t intr_type, uint32_t gpio_pin, qm_ch423s_irq_handler_t handler, void *arg);
void* qm_ch423s_init(int scl, int sda, int isr);
int qm_ch423s_set(void *handle, uint32_t gpio_pin, uint8_t value);
int qm_ch423s_deinit(void *handle);

#ifdef __cplusplus
}
#endif

#endif