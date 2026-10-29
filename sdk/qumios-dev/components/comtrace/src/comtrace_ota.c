/**
 * @file comtrace_ota.c
 * @brief CEUP/1 protocol and QUMI OTA HAL state machine.
 */
#include "comtrace_ota.h"
#include "comtrace_frame.h"
#include "comtrace_internal.h"
#include "qm_config.h"
#include "qm_kernel.h"
#include "qm_ota.h"
#include "qm_string.h"
#include "qm_utils_sha256.h"

#ifndef CONFIG_QM_COMTRACE_MAX_FRAME
#define CONFIG_QM_COMTRACE_MAX_FRAME COMTRACE_MAX_FRAME
#endif

#ifndef CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY
#define CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY 0
#endif

#ifndef CONFIG_QM_COMTRACE_OTA_ABORT_SUPPORT
#define CONFIG_QM_COMTRACE_OTA_ABORT_SUPPORT 1
#endif

#define CEUP_MAX_VERSION_LEN 64U
#define CEUP_MAX_PRODUCT_LEN 64U
#define CEUP_SHA256_HEX_LEN 64U

struct comtrace_ota {
    char sn[COMTRACE_MAX_SN_LEN + 1U];
    char current_version[CEUP_MAX_VERSION_LEN + 1U];
    char product_id[CEUP_MAX_PRODUCT_LEN + 1U];
    char target_version[CEUP_MAX_VERSION_LEN + 1U];
    uint32_t sid;
    uint16_t max_frame;
    uint16_t max_chunk;
    uint32_t max_image_size;
    comtrace_send_cb_t send;
    comtrace_ota_authorize_cb_t authorize;
    comtrace_ota_status_cb_t on_status;
    comtrace_ota_reboot_cb_t reboot;
    void *user;
    comtrace_ota_status_t status;
    comtrace_stream_t stream;
    uint8_t *chunk;
    uint8_t expected_sha256[COMTRACE_OTA_SHA256_SIZE];
    qm_sha256_context sha256;
    uint32_t ota_offset;
    uint16_t chunk_size;
    uint32_t next_chunk_index;
    uint32_t last_chunk_index;
    uint32_t last_offset;
    uint16_t last_length;
    uint8_t ota_started;
    uint8_t sha256_started;
    uint8_t error_reported;
    uint8_t registered;
    uint8_t reboot_requested;
};

static const char *const status_names[] = {
    "unregistered", "registering", "idle",      "prepared",  "transferring",
    "verifying",    "rebooting",   "success",   "failed",    "rolled_back",
    "cancelled",
};

static int valid_hex(const char *text, size_t length)
{
    size_t i;
    char c;
    if (length != CEUP_SHA256_HEX_LEN)
        return 0;
    for (i = 0U; i < length; ++i) {
        c = text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')))
            return 0;
    }
    return 1;
}

static void notify_status(comtrace_ota_t *ota)
{
    if (ota->on_status)
        ota->on_status(&ota->status, ota->user);
}

static int send_error(comtrace_ota_t *ota, uint32_t code)
{
    uint8_t *frame;
    uint16_t length;
    int result = comtrace_frame_encode_error(ota->max_frame, code, &frame, &length);
    return result == QM_EOK ? comtrace_frame_send(ota->send, ota->user, frame, length) : result;
}

static int reject(comtrace_ota_t *ota, uint32_t code, int result)
{
    ota->error_reported = 1U;
    (void)send_error(ota, code);
    return result;
}

static int body_register(CborEncoder *map, void *user)
{
    comtrace_ota_t *ota = (comtrace_ota_t *)user;
    int result = comtrace_cbor_put_text(map, "current_version", ota->current_version);
    if (result == QM_EOK)
        result = comtrace_cbor_put_text(map, "product_id", ota->product_id);
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(map, "max_chunk", ota->max_chunk);
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(map, "max_image_size", ota->max_image_size);
    return result;
}

static int body_status(CborEncoder *map, void *user)
{
    comtrace_ota_t *ota = (comtrace_ota_t *)user;
    int result = comtrace_cbor_put_text(map, "state", status_names[ota->status.state]);
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(map, "received_bytes", ota->status.received_bytes);
    if (result == QM_EOK)
        result = comtrace_cbor_put_uint(map, "error_code", ota->status.error_code);
    return result;
}

static int send_status(comtrace_ota_t *ota)
{
    uint8_t *frame;
    uint16_t length;
    int result;
    notify_status(ota);
    result = comtrace_frame_encode(ota->sn, ota->sid, ota->max_frame, "update_status", 7U,
                                   body_status, ota, &frame, &length);
    return result == QM_EOK ? comtrace_frame_send(ota->send, ota->user, frame, length) : result;
}

static void clear_digest(comtrace_ota_t *ota)
{
    if (!ota->sha256_started)
        return;
    qm_utils_sha256_free(&ota->sha256);
    ota->sha256_started = 0U;
}

static int abort_ota(comtrace_ota_t *ota)
{
    int result = QM_EOK;

#if CONFIG_QM_COMTRACE_OTA_ABORT_SUPPORT
    if (ota->ota_started)
        result = qm_ota_abort(NULL);
#else
    if (ota->ota_started)
        result = -QM_ENOSYS;
#endif
    if (result == QM_EOK) {
        ota->ota_started = 0U;
        ota->ota_offset = 0U;
    }
    clear_digest(ota);
    return result;
}

static int fail(comtrace_ota_t *ota, uint32_t code, int abort_image)
{
    int abort_result = QM_EOK;

    if (abort_image)
        abort_result = abort_ota(ota);
    ota->status.state = COMTRACE_OTA_FAILED;
    ota->status.error_code = code;
    (void)send_status(ota);
    return abort_result != QM_EOK ? abort_result : -QM_EIO;
}

static int send_rebooting(comtrace_ota_t *ota)
{
    int result = send_status(ota);

    if (result != QM_EOK || ota->reboot_requested)
        return result;
    ota->reboot_requested = 1U;
    if (ota->reboot) {
        result = ota->reboot(ota->user);
        if (result != QM_EOK)
            ota->reboot_requested = 0U;
        return result;
    }
    qm_reboot();
    return QM_EOK;
}

static int parse_digest(CborValue *root, uint8_t digest[COMTRACE_OTA_SHA256_SIZE])
{
    char hex[CEUP_SHA256_HEX_LEN + 1U];
    size_t length;
    if (comtrace_cbor_text(root, "sha256", hex, sizeof(hex), &length) != QM_EOK ||
        !valid_hex(hex, length))
        return -QM_EINVAL;
    return qm_str2hex(hex, CEUP_SHA256_HEX_LEN, digest) == QM_EOK ? QM_EOK : -QM_EINVAL;
}

static int handle_begin(comtrace_ota_t *ota, CborValue *root)
{
    comtrace_ota_request_t request;
    char target_version[CEUP_MAX_VERSION_LEN + 1U];
    char product_id[CEUP_MAX_PRODUCT_LEN + 1U];
    char image_size_arg[11U];
    uint8_t digest[COMTRACE_OTA_SHA256_SIZE];
    uint8_t image_size_arg_len = sizeof(image_size_arg) - 1U;
    size_t target_length;
    size_t product_length;
    uint64_t image_size;
    uint64_t chunk_size;

    if (comtrace_cbor_text(root, "target_version", target_version, sizeof(target_version),
                           &target_length) != QM_EOK ||
        target_length == 0U || !comtrace_text_is_printable(target_version, target_length) ||
        comtrace_cbor_text(root, "product_id", product_id, sizeof(product_id), &product_length) !=
            QM_EOK ||
        product_length == 0U || !comtrace_text_is_printable(product_id, product_length) ||
        parse_digest(root, digest) != QM_EOK ||
        comtrace_cbor_uint(root, "image_size", &image_size) != QM_EOK || image_size == 0U ||
        image_size > ota->max_image_size ||
        comtrace_cbor_uint(root, "chunk_size", &chunk_size) != QM_EOK || chunk_size == 0U ||
        chunk_size > ota->max_chunk)
        return reject(ota, COMTRACE_OTA_ERROR_IMAGE_INVALID, -QM_EINVAL);
    if (!ota->registered)
        return reject(ota, COMTRACE_OTA_ERROR_INVALID_FRAME, -QM_EINVAL);
    if (qm_strcmp(product_id, ota->product_id) != 0)
        return reject(ota, COMTRACE_OTA_ERROR_PRODUCT_MISMATCH, -QM_EINVAL);
    if (ota->status.state == COMTRACE_OTA_PREPARED &&
        ota->status.image_size == (uint32_t)image_size && ota->chunk_size == (uint16_t)chunk_size &&
        qm_memcmp(ota->expected_sha256, digest, COMTRACE_OTA_SHA256_SIZE) == 0 &&
        qm_strcmp(ota->target_version, target_version) == 0)
        return send_status(ota);
    if (ota->ota_started || ota->status.state == COMTRACE_OTA_TRANSFERRING ||
        ota->status.state == COMTRACE_OTA_VERIFYING || ota->status.state == COMTRACE_OTA_REBOOTING)
        return reject(ota, COMTRACE_OTA_ERROR_TASK_BUSY, -QM_EBUSY);
    request.target_version = target_version;
    request.product_id = product_id;
    request.sha256 = digest;
    request.image_size = (uint32_t)image_size;
    request.chunk_size = (uint16_t)chunk_size;
    if (ota->authorize(&request, ota->user) != QM_EOK)
        return reject(ota, COMTRACE_OTA_ERROR_VERSION_REJECTED, -QM_EINVAL);
    if (qm_uint2str((uint32_t)image_size, image_size_arg, &image_size_arg_len) != QM_EOK)
        return fail(ota, COMTRACE_OTA_ERROR_IMAGE_INVALID, 0);
    image_size_arg[image_size_arg_len] = '\0';
    if (qm_ota_start(image_size_arg) != QM_EOK)
        return fail(ota, COMTRACE_OTA_ERROR_IMAGE_INVALID, 0);
    ota->ota_started = 1U;
    ota->ota_offset = 0U;
#if CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY
    qm_utils_sha256_init(&ota->sha256);
    qm_utils_sha256_starts(&ota->sha256);
    ota->sha256_started = 1U;
#endif
    qm_memcpy(ota->expected_sha256, digest, sizeof(digest));
    qm_memcpy(ota->target_version, target_version, (uint32_t)(target_length + 1U));
    ota->chunk_size = (uint16_t)chunk_size;
    ota->next_chunk_index = 0U;
    ota->last_chunk_index = UINT32_MAX;
    ota->last_offset = 0U;
    ota->last_length = 0U;
    ota->status.state = COMTRACE_OTA_PREPARED;
    ota->status.image_size = (uint32_t)image_size;
    ota->status.received_bytes = 0U;
    ota->status.error_code = COMTRACE_OTA_ERROR_NONE;
    return send_status(ota);
}

static int handle_chunk(comtrace_ota_t *ota, CborValue *root)
{
    uint64_t chunk_index;
    uint64_t offset;
    uint32_t expected_length;
    size_t length;
    if (comtrace_cbor_uint(root, "chunk_index", &chunk_index) != QM_EOK ||
        chunk_index > UINT32_MAX || comtrace_cbor_uint(root, "offset", &offset) != QM_EOK ||
        offset > UINT32_MAX ||
        comtrace_cbor_bytes(root, "data", ota->chunk, ota->max_chunk, &length) != QM_EOK ||
        length == 0U || length > ota->chunk_size)
        return reject(ota, COMTRACE_OTA_ERROR_OFFSET_INVALID, -QM_EINVAL);
    if ((ota->status.state == COMTRACE_OTA_TRANSFERRING ||
         ota->status.state == COMTRACE_OTA_PREPARED) &&
        ota->last_chunk_index != UINT32_MAX && (uint32_t)chunk_index == ota->last_chunk_index &&
        (uint32_t)offset == ota->last_offset && length == ota->last_length)
        return send_status(ota);
    if ((ota->status.state != COMTRACE_OTA_PREPARED &&
         ota->status.state != COMTRACE_OTA_TRANSFERRING) ||
        (uint32_t)chunk_index != ota->next_chunk_index ||
        (uint32_t)offset != ota->status.received_bytes || offset > ota->status.image_size ||
        length > ota->status.image_size - (uint32_t)offset) {
        return reject(ota, COMTRACE_OTA_ERROR_OFFSET_INVALID, -QM_EINVAL);
    }
    expected_length = ota->status.image_size - (uint32_t)offset;
    if (expected_length > ota->chunk_size)
        expected_length = ota->chunk_size;
    if (length != expected_length)
        return reject(ota, COMTRACE_OTA_ERROR_OFFSET_INVALID, -QM_EINVAL);
    if (qm_ota_write(&ota->ota_offset, ota->chunk, (uint32_t)length) != QM_EOK ||
        ota->ota_offset != (uint32_t)offset + (uint32_t)length)
        return fail(ota, COMTRACE_OTA_ERROR_WRITE_FAILED, 1);
#if CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY
    qm_utils_sha256_update(&ota->sha256, ota->chunk, (uint32_t)length);
#endif
    ota->last_chunk_index = (uint32_t)chunk_index;
    ota->last_offset = (uint32_t)offset;
    ota->last_length = (uint16_t)length;
    ++ota->next_chunk_index;
    ota->status.received_bytes += (uint32_t)length;
    ota->status.state = COMTRACE_OTA_TRANSFERRING;
    ota->status.error_code = COMTRACE_OTA_ERROR_NONE;
    return send_status(ota);
}

static int handle_end(comtrace_ota_t *ota, CborValue *root)
{
    uint8_t digest[COMTRACE_OTA_SHA256_SIZE];
    uint8_t actual[COMTRACE_OTA_SHA256_SIZE];
#if !CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY
    uint32_t read_offset = 0U;
    uint32_t remaining;
#endif
    int result;
    if (parse_digest(root, digest) != QM_EOK)
        return reject(ota, COMTRACE_OTA_ERROR_IMAGE_INVALID, -QM_EINVAL);
    if (ota->status.state == COMTRACE_OTA_REBOOTING &&
        qm_memcmp(digest, ota->expected_sha256, sizeof(digest)) == 0)
        return send_rebooting(ota);
    if (ota->status.state != COMTRACE_OTA_TRANSFERRING ||
        ota->status.received_bytes != ota->status.image_size ||
        qm_memcmp(digest, ota->expected_sha256, sizeof(digest)) != 0)
        return fail(ota, COMTRACE_OTA_ERROR_IMAGE_INVALID, 1);
    ota->status.state = COMTRACE_OTA_VERIFYING;
    ota->status.error_code = COMTRACE_OTA_ERROR_NONE;
    result = send_status(ota);
    if (result != QM_EOK) {
        ota->status.state = COMTRACE_OTA_TRANSFERRING;
        notify_status(ota);
        return result;
    }
#if CONFIG_QM_COMTRACE_OTA_STREAM_VERIFY
    qm_utils_sha256_finish(&ota->sha256, actual);
    clear_digest(ota);
#else
    qm_utils_sha256_init(&ota->sha256);
    qm_utils_sha256_starts(&ota->sha256);
    ota->sha256_started = 1U;
    remaining = ota->status.image_size;
    while (remaining) {
        uint32_t before = read_offset;
        uint16_t read_len = remaining > ota->max_chunk ? ota->max_chunk : (uint16_t)remaining;
        if (qm_ota_read(&read_offset, ota->chunk, read_len) != QM_EOK ||
            read_offset != before + read_len)
            return fail(ota, COMTRACE_OTA_ERROR_VERIFY_FAILED, 1);
        qm_utils_sha256_update(&ota->sha256, ota->chunk, read_len);
        remaining -= read_len;
    }
    qm_utils_sha256_finish(&ota->sha256, actual);
    clear_digest(ota);
#endif
    if (qm_memcmp(actual, ota->expected_sha256, sizeof(actual)) != 0)
        return fail(ota, COMTRACE_OTA_ERROR_VERIFY_FAILED, 1);
    if (qm_ota_end(NULL) != QM_EOK)
        return fail(ota, COMTRACE_OTA_ERROR_VERIFY_FAILED, 1);
    ota->ota_started = 0U;
    ota->status.state = COMTRACE_OTA_REBOOTING;
    return send_rebooting(ota);
}

static int cancel_transfer(comtrace_ota_t *ota, int report)
{
    if (ota->status.state == COMTRACE_OTA_CANCELLED && !ota->ota_started)
        return report ? send_status(ota) : QM_EOK;
    if (ota->status.state == COMTRACE_OTA_REBOOTING)
        return report ? send_rebooting(ota) : -QM_EBUSY;
    if (!ota->ota_started)
        return report ? reject(ota, COMTRACE_OTA_ERROR_TASK_CANCELLED, -QM_ENOENT) : -QM_ENOENT;
    if (abort_ota(ota) != QM_EOK)
        return fail(ota, COMTRACE_OTA_ERROR_TASK_CANCELLED, 0);
    ota->status.state = COMTRACE_OTA_CANCELLED;
    ota->status.error_code = COMTRACE_OTA_ERROR_TASK_CANCELLED;
    return report ? send_status(ota) : (notify_status(ota), QM_EOK);
}

static int handle_error(comtrace_ota_t *ota, CborValue *root)
{
    uint64_t code;
    int result;
    if (comtrace_cbor_uint(root, "code", &code) != QM_EOK || code == 0U || code > UINT32_MAX)
        return -QM_EINVAL;
    if (ota->status.state == COMTRACE_OTA_REBOOTING)
        return QM_EOK;
    result = abort_ota(ota);
    if (result != QM_EOK) {
        ota->status.state = COMTRACE_OTA_FAILED;
        ota->status.error_code = (uint32_t)code;
        notify_status(ota);
        return result;
    }
    ota->status.state = COMTRACE_OTA_FAILED;
    ota->status.error_code = (uint32_t)code;
    notify_status(ota);
    return QM_EOK;
}

static int parse_payload(const uint8_t *payload, uint16_t length, void *user)
{
    comtrace_ota_t *ota = (comtrace_ota_t *)user;
    CborParser parser;
    CborValue root;
    char type[24];
    char sn[COMTRACE_MAX_SN_LEN + 1U];
    size_t type_len;
    uint64_t version;
    uint64_t max_frame;
    uint32_t sid;

    if (comtrace_cbor_root(payload, length, &parser, &root) != QM_EOK)
        return reject(ota, COMTRACE_OTA_ERROR_INVALID_FRAME, -QM_EINVAL);
    if (comtrace_cbor_uint(&root, "v", &version) != QM_EOK)
        return reject(ota, COMTRACE_OTA_ERROR_INVALID_FRAME, -QM_EINVAL);
    if (version != COMTRACE_PROTOCOL_VERSION)
        return reject(ota, COMTRACE_OTA_ERROR_UNSUPPORTED_VERSION, -QM_EINVAL);
    if (comtrace_cbor_text(&root, "type", type, sizeof(type), &type_len) != QM_EOK)
        return reject(ota, COMTRACE_OTA_ERROR_INVALID_FRAME, -QM_EINVAL);
    if (qm_strcmp(type, "error") == 0)
        return handle_error(ota, &root);
    if (comtrace_cbor_common(&root, type, sizeof(type), sn, sizeof(sn), &sid) != QM_EOK ||
        qm_strcmp(sn, ota->sn) != 0 || sid != ota->sid) {
        return reject(ota, COMTRACE_OTA_ERROR_INVALID_FRAME, -QM_EINVAL);
    }
    if (qm_strcmp(type, "update_register_ack") == 0) {
        if (ota->status.state != COMTRACE_OTA_REGISTERING ||
            comtrace_cbor_uint(&root, "max_frame", &max_frame) != QM_EOK || max_frame == 0U ||
            max_frame > ota->max_frame)
            return reject(ota, COMTRACE_OTA_ERROR_INVALID_FRAME, -QM_EINVAL);
        ota->registered = 1U;
        ota->status.state = COMTRACE_OTA_IDLE;
        ota->status.error_code = COMTRACE_OTA_ERROR_NONE;
        notify_status(ota);
        return QM_EOK;
    }
    if (qm_strcmp(type, "update_begin") == 0)
        return handle_begin(ota, &root);
    if (qm_strcmp(type, "update_chunk") == 0)
        return handle_chunk(ota, &root);
    if (qm_strcmp(type, "update_end") == 0)
        return handle_end(ota, &root);
    if (qm_strcmp(type, "update_cancel") == 0)
        return cancel_transfer(ota, 1);
    return reject(ota, COMTRACE_OTA_ERROR_UNSUPPORTED_TYPE, -QM_ENOSYS);
}

int comtrace_ota_init(comtrace_ota_t **out, const comtrace_ota_config_t *config)
{
    comtrace_ota_t *ota;
    size_t sn_len;
    size_t version_len;
    size_t product_len;
    uint16_t max_frame;
    int result;

    if (!out || !config || !config->sn || !config->current_version || !config->product_id ||
        !config->send || !config->authorize || config->sid == 0U || config->max_chunk == 0U ||
        config->max_image_size == 0U)
        return -QM_EINVAL;
    sn_len = qm_strlen(config->sn);
    version_len = qm_strlen(config->current_version);
    product_len = qm_strlen(config->product_id);
    max_frame = config->max_frame ? config->max_frame : CONFIG_QM_COMTRACE_MAX_FRAME;
    if (sn_len == 0U || sn_len > COMTRACE_MAX_SN_LEN ||
        !comtrace_text_is_printable(config->sn, sn_len) ||
        version_len == 0U || version_len > CEUP_MAX_VERSION_LEN ||
        !comtrace_text_is_printable(config->current_version, version_len) || product_len == 0U ||
        product_len > CEUP_MAX_PRODUCT_LEN ||
        !comtrace_text_is_printable(config->product_id, product_len) ||
        max_frame == 0U || max_frame > COMTRACE_MAX_FRAME || config->max_chunk > max_frame)
        return -QM_EINVAL;
    ota = (comtrace_ota_t *)qm_calloc(1U, sizeof(*ota));
    if (!ota)
        return -QM_ENOMEM;
    qm_memcpy(ota->sn, config->sn, (uint32_t)(sn_len + 1U));
    qm_memcpy(ota->current_version, config->current_version, (uint32_t)(version_len + 1U));
    qm_memcpy(ota->product_id, config->product_id, (uint32_t)(product_len + 1U));
    ota->sid = config->sid;
    ota->max_frame = max_frame;
    ota->max_chunk = config->max_chunk;
    ota->max_image_size = config->max_image_size;
    ota->send = config->send;
    ota->authorize = config->authorize;
    ota->on_status = config->on_status;
    ota->reboot = config->reboot;
    ota->user = config->user;
    ota->last_chunk_index = UINT32_MAX;
    ota->status.state = COMTRACE_OTA_UNREGISTERED;
    ota->chunk = (uint8_t *)qm_malloc(ota->max_chunk);
    if (!ota->chunk) {
        qm_free(ota);
        return -QM_ENOMEM;
    }
    result = comtrace_stream_init(&ota->stream, max_frame);
    if (result != QM_EOK) {
        qm_free(ota->chunk);
        qm_free(ota);
        return result;
    }
    *out = ota;
    return QM_EOK;
}

int comtrace_ota_deinit(comtrace_ota_t *ota)
{
    int result;
    if (!ota)
        return -QM_EINVAL;
    result = abort_ota(ota);
    if (result != QM_EOK)
        return result;
    comtrace_stream_deinit(&ota->stream);
    qm_free(ota->chunk);
    qm_free(ota);
    return QM_EOK;
}

int comtrace_ota_register(comtrace_ota_t *ota)
{
    uint8_t *frame;
    uint16_t length;
    int result;
    if (!ota || ota->ota_started ||
        (ota->status.state != COMTRACE_OTA_UNREGISTERED && ota->status.state != COMTRACE_OTA_IDLE &&
         ota->status.state != COMTRACE_OTA_SUCCESS &&
         ota->status.state != COMTRACE_OTA_FAILED &&
         ota->status.state != COMTRACE_OTA_ROLLED_BACK &&
         ota->status.state != COMTRACE_OTA_CANCELLED))
        return -QM_EBUSY;
    comtrace_stream_reset(&ota->stream);
    ota->registered = 0U;
    ota->status.state = COMTRACE_OTA_REGISTERING;
    ota->status.error_code = COMTRACE_OTA_ERROR_NONE;
    notify_status(ota);
    result = comtrace_frame_encode(ota->sn, ota->sid, ota->max_frame, "update_register", 8U,
                                   body_register, ota, &frame, &length);
    if (result == QM_EOK)
        result = comtrace_frame_send(ota->send, ota->user, frame, length);
    if (result != QM_EOK) {
        ota->status.state = COMTRACE_OTA_UNREGISTERED;
        notify_status(ota);
    }
    return result;
}

int comtrace_ota_feed(comtrace_ota_t *ota, const uint8_t *bytes, uint16_t length)
{
    int result;
    if (!ota)
        return -QM_EINVAL;
    ota->error_reported = 0U;
    result = comtrace_stream_feed(&ota->stream, bytes, length, parse_payload, ota);
    if ((result == -QM_EINVAL || result == -QM_EFULL) && !ota->error_reported)
        (void)send_error(ota, COMTRACE_OTA_ERROR_INVALID_FRAME);
    return result;
}

int comtrace_ota_disconnect(comtrace_ota_t *ota)
{
    int result;

    if (!ota)
        return -QM_EINVAL;
    comtrace_stream_reset(&ota->stream);
    ota->registered = 0U;
    if (ota->status.state == COMTRACE_OTA_REBOOTING)
        return QM_EOK;
    result = abort_ota(ota);
    if (result != QM_EOK) {
        ota->status.state = COMTRACE_OTA_FAILED;
        ota->status.error_code = COMTRACE_OTA_ERROR_CONNECTION_LOST;
        notify_status(ota);
        return result;
    }
    if (ota->status.state == COMTRACE_OTA_PREPARED ||
        ota->status.state == COMTRACE_OTA_TRANSFERRING ||
        ota->status.state == COMTRACE_OTA_VERIFYING) {
        ota->status.state = COMTRACE_OTA_FAILED;
        ota->status.error_code = COMTRACE_OTA_ERROR_CONNECTION_LOST;
    } else {
        ota->status.state = COMTRACE_OTA_UNREGISTERED;
        ota->status.error_code = COMTRACE_OTA_ERROR_NONE;
    }
    notify_status(ota);
    return QM_EOK;
}

int comtrace_ota_cancel(comtrace_ota_t *ota)
{
    return ota ? cancel_transfer(ota, 1) : -QM_EINVAL;
}

int comtrace_ota_report_boot_result(comtrace_ota_t *ota, comtrace_ota_state_t state,
                                    uint32_t error_code)
{
    if (!ota || !ota->registered ||
        (state != COMTRACE_OTA_SUCCESS && state != COMTRACE_OTA_ROLLED_BACK))
        return -QM_EINVAL;
    if ((state == COMTRACE_OTA_SUCCESS && error_code != COMTRACE_OTA_ERROR_NONE) ||
        (state == COMTRACE_OTA_ROLLED_BACK && error_code != COMTRACE_OTA_ERROR_ROLLBACK &&
         error_code != COMTRACE_OTA_ERROR_BOOT_FAILED))
        return -QM_EINVAL;
    if (ota->ota_started || ota->status.state == COMTRACE_OTA_REBOOTING ||
        ota->status.state == COMTRACE_OTA_PREPARED ||
        ota->status.state == COMTRACE_OTA_TRANSFERRING ||
        ota->status.state == COMTRACE_OTA_VERIFYING)
        return -QM_EBUSY;
    ota->status.state = state;
    ota->status.error_code = error_code;
    return send_status(ota);
}

int comtrace_ota_get_status(const comtrace_ota_t *ota, comtrace_ota_status_t *status)
{
    if (!ota || !status)
        return -QM_EINVAL;
    *status = ota->status;
    return QM_EOK;
}
