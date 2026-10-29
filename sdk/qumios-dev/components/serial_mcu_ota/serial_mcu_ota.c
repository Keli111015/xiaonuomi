#include "serial_mcu_ota.h"

#include "qm_types.h"
#include "qm_utils_timer.h"
#include "serial_protocol.h"

#include <stddef.h>
#include <string.h>

#define MCU_OTA_CMD_ENTER         SERIAL_MCU_OTA_CMD_ENTER
#define MCU_OTA_CMD_BEGIN         SERIAL_MCU_OTA_CMD_BEGIN
#define MCU_OTA_CMD_DATA          SERIAL_MCU_OTA_CMD_DATA
#define MCU_OTA_CMD_END           SERIAL_MCU_OTA_CMD_END
#define MCU_OTA_CMD_ABORT         SERIAL_MCU_OTA_CMD_ABORT
#define MCU_OTA_CMD_ACK           SERIAL_MCU_OTA_CMD_ACK
#define MCU_OTA_CMD_NOTIFY        SERIAL_MCU_OTA_CMD_NOTIFY

#define MCU_OTA_BEGIN_HEAD_LEN    8U
#define MCU_OTA_DATA_HEAD_LEN     8U
#define MCU_OTA_ENTER_PAYLOAD_LEN 1U
#define MCU_OTA_ACK_PAYLOAD_LEN   8U
#define MCU_OTA_NOTIFY_PAYLOAD_LEN 11U
#define MCU_OTA_MAX_PAYLOAD_LEN   (MCU_OTA_DATA_HEAD_LEN + SERIAL_MCU_OTA_MAX_CHUNK_SIZE)

#define MCU_OTA_ACK_OK             0U
#define MCU_OTA_ACK_SEQUENCE_ERROR 4U

#ifndef CONFIG_SERIAL_MCU_OTA_CHUNK_SIZE
#define CONFIG_SERIAL_MCU_OTA_CHUNK_SIZE SERIAL_MCU_OTA_DEFAULT_CHUNK_SIZE
#endif

#ifndef CONFIG_SERIAL_MCU_OTA_TIMEOUT_MS
#define CONFIG_SERIAL_MCU_OTA_TIMEOUT_MS SERIAL_MCU_OTA_DEFAULT_TIMEOUT_MS
#endif

#ifndef CONFIG_SERIAL_MCU_OTA_RETRY_MAX
#define CONFIG_SERIAL_MCU_OTA_RETRY_MAX SERIAL_MCU_OTA_DEFAULT_RETRY_MAX
#endif

typedef struct {
    uint8_t acked_cmd;
    uint8_t code;
    uint16_t next_seq;
    uint32_t next_offset;
} mcu_ota_ack_t;

typedef struct {
    uint8_t reason;
    uint16_t next_seq;
    uint32_t next_offset;
    uint32_t error_code;
} mcu_ota_notify_t;

typedef struct {
    serial_mcu_ota_state_t state;
    uint32_t total_size;
    uint32_t offset;
    uint16_t seq;
    uint16_t chunk_size;
    uint8_t check_type;
    uint8_t flags;
    uint8_t waiting_mcu_ready;
    uint8_t check_value[SERIAL_MCU_OTA_CHECK_VALUE_LEN];
    uint32_t timeout_ms;
    qm_utils_time_t ack_timer;
    uint8_t retry_max;
    uint8_t retry_count;
    uint8_t waiting_ack;
    uint8_t last_cmd;
    uint8_t last_ack_code;
    int32_t error_code;
    serial_mcu_ota_notify_reason_t notify_reason;
    uint16_t notify_next_seq;
    uint32_t notify_next_offset;
    uint32_t notify_error_code;
    uint8_t last_payload[MCU_OTA_MAX_PAYLOAD_LEN];
    uint16_t last_payload_len;
    serial_mcu_ota_read_chunk_fn read_chunk;
    serial_mcu_ota_notify_fn notify;
    void *user;
} mcu_ota_ctx_t;

static mcu_ota_ctx_t s_ota;

static uint16_t get_u16_be(const uint8_t *data)
{
    uint16_t value;

    memcpy(&value, data, sizeof(value));
    return be16_to_cpu(value);
}

static uint32_t get_u32_be(const uint8_t *data)
{
    uint32_t value;

    memcpy(&value, data, sizeof(value));
    return be32_to_cpu(value);
}

static void put_u16_be(uint8_t *data, uint16_t value)
{
    uint16_t be_value = cpu_to_be16(value);

    memcpy(data, &be_value, sizeof(be_value));
}

static void put_u32_be(uint8_t *data, uint32_t value)
{
    uint32_t be_value = cpu_to_be32(value);

    memcpy(data, &be_value, sizeof(be_value));
}

static int32_t ack_parse(const uint8_t *payload, uint16_t payload_len, mcu_ota_ack_t *ack)
{
    if (!payload || !ack || payload_len != MCU_OTA_ACK_PAYLOAD_LEN) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    ack->acked_cmd = payload[0];
    ack->code = payload[1];
    ack->next_seq = get_u16_be(&payload[2]);
    ack->next_offset = get_u32_be(&payload[4]);

    return SERIAL_MCU_OTA_OK;
}

static int32_t notify_parse(const uint8_t *payload,
                            uint16_t payload_len,
                            mcu_ota_notify_t *ota_notify)
{
    if (!payload || !ota_notify || payload_len != MCU_OTA_NOTIFY_PAYLOAD_LEN) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    ota_notify->reason = payload[0];
    ota_notify->next_seq = get_u16_be(&payload[1]);
    ota_notify->next_offset = get_u32_be(&payload[3]);
    ota_notify->error_code = get_u32_be(&payload[7]);

    return SERIAL_MCU_OTA_OK;
}

static void build_info(serial_mcu_ota_info_t *info)
{
    if (!info) {
        return;
    }

    memset(info, 0, sizeof(*info));
    info->total_size = s_ota.total_size;
    info->offset = s_ota.offset;
    info->seq = s_ota.seq;
    info->last_cmd = s_ota.last_cmd;
    info->last_ack_code = s_ota.last_ack_code;
    info->retry_count = s_ota.retry_count;
    info->error_code = s_ota.error_code;
    info->notify_reason = s_ota.notify_reason;
    info->notify_next_seq = s_ota.notify_next_seq;
    info->notify_next_offset = s_ota.notify_next_offset;
    info->notify_error_code = s_ota.notify_error_code;
}

static void notify(serial_mcu_ota_event_t event)
{
    serial_mcu_ota_info_t info;

    if (!s_ota.notify) {
        return;
    }

    build_info(&info);
    s_ota.notify(event, &info, s_ota.user);
}

static int32_t qumi_send_payload(uint8_t cmd, const uint8_t *payload, uint16_t payload_len)
{
    int32_t ret;

    if (payload_len > 0 && !payload) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    ret = serial_generate_send((frame_cmd_t)cmd, 0, (uint8_t *)payload, payload_len);
    if (ret != SERIAL_NO_ERR) {
        return SERIAL_MCU_OTA_ERR_SEND;
    }

    return SERIAL_MCU_OTA_OK;
}

static int32_t send_current_frame(uint8_t cmd,
                                  const uint8_t *payload,
                                  uint16_t payload_len)
{
    int32_t ret;

    if (payload_len > sizeof(s_ota.last_payload) ||
        (payload_len > 0 && !payload)) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    if (payload_len > 0) {
        memcpy(s_ota.last_payload, payload, payload_len);
    }
    s_ota.last_payload_len = payload_len;

    s_ota.last_cmd = cmd;
    ret = qumi_send_payload(s_ota.last_cmd,
                            s_ota.last_payload,
                            s_ota.last_payload_len);
    if (ret != SERIAL_MCU_OTA_OK) {
        return ret;
    }

    s_ota.waiting_ack = 1;
    s_ota.retry_count = 0;
    qm_utils_time_countdown_ms(&s_ota.ack_timer, s_ota.timeout_ms);

    return SERIAL_MCU_OTA_OK;
}

static uint16_t get_check_value_len(uint8_t check_type)
{
    if (check_type == SERIAL_MCU_OTA_CHECK_CRC32) {
        return SERIAL_MCU_OTA_CRC32_VALUE_LEN;
    }

    if (check_type == SERIAL_MCU_OTA_CHECK_MD5) {
        return SERIAL_MCU_OTA_MD5_VALUE_LEN;
    }

    return 0;
}

static int32_t send_begin(void)
{
    uint8_t payload[MCU_OTA_BEGIN_HEAD_LEN + SERIAL_MCU_OTA_CHECK_VALUE_LEN];
    uint16_t check_value_len = get_check_value_len(s_ota.check_type);

    if (check_value_len == 0) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    put_u32_be(&payload[0], s_ota.total_size);
    put_u16_be(&payload[4], s_ota.chunk_size);
    payload[6] = s_ota.check_type;
    payload[7] = s_ota.flags;
    memcpy(&payload[8], s_ota.check_value, check_value_len);

    return send_current_frame(MCU_OTA_CMD_BEGIN,
                              payload,
                              (uint16_t)(MCU_OTA_BEGIN_HEAD_LEN + check_value_len));
}

static int32_t send_data(void)
{
    uint8_t payload[MCU_OTA_DATA_HEAD_LEN + SERIAL_MCU_OTA_MAX_CHUNK_SIZE];
    uint32_t remain;
    uint16_t data_len;
    int32_t ret;

    if (s_ota.offset >= s_ota.total_size) {
        return SERIAL_MCU_OTA_ERR_STATE;
    }

    remain = s_ota.total_size - s_ota.offset;
    data_len = (remain > s_ota.chunk_size) ? s_ota.chunk_size : (uint16_t)remain;

    put_u32_be(&payload[0], s_ota.offset);
    put_u16_be(&payload[4], s_ota.seq);
    put_u16_be(&payload[6], data_len);

    ret = s_ota.read_chunk(s_ota.offset, &payload[MCU_OTA_DATA_HEAD_LEN], data_len, s_ota.user);
    if (ret != 0) {
        return SERIAL_MCU_OTA_ERR_READ;
    }

    return send_current_frame(MCU_OTA_CMD_DATA,
                              payload,
                              (uint16_t)(MCU_OTA_DATA_HEAD_LEN + data_len));
}

static int32_t send_end(void)
{
    return send_current_frame(MCU_OTA_CMD_END, NULL, 0);
}

static int32_t send_abort(void)
{
    return send_current_frame(MCU_OTA_CMD_ABORT, NULL, 0);
}

static int32_t send_enter(void)
{
    uint8_t payload[MCU_OTA_ENTER_PAYLOAD_LEN];

    payload[0] = s_ota.flags;

    return send_current_frame(MCU_OTA_CMD_ENTER, payload, sizeof(payload));
}

static int32_t send_next(void)
{
    if (s_ota.offset < s_ota.total_size) {
        return send_data();
    }

    return send_end();
}

static void fail(int32_t error_code)
{
    s_ota.state = SERIAL_MCU_OTA_STATE_FAILED;
    s_ota.waiting_ack = 0;
    s_ota.error_code = error_code;
    notify(SERIAL_MCU_OTA_EVENT_ERROR);
}

static int32_t handle_mcu_notify(const mcu_ota_notify_t *ota_notify)
{
    int32_t ret;

    if (!ota_notify) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    s_ota.notify_reason = (serial_mcu_ota_notify_reason_t)ota_notify->reason;
    s_ota.notify_next_seq = ota_notify->next_seq;
    s_ota.notify_next_offset = ota_notify->next_offset;
    s_ota.notify_error_code = ota_notify->error_code;

    if (ota_notify->reason == SERIAL_MCU_OTA_NOTIFY_REASON_READY) {
        s_ota.waiting_mcu_ready = 0;
    }

    if (ota_notify->reason == SERIAL_MCU_OTA_NOTIFY_REASON_ERROR) {
        s_ota.waiting_ack = 0;
        s_ota.waiting_mcu_ready = 0;
        s_ota.error_code = (int32_t)ota_notify->error_code;
        s_ota.state = SERIAL_MCU_OTA_STATE_FAILED;
    }

    notify(SERIAL_MCU_OTA_EVENT_MCU_NOTIFY);

    if (ota_notify->reason == SERIAL_MCU_OTA_NOTIFY_REASON_READY &&
        s_ota.state == SERIAL_MCU_OTA_STATE_RUNNING &&
        s_ota.last_cmd == MCU_OTA_CMD_ENTER) {
        s_ota.waiting_ack = 0;
        ret = send_begin();
        if (ret != SERIAL_MCU_OTA_OK) {
            fail(ret);
        } else {
            notify(SERIAL_MCU_OTA_EVENT_START);
        }
        return ret;
    }

    return SERIAL_MCU_OTA_OK;
}

int32_t serial_mcu_ota_start(const serial_mcu_ota_config_t *config)
{
    int32_t ret;

    if (!config || !config->read_chunk || config->total_size == 0) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    memset(&s_ota, 0, sizeof(s_ota));
    s_ota.state = SERIAL_MCU_OTA_STATE_IDLE;
    s_ota.total_size = config->total_size;
    s_ota.chunk_size = config->chunk_size ?
                          config->chunk_size :
                          CONFIG_SERIAL_MCU_OTA_CHUNK_SIZE;
    s_ota.check_type = config->check_type ?
                          (uint8_t)config->check_type :
                          SERIAL_MCU_OTA_CHECK_CRC32;
    s_ota.flags = config->flags;
    s_ota.timeout_ms = config->timeout_ms ?
                          config->timeout_ms :
                          CONFIG_SERIAL_MCU_OTA_TIMEOUT_MS;
    s_ota.retry_max = config->retry_max ?
                         config->retry_max :
                         CONFIG_SERIAL_MCU_OTA_RETRY_MAX;
    s_ota.read_chunk = config->read_chunk;
    s_ota.notify = config->notify;
    s_ota.user = config->user;
    memcpy(s_ota.check_value,
           config->check_value,
           SERIAL_MCU_OTA_CHECK_VALUE_LEN);

    if (s_ota.chunk_size == 0 ||
        s_ota.chunk_size > SERIAL_MCU_OTA_MAX_CHUNK_SIZE ||
        s_ota.timeout_ms == 0 ||
        get_check_value_len(s_ota.check_type) == 0) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    s_ota.state = SERIAL_MCU_OTA_STATE_RUNNING;
    s_ota.waiting_mcu_ready = 1;
    ret = send_enter();
    if (ret == SERIAL_MCU_OTA_OK) {
        notify(SERIAL_MCU_OTA_EVENT_ENTER);
    }

    if (ret != SERIAL_MCU_OTA_OK) {
        fail(ret);
    }

    return ret;
}

int32_t serial_mcu_ota_parse(uint8_t cmd, uint8_t *data, uint16_t len)
{
    mcu_ota_ack_t ack;
    mcu_ota_notify_t ota_notify;
    int32_t ret;

    if (len > 0 && !data) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    if (cmd == MCU_OTA_CMD_NOTIFY) {
        ret = notify_parse(data, len, &ota_notify);
        if (ret != SERIAL_MCU_OTA_OK) {
            return ret;
        }
        return handle_mcu_notify(&ota_notify);
    }

    if (s_ota.state != SERIAL_MCU_OTA_STATE_RUNNING) {
        return SERIAL_MCU_OTA_ERR_STATE;
    }

    if (cmd != MCU_OTA_CMD_ACK) {
        return SERIAL_MCU_OTA_ERR_ACK;
    }

    ret = ack_parse(data, len, &ack);
    if (ret != SERIAL_MCU_OTA_OK) {
        return ret;
    }

    if (!s_ota.waiting_ack || ack.acked_cmd != s_ota.last_cmd) {
        return SERIAL_MCU_OTA_ERR_ACK;
    }

    s_ota.last_ack_code = ack.code;

    if (ack.code == MCU_OTA_ACK_SEQUENCE_ERROR) {
        if (ack.next_offset > s_ota.total_size) {
            fail(SERIAL_MCU_OTA_ERR_ACK);
            return SERIAL_MCU_OTA_ERR_ACK;
        }
        s_ota.seq = ack.next_seq;
        s_ota.offset = ack.next_offset;
        ret = send_next();
        if (ret != SERIAL_MCU_OTA_OK) {
            fail(ret);
        }
        return ret;
    }

    if (ack.code != MCU_OTA_ACK_OK) {
        fail(SERIAL_MCU_OTA_ERR_ACK);
        return SERIAL_MCU_OTA_ERR_ACK;
    }

    s_ota.waiting_ack = 0;

    if (ack.acked_cmd == MCU_OTA_CMD_ENTER) {
        s_ota.waiting_ack = 0;
        s_ota.waiting_mcu_ready = 1;
        qm_utils_time_countdown_ms(&s_ota.ack_timer, s_ota.timeout_ms);
        return SERIAL_MCU_OTA_OK;
    }

    if (ack.acked_cmd == MCU_OTA_CMD_BEGIN) {
        s_ota.retry_count = 0;
        s_ota.seq = ack.next_seq;
        s_ota.offset = ack.next_offset;
        if (s_ota.offset > s_ota.total_size) {
            fail(SERIAL_MCU_OTA_ERR_ACK);
            return SERIAL_MCU_OTA_ERR_ACK;
        }
        ret = send_next();
        if (ret != SERIAL_MCU_OTA_OK) {
            fail(ret);
        }
        return ret;
    }

    if (ack.acked_cmd == MCU_OTA_CMD_DATA) {
        s_ota.retry_count = 0;
        s_ota.seq = ack.next_seq;
        s_ota.offset = ack.next_offset;
        if (s_ota.offset > s_ota.total_size) {
            fail(SERIAL_MCU_OTA_ERR_ACK);
            return SERIAL_MCU_OTA_ERR_ACK;
        }
        notify(SERIAL_MCU_OTA_EVENT_PROGRESS);
        ret = send_next();
        if (ret != SERIAL_MCU_OTA_OK) {
            fail(ret);
        }
        return ret;
    }

    if (ack.acked_cmd == MCU_OTA_CMD_END) {
        s_ota.retry_count = 0;
        s_ota.state = SERIAL_MCU_OTA_STATE_DONE;
        s_ota.offset = s_ota.total_size;
        s_ota.seq = ack.next_seq;
        notify(SERIAL_MCU_OTA_EVENT_FINISH);
        return SERIAL_MCU_OTA_OK;
    }

    if (ack.acked_cmd == MCU_OTA_CMD_ABORT) {
        s_ota.retry_count = 0;
        s_ota.state = SERIAL_MCU_OTA_STATE_ABORTED;
        notify(SERIAL_MCU_OTA_EVENT_ABORT);
        return SERIAL_MCU_OTA_OK;
    }

    return SERIAL_MCU_OTA_ERR_ACK;
}

int32_t serial_mcu_ota_process(void)
{
    int32_t ret;

    if (s_ota.state != SERIAL_MCU_OTA_STATE_RUNNING ||
        (!s_ota.waiting_ack && !s_ota.waiting_mcu_ready)) {
        return SERIAL_MCU_OTA_OK;
    }

    if (!qm_utils_time_is_expired(&s_ota.ack_timer)) {
        return SERIAL_MCU_OTA_OK;
    }

    if (s_ota.retry_count >= s_ota.retry_max) {
        fail(SERIAL_MCU_OTA_ERR_TIMEOUT);
        return SERIAL_MCU_OTA_ERR_TIMEOUT;
    }

    s_ota.retry_count++;
    ret = qumi_send_payload(s_ota.last_cmd,
                            s_ota.last_payload,
                            s_ota.last_payload_len);
    if (ret != SERIAL_MCU_OTA_OK) {
        fail(ret);
        return ret;
    }

    if (s_ota.waiting_mcu_ready) {
        s_ota.waiting_ack = 1;
    }

    qm_utils_time_countdown_ms(&s_ota.ack_timer, s_ota.timeout_ms);
    notify(SERIAL_MCU_OTA_EVENT_RETRY);

    return SERIAL_MCU_OTA_OK;
}

int32_t serial_mcu_ota_abort(int32_t reason)
{
    int32_t ret;

    if (s_ota.state != SERIAL_MCU_OTA_STATE_RUNNING &&
        s_ota.state != SERIAL_MCU_OTA_STATE_IDLE) {
        return SERIAL_MCU_OTA_ERR_STATE;
    }

    s_ota.error_code = reason;
    s_ota.state = SERIAL_MCU_OTA_STATE_RUNNING;
    ret = send_abort();
    if (ret != SERIAL_MCU_OTA_OK) {
        fail(ret);
    }

    return ret;
}

serial_mcu_ota_state_t serial_mcu_ota_state(void)
{
    return s_ota.state;
}

void serial_mcu_ota_get_info(serial_mcu_ota_info_t *info)
{
    build_info(info);
}
