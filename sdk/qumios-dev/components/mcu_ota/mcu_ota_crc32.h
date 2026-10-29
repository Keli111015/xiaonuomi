#ifndef MCU_OTA_CRC32_H
#define MCU_OTA_CRC32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** CRC32 初始值。 */
uint32_t mcu_ota_crc32_init(void);

/** 分段更新 CRC32。 */
uint32_t mcu_ota_crc32_update(uint32_t crc,
                              const uint8_t *data,
                              uint32_t len);

/** 结束 CRC32 计算并返回最终值。 */
uint32_t mcu_ota_crc32_finish(uint32_t crc);

/** 一次性计算 CRC32。 */
uint32_t mcu_ota_crc32(const uint8_t *data, uint32_t len);

/** 将 CRC32 转成协议使用的大端 4 字节。 */
void mcu_ota_crc32_to_be(uint32_t crc, uint8_t out[4]);

/** 比较 CRC32 与协议里的大端 4 字节 check_value。相等返回 1，否则返回 0。 */
int mcu_ota_crc32_match_be(uint32_t crc, const uint8_t expected[4]);

#ifdef __cplusplus
}
#endif

#endif
