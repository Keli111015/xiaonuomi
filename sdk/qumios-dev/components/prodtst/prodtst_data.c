#include "prodtst_data.h"
#include "prodtst_res.h"
#include "qm_errno.h"
#include "cJSON.h"
#include "qm_kernel.h"
#include "prodtst_base.h"
#include "prodtst_dt.h"
#include "prodtst_at.h"
#include "qm_types.h"
#include "qm_log.h"
#include "qm_ringbuf.h"
#include "threadpool.h"

#define LOG_TAG  "prodtst data"

#define PRODTST_CONFIG_TIMEOUT   (3000)

static int prodtst_pack_transmit(prodtst_data_info_t *data_info);
static char *prodtst_data_method_str_get(prodtst_comm_msg_type_t msg_type);

//config
static int prodtst_data_config_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info);
static int prodtst_data_config_handle(prodtst_data_info_t *data_info);
static int prodtst_data_config_transmit(prodtst_data_info_t *data_info);
static int prodtst_data_config_destroy(prodtst_data_info_t *data_info);

//config reply
static int prodtst_data_config_reply_pack(prodtst_data_info_t *data_info);
static int prodtst_data_config_reply_destroy(prodtst_data_info_t *data_info);

//ready
static int prodtst_data_ready_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info);
static int prodtst_data_ready_handle(prodtst_data_info_t *data_info);
static int prodtst_data_ready_transmit(prodtst_data_info_t *data_info);
static int prodtst_data_ready_pack(prodtst_data_info_t *data_info);
static int prodtst_data_ready_destroy(prodtst_data_info_t *data_info);

//ready reply
static int prodtst_data_ready_reply_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info);
static int prodtst_data_ready_reply_handle(prodtst_data_info_t *data_info);
static int prodtst_data_ready_reply_transmit(prodtst_data_info_t *data_info);
static int prodtst_data_ready_reply_pack(prodtst_data_info_t *data_info);
static int prodtst_data_ready_reply_destroy(prodtst_data_info_t *data_info);

//test 
static int prodtst_data_test_generic_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info);
static int prodtst_data_test_generic_handle(prodtst_data_info_t *data_info);
static int prodtst_data_test_generic_transmit(prodtst_data_info_t *data_info);
static int prodtst_data_test_generic_destroy(prodtst_data_info_t *data_info);

//test reply
static int prodtst_data_test_reply_generic_pack(prodtst_data_info_t *data_info);
static int prodtst_data_test_reply_generic_destroy(prodtst_data_info_t *data_info);

//result
static int prodtst_data_result_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info);
static int prodtst_data_result_handle(prodtst_data_info_t *data_info);
static int prodtst_data_result_transmit(prodtst_data_info_t *data_info);
static int prodtst_data_result_pack(prodtst_data_info_t *data_info);
static int prodtst_data_result_destroy(prodtst_data_info_t *data_info);

//result reply
static int prodtst_data_result_reply_pack(prodtst_data_info_t *data_info);
static int prodtst_data_result_reply_destroy(prodtst_data_info_t *data_info);

//report
static int prodtst_data_report_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info);
static int prodtst_data_report_handle(prodtst_data_info_t *data_info);
static int prodtst_data_report_transmit(prodtst_data_info_t *data_info);
static int prodtst_data_report_pack(prodtst_data_info_t *data_info);
static int prodtst_data_report_destroy(prodtst_data_info_t *data_info);

//report reply
static int prodtst_data_report_reply_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info);
static int prodtst_data_report_reply_destroy(prodtst_data_info_t *data_info);

//wireless list reply
static int prodtst_data_wireless_list_reply_pack(prodtst_data_info_t *data_info);
static int prodtst_data_wireless_list_reply_destroy(prodtst_data_info_t *data_info);

static prodtst_data_handle_t *prodtst_data_handle_get(prodtst_comm_msg_type_t msg_type);

static prodtst_data_handle_t g_prodtst_data_handle[] = {

    { PRODTST_COMM_MSG_TYPE_CONFIG,             prodtst_data_config_unpack,                 prodtst_data_config_handle,             prodtst_data_config_transmit,             NULL,                                  prodtst_data_config_destroy               },
    { PRODTST_COMM_MSG_TYPE_CONFIG_REPLY,       NULL,                                       NULL,                                   NULL,                                     prodtst_data_config_reply_pack ,       prodtst_data_config_reply_destroy         },
    { PRODTST_COMM_MSG_TYPE_READY,              prodtst_data_ready_unpack,                  prodtst_data_ready_handle,              prodtst_data_ready_transmit,              prodtst_data_ready_pack,               prodtst_data_ready_destroy                },
    { PRODTST_COMM_MSG_TYPE_READY_REPLY,        prodtst_data_ready_reply_unpack,            prodtst_data_ready_reply_handle,        prodtst_data_ready_reply_transmit,        prodtst_data_ready_reply_pack,         prodtst_data_ready_reply_destroy          },
    { PRODTST_COMM_MSG_TYPE_BEFORE_TEST,        prodtst_data_test_generic_unpack,           prodtst_data_test_generic_handle,       prodtst_data_test_generic_transmit,       NULL,                                  prodtst_data_test_generic_destroy         },
    { PRODTST_COMM_MSG_TYPE_BEFORE_TEST_REPLY,  NULL,                                       NULL,                                   NULL,                                     prodtst_data_test_reply_generic_pack,  prodtst_data_test_reply_generic_destroy   },
    { PRODTST_COMM_MSG_TYPE_TEST,               prodtst_data_test_generic_unpack,           prodtst_data_test_generic_handle,       prodtst_data_test_generic_transmit,       NULL,                                  prodtst_data_test_generic_destroy         },
    { PRODTST_COMM_MSG_TYPE_TEST_REPLY,         NULL,                                       NULL,                                   NULL,                                     prodtst_data_test_reply_generic_pack,  prodtst_data_test_reply_generic_destroy   },
    { PRODTST_COMM_MSG_TYPE_SCRIPT_TEST,        prodtst_data_test_generic_unpack,           prodtst_data_test_generic_handle,       prodtst_data_test_generic_transmit,       NULL,                                  prodtst_data_test_generic_destroy         },
    { PRODTST_COMM_MSG_TYPE_SCRIPT_TEST_REPLY,  NULL,                                       NULL,                                   NULL,                                     prodtst_data_test_reply_generic_pack,  prodtst_data_test_reply_generic_destroy   },
    { PRODTST_COMM_MSG_TYPE_AFTER_TEST,         prodtst_data_test_generic_unpack,           prodtst_data_test_generic_handle,       prodtst_data_test_generic_transmit,       NULL,                                  prodtst_data_test_generic_destroy         },
    { PRODTST_COMM_MSG_TYPE_AFTER_TEST_REPLY,   NULL,                                       NULL,                                   NULL,                                     prodtst_data_test_reply_generic_pack,  prodtst_data_test_reply_generic_destroy   },
    { PRODTST_COMM_MSG_TYPE_BEFORE_TEST,        prodtst_data_test_generic_unpack,           prodtst_data_test_generic_handle,       prodtst_data_test_generic_transmit,       NULL,                                  prodtst_data_test_generic_destroy         },
    { PRODTST_COMM_MSG_TYPE_BEFORE_TEST_REPLY,  NULL,                                       NULL,                                   NULL,                                     prodtst_data_test_reply_generic_pack,  prodtst_data_test_reply_generic_destroy   },
    { PRODTST_COMM_MSG_TYPE_RESULT,             prodtst_data_result_unpack,                 prodtst_data_result_handle,             prodtst_data_result_transmit,             prodtst_data_result_pack,              prodtst_data_result_destroy               },
    { PRODTST_COMM_MSG_TYPE_RESULT_REPLY,       NULL,                                       NULL,                                   NULL,                                     prodtst_data_result_reply_pack ,       prodtst_data_result_reply_destroy         },
    { PRODTST_COMM_MSG_TYPE_REPORT,             prodtst_data_report_unpack,                 prodtst_data_report_handle,             prodtst_data_report_transmit,             prodtst_data_report_pack,              prodtst_data_report_destroy               },
    { PRODTST_COMM_MSG_TYPE_REPORT_REPLY,       prodtst_data_report_reply_unpack,           NULL,                                   NULL,                                     NULL,                                  prodtst_data_report_reply_destroy         }
};

#if 0
static prodtst_data_method_map_t g_prodtst_data_method_map[] = {
    { PRODTST_COMM_MSG_TYPE_CONFIG,             "config"            },
    { PRODTST_COMM_MSG_TYPE_CONFIG_REPLY,       "configReply"       },
    { PRODTST_COMM_MSG_TYPE_READY,              "ready"             },
    { PRODTST_COMM_MSG_TYPE_READY_REPLY,        "readyReply"        },
    { PRODTST_COMM_MSG_TYPE_BEFORE_TEST,        "beforeTest"        },
    { PRODTST_COMM_MSG_TYPE_BEFORE_TEST_REPLY,  "beforeTestReply"   },
    { PRODTST_COMM_MSG_TYPE_TEST,               "test"              },
    { PRODTST_COMM_MSG_TYPE_TEST_REPLY,         "testReply"         },
    { PRODTST_COMM_MSG_TYPE_SCRIPT_TEST,        "scriptTest"        },
    { PRODTST_COMM_MSG_TYPE_SCRIPT_TEST_REPLY,  "scriptTestReply"   },
    { PRODTST_COMM_MSG_TYPE_AFTER_TEST,         "afterTest"         },
    { PRODTST_COMM_MSG_TYPE_AFTER_TEST_REPLY,   "afterTestReply"    },
    { PRODTST_COMM_MSG_TYPE_RESULT,             "result"            },
    { PRODTST_COMM_MSG_TYPE_RESULT_REPLY,       "resultReply"       }
};
#endif

static prodtst_data_method_map_t g_prodtst_data_method_map[] = {
    { PRODTST_COMM_MSG_TYPE_CONFIG,             "config"             },
    { PRODTST_COMM_MSG_TYPE_CONFIG_REPLY,       "config_reply"       },
    { PRODTST_COMM_MSG_TYPE_READY,              "ready"              },
    { PRODTST_COMM_MSG_TYPE_READY_REPLY,        "ready_reply"        },
    { PRODTST_COMM_MSG_TYPE_BEFORE_TEST,        "before_test"        },
    { PRODTST_COMM_MSG_TYPE_BEFORE_TEST_REPLY,  "before_test_reply"  },
    { PRODTST_COMM_MSG_TYPE_TEST,               "test"               },
    { PRODTST_COMM_MSG_TYPE_TEST_REPLY,         "test_reply"         },
    { PRODTST_COMM_MSG_TYPE_SCRIPT_TEST,        "script_test"        },
    { PRODTST_COMM_MSG_TYPE_SCRIPT_TEST_REPLY,  "script_test_reply"  },
    { PRODTST_COMM_MSG_TYPE_AFTER_TEST,         "after_test"         },
    { PRODTST_COMM_MSG_TYPE_AFTER_TEST_REPLY,   "after test_reply"   },
    { PRODTST_COMM_MSG_TYPE_RESULT,             "result"             },
    { PRODTST_COMM_MSG_TYPE_RESULT_REPLY,       "result_reply"       },
    { PRODTST_COMM_MSG_TYPE_REPORT,             "report"             },
    { PRODTST_COMM_MSG_TYPE_REPORT_REPLY,       "report_reply"       }
};

prodtst_data_ctx_t g_prodtst_data_ctx = {0};

static char *prodtst_data_method_str_get(prodtst_comm_msg_type_t msg_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_data_method_map); i++){
        if(msg_type == g_prodtst_data_method_map[i].msg_type){
            return g_prodtst_data_method_map[i].str_method;
        }
    }
    return NULL;
}

int prodtst_data_callback_register(prodtst_data_callback data_cb)
{
    if(data_cb == NULL){
        return -QM_EINVAL;
    }
    g_prodtst_data_ctx.data_cb = data_cb;
    return QM_EOK;
}

int prodtst_data_link_register(prodtst_data_link_fn link_fn)
{
    if(link_fn == NULL){
        return -QM_EINVAL;
    }
    g_prodtst_data_ctx.link_fn = link_fn;
    return QM_EOK;
}

static prodtst_data_handle_t *prodtst_data_handle_get(prodtst_comm_msg_type_t msg_type)
{
    int i = 0;
    for(i = 0; i < QM_ARRAY_SIZE(g_prodtst_data_handle); i++){
        if(msg_type == g_prodtst_data_handle[i].msg_type){
            return &g_prodtst_data_handle[i];
        }
    }
    return NULL;
}

int prodtst_data_unpack(prodtst_comm_msg_type_t msg_type, uint8_t *data, uint32_t len, prodtst_data_info_t *data_info)
{
    prodtst_data_handle_t *data_handle = NULL;
    if(msg_type < PRODTST_COMM_MSG_TYPE_NONE || msg_type > PRODTST_COMM_MSG_TYPE_MAX){
        return -QM_EINVAL;
    }
    if(data == NULL || len == 0 || data_info == NULL){
        return -QM_EINVAL;
    }
    data_handle = prodtst_data_handle_get(msg_type);
    if(data_handle == NULL){
        QM_LOGE(LOG_TAG, "prodtst data handle get fail");
        return -QM_EINVAL;
    }
    if(data_handle->unpack == NULL){
        return -QM_EINVAL;
    }

    data_info->msg_type = msg_type;

    return data_handle->unpack(data, len, data_info);
}

int prodtst_data_handle(prodtst_data_info_t *data_info)
{
    prodtst_data_handle_t *data_handle = NULL;
    if(data_info == NULL){
        return -QM_EINVAL;
    }
    data_handle = prodtst_data_handle_get(data_info->msg_type);
    if(data_handle == NULL){
        QM_LOGE(LOG_TAG, "data handle get fail");
        return -QM_EINVAL;
    }
    if(data_handle->handle == NULL){
        return -QM_EINVAL;
    }
    return data_handle->handle(data_info);
}

int prodtst_data_transmit(prodtst_data_info_t *data_info)
{
    prodtst_data_handle_t *data_handle = NULL;
    if(data_info == NULL){
        return -QM_EINVAL;
    }
    data_handle = prodtst_data_handle_get(data_info->msg_type);
    if(data_handle == NULL){
        return -QM_EINVAL;
    }
    if(data_handle->transmit == NULL){
        return -QM_EINVAL;
    }
    return data_handle->transmit(data_info);
}

int prodtst_data_destroy(prodtst_data_info_t *data_info)
{
    prodtst_data_handle_t *data_handle = NULL;
    if(data_info == NULL){
        return -QM_EINVAL;
    }
    data_handle = prodtst_data_handle_get(data_info->msg_type);
    if(data_handle == NULL){
        return -QM_EINVAL;
    }
    if(data_handle->destroy == NULL){
        return -QM_EINVAL;
    }
    return data_handle->destroy(data_info);
}

static int prodtst_pack_transmit(prodtst_data_info_t *data_info)
{
    if(g_prodtst_data_ctx.link_fn){
        g_prodtst_data_ctx.link_fn(data_info->msg_type, (uint8_t*)data_info->up_data, (uint32_t)data_info->up_data_len);
    }
    return QM_EOK;
}

static int prodtst_data_config_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info)
{
    qm_err_t ret = 0;
    cJSON *root = NULL;
    char *str = NULL;
    cJSON *params_config = NULL;
    char *method = NULL;
    prodtst_config_info_t *config_info = NULL;

    if(data == NULL || len == 0 || data_info == NULL){
        return -QM_EINVAL;
    }

    QM_LOGD(LOG_TAG, "data config unpack...");

    config_info = &data_info->data.config_info;

    root = cJSON_Parse((const char*)data);
    if(root == NULL){
        QM_LOGE(LOG_TAG, "json parse fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cjson_str_get(root, PRODTST_JSON_KEY_METHOD);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    method = prodtst_data_method_str_get(data_info->msg_type);
    if(0 != strcmp(str, method)){
        ret = -QM_EINVAL;
        goto __exit;
    }

    params_config = cJSON_GetObjectItem(root, PRODTST_JSON_KEY_PARAMS);
    if(params_config == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = prodtst_config_unpack(params_config, config_info);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "prodtst config unpack fail");
        goto __exit;
    }

    cJSON_Delete(root);
    return QM_EOK;

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    prodtst_config_destroy(config_info);

    return ret;
}

int prodtst_data_config_handle(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;
    prodtst_config_info_t *config_info = NULL;
    prodtst_notify_info_t notify_info = {0};

    if(data_info == NULL){
        return -QM_EINVAL;
    }

    QM_LOGD(LOG_TAG, "data config handle...");
    
    config_info = &data_info->data.config_info;

    if(g_prodtst_data_ctx.data_cb){
        g_prodtst_data_ctx.data_cb(data_info->msg_type, PRODTST_DATA_NOTIFY_CONFIG_START, NULL);
    }

    ret = prodtst_config_init(config_info);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "prodtst config init fail");
        goto __exit;
    }

    if(g_prodtst_data_ctx.data_cb){
        notify_info.data.config_info = config_info; 
        g_prodtst_data_ctx.data_cb(data_info->msg_type, PRODTST_DATA_NOTIFY_CONFIG_SUCESS, &notify_info);
    }

    return ret;

__exit:
    return ret;
}

static int prodtst_data_config_destroy(prodtst_data_info_t *data_info)
{
    prodtst_config_info_t *config_info = NULL;
    if(data_info == NULL){
        return -QM_EINVAL;
    }

    QM_LOGD(LOG_TAG, "data config destroy...");
    config_info = &data_info->data.config_info;
    (void)prodtst_config_destroy(config_info);

    return QM_EOK;
}

static int prodtst_data_config_transmit(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;

    QM_LOGD(LOG_TAG, "data config transmit...");

    ret = prodtst_data_config_reply_pack(data_info);
    if(ret != QM_EOK){
        return ret;
    }

    prodtst_pack_transmit(data_info);
    prodtst_data_config_destroy(data_info);
    
    return ret;
}

static int prodtst_data_config_reply_pack(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;

    QM_LOGD(LOG_TAG, "data config reply pack...");
    data_info->msg_type = PRODTST_COMM_MSG_TYPE_CONFIG_REPLY;

    root = cJSON_CreateObject();
    if(root == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_METHOD, prodtst_data_method_str_get(data_info->msg_type));
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VERSION, PRODTST_PROTOCOL_VERSION);
    cJSON_AddNumberToObject(root, PRODTST_JSON_KEY_CODE, data_info->code);

    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    data_info->up_data_len = strlen(data_info->up_data);

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    return ret;
}

static int prodtst_data_config_reply_destroy(prodtst_data_info_t *data_info)
{

    QM_LOGD(LOG_TAG, "data config reply destroy...");

    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }
    return QM_EOK;
}

static int prodtst_data_ready_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info)
{
    if(len != sizeof(prodtst_ready_info_t)){
        return -QM_EINVAL;
    }
    memcpy(&data_info->data.ready_info, data, len);
    return QM_EOK;
}

static int prodtst_data_ready_handle(prodtst_data_info_t *data_info)
{
    return QM_EOK;
}

static int prodtst_data_ready_pack(prodtst_data_info_t *data_info)
{
    int i = 0;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    cJSON *array_object = NULL;
    cJSON *arrayobj = NULL;
    qm_err_t ret = QM_EOK;
    char mac[20] = {0};
    prodtst_ready_info_t *ready_info = NULL;

    ready_info = &data_info->data.ready_info;
    data_info->msg_type = PRODTST_COMM_MSG_TYPE_READY;

    root = cJSON_CreateObject();
    if(root == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_METHOD, "ready");
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VERSION, PRODTST_PROTOCOL_VERSION);

    params_object = cJSON_AddObjectToObject(root, PRODTST_JSON_KEY_PARAMS);
    if(params_object == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    array_object = cJSON_CreateArray();
    if(array_object == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddItemToObject(params_object, PRODTST_JSON_KEY_CHANNEL_INFO, array_object);

    if(ready_info->test_type == PRODTST_TEST_TYPE_WIRED){
        cJSON_AddStringToObject(params_object, PRODTST_JSON_KEY_CONN_TYPE, PRODTST_CHANNEL_TYPE_WIRED);
        for(i = 0; i < ready_info->num; i++){
            arrayobj = cJSON_CreateObject();
            if(arrayobj == NULL){
                ret = -QM_ENOMEM;
                goto __exit;
            }
            cJSON_AddItemToArray(array_object, arrayobj);
            cJSON_AddNumberToObject(arrayobj, PRODTST_JSON_KEY_CHANNEL_ID, ready_info->channel_info[i].channel_id);
        }
    }else if(ready_info->test_type == PRODTST_TEST_TYPE_BLE_ADV){
        cJSON_AddStringToObject(params_object, PRODTST_JSON_KEY_CONN_TYPE, PRODTST_CHANNEL_TYPE_WIRELESS); 
    }else{
        cJSON_AddStringToObject(params_object, PRODTST_JSON_KEY_CONN_TYPE, PRODTST_CHANNEL_TYPE_WIRELESS); 
        for(i = 0; i < ready_info->num; i++){
            arrayobj = cJSON_CreateObject();
            if(arrayobj == NULL){
                ret = -QM_ENOMEM;
                goto __exit;
            }
            cJSON_AddItemToArray(array_object, arrayobj);
            cJSON_AddNumberToObject(arrayobj, PRODTST_JSON_KEY_CHANNEL_ID, ready_info->channel_info[i].channel_id);
            qm_snprintf(mac, 20, "%02x:%02x:%02x:%02x:%02x:%02x", ready_info->channel_info[i].mac[0], ready_info->channel_info[i].mac[1],
                                                                  ready_info->channel_info[i].mac[2], ready_info->channel_info[i].mac[3],
                                                                  ready_info->channel_info[i].mac[4], ready_info->channel_info[i].mac[5]);
            cJSON_AddStringToObject(arrayobj, PRODTST_JSON_KEY_MAC, mac);
            cJSON_AddNumberToObject(arrayobj, PRODTST_JSON_KEY_CONN_CODE, ready_info->channel_info[i].conn_code);
        }
    }

    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    data_info->up_data_len = strlen(data_info->up_data);

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    return ret;
}

static int prodtst_data_ready_transmit(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;

    ret = prodtst_data_ready_pack(data_info);
    if(ret != QM_EOK){
        return ret;
    }

    prodtst_pack_transmit(data_info);

    return ret;
}

static int prodtst_data_ready_destroy(prodtst_data_info_t *data_info)
{
    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }
    return QM_EOK;
}

//ready reply
static int prodtst_data_ready_reply_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info)
{
    return QM_EOK;
}

static int prodtst_data_ready_reply_handle(prodtst_data_info_t *data_info)
{
    return QM_EOK;
}

static int prodtst_data_ready_reply_transmit(prodtst_data_info_t *data_info)
{
    return QM_EOK;
}

static int prodtst_data_ready_reply_pack(prodtst_data_info_t *data_info)
{
    return QM_EOK;
}

static int prodtst_data_ready_reply_destroy(prodtst_data_info_t *data_info)
{
    return QM_EOK;
}

static int prodtst_data_test_generic_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info)
{
    int ret = 0;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    cJSON *json_rule = NULL;
    char *str = NULL;
    char *method = NULL;
    prodtst_dt_data_t *dt_data = NULL;
    prodtst_test_info_t *test_info = &data_info->data.test_info;
    prodtst_data_format_t data_format = PRODTST_DATA_FORMAT_NONE;

    if(data == NULL || len == 0 || data_info == NULL){
        return -QM_EINVAL;
    }

    QM_LOGD(LOG_TAG, "data test unpack...");

    root = cJSON_Parse((const char*)data);
    if(root == NULL){
        QM_LOGE(LOG_TAG, "json parse fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cjson_str_get(root, PRODTST_JSON_KEY_METHOD);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    method = prodtst_data_method_str_get(data_info->msg_type);
    if(0 != strcmp(str, method)){
        ret = -QM_EINVAL;
        goto __exit;
    }

    params_object = cJSON_GetObjectItem(root, PRODTST_JSON_KEY_PARAMS);
    if(params_object == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(params_object, PRODTST_JSON_KEY_CASE_ID, &test_info->base_info.case_id);
    if(ret != QM_EOK){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(params_object, PRODTST_JSON_KEY_CHANNEL_ID, &test_info->base_info.channel_id);
    if(ret != QM_EOK){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(params_object, PRODTST_JSON_KEY_ACK, &test_info->base_info.ack);
    if(ret != QM_EOK){
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cjson_str_get(params_object, PRODTST_JSON_KEY_UUID);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    if(strlen(str) > CONFIG_PRODTST_UUID_LEN){
        ret = -QM_EINVAL;
        goto __exit;
    }
    strcpy(data_info->data.test_info.base_info.uuid, str);

    str = cjson_str_get(params_object, PRODTST_JSON_KEY_DEST);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    test_info->addr_list = prodtst_path_unpack(str, strlen(str));
    if(test_info->addr_list == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cjson_str_get(params_object, PRODTST_JSON_KEY_FORMAT);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    data_format = prodtst_data_format_get(str);
    if(data_format == PRODTST_DATA_FORMAT_NONE){
        ret = -QM_EINVAL;
        goto __exit;
    }

    test_info->data_format = data_format;

    dt_data = &test_info->dt_data;
    dt_data->addr_list = test_info->addr_list;
    dt_data->data_format = test_info->data_format;

    json_rule = cJSON_GetObjectItem(params_object, PRODTST_JSON_KEY_RECV_RULE);
    if(json_rule == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = prodtst_dt_rule_unpack((void*)json_rule, &test_info->dt_data);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "dt rule unpack fail");
        goto __exit;
    }

    str = cjson_str_get(params_object, PRODTST_JSON_KEY_DATA);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = prodtst_dt_unpack((void*)str, &test_info->dt_data);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "dt unpack fail");
        goto __exit;
    }

    cJSON_Delete(root);
    root = NULL;

    return QM_EOK;

__exit:
    if(test_info->addr_list){
        prodtst_path_destroy(test_info->addr_list);
        test_info->addr_list = NULL;
    }
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    return ret;
}

static int prodtst_data_test_generic_handle(prodtst_data_info_t *data_info)
{   
    qm_err_t ret = QM_EOK;
    prodtst_test_info_t *test_info = NULL;
    prodtst_dt_data_t *dt_data = NULL;
    prodtst_dt_rsp_data_t *dt_rsp_data = NULL;
    prodtst_test_rsp_info_t *test_rsp_info = NULL;

    prodtst_notify_info_t notify_info = {0};

    QM_LOGD(LOG_TAG, "data test handle...");

    test_rsp_info = &data_info->data.test_rsp_info;
    test_info = &data_info->data.test_info;
    dt_data = &data_info->data.test_info.dt_data;
    dt_rsp_data = &test_rsp_info->dt_rsp_data;
    dt_rsp_data->addr_list = test_info->addr_list;
    dt_rsp_data->data_format = test_info->data_format;

    dt_data->base_info = &test_info->base_info;

    if(g_prodtst_data_ctx.data_cb){
        notify_info.data.base_info = &test_info->base_info;
        g_prodtst_data_ctx.data_cb(data_info->msg_type, PRODTST_DATA_NOTIFY_TEST_CASE_START, &notify_info);
    }

    ret = prodtst_dt_handle(dt_data, dt_rsp_data);

    test_rsp_info = &data_info->data.test_rsp_info;
    test_rsp_info->data_format = test_info->data_format;
    memcpy(&test_rsp_info->base_info, &test_info->base_info, sizeof(test_info->base_info));
    test_rsp_info->data_format = test_info->data_format;
    test_rsp_info->addr_list = test_info->addr_list;
    test_info->addr_list = NULL;
    
    if(ret != QM_EOK){
        data_info->code = PRODTST_COMM_CODE_TIMEOUT;
        ret = QM_EOK;
    }

    return ret;
}

static int prodtst_data_test_generic_transmit(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;

    QM_LOGD(LOG_TAG, "data test transmit...");

    ret = prodtst_data_test_reply_generic_pack(data_info);
    if(ret != QM_EOK){
        goto __exit;
    }
    ret = prodtst_pack_transmit(data_info);
    if(ret != QM_EOK){
        goto __exit;
    }
    ret = prodtst_data_test_generic_destroy(data_info);
    if(ret != QM_EOK){
        goto __exit;
    }

__exit:
    return ret;
}

static int prodtst_data_test_generic_destroy(prodtst_data_info_t *data_info)
{
    prodtst_dt_data_t *dt_data = NULL;
    prodtst_test_info_t *test_info = NULL;

    QM_LOGD(LOG_TAG, "data test destroy...");

    test_info = &data_info->data.test_info;
    dt_data = &data_info->data.test_info.dt_data;

    prodtst_dt_destroy(dt_data);

    if(test_info->addr_list){
        qm_list_destroy(test_info->addr_list);
        test_info->addr_list = NULL;
    }
    return QM_EOK;
}

static int prodtst_data_test_reply_generic_pack(prodtst_data_info_t *data_info)
{   
    char *path = NULL;
    int path_len = 0;
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    cJSON *data_object = NULL;
    prodtst_test_rsp_info_t *test_rsp_info = NULL;

    QM_LOGD(LOG_TAG, "data test reply pack...");
    data_info->msg_type += 1;

    test_rsp_info = &data_info->data.test_rsp_info;

    root = cJSON_CreateObject();
    if(root == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_METHOD, prodtst_data_method_str_get(data_info->msg_type));
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VERSION, PRODTST_PROTOCOL_VERSION);
    cJSON_AddNumberToObject(root, PRODTST_JSON_KEY_CODE, data_info->code);

  
    params_object = cJSON_AddObjectToObject(root, PRODTST_JSON_KEY_PARAMS);
    if(params_object == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddStringToObject(params_object, PRODTST_JSON_KEY_UUID, test_rsp_info->base_info.uuid);
    cJSON_AddNumberToObject(params_object, PRODTST_JSON_KEY_CASE_ID, test_rsp_info->base_info.case_id);
    cJSON_AddNumberToObject(params_object, PRODTST_JSON_KEY_CHANNEL_ID, test_rsp_info->base_info.channel_id);
    cJSON_AddNumberToObject(params_object, PRODTST_JSON_KEY_ACK, test_rsp_info->base_info.ack);

    ret = prodtst_path_pack(test_rsp_info->addr_list, NULL, &path_len);
    if(ret != QM_EOK){
        return ret;
    }

    path = (char*)qm_malloc(path_len);
    if(path == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    ret = prodtst_path_pack(test_rsp_info->addr_list, path, &path_len);
    if(ret != QM_EOK){
        goto __exit;
    }
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_DEST, path);
    qm_free(path);
    path = NULL;

    cJSON_AddStringToObject(params_object, PRODTST_JSON_KEY_FORMAT, prodtst_data_format_str_get(test_rsp_info->data_format));

    if(data_info->code == PRODTST_COMM_CODE_SUCCESS){

        data_object = cJSON_AddObjectToObject(params_object, PRODTST_JSON_KEY_DATA);
        if(data_object == NULL){
            ret = -QM_ENOMEM;
            goto __exit;
        }

        ret = prodtst_dt_rsp_pack(&test_rsp_info->dt_rsp_data, data_object);
        if(ret != QM_EOK){
            goto __exit;
        }
    }
    
    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    data_info->up_data_len = strlen(data_info->up_data);

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    if(path){
        qm_free(path);
        path = NULL;
    }
    return ret;
}

static int prodtst_data_test_reply_generic_destroy(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;
    prodtst_test_rsp_info_t *test_rsp_info = NULL;

    QM_LOGD(LOG_TAG, "data test reply destroy...");

    test_rsp_info = &data_info->data.test_rsp_info;

    ret = prodtst_dt_rsp_destroy(&test_rsp_info->dt_rsp_data);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "prodtst dt rsp destroy ret: %d", ret);
    }

    ret = prodtst_path_destroy(test_rsp_info->addr_list);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "prodtst path destroy ret: %d", ret);
    }
    
    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }

    return QM_EOK;
}


static int prodtst_data_result_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info)
{
    int ret = 0;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    char *str = NULL;
    int result = 0;
    int channel_id = 0;
    char *method = NULL;
    prodtst_result_info_t *result_info = NULL;

    if(data == NULL || len == 0 || data_info == NULL){
        return -QM_EINVAL;
    }

    QM_LOGD(LOG_TAG, "data result unpack...");

    result_info = &data_info->data.result_info;

    root = cJSON_Parse((const char*)data);
    if(root == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cjson_str_get(root, PRODTST_JSON_KEY_METHOD);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    method = prodtst_data_method_str_get(data_info->msg_type);
    if(0 != strcmp(str, method)){
        ret = -QM_EINVAL;
        goto __exit;
    }

    params_object = cJSON_GetObjectItem(root, PRODTST_JSON_KEY_PARAMS);
    if(params_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cjson_str_get(params_object, PRODTST_JSON_KEY_UUID);
    if(str == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }
    if(strlen(str) > CONFIG_PRODTST_UUID_LEN){
        ret = -QM_EINVAL;
        goto __exit;
    }
    strcpy(result_info->uuid, str);

    ret = cjson_int_get(params_object, PRODTST_JSON_KEY_RESULT, &result);
    if(ret != QM_EOK){
        ret = -QM_EINVAL;
        goto __exit;
    }
    result_info->result = result;
    
    ret = cjson_int_get(params_object, PRODTST_JSON_KEY_CHANNEL_ID, &channel_id);
    if(ret != QM_EOK){
        ret = -QM_EINVAL;
        goto __exit;
    }
    result_info->channel_id = channel_id;

__exit:

    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    return ret;
}

static int prodtst_data_result_handle(prodtst_data_info_t *data_info)
{
    prodtst_notify_info_t notify_info = {0};
    prodtst_result_info_t *result_info = NULL;
    prodtst_result_rsp_info_t *result_rsp_info = NULL;

    QM_LOGD(LOG_TAG, "data result handle...");

    result_info = &data_info->data.result_info;
    result_rsp_info = &data_info->data.result_rsp_info;

    notify_info.data.result_info = result_info;

    if(g_prodtst_data_ctx.data_cb){
        g_prodtst_data_ctx.data_cb(data_info->msg_type, PRODTST_DATA_NOTIFY_RESULT, (void*)&notify_info);
    }

    strcpy(result_rsp_info->uuid, result_info->uuid);
    result_rsp_info->channel_id = result_info->channel_id;

    return QM_EOK;
}

static int prodtst_data_result_transmit(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;
    QM_LOGD(LOG_TAG, "data result transmit...");

    data_info->msg_type += 1;
    ret = prodtst_data_result_reply_pack(data_info);
    prodtst_pack_transmit(data_info);
    prodtst_data_result_destroy(data_info);
    return ret;
}

static int prodtst_data_result_pack(prodtst_data_info_t *data_info)
{
    QM_LOGD(LOG_TAG, "data result pack...");

    return QM_EOK;
}

static int prodtst_data_result_destroy(prodtst_data_info_t *data_info)
{
    QM_LOGD(LOG_TAG, "data result destroy...");

    return QM_EOK;
}

static int prodtst_data_result_reply_pack(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    prodtst_result_rsp_info_t *result_rsp_info = NULL;

    QM_LOGD(LOG_TAG, "data result reply pack...");

    result_rsp_info = &data_info->data.result_rsp_info;

    root = cJSON_CreateObject();
    if(root == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_METHOD, prodtst_data_method_str_get(data_info->msg_type));
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VERSION, PRODTST_PROTOCOL_VERSION);
    cJSON_AddNumberToObject(root, PRODTST_JSON_KEY_CODE, data_info->code);

    params_object = cJSON_AddObjectToObject(root, PRODTST_JSON_KEY_PARAMS);
    if(params_object == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddNumberToObject(params_object, PRODTST_JSON_KEY_CHANNEL_ID, result_rsp_info->channel_id);
    cJSON_AddStringToObject(params_object, PRODTST_JSON_KEY_UUID, result_rsp_info->uuid);

    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    data_info->up_data_len = strlen(data_info->up_data);

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    return ret;
}

static int prodtst_data_result_reply_destroy(prodtst_data_info_t *data_info)
{
    QM_LOGD(LOG_TAG, "data result reply destroy...");

    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }
    return QM_EOK;
}

static int prodtst_data_report_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info)
{
    memcpy(&data_info->data.result_report_info, data, len);
    return QM_EOK;
}

static int prodtst_data_report_handle(prodtst_data_info_t *data_info)
{
    return QM_EOK;
}

static int prodtst_data_report_transmit(prodtst_data_info_t *data_info)
{
    qm_err_t ret = QM_EOK;
    
    QM_LOGD(LOG_TAG, "result report transmit...");

    ret = prodtst_data_report_pack(data_info);
    if(ret != QM_EOK){
        goto __exit;
    }
    prodtst_pack_transmit(data_info);

__exit:
    return ret;
}

static int prodtst_data_report_inner_send(void *handle, char *buf, int size)
{
    return qm_ringbuf_push(handle, (uint8_t*)buf, size);
}

static int prodtst_data_report_inner_read(void *handle, char *buf, int size, int timeout)
{
    return qm_ringbuf_pop(handle, (uint8_t*)buf, size);
}

static int prodtst_data_report_inner_exec_cmd(char *report_cmd, prodtst_at_rsp_data_t *at_rsp_data)
{
    #define DATA_CHANNEL_SIZE (128)

    qm_err_t ret = QM_EOK;
    int ringbuf_len =  0;
    prodtst_at_link_t at_link = {0};
    uint8_t *report_buf = NULL;
    qm_ringbuf_t report_ringbuf = {0};
    prodtst_recv_rule_at_t recv_rule_at = {0};

    recv_rule_at.wait_time = 300;
    recv_rule_at.recv_size = DATA_CHANNEL_SIZE;

    report_buf = (uint8_t *)qm_malloc(DATA_CHANNEL_SIZE);
    if(report_buf == NULL){
        return -QM_ENOMEM;
    }
    memset(report_buf, 0, DATA_CHANNEL_SIZE);

    qm_ringbuf_init(&report_ringbuf, report_buf, DATA_CHANNEL_SIZE);

    at_link.handle = &report_ringbuf;
    at_link.read = prodtst_data_report_inner_read;
    at_link.write = prodtst_data_report_inner_send;

    ret = prodtst_at_client_generic_exec_cmd(report_cmd, &recv_rule_at, &at_link, at_rsp_data);

    if(report_buf){
        qm_free(report_buf);
        report_buf = NULL;
    }
    #undef DATA_CHANNEL_SIZE

    return ret;
}

static int prodtst_data_report_pack(prodtst_data_info_t *data_info)
{ 
    qm_err_t ret = QM_EOK;
    cJSON *root = NULL;
    cJSON *params_object = NULL;
    cJSON *data_object = NULL;
    char mac[20] = {0};
    prodtst_at_rsp_data_t at_rsp_data = {0};
    prodtst_result_report_info_t *result_report_info = NULL;

    QM_LOGD(LOG_TAG, "result report pack...");

    result_report_info = &data_info->data.result_report_info;

    root = cJSON_CreateObject();
    if(root == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_METHOD, prodtst_data_method_str_get(data_info->msg_type));
    cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VERSION, PRODTST_PROTOCOL_VERSION);

    params_object = cJSON_AddObjectToObject(root, PRODTST_JSON_KEY_PARAMS);
    if(params_object == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    cJSON_AddNumberToObject(params_object, PRODTST_JSON_KEY_CASE_ID, result_report_info->case_id);
    data_object = cJSON_AddObjectToObject(params_object, PRODTST_JSON_KEY_DATA);
    if(data_object == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }

    qm_snprintf(mac, 20, "%02x:%02x:%02x:%02x:%02x:%02x", result_report_info->unique.mac[0],
                                                            result_report_info->unique.mac[1],
                                                            result_report_info->unique.mac[2],
                                                            result_report_info->unique.mac[3],
                                                            result_report_info->unique.mac[4],
                                                            result_report_info->unique.mac[5]);

    cJSON_AddStringToObject(data_object, PRODTST_JSON_KEY_MAC, mac);
    
    prodtst_data_report_inner_exec_cmd((char *)result_report_info->data, &at_rsp_data);
    if(at_rsp_data.value){
        cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VALUE, at_rsp_data.value);
    }else{
        cJSON_AddStringToObject(root, PRODTST_JSON_KEY_VALUE, "");
    }
    if(at_rsp_data.reuslt){
        cJSON_AddStringToObject(root, PRODTST_JSON_KEY_RESULT, at_rsp_data.reuslt);
    }
    
    data_info->up_data = cJSON_PrintUnformatted(root);
    if(data_info->up_data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }
    data_info->up_data_len = strlen(data_info->up_data);

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    return ret; 
}

static int prodtst_data_report_destroy(prodtst_data_info_t *data_info)
{
    prodtst_result_report_info_t *result_report_info = NULL;
    result_report_info = &data_info->data.result_report_info;

    QM_LOGD(LOG_TAG, "result report destroy...");

    if(result_report_info->data){
        QM_LOGD(LOG_TAG, "free result_report_info->data");
        qm_free(result_report_info->data);
        result_report_info->data = NULL;
    }

    if(data_info->up_data){
        cJSON_free(data_info->up_data);
        data_info->up_data = NULL;
    }
    return QM_EOK;
}

static int prodtst_data_report_reply_unpack(uint8_t *data, int len, prodtst_data_info_t *data_info)
{
    int value = 0;
    char *str = NULL;
    char *method = NULL;
    cJSON *root = NULL;
    qm_err_t ret = QM_EOK;
    cJSON *params_object = NULL;

    QM_LOGD(LOG_TAG, "result report reply unpack...");

    root = cJSON_Parse((const char*)data);
    if(root == NULL){
        QM_LOGE(LOG_TAG, "json parse fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    str = cjson_str_get(root, PRODTST_JSON_KEY_METHOD);
    if(str == NULL){
        QM_LOGE(LOG_TAG, "json key get fail");
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(root, PRODTST_JSON_KEY_CODE, &value);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "json key get fail");
        goto __exit;
    }

    QM_LOGD(LOG_TAG, "result report reply code: %d", value);

    method = prodtst_data_method_str_get(data_info->msg_type);
    if(0 != strcmp(str, method)){
        ret = -QM_EINVAL;
        goto __exit;
    }

    params_object = cJSON_GetObjectItem(root, PRODTST_JSON_KEY_PARAMS);
    if(params_object == NULL){
        ret = -QM_EINVAL;
        goto __exit;
    }

    ret = cjson_int_get(params_object, PRODTST_JSON_KEY_CASE_ID, &value);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "json key get fail");
        goto __exit;
    }

    QM_LOGD(LOG_TAG, "result report reply case id: %d", value);

__exit:
    if(root){
        cJSON_Delete(root);
        root = NULL;
    }
    return ret;
}

static int prodtst_data_report_reply_destroy(prodtst_data_info_t *data_info)
{
     QM_LOGD(LOG_TAG, "result report reply destroy...");
     return QM_EOK;
}
