#ifndef __PRODTST_CORE_H__
#define __PRODTST_CORE_H__

#include "qm_at.h"
#include "qm_kernel.h"
#include "prodtst_adapter.h"
#include "prodtst_config.h"

#if CONFIG_QM_WORK_SUPPORT
#include "qm_event.h"

/** Production-test box event base used by event-loop APIs. */
QM_EVENT_DECLARE_BASE(QM_EVENT_PRODTST_BOX);
#endif


#define PRODTST_DATA_TYPE_MAX_LEN      (8)
#define PRODTST_PLATFORM_MAX_LEN       (16) 
#define PRODTST_DEV_TYPE_MAX_LEN       (8) 
#define PRODTST_MOUDLE_NAME_MAX_LEN    (16) 
#define PRODTST_MOUDLE_TYPE_MAX_LEN    (16) 

enum{
    PRODTST_RESULT_OK,
    PRODTST_RESULT_FAIL,
};

typedef enum{
    PRODTST_STEP_TYPE_NONE,
    PRODTST_STEP_TYPE_GENERIC,
    PRODTST_STEP_TYPE_FUN,
    PRODTST_STEP_TYPE_SPECIAL,
    PRODTST_STEP_TYPE_ALL,
}prodtst_step_type_t;

typedef enum{
    MODULE_TYPE_WIFI = 1,
    MODULE_TYPE_WIFI_BLE,
    MODULE_TYPE_BLE,
    MODULE_TYPE_BLE_MESH,
}module_type_t;

enum{
    PLATFORM_TYPE_ALI = 1,
    PLATFORM_TYPE_MIOT,
    PLATFORM_TYPE_HARMONY,
    PLATFORM_TYPE_TECENT,
    PLATFORM_TYPE_XJIOT,
};

typedef enum{
    PRODTST_STATUS_NONE,
    PRODTST_STATUS_SUCCESS,
    PRODTST_STATUS_FAIL,
    PRODTST_STATUS_CONFIG_READY,
    PRODTST_STATUS_READY,
    PRODTST_STATUS_NOT_READY,
    PRODTST_STATUS_START,
    PRODTST_STATUS_GENERIC_START,
    PRODTST_STATUS_GENERIC_SUCCESS,
    PRODTST_STATUS_FUN_START,
    PRODTST_STATUS_FUN_SUCCESS,
    PRODTST_STATUS_SPECIAL_START,
    PRODTST_STATUS_SPECIAL_SUCCESS,
}prodtst_status_t;

typedef enum{
    PRODTST_STATE_NONE,
    PRODTST_STATE_SETUP,
    PRODTST_STATE_GENERIC,
    PRODTST_STATE_SPECIFIC,
    PRODTST_STATE_FUN,
    PRODTST_STATE_SPECIAL,
}prodtst_state_t;

typedef enum{
    PRODTST_EVENT_NONE,
    PRODTST_EVENT_CONFIG,
    PRODTST_EVENT_SET,
    PRODTST_EVENT_SET_REPLY,
    PRODTST_EVENT_GET,
    PRODTST_EVENT_GET_REPLY,
    PRODTST_EVENT_REPORT,
    PRODTST_EVENT_REPORT_REPLY,
    PRODTST_EVENT_TEST,
    PRODTST_EVENT_TEST_REPLY,
    PRODTST_EVENT_SPCL_DOWN,
    PRODTST_EVENT_SPCL_DOWN_REPLY,
    PRODTST_EVENT_SPCL_UP,
    PRODTST_EVENT_SPCL_UP_REPLY,
    PRODTST_EVENT_RESULT,
    PRODTST_EVENT_RESULT_REPLY,
    PRODTST_EVENT_RESULT_REPORT,
    PRODTST_EVENT_RESULT_REPORT_REPLY,
    PRODTST_EVENT_WIFI_DUT_FOUND,
    PRODTST_EVENT_BLE_DUT_FOUND,
    PRODTST_EVENT_NEXT_ROUND,
    PRODTST_EVENT_CHANNEL_DOWN,
    PRODTST_EVENT_TIMEOUT,
    PRODTST_EVENT_RESET,
    PRODTST_EVENT_EXIT,
}prodtst_event_t;

enum{
    PRODTST_DATA_STRING,
    PRODTST_DATA_INT,
    PRODTST_DATA_JSON,
};

typedef enum{
    PRODTST_DATA_TYPE_NONE,
    PRODTST_DATA_TYPE_SERIAL,
    PRODTST_DATA_TYPE_ALI,
    PRODTST_DATA_TYPE_NFC,
}prodtst_data_type_t;

typedef struct{
    char *name;
    int type;
    void *data;
    int int_data;
}prodtst_data_t;

typedef struct{
    char *platform;
    char *module_name;
    char *module_type;
}prodtst_info_t;

typedef struct{
    int test_time;
    char platform[PRODTST_PLATFORM_MAX_LEN+1];
    char dev_type[PRODTST_DEV_TYPE_MAX_LEN+1];
    char module_name[PRODTST_MOUDLE_NAME_MAX_LEN+1];
    char module_type[PRODTST_MOUDLE_TYPE_MAX_LEN+1];
}prodtst_config_info_t;

typedef struct{
    char type[16+1];
    char uuid[32+1];
    int id;
    uint8_t *data;
    int len;
}prodtst_data_info_t;

typedef struct{
    char uuid[32+1];
    int id;
    char step[16+1];
    int result;
}prodtst_result_info_t;

typedef struct{
    char uuid[32+1];
    int id;
    char *data;
    int len;
}prodtst_special_info_t;

typedef struct{
    int fd;
}prodtst_wifi_dut_info_t;

typedef struct{
    int event;
    union{
        prodtst_wifi_dut_info_t wifi_dut_info;
        prodtst_config_info_t config_info;
        prodtst_data_info_t data_info;
        prodtst_special_info_t special_info;
        prodtst_result_info_t result_info;
    }dat;
}prodtst_notify_t;

typedef struct{
    char *name;
    char *cmd;
    int recv_size;
    int timeout;
    int (*creat)(char *cmd, char *param, char **out);
    int (*parser)(void *rsp, prodtst_data_t **data, int *num);
}prodtst_cmd_t;

typedef struct{
    char *platform;
    char *module_name;
    char *module_type;
    int recv_size;
    int obj_num;
    prodtst_cmd_t *generic_cmd;
    int generic_cmd_num;
    prodtst_cmd_t *specific_cmd;
    int specific_cmd_num;
    prodtst_adapter_t *adapter;
}prodtst_testsuite_t;

typedef struct {
    prodtst_data_t *data[CONFIG_QM_PRODTST_DATA_TABLE_NUM];    
    uint16_t num[CONFIG_QM_PRODTST_DATA_TABLE_NUM];        
}prodtst_data_table_t;

typedef struct {
      union{
        prodtst_wifi_dut_info_t wifi_dut_info;
    }info;
}prodtst_dut_info_t;


typedef struct{
    char uuid[32+1];
    int index;
    prodtst_status_t status;
    prodtst_state_t state;
    qm_task_t task;
    qm_queue_t notify;
    qm_mutex_t lock;
    qm_timer_t timer;
    void *channel_handle;
    void *spcl_handle;
    qm_at_client_t *at_client;
    prodtst_data_table_t data_tbl;
}prodtst_obj_t;

typedef void (*prodtst_notify_cb)(int channel, int prodtst_status);

typedef struct{
    int run_num;
    int idle_num;
    int active_num;
    int unactive_num;
    int total;
}prodtst_obj_info_t;

typedef struct{
    int ready;
    int start;
    int test_time;
    int test_num;
    void *adapter_handle;
    qm_task_t task;
    qm_queue_t notify;
    qm_sem_t exit_sem;
    prodtst_testsuite_t *testsuite;
    prodtst_config_info_t config_info;
    prodtst_dut_info_t dut_info;
    prodtst_notify_cb notify_cb;
    prodtst_obj_t *prodtst_obj[CONFIG_QM_PRODTST_OBJ_NUM];
}prodtst_context_t; 



int prodtst_init(prodtst_notify_cb notify_cb);

int prodtst_start(prodtst_info_t *info);
int prodtst_stop(void);


/********prodtst notify********/
int prodtst_wifi_dut_found_notify(int fd);
int prodtst_data_notify(int event, char* type, char *uuid, int id, uint8_t *data, int len);
int prodtst_config_notify(int test_time, char *platform, char *module_name, char *module_type);
int prodtst_special_down_notify(char *uuid, int id, char *data, int len);
int prodtst_result_notify(char *uuid, int id, char *step, int result);
int prodtst_next_round_notify(void);
int is_prodtst_ready(void);
/*****************************/

int prodtst_device_num_get(void);

#endif

