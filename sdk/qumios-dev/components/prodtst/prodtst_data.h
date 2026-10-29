#ifndef PRODTST_DATA_H
#define PRODTST_DATA_H

#ifdef __cplusplus
extern "C" {
#endif

#include "prodtst_res.h"
#include "prodtst_common.h"
#include "prodtst_dt.h"
#include "prodtst_at.h"
#include "prodtst_path.h"
#include "prodtst_config.h"

typedef struct {
    prodtst_comm_msg_type_t msg_type;
    char *str_method;
}prodtst_data_method_map_t;

typedef struct{
    int conn_code;
    uint8_t mac[6];
    int channel_id;
}prodtst_channel_info_t;

typedef struct{
    int num;
    prodtst_test_type_t test_type;
    prodtst_channel_info_t channel_info[CONFIG_PRODTST_TEST_CHANNEL_MAX_NUM];
}prodtst_ready_info_t;

typedef struct{
    int ack;
    int channel_id;
    int case_id;
    char uuid[CONFIG_PRODTST_UUID_LEN + 1];
}prodtst_base_info_t;

typedef struct{
    prodtst_base_info_t base_info;
    qm_list_t *addr_list;
    prodtst_data_format_t data_format;
    prodtst_dt_data_t dt_data;
}prodtst_test_info_t; 

typedef struct{
    prodtst_base_info_t base_info;
    qm_list_t *addr_list;
    prodtst_data_format_t data_format;
    prodtst_dt_rsp_data_t dt_rsp_data;
}prodtst_test_rsp_info_t; 

typedef struct {
    int channel_id;
    char uuid[CONFIG_PRODTST_UUID_LEN + 1];
    int result;
    int step;
    char *desc;
}prodtst_result_info_t;

typedef struct {
    int channel_id;
    char uuid[CONFIG_PRODTST_UUID_LEN + 1];
}prodtst_result_rsp_info_t;

typedef struct {
    int case_id;
    union{
        uint8_t mac[6];
    }unique;
    uint8_t *data;
    int len;
}prodtst_result_report_info_t;

typedef struct {
    int case_id;
}prodtst_result_report_rsp_info_t;

typedef enum{
    PRODTST_DATA_NOTIFY_TEST_CASE_START,
    PRODTST_DATA_NOTIFY_TEST_CASE_END,
    PRODTST_DATA_NOTIFY_TEST_START,
    PRODTST_DATA_NOTIFY_TEST_END,
    PRODTST_DATA_NOTIFY_RESULT,
    PRODTST_DATA_NOTIFY_CONFIG_INFO,
    PRODTST_DATA_NOTIFY_CONFIG_START,
    PRODTST_DATA_NOTIFY_CONFIG_SUCESS,
    PRODTST_DATA_NOTIFY_CONFIG_FAIL,
    PRODTST_DATA_NOTIFY_READY_REPLY,
    PRODTST_DATA_NOTIFY_MAX
}prodtst_data_notify_type_t;

typedef struct{
    int channel_id;
    char *uuid;
}prodtst_test_notify_t;

typedef struct {
    prodtst_addr_t addr;
    int len;
    char data[1];
}prodtst_transmit_data_t;

typedef struct {
    prodtst_comm_msg_type_t msg_type;
    prodtst_addr_t addr;
    int code;
    struct {
        prodtst_config_info_t config_info;
        prodtst_ready_info_t ready_info;
        prodtst_test_info_t test_info;
        prodtst_test_rsp_info_t test_rsp_info;
        prodtst_result_info_t result_info;
        prodtst_result_rsp_info_t result_rsp_info;
        prodtst_result_report_info_t result_report_info;
    }data;

    char *up_data;
    int up_data_len;

}prodtst_data_info_t; 

typedef struct{
    prodtst_comm_msg_type_t msg_type;
    int (*unpack)(uint8_t *data, int len, prodtst_data_info_t *data_info);
    int (*handle)(prodtst_data_info_t *data_info);
    int (*transmit)(prodtst_data_info_t *data_info);
    int (*pack)(prodtst_data_info_t *data_info);
    int (*destroy)(prodtst_data_info_t *data_info);
}prodtst_data_handle_t;

typedef struct {

    union {
        prodtst_result_info_t *result_info;
        prodtst_base_info_t *base_info;
        prodtst_config_t *config;
        prodtst_config_info_t *config_info;
    }data;
    
}prodtst_notify_info_t;


typedef int (*prodtst_data_callback)(prodtst_comm_msg_type_t msg_type, prodtst_data_notify_type_t notify_type, void *arg);
typedef int (*prodtst_data_link_fn)(prodtst_comm_msg_type_t msg_type, uint8_t *data, uint32_t len);

typedef struct {
    prodtst_data_callback data_cb;
    prodtst_data_link_fn link_fn;
}prodtst_data_ctx_t;

extern int prodtst_data_callback_register(prodtst_data_callback data_cb);
extern int prodtst_data_link_register(prodtst_data_link_fn link_fn);

extern int prodtst_data_unpack(prodtst_comm_msg_type_t msg_type, uint8_t *data, uint32_t len, prodtst_data_info_t *data_info);
extern int prodtst_data_handle(prodtst_data_info_t *data_info);
extern int prodtst_data_transmit(prodtst_data_info_t *data_info);
extern int prodtst_data_pack(prodtst_data_info_t *data_info);
extern int prodtst_data_destroy(prodtst_data_info_t *data_info);


#ifdef __cplusplus
}
#endif


#endif /* PRODTST_DATA_H */