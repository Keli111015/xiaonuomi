#ifndef PRODTST_DT_H
#define PRODTST_DT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "qm_types.h"
#include "prodtst_common.h"
#include "prodtst_path.h"


#ifndef CONFIG_PRODTST_BLE_MESH_AT_BUF_MAX_SIZE
#define CONFIG_PRODTST_BLE_MESH_AT_BUF_MAX_SIZE        128
#endif

typedef struct {
    char *str_format;
    prodtst_data_format_t data_format;
}prodtst_data_format_map_t;

typedef struct{
    uint32_t wait_time;
    uint32_t recv_size;
}prodtst_recv_rule_at_t;

typedef enum{
     BYTE_ORDER_NONE = 1,
     BYTE_ORDER_LITTLE_ENDIAN = 1,
     BYTE_ORDER_BIG_ENDIAN = 2,
}prodtst_data_byte_order_t;

typedef struct{
    uint32_t wait_time;
    int head_len;
    uint8_t *head;
    int tail_len;
    uint8_t *tail;
    int len_pos;
    int len_range_start;
    int len_range_end;
    int fix_len;
    int match_pos;
    uint8_t *match_hex;
    int match_hex_len;
    int remain_len;
    prodtst_data_byte_order_t byteorder;
}prodtst_recv_rule_hex_t;

typedef struct{
    uint32_t wait_time;
}prodtst_recv_rule_xjiang_uart_t;

typedef struct{
    uint32_t timeout;
    prodtst_data_format_t data_format;
    union 
    {
        prodtst_recv_rule_at_t rule_at;
        prodtst_recv_rule_hex_t rule_hex;
        prodtst_recv_rule_xjiang_uart_t rule_xjiang_uart;
    }rule;
}prodtst_recv_rule_t;

typedef struct {
    char *value;
    char *reuslt;
}prodtst_at_rsp_data_t;

typedef struct {
    char *value;
    int len;
}prodtst_hex_rsp_data_t;

typedef struct {
    char *value;
    int len;
}prodtst_xjiang_uart_rsp_data_t;

typedef struct {
    uint8_t *data;
    int len;
}prodtst_hex_data_t;

typedef struct {
    uint8_t *data;
    int len;
}prodtst_xjiang_uart_data_t;

typedef struct {
    char *data;
    int len;
}prodtst_at_data_t;

enum{
    PRODTST_HEX_PARSE_STATE_NONE,
    PRODTST_HEX_PARSE_STATE_HEAD,
    PRODTST_HEX_PARSE_STATE_PRE_LEN,
    PRODTST_HEX_PARSE_STATE_LEN,
    PRODTST_HEX_PARSE_STATE_UNFIX_LEN,
    PRODTST_HEX_PARSE_STATE_FIX_LEN,
    PRODTST_HEX_PARSE_STATE_DATA,
    PRODTST_HEX_PARSE_STATE_REMAIN_LEN,
    PRODTST_HEX_PARSE_STATE_TAIL,
    PRODTST_HEX_PARSE_STATE_MATCH,
    PRODTST_HEX_PARSE_STATE_COMPLETE,
};

typedef struct {
    void *base_info;
    void *addr_list;
    prodtst_data_format_t data_format;
    prodtst_recv_rule_t recv_rule;
    union {
        prodtst_at_data_t at_data;
        prodtst_hex_data_t hex_data;
        prodtst_xjiang_uart_data_t xjiang_uart_data;
    }data; 
}prodtst_dt_data_t;

typedef struct {
    void *addr_list;
    prodtst_data_format_t data_format;
    union {
        prodtst_at_rsp_data_t at_rsp_data;
        prodtst_hex_rsp_data_t hex_rsp_data;
        prodtst_xjiang_uart_rsp_data_t xjiang_uart_rsp_data;
    }data; 
}prodtst_dt_rsp_data_t;

typedef struct {
    prodtst_hd_type_t hd_type;
    prodtst_data_format_t data_format;
    int (*rule_unpack)(void *json_object, prodtst_dt_data_t *dt_data);
    int (*rule_pack)(void *json_object, prodtst_dt_data_t *dt_data);
    int (*unpack)(void *data, prodtst_dt_data_t *dt_data);
    int (*rsp_unpack)(void *data, prodtst_dt_rsp_data_t *dt_rsp_data);
    int (*handle)(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data);
    int (*rsp_pack)(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object);
    int (*destroy)(prodtst_dt_data_t *dt_data);
    int (*rsp_destroy)(prodtst_dt_rsp_data_t *dt_rsp_data);
}prodtst_dt_handle_t;

extern prodtst_data_format_t prodtst_data_format_get(char *str_format);
extern char *prodtst_data_format_str_get(prodtst_data_format_t data_format);
extern int prodtst_dt_rule_unpack(void *json_object, prodtst_dt_data_t *dt_data);
extern int prodtst_dt_rule_pack(void *json_object, prodtst_dt_data_t *dt_data);
extern int prodtst_dt_unpack(void *data, prodtst_dt_data_t *dt_data);
extern int prodtst_dt_rsp_unpack(void *data, prodtst_dt_rsp_data_t *dt_rsp_data);
extern int prodtst_dt_handle(prodtst_dt_data_t *dt_data, prodtst_dt_rsp_data_t *dt_rsp_data);
extern int prodtst_dt_rsp_pack(prodtst_dt_rsp_data_t *dt_rsp_data, void *json_object);
extern int prodtst_dt_destroy(prodtst_dt_data_t *dt_data);
extern int prodtst_dt_rsp_destroy(prodtst_dt_rsp_data_t *dt_rsp_data);
 
#ifdef __cplusplus
}
#endif

#endif