#include "qm_types.h"
#include "qm_utils_timer.h"
#include "util_httpc.h"
#include "util_http_debug.h"
#include "util_net.h"
#include "qm_config.h"

#define HTTPCLIENT_MIN(x,y) (((x)<(y))?(x):(y))
#define HTTPCLIENT_MAX(x,y) (((x)>(y))?(x):(y))

#define HTTPCLIENT_AUTHB_SIZE     128

#ifndef CONFIG_HTTPCLIENT_CHUNK_SIZE
#define CONFIG_HTTPCLIENT_CHUNK_SIZE     (1024+1)   /* read payload */
#endif
#define HTTPCLIENT_RAED_HEAD_SIZE 32            /* read header */

#ifndef CONFIG_HTTPCLIENT_SEND_BUF_SIZE
#define CONFIG_HTTPCLIENT_SEND_BUF_SIZE  512          /* send */
#endif

#define HTTPCLIENT_MAX_HOST_LEN   64
#define HTTPCLIENT_MAX_URL_LEN    256


#define HTTP_RETRIEVE_MORE_DATA   (1)            /**< More data needs to be retrieved. */

#if defined(MBEDTLS_DEBUG_C)
    #define DEBUG_LEVEL 2
#endif

static int http_client_parse_host_and_port(const char *url, char *host, uint32_t maxhost_len, int *port);
static int http_client_parse_url(const char *url, char *scheme, uint32_t max_scheme_len, char *host,
                                uint32_t maxhost_len, int *port, char *path, uint32_t max_path_len);
static int http_client_conn(httpclient_t *client);
static int http_client_recv(httpclient_t *client, char *buf, int min_len, int max_len, int *p_read_len,
                           uint32_t timeout);
static int http_client_retrieve_content(httpclient_t *client, char *data, int len, uint32_t timeout,
                                       httpclient_data_t *client_data);
static int http_client_response_parse(httpclient_t *client, char *data, int len, uint32_t timeout,
                                     httpclient_data_t *client_data);

static void http_client_base64enc(char *out, const char *in)
{
    const char code[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=";
    int i = 0, x = 0, l = 0;

    for (; *in; in++) {
        x = x << 8 | *in;
        for (l += 8; l >= 6; l -= 6) {
            out[i++] = code[(x >> (l - 6)) & 0x3f];
        }
    }
    if (l > 0) {
        x <<= 6 - l;
        out[i++] = code[x & 0x3f];
    }
    for (; i % 4;) {
        out[i++] = '=';
    }
    out[i] = '\0';
}

static int http_client_conn(httpclient_t *client)
{
    if (client->net.connect(&client->net) < 0) {
        http_log_err("establish connection failed");
        return ERROR_HTTP_CONN;
    }

    return ERROR_HTTP_SUCCESS_RETURN;
}

static int http_client_parse_url(const char *url, char *scheme, uint32_t max_scheme_len, char *host, uint32_t maxhost_len,
                         int *port, char *path, uint32_t max_path_len)
{
    char *scheme_ptr = (char *) url;
    char *host_ptr = (char *) strstr(url, "://");
    uint32_t host_len = 0;
    uint32_t path_len;
    /* char *port_ptr; */
    char *path_ptr;
    char *port_ptr;
    char port_str[8] = {0};
    uint32_t port_len;
    char *fragment_ptr;

    if (host_ptr == NULL) {
        http_log_err("Could not find host");
        return ERROR_HTTP_PARSE; /* URL is invalid */
    }

    if (max_scheme_len < host_ptr - scheme_ptr + 1) {
        /* including NULL-terminating char */
        http_log_err("Scheme str is too small (%u >= %u)", max_scheme_len, (uint32_t)(host_ptr - scheme_ptr + 1));
        return ERROR_HTTP_PARSE;
    }
    memcpy(scheme, scheme_ptr, host_ptr - scheme_ptr);
    scheme[host_ptr - scheme_ptr] = '\0';

    host_ptr += 3;

    path_ptr = strchr(host_ptr, '/');
    if (NULL == path_ptr) {
        http_log_err("invalid path");
        return -1;
    }
    
    port_ptr = strchr(host_ptr, ':');
    if(!port_ptr){
        *port = 0;
    }else{
        port_len = path_ptr - port_ptr - 1;
        memcpy(port_str, port_ptr + 1, port_len);
        port_str[port_len] = '\0';
        *port = atoi(port_str);
    }

    if (host_len == 0) {
        if(port_ptr){
            host_len = port_ptr - host_ptr;
        }else{
            host_len = path_ptr - host_ptr;
        }
    }

    if (maxhost_len < host_len + 1) {
        /* including NULL-terminating char */
        http_log_err("Host str is too long (host_len(%d) >= max_len(%d))", host_len + 1, maxhost_len);
        return ERROR_HTTP_PARSE;
    }
    memcpy(host, host_ptr, host_len);
    host[host_len] = '\0';

    fragment_ptr = strchr(host_ptr, '#');
    if (fragment_ptr != NULL) {
        path_len = fragment_ptr - path_ptr;
    } else {
        path_len = strlen(path_ptr);
    }

    if (max_path_len < path_len + 1) {
        /* including NULL-terminating char */
        http_log_err("Path str is too small (%d >= %d)", max_path_len, path_len + 1);
        return ERROR_HTTP_PARSE;
    }
    memcpy(path, path_ptr, path_len);
    path[path_len] = '\0';

    return ERROR_HTTP_SUCCESS_RETURN;
}

static int http_client_parse_host_and_port(const char *url, char *host, uint32_t maxhost_len, int *port)
{
    const char *host_ptr = (const char *) strstr(url, "://");
    uint32_t host_len = 0;
    char *path_ptr;
    char *port_ptr;
    char port_str[8] = {0};
    uint32_t port_len;

    if (host_ptr == NULL) {
        http_log_err("Could not find host");
        return ERROR_HTTP_PARSE; /* URL is invalid */
    }
    host_ptr += 3;

    path_ptr = strchr(host_ptr, '/');
    if (NULL == path_ptr) {
        http_log_err("invalid path");
        return ERROR_HTTP_PARSE; /* URL is invalid */
    }

    port_ptr = strchr(host_ptr, ':');
    if(!port_ptr){
        *port = 0;
    }else{
        port_len = path_ptr - port_ptr - 1;
        memcpy(port_str, port_ptr + 1, port_len);
        port_str[port_len] = '\0';
        *port = atoi(port_str);
    }

    if (host_len == 0) {
        if(port_ptr){
            host_len = port_ptr - host_ptr;
        }else{
            host_len = path_ptr - host_ptr;
        }
    }

    if (maxhost_len < host_len + 1) {
        /* including NULL-terminating char */
        http_log_err("Host str is too small (%d >= %d)", maxhost_len, host_len + 1);
        return ERROR_HTTP_PARSE;
    }
    memcpy(host, host_ptr, host_len);
    host[host_len] = '\0';

    return ERROR_HTTP_SUCCESS_RETURN;
}

static int http_client_get_info(httpclient_t *client, char *send_buf, int *send_idx, char *buf,
                        uint32_t len) /* 0 on success, err code on failure */
{
    int ret;
    int cp_len;
    int idx = *send_idx;

    if (len == 0) {
        len = strlen(buf);
    }

    do {
        if ((CONFIG_HTTPCLIENT_SEND_BUF_SIZE - idx) >= len) {
            cp_len = len;
        } else {
            cp_len = CONFIG_HTTPCLIENT_SEND_BUF_SIZE - idx;
        }

        memcpy(send_buf + idx, buf, cp_len);
        idx += cp_len;
        len -= cp_len;

        if (idx == CONFIG_HTTPCLIENT_SEND_BUF_SIZE) {
            ret = client->net.write(&client->net, send_buf, CONFIG_HTTPCLIENT_SEND_BUF_SIZE, 5000);
            if (ret) {
                return (ret);
            }
        }
    } while (len);

    *send_idx = idx;
    return ERROR_HTTP_SUCCESS_RETURN;
}

static void __attribute__((unused)) http_client_set_custom_header(httpclient_t *client, char *header)
{
    client->header = header;
}

static int __attribute__((unused)) http_client_basic_auth(httpclient_t *client, char *user, char *password)
{
    if ((strlen(user) + strlen(password)) >= HTTPCLIENT_AUTHB_SIZE) {
        return ERROR_HTTP;
    }
    client->auth_user = user;
    client->auth_password = password;
    return ERROR_HTTP_SUCCESS_RETURN;
}

static int http_client_send_auth(httpclient_t *client, char *send_buf, int *send_idx)
{
    char b_auth[(int)((HTTPCLIENT_AUTHB_SIZE + 3) * 4 / 3 + 1)];
    char base64buff[HTTPCLIENT_AUTHB_SIZE + 3];

    http_client_get_info(client, send_buf, send_idx, "Authorization: Basic ", 0);
    sprintf(base64buff, "%s:%s", client->auth_user, client->auth_password);
    http_log_debug("bAuth: %s", base64buff) ;
    http_client_base64enc(b_auth, base64buff);
    b_auth[strlen(b_auth) + 1] = '\0';
    b_auth[strlen(b_auth)] = '\n';
    http_log_debug("b_auth:%s", b_auth) ;
    http_client_get_info(client, send_buf, send_idx, b_auth, 0);
    return ERROR_HTTP_SUCCESS_RETURN;
}

static int http_client_send_header(httpclient_t *client, const char *url, int method, httpclient_data_t *client_data)
{
    char scheme[8] = { 0 };
    char host[HTTPCLIENT_MAX_HOST_LEN] = { 0 };
    char path[HTTPCLIENT_MAX_URL_LEN] = { 0 };
    int len;
    char send_buf[CONFIG_HTTPCLIENT_SEND_BUF_SIZE] = { 0 };
    char buf[CONFIG_HTTPCLIENT_SEND_BUF_SIZE] = { 0 };
    char *meth = (method == HTTPCLIENT_GET) ? "GET" : (method == HTTPCLIENT_POST) ? "POST" :
                 (method == HTTPCLIENT_PUT) ? "PUT" : (method == HTTPCLIENT_DELETE) ? "DELETE" :
                 (method == HTTPCLIENT_HEAD) ? "HEAD" : "";
    int ret;
    int port;

    /* First we need to parse the url (http[s]://host[:port][/[path]]) */
    /* int res = httpclient_parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &(client->remote_port), path, sizeof(path)); */
    int res = http_client_parse_url(url, scheme, sizeof(scheme), host, sizeof(host), &port, path, sizeof(path));
    if (res != ERROR_HTTP_SUCCESS_RETURN) {
        http_log_err("httpclient_parse_url returned %d", res);
        return res;
    }

    /* if (client->remote_port == 0) */
    /* { */
    if (strcmp(scheme, "http") == 0) {
        /* client->remote_port = CONFIG_HTTP_PORT; */
    } else if (strcmp(scheme, "https") == 0) {
        /* client->remote_port = CONFIG_HTTPS_PORT; */
    }
    /* } */

    /* Send request */
    memset(send_buf, 0, CONFIG_HTTPCLIENT_SEND_BUF_SIZE);
    len = 0; /* Reset send buffer */

    qm_snprintf(buf, sizeof(buf), "%s %s HTTP/1.1\r\nHost: %s\r\n", meth, path, host); /* Write request */
    ret = http_client_get_info(client, send_buf, &len, buf, strlen(buf));
    if (ret) {
        http_log_err("Could not write request");
        return ERROR_HTTP_CONN;
    }

    /* Send all headers */
    if (client->auth_user) {
        http_client_send_auth(client, send_buf, &len); /* send out Basic Auth header */
    }

    /* Add user header information */
    if (client->header) {
        http_client_get_info(client, send_buf, &len, (char *) client->header, strlen(client->header));
    }

    if (client_data->post_buf != NULL) {
        qm_snprintf(buf, sizeof(buf), "Content-Length: %d\r\n", client_data->post_buf_len);
        http_client_get_info(client, send_buf, &len, buf, strlen(buf));

        if (client_data->post_content_type != NULL) {
            qm_snprintf(buf, sizeof(buf), "Content-Type: %s\r\n", client_data->post_content_type);
            http_client_get_info(client, send_buf, &len, buf, strlen(buf));
        }
    }

    /* Close headers */
    http_client_get_info(client, send_buf, &len, "\r\n", 0);

    /* ret = httpclient_tcp_send_all(client->net.handle, send_buf, len); */
    ret = client->net.write(&client->net, send_buf, len, 5000);
    if (ret > 0) {
        http_log_debug("Written %d bytes", ret);
    } else if (ret == 0) {
        http_log_err("ret == 0,Connection was closed by server");
        return ERROR_HTTP_CLOSED; /* Connection was closed by server */
    } else {
        http_log_err("Connection error (send returned %d)", ret);
        return ERROR_HTTP_CONN;
    }

    return ERROR_HTTP_SUCCESS_RETURN;
}

static int http_client_send_userdata(httpclient_t *client, httpclient_data_t *client_data)
{
    int ret = 0;

    if (client_data->post_buf && client_data->post_buf_len) {
        http_log_debug("client_data->post_buf: %s", client_data->post_buf);
        {
            /* ret = httpclient_tcp_send_all(client->handle, (char *)client_data->post_buf, client_data->post_buf_len); */
            ret = client->net.write(&client->net, (char *)client_data->post_buf, client_data->post_buf_len, 5000);
            if (ret > 0) {
                http_log_debug("Written %d bytes", ret);
            } else if (ret == 0) {
                http_log_err("ret == 0,Connection was closed by server");
                return ERROR_HTTP_CLOSED; /* Connection was closed by server */
            } else {
                http_log_err("Connection error (send returned %d)", ret);
                return ERROR_HTTP_CONN;
            }
        }
    }

    return ERROR_HTTP_SUCCESS_RETURN;
}

/* 0 on success, err code on failure */
static int http_client_recv(httpclient_t *client, char *buf, int min_len, int max_len, int *p_read_len, uint32_t timeout_ms)
{
    int ret = 0;
    qm_utils_time_t timer;

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, timeout_ms);

    *p_read_len = 0;

    ret = client->net.read(&client->net, buf, max_len, qm_utils_time_left(&timer));
    /* http_log_debug("Recv: | %s", buf); */

    if (ret > 0) {
        *p_read_len = ret;
    } else if (ret == 0) {
        /* timeout */
        return ERROR_HTTP_FAIL_RETURN;
    } else if (-1 == ret) {
        http_log_info("Connection closed.");
        return ERROR_HTTP_CONN;
    } else {
        http_log_err("Connection error (recv returned %d)", ret);
        return ERROR_HTTP_CONN;
    }
    http_log_info("%u bytes has been read", *p_read_len);
    return 0;
}

static int http_client_retrieve_content(httpclient_t *client, char *data, int len,
                                uint32_t timeout_ms, httpclient_data_t *client_data)
{
    int count = 0;
    int templen = 0;
    int crlf_pos;
    qm_utils_time_t timer;

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, timeout_ms);

    /* Receive data */
    client_data->is_more = HTTP_TRUE;

    /* the header is not received finished */
    if (client_data->response_content_len == -1 && client_data->is_chunked == HTTP_FALSE) {
        /* can not enter this if */
        while (1) {
            int ret, max_len;
            if (count + len < client_data->response_buf_len - 1) {
                memcpy(client_data->response_buf + count, data, len);
                count += len;
                client_data->response_buf[count] = '\0';
            } else {
                memcpy(client_data->response_buf + count, data, client_data->response_buf_len - 1 - count);
                client_data->response_buf[client_data->response_buf_len - 1] = '\0';
                return HTTP_RETRIEVE_MORE_DATA;
            }

            /* try to read more header */
            max_len = HTTPCLIENT_MIN(HTTPCLIENT_RAED_HEAD_SIZE, client_data->response_buf_len - 1 - count);
            ret = http_client_recv(client, data, 1, max_len, &len, qm_utils_time_left(&timer));

            /* Receive data */
            http_log_debug("data len: %d %d", len, count);

            if (ret == ERROR_HTTP_CONN) {
                http_log_debug("ret == ERROR_HTTP_CONN");
                return ret;
            }

            if (len == 0) {
                /* read no more data */
                http_log_debug("no more len == 0");
                client_data->is_more = HTTP_FALSE;
                return ERROR_HTTP_SUCCESS_RETURN;
            }
        }
    }

    while (1) {
        uint32_t readLen = 0;

        if (client_data->is_chunked && client_data->retrieve_len <= 0) {
            /* Read chunk header */
            int foundCrlf;
            int n;
            do {
                foundCrlf = HTTP_FALSE;
                crlf_pos = 0;
                data[len] = 0;
                if (len >= 2) {
                    for (; crlf_pos < len - 2; crlf_pos++) {
                        if (data[crlf_pos] == '\r' && data[crlf_pos + 1] == '\n') {
                            foundCrlf = HTTP_TRUE;
                            break;
                        }
                    }
                }
                if (!foundCrlf) {
                    /* Try to read more */
                    if (len < CONFIG_HTTPCLIENT_CHUNK_SIZE) {
                        int new_trf_len, ret;
                        ret = http_client_recv(client,
                                              data + len,
                                              0,
                                              1,
                                              &new_trf_len,
                                              qm_utils_time_left(&timer));
                        len += new_trf_len;
                        if (ret == ERROR_HTTP_CONN) {
                            return ret;
                        } else {
                            continue;
                        }
                    } else {
                        return ERROR_HTTP;
                    }
                }
            } while (!foundCrlf);
            data[crlf_pos] = '\0';

            /* chunk length */
            /* n = sscanf(data, "%x", &readLen); */

            readLen = strtoul(data, NULL, 16);
            n = (0 == readLen) ? 0 : 1;
            client_data->retrieve_len = readLen;
            client_data->response_content_len += client_data->retrieve_len;
            if (readLen == 0) {
                /* Last chunk */
                client_data->is_more = HTTP_FALSE;
                http_log_debug("no more (last chunk)");
                break;
            }

            if (n != 1) {
                http_log_err("Could not read chunk length");
                return ERROR_HTTP_UNRESOLVED_DNS;
            }

            memmove(data, &data[crlf_pos + 2], len - (crlf_pos + 2)); /* Not need to move NULL-terminating char any more */
            len -= (crlf_pos + 2);
        } else {
            /*readLen = client_data->retrieve_len; */
            readLen = client_data->retrieve_len;
        }

        http_log_debug("Total-Payload: %d Bytes; Read: %d Bytes", readLen, len);

        do {
            templen = HTTPCLIENT_MIN(len, readLen);
            if (count + templen < client_data->response_buf_len - 1) {
                memcpy(client_data->response_buf + count, data, templen);
                count += templen;
                client_data->response_buf[count] = '\0';
                client_data->retrieve_len -= templen;
            } else {
                memcpy(client_data->response_buf + count, data, client_data->response_buf_len - 1 - count);
                client_data->response_buf[client_data->response_buf_len - 1] = '\0';
                client_data->retrieve_len -= (client_data->response_buf_len - 1 - count);
                return HTTP_RETRIEVE_MORE_DATA;
            }

            if (len > readLen) {
                http_log_debug("memmove %d %d %d\n", readLen, len, client_data->retrieve_len);
                memmove(data, &data[readLen], len - readLen); /* chunk case, read between two chunks */
                len -= readLen;
                readLen = 0;
                client_data->retrieve_len = 0;
            } else {
                readLen -= len;
            }

            if (readLen) {
                int ret;
                int max_len = HTTPCLIENT_MIN(CONFIG_HTTPCLIENT_CHUNK_SIZE - 1, client_data->response_buf_len - 1 - count);
                max_len = HTTPCLIENT_MIN(max_len, readLen);
                http_log_debug("read more len %d", max_len);
                ret = http_client_recv(client, data, 1, max_len, &len, qm_utils_time_left(&timer));
                if (ret == ERROR_HTTP_CONN || ret == ERROR_HTTP_FAIL_RETURN) {
                    return ret;
                }
                    
            }else{
                len = 0;
            }
        } while (readLen);

        if (client_data->is_chunked) {
            if (len < 2) {
                int new_trf_len, ret;
                /* Read missing chars to find end of chunk */
                ret = http_client_recv(client, data + len, 2, 2, &new_trf_len,
                                      qm_utils_time_left(&timer));
                if (ret == ERROR_HTTP_CONN) {
                    return ret;
                }
                len += new_trf_len;
            
                if ((data[0] != '\r') || (data[1] != '\n')) {
                    http_log_err("Format error, %s", data); /* after memmove, the beginning of next chunk */
                    return ERROR_HTTP_UNRESOLVED_DNS;
                }
                memmove(data, &data[2],  2); /* remove the \r\n */
                len -= 2;
            }
        } else {
            http_log_debug("no more (content-length)");
            client_data->is_more = HTTP_FALSE;
            break;
        }

    }

    return ERROR_HTTP_SUCCESS_RETURN;
}

static int http_client_response_parse(httpclient_t *client, char *data, int len, uint32_t timeout_ms,
                              httpclient_data_t *client_data)
{
    int crlf_pos;
    qm_utils_time_t timer;
    char *tmp_ptr, *ptr_body_end;
    
    int new_trf_len, ret;

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, timeout_ms);

    client_data->response_content_len = -1;

    /* http client response */
    /* <status-line> HTTP/1.1 200 OK(CRLF)

       <headers> ...(CRLF)

       <blank line> (CRLF)

      [<response-body>] */
    char *crlf_ptr = strstr(data, "\r\n");
    if (crlf_ptr == NULL) {
        http_log_err("\r\n not found");
        return ERROR_HTTP_UNRESOLVED_DNS;
    }

    crlf_pos = crlf_ptr - data;
    data[crlf_pos] = '\0';

    /* Parse HTTP response */
#if 0
    if (sscanf(data, "HTTP/%*d.%*d %d %*[^\r\n]", &(client->response_code)) != 1) {
        /* Cannot match string, error */
        http_log_err("Not a correct HTTP answer : %s\n", data);
        return ERROR_HTTP_UNRESOLVED_DNS;
    }
#endif

    client->response_code = atoi(data + 9);

    if ((client->response_code < 200) || (client->response_code >= 400)) {
        /* Did not return a 2xx code; TODO fetch headers/(&data?) anyway and implement a mean of writing/reading headers */
        QM_LOGW("http","Response code %d", client->response_code);
    }

    memmove(data, &data[crlf_pos + 2], len - (crlf_pos + 2) + 1); /* Be sure to move NULL-terminating char as well */
    len -= (crlf_pos + 2);       /* remove status_line length */

    client_data->is_chunked = HTTP_FALSE;

    /*If not ending of response body*/
    /* try to read more header again until find response head ending "\r\n\r\n" */
    while(NULL == (ptr_body_end = strstr(data, "\r\n\r\n"))) {
        /* try to read more header */
        if(len+HTTPCLIENT_RAED_HEAD_SIZE>=CONFIG_HTTPCLIENT_CHUNK_SIZE){
                http_log_debug("http header lenth is too big");
                return ERROR_HTTP;
            }
        ret = http_client_recv(client, data + len, 1, HTTPCLIENT_RAED_HEAD_SIZE, &new_trf_len, qm_utils_time_left(&timer));
        if (ret == ERROR_HTTP_CONN) {
            return ret;
        }
        len += new_trf_len;
        data[len] = '\0';
    }

    http_log_debug("Reading headers:\r\n %s\r\n", data);

    /* parse response_content_len */
    if (NULL != (tmp_ptr = strstr(data, "Content-Length")) || NULL != (tmp_ptr = strstr(data, "content-length"))) {
        client_data->response_content_len = atoi(tmp_ptr + strlen("Content-Length: "));
        client_data->retrieve_len = client_data->response_content_len;
        http_log_debug("Content-Length %d", client_data->response_content_len);
    } else if (NULL != (tmp_ptr = strstr(data, "Transfer-Encoding"))) {
        int len_chunk = strlen("Chunked");
        char *chunk_value = tmp_ptr + strlen("Transfer-Encoding: ");

        if ((! memcmp(chunk_value, "Chunked", len_chunk))
            || (! memcmp(chunk_value, "chunked", len_chunk))) {
            http_log_debug("Chunked");
            client_data->is_chunked = HTTP_TRUE;
            client_data->response_content_len = 0;
            client_data->retrieve_len = 0;
        }
    } else {
        http_log_err("Could not parse header");
        return ERROR_HTTP;
    }
    
    /* remove header length */
    /* len is Had read body's length */
    /* if client_data->response_content_len != 0, it is know response length */
    /* the remain length is client_data->response_content_len - len */
    len = len - (ptr_body_end + 4 - data);   
    memmove(data, ptr_body_end + 4, len + 1);
    client_data->response_received_len += len;
    return http_client_retrieve_content(client, data, len, qm_utils_time_left(&timer), client_data);
}

static int http_client_connect(httpclient_t *client)
{
    int ret = ERROR_HTTP_CONN;

    client->net.handle = 0;
    ret = http_client_conn(client);
    return ret;
}

static int http_client_send_request(httpclient_t *client, const char *url, HTTPCLIENT_REQUEST_TYPE method,
                            httpclient_data_t *client_data)
{
    int ret = ERROR_HTTP_CONN;

    if (0 == client->net.handle) {
        http_log_debug("not connection have been established");
        return ret;
    }

    ret = http_client_send_header(client, url, method, client_data);
    if (ret != 0) {
        http_log_err("http_client_send_header is error,ret = %d", ret);
        return ret;
    }

    if (method == HTTPCLIENT_POST || method == HTTPCLIENT_PUT) {
        ret = http_client_send_userdata(client, client_data);
    }

    return ret;
}

int http_client_recv_response(httpclient_t *client, uint32_t timeout_ms, httpclient_data_t *client_data)
{
    int reclen = 0, ret = ERROR_HTTP_CONN;
    char *buf = NULL;
    qm_utils_time_t timer;

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, timeout_ms);

    if (0 == client->net.handle) {
        http_log_debug("not connection have been established");
        return ret;
    }

    buf = (char*)qm_malloc(CONFIG_HTTPCLIENT_CHUNK_SIZE);
    if (!buf) {
        return ret;
    }

    if (client_data->is_more) {
        client_data->response_buf[0] = '\0';
        ret = http_client_retrieve_content(client, buf, reclen, qm_utils_time_left(&timer), client_data);
    } else {
        client_data->is_more = 1;
        /* try to read header */
        ret = http_client_recv(client, buf, 1, HTTPCLIENT_RAED_HEAD_SIZE, &reclen, qm_utils_time_left(&timer));
        if (ret != 0) {
           goto __exit;
        }

        buf[reclen] = '\0';

        if (reclen) {
            ret = http_client_response_parse(client, buf, reclen, qm_utils_time_left(&timer), client_data);
        }
    }

 __exit:   
    qm_free(buf);
    buf = NULL;
    return ret;
}

void http_client_close(httpclient_t *client)
{
    if ((int)client->net.handle > 0) {
        client->net.disconnect(&client->net);
    }
    client->net.handle = 0;
    http_log_debug("client disconnected");
}

int http_client_common(httpclient_t *client, const char *url, int port, const char *ca_crt,
                      HTTPCLIENT_REQUEST_TYPE method, uint32_t timeout_ms, httpclient_data_t *client_data)
{
    qm_utils_time_t timer;
    int ret = 0;
    char host[HTTPCLIENT_MAX_HOST_LEN] = { 0 };
    int m_port = 0;

    http_client_parse_host_and_port(url, host, sizeof(host), &m_port);
    if(m_port == 0){
        m_port = port;
    }
    http_log_debug("host: '%s', port: %d", host, m_port);

    if (0 == client->net.handle) {
        /* Establish connection if no. */
        ret = util_net_init(&client->net, host, (uint16_t)m_port, ca_crt);
        if (0 != ret) {
            return ret;
        }

        ret = http_client_connect(client);
        if (0 != ret) {
            http_log_err("http_client_connect is error, ret = %d", ret);
            http_client_close(client);
            return ret;
        }
    }

    if (!client_data->is_more) {
        ret = http_client_send_request(client, url, method, client_data);
        if (0 != ret) {
            http_log_err("http_client_send_request is error, ret = %d", ret);
            http_client_close(client);
            return ret;
        }
    }

    qm_utils_time_init(&timer);
    qm_utils_time_countdown_ms(&timer, timeout_ms);

    if ((NULL != client_data->response_buf)
        && (0 != client_data->response_buf_len)) {
        ret = http_client_recv_response(client, qm_utils_time_left(&timer), client_data);
        if (ret < 0) {
            http_log_err("http_client_recv_response is error,ret = %d", ret);
            http_client_close(client);
            return ret;
        }
    }

    return 0;
}

int util_get_response_code(httpclient_t *client)
{
    return client->response_code;
}

int http_client_post(httpclient_t *client,
              const char *url,
              int port,
              const char *ca_crt,
              httpclient_data_t *client_data)
{
    /* return http_client_common(client, url, port, ca_crt, HTTPCLIENT_POST, timeout_ms, client_data); */
    int ret = ERROR_HTTP;
    int m_port = 0;
    char host[HTTPCLIENT_MAX_HOST_LEN] = { 0 };

    http_client_parse_host_and_port(url, host, sizeof(host), &m_port);
    http_log_debug("host: '%s', port: %d", host, port);

    if(m_port == 0){
        m_port = port;
    }

    if (0 == client->net.handle) {
        /* Establish connection if no. */
        ret = util_net_init(&client->net, host, (uint16_t)m_port, ca_crt);
        if (0 != ret) {
            return ret;
        }

        ret = http_client_connect(client);
        if (0 != ret) {
            http_log_err("http_client_connect is error, ret = %d", ret);
            http_client_close(client);
            return ret;
        }
    }

    ret = http_client_send_request(client, url, HTTPCLIENT_POST, client_data);
    if (0 != ret) {
        http_log_err("http_client_send_request is error, ret = %d", ret);
        http_client_close(client);
        return ret;
    }

    return ret;
}

