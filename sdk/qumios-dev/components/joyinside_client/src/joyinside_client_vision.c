/**
 * @file joyinside_client_vision.c
 * @brief Bounded JPEG-to-JSON HTTP upload. / 有界内存的 JPEG JSON HTTP 上传。
 */
#include "joyinside_client_vision.h"

#if defined(CONFIG_JOYINSIDE_CLIENT_VISION_SUPPORT) && CONFIG_JOYINSIDE_CLIENT_VISION_SUPPORT
#include "joyinside_client_internal.h"
#include "cJSON.h"
#include "qm_kernel.h"
#include "qm_string.h"
#include "qm_utils_base64.h"

#define IMAGE_REPORT_URL "https://api.joyinside.com/vision/client/image/report"
#define IMAGE_RESPONSE_BYTES 2048U
#define BASE64_INPUT_GROUP 3U
#define BASE64_OUTPUT_GROUP 4U
#ifndef CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS
#define CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS 5000U
#endif

typedef struct {
    const joyinside_client_image_t *image;
    char *prefix;
    uint32_t prefix_len;
    uint32_t encoded_len;
    uint32_t offset;
    uint32_t response_len;
    char response[IMAGE_RESPONSE_BYTES + 1U];
} image_upload_t;

/**
 * @brief Stream the JSON prefix, Base64 payload and suffix. / 流式输出 JSON 前缀、Base64 载荷和后缀。
 *
 * @details offset is the only progress state, so HTTP may request arbitrary
 * buffer sizes without requiring a full encoded JPEG allocation. offset 是
 * 唯一进度状态，HTTP 可请求任意缓冲大小而无需分配完整 Base64 JPEG。
 */
static int image_body(char *buffer, int capacity, void *context)
{
    image_upload_t *upload = context;
    uint32_t written = 0U;
    if (capacity <= 0) return -QM_EINVAL;
    while (written < (uint32_t)capacity) {
        uint32_t available = (uint32_t)capacity - written;
        uint32_t position = upload->offset;
        if (position < upload->prefix_len) {
            uint32_t count = upload->prefix_len - position;
            if (count > available) count = available;
            qm_memcpy(buffer + written, upload->prefix + position, count);
            written += count;
            upload->offset += count;
        } else if (position - upload->prefix_len < upload->encoded_len) {
            uint32_t encoded_offset = position - upload->prefix_len;
            uint32_t input_offset = encoded_offset / BASE64_OUTPUT_GROUP * BASE64_INPUT_GROUP;
            uint32_t remaining = upload->image->jpeg_len - input_offset;
            uint32_t count = remaining;
            uint32_t produced = 0U;
            uint32_t skip = encoded_offset % BASE64_OUTPUT_GROUP;
            if (skip || available < BASE64_OUTPUT_GROUP) {
                /* A short callback may start in a four-byte Base64 group;
                 * encode that group in scratch storage, then emit its suffix.
                 * 短回调可能从四字节 Base64 分组中间开始，先在临时空间编码再输出后半段。 */
                char group[BASE64_OUTPUT_GROUP];
                if (count > BASE64_INPUT_GROUP) count = BASE64_INPUT_GROUP;
                int ret = qm_utils_base64encode(upload->image->jpeg + input_offset, count,
                    sizeof(group), (uint8_t *)group, &produced);
                if (ret != QM_EOK) return ret;
                count = produced - skip;
                if (count > available) count = available;
                qm_memcpy(buffer + written, group + skip, count);
                produced = count;
            } else {
                uint32_t limit = available / BASE64_OUTPUT_GROUP * BASE64_INPUT_GROUP;
                if (count > limit) count = limit;
                int ret = qm_utils_base64encode(upload->image->jpeg + input_offset, count,
                    available, (uint8_t *)buffer + written, &produced);
                if (ret != QM_EOK) return ret;
            }
            written += produced;
            upload->offset += produced;
        } else {
            static const char suffix[] = "\"}";
            uint32_t tail = position - upload->prefix_len - upload->encoded_len;
            if (tail == sizeof(suffix) - 1U) break;
            buffer[written++] = suffix[tail];
            upload->offset++;
        }
    }
    return (int)written;
}

/**
 * @brief Collect a bounded HTTP response. / 收集有界 HTTP 响应。
 *
 * A response beyond IMAGE_RESPONSE_BYTES is rejected rather than truncated,
 * because success state/code must be parsed as a complete JSON document.
 * 响应超过上限直接拒绝，不能截断后再解析成功状态与 code。
 */
static int image_response(const char *data, int length, void *context)
{
    image_upload_t *upload = context;
    if (length < 0 || (uint32_t)length > IMAGE_RESPONSE_BYTES - upload->response_len) return -QM_EFULL;
    if (length) qm_memcpy(upload->response + upload->response_len, data, (uint32_t)length);
    upload->response_len += (uint32_t)length;
    upload->response[upload->response_len] = '\0';
    return QM_EOK;
}

/**
 * @brief Report one JPEG with a snapshot of the active WS identity. / 用活动 WS 身份快照上报一张 JPEG。
 *
 * @details The caller owns image bytes until return. This function owns upload,
 * Authorization and the temporary JSON through cleanup on every path. 调用方
 * 持有图片直到返回；upload、Authorization 与临时 JSON 始终由本函数清理。
 */
int joyinside_client_report_image(joyinside_client_handle_t handle,
                                const joyinside_client_image_t *image)
{
    if (!handle || !image || !image->app_id || !image->app_id[0] || !image->jpeg ||
        image->jpeg_len < 4U || image->jpeg_len > (uint32_t)INT32_MAX / BASE64_OUTPUT_GROUP * BASE64_INPUT_GROUP ||
        image->jpeg[0] != 0xff || image->jpeg[1] != 0xd8 || !image->timestamp_ms ||
        image->timestamp_ms > 9007199254740991ULL) return -QM_EINVAL;
    image_upload_t *upload = qm_calloc(1U, sizeof(*upload));
    if (!upload) return -QM_ENOMEM;
    cJSON *root = cJSON_CreateObject();
    char *authorization = NULL;
    int ret = root ? joyinside_client_http_context(handle, root, &authorization) : -QM_ENOMEM;
    if (ret != QM_EOK) goto cleanup;
    if (!cJSON_AddStringToObject(root, "appId", image->app_id) ||
        !cJSON_AddNumberToObject(root, "frameTimestamp", (double)image->timestamp_ms) ||
        !cJSON_AddStringToObject(root, "imgBase64", "")) { ret = -QM_ENOMEM; goto cleanup; }
    upload->image = image;
    upload->prefix = cJSON_PrintUnformatted(root);
    if (!upload->prefix) { ret = -QM_ENOMEM; goto cleanup; }
    /* imgBase64 is last: retain its opening quote and replace only the empty
     * value, keeping the generated metadata correctly escaped. imgBase64 位于
     * 最后，只替换空值，前面的元数据仍由 cJSON 正确转义。 */
    upload->prefix_len = qm_strlen(upload->prefix) - 2U;
    upload->encoded_len = (image->jpeg_len + BASE64_INPUT_GROUP - 1U) / BASE64_INPUT_GROUP * BASE64_OUTPUT_GROUP;
    if ((uint64_t)upload->prefix_len + upload->encoded_len + 2U > INT32_MAX) { ret = -QM_EFULL; goto cleanup; }
    qm_http_header_t header = {.name = "Authorization", .value = authorization};
    qm_http_client_request_t request = {0};
    qm_http_client_response_t response = {0};
    request.method = QM_HTTP_POST;
    request.url = image->url ? image->url : IMAGE_REPORT_URL;
    request.headers = &header;
    request.header_count = 1;
    request.content_type = "application/json";
    request.body_len = (int)(upload->prefix_len + upload->encoded_len + 2U);
    request.body_cb = image_body;
    request.on_body = image_response;
    request.user = upload;
    request.tls = image->tls;
    request.timeout_ms = image->timeout_ms ? image->timeout_ms : CONFIG_JOYINSIDE_CLIENT_TIMEOUT_MS;
    request.max_body_len = IMAGE_RESPONSE_BYTES;
    request.disable_auto_redirect = QM_TRUE;
    ret = qm_http_client_execute(&request, &response);
    if (ret != QM_EOK) goto cleanup;
    cJSON_Delete(root);
    root = cJSON_ParseWithLengthOpts(upload->response, upload->response_len + 1U, NULL, 1);
    const char *state = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "state"));
    const char *code = cJSON_GetStringValue(cJSON_GetObjectItemCaseSensitive(root, "code"));
    ret = response.status_code == 200 && state && !qm_strcmp(state, "SUCCESS") &&
          code && !qm_strcmp(code, "0000") ? QM_EOK : -QM_EIO;
cleanup:
    cJSON_Delete(root);
    cJSON_free(upload->prefix);
    qm_free(authorization);
    qm_free(upload);
    return ret;
}
#else
/** @brief Feature-disabled image reporter. / 视觉能力关闭时的图片上报桩实现。 */
int joyinside_client_report_image(joyinside_client_handle_t handle,
                                const joyinside_client_image_t *image)
{
    (void)handle;
    (void)image;
    return -QM_ENOSYS;
}
#endif
