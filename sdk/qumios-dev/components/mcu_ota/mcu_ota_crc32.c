#include "mcu_ota_crc32.h"

/* 16 项查表版 CRC32，比 256 项表更省 ROM，适合资源较小的 MCU。 */
static const uint32_t s_crc32_table[16] = {
    0x00000000U, 0x1DB71064U, 0x3B6E20C8U, 0x26D930ACU,
    0x76DC4190U, 0x6B6B51F4U, 0x4DB26158U, 0x5005713CU,
    0xEDB88320U, 0xF00F9344U, 0xD6D6A3E8U, 0xCB61B38CU,
    0x9B64C2B0U, 0x86D3D2D4U, 0xA00AE278U, 0xBDBDF21CU,
};

uint32_t mcu_ota_crc32_init(void)
{
    /* 使用常见 CRC32/IEEE 初始值。 */
    return 0xFFFFFFFFU;
}

uint32_t mcu_ota_crc32_update(uint32_t crc,
                              const uint8_t *data,
                              uint32_t len)
{
    uint32_t i;

    if (!data && len > 0) {
        return crc;
    }

    for (i = 0; i < len; i++) {
        crc ^= data[i];
        /* 每字节按高低两个 4-bit 分两次更新。 */
        crc = s_crc32_table[crc & 0x0FU] ^ (crc >> 4);
        crc = s_crc32_table[crc & 0x0FU] ^ (crc >> 4);
    }

    return crc;
}

uint32_t mcu_ota_crc32_finish(uint32_t crc)
{
    /* CRC32/IEEE 最终结果需要取反。 */
    return ~crc;
}

uint32_t mcu_ota_crc32(const uint8_t *data, uint32_t len)
{
    return mcu_ota_crc32_finish(mcu_ota_crc32_update(mcu_ota_crc32_init(),
                                                     data,
                                                     len));
}

void mcu_ota_crc32_to_be(uint32_t crc, uint8_t out[4])
{
    if (!out) {
        return;
    }

    out[0] = (uint8_t)(crc >> 24);
    out[1] = (uint8_t)(crc >> 16);
    out[2] = (uint8_t)(crc >> 8);
    out[3] = (uint8_t)crc;
}

int mcu_ota_crc32_match_be(uint32_t crc, const uint8_t expected[4])
{
    uint8_t value[4];
    uint8_t diff = 0;
    uint8_t i;

    if (!expected) {
        return 0;
    }

    mcu_ota_crc32_to_be(crc, value);
    /* 使用异或累计比较，避免比较过程提前退出。 */
    for (i = 0; i < sizeof(value); i++) {
        diff |= (uint8_t)(value[i] ^ expected[i]);
    }

    return diff == 0;
}
