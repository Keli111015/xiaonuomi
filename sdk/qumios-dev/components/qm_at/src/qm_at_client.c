#include "qm_at.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_log.h"
#include "qm_config.h"
#include "qm_utils_timer.h"


#define LOG_TAG              "at.clnt"

static volatile uint32_t g_qm_at_log_flags = QM_AT_KCONFIG_LOG_FLAGS;

uint32_t qm_at_get_log_flags(void)
{
    return g_qm_at_log_flags;
}

int qm_at_set_log_flags(uint32_t log_flags)
{
    if ((log_flags & ~((uint32_t)QM_AT_LOG_ALL)) != 0U) {
        return -QM_EINVAL;
    }
    g_qm_at_log_flags = log_flags;
    return QM_EOK;
}

#if CONFIG_QM_AT_CLIENT

#define QM_AT_RESP_END_OK                 "OK"
#define QM_AT_RESP_END_ERROR              "ERROR"
#define QM_AT_RESP_END_FAIL               "FAIL"
#define QM_AT_END_CR_LF                   "\r\n"

static qm_at_client_t at_client_table[CONFIG_QM_AT_CLIENT_MAX_NUM] = { 0 };

extern int32_t qm_at_vprintfln(void *channel, const char *format, va_list args);
extern void qm_at_print_raw_cmd(const char *type, const char *cmd, uint32_t size);

/**
 * Create response object.
 *
 * @param buf_size the maximum response buffer size
 * @param line_num the number of setting response lines
 *         = 0: the response data will auto return when received 'OK' or 'ERROR'
 *        != 0: the response data will return when received setting lines number data
 * @param timeout the maximum response time
 *
 * @return != NULL: response object
 *          = NULL: no memory
 */
qm_at_response_t *qm_at_client_create_resp(uint32_t buf_size, uint32_t line_num, int32_t timeout)
{
    qm_at_response_t *resp = NULL;

    resp = (qm_at_response_t*)qm_malloc(sizeof(qm_at_response_t));
    if (resp == NULL)
    {
        QM_AT_LOGE(LOG_TAG, "AT create response object failed! No memory for response object!");
        return NULL;
    }

    resp->buf = (char *) qm_malloc(buf_size);
    if (resp->buf == NULL)
    {
        QM_AT_LOGE(LOG_TAG, "AT create response object failed! No memory for response buffer!");
        qm_free(resp);
        return NULL;
    }

    resp->buf_size = buf_size;
    resp->line_num = line_num;
    resp->line_counts = 0;
    resp->timeout = timeout;

    return resp;
}

/**
 * Delete and free response object.
 *
 * @param resp response object
 */
void qm_at_client_delete_resp(qm_at_response_t *resp)
{
    if (resp && resp->buf)
    {
        qm_free(resp->buf);
    }

    if (resp)
    {
        qm_free(resp);
        resp = NULL;
    }
}

/**
 * Set response object information
 *
 * @param resp response object
 * @param buf_size the maximum response buffer size
 * @param line_num the number of setting response lines
 *         = 0: the response data will auto return when received 'OK' or 'ERROR'
 *        != 0: the response data will return when received setting lines number data
 * @param timeout the maximum response time
 *
 * @return  != NULL: response object
 *           = NULL: no memory
 */
qm_at_response_t *qm_at_client_resp_set_info(qm_at_response_t *resp, uint32_t buf_size, uint32_t line_num, int32_t timeout)
{
    char *p_temp;
    QM_ASSERT(resp);

    if (resp->buf_size != buf_size)
    {
        resp->buf_size = buf_size;

        p_temp = (char *) qm_realloc(resp->buf, buf_size);
        if (p_temp == NULL)
        {
            QM_AT_LOGD(LOG_TAG, QM_AT_LOG_CONTROL, "No memory for realloc response buffer size(%d).", buf_size);
            return NULL;
        }
        else
        {
            resp->buf = p_temp;
        }
    }

    resp->line_num = line_num;
    resp->timeout = timeout;

    return resp;
}

/**
 * Get one line AT response buffer by line number.
 *
 * @param resp response object
 * @param resp_line line number, start from '1'
 *
 * @return != NULL: response line buffer
 *          = NULL: input response line error
 */
const char *qm_at_client_resp_get_line(qm_at_response_t *resp, uint32_t resp_line)
{
    char *resp_buf = resp->buf;
    char *resp_line_buf = NULL;
    uint32_t line_num = 1;

    QM_ASSERT(resp);

    if (resp_line > resp->line_counts || resp_line <= 0)
    {
        QM_AT_LOGE(LOG_TAG, "AT response get line failed! Input response line(%d) error!", resp_line);
        return NULL;
    }

    for (line_num = 1; line_num <= resp->line_counts; line_num++)
    {
        if (resp_line == line_num)
        {
            resp_line_buf = resp_buf;

            return resp_line_buf;
        }

        resp_buf += strlen(resp_buf) + 1;
    }

    return NULL;
}

/**
 * Get one line AT response buffer by keyword
 *
 * @param resp response object
 * @param keyword query keyword
 *
 * @return != NULL: response line buffer
 *          = NULL: no matching data
 */
const char *qm_at_client_resp_get_line_by_kw(qm_at_response_t *resp, const char *keyword)
{
    char *resp_buf = resp->buf;
    char *resp_line_buf = NULL;
    uint32_t line_num = 1;

    QM_ASSERT(resp);
    QM_ASSERT(keyword);

    for (line_num = 1; line_num <= resp->line_counts; line_num++)
    {
        if (strstr(resp_buf, keyword))
        {
            resp_line_buf = resp_buf;

            return resp_line_buf;
        }

        resp_buf += strlen(resp_buf) + 1;
    }

    return NULL;
}

/**
 * Get and parse AT response buffer arguments by line number.
 *
 * @param resp response object
 * @param resp_line line number, start from '1'
 * @param resp_expr response buffer expression
 *
 * @return -1 : input response line number error or get line buffer error
 *          0 : parsed without match
 *         >0 : the number of arguments successfully parsed
 */
int32_t qm_at_client_resp_parse_line_args(qm_at_response_t *resp, uint32_t resp_line, const char *resp_expr, ...)
{
    va_list args;
    int32_t resp_args_num = 0;
    const char *resp_line_buf = NULL;

    QM_ASSERT(resp);
    QM_ASSERT(resp_expr);

    if ((resp_line_buf = qm_at_client_resp_get_line(resp, resp_line)) == NULL)
    {
        return -1;
    }

    va_start(args, resp_expr);

    resp_args_num = vsscanf(resp_line_buf, resp_expr, args);

    va_end(args);

    return resp_args_num;
}

/**
 * Get and parse AT response buffer arguments by keyword.
 *
 * @param resp response object
 * @param keyword query keyword
 * @param resp_expr response buffer expression
 *
 * @return -1 : input keyword error or get line buffer error
 *          0 : parsed without match
 *         >0 : the number of arguments successfully parsed
 */
int32_t qm_at_client_resp_parse_line_args_by_kw(qm_at_response_t *resp, const char *keyword, const char *resp_expr, ...)
{
    va_list args;
    int32_t resp_args_num = 0;
    const char *resp_line_buf = NULL;

    QM_ASSERT(resp);
    QM_ASSERT(resp_expr);

    if ((resp_line_buf = qm_at_client_resp_get_line_by_kw(resp, keyword)) == NULL)
    {
        return -1;
    }

    va_start(args, resp_expr);

    resp_args_num = vsscanf(resp_line_buf, resp_expr, args);

    va_end(args);

    return resp_args_num;
}

/**
 * Send commands to AT server and wait response.
 *
 * @param client current AT client object
 * @param resp AT response object, using NULL when you don't care response
 * @param cmd_expr AT commands expression
 *
 * @return 0 : success
 *        -1 : response status error
 *        -2 : wait timeout
 *        -7 : enter AT CLI mode
 */
int32_t qm_at_client_obj_exec_cmd(qm_at_client_t *client, qm_at_response_t *resp, const char *cmd_expr, ...)
{
    va_list args;
    qm_err_t result = QM_EOK;

    QM_ASSERT(cmd_expr);

    if (client == NULL)
    {
        QM_AT_LOGE(LOG_TAG, "input AT Client object is NULL, please create or get AT Client object!");
        return -QM_ERROR;
    }

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);

    client->resp_status = QM_AT_RESP_OK;

    if (resp != NULL)
    {
        resp->buf_len = 0;
        resp->line_counts = 0;
    }

    client->resp = resp;

    va_start(args, cmd_expr);
    qm_at_vprintfln((void*)&client->channel, cmd_expr, args);
    va_end(args);

    if (resp != NULL)
    {
        if (qm_sem_wait(&client->resp_notice, resp->timeout) != QM_EOK)
        {
            QM_AT_LOGW(LOG_TAG, QM_AT_LOG_CONTROL, "execute command timeout (%d ticks)!", resp->timeout);
            client->resp_status = QM_AT_RESP_TIMEOUT;
            result = -QM_ETIMEOUT;
            goto __exit;
        }
        if (client->resp_status != QM_AT_RESP_OK)
        {
            QM_AT_LOGE(LOG_TAG, "execute command failed!");
            result = -QM_ERROR;
            goto __exit;
        }
    }

__exit:
    client->resp = NULL;

    qm_mutex_unlock(&client->lock);

    return result;
}

/**
 * Waiting for connection to external devices.
 *
 * @param client current AT client object
 * @param timeout millisecond for timeout
 *
 * @return 0 : success
 *        -2 : timeout
 *        -5 : no memory
 */
int32_t qm_at_client_obj_wait_connect(qm_at_client_t *client, uint32_t timeout)
{
    qm_err_t result = QM_EOK;
    qm_at_response_t *resp = NULL;
    qm_utils_time_t deadline;

    if (client == NULL)
    {
        QM_AT_LOGE(LOG_TAG, "input AT client object is NULL, please create or get AT Client object!");
        return -QM_ERROR;
    }

    resp = qm_at_client_create_resp(64, 0, 300);
    if (resp == NULL)
    {
        QM_AT_LOGE(LOG_TAG, "no memory for AT client response object.");
        return -QM_ENOMEM;
    }

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    client->resp = resp;

    qm_utils_time_countdown_ms(&deadline, timeout);

    while (1)
    {
        /* Check whether it is timeout */
        if (qm_utils_time_is_expired(&deadline))
        {
            QM_AT_LOGE(LOG_TAG, "wait AT connect timeout(%d tick).", timeout);
            result = -QM_ETIMEOUT;
            break;
        }

        /* Check whether it is already connected */
        resp->buf_len = 0;
        resp->line_counts = 0;

        client->channel.send(client->channel.handle, "AT\r\n", 4);

        if (qm_sem_wait(&client->resp_notice, resp->timeout) != QM_EOK)
            continue;
        else
            break;
    }

    qm_at_client_delete_resp(resp);

    client->resp = NULL;

    qm_mutex_unlock(&client->lock);

    return result;
}

/**
 * Send data to AT server, send data don't have end sign(eg: \r\n).
 *
 * @param client current AT client object
 * @param buf   send data buffer
 * @param size  send fixed data size
 *
 * @return >0: send data size
 *         =0: send failed
 */
int32_t qm_at_client_obj_send(qm_at_client_t *client, char *buf, uint32_t size)
{
    int32_t len;

    QM_ASSERT(buf);

    if (client == NULL)
    {
        QM_AT_LOGE(LOG_TAG, "input AT Client object is NULL, please create or get AT Client object!");
        return 0;
    }

#if CONFIG_QM_AT_PRINT_RAW_CMD
    qm_at_print_raw_cmd("sendline", buf, size);
#endif

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);

    len = client->channel.send(client->channel.handle, buf, size);

    qm_mutex_unlock(&client->lock);

    return len;
}

static qm_err_t qm_at_client_getchar(qm_at_client_t *client, char *ch, int32_t timeout)
{
    int ret = 0;
    ret = client->channel.recv(client->channel.handle, ch, 1, timeout);
    if(ret == 1){
        return QM_EOK;
    }else if(ret == 0){
        return -QM_ETIMEOUT;
    }else {
        return -QM_EIO;
    }
}

/**
 * AT client receive fixed-length data.
 *
 * @param client current AT client object
 * @param buf   receive data buffer
 * @param size  receive fixed data size
 * @param timeout  receive data timeout (ms)
 *
 * @note this function can only be used in execution function of URC data
 *
 * @return >0: receive data size
 *         =0: receive failed
 */
int32_t qm_at_client_obj_recv(qm_at_client_t *client, char *buf, uint32_t size, int32_t timeout)
{
    int32_t len = 0;
    int32_t read_len;

    QM_ASSERT(buf);

    if (client == NULL)
    {
        QM_AT_LOGE(LOG_TAG, "input AT Client object is NULL, please create or get AT Client object!");
        return 0;
    }

    while (1)
    {
        read_len = client->channel.recv(client->channel.handle, buf + len, size, timeout);
        if(read_len > 0)
        {
            len += read_len;
            size -= read_len;
            if(size == 0)
                break;
        }
    }

#ifdef AT_PRINT_RAW_CMD
    qm_at_print_raw_cmd("urc_recv", buf, len);
#endif

    return len;
}

/**
 *  AT client set end sign.
 *
 * @param client current AT client object
 * @param ch the end sign, can not be used when it is '\0'
 */
void qm_at_client_obj_set_end_sign(qm_at_client_t *client, char ch)
{
    if (client == NULL)
    {
        QM_AT_LOGE(LOG_TAG, "input AT Client object is NULL, please create or get AT Client object!");
        return;
    }

    client->end_sign = ch;
}

static qm_err_t qm_check_urc_tbl(qm_at_urc_t *urc_tbl, uint16_t urc_num)
{
    uint16_t i = 0, j = 0;

    for (i = 0; i < urc_num; i++) {
        if (strlen(urc_tbl[i].cmd_prefix) >= CONFIG_QM_AT_CMD_NAME_MAX_LEN ||
            strlen(urc_tbl[i].cmd_suffix) >= CONFIG_QM_AT_CMD_NAME_MAX_LEN) {
            return -QM_EINVAL;
        }

        for (j = 0; j < urc_num; j++) {
            if (i == j) {
                continue;
            }

            if (((strlen(urc_tbl[j].cmd_prefix) == strlen(urc_tbl[i].cmd_prefix)) &&
                (strlen(urc_tbl[j].cmd_suffix) == strlen(urc_tbl[i].cmd_suffix)) &&
                (strcmp(urc_tbl[j].cmd_prefix, urc_tbl[i].cmd_prefix) == 0) &&
                (strcmp(urc_tbl[j].cmd_suffix, urc_tbl[i].cmd_suffix) == 0)) ||
                ((urc_tbl[j].func != NULL) && (urc_tbl[j].func == urc_tbl[i].func))) {
                return -QM_EINVAL;
            }
        }
    }

    return QM_EOK;
}

static qm_err_t qm_check_urc_name_and_callback(qm_at_urc_table_t *urc_table, uint8_t tbl_index, 
                                                                qm_at_urc_t *urc_tbl,
                                                                uint16_t urc_num)
{
    uint16_t i = 0, j =0;
    qm_at_urc_t *urc_func = NULL;

    for (i = 0; i < urc_table->at_urc_num[tbl_index]; i++) {
        urc_func = (qm_at_urc_t *)((urc_table->at_urc_table[tbl_index] + i));

        for (j = 0; j < urc_num; j++) {
            if (((strlen(urc_func->cmd_prefix) == strlen(urc_tbl[j].cmd_prefix)) &&
                (strlen(urc_func->cmd_suffix) == strlen(urc_tbl[j].cmd_suffix)) &&
                (strcmp(urc_func->cmd_prefix, urc_tbl[j].cmd_prefix) == 0) &&
                (strcmp(urc_func->cmd_suffix, urc_tbl[j].cmd_suffix) == 0)) ||
                ((urc_func->func != NULL) && (urc_func->func == urc_tbl[j].func))) {
                return -QM_EINVAL;
            }
        }
    }

    return QM_EOK;
}
/**
 * set URC(Unsolicited Result Code) table
 *
 * @param client current AT client object
 * @param table URC table
 * @param size table size
 */
int32_t qm_at_client_obj_register_urc(qm_at_client_t *client, qm_at_urc_t *urc_tbl, uint32_t urc_num)
{
    uint16_t i = 0;
    qm_err_t ret = QM_EOK;
    qm_at_urc_table_t *urc_table = NULL;

    if (client == NULL || urc_tbl == NULL || urc_num == 0) {
        return QM_EINVAL;
    }

    ret = qm_check_urc_tbl(urc_tbl, urc_num);
    if (ret != QM_EOK) {
        return ret;
    }

    urc_table = &client->urc_table;
    for (i = 0; i < CONFIG_QM_AT_CMD_TABLE_MAX_NUM; i++) {
        if ((urc_table->at_urc_table[i] == NULL) || (urc_table->at_urc_num[i] == 0)) {
            urc_table->at_urc_table[i] = urc_tbl;
            urc_table->at_urc_num[i] = urc_num;
            urc_table->urc_num += urc_num;
            ret = QM_EOK;
            break;
        }

        ret = qm_check_urc_name_and_callback(urc_table, i, urc_tbl, urc_num);
        if (ret != QM_EOK) {
            break;
        }
    }

    return ret;
}

/**
 * get first AT client object in the table.
 *
 * @return AT client object
 */
qm_at_client_t *qm_at_client_get_first(void)
{
    if (at_client_table[0].recv_line_buf == NULL)
    {
        return NULL;
    }

    return &at_client_table[0];
}

static qm_at_urc_t *qm_at_client_get_urc_obj(qm_at_client_t *client)
{
    uint16_t i = 0, j = 0;
    uint32_t prefix_len = 0, suffix_len = 0;
    uint32_t bufsz = 0;
    char *buffer = NULL;
    qm_at_urc_t *urc_func = NULL;
    qm_at_urc_table_t *urc_table = NULL;

    buffer = client->recv_line_buf;
    bufsz = client->recv_line_len;

    urc_table = &client->urc_table;

    for (i = 0; i < CONFIG_QM_AT_URC_TABLE_MAX_NUM; i++){

        for (j = 0; j < urc_table->at_urc_num[i]; j++){

            urc_func = (qm_at_urc_t *)(urc_table->at_urc_table[i] + j);

            prefix_len = strlen(urc_func->cmd_prefix);
            suffix_len = strlen(urc_func->cmd_suffix);
            if (bufsz < prefix_len + suffix_len)
            {
                continue;
            }
            if ((prefix_len ? !strncmp(buffer, urc_func->cmd_prefix, prefix_len) : 1)
                    && (suffix_len ? !strncmp(buffer + bufsz - suffix_len, urc_func->cmd_suffix, suffix_len) : 1))
            {
                return urc_func;
            }
        }
    }

    return NULL;
}

static int qm_at_recv_readline(qm_at_client_t *client)
{
    qm_err_t ret = QM_EOK;
    uint32_t read_len = 0;
    char ch = 0, last_ch = 0;
    uint8_t is_full = QM_FALSE;

    memset(client->recv_line_buf, 0x00, client->recv_buf_len);
    client->recv_line_len = 0;

    while (1)
    {
        ret = qm_at_client_getchar(client, &ch, 1000);
        if(ret == -QM_ETIMEOUT){
            if(client->exit){
                return ret;
            }
            continue;
        }else if(ret == -QM_EIO){
            return ret;
        }

        if (read_len < client->recv_buf_len)
        {
            client->recv_line_buf[read_len++] = ch;
            client->recv_line_len = read_len;
        }
        else
        {
            is_full = QM_TRUE;
        }

        /* is newline or URC data */
        if ((ch == '\n' && last_ch == '\r') || (client->end_sign != 0 && ch == client->end_sign)
                || qm_at_client_get_urc_obj(client))
        {
            if (is_full)
            {
                QM_AT_LOGE(LOG_TAG, "read line failed. The line data length is out of buffer size(%d)!", client->recv_buf_len);
                memset(client->recv_line_buf, 0x00, client->recv_buf_len);
                client->recv_line_len = 0;
                return -QM_EFULL;
            }
            break;
        }
        last_ch = ch;
    }

#if CONFIG_QM_AT_PRINT_RAW_CMD
    qm_at_print_raw_cmd("recvline", client->recv_line_buf, read_len);
#endif

    return read_len;
}

static int qm_at_recv_readline_timeout(qm_at_client_t *client, uint32_t timeout)
{
    qm_err_t ret = QM_EOK;
    uint32_t read_len = 0;
    char ch = 0, last_ch = 0;
    uint8_t is_full = QM_FALSE;
    uint32_t left = 0;
    qm_utils_time_t readline_timer = {0};

    memset(client->recv_line_buf, 0x00, client->recv_buf_len);
    client->recv_line_len = 0;

    qm_utils_time_init(&readline_timer);
    qm_utils_time_countdown_ms(&readline_timer, timeout);

    while (1)
    {
        left = qm_utils_time_left(&readline_timer);
        if(left == 0){
            return -QM_ETIMEOUT;
        }
        ret = qm_at_client_getchar(client, &ch, left);
        if(ret == -QM_ETIMEOUT){
            return -QM_ETIMEOUT;
        }else if(ret == -QM_EIO){
            return ret;
        }
        if (read_len < client->recv_buf_len)
        {
            client->recv_line_buf[read_len++] = ch;
            client->recv_line_len = read_len;
        }
        else
        {
            is_full = QM_TRUE;
        }
        /* is newline or URC data */
        if ((ch == '\n' && last_ch == '\r') || (client->end_sign != 0 && ch == client->end_sign)
                || qm_at_client_get_urc_obj(client))
        {
            if (is_full)
            {
            QM_AT_LOGE(LOG_TAG, "read line failed. The line data length is out of buffer size(%d)!", client->recv_buf_len);
                memset(client->recv_line_buf, 0x00, client->recv_buf_len);
                client->recv_line_len = 0;
                return -QM_EFULL;
            } 
            break;
        }
        last_ch = ch;
    }

#if CONFIG_QM_AT_PRINT_RAW_CMD
    qm_at_print_raw_cmd("recvline", client->recv_line_buf, read_len);
#endif

    return read_len;
}

int32_t qm_at_client_obj_exec_cmd_timeout(qm_at_client_t *client, uint32_t timeout, qm_at_response_t *resp, const char *cmd_expr, ...)
{
    va_list args;
    qm_err_t ret = 0;
    qm_at_urc_t *urc = NULL;
    uint32_t left = 0;
    qm_utils_time_t readline_timer = {0};

    if(client == NULL){
        return -QM_EINVAL;
    }

    qm_utils_time_init(&readline_timer);
    qm_utils_time_countdown_ms(&readline_timer, timeout);

    client->resp_status = QM_AT_RESP_OK;
    client->resp = resp;

    if (resp != NULL)
    {
        resp->buf_len = 0;
        resp->line_counts = 0;
    }

    va_start(args, cmd_expr);
    qm_at_vprintfln((void*)&client->channel, cmd_expr, args);
    va_end(args);

    left = qm_utils_time_left(&readline_timer);

    do{
        ret = qm_at_recv_readline_timeout(client, left);
        if (ret > 0)
        {
            if ((urc = qm_at_client_get_urc_obj(client)) != NULL)
            {
                /* current receive is request, try to execute related operations */
                if (urc->func != NULL)
                {
                    urc->func(client, client->recv_line_buf, client->recv_line_len);
                }
            }
            else if (client->resp != NULL)
            {
                char end_ch = client->recv_line_buf[client->recv_line_len - 1];
                /* current receive is response */
                client->recv_line_buf[client->recv_line_len - 2] = '\0';
                if (resp->buf_len + client->recv_line_len < resp->buf_size){
                    /* copy response lines, separated by '\0' */
                    memcpy(resp->buf + resp->buf_len, client->recv_line_buf, client->recv_line_len - 1);

                    /* update the current response information */
                    resp->buf_len += client->recv_line_len - 1;
                    resp->line_counts++;
                }
                else
                {
                    client->resp_status = QM_AT_RESP_BUFF_FULL;
                    QM_AT_LOGE(LOG_TAG, "Read response buffer failed. The Response buffer size is out of buffer size(%d)!", resp->buf_size);
                }
                
                /* check response result */
                if ((client->end_sign != 0) && (end_ch == client->end_sign) && (resp->line_num == 0))
                {
                    /* get the end sign, return response state END_OK.*/
                    client->resp_status = QM_AT_RESP_OK;
                }
                else if (memcmp(client->recv_line_buf, QM_AT_RESP_END_OK, strlen(QM_AT_RESP_END_OK)) == 0
                        && resp->line_num == 0)
                {
                    /* get the end data by response result, return response state END_OK. */
                    client->resp_status = QM_AT_RESP_OK;
                }
                else if (strstr(client->recv_line_buf, QM_AT_RESP_END_ERROR)
                        || (memcmp(client->recv_line_buf, QM_AT_RESP_END_FAIL, strlen(QM_AT_RESP_END_FAIL)) == 0))
                {
                    client->resp_status = QM_AT_RESP_ERROR;
                }
                else if (resp->line_counts == resp->line_num && resp->line_num)
                {
                    /* get the end data by response line, return response state END_OK.*/
                    client->resp_status = QM_AT_RESP_OK;
                }
                else
                {
                    continue;
                }
                break;
            }
            else
            {
                QM_AT_LOGI(LOG_TAG, QM_AT_LOG_DOWNLINK, "unrecognized line: %.*s", client->recv_line_len, client->recv_line_buf);
            }
        }else if(ret == -QM_EIO){
            if(client->cb){
                client->cb(QM_AT_RESULT_CHANNEL_DOWN, client->arg);
                client->exit = 1;
            }
        }
    }while((left = qm_utils_time_left(&readline_timer)) > 0);

    if (resp != NULL)
    {
        if (client->resp_status != QM_AT_RESP_OK)
        {
            QM_AT_LOGE(LOG_TAG, "execute command failed!");
            ret = -QM_ERROR;
        }
    }

    if(ret < QM_EOK){
        return ret;
    }else{
        return QM_EOK;
    }
}

static void client_parser(void *arg)
{
    qm_err_t ret = 0;
    qm_at_urc_t *urc = NULL;
    qm_at_response_t *resp = NULL;
    qm_at_client_t *client = (qm_at_client_t*)arg;
    qm_task_t task = {0};

    while(!client->exit)
    {
        ret = qm_at_recv_readline(client);
        if (ret > 0)
        {
            if ((urc = qm_at_client_get_urc_obj(client)) != NULL)
            {
                /* current receive is request, try to execute related operations */
                if (urc->func != NULL)
                {
                    urc->func(client, client->recv_line_buf, client->recv_line_len);
                }
            }
            else if (client->resp != NULL)
            {
                resp = client->resp;

                char end_ch = client->recv_line_buf[client->recv_line_len - 1];

                /* current receive is response */
                client->recv_line_buf[client->recv_line_len - 1] = '\0';
                if (resp->buf_len + client->recv_line_len < resp->buf_size)
                {
                    /* copy response lines, separated by '\0' */
                    memcpy(resp->buf + resp->buf_len, client->recv_line_buf, client->recv_line_len);

                    /* update the current response information */
                    resp->buf_len += client->recv_line_len;
                    resp->line_counts++;
                }
                else
                {
                    client->resp_status = QM_AT_RESP_BUFF_FULL;
                    QM_AT_LOGE(LOG_TAG, "Read response buffer failed. The Response buffer size is out of buffer size(%d)!", resp->buf_size);
                }
                /* check response result */
                if ((client->end_sign != 0) && (end_ch == client->end_sign) && (resp->line_num == 0))
                {
                    /* get the end sign, return response state END_OK.*/
                    client->resp_status = QM_AT_RESP_OK;
                }
                else if (memcmp(client->recv_line_buf, QM_AT_RESP_END_OK, strlen(QM_AT_RESP_END_OK)) == 0
                        && resp->line_num == 0)
                {
                    /* get the end data by response result, return response state END_OK. */
                    client->resp_status = QM_AT_RESP_OK;
                }
                else if (strstr(client->recv_line_buf, QM_AT_RESP_END_ERROR)
                        || (memcmp(client->recv_line_buf, QM_AT_RESP_END_FAIL, strlen(QM_AT_RESP_END_FAIL)) == 0))
                {
                    client->resp_status = QM_AT_RESP_ERROR;
                }
                else if (resp->line_counts == resp->line_num && resp->line_num)
                {
                    /* get the end data by response line, return response state END_OK.*/
                    client->resp_status = QM_AT_RESP_OK;
                }
                else
                {
                    continue;
                }


                client->resp = NULL;
                qm_sem_signal(&client->resp_notice);
            }
            else
            {
                QM_AT_LOGI(LOG_TAG, QM_AT_LOG_DOWNLINK, "unrecognized line: %.*s", client->recv_line_len, client->recv_line_buf);
            }
        }else if(ret == -QM_EIO){
            if(client->cb){
                client->cb(QM_AT_RESULT_CHANNEL_DOWN, client->arg);
                client->exit = 1;
            }
        }
    }

    QM_AT_LOGI(LOG_TAG, QM_AT_LOG_LIFECYCLE, "at client exit");

    qm_mutex_lock(&client->lock, QM_WAIT_FOREVER);
    qm_mutex_unlock(&client->lock);
    qm_mutex_free(&client->lock);

    qm_sem_free(&client->resp_notice);
    
    qm_free(client->recv_line_buf);
    memcpy(&task, &client->task, sizeof(task));
    memset(client, 0, sizeof(qm_at_client_t));

    qm_task_exit(&task);
}

/**
 * AT client initialize.
 *
 * @param dev_name AT client device name
 * @param recv_buf receive buffer length
 * @param size     the maximum number of receive buffer length
 *
 * @return AT client object
 */
qm_at_client_t* qm_at_client_init(qm_at_client_param_t *param)
{
    int idx = 0;
    qm_err_t ret = QM_EOK;
    qm_at_client_t *client = NULL;

    if(param == NULL){
        return NULL;
    }

    for (idx = 0; idx < CONFIG_QM_AT_CLIENT_MAX_NUM && at_client_table[idx].recv_line_buf; idx++);

    if (idx >= CONFIG_QM_AT_CLIENT_MAX_NUM)
    {
        QM_AT_LOGE(LOG_TAG, "AT client initialize failed! Check the maximum number(%d) of AT client.", CONFIG_QM_AT_CLIENT_MAX_NUM);
        return NULL;
    }

    client = &at_client_table[idx];
    client->recv_line_buf = (char*)qm_malloc(param->recv_buf_len);
    if(client->recv_line_buf == NULL){
        return NULL;
    }
    client->recv_buf_len = param->recv_buf_len;
    client->channel.channel_type = param->channel_type;
    client->channel.handle = param->handle;
    client->channel.send = param->send;
    client->channel.recv = param->recv;
    client->sync = param->sync;

    if(client->sync){
        return client;
    }

    if(!client->parser_entry){
        client->parser_entry = client_parser;
    }

    ret = qm_mutex_new(&client->lock);
    if(ret != QM_EOK){
        goto __exit;
    }
    ret = qm_sem_new(&client->resp_notice, 0);
    if(ret != QM_EOK){
        goto __exit;
    }
    ret = qm_task_new(&client->task, "at_client", client->parser_entry, (void*)client, CONFIG_QM_AT_CLIENT_TASK_SIZE, CONFIG_QM_AT_CLIENT_TASK_PRIO);
    if(ret != QM_EOK){
        goto __exit;
    }
    return client;

__exit:
    if(client->lock.hdl){
        qm_mutex_free(&client->lock);
    }
    if(client->resp_notice.hdl){
        qm_sem_free(&client->resp_notice);
    }

    if(client->recv_line_buf){
        qm_free(client->recv_line_buf);
        client->recv_line_buf = NULL;
    }
    memset(client, 0, sizeof(qm_at_client_t));
    return NULL;
}


int32_t qm_at_client_deinit(qm_at_client_t *client)
{
    if(client == NULL){
        return -QM_EINVAL;
    }
    if(client->sync){
        if(client->recv_line_buf){
            qm_free(client->recv_line_buf);
            client->recv_line_buf = NULL;
        }
        memset(client, 0, sizeof(qm_at_client_t));
        return QM_EOK;
    }
    if(client->task.hdl){
        client->exit = 1;
    }
    return QM_EOK;
}

int32_t qm_at_client_register_result_cb(qm_at_client_t *client, at_result_cb cb, void *arg)
{
    if(client == NULL || cb == NULL){
        return -QM_EINVAL;
    }
    client->arg = arg;
    client->cb = cb;
    return QM_EOK;
}

#endif /* CONFIG_QM_AT_CLIENT */
