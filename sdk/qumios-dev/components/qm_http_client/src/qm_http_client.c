#include "qm_http_client.h"
#include "qm_http_client_log.h"

#define QM_HTTP_CLIENT_INTERNAL
#include "qm_http_client_transport.h"
#undef QM_HTTP_CLIENT_INTERNAL

#include "qm_kernel.h"
#include "qm_string.h"
#include "qm_utils_base64.h"
#include "qm_utils_md5.h"
#include "qm_utils_sha256.h"

#include <limits.h>

#ifndef CONFIG_QM_HTTP_CLIENT_TIMEOUT_MS
#define CONFIG_QM_HTTP_CLIENT_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_QM_HTTP_CLIENT_HEADER_MAX
#define CONFIG_QM_HTTP_CLIENT_HEADER_MAX 1024
#endif

#ifndef CONFIG_QM_HTTP_CLIENT_BODY_MAX
#define CONFIG_QM_HTTP_CLIENT_BODY_MAX 4096
#endif

#ifndef CONFIG_QM_HTTP_CLIENT_RX_BUF_SIZE
#define CONFIG_QM_HTTP_CLIENT_RX_BUF_SIZE 512
#endif

#ifndef CONFIG_QM_HTTP_CLIENT_LINE_MAX
#define CONFIG_QM_HTTP_CLIENT_LINE_MAX 64
#endif

#ifndef CONFIG_QM_HTTP_CLIENT_REDIRECT_MAX
#define CONFIG_QM_HTTP_CLIENT_REDIRECT_MAX 5
#endif

#ifndef CONFIG_QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986
#define CONFIG_QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986 0
#endif

#ifndef CONFIG_QM_HTTP_CLIENT_AUTH_RETRY_MAX
#define CONFIG_QM_HTTP_CLIENT_AUTH_RETRY_MAX 2
#endif

#define LOG_TAG "qm_http_client"
#define QM_HTTP_CLIENT_DEFAULT_REDIRECTS CONFIG_QM_HTTP_CLIENT_REDIRECT_MAX
#define QM_HTTP_CLIENT_DEFAULT_AUTH_RETRIES CONFIG_QM_HTTP_CLIENT_AUTH_RETRY_MAX
#define QM_HTTP_CLIENT_DIGEST_MD5_BIN_LEN 16
#define QM_HTTP_CLIENT_DIGEST_MD5_HEX_LEN (QM_HTTP_CLIENT_DIGEST_MD5_BIN_LEN * 2)
#define QM_HTTP_CLIENT_DIGEST_SHA256_BIN_LEN 32
#define QM_HTTP_CLIENT_DIGEST_SHA256_HEX_LEN (QM_HTTP_CLIENT_DIGEST_SHA256_BIN_LEN * 2)
#define QM_HTTP_CLIENT_DIGEST_HEX_MAX_LEN QM_HTTP_CLIENT_DIGEST_SHA256_HEX_LEN
#define QM_HTTP_CLIENT_HOST_MAX 96
#define QM_HTTP_CLIENT_PATH_MAX 192
#define QM_HTTP_CLIENT_URL_MAX 384

typedef struct {
    char scheme[6];
    char host[QM_HTTP_CLIENT_HOST_MAX];
    uint16_t port;
    char path[QM_HTTP_CLIENT_PATH_MAX];
    qm_bool secure;
} qm_http_client_url_t;

typedef enum {
    QM_HTTP_BODY_NONE = 0,
    QM_HTTP_BODY_FIXED,
    QM_HTTP_BODY_CHUNKED,
    QM_HTTP_BODY_UNTIL_CLOSE,
} qm_http_client_body_mode_t;

typedef enum {
    QM_HTTP_CLIENT_DIGEST_ALG_MD5 = 0,
    QM_HTTP_CLIENT_DIGEST_ALG_MD5_SESS,
    QM_HTTP_CLIENT_DIGEST_ALG_SHA256,
    QM_HTTP_CLIENT_DIGEST_ALG_SHA256_SESS,
} qm_http_client_digest_alg_t;

typedef struct {
    char *realm;
    char *nonce;
    char *opaque;
    char *algorithm;
    char *qop;
    uint32_t nc;
    qm_bool valid;
} qm_http_client_digest_t;

struct qm_http_client {
    qm_http_method_t method;
    char *url;
    char *username;
    char *password;
    qm_http_client_auth_type_t auth_type;
    qm_http_client_digest_t digest;
    const char *digest_body;
    int digest_body_len;
    char *content_type;
    char *user_agent;
    qm_http_header_t *headers;
    int header_count;
    uint32_t timeout_ms;
    uint32_t max_header_len;
    uint32_t max_body_len;
    uint32_t rx_buf_size;
    qm_bool disable_auto_redirect;
    int max_redirection_count;
    qm_bool keep_alive;
    const qm_http_tls_config_t *tls;
    qm_http_client_event_cb_t on_event;
    qm_http_client_interrupt_fn interrupted;
    void *user;

    qm_http_client_transport_t transport;
    void *conn;
    qm_http_client_url_t conn_url;
    qm_bool conn_url_valid;

    qm_http_client_response_t response;
    qm_http_client_body_mode_t body_mode;
    uint32_t body_read;
    uint32_t body_left;
    uint32_t chunk_left;
    qm_bool headers_fetched;
    qm_bool complete;
    int write_len;
    int written;
    char *location;
    qm_bool finish_event_sent;
};

static volatile uint32_t g_qm_http_client_log_flags = QM_HTTP_CLIENT_KCONFIG_LOG_FLAGS;

uint32_t qm_http_client_get_log_flags(void)
{
    return g_qm_http_client_log_flags;
}

int qm_http_client_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~(uint32_t)QM_HTTP_CLIENT_LOG_ALL) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_http_client_log_flags = log_flags;
    return QM_EOK;
}

static int qm_http_client_read_line(qm_http_client_handle_t client, char *line,
                                    int line_size, uint32_t timeout_ms);
static int qm_http_client_dispatch_event(qm_http_client_handle_t client,
                                         qm_http_client_event_id_t event_id,
                                         const char *data, int data_len,
                                         const char *header_name,
                                         const char *header_value,
                                         int error_code,
                                         const char *redirect_url);
static int qm_http_client_report_error(qm_http_client_handle_t client, int error_code);
static int qm_http_client_dispatch_finish(qm_http_client_handle_t client);
static int qm_http_client_drain_response_body_internal(qm_http_client_handle_t client,
                                                       qm_bool dispatch_events);
static int qm_http_client_perform_internal(qm_http_client_handle_t client,
                                           const char *body, int body_len,
                                           qm_http_request_body_cb_t body_cb,
                                           qm_http_request_body_reset_cb_t body_reset,
                                           qm_http_body_cb_t on_body);

static int qm_http_client_parse_decimal(const char *value, int len, uint32_t *out)
{
    uint32_t parsed = 0;
    int i;

    if (!value || len <= 0 || !out) return -QM_EINVAL;
    for (i = 0; i < len; i++) {
        uint32_t digit;

        if (value[i] < '0' || value[i] > '9') return -QM_EINVAL;
        digit = (uint32_t)(value[i] - '0');
        if (parsed > (UINT32_MAX - digit) / 10) return -QM_EFULL;
        parsed = parsed * 10 + digit;
    }
    *out = parsed;
    return QM_EOK;
}

static int qm_http_client_token_has(const char *value, const char *token)
{
    int token_len;

    if (!value || !token) return 0;
    token_len = (int)qm_strlen(token);
    while (*value) {
        const char *start;
        int len;

        while (*value == ' ' || *value == '\t' || *value == ',') value++;
        if (*value == 0) break;
        start = value;
        while (*value && *value != ',') value++;
        len = (int)(value - start);
        while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) len--;
        if (len == token_len && qm_strncasecmp(start, token, token_len) == 0) {
            return 1;
        }
    }

    return 0;
}

static int qm_http_client_event_ret(int ret)
{
    return ret == QM_EOK ? QM_EOK : -QM_EIO;
}

static int qm_http_client_dispatch_event(qm_http_client_handle_t client,
                                         qm_http_client_event_id_t event_id,
                                         const char *data, int data_len,
                                         const char *header_name,
                                         const char *header_value,
                                         int error_code,
                                         const char *redirect_url)
{
    qm_http_client_event_t event;

    if (!client || !client->on_event) return QM_EOK;
    qm_memset(&event, 0, sizeof(event));
    event.event_id = event_id;
    event.client = client;
    event.user = client->user;
    event.data = data;
    event.data_len = data_len;
    event.header_name = header_name;
    event.header_value = header_value;
    event.status_code = client->response.status_code;
    event.error_code = error_code;
    event.redirect_url = redirect_url;
    return client->on_event(&event);
}

static int qm_http_client_dispatch_error(qm_http_client_handle_t client, int error_code)
{
    int ret;

    if (!client) return -QM_EINVAL;
    ret = qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_ERROR,
                                        NULL, 0, NULL, NULL, error_code, NULL);
    return ret == QM_EOK ? error_code : qm_http_client_event_ret(ret);
}

static int qm_http_client_report_error(qm_http_client_handle_t client, int error_code)
{
    if (error_code > 0) error_code = -error_code;
    if (!client || error_code == QM_EOK) return error_code;
    QM_HTTP_CLIENT_LOGE(LOG_TAG, "request error code=%d", error_code);
    return qm_http_client_dispatch_error(client, error_code);
}

static int qm_http_client_dispatch_finish(qm_http_client_handle_t client)
{
    int ret;

    if (!client || client->finish_event_sent) return QM_EOK;
    client->finish_event_sent = QM_TRUE;
    ret = qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_FINISH,
                                        NULL, 0, NULL, NULL, QM_EOK, NULL);
    if (ret != QM_EOK) {
        return qm_http_client_report_error(client, qm_http_client_event_ret(ret));
    }
    QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_LIFECYCLE, "response finished status=%d bytes=%lu",
                        client->response.status_code,
                        (unsigned long)client->body_read);
    return QM_EOK;
}

/**
 * @brief Dispatch parsed response headers one by one as header events.
 *
 * The response head buffer is parsed in-place. Each `name: value` pair is
 * temporarily zero-terminated and forwarded through the event callback, then
 * the original buffer content is restored before continuing.
 *
 * @param[in] client HTTP client handle
 * @param[in,out] head Raw response head buffer ending with `\r\n\r\n`
 * @param[in] header_len Valid bytes in @p head
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
static int qm_http_client_dispatch_headers(qm_http_client_handle_t client,
                                           char *head, int header_len)
{
    char *p;
    char *end;

    if (!client || !head || header_len <= 0) return -QM_EINVAL;
    end = head + header_len;
    p = (char *)qm_memstr(head, (uint32_t)header_len, "\r\n");
    if (!p) return -QM_EINVAL;
    p += 2;

    while (p < end) {
        char *line_end;
        char *colon;
        char *name;
        char *name_end;
        char *value;
        char *value_end;
        char saved_colon;
        char saved_line;
        int line_len;
        int ret;

        line_end = (char *)qm_memstr(p, (uint32_t)(end - p), "\r\n");
        if (!line_end) break;
        line_len = (int)(line_end - p);
        if (line_len == 0) break;

        colon = qm_memchr(p, ':', (uint32_t)line_len);
        if (!colon) {
            p = line_end + 2;
            continue;
        }

        name = p;
        name_end = colon;
        while (name_end > name && (name_end[-1] == ' ' || name_end[-1] == '\t')) {
            name_end--;
        }
        value = colon + 1;
        while (value < line_end && (*value == ' ' || *value == '\t')) {
            value++;
        }
        value_end = line_end;
        while (value_end > value && (value_end[-1] == ' ' || value_end[-1] == '\t')) {
            value_end--;
        }

        saved_colon = *name_end;
        saved_line = *value_end;
        *name_end = 0;
        *value_end = 0;
        ret = qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_HEADER,
                                            p, line_len, name, value, QM_EOK, NULL);
        *value_end = saved_line;
        *name_end = saved_colon;
        if (ret != QM_EOK) return qm_http_client_event_ret(ret);

        p = line_end + 2;
    }

    return QM_EOK;
}

static char *qm_http_client_copy_value(const char *value, int len, qm_bool quoted)
{
    char *copy;
    int start = 0;
    int end = len;
    int out = 0;

    if (!value || len < 0) return NULL;
    while (start < end && (value[start] == ' ' || value[start] == '\t')) start++;
    while (end > start && (value[end - 1] == ' ' || value[end - 1] == '\t')) end--;
    if (quoted && end - start >= 2 && value[start] == '"' && value[end - 1] == '"') {
        start++;
        end--;
    }

    copy = (char *)qm_malloc((unsigned int)(end - start) + 1);
    if (!copy) return NULL;
    while (start < end) {
        char c = value[start++];
        if (quoted && c == '\\' && start < end) {
            c = value[start++];
        }
        copy[out++] = c;
    }
    copy[out] = 0;
    return copy;
}

static qm_bool qm_http_client_key_equals(const char *key, int key_len, const char *name)
{
    int name_len;

    if (!key || !name || key_len < 0) return QM_FALSE;
    name_len = (int)qm_strlen(name);
    return key_len == name_len &&
           qm_strncasecmp(key, name, name_len) == 0 ? QM_TRUE : QM_FALSE;
}

static void qm_http_client_digest_clear(qm_http_client_digest_t *digest)
{
    if (!digest) return;
    if (digest->realm) qm_free(digest->realm);
    if (digest->nonce) qm_free(digest->nonce);
    if (digest->opaque) qm_free(digest->opaque);
    if (digest->algorithm) qm_free(digest->algorithm);
    if (digest->qop) qm_free(digest->qop);
    qm_memset(digest, 0, sizeof(*digest));
}

static void qm_http_client_free_str(char **value)
{
    if (!value || !*value) return;
    qm_free(*value);
    *value = NULL;
}

static int qm_http_client_digest_set_value(char **field, const char *value,
                                           int len, qm_bool quoted)
{
    char *copy;

    if (!field) return -QM_EINVAL;
    copy = qm_http_client_copy_value(value, len, quoted);
    if (!copy) return -QM_ENOMEM;
    qm_http_client_free_str(field);
    *field = copy;
    return QM_EOK;
}

static const char *qm_http_client_find_auth_scheme(const char *value, const char *scheme)
{
    const char *p;
    int scheme_len;

    if (!value || !scheme) return NULL;
    scheme_len = (int)qm_strlen(scheme);
    p = value;
    while ((p = qm_strcasestr(p, scheme)) != NULL) {
        char before = p == value ? 0 : p[-1];
        char after = p[scheme_len];
        qm_bool valid_before = (p == value || before == ',' ||
                                before == ' ' || before == '\t') ? QM_TRUE : QM_FALSE;
        qm_bool valid_after = (after == 0 || after == ',' ||
                               after == ' ' || after == '\t') ? QM_TRUE : QM_FALSE;
        if (valid_before && valid_after) return p;
        p += scheme_len;
    }
    return NULL;
}

/**
 * @brief Parse a `WWW-Authenticate: Digest ...` challenge.
 *
 * This helper extracts digest authentication parameters from the header value
 * and refreshes the client's cached digest challenge state. Unsupported or
 * malformed challenges clear the previous digest state so later retries do not
 * accidentally reuse stale parameters.
 *
 * @param[in] client HTTP client handle
 * @param[in] value Header value of `WWW-Authenticate`
 * @return QM_EOK on success
 * @return -QM_ENOENT if no Digest challenge is present
 * @return Negative error code on failure
 */
static int qm_http_client_parse_digest_challenge(qm_http_client_handle_t client,
                                                 const char *value)
{
    const char *p;
    char *realm = NULL;
    char *nonce = NULL;
    char *opaque = NULL;
    char *algorithm = NULL;
    char *qop = NULL;
    int ret = QM_EOK;

    if (!client || !value) return -QM_EINVAL;
    p = qm_http_client_find_auth_scheme(value, "Digest");
    if (!p) {
        qm_http_client_digest_clear(&client->digest);
        return -QM_ENOENT;
    }
    p += (int)qm_strlen("Digest");

    while (*p) {
        const char *key;
        const char *param_value;
        int key_len;
        int value_len;
        qm_bool quoted = QM_FALSE;

        while (*p == ' ' || *p == '\t' || *p == ',') p++;
        if (*p == 0) break;

        key = p;
        while (*p && *p != '=' && *p != ',' && *p != ' ' && *p != '\t') p++;
        key_len = (int)(p - key);
        while (*p == ' ' || *p == '\t') p++;
        if (*p != '=') {
            break;
        }
        p++;
        while (*p == ' ' || *p == '\t') p++;

        if (*p == '"') {
            quoted = QM_TRUE;
            p++;
            param_value = p;
            while (*p) {
                if (*p == '\\' && p[1]) {
                    p += 2;
                    continue;
                }
                if (*p == '"') break;
                p++;
            }
            value_len = (int)(p - param_value);
            if (*p == '"') p++;
        } else {
            param_value = p;
            while (*p && *p != ',') p++;
            value_len = (int)(p - param_value);
        }

        if (qm_http_client_key_equals(key, key_len, "realm")) {
            ret = qm_http_client_digest_set_value(&realm, param_value, value_len, quoted);
        } else if (qm_http_client_key_equals(key, key_len, "nonce")) {
            ret = qm_http_client_digest_set_value(&nonce, param_value, value_len, quoted);
        } else if (qm_http_client_key_equals(key, key_len, "opaque")) {
            ret = qm_http_client_digest_set_value(&opaque, param_value, value_len, quoted);
        } else if (qm_http_client_key_equals(key, key_len, "algorithm")) {
            ret = qm_http_client_digest_set_value(&algorithm, param_value, value_len, quoted);
        } else if (qm_http_client_key_equals(key, key_len, "qop")) {
            ret = qm_http_client_digest_set_value(&qop, param_value, value_len, quoted);
        }
        if (ret != QM_EOK) goto fail;
    }

    if (!realm || !nonce) {
        ret = -QM_EINVAL;
        goto fail;
    }

    qm_http_client_digest_clear(&client->digest);
    client->digest.realm = realm;
    client->digest.nonce = nonce;
    client->digest.opaque = opaque;
    client->digest.algorithm = algorithm;
    client->digest.qop = qop;
    client->digest.nc = 0;
    client->digest.valid = QM_TRUE;
    return QM_EOK;

fail:
    if (realm) qm_free(realm);
    if (nonce) qm_free(nonce);
    if (opaque) qm_free(opaque);
    if (algorithm) qm_free(algorithm);
    if (qop) qm_free(qop);
    qm_http_client_digest_clear(&client->digest);
    return ret;
}

static uint32_t qm_http_client_digest_hex_len(qm_http_client_digest_alg_t algorithm)
{
    switch (algorithm) {
    case QM_HTTP_CLIENT_DIGEST_ALG_SHA256:
    case QM_HTTP_CLIENT_DIGEST_ALG_SHA256_SESS:
        return QM_HTTP_CLIENT_DIGEST_SHA256_HEX_LEN;
    case QM_HTTP_CLIENT_DIGEST_ALG_MD5:
    case QM_HTTP_CLIENT_DIGEST_ALG_MD5_SESS:
    default:
        return QM_HTTP_CLIENT_DIGEST_MD5_HEX_LEN;
    }
}

static qm_bool qm_http_client_digest_is_sess(qm_http_client_digest_alg_t algorithm)
{
    return algorithm == QM_HTTP_CLIENT_DIGEST_ALG_MD5_SESS ||
           algorithm == QM_HTTP_CLIENT_DIGEST_ALG_SHA256_SESS ? QM_TRUE : QM_FALSE;
}

static void qm_http_client_hex_encode(const uint8_t *data, uint32_t len, char *out)
{
    static const char hex[] = "0123456789abcdef";
    uint32_t i;

    for (i = 0; i < len; i++) {
        out[i * 2] = hex[(data[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[data[i] & 0x0f];
    }
    out[len * 2] = 0;
}

static int qm_http_client_digest_hash_hex(qm_http_client_digest_alg_t algorithm,
                                          const uint8_t *data, uint32_t len,
                                          char *out, uint32_t out_size)
{
    const uint8_t empty = 0;
    const uint8_t *input;
    uint32_t hex_len;

    if ((!data && len > 0) || !out) return -QM_EINVAL;
    hex_len = qm_http_client_digest_hex_len(algorithm);
    if (out_size < hex_len + 1) return -QM_EFULL;
    input = len > 0 ? data : &empty;

    switch (algorithm) {
    case QM_HTTP_CLIENT_DIGEST_ALG_MD5:
    case QM_HTTP_CLIENT_DIGEST_ALG_MD5_SESS: {
        unsigned char digest[QM_HTTP_CLIENT_DIGEST_MD5_BIN_LEN];

        qm_utils_md5((const unsigned char *)input, (size_t)len, digest);
        qm_utils_md5_hexstr(digest, (unsigned char *)out);
        out[QM_HTTP_CLIENT_DIGEST_MD5_HEX_LEN] = 0;
        break;
    }
    case QM_HTTP_CLIENT_DIGEST_ALG_SHA256:
    case QM_HTTP_CLIENT_DIGEST_ALG_SHA256_SESS: {
        unsigned char digest[QM_HTTP_CLIENT_DIGEST_SHA256_BIN_LEN];

        qm_utils_sha256((const unsigned char *)input, len, digest);
        qm_http_client_hex_encode(digest, QM_HTTP_CLIENT_DIGEST_SHA256_BIN_LEN, out);
        break;
    }
    default:
        return -QM_ENOSYS;
    }

    return QM_EOK;
}

static int qm_http_client_digest_hash_join_hex(qm_http_client_digest_alg_t algorithm,
                                               const char *const *parts, int part_count,
                                               char *out, uint32_t out_size)
{
    char *joined;
    char *p;
    uint32_t total = 0;
    int i;
    int ret;

    if (!parts || part_count <= 0 || !out) return -QM_EINVAL;
    for (i = 0; i < part_count; i++) {
        uint32_t len;

        if (!parts[i]) return -QM_EINVAL;
        len = qm_strlen(parts[i]);
        if (len > UINT32_MAX - total) return -QM_EFULL;
        total += len;
        if (i + 1 < part_count) {
            if (total == UINT32_MAX) return -QM_EFULL;
            total++;
        }
    }

    joined = (char *)qm_malloc(total + 1);
    if (!joined) return -QM_ENOMEM;
    p = joined;
    for (i = 0; i < part_count; i++) {
        unsigned int len = (unsigned int)qm_strlen(parts[i]);
        if (i > 0) *p++ = ':';
        qm_memcpy(p, parts[i], len);
        p += len;
    }
    *p = 0;

    ret = qm_http_client_digest_hash_hex(algorithm, (const uint8_t *)joined, total,
                                         out, out_size);
    qm_free(joined);
    return ret;
}

static int qm_http_client_digest_algorithm(qm_http_client_digest_t *digest,
                                           qm_http_client_digest_alg_t *algorithm,
                                           const char **algorithm_name)
{
    const char *value;

    if (!algorithm || !algorithm_name) return -QM_EINVAL;
    value = (!digest || !digest->algorithm || digest->algorithm[0] == 0) ?
            "MD5" : digest->algorithm;

    if (qm_strcasecmp(value, "MD5") == 0) {
        *algorithm = QM_HTTP_CLIENT_DIGEST_ALG_MD5;
        *algorithm_name = "MD5";
        return QM_EOK;
    }
    if (qm_strcasecmp(value, "MD5-sess") == 0) {
        *algorithm = QM_HTTP_CLIENT_DIGEST_ALG_MD5_SESS;
        *algorithm_name = "MD5-sess";
        return QM_EOK;
    }
    if (qm_strcasecmp(value, "SHA-256") == 0) {
        *algorithm = QM_HTTP_CLIENT_DIGEST_ALG_SHA256;
        *algorithm_name = "SHA-256";
        return QM_EOK;
    }
    if (qm_strcasecmp(value, "SHA-256-sess") == 0) {
        *algorithm = QM_HTTP_CLIENT_DIGEST_ALG_SHA256_SESS;
        *algorithm_name = "SHA-256-sess";
        return QM_EOK;
    }

    return -QM_ENOSYS;
}

static const char *qm_http_client_digest_qop(qm_http_client_digest_t *digest)
{
    if (!digest || !digest->qop || digest->qop[0] == 0) return NULL;
    if (qm_http_client_token_has(digest->qop, "auth")) return "auth";
    if (qm_http_client_token_has(digest->qop, "auth-int")) return "auth-int";
    return NULL;
}

static int qm_http_client_digest_cnonce(qm_http_client_handle_t client, char cnonce[33])
{
    uint32_t seed;
    uint32_t r1;
    uint32_t r2;
    uint32_t r3;
    uint32_t r4;

    if (!cnonce) return -QM_EINVAL;
    seed = qm_now_ms() ^ (uint32_t)(uintptr_t)client;
    qm_srandom(seed);
    r1 = qm_random_get(UINT32_MAX) ^ seed;
    r2 = qm_random_get(UINT32_MAX);
    r3 = qm_random_get(UINT32_MAX) ^ qm_now_ms();
    r4 = qm_random_get(UINT32_MAX);
    qm_snprintf(cnonce, 33, "%08x%08x%08x%08x",
             (unsigned int)r1, (unsigned int)r2,
             (unsigned int)r3, (unsigned int)r4);
    return QM_EOK;
}

static int qm_http_client_digest_auth_value(qm_http_client_handle_t client,
                                            const char *method, const char *uri,
                                            const char *body, int body_len,
                                            uint32_t *next_nc, char **out_value)
{
    qm_http_client_digest_t *digest;
    qm_http_client_digest_alg_t algorithm;
    const char *algorithm_name;
    const char *qop;
    char ha1[QM_HTTP_CLIENT_DIGEST_HEX_MAX_LEN + 1];
    char ha2[QM_HTTP_CLIENT_DIGEST_HEX_MAX_LEN + 1];
    char response[QM_HTTP_CLIENT_DIGEST_HEX_MAX_LEN + 1];
    char cnonce[33];
    char nc_buf[9];
    char entity_hash[QM_HTTP_CLIENT_DIGEST_HEX_MAX_LEN + 1];
    char *value;
    int value_len;
    int used;
    uint32_t nc;
    int ret;

    if (!client || !method || !uri || !next_nc || !out_value) return -QM_EINVAL;
    *out_value = NULL;
    digest = &client->digest;
    if (!digest->valid || !digest->realm || !digest->nonce ||
        !client->username || !client->password) {
        return -QM_EINVAL;
    }

    ret = qm_http_client_digest_algorithm(digest, &algorithm, &algorithm_name);
    if (ret != QM_EOK) return ret;

    qop = qm_http_client_digest_qop(digest);
    if (digest->qop && !qop) return -QM_ENOSYS;
    if (body_len < 0) return -QM_EINVAL;

    ret = qm_http_client_digest_cnonce(client, cnonce);
    if (ret != QM_EOK) return ret;

    {
        const char *parts[] = {client->username, digest->realm, client->password};
        ret = qm_http_client_digest_hash_join_hex(algorithm, parts, 3,
                                                  ha1, sizeof(ha1));
        if (ret != QM_EOK) return ret;
    }
    if (qm_http_client_digest_is_sess(algorithm)) {
        const char *parts[] = {ha1, digest->nonce, cnonce};
        ret = qm_http_client_digest_hash_join_hex(algorithm, parts, 3,
                                                  ha1, sizeof(ha1));
        if (ret != QM_EOK) return ret;
    }

    if (qop && qm_strcasecmp(qop, "auth-int") == 0) {
        const uint8_t empty_body = 0;
        const uint8_t *body_data = body_len > 0 ? (const uint8_t *)body : &empty_body;
        if (body_len > 0 && !body) return -QM_EINVAL;
        ret = qm_http_client_digest_hash_hex(algorithm, body_data, (uint32_t)body_len,
                                             entity_hash, sizeof(entity_hash));
        if (ret != QM_EOK) return ret;
        {
            const char *parts[] = {method, uri, entity_hash};
            ret = qm_http_client_digest_hash_join_hex(algorithm, parts, 3,
                                                      ha2, sizeof(ha2));
            if (ret != QM_EOK) return ret;
        }
    } else {
        const char *parts[] = {method, uri};
        ret = qm_http_client_digest_hash_join_hex(algorithm, parts, 2,
                                                  ha2, sizeof(ha2));
        if (ret != QM_EOK) return ret;
    }

    nc = digest->nc + 1;
    qm_snprintf(nc_buf, sizeof(nc_buf), "%08x", (unsigned int)nc);
    if (qop) {
        const char *parts[] = {ha1, digest->nonce, nc_buf, cnonce, qop, ha2};
        ret = qm_http_client_digest_hash_join_hex(algorithm, parts, 6,
                                                  response, sizeof(response));
        if (ret != QM_EOK) return ret;
    } else {
        const char *parts[] = {ha1, digest->nonce, ha2};
        ret = qm_http_client_digest_hash_join_hex(algorithm, parts, 3,
                                                  response, sizeof(response));
        if (ret != QM_EOK) return ret;
    }

    value_len = (int)(qm_strlen("Digest username=\"") + qm_strlen(client->username) +
                      qm_strlen("\", realm=\"") + qm_strlen(digest->realm) +
                      qm_strlen("\", nonce=\"") + qm_strlen(digest->nonce) +
                      qm_strlen("\", uri=\"") + qm_strlen(uri) +
                      qm_strlen("\", algorithm=") + qm_strlen(algorithm_name) +
                      qm_strlen(", response=\"") + qm_strlen(response) + qm_strlen("\""));
    if (qop) {
        value_len += (int)(qm_strlen(", qop=") + qm_strlen(qop) +
                           qm_strlen(", nc=") + qm_strlen(nc_buf) +
                           qm_strlen(", cnonce=\"") + qm_strlen(cnonce) + qm_strlen("\""));
    } else if (qm_http_client_digest_is_sess(algorithm)) {
        value_len += (int)(qm_strlen(", cnonce=\"") + qm_strlen(cnonce) + qm_strlen("\""));
    }
    if (digest->opaque) {
        value_len += (int)(qm_strlen(", opaque=\"") + qm_strlen(digest->opaque) + qm_strlen("\""));
    }

    value = (char *)qm_malloc((unsigned int)value_len + 1);
    if (!value) return -QM_ENOMEM;
    used = qm_snprintf(value, (unsigned int)value_len + 1,
                    "Digest username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", "
                    "algorithm=%s, response=\"%s\"",
                    client->username, digest->realm, digest->nonce, uri,
                    algorithm_name, response);
    if (used < 0 || used > value_len) {
        qm_free(value);
        return -QM_EIO;
    }
    if (qop) {
        ret = qm_snprintf(value + used, (unsigned int)(value_len - used) + 1,
                       ", qop=%s, nc=%s, cnonce=\"%s\"", qop, nc_buf, cnonce);
        if (ret < 0 || used + ret > value_len) {
            qm_free(value);
            return -QM_EIO;
        }
        used += ret;
    } else if (qm_http_client_digest_is_sess(algorithm)) {
        ret = qm_snprintf(value + used, (unsigned int)(value_len - used) + 1,
                       ", cnonce=\"%s\"", cnonce);
        if (ret < 0 || used + ret > value_len) {
            qm_free(value);
            return -QM_EIO;
        }
        used += ret;
    }
    if (digest->opaque) {
        ret = qm_snprintf(value + used, (unsigned int)(value_len - used) + 1,
                       ", opaque=\"%s\"", digest->opaque);
        if (ret < 0 || used + ret > value_len) {
            qm_free(value);
            return -QM_EIO;
        }
    }

    *next_nc = nc;
    *out_value = value;
    return QM_EOK;
}

static void qm_http_client_header_free(qm_http_header_t *header)
{
    if (!header) return;
    if (header->name) qm_free((void *)header->name);
    if (header->value) qm_free((void *)header->value);
    header->name = NULL;
    header->value = NULL;
}

static qm_bool qm_http_client_is_reserved_header(const char *name)
{
    if (!name) return QM_FALSE;
    return qm_strcasecmp(name, "Host") == 0 ||
           qm_strcasecmp(name, "Connection") == 0 ||
           qm_strcasecmp(name, "Content-Length") == 0;
}

static int qm_http_client_header_find(qm_http_client_handle_t client, const char *name)
{
    int i;
    int name_len;

    if (!client || !name) return -1;
    name_len = (int)qm_strlen(name);
    for (i = 0; i < client->header_count; i++) {
        int header_len;
        if (!client->headers[i].name) continue;
        header_len = (int)qm_strlen(client->headers[i].name);
        if (header_len == name_len &&
            qm_strncasecmp(client->headers[i].name, name, name_len) == 0) {
            return i;
        }
    }

    return -1;
}

static const char *qm_http_client_method_str(qm_http_method_t method)
{
    switch (method) {
    case QM_HTTP_GET: return "GET";
    case QM_HTTP_POST: return "POST";
    case QM_HTTP_PUT: return "PUT";
    case QM_HTTP_DELETE: return "DELETE";
    case QM_HTTP_HEAD: return "HEAD";
    default: return "GET";
    }
}

static int qm_http_client_url_parse(const char *url, qm_http_client_url_t *out)
{
    const char *p;
    const char *authority;
    const char *authority_end;
    const char *path;
    const char *query;
    const char *fragment;
    const char *userp;
    const char *host_start;
    const char *host_end;
    const char *portp = NULL;
    int host_len;
    int scheme_len;
    int port = 0;
    int path_len;

    if (!url || !out) return -QM_EINVAL;
    qm_memset(out, 0, sizeof(*out));
    p = qm_strstr(url, "://");
    if (!p) return -QM_EINVAL;
    scheme_len = (int)(p - url);
    if (scheme_len <= 0 || scheme_len >= (int)sizeof(out->scheme)) return -QM_EINVAL;
    qm_memcpy(out->scheme, url, scheme_len);
    out->scheme[scheme_len] = 0;
    if (qm_strcasecmp(out->scheme, "https") == 0) {
        out->secure = QM_TRUE;
    } else if (qm_strcasecmp(out->scheme, "http") == 0) {
        out->secure = QM_FALSE;
    } else {
        return -QM_EINVAL;
    }
    authority = p + 3;
    path = qm_strchr(authority, '/');
    query = qm_strchr(authority, '?');
    fragment = qm_strchr(authority, '#');
    if (fragment && query && query > fragment) {
        query = NULL;
    }
    authority_end = authority + qm_strlen(authority);

    if (path && path < authority_end) authority_end = path;
    if (query && query < authority_end) authority_end = query;
    if (fragment && fragment < authority_end) authority_end = fragment;
    if (path != authority_end) path = NULL;
    if (authority >= authority_end) return -QM_EINVAL;

    userp = qm_memchr(authority, '@', (int)(authority_end - authority));
    if (userp) return -QM_EINVAL;

    host_start = authority;
    host_end = authority_end;

    if (*host_start == '[') {
        const char *ipv6_end = qm_memchr(host_start + 1, ']', (int)(authority_end - host_start - 1));
        if (!ipv6_end) return -QM_EINVAL;
        host_start++;
        host_end = ipv6_end;
        if (ipv6_end + 1 < authority_end) {
            if (ipv6_end[1] != ':') return -QM_EINVAL;
            portp = ipv6_end + 1;
        }
    } else {
        portp = qm_memchr(host_start, ':', (int)(authority_end - host_start));
        if (portp) host_end = portp;
    }

    host_len = (int)(host_end - host_start);
    if (host_len <= 0) return -QM_EINVAL;
    if (host_len >= (int)sizeof(out->host)) return -QM_EFULL;

    if (portp) {
        uint32_t parsed_port;
        int port_len = (int)(authority_end - portp - 1);
        if (port_len <= 0 ||
            qm_http_client_parse_decimal(portp + 1, port_len, &parsed_port) != QM_EOK ||
            parsed_port > UINT16_MAX) {
            return -QM_EINVAL;
        }
        port = (int)parsed_port;
    }

    qm_memcpy(out->host, host_start, host_len);
    out->host[host_len] = 0;
    out->port = port > 0 ? (uint16_t)port : (out->secure ? 443 : 80);
    if (path) {
        const char *path_end = fragment ? fragment : (url + qm_strlen(url));
        path_len = (int)(path_end - path);
    } else if (query || fragment) {
        const char *suffix = query ? query : fragment;
        const char *suffix_end = fragment ? fragment : (url + qm_strlen(url));
        path_len = 1 + (int)(suffix_end - suffix);
        if (path_len >= (int)sizeof(out->path)) return -QM_EFULL;
        qm_strcpy(out->path, "/");
        qm_memcpy(out->path + 1, suffix, (uint32_t)(path_len - 1));
        out->path[path_len] = 0;
        return QM_EOK;
    } else {
        qm_strcpy(out->path, "/");
        return QM_EOK;
    }
    if (path_len <= 0) {
        qm_strcpy(out->path, "/");
        return QM_EOK;
    }
    if (path_len >= (int)sizeof(out->path)) return -QM_EFULL;
    qm_memcpy(out->path, path, (uint32_t)path_len);
    out->path[path_len] = 0;
    return QM_EOK;
}

static int qm_http_client_header_get(const char *headers, int headers_len,
                                     const char *name, char *value, int value_len)
{
    const char *p;
    int name_len;
    if (!headers || headers_len <= 0 || !name || !value || value_len <= 0) return -QM_EINVAL;
    name_len = (int)qm_strlen(name);
    p = headers;
    while (p < headers + headers_len) {
        int remain = headers_len - (int)(p - headers);
        const char *line_end = (const char *)qm_memstr(p, (uint32_t)remain, "\r\n");
        const char *colon;
        int line_len;
        int vlen;
        if (!line_end) break;
        line_len = (int)(line_end - p);
        colon = qm_memchr(p, ':', line_len);
        if (colon && (int)(colon - p) == name_len &&
            qm_strncasecmp(p, name, name_len) == 0) {
            const char *v = colon + 1;
            const char *vend = line_end;
            while (v < line_end && (*v == ' ' || *v == '\t')) v++;
            while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t')) vend--;
            vlen = (int)(vend - v);
            if (vlen >= value_len) vlen = value_len - 1;
            qm_memcpy(value, v, vlen);
            value[vlen] = 0;
            return QM_EOK;
        }
        p = line_end + 2;
    }
    return -QM_ENOENT;
}

static int qm_http_client_header_get_auth_scheme(const char *headers, int headers_len,
                                                 const char *name, const char *scheme,
                                                 char *value, int value_len)
{
    const char *p;
    int name_len;

    if (!headers || headers_len <= 0 || !name || !scheme || !value || value_len <= 0) {
        return -QM_EINVAL;
    }
    name_len = (int)qm_strlen(name);
    p = headers;
    while (p < headers + headers_len) {
        int remain = headers_len - (int)(p - headers);
        const char *line_end = (const char *)qm_memstr(p, (uint32_t)remain, "\r\n");
        const char *colon;
        int line_len;
        int vlen;
        if (!line_end) break;
        line_len = (int)(line_end - p);
        colon = qm_memchr(p, ':', line_len);
        if (colon && (int)(colon - p) == name_len &&
            qm_strncasecmp(p, name, name_len) == 0) {
            const char *v = colon + 1;
            const char *vend = line_end;
            while (v < line_end && (*v == ' ' || *v == '\t')) v++;
            while (vend > v && (vend[-1] == ' ' || vend[-1] == '\t')) vend--;
            if (qm_http_client_find_auth_scheme(v, scheme)) {
                vlen = (int)(vend - v);
                if (vlen >= value_len) vlen = value_len - 1;
                qm_memcpy(value, v, vlen);
                value[vlen] = 0;
                return QM_EOK;
            }
        }
        p = line_end + 2;
    }
    return -QM_ENOENT;
}

static int qm_http_client_parse_content_length(const char *value, uint32_t *content_length)
{
    uint32_t parsed;
    int len;

    if (!value || !content_length) return -QM_EINVAL;
    len = (int)qm_strlen(value);
    if (qm_http_client_parse_decimal(value, len, &parsed) != QM_EOK) {
        return -QM_EFULL;
    }
    *content_length = parsed;
    return QM_EOK;
}

static int qm_http_client_parse_chunk_size(const char *line, uint32_t *chunk_len)
{
    const char *p;
    uint32_t value = 0;
    int digits = 0;

    if (!line || !chunk_len) return -QM_EINVAL;
    p = line;
    while (*p == ' ' || *p == '\t') p++;
    while (*p) {
        int digit;

        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (*p >= 'a' && *p <= 'f') {
            digit = *p - 'a' + 10;
        } else if (*p >= 'A' && *p <= 'F') {
            digit = *p - 'A' + 10;
        } else {
            break;
        }
        if (value > (UINT32_MAX - (uint32_t)digit) / 16U) {
            return -QM_EFULL;
        }
        value = value * 16U + (uint32_t)digit;
        digits++;
        p++;
    }
    if (digits == 0) return -QM_EINVAL;
    while (*p == ' ' || *p == '\t') p++;
    if (*p != 0 && *p != ';') return -QM_EINVAL;
    *chunk_len = value;
    return QM_EOK;
}

static int qm_http_client_read_chunk_trailer(qm_http_client_handle_t client)
{
    char *line;
    int ret;

    if (!client) return -QM_EINVAL;
    line = (char *)qm_malloc(CONFIG_QM_HTTP_CLIENT_LINE_MAX);
    if (!line) return -QM_ENOMEM;
    do {
        ret = qm_http_client_read_line(client, line,
                                       CONFIG_QM_HTTP_CLIENT_LINE_MAX, client->timeout_ms);
        if (ret < 0) {
            qm_free(line);
            return ret;
        }
    } while (ret > 0);
    qm_free(line);
    return QM_EOK;
}

int qm_http_client_tls_config_init(qm_http_tls_config_t *cfg,
                                   const char *ca_crt, int ca_crt_len,
                                   const char *client_crt, int client_crt_len,
                                   const char *client_key, int client_key_len)
{
    if (!cfg || ca_crt_len < 0 || client_crt_len < 0 || client_key_len < 0) return -QM_EINVAL;
    if ((client_crt && !client_key) || (!client_crt && client_key)) return -QM_EINVAL;
    qm_memset(cfg, 0, sizeof(*cfg));
    cfg->ca_crt = ca_crt;
    cfg->ca_crt_len = ca_crt_len;
    cfg->client_crt = client_crt;
    cfg->client_crt_len = client_crt_len;
    cfg->client_key = client_key;
    cfg->client_key_len = client_key_len;
    return QM_EOK;
}

static qm_bool interrupted(qm_http_client_handle_t client)
{
    return client->interrupted != NULL && client->interrupted(client->user);
}

static int transport_read(qm_http_client_handle_t client, char *buf, int len,
                          uint32_t timeout_ms)
{
    int ret;
    if (interrupted(client)) return -QM_EINTR;
    ret = client->transport.read(client->conn, buf, len, timeout_ms);
    return interrupted(client) ? -QM_EINTR : ret;
}

static int transport_write(qm_http_client_handle_t client, const char *buf, int len,
                           uint32_t timeout_ms)
{
    int ret;
    if (interrupted(client)) return -QM_EINTR;
    ret = client->transport.write(client->conn, buf, len, timeout_ms);
    return interrupted(client) ? -QM_EINTR : ret;
}

static int qm_http_client_read_exact(qm_http_client_handle_t client,
                                     char *buf, int len, uint32_t timeout_ms)
{
    int got = 0;

    while (got < len) {
        int ret = transport_read(client, buf + got, len - got, timeout_ms);
        if (ret <= 0) return ret == 0 ? -QM_ETIMEOUT : ret;
        got += ret;
    }

    return QM_EOK;
}

static int qm_http_client_write_all(qm_http_client_handle_t client,
                                    const char *buf, int len, uint32_t timeout_ms)
{
    int done = 0;

    while (done < len) {
        int ret = transport_write(client, buf + done, len - done, timeout_ms);
        if (ret <= 0) return ret == 0 ? -QM_ETIMEOUT : ret;
        done += ret;
    }

    return QM_EOK;
}

static int qm_http_client_read_header(qm_http_client_handle_t client, char *buf,
                                      int buf_size, int max_header, uint32_t timeout_ms,
                                      int *header_len)
{
    int used = 0;

    if (max_header > buf_size - 1) max_header = buf_size - 1;
    while (used < max_header) {
        const char *end;
        int ret = transport_read(client, buf + used, 1, timeout_ms);
        if (ret <= 0) return ret == 0 ? -QM_ETIMEOUT : ret;
        used += ret;
        buf[used] = 0;
        end = (const char *)qm_memstr(buf, (uint32_t)used, "\r\n\r\n");
        if (end) {
            *header_len = (int)(end - buf) + 4;
            return QM_EOK;
        }
    }

    return -QM_EFULL;
}

/**
 * @brief Read a CRLF-terminated line from the transport.
 *
 * The trailing `\r\n` is stripped and the output buffer is always terminated
 * with `\0` on success.
 *
 * @param[in] client HTTP client owning the transport
 * @param[out] line Destination line buffer
 * @param[in] line_size Buffer size in bytes
 * @param[in] timeout_ms Read timeout in milliseconds
 * @return Length of the stripped line on success
 * @return Negative error code on failure
 */
static int qm_http_client_read_line(qm_http_client_handle_t client, char *line,
                                    int line_size, uint32_t timeout_ms)
{
    int used = 0;

    while (used < line_size - 1) {
        int ret = transport_read(client, line + used, 1, timeout_ms);
        if (ret <= 0) return ret == 0 ? -QM_ETIMEOUT : ret;
        used += ret;
        if (used >= 2 && line[used - 2] == '\r' && line[used - 1] == '\n') {
            line[used - 2] = 0;
            return used - 2;
        }
    }

    return -QM_EFULL;
}

static int qm_http_client_parse_response_head(char *head, int header_len,
                                              qm_http_client_response_t *resp)
{
    char *line;
    const char *version_end;
    const char *status_start;
    const char *status_end;
    char value[32];
    uint32_t parsed_status;
    int status_code;

    if (!head || header_len < 12 || !resp) return -QM_EINVAL;
    line = (char *)qm_memstr(head, (uint32_t)header_len, "\r\n");
    if (!line) return -QM_EINVAL;
    version_end = qm_memchr(head, ' ', (int)(line - head));
    if (!version_end ||
        (int)(version_end - head) < 5 ||
        qm_strncasecmp(head, "HTTP/", 5) != 0) {
        return -QM_EINVAL;
    }

    status_start = version_end;
    while (status_start < line && (*status_start == ' ' || *status_start == '\t')) status_start++;
    status_end = status_start;
    while (status_end < line && *status_end >= '0' && *status_end <= '9') status_end++;
    if (qm_http_client_parse_decimal(status_start, (int)(status_end - status_start),
                                     &parsed_status) != QM_EOK ||
        parsed_status < 100 || parsed_status > 999) {
        return -QM_EINVAL;
    }
    status_code = (int)parsed_status;

    resp->status_code = status_code;
    resp->content_length_known = QM_FALSE;
    resp->content_length = 0;
    resp->chunked = QM_FALSE;
    resp->keep_alive = QM_FALSE;
    if (qm_http_client_header_get(line + 2, header_len - (int)(line + 2 - head),
                                  "Content-Length", value, sizeof(value)) == QM_EOK) {
        int ret = qm_http_client_parse_content_length(value, &resp->content_length);

        if (ret != QM_EOK) {
            return ret;
        }
        resp->content_length_known = QM_TRUE;
    }
    if (qm_http_client_header_get(line + 2, header_len - (int)(line + 2 - head),
                                  "Transfer-Encoding", value, sizeof(value)) == QM_EOK &&
        qm_http_client_token_has(value, "chunked")) {
        resp->chunked = QM_TRUE;
    }
    if ((int)(version_end - head) == (int)qm_strlen("HTTP/1.1") &&
        qm_strncasecmp(head, "HTTP/1.1", (int)qm_strlen("HTTP/1.1")) == 0) {
        resp->keep_alive = QM_TRUE;
    }
    if (qm_http_client_header_get(line + 2, header_len - (int)(line + 2 - head),
                                  "Connection", value, sizeof(value)) == QM_EOK) {
        if (qm_http_client_token_has(value, "close")) {
            resp->keep_alive = QM_FALSE;
        } else if (qm_http_client_token_has(value, "keep-alive")) {
            resp->keep_alive = QM_TRUE;
        }
    }

    return QM_EOK;
}

static qm_bool qm_http_client_response_has_body(qm_http_method_t method, int status_code)
{
    if (method == QM_HTTP_HEAD) return QM_FALSE;
    if (status_code >= 100 && status_code < 200) return QM_FALSE;
    if (status_code == 204 || status_code == 304) return QM_FALSE;
    return QM_TRUE;
}

static qm_bool qm_http_client_is_redirect_status(int status_code)
{
    return status_code == 301 || status_code == 302 || status_code == 303 ||
           status_code == 307 || status_code == 308;
}

#if CONFIG_QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986
static qm_bool qm_http_client_starts_with(const char *s, const char *prefix)
{
    uint32_t len;

    if (!s || !prefix) return QM_FALSE;
    len = qm_strlen(prefix);
    return qm_strncmp(s, prefix, len) == 0 ? QM_TRUE : QM_FALSE;
}

static void qm_http_client_remove_last_path_segment(char *path, int *len)
{
    int i;

    if (!path || !len || *len <= 0) return;
    i = *len - 1;
    while (i > 0 && path[i] != '/') {
        i--;
    }
    *len = i == 0 ? 0 : i;
    path[*len] = '\0';
}

static int qm_http_client_remove_dot_segments(const char *path, int path_len,
                                              char *out, int out_len)
{
    char input[QM_HTTP_CLIENT_URL_MAX];
    int input_len;
    int used = 0;

    if (!path || !out || out_len <= 0) return -QM_EINVAL;
    if (path_len <= 0) {
        if (out_len < 2) return -QM_EFULL;
        qm_strcpy(out, "/");
        return QM_EOK;
    }
    if (path_len >= (int)sizeof(input)) return -QM_EFULL;

    qm_memcpy(input, path, (uint32_t)path_len);
    input[path_len] = '\0';
    input_len = path_len;

    while (input_len > 0) {
        int segment_len;

        if (qm_http_client_starts_with(input, "../")) {
            qm_memmove(input, input + 3, (uint32_t)(input_len - 2));
            input_len -= 3;
        } else if (qm_http_client_starts_with(input, "./")) {
            qm_memmove(input, input + 2, (uint32_t)(input_len - 1));
            input_len -= 2;
        } else if (qm_http_client_starts_with(input, "/./")) {
            qm_memmove(input + 1, input + 3, (uint32_t)(input_len - 2));
            input_len -= 2;
        } else if (qm_strcmp(input, "/.") == 0) {
            input[1] = '\0';
            input_len = 1;
        } else if (qm_http_client_starts_with(input, "/../")) {
            qm_memmove(input + 1, input + 4, (uint32_t)(input_len - 3));
            input_len -= 3;
            qm_http_client_remove_last_path_segment(out, &used);
        } else if (qm_strcmp(input, "/..") == 0) {
            input[1] = '\0';
            input_len = 1;
            qm_http_client_remove_last_path_segment(out, &used);
        } else if (qm_strcmp(input, ".") == 0 || qm_strcmp(input, "..") == 0) {
            input[0] = '\0';
            input_len = 0;
        } else {
            if (input[0] == '/') {
                segment_len = 1;
                while (segment_len < input_len && input[segment_len] != '/') {
                    segment_len++;
                }
            } else {
                segment_len = 0;
                while (segment_len < input_len && input[segment_len] != '/') {
                    segment_len++;
                }
            }
            if (used + segment_len >= out_len) return -QM_EFULL;
            qm_memcpy(out + used, input, (uint32_t)segment_len);
            used += segment_len;
            out[used] = '\0';
            qm_memmove(input, input + segment_len, (uint32_t)(input_len - segment_len + 1));
            input_len -= segment_len;
        }
    }

    if (used == 0) {
        if (out_len < 2) return -QM_EFULL;
        qm_strcpy(out, "/");
    } else {
        out[used] = '\0';
    }
    return QM_EOK;
}

static const char *qm_http_client_find_ref_end(const char *ref)
{
    const char *fragment;

    fragment = qm_strchr(ref, '#');
    return fragment ? fragment : ref + qm_strlen(ref);
}

static const char *qm_http_client_find_query_before(const char *ref, const char *end)
{
    if (!ref || !end || end < ref) return NULL;
    return (const char *)qm_memchr(ref, '?', (uint32_t)(end - ref));
}

static int qm_http_client_copy_path_query(char *out, int out_len,
                                          const char *path, int path_len,
                                          const char *query, int query_len)
{
    if (!out || !path || path_len <= 0 || out_len <= 0) return -QM_EINVAL;
    if (path_len + query_len >= out_len) return -QM_EFULL;
    qm_memcpy(out, path, (uint32_t)path_len);
    if (query_len > 0) {
        qm_memcpy(out + path_len, query, (uint32_t)query_len);
    }
    out[path_len + query_len] = '\0';
    return QM_EOK;
}

static int qm_http_client_resolve_relative_path(const qm_http_client_url_t *base,
                                                const char *location,
                                                char *out,
                                                int out_len)
{
    const char *ref_end;
    const char *query;
    const char *path_end;
    const char *base_query;
    const char *base_slash;
    char merged[QM_HTTP_CLIENT_URL_MAX];
    char normalized[QM_HTTP_CLIENT_PATH_MAX];
    int base_path_len;
    int ref_path_len;
    int prefix_len;
    int query_len;
    int ret;

    if (!base || !location || !out || out_len <= 0) return -QM_EINVAL;

    ref_end = qm_http_client_find_ref_end(location);
    query = qm_http_client_find_query_before(location, ref_end);
    base_query = qm_strchr(base->path, '?');
    base_path_len = base_query ? (int)(base_query - base->path) : (int)qm_strlen(base->path);

    if (ref_end == location) {
        return qm_http_client_copy_path_query(out, out_len, base->path,
                                             (int)qm_strlen(base->path), NULL, 0);
    }
    if (location[0] == '#') {
        return qm_http_client_copy_path_query(out, out_len, base->path,
                                             (int)qm_strlen(base->path), NULL, 0);
    }
    if (location[0] == '?') {
        return qm_http_client_copy_path_query(out, out_len, base->path, base_path_len,
                                             location, (int)(ref_end - location));
    }

    path_end = query ? query : ref_end;
    ref_path_len = (int)(path_end - location);
    query_len = query ? (int)(ref_end - query) : 0;

    if (location[0] == '/') {
        ret = qm_http_client_remove_dot_segments(location, ref_path_len,
                                                 normalized, sizeof(normalized));
        if (ret != QM_EOK) return ret;
        return qm_http_client_copy_path_query(out, out_len, normalized,
                                             (int)qm_strlen(normalized), query, query_len);
    }

    base_slash = NULL;
    if (base_path_len > 0) {
        int i;
        for (i = base_path_len - 1; i >= 0; i--) {
            if (base->path[i] == '/') {
                base_slash = base->path + i;
                break;
            }
        }
    }
    prefix_len = base_slash ? (int)(base_slash - base->path) + 1 : 1;
    if (prefix_len + ref_path_len >= (int)sizeof(merged)) return -QM_EFULL;
    qm_memcpy(merged, base->path, (uint32_t)prefix_len);
    qm_memcpy(merged + prefix_len, location, (uint32_t)ref_path_len);
    merged[prefix_len + ref_path_len] = '\0';

    ret = qm_http_client_remove_dot_segments(merged, prefix_len + ref_path_len,
                                             normalized, sizeof(normalized));
    if (ret != QM_EOK) return ret;
    return qm_http_client_copy_path_query(out, out_len, normalized,
                                         (int)qm_strlen(normalized), query, query_len);
}

static int qm_http_client_normalize_parsed_path(const qm_http_client_url_t *url,
                                                char *out,
                                                int out_len)
{
    const char *query;
    char normalized[QM_HTTP_CLIENT_PATH_MAX];
    int path_len;
    int query_len;
    int ret;

    if (!url || !out || out_len <= 0) return -QM_EINVAL;

    query = qm_strchr(url->path, '?');
    path_len = query ? (int)(query - url->path) : (int)qm_strlen(url->path);
    query_len = query ? (int)qm_strlen(query) : 0;

    ret = qm_http_client_remove_dot_segments(url->path, path_len,
                                             normalized, sizeof(normalized));
    if (ret != QM_EOK) return ret;
    return qm_http_client_copy_path_query(out, out_len, normalized,
                                         (int)qm_strlen(normalized), query, query_len);
}

static int qm_http_client_build_origin(const qm_http_client_url_t *current,
                                       char *url,
                                       int url_len)
{
    int default_port;
    int used;
    int ret;
    qm_bool bracket_host;

    if (!current || !url || url_len <= 0) return -QM_EINVAL;

    default_port = current->secure ? 443 : 80;
    used = qm_snprintf(url, url_len, "%s://", current->secure ? "https" : "http");
    if (used < 0 || used >= url_len) return -QM_EFULL;

    bracket_host = qm_strchr(current->host, ':') ? QM_TRUE : QM_FALSE;
    if (bracket_host) {
        ret = qm_snprintf(url + used, url_len - used, "[%s]", current->host);
    } else {
        ret = qm_snprintf(url + used, url_len - used, "%s", current->host);
    }
    if (ret < 0 || used + ret >= url_len) return -QM_EFULL;
    used += ret;

    if (current->port != default_port) {
        ret = qm_snprintf(url + used, url_len - used, ":%u", current->port);
        if (ret < 0 || used + ret >= url_len) return -QM_EFULL;
        used += ret;
    }

    return used;
}

static int qm_http_client_build_url_from_parsed(const qm_http_client_url_t *target,
                                                char *url,
                                                int url_len)
{
    char path[QM_HTTP_CLIENT_PATH_MAX];
    int used;
    int ret;

    ret = qm_http_client_normalize_parsed_path(target, path, sizeof(path));
    if (ret != QM_EOK) return ret;

    used = qm_http_client_build_origin(target, url, url_len);
    if (used < 0) return used;

    ret = qm_snprintf(url + used, url_len - used, "%s", path);
    if (ret < 0 || used + ret >= url_len) return -QM_EFULL;
    return used + ret;
}
#endif

static int qm_http_client_set_redirect_url(qm_http_client_handle_t client)
{
    qm_http_client_url_t current;
#if CONFIG_QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986
    qm_http_client_url_t target;
#endif
    const char *location;
#if CONFIG_QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986
    char path[QM_HTTP_CLIENT_PATH_MAX];
#endif
    char *url;
    int ret;
#if !CONFIG_QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986
    int default_port;
#endif
    int used;

    if (!client || !client->location) return -QM_EINVAL;
    location = client->location;
#if !CONFIG_QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986
    if (qm_strstr(location, "://")) {
        return qm_http_client_set_url(client, location);
    }
#endif

    ret = qm_http_client_url_parse(client->url, &current);
    if (ret != QM_EOK) return ret;

    url = (char *)qm_malloc(QM_HTTP_CLIENT_URL_MAX);
    if (!url) return -QM_ENOMEM;

#if CONFIG_QM_HTTP_CLIENT_REDIRECT_RESOLVE_RFC3986
    if (qm_strstr(location, "://")) {
        ret = qm_http_client_url_parse(location, &target);
        if (ret != QM_EOK) goto fail;
        used = qm_http_client_build_url_from_parsed(&target, url, QM_HTTP_CLIENT_URL_MAX);
        if (used < 0) {
            ret = used;
            goto fail;
        }
    } else if (location[0] == '/' && location[1] == '/') {
        used = qm_snprintf(url, QM_HTTP_CLIENT_URL_MAX, "%s:%s",
                           current.secure ? "https" : "http", location);
        if (used < 0 || used >= QM_HTTP_CLIENT_URL_MAX) {
            ret = -QM_EFULL;
            goto fail;
        }
        ret = qm_http_client_url_parse(url, &target);
        if (ret != QM_EOK) goto fail;
        used = qm_http_client_build_url_from_parsed(&target, url, QM_HTTP_CLIENT_URL_MAX);
        if (used < 0) {
            ret = used;
            goto fail;
        }
    } else {
        ret = qm_http_client_resolve_relative_path(&current, location, path, sizeof(path));
        if (ret != QM_EOK) goto fail;
        used = qm_http_client_build_origin(&current, url, QM_HTTP_CLIENT_URL_MAX);
        if (used < 0) {
            ret = used;
            goto fail;
        }
        ret = qm_snprintf(url + used, QM_HTTP_CLIENT_URL_MAX - used, "%s", path);
        if (ret < 0 || used + ret >= QM_HTTP_CLIENT_URL_MAX) {
            ret = -QM_EFULL;
            goto fail;
        }
        used += ret;
    }
#else
    default_port = current.secure ? 443 : 80;
    used = qm_snprintf(url, QM_HTTP_CLIENT_URL_MAX, "%s://%s",
                       current.secure ? "https" : "http", current.host);
    if (used < 0 || used >= QM_HTTP_CLIENT_URL_MAX) {
        ret = -QM_EFULL;
        goto fail;
    }
    if (current.port != default_port) {
        ret = qm_snprintf(url + used, QM_HTTP_CLIENT_URL_MAX - used,
                          ":%u", current.port);
        if (ret < 0 || used + ret >= QM_HTTP_CLIENT_URL_MAX) {
            ret = -QM_EFULL;
            goto fail;
        }
        used += ret;
    }
    if (location[0] == '/') {
        ret = qm_snprintf(url + used, QM_HTTP_CLIENT_URL_MAX - used, "%s", location);
    } else {
        const char *slash = qm_strrchr(current.path, '/');
        int base_len = slash ? (int)(slash - current.path) + 1 : 1;
        ret = qm_snprintf(url + used, QM_HTTP_CLIENT_URL_MAX - used,
                          "%.*s%s", base_len, current.path, location);
    }
    if (ret < 0 || used + ret >= QM_HTTP_CLIENT_URL_MAX) {
        ret = -QM_EFULL;
        goto fail;
    }
    used += ret;
#endif
    if (used < 0 || used >= QM_HTTP_CLIENT_URL_MAX) {
        ret = -QM_EFULL;
        goto fail;
    }

    ret = qm_http_client_set_url(client, url);
    qm_free(url);
    return ret;

fail:
    qm_free(url);
    return ret;
}

static int qm_http_client_endpoint_same(const qm_http_client_url_t *a, const qm_http_client_url_t *b)
{
    if (!a || !b) return 0;
    return a->secure == b->secure &&
           a->port == b->port &&
           qm_strncasecmp(a->host, b->host, (int)sizeof(a->host)) == 0;
}

static void qm_http_client_reset_response_state(qm_http_client_handle_t client)
{
    if (!client) return;
    qm_memset(&client->response, 0, sizeof(client->response));
    client->response.content_length_known = QM_FALSE;
    client->response.content_length = 0;

    QM_HTTP_CLIENT_LOGD(LOG_TAG, QM_HTTP_CLIENT_LOG_LIFECYCLE,
                        "request state reset timeout=%lu rx_buf=%lu keep_alive=%d auth=%d",
                        (unsigned long)client->timeout_ms,
                        (unsigned long)client->rx_buf_size,
                        client->keep_alive,
                        client->auth_type);
    client->body_mode = QM_HTTP_BODY_NONE;
    client->body_read = 0;
    client->body_left = 0;
    client->chunk_left = 0;
    client->headers_fetched = QM_FALSE;
    client->complete = QM_FALSE;
    client->write_len = 0;
    client->written = 0;
    client->finish_event_sent = QM_FALSE;
    if (client->location) {
        qm_free(client->location);
        client->location = NULL;
    }
}

qm_http_client_handle_t qm_http_client_init(const qm_http_client_config_t *config)
{
    qm_http_client_handle_t client;
    int i;

    client = (qm_http_client_handle_t)qm_calloc(1, sizeof(*client));
    if (!client) return NULL;
    client->method = config ? config->method : QM_HTTP_GET;
    client->timeout_ms = config && config->timeout_ms ? config->timeout_ms : CONFIG_QM_HTTP_CLIENT_TIMEOUT_MS;
    client->max_header_len = config && config->max_header_len ? config->max_header_len : CONFIG_QM_HTTP_CLIENT_HEADER_MAX;
    client->max_body_len = config && config->max_body_len ? config->max_body_len : CONFIG_QM_HTTP_CLIENT_BODY_MAX;
    client->rx_buf_size = config && config->rx_buf_size ? config->rx_buf_size : CONFIG_QM_HTTP_CLIENT_RX_BUF_SIZE;
    if (client->rx_buf_size > (uint32_t)INT_MAX) goto fail;
    client->disable_auto_redirect = config ? config->disable_auto_redirect : QM_FALSE;
    client->max_redirection_count = config && config->max_redirection_count > 0 ?
                                    config->max_redirection_count : QM_HTTP_CLIENT_DEFAULT_REDIRECTS;
    client->keep_alive = config ? config->keep_alive : QM_FALSE;
    client->auth_type = config ? config->auth_type : QM_HTTP_AUTH_TYPE_NONE;
    client->tls = config ? config->tls : NULL;
    client->on_event = config ? config->on_event : NULL;
    client->interrupted = config ? config->interrupted : NULL;
    client->user = config ? config->user : NULL;
    client->response.content_length_known = QM_FALSE;
    client->response.content_length = 0;

    if (config && config->url && qm_http_client_set_url(client, config->url) != QM_EOK) goto fail;
    if (config && config->username && qm_http_client_set_username(client, config->username) != QM_EOK) goto fail;
    if (config && config->password && qm_http_client_set_password(client, config->password) != QM_EOK) goto fail;
    if (config && config->content_type) {
        client->content_type = qm_strdup(config->content_type);
        if (!client->content_type) goto fail;
    }
    if (config && config->user_agent) {
        client->user_agent = qm_strdup(config->user_agent);
        if (!client->user_agent) goto fail;
    }
    if (config && config->headers && config->header_count > 0) {
        for (i = 0; i < config->header_count; i++) {
            if (qm_http_client_set_header(client, config->headers[i].name,
                                          config->headers[i].value) != QM_EOK) {
                goto fail;
            }
        }
    }

    return client;

fail:
    qm_http_client_cleanup(client);
    return NULL;
}

int qm_http_client_set_url(qm_http_client_handle_t client, const char *url)
{
    qm_http_client_url_t parsed_url;
    char *copy;
    int ret;

    if (!client || !url) return -QM_EINVAL;
    ret = qm_http_client_url_parse(url, &parsed_url);
    if (ret != QM_EOK) return ret;
    copy = qm_strdup(url);
    if (!copy) return -QM_ENOMEM;
    if (client->url) qm_free(client->url);
    client->url = copy;
    qm_http_client_digest_clear(&client->digest);
    return QM_EOK;
}

int qm_http_client_set_method(qm_http_client_handle_t client, qm_http_method_t method)
{
    if (!client) return -QM_EINVAL;
    if (method < QM_HTTP_GET || method >= QM_HTTP_ANY) return -QM_EINVAL;
    client->method = method;
    return QM_EOK;
}

int qm_http_client_set_username(qm_http_client_handle_t client, const char *username)
{
    char *copy = NULL;

    if (!client) return -QM_EINVAL;
    if (username) {
        copy = qm_strdup(username);
        if (!copy) return -QM_ENOMEM;
    }
    if (client->username) qm_free(client->username);
    client->username = copy;
    return QM_EOK;
}

int qm_http_client_set_password(qm_http_client_handle_t client, const char *password)
{
    char *copy = NULL;

    if (!client) return -QM_EINVAL;
    if (password) {
        copy = qm_strdup(password);
        if (!copy) return -QM_ENOMEM;
    }
    if (client->password) qm_free(client->password);
    client->password = copy;
    return QM_EOK;
}

int qm_http_client_set_auth_type(qm_http_client_handle_t client, qm_http_client_auth_type_t auth_type)
{
    if (!client) return -QM_EINVAL;
    if (auth_type != QM_HTTP_AUTH_TYPE_NONE &&
        auth_type != QM_HTTP_AUTH_TYPE_BASIC &&
        auth_type != QM_HTTP_AUTH_TYPE_DIGEST) {
        return -QM_EINVAL;
    }
    if (client->auth_type != auth_type) {
        qm_http_client_digest_clear(&client->digest);
    }
    client->auth_type = auth_type;
    return QM_EOK;
}

int qm_http_client_set_header(qm_http_client_handle_t client, const char *name, const char *value)
{
    qm_http_header_t *headers;
    char *name_copy;
    char *value_copy;
    int index;

    if (!client || !name || !value) return -QM_EINVAL;
    if (qm_http_client_is_reserved_header(name)) return -QM_EINVAL;
    index = qm_http_client_header_find(client, name);
    name_copy = qm_strdup(name);
    value_copy = qm_strdup(value);
    if (!name_copy || !value_copy) {
        if (name_copy) qm_free(name_copy);
        if (value_copy) qm_free(value_copy);
        return -QM_ENOMEM;
    }

    if (index >= 0) {
        qm_http_client_header_free(&client->headers[index]);
        client->headers[index].name = name_copy;
        client->headers[index].value = value_copy;
        return QM_EOK;
    }

    headers = (qm_http_header_t *)qm_realloc(client->headers,
                                             (unsigned int)(client->header_count + 1) * sizeof(*headers));
    if (!headers) {
        qm_free(name_copy);
        qm_free(value_copy);
        return -QM_ENOMEM;
    }
    client->headers = headers;
    client->headers[client->header_count].name = name_copy;
    client->headers[client->header_count].value = value_copy;
    client->header_count++;
    return QM_EOK;
}

int qm_http_client_delete_header(qm_http_client_handle_t client, const char *name)
{
    qm_http_header_t *headers;
    int index;
    int i;
    int old_count;

    if (!client || !name) return -QM_EINVAL;
    if (qm_http_client_is_reserved_header(name)) return -QM_EINVAL;
    index = qm_http_client_header_find(client, name);
    if (index < 0) return -QM_ENOENT;
    old_count = client->header_count;
    qm_http_client_header_free(&client->headers[index]);
    for (i = index; i < old_count - 1; i++) {
        client->headers[i] = client->headers[i + 1];
    }
    qm_memset(&client->headers[old_count - 1], 0, sizeof(client->headers[old_count - 1]));
    client->header_count = old_count - 1;
    if (client->header_count == 0) {
        qm_free(client->headers);
        client->headers = NULL;
    } else {
        headers = (qm_http_header_t *)qm_realloc(client->headers,
                                                 (unsigned int)client->header_count * sizeof(*headers));
        if (headers) {
            client->headers = headers;
        }
    }
    return QM_EOK;
}

int qm_http_client_close(qm_http_client_handle_t client)
{
    int ret = QM_EOK;

    if (!client) return -QM_EINVAL;
    QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_LIFECYCLE,
                        "close connection active=%d status=%d",
                        client->conn != NULL, client->response.status_code);
    if (client->conn) {
        ret = client->transport.close(client->conn);
        client->conn = NULL;
        qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_DISCONNECTED,
                                      NULL, 0, NULL, NULL, QM_EOK, NULL);
    }
    client->conn_url_valid = QM_FALSE;
    client->response.keep_alive = QM_FALSE;
    client->complete = QM_TRUE;
    return ret;
}

static int qm_http_client_append_header(char *head, uint32_t max_header_len, int used,
                                        const char *name, const char *value)
{
    int ret;

    if (!head || !name || !value || used < 0 || (uint32_t)used >= max_header_len) return -QM_EINVAL;
    ret = qm_snprintf(head + used, max_header_len - (uint32_t)used, "%s: %s\r\n", name, value);
    if (ret < 0 || (uint32_t)(used + ret) >= max_header_len) return -QM_EFULL;
    return used + ret;
}

static char *qm_http_client_basic_auth_value(const char *username, const char *password)
{
    char *plain;
    char *value;
    uint8_t *encoded;
    uint32_t encoded_len;
    int plain_len;
    int value_len;

    if (!username || !password) return NULL;
    plain_len = (int)qm_strlen(username) + 1 + (int)qm_strlen(password);
    plain = (char *)qm_malloc((unsigned int)plain_len + 1);
    if (!plain) return NULL;
    qm_snprintf(plain, (unsigned int)plain_len + 1, "%s:%s", username, password);

    encoded_len = 4 * (((uint32_t)plain_len + 2) / 3);
    encoded = (uint8_t *)qm_malloc(encoded_len + 1);
    if (!encoded) {
        qm_free(plain);
        return NULL;
    }
    if (qm_utils_base64encode((const uint8_t *)plain, (uint32_t)plain_len,
                              encoded_len, encoded, &encoded_len) != 0) {
        qm_free(plain);
        qm_free(encoded);
        return NULL;
    }
    encoded[encoded_len] = 0;
    qm_free(plain);

    value_len = (int)qm_strlen("Basic ") + (int)encoded_len;
    value = (char *)qm_malloc((unsigned int)value_len + 1);
    if (!value) {
        qm_free(encoded);
        return NULL;
    }
    qm_snprintf(value, (unsigned int)value_len + 1, "Basic %s", encoded);
    qm_free(encoded);
    return value;
}

int qm_http_client_open(qm_http_client_handle_t client, int write_len)
{
    qm_http_client_url_t url;
    char *head;
    int ret;
    int i;
    qm_bool has_host;
    qm_bool has_user_agent;
    qm_bool has_connection;
    qm_bool has_content_type;
    qm_bool has_content_length;
    qm_bool has_authorization;
    qm_bool digest_auth_used = QM_FALSE;
    uint32_t digest_next_nc = 0;
    char *auth_value = NULL;
    qm_bool connected_now = QM_FALSE;

    if (!client || !client->url || write_len < 0) {
        return qm_http_client_report_error(client, -QM_EINVAL);
    }
    if (interrupted(client)) return qm_http_client_report_error(client, -QM_EINTR);
    QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_CONTROL,
                        "open method=%s write_len=%d",
                        qm_http_client_method_str(client->method), write_len);
    if (client->max_header_len < 128) return qm_http_client_report_error(client, -QM_EINVAL);
    if (!client->complete && client->headers_fetched) qm_http_client_close(client);
    ret = qm_http_client_url_parse(client->url, &url);
    if (ret != QM_EOK) return qm_http_client_report_error(client, ret);

    if (client->conn && (!client->keep_alive || !client->response.keep_alive ||
                         !client->conn_url_valid ||
                         !qm_http_client_endpoint_same(&client->conn_url, &url))) {
        qm_http_client_close(client);
    }
    if (!client->conn) {
        if (url.secure) qm_http_client_transport_tls_init(&client->transport, client->tls);
        else qm_http_client_transport_tcp_init(&client->transport);
        client->conn = client->transport.connect(url.host, url.port, client->transport.config);
        if (interrupted(client)) {
            qm_http_client_close(client);
            return qm_http_client_report_error(client, -QM_EINTR);
        }
        if (!client->conn) return qm_http_client_report_error(client, -QM_EIO);
        client->conn_url = url;
        client->conn_url_valid = QM_TRUE;
        connected_now = QM_TRUE;
        QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_LIFECYCLE,
                            "transport connected host=%s port=%u secure=%d",
                            url.host, url.port, url.secure);
    }

    qm_http_client_reset_response_state(client);
    client->write_len = write_len;

    if (connected_now) {
        ret = qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_CONNECTED,
                                            NULL, 0, NULL, NULL, QM_EOK, NULL);
        if (ret != QM_EOK) {
            qm_http_client_close(client);
            return qm_http_client_report_error(client, qm_http_client_event_ret(ret));
        }
    }

    head = (char *)qm_malloc(client->max_header_len + 1);
    if (!head) return qm_http_client_report_error(client, -QM_ENOMEM);

    ret = qm_snprintf(head, client->max_header_len, "%s %s HTTP/1.1\r\n",
                   qm_http_client_method_str(client->method), url.path);
    if (ret < 0 || (uint32_t)ret >= client->max_header_len) goto header_full;

    has_host = qm_http_client_header_find(client, "Host") >= 0;
    has_user_agent = qm_http_client_header_find(client, "User-Agent") >= 0;
    has_connection = qm_http_client_header_find(client, "Connection") >= 0;
    has_content_type = qm_http_client_header_find(client, "Content-Type") >= 0;
    has_content_length = qm_http_client_header_find(client, "Content-Length") >= 0;
    has_authorization = qm_http_client_header_find(client, "Authorization") >= 0;

    if (!has_host) {
        ret = qm_http_client_append_header(head, client->max_header_len, ret, "Host", url.host);
        if (ret < 0) goto header_full;
    }
    if (!has_user_agent) {
        ret = qm_http_client_append_header(head, client->max_header_len, ret, "User-Agent",
                                           client->user_agent ? client->user_agent : "qumi-http-client/1.0");
        if (ret < 0) goto header_full;
    }
    if (!has_connection) {
        ret = qm_http_client_append_header(head, client->max_header_len, ret, "Connection",
                                           client->keep_alive ? "keep-alive" : "close");
        if (ret < 0) goto header_full;
    }
    if (!has_content_type && client->content_type && write_len > 0) {
        ret = qm_http_client_append_header(head, client->max_header_len, ret, "Content-Type", client->content_type);
        if (ret < 0) goto header_full;
    }
    if (!has_content_length) {
        char len_buf[16];
        qm_snprintf(len_buf, sizeof(len_buf), "%u", (unsigned int)write_len);
        ret = qm_http_client_append_header(head, client->max_header_len, ret, "Content-Length", len_buf);
        if (ret < 0) goto header_full;
    }
    if (!has_authorization && client->auth_type == QM_HTTP_AUTH_TYPE_BASIC) {
        if (!client->username || !client->password) {
            qm_free(head);
            qm_http_client_close(client);
            return qm_http_client_report_error(client, -QM_EINVAL);
        }
        auth_value = qm_http_client_basic_auth_value(client->username, client->password);
        if (!auth_value) {
            qm_free(head);
            qm_http_client_close(client);
            return qm_http_client_report_error(client, -QM_ENOMEM);
        }
        ret = qm_http_client_append_header(head, client->max_header_len, ret, "Authorization", auth_value);
        qm_free(auth_value);
        auth_value = NULL;
        if (ret < 0) goto header_full;
    } else if (!has_authorization && client->auth_type == QM_HTTP_AUTH_TYPE_DIGEST &&
               client->digest.valid) {
        if (client->digest_body_len > 0 && !client->digest_body) {
            const char *qop = qm_http_client_digest_qop(&client->digest);

            if (qop && qm_strcasecmp(qop, "auth-int") == 0) {
                qm_free(head);
                qm_http_client_close(client);
                return qm_http_client_report_error(client, -QM_ENOSYS);
            }
        }
        const char *body = client->digest_body;
        int body_len = client->digest_body ? client->digest_body_len : write_len;
        ret = qm_http_client_digest_auth_value(client, qm_http_client_method_str(client->method),
                                               url.path, body, body_len,
                                               &digest_next_nc, &auth_value);
        if (ret != QM_EOK) {
            qm_free(head);
            qm_http_client_close(client);
            return qm_http_client_report_error(client, ret);
        }
        ret = qm_http_client_append_header(head, client->max_header_len, ret, "Authorization", auth_value);
        qm_free(auth_value);
        auth_value = NULL;
        digest_auth_used = QM_TRUE;
        if (ret < 0) goto header_full;
    }
    for (i = 0; i < client->header_count; i++) {
        ret = qm_http_client_append_header(head, client->max_header_len, ret,
                                           client->headers[i].name, client->headers[i].value);
        if (ret < 0) goto header_full;
    }
    ret += qm_snprintf(head + ret, client->max_header_len - (uint32_t)ret, "\r\n");
    if ((uint32_t)ret >= client->max_header_len) goto header_full;
    i = qm_http_client_write_all(client, head, ret, client->timeout_ms);
    if (i != QM_EOK) {
        qm_free(head);
        qm_http_client_close(client);
        return qm_http_client_report_error(client, i);
    }
    QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_UPLINK,
                        "request headers sent method=%s bytes=%d",
                        qm_http_client_method_str(client->method), ret);
    if (digest_auth_used) {
        client->digest.nc = digest_next_nc;
    }
    qm_free(head);
    return QM_EOK;

header_full:
    qm_free(head);
    qm_http_client_close(client);
    return qm_http_client_report_error(client, -QM_EFULL);
}

int qm_http_client_write(qm_http_client_handle_t client, const char *buffer, int len)
{
    int ret;

    if (!client || !client->conn || !buffer || len < 0) {
        return qm_http_client_report_error(client, -QM_EINVAL);
    }
    if (client->written + len > client->write_len) return qm_http_client_report_error(client, -QM_EFULL);
    ret = qm_http_client_write_all(client, buffer, len, client->timeout_ms);
    if (ret != QM_EOK) {
        qm_http_client_close(client);
        return qm_http_client_report_error(client, ret);
    }
    client->written += len;
    QM_HTTP_CLIENT_LOGD(LOG_TAG, QM_HTTP_CLIENT_LOG_UPLINK,
                        "request body sent len=%d total=%d/%d", len, client->written, client->write_len);
    return len;
}

int qm_http_client_fetch_headers(qm_http_client_handle_t client)
{
    char *head;
    char *location;
    int header_len = 0;
    int ret;

    if (!client || !client->conn) return qm_http_client_report_error(client, -QM_EINVAL);
    if (client->headers_fetched) return QM_EOK;
    if (client->written != client->write_len) return qm_http_client_report_error(client, -QM_EINVAL);

    head = (char *)qm_malloc(client->max_header_len + 1);
    if (!head) return qm_http_client_report_error(client, -QM_ENOMEM);
    ret = qm_http_client_read_header(client, head,
                                     client->max_header_len + 1, client->max_header_len,
                                     client->timeout_ms, &header_len);
    if (ret != QM_EOK) {
        qm_free(head);
        qm_http_client_close(client);
        return qm_http_client_report_error(client, ret);
    }
    ret = qm_http_client_parse_response_head(head, header_len, &client->response);
    if (ret == QM_EOK) {
        ret = qm_http_client_dispatch_headers(client, head, header_len);
    }
    location = NULL;
    if (ret == QM_EOK && client->auth_type == QM_HTTP_AUTH_TYPE_DIGEST &&
        client->response.status_code == 401) {
        char *auth_header = (char *)qm_malloc(client->max_header_len + 1);
        int auth_ret;

        if (!auth_header) {
            qm_free(head);
            qm_http_client_close(client);
            return qm_http_client_report_error(client, -QM_ENOMEM);
        }
        auth_ret = qm_http_client_header_get_auth_scheme(head, header_len,
                                                         "WWW-Authenticate", "Digest",
                                                         auth_header,
                                                         (int)client->max_header_len + 1);
        if (auth_ret == QM_EOK) {
            auth_ret = qm_http_client_parse_digest_challenge(client, auth_header);
            if (auth_ret == -QM_ENOMEM) {
                qm_free(auth_header);
                qm_free(head);
                qm_http_client_close(client);
                return qm_http_client_report_error(client, -QM_ENOMEM);
            }
        } else {
            qm_http_client_digest_clear(&client->digest);
        }
        qm_free(auth_header);
    }
    if (ret == QM_EOK && qm_http_client_is_redirect_status(client->response.status_code)) {
        location = (char *)qm_malloc(client->max_header_len + 1);
        if (!location) {
            qm_free(head);
            qm_http_client_close(client);
            return qm_http_client_report_error(client, -QM_ENOMEM);
        }
        if (qm_http_client_header_get(head, header_len, "Location",
                                      location, (int)client->max_header_len + 1) == QM_EOK) {
            client->location = location;
            client->response.location = client->location;
        } else {
            qm_free(location);
        }
    }
    qm_free(head);
    if (ret != QM_EOK) {
        qm_http_client_close(client);
        return qm_http_client_report_error(client, ret);
    }
    QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_DOWNLINK,
                        "response headers status=%d header_len=%d content_length=%lu chunked=%d",
                        client->response.status_code, header_len,
                        (unsigned long)client->response.content_length,
                        client->response.chunked);
    if (client->location) {
        ret = qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_REDIRECT,
                                            NULL, 0, NULL, NULL, QM_EOK, client->location);
        if (ret != QM_EOK) {
            qm_http_client_close(client);
            return qm_http_client_report_error(client, qm_http_client_event_ret(ret));
        }
    }

    client->headers_fetched = QM_TRUE;
    if (!qm_http_client_response_has_body(client->method, client->response.status_code)) {
        client->body_mode = QM_HTTP_BODY_NONE;
        client->complete = QM_TRUE;
    } else if (client->response.chunked) {
        client->body_mode = QM_HTTP_BODY_CHUNKED;
    } else if (client->response.content_length_known) {
        if (client->response.content_length > client->max_body_len) {
            qm_http_client_close(client);
            return -QM_EFULL;
        }
        client->body_mode = QM_HTTP_BODY_FIXED;
        client->body_left = client->response.content_length;
        client->complete = client->body_left == 0 ? QM_TRUE : QM_FALSE;
    } else {
        client->body_mode = QM_HTTP_BODY_UNTIL_CLOSE;
        client->response.keep_alive = QM_FALSE;
    }

    if (client->complete) {
        ret = qm_http_client_dispatch_finish(client);
        if (ret != QM_EOK) {
            qm_http_client_close(client);
            return ret;
        }
    }

    return QM_EOK;
}

static int qm_http_client_read_chunked_stream(qm_http_client_handle_t client, char *buffer, int len)
{
    int got = 0;

    while (got < len && !client->complete) {
        int ret;
        if (client->chunk_left == 0) {
            char *line = (char *)qm_malloc(CONFIG_QM_HTTP_CLIENT_LINE_MAX);
            uint32_t chunk_len;
            if (!line) return got > 0 ? got : -QM_ENOMEM;
            ret = qm_http_client_read_line(client, line,
                                           CONFIG_QM_HTTP_CLIENT_LINE_MAX, client->timeout_ms);
            if (ret < 0) {
                qm_free(line);
                return got > 0 ? got : ret;
            }
            ret = qm_http_client_parse_chunk_size(line, &chunk_len);
            if (ret != QM_EOK) {
                qm_free(line);
                return got > 0 ? got : ret;
            }
            qm_free(line);
            if (chunk_len == 0) {
                ret = qm_http_client_read_chunk_trailer(client);
                if (ret != QM_EOK) return got > 0 ? got : ret;
                client->complete = QM_TRUE;
                break;
            }
            client->chunk_left = chunk_len;
        }

        ret = client->chunk_left > (uint32_t)(len - got) ? (len - got) : (int)client->chunk_left;
        ret = transport_read(client, buffer + got, ret, client->timeout_ms);
        if (ret <= 0) return got > 0 ? got : (ret == 0 ? -QM_ETIMEOUT : ret);
        client->chunk_left -= ret;
        client->body_read += (uint32_t)ret;
        got += ret;

        if (client->chunk_left == 0) {
            char crlf[2];
            ret = qm_http_client_read_exact(client, crlf, 2, client->timeout_ms);
            if (ret != QM_EOK) return got > 0 ? got : ret;
            if (crlf[0] != '\r' || crlf[1] != '\n') return got > 0 ? got : -QM_EINVAL;
        }
    }

    return got;
}

/**
 * @brief Consume the remaining response body for the current request.
 *
 * This helper is used when the caller wants to reuse a keep-alive connection
 * or needs to move the connection back to a clean state before following a
 * redirect or retrying authentication.
 *
 * @param[in] client HTTP client handle
 * @param[in] dispatch_events Whether body data should still trigger data events
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
static int qm_http_client_drain_response_body_internal(qm_http_client_handle_t client,
                                                       qm_bool dispatch_events)
{
    char *buffer;
    int ret = QM_EOK;

    if (!client) return qm_http_client_report_error(client, -QM_EINVAL);
    if (!client->headers_fetched) {
        ret = qm_http_client_fetch_headers(client);
        if (ret < 0) return ret;
    }
    if (client->complete) {
        if (dispatch_events) {
            ret = qm_http_client_dispatch_finish(client);
            if (ret != QM_EOK) {
                qm_http_client_close(client);
                return ret;
            }
        }
        return QM_EOK;
    }

    if (dispatch_events) {
        buffer = (char *)qm_malloc(client->rx_buf_size);
        if (!buffer) return qm_http_client_report_error(client, -QM_ENOMEM);
        for (;;) {
            ret = qm_http_client_read(client, buffer, (int)client->rx_buf_size);
            if (ret < 0) break;
            if (ret == 0) {
                ret = QM_EOK;
                break;
            }
        }
        qm_free(buffer);
        return ret;
    }

    buffer = (char *)qm_malloc(client->rx_buf_size);
    if (!buffer) return qm_http_client_report_error(client, -QM_ENOMEM);

    while (!client->complete) {
        int want;

        if (client->body_mode == QM_HTTP_BODY_FIXED) {
            want = client->body_left > client->rx_buf_size ?
                   (int)client->rx_buf_size : (int)client->body_left;
            ret = transport_read(client, buffer, want, client->timeout_ms);
            if (ret <= 0) {
                qm_http_client_close(client);
                ret = qm_http_client_report_error(client, ret == 0 ? -QM_ETIMEOUT : ret);
                goto done;
            }
            client->body_left -= ret;
            client->body_read += (uint32_t)ret;
            if (client->body_left == 0) client->complete = QM_TRUE;
            continue;
        }

        if (client->body_mode == QM_HTTP_BODY_CHUNKED) {
            ret = qm_http_client_read_chunked_stream(client, buffer, (int)client->rx_buf_size);
            if (ret < 0) {
                qm_http_client_close(client);
                ret = qm_http_client_report_error(client, ret);
                goto done;
            }
            if (ret == 0 && !client->complete) {
                qm_http_client_close(client);
                ret = qm_http_client_report_error(client, -QM_EIO);
                goto done;
            }
            continue;
        }

        if (client->body_mode == QM_HTTP_BODY_UNTIL_CLOSE) {
            uint32_t left = client->max_body_len - client->body_read;

            if (left == 0) {
                qm_http_client_close(client);
                ret = qm_http_client_report_error(client, -QM_EFULL);
                goto done;
            }
            want = client->rx_buf_size > left ? (int)left : (int)client->rx_buf_size;
            ret = transport_read(client, buffer, want, client->timeout_ms);
            if (ret > 0) {
                client->body_read += (uint32_t)ret;
                continue;
            }
            if (ret == -QM_EIO) {
                client->complete = QM_TRUE;
                qm_http_client_close(client);
                ret = QM_EOK;
                goto done;
            }
            qm_http_client_close(client);
            ret = qm_http_client_report_error(client, ret == 0 ? -QM_ETIMEOUT : ret);
            goto done;
        }

        client->complete = QM_TRUE;
    }

    ret = QM_EOK;

done:
    qm_free(buffer);
    return ret;
}

int qm_http_client_read(qm_http_client_handle_t client, char *buffer, int len)
{
    int ret;
    int want;
    int event_ret;

    if (!client || !buffer || len <= 0) return qm_http_client_report_error(client, -QM_EINVAL);
    if (interrupted(client)) return qm_http_client_report_error(client, -QM_EINTR);
    if (!client->headers_fetched) {
        ret = qm_http_client_fetch_headers(client);
        if (ret < 0) return ret;
    }
    if (client->complete) {
        ret = qm_http_client_dispatch_finish(client);
        if (ret != QM_EOK) {
            qm_http_client_close(client);
            return ret;
        }
        return 0;
    }

    if (client->body_mode == QM_HTTP_BODY_FIXED) {
        want = client->body_left > (uint32_t)len ? len : (int)client->body_left;
        ret = transport_read(client, buffer, want, client->timeout_ms);
        if (ret <= 0) {
            qm_http_client_close(client);
            return qm_http_client_report_error(client, ret == 0 ? -QM_ETIMEOUT : ret);
        }
        client->body_left -= ret;
        client->body_read += (uint32_t)ret;
        if (client->body_left == 0) client->complete = QM_TRUE;
        QM_HTTP_CLIENT_LOGD(LOG_TAG, QM_HTTP_CLIENT_LOG_DOWNLINK,
                            "response body read len=%d total=%lu", ret,
                            (unsigned long)client->body_read);
        event_ret = qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_DATA,
                                                  buffer, ret, NULL, NULL, QM_EOK, NULL);
        if (event_ret != QM_EOK) {
            qm_http_client_close(client);
            return qm_http_client_report_error(client, qm_http_client_event_ret(event_ret));
        }
        return ret;
    }

    if (client->body_mode == QM_HTTP_BODY_CHUNKED) {
        ret = qm_http_client_read_chunked_stream(client, buffer, len);
        if (interrupted(client)) ret = -QM_EINTR;
        if (ret < 0) {
            qm_http_client_close(client);
            return qm_http_client_report_error(client, ret);
        }
        if (ret > 0) {
            QM_HTTP_CLIENT_LOGD(LOG_TAG, QM_HTTP_CLIENT_LOG_DOWNLINK,
                                "chunked response body read len=%d total=%lu", ret,
                                (unsigned long)client->body_read);
            event_ret = qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_DATA,
                                                      buffer, ret, NULL, NULL, QM_EOK, NULL);
            if (event_ret != QM_EOK) {
                qm_http_client_close(client);
                return qm_http_client_report_error(client, qm_http_client_event_ret(event_ret));
            }
        } else if (client->complete) {
            ret = qm_http_client_dispatch_finish(client);
            if (ret != QM_EOK) {
                qm_http_client_close(client);
                return ret;
            }
        }
        return ret;
    }

    if (client->body_mode == QM_HTTP_BODY_UNTIL_CLOSE) {
        uint32_t left = client->max_body_len - client->body_read;
        if (left == 0) {
            qm_http_client_close(client);
            return qm_http_client_report_error(client, -QM_EFULL);
        }
        want = (uint32_t)len > left ? (int)left : len;
        ret = transport_read(client, buffer, want, client->timeout_ms);
        if (ret > 0) {
            client->body_read += (uint32_t)ret;
            QM_HTTP_CLIENT_LOGD(LOG_TAG, QM_HTTP_CLIENT_LOG_DOWNLINK,
                                "until-close response body read len=%d total=%lu", ret,
                                (unsigned long)client->body_read);
            event_ret = qm_http_client_dispatch_event(client, QM_HTTP_CLIENT_EVENT_DATA,
                                                      buffer, ret, NULL, NULL, QM_EOK, NULL);
            if (event_ret != QM_EOK) {
                qm_http_client_close(client);
                return qm_http_client_report_error(client, qm_http_client_event_ret(event_ret));
            }
            return ret;
        }
        if (ret == -QM_EIO) {
            client->complete = QM_TRUE;
            ret = qm_http_client_dispatch_finish(client);
            qm_http_client_close(client);
            return ret == QM_EOK ? 0 : ret;
        }
        qm_http_client_close(client);
        return qm_http_client_report_error(client, ret == 0 ? -QM_ETIMEOUT : ret);
    }

    client->complete = QM_TRUE;
    ret = qm_http_client_dispatch_finish(client);
    if (ret != QM_EOK) {
        qm_http_client_close(client);
        return ret;
    }
    return 0;
}

int qm_http_client_drain_response_body(qm_http_client_handle_t client)
{
    return qm_http_client_drain_response_body_internal(client, QM_FALSE);
}

static int qm_http_client_stream_request_body(qm_http_client_handle_t client,
                                              int body_len,
                                              qm_http_request_body_cb_t body_cb)
{
    char *buf;
    int remaining;
    int ret = QM_EOK;

    if (!client || body_len < 0 || (body_len > 0 && !body_cb)) {
        return qm_http_client_report_error(client, -QM_EINVAL);
    }
    if (body_len == 0) return QM_EOK;

    buf = (char *)qm_malloc(client->rx_buf_size);
    if (!buf) return qm_http_client_report_error(client, -QM_ENOMEM);

    remaining = body_len;
    while (remaining > 0) {
        int want = remaining < (int)client->rx_buf_size ? remaining : (int)client->rx_buf_size;
        int produced = body_cb(buf, want, client->user);

        if (produced < 0) {
            ret = qm_http_client_report_error(client, produced);
            goto done;
        }
        if (produced == 0) {
            ret = qm_http_client_report_error(client, -QM_EIO);
            goto done;
        }
        if (produced > want) {
            ret = qm_http_client_report_error(client, -QM_EFULL);
            goto done;
        }

        ret = qm_http_client_write(client, buf, produced);
        if (ret < 0) goto done;
        remaining -= produced;
    }

    ret = QM_EOK;

done:
    qm_free(buf);
    return ret;
}

static int qm_http_client_reset_request_body(qm_http_client_handle_t client,
                                             int body_len,
                                             qm_http_request_body_cb_t body_cb,
                                             qm_http_request_body_reset_cb_t body_reset)
{
    int ret;

    if (body_len <= 0 || !body_cb) return QM_EOK;
    if (!body_reset) return qm_http_client_report_error(client, -QM_ENOSYS);

    ret = body_reset(client->user);
    if (ret != QM_EOK) return qm_http_client_report_error(client, ret);
    return QM_EOK;
}

/**
 * @brief Execute one full HTTP transaction, including retries if needed.
 *
 * This is the core request engine behind both one-shot and streaming-style
 * public APIs. It owns request serialization, transport connection reuse,
 * response parsing, redirect handling, and authentication retry flow.
 *
 * @param[in] client HTTP client handle
 * @param[in] body Optional request body pointer
 * @param[in] body_len Request body length when @p body is used
 * @param[in] body_cb Optional streaming request body callback
 * @param[in] body_reset Optional reset callback used before retries
 * @param[in] on_body Optional response body consumer callback
 * @return QM_EOK on success
 * @return Negative error code on failure
 */
static int qm_http_client_perform_internal(qm_http_client_handle_t client,
                                           const char *body, int body_len,
                                           qm_http_request_body_cb_t body_cb,
                                           qm_http_request_body_reset_cb_t body_reset,
                                           qm_http_body_cb_t on_body)
{
    char *buf;
    int ret;
    int final_ret = QM_EOK;
    int redirect_count = 0;
    int auth_retry_count = 0;
    const char *current_body = body;
    int current_body_len = body_len;
    int replay_count = 0;

    if (!client || body_len < 0 || (body_len > 0 && !body && !body_cb) ||
        (body && body_cb) || (!body_cb && body_reset)) {
        return qm_http_client_report_error(client, -QM_EINVAL);
    }
    for (;;) {
        if (replay_count > 0) {
            ret = qm_http_client_reset_request_body(client, current_body_len, body_cb, body_reset);
            if (ret != QM_EOK) {
                client->digest_body = NULL;
                client->digest_body_len = 0;
                return ret;
            }
        }
        client->digest_body = current_body;
        client->digest_body_len = current_body_len;
        ret = qm_http_client_open(client, current_body_len);
        if (ret != QM_EOK) {
            client->digest_body = NULL;
            client->digest_body_len = 0;
            return ret;
        }
        if (current_body_len > 0) {
            if (body_cb) {
                ret = qm_http_client_stream_request_body(client, current_body_len, body_cb);
            } else {
                ret = qm_http_client_write(client, current_body, current_body_len);
            }
            if (ret < 0) {
                client->digest_body = NULL;
                client->digest_body_len = 0;
                return ret;
            }
        }
        ret = qm_http_client_fetch_headers(client);
        if (ret < 0) {
            client->digest_body = NULL;
            client->digest_body_len = 0;
            return ret;
        }

        if (client->auth_type == QM_HTTP_AUTH_TYPE_DIGEST &&
            client->response.status_code == 401 &&
            client->digest.valid &&
            qm_http_client_header_find(client, "Authorization") < 0 &&
            auth_retry_count < QM_HTTP_CLIENT_DEFAULT_AUTH_RETRIES) {
            ret = qm_http_client_drain_response_body_internal(client, QM_FALSE);
            if (ret != QM_EOK) {
                client->digest_body = NULL;
                client->digest_body_len = 0;
                return ret;
            }
            auth_retry_count++;
            replay_count++;
            continue;
        }

        if (client->disable_auto_redirect ||
            !qm_http_client_is_redirect_status(client->response.status_code)) {
            break;
        }
        if (!client->location) {
            qm_http_client_close(client);
            client->digest_body = NULL;
            client->digest_body_len = 0;
            return qm_http_client_report_error(client, -QM_EINVAL);
        }
        if (redirect_count >= client->max_redirection_count) {
            qm_http_client_close(client);
            client->digest_body = NULL;
            client->digest_body_len = 0;
            return qm_http_client_report_error(client, -QM_EFULL);
        }
        redirect_count++;
        ret = qm_http_client_drain_response_body_internal(client, QM_FALSE);
        if (ret != QM_EOK) {
            client->digest_body = NULL;
            client->digest_body_len = 0;
            return ret;
        }
        if (client->response.status_code == 303 ||
            ((client->response.status_code == 301 || client->response.status_code == 302) &&
             client->method == QM_HTTP_POST)) {
            client->method = QM_HTTP_GET;
            current_body = NULL;
            current_body_len = 0;
            qm_http_client_delete_header(client, "Content-Length");
            qm_http_client_delete_header(client, "Content-Type");
        }
        ret = qm_http_client_set_redirect_url(client);
        if (ret != QM_EOK) {
            qm_http_client_close(client);
            client->digest_body = NULL;
            client->digest_body_len = 0;
            return qm_http_client_report_error(client, ret);
        }
        replay_count++;
    }

    buf = (char *)qm_malloc(client->rx_buf_size);
    if (!buf) {
        qm_http_client_close(client);
        client->digest_body = NULL;
        client->digest_body_len = 0;
        return qm_http_client_report_error(client, -QM_ENOMEM);
    }
    for (;;) {
        ret = qm_http_client_read(client, buf, (int)client->rx_buf_size);
        if (ret < 0) {
            final_ret = ret;
            break;
        }
        if (ret == 0) break;
        if (on_body && on_body(buf, ret, client->user) != QM_EOK) {
            final_ret = qm_http_client_report_error(client, -QM_EIO);
            break;
        }
    }
    qm_free(buf);

    if (final_ret != QM_EOK || !client->keep_alive || !client->response.keep_alive) {
        qm_http_client_close(client);
    }
    client->digest_body = NULL;
    client->digest_body_len = 0;
    return final_ret;
}

int qm_http_client_perform(qm_http_client_handle_t client, const char *body, int body_len,
                           qm_http_body_cb_t on_body)
{
    return qm_http_client_perform_internal(client, body, body_len, NULL, NULL, on_body);
}

int qm_http_client_cleanup(qm_http_client_handle_t client)
{
    int i;

    if (!client) return -QM_EINVAL;
    QM_HTTP_CLIENT_LOGI(LOG_TAG, QM_HTTP_CLIENT_LOG_LIFECYCLE, "cleanup client");
    qm_http_client_close(client);
    if (client->url) qm_free(client->url);
    if (client->username) qm_free(client->username);
    if (client->password) qm_free(client->password);
    if (client->content_type) qm_free(client->content_type);
    if (client->user_agent) qm_free(client->user_agent);
    if (client->location) qm_free(client->location);
    qm_http_client_digest_clear(&client->digest);
    for (i = 0; i < client->header_count; i++) {
        qm_http_client_header_free(&client->headers[i]);
    }
    if (client->headers) qm_free(client->headers);
    qm_free(client);
    return QM_EOK;
}

int qm_http_client_get_status_code(qm_http_client_handle_t client)
{
    return client ? client->response.status_code : 0;
}

uint32_t qm_http_client_get_content_length(qm_http_client_handle_t client)
{
    return client && client->response.content_length_known ? client->response.content_length : 0;
}

qm_bool qm_http_client_has_content_length(qm_http_client_handle_t client)
{
    return client ? client->response.content_length_known : QM_FALSE;
}

const char *qm_http_client_get_location(qm_http_client_handle_t client)
{
    return client ? client->location : NULL;
}

qm_bool qm_http_client_is_chunked_response(qm_http_client_handle_t client)
{
    return client ? client->response.chunked : QM_FALSE;
}

qm_bool qm_http_client_is_complete_data_received(qm_http_client_handle_t client)
{
    return client ? client->complete : QM_FALSE;
}

void *qm_http_client_get_user_data(qm_http_client_handle_t client)
{
    return client ? client->user : NULL;
}

int qm_http_client_set_user_data(qm_http_client_handle_t client, void *user)
{
    if (!client) return -QM_EINVAL;
    client->user = user;
    return QM_EOK;
}

int qm_http_client_execute(const qm_http_client_request_t *req,
                           qm_http_client_response_t *resp)
{
    qm_http_client_config_t config;
    qm_http_client_handle_t client;
    int i;
    int ret;

    if (!req || !req->url || !resp) return -QM_EINVAL;
    if (req->body_len < 0 || req->header_count < 0) return -QM_EINVAL;
    if (req->body_len > 0 && !req->body && !req->body_cb) return -QM_EINVAL;
    if (req->body && req->body_cb) return -QM_EINVAL;
    if (!req->body_cb && req->body_reset) return -QM_EINVAL;
    if (req->header_count > 0 && !req->headers) return -QM_EINVAL;
    for (i = 0; i < req->header_count; i++) {
        if (!req->headers[i].name || !req->headers[i].value) return -QM_EINVAL;
        if (qm_http_client_is_reserved_header(req->headers[i].name)) return -QM_EINVAL;
    }

    qm_memset(&config, 0, sizeof(config));
    config.method = req->method;
    config.url = req->url;
    config.username = req->username;
    config.password = req->password;
    config.auth_type = req->auth_type;
    config.content_type = req->content_type;
    config.user_agent = req->user_agent;
    config.headers = req->headers;
    config.header_count = req->header_count;
    config.timeout_ms = req->timeout_ms;
    config.max_header_len = req->max_header_len;
    config.max_body_len = req->max_body_len;
    config.rx_buf_size = req->rx_buf_size;
    config.disable_auto_redirect = req->disable_auto_redirect;
    config.max_redirection_count = req->max_redirection_count;
    config.keep_alive = req->keep_alive;
    config.tls = req->tls;
    config.user = req->user;
    config.on_event = req->on_event;
    config.interrupted = req->interrupted;

    client = qm_http_client_init(&config);
    if (!client) return -QM_ENOMEM;
    ret = qm_http_client_perform_internal(client, req->body, req->body_len,
                                          req->body_cb, req->body_reset,
                                          req->on_body);
    *resp = client->response;
    qm_http_client_cleanup(client);
    resp->keep_alive = QM_FALSE;
    resp->location = NULL;
    return ret;
}
