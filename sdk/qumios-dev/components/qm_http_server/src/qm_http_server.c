#include "qm_http_server.h"

#define QM_HTTP_SERVER_INTERNAL
#include "qm_http_server_transport.h"
#undef QM_HTTP_SERVER_INTERNAL

#include "qm_kernel.h"
#include "qm_string.h"

#define LOG_TAG "qm_http_server"

static volatile uint32_t g_qm_http_server_log_flags = QM_HTTP_SERVER_KCONFIG_LOG_FLAGS;

uint32_t qm_http_server_get_log_flags(void)
{
    return g_qm_http_server_log_flags;
}

int qm_http_server_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_HTTP_SERVER_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_http_server_log_flags = log_flags;
    return QM_EOK;
}

#ifndef CONFIG_QM_HTTP_SERVER_TIMEOUT_MS
#define CONFIG_QM_HTTP_SERVER_TIMEOUT_MS 5000
#endif

#ifndef CONFIG_QM_HTTP_SERVER_HEADER_MAX
#define CONFIG_QM_HTTP_SERVER_HEADER_MAX 1024
#endif

#ifndef CONFIG_QM_HTTP_SERVER_BODY_MAX
#define CONFIG_QM_HTTP_SERVER_BODY_MAX 4096
#endif

#ifndef CONFIG_QM_HTTP_SERVER_STACK
#define CONFIG_QM_HTTP_SERVER_STACK 4096
#endif

#ifndef CONFIG_QM_HTTP_SERVER_PRIO
#define CONFIG_QM_HTTP_SERVER_PRIO 32
#endif

#define QM_HTTP_DEFAULT_ACCEPT_TIMEOUT_MS 200
#define QM_HTTP_DEFAULT_CLIENT_TIMEOUT_MS CONFIG_QM_HTTP_SERVER_TIMEOUT_MS
#define QM_HTTP_DEFAULT_HEADER_MAX CONFIG_QM_HTTP_SERVER_HEADER_MAX
#define QM_HTTP_RESPONSE_HEADER_INLINE_MAX 8
#define QM_HTTP_INT_MAX 2147483647

typedef struct qm_http_session {
    int sockfd;
    uint32_t last_active_ms;
    qm_bool close_pending;
    struct qm_http_session *next;
} qm_http_session_t;

typedef struct {
    char *name;
    char *value;
} qm_http_header_entry_t;

typedef struct {
    qm_http_header_entry_t inline_headers[QM_HTTP_RESPONSE_HEADER_INLINE_MAX];
    qm_http_header_entry_t *headers;
    int count;
    int capacity;
} qm_http_response_headers_t;

struct qm_http_server {
    qm_http_server_config_t cfg;
    qm_http_server_listener_t *listener;
    qm_task_t task;
    qm_mutex_t lock;
    qm_http_session_t *sessions;
    qm_bool running;
    qm_bool stopping;
};

struct qm_http_response {
    qm_http_server_t server;
    qm_http_server_connection_t *connection;
    qm_http_session_t *session;
    uint32_t timeout_ms;
    qm_bool header_sent;
    qm_bool chunked;
    int status_code;
    const char *content_type;
    qm_http_response_headers_t headers;
};

typedef struct {
    qm_http_server_t server;
    qm_http_server_connection_t *connection;
    qm_http_session_t *session;
} qm_http_request_priv_t;

typedef struct {
    qm_http_request_t req;
    qm_http_request_priv_t priv;
    char *headers;
    char *uri_buf;
    char *path_buf;
    char *query_buf;
    int remaining_body_len;
    qm_bool request_chunked;
} qm_http_request_ctx_t;

static const char *qm_http_status_text(int status_code)
{
    switch (status_code) {
    case 200: return "200 OK";
    case 201: return "201 Created";
    case 202: return "202 Accepted";
    case 204: return "204 No Content";
    case 301: return "301 Moved Permanently";
    case 302: return "302 Found";
    case 304: return "304 Not Modified";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    case 408: return "408 Request Timeout";
    case 409: return "409 Conflict";
    case 411: return "411 Length Required";
    case 413: return "413 Content Too Large";
    case 414: return "414 URI Too Long";
    case 415: return "415 Unsupported Media Type";
    case 429: return "429 Too Many Requests";
    case 431: return "431 Request Header Fields Too Large";
    case 500: return "500 Internal Server Error";
    case 501: return "501 Not Implemented";
    case 503: return "503 Service Unavailable";
    case 505: return "505 HTTP Version Not Supported";
    default: return NULL;
    }
}

static char *qm_http_strdup(const char *src)
{
    uint32_t len;
    char *dst;

    if (!src) {
        return NULL;
    }

    len = qm_strlen(src) + 1;
    dst = (char *)qm_malloc((unsigned int)len);
    if (!dst) {
        return NULL;
    }
    qm_memcpy(dst, src, len);
    return dst;
}

static char *qm_http_strndup(const char *src, uint32_t len)
{
    char *dst;

    if (!src) {
        return NULL;
    }

    dst = (char *)qm_malloc((unsigned int)(len + 1));
    if (!dst) {
        return NULL;
    }
    qm_memcpy(dst, src, len);
    dst[len] = 0;
    return dst;
}

static char *qm_http_strcasestr_local(const char *haystack, const char *needle)
{
    uint32_t needle_len;

    if (!haystack || !needle) {
        return NULL;
    }
    needle_len = qm_strlen(needle);
    if (needle_len == 0) {
        return (char *)haystack;
    }

    while (*haystack) {
        if (qm_strncasecmp(haystack, needle, needle_len) == 0) {
            return (char *)haystack;
        }
        haystack++;
    }
    return NULL;
}

static int qm_http_method_from_string(const char *method, qm_http_method_t *out)
{
    if (!method || !out) {
        return -QM_EINVAL;
    }
    if (qm_strcmp(method, "GET") == 0) {
        *out = QM_HTTP_GET;
    } else if (qm_strcmp(method, "POST") == 0) {
        *out = QM_HTTP_POST;
    } else if (qm_strcmp(method, "PUT") == 0) {
        *out = QM_HTTP_PUT;
    } else if (qm_strcmp(method, "DELETE") == 0) {
        *out = QM_HTTP_DELETE;
    } else if (qm_strcmp(method, "HEAD") == 0) {
        *out = QM_HTTP_HEAD;
    } else {
        return -QM_ENOSYS;
    }
    return QM_EOK;
}

static int qm_http_hex_value(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

static int qm_http_parse_decimal(const char *value, int *out)
{
    int parsed = 0;
    qm_bool seen_digit = QM_FALSE;

    if (!value || !out) {
        return -QM_EINVAL;
    }
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    while (*value >= '0' && *value <= '9') {
        int digit = *value - '0';

        if (parsed > (QM_HTTP_INT_MAX - digit) / 10) {
            return -QM_EFULL;
        }
        parsed = parsed * 10 + digit;
        seen_digit = QM_TRUE;
        value++;
    }
    while (*value == ' ' || *value == '\t') {
        value++;
    }
    if (!seen_digit || (*value != 0 && *value != '\r' && *value != '\n')) {
        return -QM_EINVAL;
    }
    *out = parsed;
    return QM_EOK;
}

static int qm_http_url_decode(const char *src, uint32_t src_len, char *dst, int dst_len)
{
    int out = 0;
    uint32_t i;

    if (!src || !dst || dst_len <= 0) {
        return -QM_EINVAL;
    }

    for (i = 0; i < src_len; i++) {
        char c = src[i];

        if (out >= dst_len - 1) {
            return -QM_EFULL;
        }
        if (c == '+') {
            dst[out++] = ' ';
        } else if (c == '%' && i + 2 < src_len) {
            int hi = qm_http_hex_value(src[i + 1]);
            int lo = qm_http_hex_value(src[i + 2]);

            if (hi >= 0 && lo >= 0) {
                dst[out++] = (char)((hi << 4) | lo);
                i += 2;
            } else {
                dst[out++] = c;
            }
        } else {
            dst[out++] = c;
        }
    }

    dst[out] = 0;
    return QM_EOK;
}

static int qm_http_write_all(qm_http_server_connection_t *connection,
                             const char *buf,
                             int len,
                             uint32_t timeout_ms)
{
    int total = 0;

    if (!connection || !connection->write || (!buf && len > 0) || len < 0) {
        return -QM_EINVAL;
    }

    while (total < len) {
        int ret = connection->write(connection->handle, buf + total, len - total, timeout_ms);
        if (ret <= 0) {
            return ret == 0 ? -QM_ETIMEOUT : ret;
        }
        total += ret;
    }
    return QM_EOK;
}

static void qm_http_response_headers_init(qm_http_response_headers_t *headers)
{
    if (!headers) {
        return;
    }
    qm_memset(headers, 0, sizeof(*headers));
    headers->headers = headers->inline_headers;
    headers->capacity = QM_HTTP_RESPONSE_HEADER_INLINE_MAX;
}

static void qm_http_response_headers_cleanup(qm_http_response_headers_t *headers)
{
    int i;

    if (!headers) {
        return;
    }
    for (i = 0; i < headers->count; i++) {
        qm_free(headers->headers[i].name);
        qm_free(headers->headers[i].value);
    }
    if (headers->headers != headers->inline_headers) {
        qm_free(headers->headers);
    }
    qm_memset(headers, 0, sizeof(*headers));
}

static int qm_http_response_headers_add(qm_http_response_headers_t *headers,
                                        const char *name,
                                        const char *value)
{
    char *name_copy;
    char *value_copy;

    if (!headers || !name || !value) {
        return -QM_EINVAL;
    }
    if (headers->count >= headers->capacity) {
        int new_capacity = headers->capacity * 2;
        qm_http_header_entry_t *new_headers;

        new_headers = (qm_http_header_entry_t *)qm_calloc((unsigned int)new_capacity,
                                                          sizeof(*new_headers));
        if (!new_headers) {
            return -QM_ENOMEM;
        }
        qm_memcpy(new_headers, headers->headers, sizeof(*new_headers) * headers->count);
        if (headers->headers != headers->inline_headers) {
            qm_free(headers->headers);
        }
        headers->headers = new_headers;
        headers->capacity = new_capacity;
    }

    name_copy = qm_http_strdup(name);
    value_copy = qm_http_strdup(value);
    if (!name_copy || !value_copy) {
        qm_free(name_copy);
        qm_free(value_copy);
        return -QM_ENOMEM;
    }

    headers->headers[headers->count].name = name_copy;
    headers->headers[headers->count].value = value_copy;
    headers->count++;
    return QM_EOK;
}

static int qm_http_response_headers_write(qm_http_response_t *resp)
{
    int i;
    int ret;

    if (!resp) {
        return -QM_EINVAL;
    }

    for (i = 0; i < resp->headers.count; i++) {
        char line[256];
        int len = qm_snprintf(line, sizeof(line), "%s: %s\r\n",
                              resp->headers.headers[i].name,
                              resp->headers.headers[i].value);
        if (len <= 0 || len >= (int)sizeof(line)) {
            return -QM_EFULL;
        }
        ret = qm_http_write_all(resp->connection, line, len, resp->timeout_ms);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    return QM_EOK;
}

static int qm_http_response_send_headers(qm_http_response_t *resp, qm_bool chunked, int content_length)
{
    char line[160];
    const char *status;
    int len;
    int ret;

    if (!resp || !resp->connection || resp->header_sent) {
        return -QM_EINVAL;
    }

    status = qm_http_status_text(resp->status_code);
    if (!status) {
        return -QM_EINVAL;
    }

    len = qm_snprintf(line, sizeof(line), "HTTP/1.1 %s\r\n", status);
    if (len <= 0 || len >= (int)sizeof(line)) {
        return -QM_EFULL;
    }
    ret = qm_http_write_all(resp->connection, line, len, resp->timeout_ms);
    if (ret != QM_EOK) {
        return ret;
    }

    if (resp->content_type) {
        len = qm_snprintf(line, sizeof(line), "Content-Type: %s\r\n", resp->content_type);
        if (len <= 0 || len >= (int)sizeof(line)) {
            return -QM_EFULL;
        }
        ret = qm_http_write_all(resp->connection, line, len, resp->timeout_ms);
        if (ret != QM_EOK) {
            return ret;
        }
    }

    if (chunked) {
        ret = qm_http_write_all(resp->connection, "Transfer-Encoding: chunked\r\n",
                                28, resp->timeout_ms);
    } else {
        len = qm_snprintf(line, sizeof(line), "Content-Length: %d\r\n", content_length);
        if (len <= 0 || len >= (int)sizeof(line)) {
            return -QM_EFULL;
        }
        ret = qm_http_write_all(resp->connection, line, len, resp->timeout_ms);
    }
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_http_response_headers_write(resp);
    if (ret != QM_EOK) {
        return ret;
    }

    if (!resp->session || resp->session->close_pending) {
        ret = qm_http_write_all(resp->connection, "Connection: close\r\n", 19, resp->timeout_ms);
    } else {
        ret = qm_http_write_all(resp->connection, "Connection: keep-alive\r\n", 24, resp->timeout_ms);
    }
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_http_write_all(resp->connection, "\r\n", 2, resp->timeout_ms);
    if (ret == QM_EOK) {
        resp->header_sent = QM_TRUE;
    }
    return ret;
}

static qm_bool qm_http_route_match_path(const char *pattern, const char *path)
{
    uint32_t len;

    if (!pattern || !path) {
        return QM_FALSE;
    }
    if (qm_strcmp(pattern, path) == 0) {
        return QM_TRUE;
    }

    len = qm_strlen(pattern);
    if (len >= 2 && pattern[len - 1] == '*' && pattern[len - 2] == '/') {
        return qm_strncmp(pattern, path, len - 1) == 0 ? QM_TRUE : QM_FALSE;
    }
    if (len >= 2 && pattern[len - 2] == '?' && pattern[len - 1] == '*') {
        return qm_strncmp(pattern, path, len - 2) == 0 ? QM_TRUE : QM_FALSE;
    }
    return QM_FALSE;
}

static const qm_http_route_t *qm_http_route_find(qm_http_server_t server,
                                                 qm_http_method_t method,
                                                 const char *path,
                                                 qm_bool *method_exists)
{
    const qm_http_route_t *any_match = NULL;
    int i;

    if (method_exists) {
        *method_exists = QM_FALSE;
    }
    if (!server || !path) {
        return NULL;
    }

    for (i = 0; i < server->cfg.route_count; i++) {
        const qm_http_route_t *route = &server->cfg.routes[i];
        if (!route->path || !route->handler ||
            !qm_http_route_match_path(route->path, path)) {
            continue;
        }
        if (route->method == method) {
            return route;
        }
        if (route->method == QM_HTTP_ANY) {
            any_match = route;
        } else if (method_exists) {
            *method_exists = QM_TRUE;
        }
    }

    return any_match;
}

static void qm_http_request_cleanup(qm_http_request_ctx_t *ctx)
{
    if (!ctx) {
        return;
    }
    qm_free(ctx->query_buf);
    qm_free(ctx->path_buf);
    qm_free(ctx->uri_buf);
    ctx->query_buf = NULL;
    ctx->path_buf = NULL;
    ctx->uri_buf = NULL;
    ctx->headers = NULL;
    ctx->req.query = NULL;
    ctx->req.path = NULL;
    ctx->req.uri = NULL;
}

static qm_http_session_t *qm_http_session_add(qm_http_server_t server, int sockfd)
{
    qm_http_session_t *session;

    if (!server || sockfd < 0) {
        return NULL;
    }

    session = (qm_http_session_t *)qm_calloc(1, sizeof(*session));
    if (!session) {
        return NULL;
    }
    session->sockfd = sockfd;
    session->last_active_ms = qm_now_ms();

    qm_mutex_lock(&server->lock, QM_WAIT_FOREVER);
    session->next = server->sessions;
    server->sessions = session;
    qm_mutex_unlock(&server->lock);

    return session;
}

static void qm_http_session_remove(qm_http_server_t server, qm_http_session_t *session)
{
    qm_http_session_t *prev = NULL;
    qm_http_session_t *item;

    if (!server || !session) {
        return;
    }

    qm_mutex_lock(&server->lock, QM_WAIT_FOREVER);
    for (item = server->sessions; item; item = item->next) {
        if (item == session) {
            if (prev) {
                prev->next = item->next;
            } else {
                server->sessions = item->next;
            }
            break;
        }
        prev = item;
    }
    qm_mutex_unlock(&server->lock);

    qm_free(session);
}

static void qm_http_sessions_mark_close(qm_http_server_t server)
{
    qm_http_session_t *session;

    if (!server) {
        return;
    }
    qm_mutex_lock(&server->lock, QM_WAIT_FOREVER);
    for (session = server->sessions; session; session = session->next) {
        session->close_pending = QM_TRUE;
    }
    qm_mutex_unlock(&server->lock);
}

static int qm_http_read_headers(qm_http_server_connection_t *connection,
                                char *buf,
                                int max_len,
                                int *header_len,
                                uint32_t timeout_ms)
{
    int total = 0;

    if (!connection || !buf || max_len <= 0 || !header_len) {
        return -QM_EINVAL;
    }

    while (total < max_len) {
        int ret = connection->read(connection->handle, buf + total, 1, timeout_ms);
        if (ret < 0) {
            return ret;
        }
        if (ret == 0) {
            return total == 0 ? -QM_ETIMEOUT : -QM_EIO;
        }
        total += ret;
        if (total >= 4 && qm_memcmp(buf + total - 4, "\r\n\r\n", 4) == 0) {
            buf[total] = 0;
            *header_len = total;
            return QM_EOK;
        }
    }

    return -QM_EFULL;
}

static int qm_http_request_parse(qm_http_server_t server,
                                 qm_http_server_connection_t *connection,
                                 qm_http_session_t *session,
                                 char *header_buf,
                                 int header_len,
                                 qm_http_request_ctx_t *ctx)
{
    qm_http_request_t *req;
    qm_http_request_priv_t *priv;
    char *line_end;
    char *method_end;
    char *uri_end;
    char *query_sep;
    char *headers;
    char *content_length;
    char *transfer_encoding;
    char *connection_header;
    char method[12];
    uint32_t method_len;
    uint32_t uri_len;
    int ret;

    if (!server || !connection || !session || !header_buf || !ctx) {
        return -QM_EINVAL;
    }

    qm_memset(ctx, 0, sizeof(*ctx));
    req = &ctx->req;
    priv = &ctx->priv;

    line_end = qm_strstr(header_buf, "\r\n");
    if (!line_end) {
        return -QM_EINVAL;
    }
    *line_end = 0;
    headers = line_end + 2;

    method_end = qm_strchr(header_buf, ' ');
    if (!method_end) {
        return -QM_EINVAL;
    }
    uri_end = qm_strchr(method_end + 1, ' ');
    if (!uri_end) {
        return -QM_EINVAL;
    }

    method_len = (uint32_t)(method_end - header_buf);
    if (method_len == 0 || method_len >= sizeof(method)) {
        return -QM_EINVAL;
    }
    qm_memcpy(method, header_buf, method_len);
    method[method_len] = 0;
    ret = qm_http_method_from_string(method, &req->method);
    if (ret != QM_EOK) {
        return ret;
    }

    uri_len = (uint32_t)(uri_end - method_end - 1);
    ctx->uri_buf = qm_http_strndup(method_end + 1, uri_len);
    if (!ctx->uri_buf) {
        return -QM_ENOMEM;
    }
    req->uri = ctx->uri_buf;

    query_sep = qm_strchr(ctx->uri_buf, '?');
    if (query_sep) {
        uint32_t path_len = (uint32_t)(query_sep - ctx->uri_buf);
        ctx->path_buf = qm_http_strndup(ctx->uri_buf, path_len);
        ctx->query_buf = qm_http_strdup(query_sep + 1);
        if (!ctx->path_buf || !ctx->query_buf) {
            return -QM_ENOMEM;
        }
        req->path = ctx->path_buf;
        req->query = ctx->query_buf;
    } else {
        req->path = ctx->uri_buf;
    }

    (void)header_len;
    ctx->headers = headers;

    content_length = qm_http_strcasestr_local(headers, "\r\nContent-Length:");
    if (!content_length && qm_strncasecmp(headers, "Content-Length:", 15) == 0) {
        content_length = headers;
    }
    if (content_length) {
        char *value = qm_strchr(content_length, ':');
        if (value) {
            ret = qm_http_parse_decimal(value + 1, &req->content_length);
            if (ret != QM_EOK) {
                return ret;
            }
        }
    }
    if (req->content_length < 0) {
        return -QM_EINVAL;
    }
    ctx->remaining_body_len = req->content_length;

    transfer_encoding = qm_http_strcasestr_local(headers, "\r\nTransfer-Encoding:");
    if (!transfer_encoding && qm_strncasecmp(headers, "Transfer-Encoding:", 18) == 0) {
        transfer_encoding = headers;
    }
    if (transfer_encoding) {
        char *line = transfer_encoding;
        char *end = qm_strstr(line, "\r\n");
        if (end && qm_http_strcasestr_local(line, "chunked") && qm_http_strcasestr_local(line, "chunked") < end) {
            ctx->request_chunked = QM_TRUE;
        }
    }

    connection_header = qm_http_strcasestr_local(headers, "\r\nConnection:");
    if (!connection_header && qm_strncasecmp(headers, "Connection:", 11) == 0) {
        connection_header = headers;
    }
    if (connection_header) {
        char *line = connection_header;
        char *end = qm_strstr(line, "\r\n");
        char *close_value = qm_http_strcasestr_local(line, "close");
        char *keep_alive_value = qm_http_strcasestr_local(line, "keep-alive");
        if (close_value && (!end || close_value < end)) {
            session->close_pending = QM_TRUE;
        } else if (keep_alive_value && (!end || keep_alive_value < end)) {
            session->close_pending = server->cfg.keep_alive_enable ? QM_FALSE : QM_TRUE;
        }
    }
    if (!server->cfg.keep_alive_enable) {
        session->close_pending = QM_TRUE;
    }

    priv->server = server;
    priv->connection = connection;
    priv->session = session;
    return QM_EOK;
}

int qm_http_query_get(const char *query, const char *name, char *value, int value_len)
{
    uint32_t name_len;
    const char *cursor;

    if (!query || !name || !value || value_len <= 0) {
        return -QM_EINVAL;
    }

    value[0] = 0;
    name_len = qm_strlen(name);
    cursor = query;

    while (*cursor) {
        const char *pair_end = qm_strchr(cursor, '&');
        const char *eq;
        uint32_t pair_len;

        if (!pair_end) {
            pair_end = cursor + qm_strlen(cursor);
        }
        pair_len = (uint32_t)(pair_end - cursor);
        eq = qm_memchr(cursor, '=', pair_len);
        if (eq) {
            uint32_t key_len = (uint32_t)(eq - cursor);
            if (key_len == name_len && qm_strncmp(cursor, name, name_len) == 0) {
                return qm_http_url_decode(eq + 1, pair_len - key_len - 1, value, value_len);
            }
        } else if (pair_len == name_len && qm_strncmp(cursor, name, name_len) == 0) {
            value[0] = 0;
            return QM_EOK;
        }

        cursor = *pair_end ? pair_end + 1 : pair_end;
    }

    return -QM_ENOENT;
}

int qm_http_request_get_header(qm_http_request_t *req, const char *name, char *value, int value_len)
{
    qm_http_request_ctx_t *ctx = (qm_http_request_ctx_t *)req;
    char *cursor;
    uint32_t name_len;

    if (!ctx || !ctx->headers || !name || !value || value_len <= 0) {
        return -QM_EINVAL;
    }

    value[0] = 0;
    cursor = ctx->headers;
    name_len = qm_strlen(name);

    while (cursor && *cursor) {
        char *line_end = qm_strstr(cursor, "\r\n");
        char *colon;
        uint32_t line_len;
        char *line_value;
        int copy_len;

        if (!line_end) {
            break;
        }
        if (line_end == cursor) {
            break;
        }
        line_len = (uint32_t)(line_end - cursor);
        colon = qm_memchr(cursor, ':', line_len);
        if (!colon) {
            cursor = line_end + 2;
            continue;
        }
        if ((uint32_t)(colon - cursor) == name_len && qm_strncasecmp(cursor, name, name_len) == 0) {
            line_value = colon + 1;
            while (line_value < line_end && (*line_value == ' ' || *line_value == '\t')) {
                line_value++;
            }
            copy_len = (int)(line_end - line_value);
            while (copy_len > 0 &&
                   (line_value[copy_len - 1] == ' ' || line_value[copy_len - 1] == '\t')) {
                copy_len--;
            }
            if (copy_len >= value_len) {
                return -QM_EFULL;
            }
            qm_memcpy(value, line_value, (uint32_t)copy_len);
            value[copy_len] = 0;
            return QM_EOK;
        }
        cursor = line_end + 2;
    }

    return -QM_ENOENT;
}

int qm_http_request_read(qm_http_request_t *req, char *buf, int buf_len)
{
    qm_http_request_ctx_t *ctx = (qm_http_request_ctx_t *)req;
    qm_http_request_priv_t *priv;
    int ret;
    int to_read;

    if (!ctx || !buf || buf_len < 0) {
        return -QM_EINVAL;
    }
    if (buf_len == 0) {
        return 0;
    }
    if (ctx->remaining_body_len <= 0) {
        return 0;
    }

    priv = &ctx->priv;
    to_read = buf_len < ctx->remaining_body_len ? buf_len : ctx->remaining_body_len;
    ret = priv->connection->read(priv->connection->handle,
                                 buf,
                                 to_read,
                                 priv->server->cfg.timeout_ms ?
                                 priv->server->cfg.timeout_ms :
                                 QM_HTTP_DEFAULT_CLIENT_TIMEOUT_MS);
    if (ret > 0) {
        ctx->remaining_body_len -= ret;
        if (ctx->remaining_body_len < 0) {
            ctx->remaining_body_len = 0;
        }
        priv->session->last_active_ms = qm_now_ms();
        return ret;
    }
    if (ret == 0) {
        return -QM_ETIMEOUT;
    }
    return ret;
}

int qm_http_request_read_all(qm_http_request_t *req, char **body, int *body_len)
{
    char *buf;
    int total = 0;

    if (!req || !body || !body_len) {
        return -QM_EINVAL;
    }

    *body = NULL;
    *body_len = 0;

    if (req->content_length <= 0) {
        buf = (char *)qm_malloc(1);
        if (!buf) {
            return -QM_ENOMEM;
        }
        buf[0] = 0;
        *body = buf;
        return QM_EOK;
    }

    buf = (char *)qm_malloc((unsigned int)(req->content_length + 1));
    if (!buf) {
        return -QM_ENOMEM;
    }

    while (total < req->content_length) {
        int ret = qm_http_request_read(req, buf + total, req->content_length - total);
        if (ret <= 0) {
            qm_free(buf);
            return ret == 0 ? -QM_EIO : ret;
        }
        total += ret;
    }
    buf[total] = 0;

    *body = buf;
    *body_len = total;
    return QM_EOK;
}

static int qm_http_response_apply_status(qm_http_response_t *resp, int status_code)
{
    if (!resp || resp->header_sent || !qm_http_status_text(status_code)) {
        return -QM_EINVAL;
    }

    resp->status_code = status_code;
    return QM_EOK;
}

int qm_http_response_set_header(qm_http_response_t *resp, const char *name, const char *value)
{
    if (!resp || resp->header_sent || !name || !value) {
        return -QM_EINVAL;
    }
    return qm_http_response_headers_add(&resp->headers, name, value);
}

static int qm_http_response_apply_content_type(qm_http_response_t *resp, const char *content_type)
{
    if (!resp || resp->header_sent || !content_type) {
        return -QM_EINVAL;
    }
    resp->content_type = content_type;
    return QM_EOK;
}

int qm_http_response_send(qm_http_response_t *resp, int status_code,
                          const char *content_type, const char *body, int body_len)
{
    int ret;

    if (!resp || !resp->connection || body_len < 0 || (!body && body_len > 0)) {
        return -QM_EINVAL;
    }
    if (!body) {
        body = "";
        body_len = 0;
    }
    if (!content_type) {
        content_type = "text/plain";
    }

    ret = qm_http_response_apply_status(resp, status_code);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_http_response_apply_content_type(resp, content_type);
    if (ret != QM_EOK) {
        return ret;
    }

    ret = qm_http_response_send_headers(resp, QM_FALSE, body_len);
    if (ret != QM_EOK) {
        return ret;
    }
    if (body_len > 0 && resp->status_code != 204) {
        ret = qm_http_write_all(resp->connection, body, body_len, resp->timeout_ms);
    }
    return ret;
}

int qm_http_response_begin_chunked(qm_http_response_t *resp, int status_code,
                                   const char *content_type)
{
    int ret;

    if (!resp || !resp->connection) {
        return -QM_EINVAL;
    }
    if (!content_type) {
        content_type = "application/octet-stream";
    }

    ret = qm_http_response_apply_status(resp, status_code);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_http_response_apply_content_type(resp, content_type);
    if (ret != QM_EOK) {
        return ret;
    }
    ret = qm_http_response_send_headers(resp, QM_TRUE, 0);
    if (ret == QM_EOK) {
        resp->chunked = QM_TRUE;
    }
    return ret;
}

int qm_http_response_write_chunk(qm_http_response_t *resp, const char *data, int len)
{
    char header[16];
    int header_len;
    int ret;

    if (!resp || !resp->connection || !resp->chunked || len < 0 || (!data && len > 0)) {
        return -QM_EINVAL;
    }
    if (!data) {
        data = "";
    }

    header_len = qm_snprintf(header, sizeof(header), "%x\r\n", len);
    if (header_len <= 0 || header_len >= (int)sizeof(header)) {
        return -QM_EFULL;
    }
    ret = qm_http_write_all(resp->connection, header, header_len, resp->timeout_ms);
    if (ret != QM_EOK) {
        return ret;
    }
    if (len > 0) {
        ret = qm_http_write_all(resp->connection, data, len, resp->timeout_ms);
        if (ret != QM_EOK) {
            return ret;
        }
    }
    return qm_http_write_all(resp->connection, "\r\n", 2, resp->timeout_ms);
}

int qm_http_response_end_chunked(qm_http_response_t *resp)
{
    if (!resp || !resp->connection || !resp->chunked) {
        return -QM_EINVAL;
    }
    resp->chunked = QM_FALSE;
    return qm_http_write_all(resp->connection, "0\r\n\r\n", 5, resp->timeout_ms);
}

static int qm_http_server_send_default_error(qm_http_response_t *resp, int status_code)
{
    char body[96];
    const char *status = qm_http_status_text(status_code);
    int len;

    if (!status) {
        status = "500 Internal Server Error";
        status_code = 500;
    }
    len = qm_snprintf(body, sizeof(body), "{\"error\":%d,\"message\":\"%s\"}\n",
                      status_code, status);
    if (len <= 0 || len >= (int)sizeof(body)) {
        return -QM_EIO;
    }
    return qm_http_response_send(resp, status_code, "application/json", body, len);
}

static int qm_http_server_handle_error(qm_http_server_t server,
                                       qm_http_request_t *req,
                                       qm_http_response_t *resp,
                                       int status_code)
{
    qm_http_error_handler_t handler;
    int ret;

    if (!server || !resp) {
        return -QM_EINVAL;
    }

    handler = server->cfg.error_handler;
    if (handler) {
        ret = handler(req, resp, status_code);
        if (ret == QM_EOK && resp->header_sent) {
            return QM_EOK;
        }
    }

    if (!resp->header_sent) {
        return qm_http_server_send_default_error(resp, status_code);
    }
    return QM_EOK;
}

static void qm_http_response_init(qm_http_response_t *resp,
                                  qm_http_server_t server,
                                  qm_http_server_connection_t *connection,
                                  qm_http_session_t *session)
{
    qm_memset(resp, 0, sizeof(*resp));
    resp->server = server;
    resp->connection = connection;
    resp->session = session;
    resp->timeout_ms = server->cfg.timeout_ms ? server->cfg.timeout_ms : QM_HTTP_DEFAULT_CLIENT_TIMEOUT_MS;
    resp->status_code = 200;
    qm_http_response_headers_init(&resp->headers);
}

static int qm_http_server_handle_request(qm_http_server_t server,
                                         qm_http_server_connection_t *connection,
                                         qm_http_session_t *session,
                                         char *header_buf,
                                         int header_len)
{
    qm_http_request_ctx_t req_ctx;
    qm_http_request_t *req = &req_ctx.req;
    qm_http_response_t resp;
    const qm_http_route_t *route;
    qm_bool method_exists;
    int ret;

    qm_http_response_init(&resp, server, connection, session);

    ret = qm_http_request_parse(server, connection, session, header_buf, header_len, &req_ctx);
    if (ret != QM_EOK) {
        qm_memset(&req_ctx, 0, sizeof(req_ctx));
        req_ctx.priv.server = server;
        req_ctx.priv.connection = connection;
        req_ctx.priv.session = session;
        ret = qm_http_server_handle_error(server, req, &resp,
                                          ret == -QM_ENOSYS ? 501 : 400);
        qm_http_response_headers_cleanup(&resp.headers);
        return ret;
    }

    if (server->cfg.max_body_len > 0 && req->content_length > (int)server->cfg.max_body_len) {
        ret = qm_http_server_handle_error(server, req, &resp, 413);
        qm_http_request_cleanup(&req_ctx);
        qm_http_response_headers_cleanup(&resp.headers);
        return ret;
    }
    if (req_ctx.request_chunked) {
        ret = qm_http_server_handle_error(server, req, &resp, 411);
        qm_http_request_cleanup(&req_ctx);
        qm_http_response_headers_cleanup(&resp.headers);
        return ret;
    }

    route = qm_http_route_find(server, req->method, req->path, &method_exists);
    if (!route) {
        ret = qm_http_server_handle_error(server, req, &resp, method_exists ? 405 : 404);
        qm_http_request_cleanup(&req_ctx);
        qm_http_response_headers_cleanup(&resp.headers);
        return ret;
    }

    ret = route->handler(req, &resp);
    if (ret != QM_EOK && !resp.header_sent) {
        ret = qm_http_server_handle_error(server, req, &resp, 500);
    } else if (!resp.header_sent) {
        ret = qm_http_response_send(&resp, 204, NULL, NULL, 0);
    }

    qm_http_request_cleanup(&req_ctx);
    qm_http_response_headers_cleanup(&resp.headers);
    session->last_active_ms = qm_now_ms();
    return ret;
}

static void qm_http_server_process_connection(qm_http_server_t server,
                                              qm_http_server_connection_t *connection)
{
    qm_http_session_t *session;
    uint32_t timeout_ms;

    session = qm_http_session_add(server, connection->sockfd);
    if (!session) {
        return;
    }

    timeout_ms = server->cfg.timeout_ms ? server->cfg.timeout_ms : QM_HTTP_DEFAULT_CLIENT_TIMEOUT_MS;

    while (server->running && !server->stopping && !session->close_pending) {
        uint32_t header_max = server->cfg.max_header_len ?
                              server->cfg.max_header_len :
                              QM_HTTP_DEFAULT_HEADER_MAX;
        char *header_buf;
        int header_len = 0;
        int ret;

        header_buf = (char *)qm_malloc((unsigned int)(header_max + 1));
        if (!header_buf) {
            break;
        }

        ret = qm_http_read_headers(connection, header_buf, (int)header_max,
                                   &header_len, timeout_ms);
        if (ret == QM_EOK) {
            ret = qm_http_server_handle_request(server, connection, session,
                                                header_buf, header_len);
            if (ret != QM_EOK) {
                session->close_pending = QM_TRUE;
            }
        } else if (ret == -QM_EFULL) {
            qm_http_response_t resp;
            qm_http_request_ctx_t req_ctx;

            qm_memset(&req_ctx, 0, sizeof(req_ctx));
            req_ctx.priv.server = server;
            req_ctx.priv.connection = connection;
            req_ctx.priv.session = session;
            qm_http_response_init(&resp, server, connection, session);
            session->close_pending = QM_TRUE;
            qm_http_server_handle_error(server, &req_ctx.req, &resp, 431);
            qm_http_response_headers_cleanup(&resp.headers);
        } else {
            session->close_pending = QM_TRUE;
        }

        qm_free(header_buf);
    }

    qm_http_session_remove(server, session);
}

static void qm_http_server_task(void *arg)
{
    qm_http_server_t server = (qm_http_server_t)arg;

    if (!server) {
        qm_task_exit(NULL);
        return;
    }

    while (server->running && !server->stopping) {
        qm_http_server_connection_t connection;
        int ret;

        ret = qm_http_server_transport_listener_accept(server->listener,
                                                       &connection,
                                                       QM_HTTP_DEFAULT_ACCEPT_TIMEOUT_MS);
        if (ret == QM_EOK) {
            QM_HTTP_SERVER_LOGI(LOG_TAG, QM_HTTP_SERVER_LOG_LIFECYCLE, "accepted %s client fd=%d ip=%s",
                    server->cfg.scheme == QM_HTTP_SERVER_SCHEME_HTTPS ? "https" : "http",
                    connection.sockfd, connection.peer_ip);
            qm_http_server_process_connection(server, &connection);
            qm_http_server_transport_connection_close(&connection);
        } else if (ret != -QM_ETIMEOUT && server->running && !server->stopping) {
            QM_HTTP_SERVER_LOGW(LOG_TAG, QM_HTTP_SERVER_LOG_CONTROL, "accept failed ret=%d", ret);
            qm_msleep(50);
        }
    }

    qm_http_sessions_mark_close(server);
    server->running = QM_FALSE;
    server->stopping = QM_FALSE;
    qm_task_exit(NULL);
}

qm_http_server_t qm_http_server_create(const qm_http_server_config_t *config)
{
    qm_http_server_t server;

    if (!config || !config->port || config->route_count < 0 ||
        (config->route_count > 0 && !config->routes) ||
        config->scheme > QM_HTTP_SERVER_SCHEME_HTTPS ||
        (config->scheme == QM_HTTP_SERVER_SCHEME_HTTPS &&
         (!config->https_cert_pem || config->https_cert_len <= 0 ||
          !config->https_key_pem || config->https_key_len <= 0))) {
        return NULL;
    }

    server = (qm_http_server_t)qm_calloc(1, sizeof(*server));
    if (!server) {
        return NULL;
    }
    qm_memcpy(&server->cfg, config, sizeof(*config));

    if (!server->cfg.timeout_ms) {
        server->cfg.timeout_ms = CONFIG_QM_HTTP_SERVER_TIMEOUT_MS;
    }
    if (!server->cfg.max_header_len) {
        server->cfg.max_header_len = CONFIG_QM_HTTP_SERVER_HEADER_MAX;
    }
    if (!server->cfg.max_body_len) {
        server->cfg.max_body_len = CONFIG_QM_HTTP_SERVER_BODY_MAX;
    }
    if (!server->cfg.task_stack) {
        server->cfg.task_stack = CONFIG_QM_HTTP_SERVER_STACK;
    }
    if (!server->cfg.task_prio) {
        server->cfg.task_prio = CONFIG_QM_HTTP_SERVER_PRIO;
    }

    if (qm_mutex_new(&server->lock) != QM_EOK) {
        qm_free(server);
        return NULL;
    }
    return server;
}

int qm_http_server_start(qm_http_server_t server)
{
    int ret;

    if (!server) {
        return -QM_EINVAL;
    }
    if (server->running) {
        return -QM_EBUSY;
    }

    ret = qm_http_server_transport_listener_open(&server->cfg, &server->listener);
    if (ret != QM_EOK) {
        QM_HTTP_SERVER_LOGE(LOG_TAG, "%s listen open failed port=%u ret=%d",
                server->cfg.scheme == QM_HTTP_SERVER_SCHEME_HTTPS ? "https" : "http",
                server->cfg.port, ret);
        return ret;
    }

    server->running = QM_TRUE;
    server->stopping = QM_FALSE;
    ret = qm_task_new(&server->task, "qm_http_server", qm_http_server_task, server,
                      server->cfg.task_stack, server->cfg.task_prio);
    if (ret != QM_EOK) {
        server->running = QM_FALSE;
        qm_http_server_transport_listener_close(server->listener);
        server->listener = NULL;
        return -QM_EIO;
    }

    QM_HTTP_SERVER_LOGI(LOG_TAG, QM_HTTP_SERVER_LOG_LIFECYCLE, "%s listen port=%u routes=%d",
            server->cfg.scheme == QM_HTTP_SERVER_SCHEME_HTTPS ? "https" : "http",
            server->cfg.port,
            server->cfg.route_count);
    return QM_EOK;
}

int qm_http_server_stop(qm_http_server_t server)
{
    int i;

    if (!server) {
        return -QM_EINVAL;
    }
    if (!server->running) {
        return QM_EOK;
    }

    server->stopping = QM_TRUE;
    qm_http_sessions_mark_close(server);

    for (i = 0; i < 50 && server->running; i++) {
        qm_msleep(20);
    }
    if (server->running) {
        server->running = QM_FALSE;
    }
    if (server->listener) {
        qm_http_server_transport_listener_close(server->listener);
        server->listener = NULL;
    }

    return QM_EOK;
}

void qm_http_server_destroy(qm_http_server_t server)
{
    if (!server) {
        return;
    }
    qm_http_server_stop(server);
    if (qm_mutex_is_valid(&server->lock)) {
        qm_mutex_free(&server->lock);
    }
    qm_free(server);
}
