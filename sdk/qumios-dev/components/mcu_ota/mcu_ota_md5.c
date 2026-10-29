#include "mcu_ota_md5.h"

#include <string.h>

/* 这里实现的是标准 MD5 分段计算，只依赖 string.h，便于第三方 MCU 移植。 */
#define MD5_F(x, y, z) (((x) & (y)) | (~(x) & (z)))
#define MD5_G(x, y, z) (((x) & (z)) | ((y) & ~(z)))
#define MD5_H(x, y, z) ((x) ^ (y) ^ (z))
#define MD5_I(x, y, z) ((y) ^ ((x) | ~(z)))
#define MD5_ROTL(x, n) (((x) << (n)) | ((x) >> (32U - (n))))
#define MD5_STEP(f, a, b, c, d, x, s, ac) \
    do { \
        (a) += f((b), (c), (d)) + (x) + (uint32_t)(ac); \
        (a) = MD5_ROTL((a), (s)); \
        (a) += (b); \
    } while (0)

static uint32_t get_u32_le(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static void put_u32_le(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8);
    data[2] = (uint8_t)(value >> 16);
    data[3] = (uint8_t)(value >> 24);
}

static void md5_process(mcu_ota_md5_t *ctx, const uint8_t data[64])
{
    uint32_t x[16];
    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint8_t i;

    /* MD5 每次处理 64 字节块，块内整数按小端序解释。 */
    for (i = 0; i < 16; i++) {
        x[i] = get_u32_le(&data[i * 4U]);
    }

    MD5_STEP(MD5_F, a, b, c, d, x[0],  7, 0xD76AA478U);
    MD5_STEP(MD5_F, d, a, b, c, x[1], 12, 0xE8C7B756U);
    MD5_STEP(MD5_F, c, d, a, b, x[2], 17, 0x242070DBU);
    MD5_STEP(MD5_F, b, c, d, a, x[3], 22, 0xC1BDCEEEU);
    MD5_STEP(MD5_F, a, b, c, d, x[4],  7, 0xF57C0FAFU);
    MD5_STEP(MD5_F, d, a, b, c, x[5], 12, 0x4787C62AU);
    MD5_STEP(MD5_F, c, d, a, b, x[6], 17, 0xA8304613U);
    MD5_STEP(MD5_F, b, c, d, a, x[7], 22, 0xFD469501U);
    MD5_STEP(MD5_F, a, b, c, d, x[8],  7, 0x698098D8U);
    MD5_STEP(MD5_F, d, a, b, c, x[9], 12, 0x8B44F7AFU);
    MD5_STEP(MD5_F, c, d, a, b, x[10], 17, 0xFFFF5BB1U);
    MD5_STEP(MD5_F, b, c, d, a, x[11], 22, 0x895CD7BEU);
    MD5_STEP(MD5_F, a, b, c, d, x[12],  7, 0x6B901122U);
    MD5_STEP(MD5_F, d, a, b, c, x[13], 12, 0xFD987193U);
    MD5_STEP(MD5_F, c, d, a, b, x[14], 17, 0xA679438EU);
    MD5_STEP(MD5_F, b, c, d, a, x[15], 22, 0x49B40821U);

    MD5_STEP(MD5_G, a, b, c, d, x[1],  5, 0xF61E2562U);
    MD5_STEP(MD5_G, d, a, b, c, x[6],  9, 0xC040B340U);
    MD5_STEP(MD5_G, c, d, a, b, x[11], 14, 0x265E5A51U);
    MD5_STEP(MD5_G, b, c, d, a, x[0], 20, 0xE9B6C7AAU);
    MD5_STEP(MD5_G, a, b, c, d, x[5],  5, 0xD62F105DU);
    MD5_STEP(MD5_G, d, a, b, c, x[10],  9, 0x02441453U);
    MD5_STEP(MD5_G, c, d, a, b, x[15], 14, 0xD8A1E681U);
    MD5_STEP(MD5_G, b, c, d, a, x[4], 20, 0xE7D3FBC8U);
    MD5_STEP(MD5_G, a, b, c, d, x[9],  5, 0x21E1CDE6U);
    MD5_STEP(MD5_G, d, a, b, c, x[14],  9, 0xC33707D6U);
    MD5_STEP(MD5_G, c, d, a, b, x[3], 14, 0xF4D50D87U);
    MD5_STEP(MD5_G, b, c, d, a, x[8], 20, 0x455A14EDU);
    MD5_STEP(MD5_G, a, b, c, d, x[13],  5, 0xA9E3E905U);
    MD5_STEP(MD5_G, d, a, b, c, x[2],  9, 0xFCEFA3F8U);
    MD5_STEP(MD5_G, c, d, a, b, x[7], 14, 0x676F02D9U);
    MD5_STEP(MD5_G, b, c, d, a, x[12], 20, 0x8D2A4C8AU);

    MD5_STEP(MD5_H, a, b, c, d, x[5],  4, 0xFFFA3942U);
    MD5_STEP(MD5_H, d, a, b, c, x[8], 11, 0x8771F681U);
    MD5_STEP(MD5_H, c, d, a, b, x[11], 16, 0x6D9D6122U);
    MD5_STEP(MD5_H, b, c, d, a, x[14], 23, 0xFDE5380CU);
    MD5_STEP(MD5_H, a, b, c, d, x[1],  4, 0xA4BEEA44U);
    MD5_STEP(MD5_H, d, a, b, c, x[4], 11, 0x4BDECFA9U);
    MD5_STEP(MD5_H, c, d, a, b, x[7], 16, 0xF6BB4B60U);
    MD5_STEP(MD5_H, b, c, d, a, x[10], 23, 0xBEBFBC70U);
    MD5_STEP(MD5_H, a, b, c, d, x[13],  4, 0x289B7EC6U);
    MD5_STEP(MD5_H, d, a, b, c, x[0], 11, 0xEAA127FAU);
    MD5_STEP(MD5_H, c, d, a, b, x[3], 16, 0xD4EF3085U);
    MD5_STEP(MD5_H, b, c, d, a, x[6], 23, 0x04881D05U);
    MD5_STEP(MD5_H, a, b, c, d, x[9],  4, 0xD9D4D039U);
    MD5_STEP(MD5_H, d, a, b, c, x[12], 11, 0xE6DB99E5U);
    MD5_STEP(MD5_H, c, d, a, b, x[15], 16, 0x1FA27CF8U);
    MD5_STEP(MD5_H, b, c, d, a, x[2], 23, 0xC4AC5665U);

    MD5_STEP(MD5_I, a, b, c, d, x[0],  6, 0xF4292244U);
    MD5_STEP(MD5_I, d, a, b, c, x[7], 10, 0x432AFF97U);
    MD5_STEP(MD5_I, c, d, a, b, x[14], 15, 0xAB9423A7U);
    MD5_STEP(MD5_I, b, c, d, a, x[5], 21, 0xFC93A039U);
    MD5_STEP(MD5_I, a, b, c, d, x[12],  6, 0x655B59C3U);
    MD5_STEP(MD5_I, d, a, b, c, x[3], 10, 0x8F0CCC92U);
    MD5_STEP(MD5_I, c, d, a, b, x[10], 15, 0xFFEFF47DU);
    MD5_STEP(MD5_I, b, c, d, a, x[1], 21, 0x85845DD1U);
    MD5_STEP(MD5_I, a, b, c, d, x[8],  6, 0x6FA87E4FU);
    MD5_STEP(MD5_I, d, a, b, c, x[15], 10, 0xFE2CE6E0U);
    MD5_STEP(MD5_I, c, d, a, b, x[6], 15, 0xA3014314U);
    MD5_STEP(MD5_I, b, c, d, a, x[13], 21, 0x4E0811A1U);
    MD5_STEP(MD5_I, a, b, c, d, x[4],  6, 0xF7537E82U);
    MD5_STEP(MD5_I, d, a, b, c, x[11], 10, 0xBD3AF235U);
    MD5_STEP(MD5_I, c, d, a, b, x[2], 15, 0x2AD7D2BBU);
    MD5_STEP(MD5_I, b, c, d, a, x[9], 21, 0xEB86D391U);

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
}

void mcu_ota_md5_init(mcu_ota_md5_t *ctx)
{
    if (!ctx) {
        return;
    }

    memset(ctx, 0, sizeof(*ctx));
    /* 标准 MD5 初始状态。 */
    ctx->state[0] = 0x67452301U;
    ctx->state[1] = 0xEFCDAB89U;
    ctx->state[2] = 0x98BADCFEU;
    ctx->state[3] = 0x10325476U;
}

void mcu_ota_md5_update(mcu_ota_md5_t *ctx,
                        const uint8_t *data,
                        uint32_t len)
{
    uint32_t left;
    uint32_t fill;

    if (!ctx || (!data && len > 0)) {
        return;
    }

    /* total 记录已输入的字节数，finish 时会转换为 bit 长度写入尾部。 */
    left = ctx->total[0] & 0x3FU;
    fill = 64U - left;
    ctx->total[0] += len;
    if (ctx->total[0] < len) {
        ctx->total[1]++;
    }

    if (left && len >= fill) {
        /* 先补齐上一次剩余的半块数据。 */
        memcpy(ctx->buffer + left, data, fill);
        md5_process(ctx, ctx->buffer);
        data += fill;
        len -= fill;
        left = 0;
    }

    while (len >= 64U) {
        /* 输入中完整的 64 字节块可以直接处理。 */
        md5_process(ctx, data);
        data += 64U;
        len -= 64U;
    }

    if (len > 0) {
        memcpy(ctx->buffer + left, data, len);
    }
}

void mcu_ota_md5_finish(mcu_ota_md5_t *ctx, uint8_t out[16])
{
    static const uint8_t padding[64] = {0x80};
    uint8_t msglen[8];
    uint32_t high;
    uint32_t low;
    uint32_t last;
    uint32_t padn;
    uint8_t i;

    if (!ctx || !out) {
        return;
    }

    /* MD5 结尾追加原始消息 bit 长度，低 32 位在前。 */
    high = (ctx->total[0] >> 29) | (ctx->total[1] << 3);
    low = ctx->total[0] << 3;
    put_u32_le(&msglen[0], low);
    put_u32_le(&msglen[4], high);

    /* 填充到 56 mod 64，再追加 8 字节长度。 */
    last = ctx->total[0] & 0x3FU;
    padn = (last < 56U) ? (56U - last) : (120U - last);
    mcu_ota_md5_update(ctx, padding, padn);
    mcu_ota_md5_update(ctx, msglen, sizeof(msglen));

    for (i = 0; i < 4; i++) {
        put_u32_le(&out[i * 4U], ctx->state[i]);
    }

    memset(ctx, 0, sizeof(*ctx));
}

void mcu_ota_md5(const uint8_t *data, uint32_t len, uint8_t out[16])
{
    mcu_ota_md5_t ctx;

    mcu_ota_md5_init(&ctx);
    mcu_ota_md5_update(&ctx, data, len);
    mcu_ota_md5_finish(&ctx, out);
}
