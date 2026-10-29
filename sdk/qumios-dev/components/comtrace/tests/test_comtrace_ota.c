#include "comtrace_frame.h"
#include "comtrace_ota.h"
#include "qm_config.h"
#include "qm_ota.h"
#include "qm_string.h"
#include "qm_utils_sha256.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_SN "SN-001"
#define TEST_PRODUCT "sensor-a1"
#define TEST_VERSION "1.0.0"
#define TEST_TARGET_VERSION "1.1.0"
#define TEST_SID 7U
#define TEST_MAX_FRAME 256U
#define TEST_CHUNK_SIZE 4U
#define TEST_IMAGE_SIZE 8U

typedef struct {
    uint8_t image[TEST_IMAGE_SIZE];
    uint32_t image_size;
    uint32_t written;
    int start_count;
    int write_count;
    int read_count;
    int abort_count;
    int end_count;
    int abort_result;
    int authorize_result;
    int authorize_count;
    int reboot_result;
    int reboot_count;
    int reboot_send_count;
    int fail_verifying_once;
    int send_count;
    int status_count;
    comtrace_ota_status_t last_status;
    char last_wire_state[24];
} test_context_t;

typedef struct {
    const char *target_version;
    const char *product_id;
    const char *sha256;
    uint32_t image_size;
    uint16_t chunk_size;
} begin_body_t;

typedef struct {
    uint32_t chunk_index;
    uint32_t offset;
    const uint8_t *data;
    uint16_t length;
} chunk_body_t;

static test_context_t *g_context;

void *qm_malloc(unsigned int size)
{
    return malloc(size);
}

void *qm_calloc(unsigned int count, unsigned int size)
{
    return calloc(count, size);
}

void qm_free(void *memory)
{
    free(memory);
}

uint32_t qm_strlen(const char *text)
{
    return text ? (uint32_t)strlen(text) : 0U;
}

int32_t qm_strcmp(const char *left, const char *right)
{
    return strcmp(left, right);
}

int32_t qm_memcmp(const void *left, const void *right, uint32_t length)
{
    return memcmp(left, right, length);
}

void *qm_memcpy(void *destination, const void *source, uint32_t length)
{
    return memcpy(destination, source, length);
}

void *qm_memmove(void *destination, const void *source, uint32_t length)
{
    return memmove(destination, source, length);
}

void *qm_memset(void *memory, int32_t value, uint32_t length)
{
    return memset(memory, value, length);
}

int32_t qm_uint2str(uint32_t input, char *output, uint8_t *output_len)
{
    int length = snprintf(output, 11U, "%u", input);

    if (length < 0 || length > 10)
        return -QM_EINVAL;
    if (output_len)
        *output_len = (uint8_t)length;
    return QM_EOK;
}

int32_t qm_str2hex(char *input, uint32_t input_len, uint8_t *output)
{
    uint32_t i;

    if (!input || !output || (input_len & 1U))
        return -QM_EINVAL;
    for (i = 0U; i < input_len / 2U; ++i) {
        char high = input[i * 2U];
        char low = input[i * 2U + 1U];
        uint8_t high_value = (uint8_t)(high <= '9' ? high - '0' : (high | 0x20) - 'a' + 10);
        uint8_t low_value = (uint8_t)(low <= '9' ? low - '0' : (low | 0x20) - 'a' + 10);
        output[i] = (uint8_t)((high_value << 4) | low_value);
    }
    return QM_EOK;
}

void qm_reboot(void)
{
    assert(g_context != NULL);
    ++g_context->reboot_count;
    g_context->reboot_send_count = g_context->send_count;
}

int qm_ota_start(void *arg)
{
    unsigned long image_size;

    assert(g_context != NULL);
    assert(arg != NULL);
    image_size = strtoul((const char *)arg, NULL, 10);
    assert(image_size <= sizeof(g_context->image));
    g_context->image_size = (uint32_t)image_size;
    g_context->written = 0U;
    memset(g_context->image, 0, sizeof(g_context->image));
    ++g_context->start_count;
    return QM_EOK;
}

int qm_ota_write(uint32_t *offset, uint8_t *input, uint32_t length)
{
    assert(g_context != NULL);
    assert(offset != NULL && input != NULL);
    assert(*offset + length <= g_context->image_size);
    memcpy(g_context->image + *offset, input, length);
    *offset += length;
    g_context->written += length;
    ++g_context->write_count;
    return QM_EOK;
}

int qm_ota_read(uint32_t *offset, uint8_t *output, uint32_t length)
{
    assert(g_context != NULL);
    assert(offset != NULL && output != NULL);
    assert(*offset + length <= g_context->written);
    memcpy(output, g_context->image + *offset, length);
    *offset += length;
    ++g_context->read_count;
    return QM_EOK;
}

int qm_ota_abort(void *arg)
{
    (void)arg;
    assert(g_context != NULL);
    ++g_context->abort_count;
    if (g_context->abort_result == QM_EOK) {
        g_context->written = 0U;
        g_context->image_size = 0U;
    }
    return g_context->abort_result;
}

int qm_ota_end(void *arg)
{
    (void)arg;
    assert(g_context != NULL);
    assert(g_context->written == g_context->image_size);
    ++g_context->end_count;
    return QM_EOK;
}

static int parse_wire_status(const uint8_t *frame, uint16_t frame_len, char state[24])
{
    CborParser parser;
    CborValue root;
    char type[24];
    size_t length;
    uint16_t payload_len;

    if (frame_len <= COMTRACE_FRAME_HEADER_SIZE)
        return -QM_EINVAL;
    payload_len = (uint16_t)(((uint16_t)frame[0] << 8) | frame[1]);
    if (payload_len + COMTRACE_FRAME_HEADER_SIZE != frame_len ||
        comtrace_cbor_root(frame + COMTRACE_FRAME_HEADER_SIZE, payload_len, &parser, &root) !=
            QM_EOK ||
        comtrace_cbor_text(&root, "type", type, sizeof(type), &length) != QM_EOK ||
        strcmp(type, "update_status") != 0)
        return -QM_EINVAL;
    return comtrace_cbor_text(&root, "state", state, 24U, &length);
}

static int send_frame(const uint8_t *frame, uint16_t frame_len, void *user)
{
    test_context_t *context = (test_context_t *)user;
    int status_result;

    ++context->send_count;
    status_result = parse_wire_status(frame, frame_len, context->last_wire_state);
    if (status_result == QM_EOK) {
        ++context->status_count;
        if (context->fail_verifying_once && strcmp(context->last_wire_state, "verifying") == 0) {
            context->fail_verifying_once = 0;
            return -QM_EIO;
        }
    }
    return (int)frame_len;
}

static void on_status(const comtrace_ota_status_t *status, void *user)
{
    test_context_t *context = (test_context_t *)user;
    context->last_status = *status;
}

static int authorize_update(const comtrace_ota_request_t *request, void *user)
{
    test_context_t *context = (test_context_t *)user;

    assert(request != NULL);
    assert(strcmp(request->target_version, TEST_TARGET_VERSION) == 0);
    assert(strcmp(request->product_id, TEST_PRODUCT) == 0);
    assert(request->sha256 != NULL);
    assert(request->image_size == TEST_IMAGE_SIZE);
    assert(request->chunk_size == TEST_CHUNK_SIZE);
    ++context->authorize_count;
    return context->authorize_result;
}

static int schedule_reboot(void *user)
{
    test_context_t *context = (test_context_t *)user;
    ++context->reboot_count;
    context->reboot_send_count = context->send_count;
    return context->reboot_result;
}

static comtrace_ota_t *new_ota(test_context_t *context)
{
    comtrace_ota_config_t config;
    comtrace_ota_t *ota = NULL;

    memset(context, 0, sizeof(*context));
    context->authorize_result = QM_EOK;
    memset(&config, 0, sizeof(config));
    config.sn = TEST_SN;
    config.sid = TEST_SID;
    config.current_version = TEST_VERSION;
    config.product_id = TEST_PRODUCT;
    config.max_frame = TEST_MAX_FRAME;
    config.max_chunk = TEST_CHUNK_SIZE;
    config.max_image_size = TEST_IMAGE_SIZE;
    config.send = send_frame;
    config.authorize = authorize_update;
    config.on_status = on_status;
    config.reboot = schedule_reboot;
    config.user = context;
    g_context = context;
    assert(comtrace_ota_init(&ota, &config) == QM_EOK);
    assert(context->last_status.state == COMTRACE_OTA_UNREGISTERED);
    return ota;
}

static void digest_hex(const uint8_t *data, uint32_t length, char output[65])
{
    static const char digits[] = "0123456789abcdef";
    uint8_t digest[COMTRACE_OTA_SHA256_SIZE];
    uint32_t i;

    qm_utils_sha256(data, length, digest);
    for (i = 0U; i < sizeof(digest); ++i) {
        output[i * 2U] = digits[digest[i] >> 4];
        output[i * 2U + 1U] = digits[digest[i] & 0x0fU];
    }
    output[64] = '\0';
}

static int body_ack(CborEncoder *map, void *user)
{
    (void)user;
    return comtrace_cbor_put_uint(map, "max_frame", TEST_MAX_FRAME);
}

static int body_begin(CborEncoder *map, void *user)
{
    begin_body_t *body = (begin_body_t *)user;
    int result = comtrace_cbor_put_text(map, "target_version", body->target_version);

    if (result == QM_EOK)
        result = comtrace_cbor_put_text(map, "product_id", body->product_id);
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(map, "image_size", body->image_size);
    if (result == QM_EOK)
        result = comtrace_cbor_put_text(map, "sha256", body->sha256);
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(map, "chunk_size", body->chunk_size);
    return result;
}

static int body_chunk(CborEncoder *map, void *user)
{
    chunk_body_t *body = (chunk_body_t *)user;
    int result = comtrace_cbor_put_uint(map, "chunk_index", body->chunk_index);

    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(map, "offset", body->offset);
    if (result == QM_EOK && cbor_encode_text_stringz(map, "data") != CborNoError)
        result = -QM_EFULL;
    if (result == QM_EOK &&
        cbor_encode_byte_string(map, body->data, body->length) != CborNoError)
        result = -QM_EFULL;
    return result;
}

static int body_end(CborEncoder *map, void *user)
{
    return comtrace_cbor_put_text(map, "sha256", (const char *)user);
}

static int feed_message(comtrace_ota_t *ota, const char *type, size_t map_count,
                        comtrace_encode_body_cb_t body, void *user)
{
    uint8_t *frame;
    uint16_t frame_len;
    int result = comtrace_frame_encode(TEST_SN, TEST_SID, TEST_MAX_FRAME, type, map_count, body,
                                       user, &frame, &frame_len);

    assert(result == QM_EOK);
    result = comtrace_ota_feed(ota, frame, frame_len);
    qm_free(frame);
    return result;
}

static void register_ota(comtrace_ota_t *ota)
{
    assert(comtrace_ota_register(ota) == QM_EOK);
    assert(feed_message(ota, "update_register_ack", 5U, body_ack, NULL) == QM_EOK);
}

static int begin_update(comtrace_ota_t *ota, const char *sha256)
{
    begin_body_t body = {
        TEST_TARGET_VERSION, TEST_PRODUCT, sha256, TEST_IMAGE_SIZE, TEST_CHUNK_SIZE,
    };
    return feed_message(ota, "update_begin", 9U, body_begin, &body);
}

static int send_chunk(comtrace_ota_t *ota, uint32_t index, uint32_t offset,
                      const uint8_t *data, uint16_t length)
{
    chunk_body_t body = {index, offset, data, length};
    return feed_message(ota, "update_chunk", 7U, body_chunk, &body);
}

static void test_registration_authorization_and_chunk_size(void)
{
    static const uint8_t image[TEST_IMAGE_SIZE] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    test_context_t context;
    comtrace_ota_t *ota = new_ota(&context);
    char sha256[65];

    digest_hex(image, sizeof(image), sha256);
    assert(begin_update(ota, sha256) == -QM_EINVAL);
    assert(context.start_count == 0);
    register_ota(ota);
    context.authorize_result = -QM_EINVAL;
    assert(begin_update(ota, sha256) == -QM_EINVAL);
    assert(context.start_count == 0 && context.authorize_count == 1);
    context.authorize_result = QM_EOK;
    assert(begin_update(ota, sha256) == QM_EOK);
    assert(context.start_count == 1 && context.authorize_count == 2);
    assert(send_chunk(ota, 0U, 0U, image, 3U) == -QM_EINVAL);
    assert(context.write_count == 0);
#if CONFIG_QM_COMTRACE_OTA_ABORT_SUPPORT
    assert(comtrace_ota_cancel(ota) == QM_EOK);
    assert(context.last_status.state == COMTRACE_OTA_CANCELLED);
    assert(comtrace_ota_deinit(ota) == QM_EOK);
#else
    assert(comtrace_ota_cancel(ota) == -QM_EIO);
    assert(context.last_status.state == COMTRACE_OTA_FAILED);
    assert(comtrace_ota_deinit(ota) == -QM_ENOSYS);
#endif
}

static void test_complete_update_and_committed_terminal(void)
{
    static const uint8_t image[TEST_IMAGE_SIZE] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    test_context_t context;
    comtrace_ota_t *ota = new_ota(&context);
    char sha256[65];
    int reboot_count;

    digest_hex(image, sizeof(image), sha256);
    register_ota(ota);
    assert(begin_update(ota, sha256) == QM_EOK);
    assert(send_chunk(ota, 0U, 0U, image, TEST_CHUNK_SIZE) == QM_EOK);
    assert(send_chunk(ota, 0U, 0U, image, TEST_CHUNK_SIZE) == QM_EOK);
    assert(context.write_count == 1);
    assert(send_chunk(ota, 1U, TEST_CHUNK_SIZE, image + TEST_CHUNK_SIZE, TEST_CHUNK_SIZE) ==
           QM_EOK);
    context.fail_verifying_once = 1;
    assert(feed_message(ota, "update_end", 5U, body_end, sha256) == -QM_EIO);
    assert(context.end_count == 0);
    assert(context.last_status.state == COMTRACE_OTA_TRANSFERRING);
    context.reboot_result = -QM_EIO;
    assert(feed_message(ota, "update_end", 5U, body_end, sha256) == -QM_EIO);
    assert(context.end_count == 1);
#if CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY
    assert(context.read_count == 0);
#else
    assert(context.read_count == 2);
#endif
    assert(context.last_status.state == COMTRACE_OTA_REBOOTING);
    assert(context.reboot_count == 1);
    context.reboot_result = QM_EOK;
    assert(feed_message(ota, "update_end", 5U, body_end, sha256) == QM_EOK);
    assert(context.end_count == 1);
#if CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY
    assert(context.read_count == 0);
#else
    assert(context.read_count == 2);
#endif
    assert(context.last_status.state == COMTRACE_OTA_REBOOTING);
    assert(strcmp(context.last_wire_state, "rebooting") == 0);
    assert(context.reboot_count == 2 && context.reboot_send_count == context.send_count);
    reboot_count = context.reboot_count;
    assert(comtrace_ota_cancel(ota) == QM_EOK);
    assert(context.last_status.state == COMTRACE_OTA_REBOOTING);
    assert(context.reboot_count == reboot_count);
    assert(comtrace_ota_deinit(ota) == QM_EOK);
}

static void test_digest_mismatch_is_terminal(void)
{
    static const uint8_t expected[TEST_IMAGE_SIZE] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    static const uint8_t corrupted[TEST_IMAGE_SIZE] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 8U};
    test_context_t context;
    comtrace_ota_t *ota = new_ota(&context);
    char sha256[65];

    digest_hex(expected, sizeof(expected), sha256);
    register_ota(ota);
    assert(begin_update(ota, sha256) == QM_EOK);
    assert(send_chunk(ota, 0U, 0U, corrupted, TEST_CHUNK_SIZE) == QM_EOK);
    assert(send_chunk(ota, 1U, TEST_CHUNK_SIZE, corrupted + TEST_CHUNK_SIZE,
                      TEST_CHUNK_SIZE) == QM_EOK);
#if CONFIG_QM_COMTRACE_OTA_ABORT_SUPPORT
    assert(feed_message(ota, "update_end", 5U, body_end, sha256) == -QM_EIO);
    assert(context.abort_count == 1);
    assert(comtrace_ota_deinit(ota) == QM_EOK);
#else
    assert(feed_message(ota, "update_end", 5U, body_end, sha256) == -QM_ENOSYS);
    assert(context.abort_count == 0);
    assert(comtrace_ota_deinit(ota) == -QM_ENOSYS);
#endif
    assert(context.end_count == 0);
    assert(context.last_status.state == COMTRACE_OTA_FAILED);
    assert(context.last_status.error_code == COMTRACE_OTA_ERROR_VERIFY_FAILED);
#if CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY
    assert(context.read_count == 0);
#else
    assert(context.read_count == 2);
#endif
}

static void test_disconnect_and_abort_failure(void)
{
    static const uint8_t image[TEST_IMAGE_SIZE] = {0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U};
    test_context_t context;
    comtrace_ota_t *ota = new_ota(&context);
    char sha256[65];
    uint8_t partial = 0U;

    digest_hex(image, sizeof(image), sha256);
    register_ota(ota);
    assert(begin_update(ota, sha256) == QM_EOK);
    assert(comtrace_ota_feed(ota, &partial, 1U) == QM_EOK);
#if CONFIG_QM_COMTRACE_OTA_ABORT_SUPPORT
    assert(comtrace_ota_disconnect(ota) == QM_EOK);
    assert(context.abort_count == 1);
    assert(context.last_status.state == COMTRACE_OTA_FAILED);
    assert(context.last_status.error_code == COMTRACE_OTA_ERROR_CONNECTION_LOST);
    register_ota(ota);
    assert(context.last_status.state == COMTRACE_OTA_IDLE);
    assert(begin_update(ota, sha256) == QM_EOK);
    context.abort_result = -QM_EIO;
    assert(comtrace_ota_disconnect(ota) == -QM_EIO);
    assert(context.last_status.state == COMTRACE_OTA_FAILED);
    assert(comtrace_ota_register(ota) == -QM_EBUSY);
    context.abort_result = QM_EOK;
    assert(comtrace_ota_cancel(ota) == QM_EOK);
    assert(comtrace_ota_deinit(ota) == QM_EOK);
#else
    assert(comtrace_ota_disconnect(ota) == -QM_ENOSYS);
    assert(context.abort_count == 0);
    assert(context.last_status.state == COMTRACE_OTA_FAILED);
    assert(context.last_status.error_code == COMTRACE_OTA_ERROR_CONNECTION_LOST);
    assert(comtrace_ota_register(ota) == -QM_EBUSY);
    assert(comtrace_ota_deinit(ota) == -QM_ENOSYS);
#endif
}

static void test_trailing_data_and_boot_result(void)
{
    test_context_t context;
    comtrace_ota_t *ota = new_ota(&context);
    uint8_t *frame;
    uint8_t *invalid;
    uint16_t frame_len;
    uint16_t payload_len;

    assert(comtrace_ota_register(ota) == QM_EOK);
    assert(comtrace_frame_encode(TEST_SN, TEST_SID, TEST_MAX_FRAME, "update_register_ack", 5U,
                                 body_ack, NULL, &frame, &frame_len) == QM_EOK);
    invalid = (uint8_t *)qm_malloc(frame_len + 1U);
    assert(invalid != NULL);
    qm_memcpy(invalid, frame, frame_len);
    payload_len = (uint16_t)(((uint16_t)invalid[0] << 8) | invalid[1]);
    ++payload_len;
    invalid[0] = (uint8_t)(payload_len >> 8);
    invalid[1] = (uint8_t)payload_len;
    invalid[frame_len] = 0U;
    assert(comtrace_ota_feed(ota, invalid, (uint16_t)(frame_len + 1U)) == -QM_EINVAL);
    qm_free(invalid);
    qm_free(frame);
    assert(feed_message(ota, "update_register_ack", 5U, body_ack, NULL) == QM_EOK);
    assert(comtrace_ota_report_boot_result(ota, COMTRACE_OTA_SUCCESS,
                                           COMTRACE_OTA_ERROR_NONE) == QM_EOK);
    assert(strcmp(context.last_wire_state, "success") == 0);
    assert(comtrace_ota_report_boot_result(ota, COMTRACE_OTA_ROLLED_BACK,
                                           COMTRACE_OTA_ERROR_ROLLBACK) == QM_EOK);
    assert(strcmp(context.last_wire_state, "rolled_back") == 0);
    assert(comtrace_ota_deinit(ota) == QM_EOK);
}

int main(void)
{
    test_registration_authorization_and_chunk_size();
    test_complete_update_and_committed_terminal();
    test_digest_mismatch_is_terminal();
    test_disconnect_and_abort_failure();
    test_trailing_data_and_boot_result();
    puts("comtrace OTA tests passed");
    return 0;
}
