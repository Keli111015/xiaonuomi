#ifndef _ES8311_REG_H_
#define _ES8311_REG_H_

/*
 * ES8311 register definitions
 */
#define ES8311_RESET_REG00              0x00

/*
 * Clock manager
 */
#define ES8311_CLK_MANAGER_REG01        0x01
#define ES8311_CLK_MANAGER_REG02        0x02
#define ES8311_CLK_MANAGER_REG03        0x03
#define ES8311_CLK_MANAGER_REG04        0x04
#define ES8311_CLK_MANAGER_REG05        0x05
#define ES8311_CLK_MANAGER_REG06        0x06
#define ES8311_CLK_MANAGER_REG07        0x07
#define ES8311_CLK_MANAGER_REG08        0x08

/*
 * Serial data port
 */
#define ES8311_SDPIN_REG09              0x09
#define ES8311_SDPOUT_REG0A             0x0A

/*
 * System
 */
#define ES8311_SYSTEM_REG0B             0x0B
#define ES8311_SYSTEM_REG0C             0x0C
#define ES8311_SYSTEM_REG0D             0x0D
#define ES8311_SYSTEM_REG0E             0x0E
#define ES8311_SYSTEM_REG0F             0x0F
#define ES8311_SYSTEM_REG10             0x10
#define ES8311_SYSTEM_REG11             0x11
#define ES8311_SYSTEM_REG12             0x12
#define ES8311_SYSTEM_REG13             0x13
#define ES8311_SYSTEM_REG14             0x14

/*
 * ADC
 */
#define ES8311_ADC_REG15                0x15
#define ES8311_ADC_REG16                0x16
#define ES8311_ADC_REG17                0x17
#define ES8311_ADC_REG18                0x18
#define ES8311_ADC_REG19                0x19
#define ES8311_ADC_REG1A                0x1A
#define ES8311_ADC_REG1B                0x1B
#define ES8311_ADC_REG1C                0x1C

/*
 * DAC
 */
#define ES8311_DAC_REG31                0x31
#define ES8311_DAC_REG32                0x32
#define ES8311_DAC_REG33                0x33
#define ES8311_DAC_REG34                0x34
#define ES8311_DAC_REG35                0x35
#define ES8311_DAC_REG37                0x37

/*
 * GPIO
 */
#define ES8311_GPIO_REG44               0x44
#define ES8311_GP_REG45                 0x45

/*
 * Chip ID
 */
#define ES8311_CHD1_REGFD               0xFD
#define ES8311_CHD2_REGFE               0xFE
#define ES8311_CHVER_REGFF              0xFF

#define ES8311_MAX_REGISTER             0xFF

#endif /* _ES8311_REG_H_ */
