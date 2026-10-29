#include "qm_log.h"
#include "qm_types.h"
#include "qm_kernel.h"

#include "qm_i2c.h"
#include "bp5768d.h"

#define LOG_TAG  "bp5768d"


#define INVALID_ADDR        0xFF
#define IIC_BASE_UNIT_HZ    1000
#define BP5768D_MAX_PIN     5

/**
 * BP5758D register start address - Byte0
 */
#define BASE_ADDR           0x80

/* B[3:0] */
#define BIT_OUT_SELECT      0x00
#define BIT_OUT1_CURRENT    0x01
#define BIT_OUT2_CURRENT    0x02
#define BIT_OUT3_CURRENT    0x03
#define BIT_OUT4_CURRENT    0x04
#define BIT_OUT5_CURRENT    0x05
#define BIT_OUT1_GRAYSCALE  0x06
#define BIT_OUT2_GRAYSCALE  0x08
#define BIT_OUT3_GRAYSCALE  0x0A
#define BIT_OUT4_GRAYSCALE  0x0C
#define BIT_OUT5_GRAYSCALE  0x0E

/* B[5:4] */
#define BIT_ENABLE_SLEEP_MODE   0x00
#define BIT_DISABLE_SLEEP_MODE  0x20

/**
 * BP5758D register out control address - Byte1
 */
/* B[4:0] */
#define BIT_OUT1_ENABLE 0x01
#define BIT_OUT2_ENABLE 0x02
#define BIT_OUT3_ENABLE 0x04
#define BIT_OUT4_ENABLE 0x08
#define BIT_OUT5_ENABLE 0x10

#define BIT_ALL_OUT_ENABLE (BIT_OUT1_ENABLE | BIT_OUT2_ENABLE | BIT_OUT3_ENABLE | BIT_OUT4_ENABLE | BIT_OUT5_ENABLE)
#define BIT_ALL_OUT_DISABLE 0x00


/**
 * BP5758D register current address - Byte2-6
 *
 *
 */
// Nothing

/**
 * BP5758D register grayscale address - Byte7-16
 *
 *
 */
#define BIT_GRAYSCALE_DATA_HEAD 0x00

typedef struct {
    bool_t init_done;
    qm_i2c_dev_t i2c_dev;
    uint8_t current[BP5768D_MAX_PIN];
    uint8_t mapping_addr[BP5768D_MAX_PIN];
} bp5768d_handle_t;

static bp5768d_handle_t *g_bp5768d = NULL;

static qm_err_t bp5768d_i2c_write(uint8_t addr, uint8_t *data_wr, size_t size)
{
    return qm_i2c_master_send(&(g_bp5768d->i2c_dev), addr, data_wr, size, 10);
}

static uint8_t get_mapping_addr(qm_bp5768d_channel_t channel)
{
    uint8_t addr[] = { BIT_OUT1_GRAYSCALE, BIT_OUT2_GRAYSCALE, BIT_OUT3_GRAYSCALE, BIT_OUT4_GRAYSCALE, BIT_OUT5_GRAYSCALE };
    uint8_t result = addr[g_bp5768d->mapping_addr[channel]];

    return result;
}

static qm_err_t set_sleep_mode_and_current(bool_t enable_sleep, uint8_t *current)
{
    uint8_t addr = BASE_ADDR | BIT_OUT_SELECT;
    uint8_t value[6] = { 0 };

    if (enable_sleep) {
        addr |= BIT_ENABLE_SLEEP_MODE;
        value[0] = BIT_ALL_OUT_DISABLE;
        g_bp5768d->init_done = 0;
        // The current parameters are not updated here.
    } else {
        addr |= BIT_DISABLE_SLEEP_MODE;
        value[0] = BIT_ALL_OUT_ENABLE;
        memcpy(&value[1], current, 5);
        g_bp5768d->init_done = 1;
    }

#if BP5768D_ADDR_BIT_TYPE == 7
    addr = addr >> 1;
#endif

 #ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "set_sleep_mode_and_current %02X", addr);
#endif   
    return bp5768d_i2c_write(addr, value, sizeof(value));
}

static void convert_current_value(uint8_t *output, uint8_t *input)
{
    for (int i = 0; i < 5; i++) {
        if (input[i] >= 64) {
            uint8_t temp = input[i];
            temp -= 62;
            input[i] = temp | 0x60;
        }
    }
    memcpy(output, input, 5);
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "convert_current_value %d %d %d %d %d", output[0], output[1], output[2], output[3], output[4]);
#endif
}

/**
 * @brief Initialize BP5768D output
 *
 * @param config Driver configuration
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_init(qm_driver_bp5768d_t *config)
{
    qm_err_t ret = QM_EOK;
    if(config == NULL || g_bp5768d){
        return -QM_EINVAL;
    }

    g_bp5768d = (bp5768d_handle_t *)qm_malloc(sizeof(bp5768d_handle_t));
    if(g_bp5768d == NULL){
        return -QM_ENOMEM;
    }
    memset(g_bp5768d, 0, sizeof(bp5768d_handle_t));
    convert_current_value(g_bp5768d->current, config->current);
    memset(g_bp5768d->mapping_addr, INVALID_ADDR, BP5768D_MAX_PIN);
    
    g_bp5768d->i2c_dev.config.freq = config->freq_khz * IIC_BASE_UNIT_HZ;
    
    if (config->freq_khz > 300) {
        g_bp5768d->i2c_dev.config.freq = 300 * IIC_BASE_UNIT_HZ;
#ifdef LOG_TAG
    QM_LOGW(LOG_TAG, "The frequency is too high, adjust it to 300khz");
#endif
    }

    g_bp5768d->i2c_dev.port  = config->port;
    g_bp5768d->i2c_dev.pins.sck_pin = config->iic_clk;
    g_bp5768d->i2c_dev.pins.sda_pin = config->iic_sda;
    
    ret = qm_i2c_init(&g_bp5768d->i2c_dev);
    if(ret != QM_EOK){
        qm_free(g_bp5768d);
        g_bp5768d = NULL;
    }
    
    return ret;
}

/**
 * @brief Register the BP5768D channel
 * @note Needs to correspond to the real hardware
 *
 * @param channel Abstract channel
 * @param pin Chip pin
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_regist_channel(qm_bp5768d_channel_t channel, qm_bp5758d_out_pin_t pin)
{
    if((channel >= QM_BP5768D_CHANNEL_MAX) || (pin >= QM_BP5768D_PIN_OUT_MAX))
    {
        return -QM_EINVAL;
    }

    g_bp5768d->mapping_addr[channel] = pin;

#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "BP5768D regist_channel %d CHIP out pin %d", channel, pin);
#endif

    return QM_EOK;
}

/**
 * @brief Set any channel output
 *
 * @param channel Abstract pin
 * @param value Output value
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_channel(qm_bp5768d_channel_t channel, uint16_t value)
{
    if((channel >= QM_BP5768D_CHANNEL_MAX) || (value > BP5768D_MAX_GRADE))
    {
        return -QM_EINVAL;
    }

    if (!g_bp5768d->init_done) {
        set_sleep_mode_and_current(0, g_bp5768d->current);
    }

    uint8_t _value[2] = { 0 };
    uint8_t addr = BASE_ADDR | BIT_DISABLE_SLEEP_MODE;

    _value[0] = (value & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[1] = (value >> 5) | BIT_GRAYSCALE_DATA_HEAD;
    addr |= (get_mapping_addr(channel));

    return bp5768d_i2c_write(addr, _value, sizeof(_value));
}



/**
 * @brief Set only rgb channel output
 *
 * @param value_r Output red value
 * @param value_g Output green value
 * @param value_b Output blue value
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_rgb_channel(uint16_t value_r, uint16_t value_g, uint16_t value_b)
{
    if((value_r > BP5768D_MAX_GRADE) || (value_g > BP5768D_MAX_GRADE) || (value_b > BP5768D_MAX_GRADE))
    {
        return -QM_EINVAL;
    }

    if (!g_bp5768d->init_done) {
        set_sleep_mode_and_current(0, g_bp5768d->current);
    }

    uint8_t _value[6] = { 0 };
    uint8_t addr = BASE_ADDR | BIT_OUT1_GRAYSCALE | BIT_DISABLE_SLEEP_MODE;

#if BP5768D_ADDR_BIT_TYPE == 7
    addr = addr >> 1;
#endif

#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "BP5768D addr %02X", addr);
#endif
    _value[g_bp5768d->mapping_addr[0] * 2 + 0] = (value_r & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[g_bp5768d->mapping_addr[0] * 2 + 1] = (value_r >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    _value[g_bp5768d->mapping_addr[1] * 2 + 0] = (value_g & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[g_bp5768d->mapping_addr[1] * 2 + 1] = (value_g >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    _value[g_bp5768d->mapping_addr[2] * 2 + 0] = (value_b & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[g_bp5768d->mapping_addr[2] * 2 + 1] = (value_b >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    return bp5768d_i2c_write(addr, _value, sizeof(_value));
}

/**
 * @brief Set only cw channel output
 *
 * @param value_c Output cold value
 * @param value_w Output white value
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_cw_channel(uint16_t value_c, uint16_t value_w)
{
    if((value_c > BP5768D_MAX_GRADE) || (value_w > BP5768D_MAX_GRADE))
    {
        return -QM_EINVAL;
    }

    if (!g_bp5768d->init_done) {
        set_sleep_mode_and_current(0, g_bp5768d->current);
    }

    uint8_t _value[4] = { 0 };
    uint8_t addr = BASE_ADDR | BIT_OUT4_GRAYSCALE | BIT_DISABLE_SLEEP_MODE;

#if BP5768D_ADDR_BIT_TYPE == 7
    addr = addr >> 1;
#endif
    
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "BP5768D addr %02X", addr);
#endif

    _value[(g_bp5768d->mapping_addr[3] - 3) * 2 + 0] = (value_c & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[(g_bp5768d->mapping_addr[3] - 3) * 2 + 1] = (value_c >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    _value[(g_bp5768d->mapping_addr[4] - 3) * 2 + 0] = (value_w & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[(g_bp5768d->mapping_addr[4] - 3) * 2 + 1] = (value_w >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    return bp5768d_i2c_write(addr, _value, sizeof(_value));
}

/**
 * @brief Set all channel output
 *
 * @param value_r Output red value
 * @param value_g Output green value
 * @param value_b Output blue value
 * @param value_w Output cold value
 * @param value_y Output white value
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_rgbcw_channel(uint16_t value_r, uint16_t value_g, uint16_t value_b, uint16_t value_c, uint16_t value_w)
{
    if((value_r > BP5768D_MAX_GRADE) || (value_g > BP5768D_MAX_GRADE) || (value_b > BP5768D_MAX_GRADE) || (value_c > BP5768D_MAX_GRADE) || (value_w > BP5768D_MAX_GRADE))
    {
        return -QM_EINVAL;
    }

    if (!g_bp5768d->init_done) {
        set_sleep_mode_and_current(0, g_bp5768d->current);
    }

    uint8_t _value[10] = { 0 };
    uint8_t addr = BASE_ADDR | BIT_OUT1_GRAYSCALE | BIT_DISABLE_SLEEP_MODE;

#if BP5768D_ADDR_BIT_TYPE == 7
    addr = addr >> 1;
#endif
    
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "BP5768D addr %02X", addr);
#endif

    _value[g_bp5768d->mapping_addr[0] * 2 + 0] = (value_r & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[g_bp5768d->mapping_addr[0] * 2 + 1] = (value_r >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    _value[g_bp5768d->mapping_addr[1] * 2 + 0] = (value_g & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[g_bp5768d->mapping_addr[1] * 2 + 1] = (value_g >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    _value[g_bp5768d->mapping_addr[2] * 2 + 0] = (value_b & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[g_bp5768d->mapping_addr[2] * 2 + 1] = (value_b >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    _value[g_bp5768d->mapping_addr[3] * 2 + 0] = (value_c & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[g_bp5768d->mapping_addr[3] * 2 + 1] = (value_c >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    _value[g_bp5768d->mapping_addr[4] * 2 + 0] = (value_w & 0x1F) | BIT_GRAYSCALE_DATA_HEAD;
    _value[g_bp5768d->mapping_addr[4] * 2 + 1] = (value_w >> 5) | BIT_GRAYSCALE_DATA_HEAD;

    return bp5768d_i2c_write(addr, _value, sizeof(_value));
}

/**
 * @brief Stop all channel output
 *
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_shutdown(void)
{
    uint8_t _value[10] = { 0 };
    uint8_t addr = BASE_ADDR | BIT_DISABLE_SLEEP_MODE | BIT_OUT1_GRAYSCALE;

#if BP5768D_ADDR_BIT_TYPE == 7
    addr = addr >> 1;
#endif
    
#ifdef LOG_TAG
    QM_LOGD(LOG_TAG, "BP5768D addr %02X", addr);
#endif

    return bp5768d_i2c_write(addr, _value, sizeof(_value));
}

/**
 * @brief Set standby mode
 *
 * @param enable_sleep If set to true will enter standby mode
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_set_standby_mode(bool_t enable_sleep)
{
    qm_bp5768d_set_shutdown();
    return set_sleep_mode_and_current(enable_sleep, g_bp5768d->current);
}

/**
 * @brief Deinitialize bp5758d and release resources
 *
 * @return qm_err_t
 */
qm_err_t qm_bp5768d_deinit(void)
{
    if(g_bp5768d == NULL)
    {
        return -QM_ERROR;
    }
    qm_bp5768d_set_shutdown();
    qm_i2c_deinit(&(g_bp5768d->i2c_dev));
    qm_free(g_bp5768d);
    g_bp5768d = NULL;
    return QM_EOK;
}