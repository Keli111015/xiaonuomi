#include "mcu_ota.h"

#if MCU_OTA_USE_CRC32
#include "mcu_ota_crc32.h"
#endif
#if MCU_OTA_USE_MD5
#include "mcu_ota_md5.h"
#endif

#include <string.h>

#define OTA_HEAD0          0xA5U
#define OTA_HEAD1          0xA5U
#define OTA_VERSION        0x03U
#define OTA_FRAME_HEAD_LEN 6U
#define OTA_FRAME_MIN_LEN  7U
#define OTA_BEGIN_BASE_LEN 8U
#define OTA_ENTER_LEN      1U
#define OTA_ACK_LEN        8U
#define OTA_NOTIFY_LEN     11U
#define OTA_DATA_HEAD_LEN  8U
#define OTA_MAX_PAYLOAD_LEN (OTA_DATA_HEAD_LEN + MCU_OTA_MAX_CHUNK_SIZE)

enum {
    OTA_CMD_ENTER  = 0x35,
    OTA_CMD_BEGIN  = 0x36,
    OTA_CMD_DATA   = 0x37,
    OTA_CMD_END    = 0x38,
    OTA_CMD_ABORT  = 0x39,
    OTA_CMD_ACK    = 0x3A,
    OTA_CMD_NOTIFY = 0x3B,
};

enum {
    OTA_NOTIFY_READY = 1,
    OTA_NOTIFY_ERROR = 2,
};

/* 组件只保存一条 OTA 会话，适合 bootloader 或单任务串口接收场景。 */
typedef struct {
    mcu_ota_config_t cfg;
    mcu_ota_info_t info;
    mcu_ota_state_t state;
#if MCU_OTA_USE_CRC32
    uint32_t crc32;
#endif
#if MCU_OTA_USE_MD5
    mcu_ota_md5_t md5;
#endif
    uint8_t rx_buf[OTA_FRAME_MIN_LEN + OTA_DATA_HEAD_LEN + MCU_OTA_MAX_CHUNK_SIZE];
    uint16_t rx_len;
    uint16_t payload_len;
} mcu_ota_ctx_t;

static mcu_ota_ctx_t s_ota;

static uint16_t get_u16_be(const uint8_t *data)
{
    return (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static uint32_t get_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) |
           ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) |
           data[3];
}

static void put_u16_be(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)(value >> 8);
    data[1] = (uint8_t)value;
}

static void put_u32_be(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static uint8_t check_sum(const uint8_t *data, uint16_t len)
{
    uint16_t i;
    uint8_t sum = 0;

    for (i = 0; i < len; i++) {
        sum = (uint8_t)(sum + data[i]);
    }

    return sum;
}

/* 根据编译期开关判断当前固件包携带的校验值长度；未启用的算法返回 0。 */
static uint16_t check_value_len(uint8_t check_type)
{
#if MCU_OTA_USE_CRC32
    if (check_type == MCU_OTA_CHECK_CRC32) {
        return MCU_OTA_CRC32_VALUE_LEN;
    }
#endif
#if MCU_OTA_USE_MD5
    if (check_type == MCU_OTA_CHECK_MD5) {
        return MCU_OTA_MD5_VALUE_LEN;
    }
#endif

    return 0;
}

/* 回调可直接返回 MCU_OTA_ACK_*；其他非 0 值会映射为当前阶段默认错误。 */
static uint8_t callback_code(int ret, uint8_t default_error)
{
    if (ret == 0) {
        return MCU_OTA_ACK_OK;
    }

    if (ret > 0 && ret <= MCU_OTA_ACK_UNSUPPORTED) {
        return (uint8_t)ret;
    }

    return default_error;
}

/* 统一封装 ACK/NOTIFY 的完整串口帧，最终通过 send_cb 发出。 */
static int send_cmd(mcu_ota_ctx_t *ctx,
                    uint8_t cmd,
                    const uint8_t *payload,
                    uint16_t payload_len)
{
    uint8_t frame[OTA_FRAME_MIN_LEN + OTA_NOTIFY_LEN];

    if (!ctx || !ctx->cfg.send_cb ||
        payload_len > OTA_NOTIFY_LEN ||
        (payload_len > 0 && !payload)) {
        return MCU_OTA_ERR_PARAM;
    }

    frame[0] = OTA_HEAD0;
    frame[1] = OTA_HEAD1;
    frame[2] = OTA_VERSION;
    frame[3] = cmd;
    put_u16_be(&frame[4], payload_len);

    if (payload_len > 0) {
        memcpy(&frame[OTA_FRAME_HEAD_LEN],
               payload,
               payload_len);
    }

    frame[OTA_FRAME_HEAD_LEN + payload_len] =
        check_sum(frame, (uint16_t)(OTA_FRAME_HEAD_LEN +
                                    payload_len));

    if (ctx->cfg.send_cb(frame,
                         (uint16_t)(OTA_FRAME_MIN_LEN +
                                    payload_len),
                         ctx->cfg.user_data) != 0) {
        return MCU_OTA_ERR_SEND;
    }

    return MCU_OTA_OK;
}

static int send_ack(mcu_ota_ctx_t *ctx,
                    uint8_t acked_cmd,
                    uint8_t code)
{
    uint8_t payload[OTA_ACK_LEN];

    payload[0] = acked_cmd;
    payload[1] = code;
    put_u16_be(&payload[2], ctx->info.seq);
    put_u32_be(&payload[4], ctx->info.offset);

    return send_cmd(ctx, OTA_CMD_ACK, payload, sizeof(payload));
}

static int send_notify(mcu_ota_ctx_t *ctx,
                       uint8_t reason,
                       uint32_t error_code)
{
    uint8_t payload[OTA_NOTIFY_LEN];

    payload[0] = reason;
    put_u16_be(&payload[1], ctx->info.seq);
    put_u32_be(&payload[3], ctx->info.offset);
    put_u32_be(&payload[7], error_code);

    return send_cmd(ctx,
                    OTA_CMD_NOTIFY,
                    payload,
                    sizeof(payload));
}

static void reset_transfer(mcu_ota_ctx_t *ctx)
{
    memset(&ctx->info, 0, sizeof(ctx->info));
#if MCU_OTA_USE_CRC32
    ctx->crc32 = 0;
#endif
#if MCU_OTA_USE_MD5
    memset(&ctx->md5, 0, sizeof(ctx->md5));
#endif
}

/* OTA_BEGIN 参数合法后初始化整包校验上下文。 */
static void init_image_check(mcu_ota_ctx_t *ctx)
{
#if MCU_OTA_USE_CRC32
    if (ctx->info.check_type == MCU_OTA_CHECK_CRC32) {
        ctx->crc32 = mcu_ota_crc32_init();
    }
#endif
#if MCU_OTA_USE_MD5
    if (ctx->info.check_type == MCU_OTA_CHECK_MD5) {
        mcu_ota_md5_init(&ctx->md5);
    }
#endif
}

/* 每收到一个合法分片就累计校验值，校验的是串口收到的原始数据。 */
static void update_image_check(mcu_ota_ctx_t *ctx,
                               const uint8_t *data,
                               uint16_t len)
{
#if MCU_OTA_USE_CRC32
    if (ctx->info.check_type == MCU_OTA_CHECK_CRC32) {
        ctx->crc32 = mcu_ota_crc32_update(ctx->crc32, data, len);
    }
#endif
#if MCU_OTA_USE_MD5
    if (ctx->info.check_type == MCU_OTA_CHECK_MD5) {
        mcu_ota_md5_update(&ctx->md5, data, len);
    }
#endif
}

/* OTA_END 时计算最终校验值，并与 OTA_BEGIN 里的 check_value 比较。 */
static int image_check_ok(mcu_ota_ctx_t *ctx)
{
#if MCU_OTA_USE_CRC32
    if (ctx->info.check_type == MCU_OTA_CHECK_CRC32) {
        ctx->crc32 = mcu_ota_crc32_finish(ctx->crc32);
        return mcu_ota_crc32_match_be(ctx->crc32,
                                      ctx->info.check_value);
    }
#endif
#if MCU_OTA_USE_MD5
    uint8_t md5_value[MCU_OTA_MD5_VALUE_LEN];

    if (ctx->info.check_type == MCU_OTA_CHECK_MD5) {
        mcu_ota_md5_finish(&ctx->md5, md5_value);
        return memcmp(md5_value,
                      ctx->info.check_value,
                      sizeof(md5_value)) == 0;
    }
#endif

    return 0;
}

/* 处理进入 OTA 模式请求；App 可在 enter_cb 中决定是否重启到 bootloader。 */
static int handle_enter(mcu_ota_ctx_t *ctx,
                        const uint8_t *payload,
                        uint16_t payload_len)
{
    int ret = 0;
    uint8_t flags;
    uint8_t code;

    if (!payload || payload_len != OTA_ENTER_LEN) {
        return send_ack(ctx,
                        OTA_CMD_ENTER,
                        MCU_OTA_ACK_PARAM_ERROR);
    }

    flags = payload[0];
    if (ctx->cfg.enter_cb) {
        ret = ctx->cfg.enter_cb(flags, ctx->cfg.user_data);
    }

    code = callback_code(ret, MCU_OTA_ACK_ERROR);
    if (code == MCU_OTA_ACK_OK) {
        reset_transfer(ctx);
        ctx->info.flags = flags;
        ctx->state = MCU_OTA_STATE_READY;
    }

    return send_ack(ctx, OTA_CMD_ENTER, code);
}

/* 处理 OTA_BEGIN：记录固件大小、分片大小、校验方式，并通知平台准备升级区。 */
static int handle_begin(mcu_ota_ctx_t *ctx,
                        const uint8_t *payload,
                        uint16_t payload_len)
{
    uint32_t image_size;
    uint16_t chunk_size;
    uint8_t check_type;
    uint16_t value_len;
    int ret = 0;
    uint8_t code;

    if (!payload || payload_len < OTA_BEGIN_BASE_LEN) {
        return send_ack(ctx,
                        OTA_CMD_BEGIN,
                        MCU_OTA_ACK_PARAM_ERROR);
    }

    image_size = get_u32_be(&payload[0]);
    chunk_size = get_u16_be(&payload[4]);
    check_type = payload[6];
    value_len = check_value_len(check_type);

    if (image_size == 0 ||
        chunk_size == 0 ||
        chunk_size > MCU_OTA_MAX_CHUNK_SIZE ||
        value_len == 0 ||
        payload_len != (uint16_t)(OTA_BEGIN_BASE_LEN + value_len)) {
        return send_ack(ctx,
                        OTA_CMD_BEGIN,
                        value_len == 0 ? MCU_OTA_ACK_UNSUPPORTED :
                                         MCU_OTA_ACK_PARAM_ERROR);
    }

    if (ctx->state == MCU_OTA_STATE_RECEIVING) {
        return send_ack(ctx,
                        OTA_CMD_BEGIN,
                        MCU_OTA_ACK_BUSY);
    }

    reset_transfer(ctx);
    ctx->info.image_size = image_size;
    ctx->info.chunk_size = chunk_size;
    ctx->info.check_type = check_type;
    ctx->info.flags = payload[7];
    memcpy(ctx->info.check_value, &payload[8], value_len);
    init_image_check(ctx);

    if (ctx->cfg.begin_cb) {
        ret = ctx->cfg.begin_cb(&ctx->info, ctx->cfg.user_data);
    }

    code = callback_code(ret, MCU_OTA_ACK_ERROR);
    if (code == MCU_OTA_ACK_OK) {
        ctx->state = MCU_OTA_STATE_RECEIVING;
    } else {
        reset_transfer(ctx);
        ctx->state = MCU_OTA_STATE_FAILED;
    }

    return send_ack(ctx, OTA_CMD_BEGIN, code);
}

/* 处理 OTA_DATA：只接受期望的 seq/offset，避免重复包或乱序包写入。 */
static int handle_data(mcu_ota_ctx_t *ctx,
                       const uint8_t *payload,
                       uint16_t payload_len)
{
    uint32_t offset;
    uint16_t seq;
    uint16_t data_len;
    const uint8_t *data;
    int ret;

    if (!payload || payload_len < OTA_DATA_HEAD_LEN) {
        return send_ack(ctx,
                        OTA_CMD_DATA,
                        MCU_OTA_ACK_PARAM_ERROR);
    }

    if (ctx->state != MCU_OTA_STATE_RECEIVING) {
        return send_ack(ctx,
                        OTA_CMD_DATA,
                        MCU_OTA_ACK_SEQUENCE_ERROR);
    }

    offset = get_u32_be(&payload[0]);
    seq = get_u16_be(&payload[4]);
    data_len = get_u16_be(&payload[6]);
    data = &payload[OTA_DATA_HEAD_LEN];

    if (data_len == 0 ||
        data_len > ctx->info.chunk_size ||
        payload_len != (uint16_t)(OTA_DATA_HEAD_LEN + data_len) ||
        offset > ctx->info.image_size ||
        (uint32_t)data_len > (ctx->info.image_size - offset)) {
        return send_ack(ctx,
                        OTA_CMD_DATA,
                        MCU_OTA_ACK_PARAM_ERROR);
    }

    if (offset != ctx->info.offset || seq != ctx->info.seq) {
        return send_ack(ctx,
                        OTA_CMD_DATA,
                        MCU_OTA_ACK_SEQUENCE_ERROR);
    }

    if (!ctx->cfg.write_cb) {
        ctx->state = MCU_OTA_STATE_FAILED;
        return send_ack(ctx,
                        OTA_CMD_DATA,
                        MCU_OTA_ACK_WRITE_ERROR);
    }

    /* 先累计校验，再通知平台写入，避免平台回调意外改动 data 缓冲区。 */
    update_image_check(ctx, data, data_len);

    ret = ctx->cfg.write_cb(offset, data, data_len, ctx->cfg.user_data);
    if (ret != 0) {
        ctx->state = MCU_OTA_STATE_FAILED;
        return send_ack(ctx,
                        OTA_CMD_DATA,
                        callback_code(ret, MCU_OTA_ACK_WRITE_ERROR));
    }

    ctx->info.offset += data_len;
    ctx->info.seq++;

    return send_ack(ctx,
                    OTA_CMD_DATA,
                    MCU_OTA_ACK_OK);
}

/* 处理 OTA_END：先确认长度和整包校验，再调用 finish_cb 做固件确认。 */
static int handle_end(mcu_ota_ctx_t *ctx,
                      const uint8_t *payload,
                      uint16_t payload_len)
{
    int ret = 0;
    uint8_t code;

    (void)payload;

    if (payload_len != 0) {
        return send_ack(ctx,
                        OTA_CMD_END,
                        MCU_OTA_ACK_PARAM_ERROR);
    }

    if (ctx->state != MCU_OTA_STATE_RECEIVING ||
        ctx->info.offset != ctx->info.image_size) {
        return send_ack(ctx,
                        OTA_CMD_END,
                        MCU_OTA_ACK_SEQUENCE_ERROR);
    }

    if (!image_check_ok(ctx)) {
        ctx->state = MCU_OTA_STATE_FAILED;
        return send_ack(ctx,
                        OTA_CMD_END,
                        MCU_OTA_ACK_CHECK_ERROR);
    }

    if (ctx->cfg.finish_cb) {
        ret = ctx->cfg.finish_cb(&ctx->info, ctx->cfg.user_data);
    }

    code = callback_code(ret, MCU_OTA_ACK_CHECK_ERROR);
    if (code == MCU_OTA_ACK_OK) {
        ctx->state = MCU_OTA_STATE_DONE;
    } else {
        ctx->state = MCU_OTA_STATE_FAILED;
    }

    return send_ack(ctx, OTA_CMD_END, code);
}

/* 处理 OTA_ABORT：平台可在 abort_cb 中清理临时升级区。 */
static int handle_abort(mcu_ota_ctx_t *ctx,
                        const uint8_t *payload,
                        uint16_t payload_len)
{
    (void)payload;

    if (payload_len != 0) {
        return send_ack(ctx,
                        OTA_CMD_ABORT,
                        MCU_OTA_ACK_PARAM_ERROR);
    }

    if (ctx->cfg.abort_cb) {
        ctx->cfg.abort_cb(ctx->cfg.user_data);
    }

    reset_transfer(ctx);
    ctx->state = MCU_OTA_STATE_ABORTED;

    return send_ack(ctx,
                    OTA_CMD_ABORT,
                    MCU_OTA_ACK_OK);
}

void mcu_ota_init(const mcu_ota_config_t *cfg)
{
    memset(&s_ota, 0, sizeof(s_ota));
    if (cfg) {
        s_ota.cfg = *cfg;
    }
    s_ota.state = MCU_OTA_STATE_IDLE;
}

/* 校验完整帧后按 CMD 分发到对应处理函数。 */
static int parse_frame(mcu_ota_ctx_t *ctx,
                       const uint8_t *frame,
                       uint16_t len)
{
    uint8_t cmd;
    uint16_t payload_len;
    const uint8_t *payload;

    if (!ctx || !frame || len < OTA_FRAME_MIN_LEN) {
        return MCU_OTA_ERR_PARAM;
    }

    if (frame[0] != OTA_HEAD0 ||
        frame[1] != OTA_HEAD1 ||
        frame[2] != OTA_VERSION) {
        return MCU_OTA_ERR_FRAME;
    }

    payload_len = get_u16_be(&frame[4]);
    if (payload_len > OTA_MAX_PAYLOAD_LEN ||
        len != (uint16_t)(OTA_FRAME_MIN_LEN + payload_len)) {
        return MCU_OTA_ERR_FRAME;
    }

    if (check_sum(frame,
                  (uint16_t)(OTA_FRAME_HEAD_LEN +
                             payload_len)) != frame[len - 1]) {
        return MCU_OTA_ERR_FRAME;
    }

    cmd = frame[3];
    payload = &frame[OTA_FRAME_HEAD_LEN];

    switch (cmd) {
    case OTA_CMD_ENTER:
        return handle_enter(ctx, payload, payload_len);
    case OTA_CMD_BEGIN:
        return handle_begin(ctx, payload, payload_len);
    case OTA_CMD_DATA:
        return handle_data(ctx, payload, payload_len);
    case OTA_CMD_END:
        return handle_end(ctx, payload, payload_len);
    case OTA_CMD_ABORT:
        return handle_abort(ctx, payload, payload_len);
    default:
        return MCU_OTA_ERR_FRAME;
    }
}

int mcu_ota_feed(uint8_t byte)
{
    mcu_ota_ctx_t *ctx = &s_ota;
    uint16_t expected_len;

    if (ctx->rx_len == 0) {
        if (byte != OTA_HEAD0) {
            return MCU_OTA_OK;
        }
        ctx->rx_buf[ctx->rx_len++] = byte;
        return MCU_OTA_OK;
    }

    /* 第二个帧头字节不匹配时，如果当前字节仍是 0xA5，就作为新帧头继续同步。 */
    if (ctx->rx_len == 1) {
        if (byte != OTA_HEAD1) {
            ctx->rx_len = (byte == OTA_HEAD0) ? 1U : 0U;
            ctx->rx_buf[0] = byte;
            return MCU_OTA_OK;
        }
        ctx->rx_buf[ctx->rx_len++] = byte;
        return MCU_OTA_OK;
    }

    if (ctx->rx_len >= sizeof(ctx->rx_buf)) {
        ctx->rx_len = 0;
        return MCU_OTA_ERR_FRAME;
    }

    ctx->rx_buf[ctx->rx_len++] = byte;

    if (ctx->rx_len == 3 &&
        ctx->rx_buf[2] != OTA_VERSION) {
        ctx->rx_len = 0;
        return MCU_OTA_ERR_FRAME;
    }

    /* 收到帧头和长度后，就能提前判断本帧总长度是否超过接收缓冲区。 */
    if (ctx->rx_len == OTA_FRAME_HEAD_LEN) {
        ctx->payload_len = get_u16_be(&ctx->rx_buf[4]);
        if (ctx->payload_len > OTA_MAX_PAYLOAD_LEN) {
            ctx->rx_len = 0;
            return MCU_OTA_ERR_FRAME;
        }
    }

    if (ctx->rx_len < OTA_FRAME_HEAD_LEN) {
        return MCU_OTA_OK;
    }

    expected_len = (uint16_t)(OTA_FRAME_MIN_LEN +
                              ctx->payload_len);
    if (ctx->rx_len < expected_len) {
        return MCU_OTA_OK;
    }

    ctx->rx_len = 0;
    return parse_frame(ctx, ctx->rx_buf, expected_len);
}

int mcu_ota_notify_ready(void)
{
    mcu_ota_ctx_t *ctx = &s_ota;

    ctx->state = MCU_OTA_STATE_READY;
    return send_notify(ctx, OTA_NOTIFY_READY, 0);
}

int mcu_ota_notify_error(uint32_t error_code)
{
    mcu_ota_ctx_t *ctx = &s_ota;

    ctx->state = MCU_OTA_STATE_FAILED;
    return send_notify(ctx, OTA_NOTIFY_ERROR, error_code);
}

mcu_ota_state_t mcu_ota_get_state(void)
{
    return s_ota.state;
}

void mcu_ota_get_info(mcu_ota_info_t *info)
{
    if (!info) {
        return;
    }

    *info = s_ota.info;
}
