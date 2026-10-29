#ifndef MCU_OTA_MD5_H
#define MCU_OTA_MD5_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** MD5 分段计算上下文。 */
typedef struct {
    uint32_t total[2];
    uint32_t state[4];
    uint8_t buffer[64];
} mcu_ota_md5_t;

/** 初始化 MD5 上下文。 */
void mcu_ota_md5_init(mcu_ota_md5_t *ctx);

/** 分段更新 MD5。 */
void mcu_ota_md5_update(mcu_ota_md5_t *ctx,
                        const uint8_t *data,
                        uint32_t len);

/** 结束 MD5 计算，输出 16 字节摘要。 */
void mcu_ota_md5_finish(mcu_ota_md5_t *ctx, uint8_t out[16]);

/** 一次性计算 MD5。 */
void mcu_ota_md5(const uint8_t *data, uint32_t len, uint8_t out[16]);

#ifdef __cplusplus
}
#endif

#endif
