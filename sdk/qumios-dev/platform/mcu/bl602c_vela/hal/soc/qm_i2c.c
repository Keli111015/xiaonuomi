#include "qm.h"
#include "qm_i2c.h"

// 截止2024年12月23日，暂未验证

#if CONFIG_QM_I2C_SUPPORT

// #include <applet.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <nuttx/i2c/i2c_master.h>
#include <syslog.h>
#include <stdlib.h>
#include <string.h>
#include "qm_errno.h"
#include "qm_kernel.h"
#include "util.h"

#include <nuttx/config.h>

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <debug.h>
#include <errno.h>

#include <nuttx/arch.h>
#include <nuttx/i2c/i2c_master.h>

#include <nuttx/irq.h>
#include <arch/board/board.h>

#include "chip.h"
#include "riscv_arch.h"

#include "hardware/bl602_glb.h"
#include "hardware/bl602_hbn.h"
#include "hardware/bl602_i2c.h"
#include "bl602_i2c.h"
#include "bl602_gpio.h"
#include "bl602_glb.h"

#define I2C_FIFO_STATUS_TIMEOUT (160 * 1000 * 2)

/**
 *  @brief I2C No. type definition
 */
typedef enum
{
    I2C0_ID = 0, /*!< I2C0 define */
    I2C_ID_MAX,  /*!< I2C max define */
} I2C_ID_Type;

/**
 *  @brief I2C read/write type definition
 */
typedef enum
{
    I2C_WRITE = 0, /*!< I2C write direction */
    I2C_READ,      /*!< I2C read direction */
} I2C_Direction_Type;

/**
 *  @brief I2S transfer structure type definition
 */
typedef struct
{
    uint8_t slaveAddr;     /*!< I2C slave address */
    uint8_t stopEveryByte; /*!< I2C all data byte with stop bit */
    uint8_t subAddrSize;   /*!< Specifies the size of I2C sub address section */
    uint32_t subAddr;      /*!< I2C sub address */
    uint16_t dataSize;     /*!< Specifies the size of I2C data section */
    uint8_t *data;         /*!< Specifies the pointer of I2C R/W data */
} I2C_Transfer_Cfg;

#define PUT_UINT32_LE(n, b, i)               \
    {                                        \
        (b)[(i)] = (uint8_t)((n));           \
        (b)[(i) + 1] = (uint8_t)((n) >> 8);  \
        (b)[(i) + 2] = (uint8_t)((n) >> 16); \
        (b)[(i) + 3] = (uint8_t)((n) >> 24); \
    }

/****************************************************************************
 * Name: bl602_i2c_clear_status
 *
 * Description:
 *   clear i2c status
 *
 ***************************************************************************/
static void bl602_i2c_clear_status(int i2cx)
{
    modifyreg32(BL602_I2C_BUS_BUSY, 0, I2C_BUS_BUSY_CR_BUS_BUSY_CLR);
    modifyreg32(BL602_I2C_INT_STS, 0, I2C_INT_STS_CR_END_CLR | I2C_INT_STS_CR_NAK_CLR | I2C_INT_STS_CR_ARB_CLR);
}

/****************************************************************************
 * Name: bl602_i2c_config_para
 *
 * Description:
 *   config i2c param
 *
 ****************************************************************************/
static void I2C_Init(I2C_ID_Type i2cNo, I2C_Direction_Type direct, I2C_Transfer_Cfg *cfg)
{
    if (i2cNo != 0)
        return;
    if (direct == I2C_READ) //=1 读
    {
        modifyreg32(BL602_I2C_CONFIG, 0, I2C_CONFIG_CR_I2C_PKT_DIR);
    }
    else //=0 write
    {
        modifyreg32(BL602_I2C_CONFIG, I2C_CONFIG_CR_I2C_PKT_DIR, 0);
    }

    modifyreg32(BL602_I2C_CONFIG,
                I2C_CONFIG_CR_I2C_SLV_ADDR_MASK,
                cfg->slaveAddr << I2C_CONFIG_CR_I2C_SLV_ADDR_SHIFT);

    modifyreg32(BL602_I2C_CONFIG, I2C_CONFIG_CR_I2C_SUB_ADDR_EN, 0);
    modifyreg32(BL602_I2C_CONFIG,
                I2C_CONFIG_CR_I2C_PKT_LEN_MASK,
                (cfg->dataSize - 1) << I2C_CONFIG_CR_I2C_PKT_LEN_SHIFT);
}

/****************************************************************************
 * Name: bl602_i2c_enable
 *
 * Description:
 *   i2c enable
 *
 ****************************************************************************/
static void I2C_Enable(uint8_t i2cNo)
{
    modifyreg32(BL602_I2C_FIFO_CONFIG_0, 0, I2C_FIFO_CONFIG_0_TX_FIFO_CLR);
    modifyreg32(BL602_I2C_FIFO_CONFIG_0, 0, I2C_FIFO_CONFIG_0_RX_FIFO_CLR);
    modifyreg32(BL602_I2C_CONFIG, 0, I2C_CONFIG_CR_I2C_M_EN);
}

/****************************************************************************
 * Name: bl602_i2c_setsclsync
 *
 * Description:
 *   set i2c scl sync
 *
 ****************************************************************************/
static void bl602_i2c_setsclsync(uint8_t enable)
{
    if (enable)
    {
        modifyreg32(BL602_I2C_CONFIG, 0, I2C_CONFIG_CR_I2C_SCL_SYNC_EN);
    }
    else
    {
        modifyreg32(BL602_I2C_CONFIG, I2C_CONFIG_CR_I2C_SCL_SYNC_EN, 0);
    }
}

/****************************************************************************
 * Name: bl602_i2c_setprd
 *
 * Description:
 *   set i2c prd
 *
 ****************************************************************************/
static void bl602_i2c_setprd(uint8_t phase)
{
    modifyreg32(BL602_I2C_PRD_START, I2C_PRD_START_CR_PRD_S_PH_0_MASK, phase);
    modifyreg32(BL602_I2C_PRD_START,
                I2C_PRD_START_CR_PRD_S_PH_1_MASK,
                phase << I2C_PRD_START_CR_PRD_S_PH_1_SHIFT);
    modifyreg32(BL602_I2C_PRD_START,
                I2C_PRD_START_CR_PRD_S_PH_2_MASK,
                phase << I2C_PRD_START_CR_PRD_S_PH_2_SHIFT);
    modifyreg32(BL602_I2C_PRD_START,
                I2C_PRD_START_CR_PRD_S_PH_3_MASK,
                phase << I2C_PRD_START_CR_PRD_S_PH_3_SHIFT);

    modifyreg32(BL602_I2C_PRD_STOP, I2C_PRD_STOP_CR_PRD_P_PH_0_MASK, phase);
    modifyreg32(BL602_I2C_PRD_STOP,
                I2C_PRD_STOP_CR_PRD_P_PH_1_MASK,
                phase << I2C_PRD_STOP_CR_PRD_P_PH_1_SHIFT);
    modifyreg32(BL602_I2C_PRD_STOP,
                I2C_PRD_STOP_CR_PRD_P_PH_2_MASK,
                phase << I2C_PRD_STOP_CR_PRD_P_PH_2_SHIFT);
    modifyreg32(BL602_I2C_PRD_STOP,
                I2C_PRD_STOP_CR_PRD_P_PH_3_MASK,
                phase << I2C_PRD_STOP_CR_PRD_P_PH_3_SHIFT);

    modifyreg32(BL602_I2C_PRD_DATA, I2C_PRD_DATA_CR_PRD_D_PH_0_MASK, phase);
    modifyreg32(BL602_I2C_PRD_DATA,
                I2C_PRD_DATA_CR_PRD_D_PH_1_MASK,
                phase << I2C_PRD_DATA_CR_PRD_D_PH_1_SHIFT);
    modifyreg32(BL602_I2C_PRD_DATA,
                I2C_PRD_DATA_CR_PRD_D_PH_2_MASK,
                phase << I2C_PRD_DATA_CR_PRD_D_PH_2_SHIFT);
    modifyreg32(BL602_I2C_PRD_DATA,
                I2C_PRD_DATA_CR_PRD_D_PH_3_MASK,
                phase << I2C_PRD_DATA_CR_PRD_D_PH_3_SHIFT);
}

/****************************************************************************
 * Name: bl602_set_i2c_clk
 *
 * Description:
 *   set I2C clock.
 *
 ****************************************************************************/
void bl602_set_i2c_clk(uint8_t enable, uint8_t div)
{
    modifyreg32(BL602_CLK_CFG3,
                CLK_CFG3_I2C_CLK_DIV_MASK,
                div << CLK_CFG3_I2C_CLK_DIV_SHIFT);

    if (enable)
    {
        modifyreg32(BL602_CLK_CFG3, 0, CLK_CFG3_I2C_CLK_EN);
    }
    else
    {
        modifyreg32(BL602_CLK_CFG3, CLK_CFG3_I2C_CLK_EN, 0);
    }
}

/****************************************************************************
 * Name: bl602_i2c_clockset
 *
 * Description:
 *   set i2c clock
 *
 ****************************************************************************/
static void bl602_i2c_clockset(uint32_t clk)
{
    uint8_t bclk_div = 0;
    uint32_t sys_clock = 0;

    bclk_div = bl602_glb_get_bclk_div();
    sys_clock = getreg32(BL602_HBN_RSV2);

    if (clk >= 100000)
    {
        bl602_set_i2c_clk(1, 0);
        bl602_i2c_setprd((sys_clock / (bclk_div + 1)) / (clk * 4) -
                         1);
    }
    else if (clk >= 8000)
    {
        bl602_set_i2c_clk(1, 9);
        bl602_i2c_setprd(
            ((sys_clock / (bclk_div + 1)) / 10) / (clk * 4) - 1);
    }
    else if (clk >= 800)
    {
        bl602_set_i2c_clk(1, 99);
        bl602_i2c_setprd(
            ((sys_clock / (bclk_div + 1)) / 100) / (clk * 4) - 1);
    }
    else
    {
        bl602_set_i2c_clk(1, 255);
        bl602_i2c_setprd(
            ((sys_clock / (bclk_div + 1)) / 256) / (clk * 4) - 1);
    }
}

/****************************************************************************
 * Name: bl602_i2c_set_freq
 *
 * Description:
 *   set i2c freq
 *
 ****************************************************************************/
static void bl602_i2c_set_freq(int freq)
{
    bl602_i2c_setsclsync(0);
    bl602_i2c_clockset(freq);
}

/**
* @brief  Get i2c busy state
*
* @param  i2cNo: I2C ID type
*
* @return RESET or SET
*
*******************************************************************************/
uint8_t I2C_IsBusy(I2C_ID_Type i2cNo)
{
    uint32_t tmpVal;
    tmpVal = getreg32(BL602_I2C_BUS_BUSY);
    return (((tmpVal) & (1U << ((0U)))) != 0);
}

/**
* @brief  Get i2c transfer end state
*
* @param  i2cNo: I2C ID type
*
* @return RESET or SET
*
*******************************************************************************/
uint8_t I2C_TransferEndStatus(I2C_ID_Type i2cNo)
{
    uint32_t tmpVal;

    tmpVal = getreg32(BL602_I2C_INT_STS);
    return ((((tmpVal) & (1U << ((0U)))) != 0));
}

/****************************************************************************
 * Name: bl602_i2c_transfer
 *
 * Description:
 *   Generic I2C transfer function
 *
 ****************************************************************************/
static int i2c_master_send(I2C_ID_Type i2cNo, I2C_Transfer_Cfg *cfg, uint32_t timeout)
{
    uint8_t i = 0;
    uint32_t temp = 0;
    uint32_t timeOut = 0;

    I2C_Disable(i2cNo);
    I2C_Init(i2cNo, I2C_WRITE, cfg);
    I2C_Enable(i2cNo);

    /* Set I2C write data */
    for (i = 0; i < cfg->dataSize; i++)
    {
        temp += (cfg->data[i] << ((i % 4) * 8));
        if ((i + 1) % 4 == 0)
        {
            timeOut = I2C_FIFO_STATUS_TIMEOUT;
            while ((((((*((volatile uint32_t *)(BL602_I2C_FIFO_CONFIG_1))))) & (((1U << (2U)) - 1) << (0U))) >> (0U)) == 0)
            {
                timeOut--;
                if (timeOut == 0)
                {
                    I2C_Disable(i2cNo);
                    return (-1);
                }
            }
            putreg32(temp, BL602_I2C_FIFO_WDATA);
            temp = 0;
        }
    }

    if ((cfg->dataSize % 4) != 0)
    {
        timeOut = I2C_FIFO_STATUS_TIMEOUT;
        while ((((((*((volatile uint32_t *)(BL602_I2C_FIFO_CONFIG_1))))) & (((1U << (2U)) - 1) << (0U))) >> (0U)) == 0)
        {
            timeOut--;
            if (timeOut == 0)
            {
                I2C_Disable(i2cNo);
                return (-1);
            }
        }
        putreg32(temp, BL602_I2C_FIFO_WDATA);
    }
    timeOut = I2C_FIFO_STATUS_TIMEOUT;
    while (I2C_IsBusy(i2cNo) || !I2C_TransferEndStatus(i2cNo))
    {
        timeOut--;
        if (timeOut == 0)
        {
            I2C_Disable(i2cNo);
            return (-1);
        }
    }
    I2C_Disable(i2cNo);

    return 0;
}

static int i2c_master_receive(I2C_ID_Type i2cNo, I2C_Transfer_Cfg *cfg, uint32_t timeout)
{
    uint8_t i = 0;
    uint32_t temp = 0;
    uint32_t timeOut = 0;

    I2C_Disable(i2cNo);
    I2C_Init(i2cNo, I2C_READ, cfg);
    I2C_Enable(i2cNo);

    /* Read I2C data */
    while (cfg->dataSize - i >= 4)
    {
        timeOut = I2C_FIFO_STATUS_TIMEOUT;
        while ((((((*((volatile uint32_t *)(BL602_I2C_FIFO_CONFIG_1))))) & (((1U << (2U)) - 1) << (8U))) >> (8U)) == 0)
        {
            timeOut--;
            if (timeOut == 0)
            {
                I2C_Disable(i2cNo);
                return (-1);
            }
        }
        temp = getreg32(BL602_I2C_FIFO_RDATA);
        PUT_UINT32_LE(temp, cfg->data, i);
        i += 4;
    }
    if (i < cfg->dataSize)
    {
        timeOut = I2C_FIFO_STATUS_TIMEOUT;
        while ((((((*((volatile uint32_t *)(BL602_I2C_FIFO_CONFIG_1))))) & (((1U << (2U)) - 1) << (8U))) >> (8U)) == 0)
        {
            timeOut--;
            if (timeOut == 0)
            {
                I2C_Disable(i2cNo);
                return (-1);
            }
        }
        temp = getreg32(BL602_I2C_FIFO_RDATA);
        while (i < cfg->dataSize)
        {
            cfg->data[i] = (temp & 0xff);
            temp = (temp >> 8);
            i++;
        }
    }

    timeOut = I2C_FIFO_STATUS_TIMEOUT;
    while (I2C_IsBusy(i2cNo) || !I2C_TransferEndStatus(i2cNo))
    {
        timeOut--;
        if (timeOut == 0)
        {
            I2C_Disable(i2cNo);
            return (-1);
        }
    }
    I2C_Disable(i2cNo);
    return 0;
}

/****************************************************************************
 * Name: bl602_i2c_disable
 *
 * Description:
 *   disable i2c
 *
 ****************************************************************************/

void I2C_Disable(uint8_t i2cNo)
{
    modifyreg32(BL602_I2C_CONFIG, I2C_CONFIG_CR_I2C_M_EN, 0);

    /* Clear I2C fifo */

    modifyreg32(BL602_I2C_FIFO_CONFIG_0, 0, I2C_FIFO_CONFIG_0_TX_FIFO_CLR);
    modifyreg32(BL602_I2C_FIFO_CONFIG_0, 0, I2C_FIFO_CONFIG_0_RX_FIFO_CLR);

    modifyreg32(BL602_I2C_INT_STS, 0, I2C_INT_STS_CR_END_CLR);
    modifyreg32(BL602_I2C_INT_STS, 0, I2C_INT_STS_CR_NAK_CLR);
    modifyreg32(BL602_I2C_INT_STS, 0, I2C_INT_STS_CR_ARB_CLR);
}

int32_t qm_i2c_master_recv(qm_i2c_dev_t *i2c, uint16_t dev_addr, uint8_t *data,
                           uint16_t size, uint32_t timeout)
{
    I2C_Transfer_Cfg i2c_cfg_recv = {
        .slaveAddr = 0x50,
        .stopEveryByte = 0,
        .subAddrSize = 0,
        .subAddr = 0x00,
        .dataSize = 0,
        .data = NULL,
    };

    if (NULL == data || size < 1)
    {
        qm_printf("parameter is error!\r\n");
        return -1;
    }

    i2c_cfg_recv.slaveAddr = dev_addr;
    i2c_cfg_recv.data = (uint8_t *)data;
    i2c_cfg_recv.dataSize = size;

    return i2c_master_receive(0, &i2c_cfg_recv, timeout);
}

int32_t qm_i2c_master_send(qm_i2c_dev_t *i2c, uint16_t dev_addr, const uint8_t *data,
                           uint16_t size, uint32_t timeout)
{
    I2C_Transfer_Cfg i2c_cfg_send = {
        .slaveAddr = 0x50,
        .stopEveryByte = 0,
        .subAddrSize = 0,
        .subAddr = 0x00,
        .dataSize = 0,
        .data = NULL,
    };

    if (NULL == data || size < 1)
    {
        qm_printf("parameter is error!\r\n");
        return -1;
    }

    i2c_cfg_send.slaveAddr = dev_addr;
    i2c_cfg_send.data = (uint8_t *)data;
    i2c_cfg_send.dataSize = size;

    return i2c_master_send(0, &i2c_cfg_send, timeout);
}

/****************************************************************************
 * Name: bl602_i2cbus_initialize
 *
 * Description:
 *   Initialize one I2C bus
 *
 ****************************************************************************/

struct i2c_master_s *bl602_i2cbus_initialize(int port)
{
    bl602_configgpio(BOARD_I2C_SCL);
    bl602_configgpio(BOARD_I2C_SDA);

    bl602_i2c_set_freq(40000);
    I2C_Disable(0);
    return 0;
}

// IIC初始化
int32_t qm_i2c_init(qm_i2c_dev_t *i2c)
{
    (void)i2c;
    bl602_i2cbus_initialize(0);
    return 0;
}

// IIC初始化
int32_t qm_i2c_deinit(qm_i2c_dev_t *i2c)
{
    (void)i2c;

    return 0;
}

#endif
