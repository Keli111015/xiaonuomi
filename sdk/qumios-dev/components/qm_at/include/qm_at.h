#ifndef __QM_AT_H__
#define __QM_AT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "qm_kernel.h"
#include "qm_config.h"
#include "qm_at_log.h"

#if CONFIG_QM_WORK_SUPPORT
#include "qm_event.h"

/** AT server event base used by event-loop APIs. */
QM_EVENT_DECLARE_BASE(QM_EVENT_AT_SERVER);
#endif

#ifndef CONFIG_QM_AT_SERVER
#define CONFIG_QM_AT_SERVER   1
#endif

#ifndef CONFIG_QM_AT_CLIENT
#define CONFIG_QM_AT_CLIENT   0
#endif

#ifndef CONFIG_QM_AT_PRINT_RAW_CMD
#define CONFIG_QM_AT_PRINT_RAW_CMD      0
#endif

#define QM_AT_SW_VERSION                  "1.0.0"


#ifndef CONFIG_QM_AT_SERVER_TASK_SIZE  
#define CONFIG_QM_AT_SERVER_TASK_SIZE            2*1024    
#endif

#ifndef CONFIG_QM_AT_SERVER_TASK_PRIO  
#define CONFIG_QM_AT_SERVER_TASK_PRIO             20    
#endif

#ifndef CONFIG_QM_AT_CLIENT_TASK_SIZE  
#define CONFIG_QM_AT_CLIENT_TASK_SIZE             4*1024    
#endif

#ifndef CONFIG_QM_AT_CLIENT_TASK_PRIO  
#define CONFIG_QM_AT_CLIENT_TASK_PRIO             20    
#endif

/* the maximum number of supported AT servers */
#ifndef CONFIG_QM_AT_SERVER_MAX_NUM
#define CONFIG_QM_AT_SERVER_MAX_NUM                3
#endif

/* the maximum number of supported AT clients */
#ifndef CONFIG_QM_AT_CLIENT_MAX_NUM
#define CONFIG_QM_AT_CLIENT_MAX_NUM                16
#endif

#ifndef CONFIG_QM_AT_CMD_NAME_MAX_LEN
#define CONFIG_QM_AT_CMD_NAME_MAX_LEN              16
#endif

#ifndef CONFIG_QM_AT_END_MARK_LEN
#define CONFIG_QM_AT_END_MARK_LEN                  4
#endif

#ifndef CONFIG_QM_AT_CMD_MAX_LEN
#define CONFIG_QM_AT_CMD_MAX_LEN                  32
#endif

#ifndef CONFIG_QM_AT_CMD_TABLE_MAX_NUM
#define CONFIG_QM_AT_CMD_TABLE_MAX_NUM            10
#endif

#ifndef CONFIG_QM_AT_URC_TABLE_MAX_NUM
#define CONFIG_QM_AT_URC_TABLE_MAX_NUM            5
#endif

#define QM_AT_CMD_END_MARK_CRLF

/* the server AT commands new line sign */
#if defined(QM_AT_CMD_END_MARK_CRLF)
#define QM_AT_CMD_END_MARK                "\r\n"
#elif defined(QM_AT_CMD_END_MARK_CR)
#define QM_AT_CMD_END_MARK                "\r"
#elif defined(QM_AT_CMD_END_MARK_LF)
#define QM_AT_CMD_END_MARK                "\n"
#endif


#define QM_AT_ECHO_MODE_CLOSE             0
#define QM_AT_ECHO_MODE_OPEN              1

enum{
    AT_CHANNEL_UART = 1,
    AT_CHANNEL_SOCKET,
};

typedef enum {
    QM_AT_CMD_TYPE_TEST   = 1,
    QM_AT_CMD_TYPE_QUERY  = 2,
    QM_AT_CMD_TYPE_SETUP  = 3,
    QM_AT_CMD_TYPE_EXE = 4,
}qm_at_cmd_type_t;

typedef struct{

    uint8_t channel_type;
    char *recv_buf;
    uint32_t recv_buf_len;
    uint32_t cur_recv_len;
    void *handle;
    int (*send)(void *handle, char *buf, int size);
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    int (*recv)(void *handle, char *buf, int size, int timeout);
#endif
}qm_at_channel_t;

typedef enum
{
    QM_AT_RESULT_OK = 0,                  /* AT result is no error */
    QM_AT_RESULT_FAILE = -1,              /* AT result have a generic error */
    QM_AT_RESULT_NULL = -2,               /* AT result not need return */
    QM_AT_RESULT_CMD_ERR = -3,            /* AT command format error or No way to execute */
    QM_AT_RESULT_CHECK_FAILE = -4,        /* AT command expression format is error */
    QM_AT_RESULT_PARSE_FAILE = -5,        /* AT command arguments parse is error */
    QM_AT_RESULT_CHANNEL_DOWN = -6,       /* AT command channel is error */
}qm_at_result_t;

typedef void (*at_result_cb)(int result, void *arg);

#if CONFIG_QM_AT_SERVER

typedef struct
{
    char *name;
    char *args_expr;
    qm_at_result_t (*test)(void *channel);
    qm_at_result_t (*query)(void *channel);
    qm_at_result_t (*setup)(void *channel, const char *args);
    qm_at_result_t (*exec)(void *channel);
}qm_at_cmd_t;

typedef struct {
    qm_at_cmd_t *at_cmd_table[CONFIG_QM_AT_CMD_TABLE_MAX_NUM];     /* user input at cmd table */
    uint16_t at_cmd_num[CONFIG_QM_AT_CMD_TABLE_MAX_NUM];         /* command number every table*/
    uint16_t cmd_num;                                       /* command number */
}qm_at_cmd_table_t; 

typedef struct{

    uint8_t echo_mode;
    uint8_t channel_type;
    uint32_t recv_buf_len;
    void *handle;
    int (*send)(void *handle, char *buf, int size);
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    int (*recv)(void *handle, char *buf, int size, int timeout);
#endif
}qm_at_server_param_t;

typedef struct
{
    uint8_t echo_mode;
    qm_at_channel_t channel;
    qm_at_cmd_table_t cmd_table;
#if CONFIG_QM_AT_SERVER_OS_SUPPORT
    qm_task_t task;
    int exit;
    void (*parser_entry)(void *arg);
#endif
    void *arg;
    at_result_cb cb;
}qm_at_server_t;

#endif /* CONFIG_QM_AT_SERVER */

#if CONFIG_QM_AT_CLIENT
typedef enum 
{
    QM_AT_RESP_OK = 0,                   /* AT response end is OK */
    QM_AT_RESP_ERROR = -1,               /* AT response end is ERROR */
    QM_AT_RESP_TIMEOUT = -2,             /* AT response is timeout */
    QM_AT_RESP_BUFF_FULL= -3,            /* AT response buffer is full */
}qm_at_resp_status_t;

typedef struct
{
    /* response buffer */
    char *buf;
    /* the maximum response buffer size, it set by `at_create_resp()` function */
    uint32_t buf_size;
    /* the length of current response buffer */
    uint32_t buf_len;
    /* the number of setting response lines, it set by `at_create_resp()` function
     * == 0: the response data will auto return when received 'OK' or 'ERROR'
     * != 0: the response data will return when received setting lines number data */
    uint32_t line_num;
    /* the count of received response lines */
    uint32_t line_counts;
    /* the maximum response time */
    int32_t timeout;
}qm_at_response_t;

struct qm_at_client;

/* URC() object, such as: 'RING', 'READY' request by AT server */
typedef struct
{
    const char *cmd_prefix;
    const char *cmd_suffix;
    void (*func)(struct qm_at_client *client, const char *data, uint32_t size);
}qm_at_urc_t;

typedef struct at_urc_table
{
    qm_at_urc_t *at_urc_table[CONFIG_QM_AT_URC_TABLE_MAX_NUM];    /* user input at urc table */
    uint16_t at_urc_num[CONFIG_QM_AT_URC_TABLE_MAX_NUM];  /* urc number */
    uint16_t urc_num;         
}qm_at_urc_table_t;

typedef struct{
    uint8_t channel_type;
    uint32_t recv_buf_len;
    void *handle;
    int (*send)(void *handle, char *buf, int size);
    int (*recv)(void *handle, char *buf, int size, int timeout);
    int sync;
}qm_at_client_param_t;

typedef struct qm_at_client
{
    char end_sign;

    qm_at_channel_t channel;
    /* the current received one line data buffer */
    char *recv_line_buf;
    /* The length of the currently received one line data */
    uint32_t recv_line_len;
    /* The maximum supported receive data length */
    uint32_t recv_buf_len;

    qm_at_response_t *resp;

    qm_at_resp_status_t resp_status;

    qm_at_urc_table_t urc_table;

    qm_sem_t resp_notice;
    qm_mutex_t lock;
    int exit;
    int sync;
    qm_task_t task;

    void (*parser_entry)(void *arg);
    void *arg;
    at_result_cb cb;
    
}qm_at_client_t;

#endif /* CONFIG_QM_AT_CLIENT */

#if CONFIG_QM_AT_SERVER
/* AT server initialize and start */
qm_at_server_t *qm_at_server_init(qm_at_server_param_t *param);
int32_t qm_at_server_deinit(qm_at_server_t *server);
int32_t qm_at_server_register_result_cb(qm_at_server_t *server, at_result_cb cb, void *arg);
int32_t qm_at_register_commands(qm_at_server_t *server, qm_at_cmd_t *cmd_tbl, uint16_t cmd_num);

/* AT server send command execute result to AT device */
void qm_at_server_printf(void *channel, const char *format, ...);
void qm_at_server_printfln(void *channel, const char *format, ...);
void qm_at_server_print_result(void *channel, qm_at_result_t result);
/* AT server print all commands to AT device */
void qm_at_server_print_all_cmd(void *channel);

/* AT server request arguments parse */
int32_t qm_at_req_parse_args(const char *req_args, const char *req_expr, ...);

int32_t qm_at_server_send(void *channel, const char *buf, uint32_t size);

#if CONFIG_QM_AT_SERVER_OS_SUPPORT
int32_t qm_at_server_recv(void *channel, char *buf, uint32_t size, int32_t timeout);
#else
int32_t qm_at_server_data_push(qm_at_server_t *server, char *buf, uint32_t size);
int32_t qm_at_server_data_push_form_isr(qm_at_server_t *server, char *buf, uint32_t size);
#endif
int32_t qm_at_server_echo_mode_set(void *channel, uint8_t echo_mode);

#endif /* CONFIG_QM_AT_SERVER */

#if CONFIG_QM_AT_CLIENT

/* AT client initialize and start*/
qm_at_client_t* qm_at_client_init(qm_at_client_param_t *param);
int32_t qm_at_client_deinit(qm_at_client_t *client);
int32_t qm_at_client_register_result_cb(qm_at_client_t *client, at_result_cb cb, void *arg);
/* ========================== multiple AT client function ============================ */

/* get first AT client object */
qm_at_client_t *qm_at_client_get_first(void);

/* AT client wait for connection to external devices. */
int32_t qm_at_client_obj_wait_connect(qm_at_client_t *client, uint32_t timeout);

/* AT client send or receive data */
int32_t qm_at_client_obj_send(qm_at_client_t *client, char *buf, uint32_t size);
int32_t qm_at_client_obj_recv(qm_at_client_t *client, char *buf, uint32_t size, int32_t timeout);

/* set AT client a line end sign */
void qm_at_client_obj_set_end_sign(qm_at_client_t *client, char ch);

/* Set URC(Unsolicited Result Code) table */
int32_t qm_at_client_obj_register_urc(qm_at_client_t *client, qm_at_urc_t * urc_tbl, uint32_t urc_num);

/* AT client send commands to AT server and waiter response */
int32_t qm_at_client_obj_exec_cmd(qm_at_client_t *client, qm_at_response_t *resp, const char *cmd_expr, ...);
//just use in synchronous environment
int32_t qm_at_client_obj_exec_cmd_timeout(qm_at_client_t *client, uint32_t timeout, qm_at_response_t *resp, const char *cmd_expr, ...);

/* AT response object create and delete */
qm_at_response_t *qm_at_client_create_resp(uint32_t buf_size, uint32_t line_num, int32_t timeout);
void qm_at_client_delete_resp(qm_at_response_t *resp);
qm_at_response_t *qm_at_client_resp_set_info(qm_at_response_t *resp, uint32_t buf_size, uint32_t line_num, int32_t timeout);


/* AT response line buffer get and parse response buffer arguments */
const char *qm_at_client_resp_get_line(qm_at_response_t *resp, uint32_t resp_line);
const char *qm_at_client_resp_get_line_by_kw(qm_at_response_t *resp, const char *keyword);
int32_t qm_at_client_resp_parse_line_args(qm_at_response_t *resp, uint32_t resp_line, const char *resp_expr, ...);
int32_t qm_at_client_resp_parse_line_args_by_kw(qm_at_response_t *resp, const char *keyword, const char *resp_expr, ...);

/* ========================== single AT client function ============================ */

/**
 * NOTE: These functions can be used directly when there is only one AT client.
 * If there are multiple AT Client in the program, these functions can operate on the first initialized AT client.
 */

#define qm_at_client_exec_cmd(resp, ...)                       qm_at_client_obj_exec_cmd(qm_at_client_get_first(), resp, __VA_ARGS__)
#define qm_at_client_wait_connect(timeout)                     qm_at_client_obj_wait_connect(qm_at_client_get_first(), timeout)
#define qm_at_client_send(buf, size)                           qm_at_client_obj_send(qm_at_client_get_first(), buf, size)
#define qm_at_client_recv(buf, size, timeout)                  qm_at_client_obj_recv(qm_at_client_get_first(), buf, size, timeout)
#define qm_at_set_end_sign(ch)                                 qm_at_client_obj_set_end_sign(qm_at_client_get_first(), ch)
#define qm_at_client_register_urc(urc_table, table_sz)         qm_at_client_obj_register_urc(qm_at_client_get_first(), urc_table, table_sz)

#endif /* CONFIG_QM_AT_CLIENT */


#ifdef __cplusplus
}
#endif

#endif /* __AT_H__ */
