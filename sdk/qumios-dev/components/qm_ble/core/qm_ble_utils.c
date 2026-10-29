
#include "qm_ble_bzopt.h"
#include "qm_ble_utils.h"

uint8_t qm_ble_hex2ascii(uint8_t digit)
{
    uint8_t val;

    if (digit <= 9) {
        val = digit - 0x0 + '0';
    } else {
        val = digit - 0xA + 'A';
    }

    return val;
}

void qm_ble_hex2string(uint8_t *hex, uint32_t len, uint8_t *str)
{
    uint32_t index;

    for (index = 0; index < len; index++) {
        str[index * 2] = qm_ble_hex2ascii(hex[index] >> 4 & 0x0f);
        str[index * 2 + 1] = qm_ble_hex2ascii(hex[index] & 0x0f);
    }
}

static void hex_byte_dump(uint8_t *data, int len, int tab_num)
{
    (void)tab_num;
    if (data == NULL || len <= 0) {
        return;
    }
    QM_BLE_LOG_HEX(QM_BLE_LOG_PAYLOAD, "ble data", data, len);
}

void qm_ble_hex_byte_dump_debug(uint8_t *data, int len, int tab_num)
{
    hex_byte_dump(data, len, tab_num);
}

void qm_ble_hex_byte_dump_verbose(uint8_t *data, int len, int tab_num)
{
    hex_byte_dump(data, len, tab_num);
}
