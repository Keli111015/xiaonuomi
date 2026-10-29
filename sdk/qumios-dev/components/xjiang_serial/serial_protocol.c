#include "serial_protocol.h"
#include "qm_utils_timer.h"
#include "qm_utils_list.h"
#include "serial_platform.h"
#include "qm_utils_string.h"
#include "serial_common.h"
#include "qm_tlv.h"
#include "qm.h"

#define LOG_TAG "serial"

#if CONFIG_QM_SERIAL_OTA_SUPPORT
#include "serial_ota.h"
#endif

static uint8_t frame_chksum(uint8_t *buffer, uint16_t length);
static void frame_head_init(uint8_t *buffer, frame_cmd_t cmd, uint16_t len);
static int32_t frame_creat(frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t data_len, uint8_t *buf, uint16_t *buf_len);
static uint8_t packet_id_get(void);
static int32_t serial_heartbeat_send(void);
int32_t serial_generate_send(frame_cmd_t cmd, uint8_t packet_id, uint8_t *data, uint16_t len);
static int32_t serial_generate_send_with_code(frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t len);
static uint16_t get_packet_len(uint8_t*packet_buffer);
static int32_t serial_keepalive(serial_context_t *pcontext);
static int32_t serial_id_info_push(serial_data_type_t type, platform_id_t platform_id, uint8_t local_id, uint8_t *data);
static int32_t serial_id_uptime(uint8_t local_id, uint8_t *info);
static int32_t serial_cloud_generate_send(frame_cmd_t cmd, platform_id_t id, uint8_t *data, uint16_t len);
static int id_match(void *a, void *b);
static int32_t serial_local_id_find(uint8_t local_id, platform_id_t *platform_id);
static serial_data_type_t data_type_get(uint8_t local_id);
static int32_t frame_parse(uint8_t *data, uint16_t len);
static int serial_recv_cycle(serial_context_t *pcontext, qm_utils_time_t *timer);
static int32_t serial_deviceinfo_get_send(void);
static int32_t serial_snapshot_get_send(void);
static int32_t serial_process_cycle(serial_context_t *pcontext);
static int32_t id_check_cycle(serial_context_t *pcontext);
static uint8_t local_id_get(uint8_t *raw);
static int32_t down_queue_send(void);
static int32_t serial_service_register(module_service_type_t service_type, serial_service_fn fn, int32_t is_inner);
static int32_t serial_service_handler(uint8_t *data, uint16_t len);
static int32_t serial_setmode_handler(uint8_t *data, uint16_t len);
static int32_t serial_getmode_handler(uint8_t *attr_id, uint16_t num);
static int32_t serial_send_by_attrid(frame_cmd_t cmd, uint8_t *attr_id, uint16_t num, int response, uint8_t packet_id);
static int32_t set_get_unpack(frame_cmd_t cmd, uint8_t *data, uint16_t len);
/*cmd unpack table*/
static int32_t set_unpack(uint8_t *data, uint16_t len);
static int32_t service_unpack(uint8_t *data, uint16_t len);
static int32_t get_unpack(uint8_t *data, uint16_t len);
static int32_t notify_status_unpack(uint8_t *data, uint16_t len);
static int32_t report_unpack(uint8_t *data, uint16_t len);
static int32_t heartbeat_unpack(uint8_t *data, uint16_t len);
static int32_t get_deviceinfo_unpack(uint8_t *data, uint16_t len);
static int32_t get_snapshot_unpack(uint8_t *data, uint16_t len);
static int32_t mcu_notify_status_unpack(uint8_t *data, uint16_t len);
static int32_t timeget_unpack(uint8_t *data, uint16_t len);
static int32_t netconfig_unpack(uint8_t *data, uint16_t len);
static int32_t getmode_unpack(uint8_t *data, uint16_t len);
static int32_t setmode_unpack(uint8_t *data, uint16_t len);
#if CONFIG_QM_SERIAL_EVENT_SUPPORT
static int32_t event_unpack(uint8_t *data, uint16_t len);
#endif
#if CONFIG_QM_SERIAL_ACTION_SUPPORT
static int32_t action_unpack(uint8_t *data, uint16_t len);
#endif
static int32_t prodtst_entry_unpack(uint8_t *data, uint16_t len);
static int32_t prodtst_status_unpack(uint8_t *data, uint16_t len);
#if CONFIG_QM_SERIAL_OTA_SUPPORT
static int32_t ota_start_unpack(uint8_t *data, uint16_t len);
static int32_t ota_transfer_unpack(uint8_t *data, uint16_t len);
static int32_t ota_querycrc_unpack(uint8_t *data, uint16_t len);
static int32_t ota_verify_unpack(uint8_t *data, uint16_t len);
static int32_t ota_switch_unpack(uint8_t *data, uint16_t len);
#endif
#if CONFIG_SERIAL_MCU_OTA_SUPPORT
static int32_t mcu_ota_ack_unpack(uint8_t *data, uint16_t len);
static int32_t mcu_ota_notify_unpack(uint8_t *data, uint16_t len);
#endif

/*cmd pack table*/
#if 0
int32_t set_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t service_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t get_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t notify_status_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t report_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t heartbeat_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t get_deviceinfo_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t get_snapshot_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t mcu_notify_status_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t timeget_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t netconfig_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t getmode_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t setmode_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t event_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t action_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t prodtst_entry_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t prodtst_status_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
#if CONFIG_QM_SERIAL_OTA_SUPPORT
int32_t ota_start_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t ota_transfer_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t ota_querycrc_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t ota_verify_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
int32_t ota_switch_pack(pack_info_t *info, uint8_t *buf, uint16_t *buf_len);
#endif
#endif
static serial_context_t *g_serial_context;

typedef struct
{
    uint8_t packet_id;
    uint8_t code;
    uint8_t *data;
    uint16_t len;
}pack_info_t;

typedef struct
{
    frame_cmd_t cmd;
    int32_t (*unpack)(uint8_t *data, uint16_t len);
}qm_serial_cmd_table_t;

static qm_serial_cmd_table_t g_qm_serial_cmd_table[] = {

    { CMD_SET,               set_unpack,                    },
    { CMD_SERVICE,           service_unpack,                },
    { CMD_GET,               get_unpack,                    },
    { CMD_NOTIFY_STATUS,     notify_status_unpack,          },
    { CMD_REPORT,            report_unpack,                 },
    { CMD_HEARTBEAT,         heartbeat_unpack,              },
    { CMD_GET_DEVICEINFO,    get_deviceinfo_unpack,         },
    { CMD_GET_SNAPSHOT,      get_snapshot_unpack,           },
    { CMD_MCU_NOTIFY_STATUS, mcu_notify_status_unpack,      },
    { CMD_TIMEGET,           timeget_unpack,                },
    { CMD_NETCONFIG,         netconfig_unpack,              },
    { CMD_GETMOD,            getmode_unpack,                },
    { CMD_SETMOD,            setmode_unpack,                },
#if CONFIG_QM_SERIAL_EVENT_SUPPORT
    { CMD_EVENT,             event_unpack,                  },
#endif
#if CONFIG_QM_SERIAL_ACTION_SUPPORT
    { CMD_ACTION,            action_unpack,                 },
#endif
    { CMD_PRODTST_ENTERY,    prodtst_entry_unpack,          },
    { CMD_PRODTST_STATUS,    prodtst_status_unpack,         },
#if CONFIG_QM_SERIAL_OTA_SUPPORT
    { CMD_OTA_START,         ota_start_unpack,              },
    { CMD_OTA_TRANSFER,      ota_transfer_unpack,           },
    { CMD_OTA_QUERYCRC,      ota_querycrc_unpack,           },
    { CMD_OTA_VERIFY,        ota_verify_unpack,             },
    { CMD_OTA_SWITCH,        ota_switch_unpack,             },
#endif
#if CONFIG_SERIAL_MCU_OTA_SUPPORT
    { CMD_MCU_OTA_ACK,       mcu_ota_ack_unpack,            },
    { CMD_MCU_OTA_NOTIFY,    mcu_ota_notify_unpack,         },
#endif
};

#define QM_SERIAL_CMD_TABLE_NUM   (sizeof(g_qm_serial_cmd_table)/sizeof(g_qm_serial_cmd_table[0]))

#if CONFIG_QM_SERIAL_OTA_SUPPORT

static int32_t ota_start_unpack(uint8_t *data, uint16_t len)
{
    return qm_serial_ota_parse(CMD_OTA_START, data, len);
}

static int32_t ota_transfer_unpack(uint8_t *data, uint16_t len)
{
    return qm_serial_ota_parse(CMD_OTA_TRANSFER, data, len);
}

static int32_t ota_querycrc_unpack(uint8_t *data, uint16_t len)
{
    return qm_serial_ota_parse(CMD_OTA_QUERYCRC, data, len);
}

static int32_t ota_verify_unpack(uint8_t *data, uint16_t len)
{
    return qm_serial_ota_parse(CMD_OTA_VERIFY, data, len);
}

static int32_t ota_switch_unpack(uint8_t *data, uint16_t len)
{
    return qm_serial_ota_parse(CMD_OTA_SWITCH, data, len);  
}

#endif

#if CONFIG_SERIAL_MCU_OTA_SUPPORT
static serial_mcu_ota_notify_fn g_mcu_ota_user_notify;

static void mcu_ota_notify(serial_mcu_ota_event_t event,
                           const serial_mcu_ota_info_t *info,
                           void *user)
{
    if (event == SERIAL_MCU_OTA_EVENT_FINISH ||
        event == SERIAL_MCU_OTA_EVENT_ABORT ||
        event == SERIAL_MCU_OTA_EVENT_ERROR) {
        qm_serial_state_set(SERIAL_STATE_HEARTBEAT);
    }

    if (g_mcu_ota_user_notify) {
        g_mcu_ota_user_notify(event, info, user);
    }
}

static int32_t mcu_ota_ack_unpack(uint8_t *data, uint16_t len)
{
    return serial_mcu_ota_parse(CMD_MCU_OTA_ACK, data, len);
}

static int32_t mcu_ota_notify_unpack(uint8_t *data, uint16_t len)
{
    return serial_mcu_ota_parse(CMD_MCU_OTA_NOTIFY, data, len);
}

int32_t qm_serial_mcu_ota_start(const serial_mcu_ota_config_t *config)
{
    int32_t ret;
    serial_state_t state;
    serial_mcu_ota_config_t ota_config;

    if (!config) {
        return SERIAL_MCU_OTA_ERR_PARAM;
    }

    ret = qm_serial_state_get(&state);
    if (ret != SERIAL_NO_ERR) {
        return SERIAL_MCU_OTA_ERR_STATE;
    }
    if (state == SERIAL_STATE_OTA) {
        return SERIAL_MCU_OTA_ERR_STATE;
    }

    memcpy(&ota_config, config, sizeof(ota_config));
    g_mcu_ota_user_notify = config->notify;
    ota_config.notify = mcu_ota_notify;

    qm_serial_state_set(SERIAL_STATE_OTA);
    ret = serial_mcu_ota_start(&ota_config);
    if (ret != SERIAL_MCU_OTA_OK) {
        qm_serial_state_set(SERIAL_STATE_HEARTBEAT);
    }

    return ret;
}

int32_t qm_serial_mcu_ota_abort(int32_t reason)
{
    return serial_mcu_ota_abort(reason);
}

#endif

static int32_t set_get_unpack(frame_cmd_t cmd, uint8_t *data, uint16_t len)
{
    uint8_t local_id = 0;
    platform_id_t platform_id = 0;
    serial_data_type_t data_type = 0;
    int32_t ret = SERIAL_NO_ERR;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    serial_context_t *context = qm_serial_context_get();
    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }
    local_id = *data;
    data_type = (serial_data_type_t)data_type_get(local_id);
    if(data_type == TYPE_MCU){
        ret = serial_local_id_find(local_id, &platform_id);
        if(ret == SERIAL_NO_ERR){
        #if CONFIG_QM_SERIAL_RESEND_SUPPORT
            down_queue_send();
        #endif
            if(len > 1){
                tlv_ctx = qm_tlv_ctx_creat();
                if(tlv_ctx == NULL){
                    return SERIAL_ALLOC_MEM_ERR;
                }
                ret = qm_tlv_deserialize(tlv_ctx, data + 1, len - 1, QM_BLOCK_TYPE_DTTV);
                if(ret != 0){
                    qm_tlv_ctx_delete(tlv_ctx);
                    return SERIAL_DATA_TYPE_ERR;
                }
            }
            if(context->callbacks.m_responsecb){
                context->callbacks.m_responsecb(cmd, platform_id, data + 1, len - 1, 0, tlv_ctx);
            }
            qm_tlv_ctx_delete(tlv_ctx); 
        }
    }
    else if(data_type > TYPE_MCU && data_type < TYPE_MAX){
#if CONFIG_QM_SERIAL_RAW_SUPPORT
        if(context->callbacks.m_ramcb){
            context->callbacks.m_ramcb(data_type, data, len);
        }
#endif
    }
    return SERIAL_NO_ERR;
}

static int32_t set_unpack(uint8_t *data, uint16_t len)
{
    return set_get_unpack(CMD_SET, data, len);
}

static int32_t get_unpack(uint8_t *data, uint16_t len)
{
    return set_get_unpack(CMD_GET, data, len);
}

static int32_t service_unpack(uint8_t *data, uint16_t len)
{
    int32_t ret = SERIAL_NO_ERR;
    ret = serial_service_handler(data, len);
    if(ret != 0){
        serial_generate_send_with_code(CMD_SERVICE, 0, SERIAL_COM_ERROR, NULL, 0);
    }else{
        serial_generate_send_with_code(CMD_SERVICE, 0, SERIAL_COM_OK, NULL, 0);
    }
    return SERIAL_NO_ERR;
}

static int32_t notify_status_unpack(uint8_t *data, uint16_t len)
{
    return SERIAL_NO_ERR;
}

#if CONFIG_QM_SERIAL_EVENT_SUPPORT

static int32_t event_unpack(uint8_t *data, uint16_t len)
{
    uint32_t eaid = 0;
#if CONFIG_QM_SERIAL_EVENT_ONLY_NOTIFY_SUPPORT
    qm_list_node_t *node = NULL;
    qm_list_node_t *tmp_node = NULL;
#endif
    int32_t ret = SERIAL_NO_ERR;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    serial_context_t *context = qm_serial_context_get();

    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }
    serial_generate_send(CMD_EVENT, 0, NULL, 0);
    if(context->state != SERIAL_STATE_NORMAL){
        SERIAL_LOG("process error");
        return SERIAL_PROCESS_ERR;
    }

    eaid = (uint32_t)(*data);

#if CONFIG_QM_SERIAL_EVENT_NOTIFY_SUPPORT

    if(len - 1 == 0){
        tlv_ctx = NULL;
    }else{
        if(len - 1 < DATA_MIN_LEN){
            return SERIAL_PACKET_LEN_ERR;
        }
        tlv_ctx = qm_tlv_ctx_creat();
        if(tlv_ctx == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }
        ret = qm_tlv_deserialize(tlv_ctx, data + 1, len - 1, QM_BLOCK_TYPE_DTTV);
        if(ret != 0){
            qm_tlv_ctx_delete(tlv_ctx);
            return SERIAL_PARAM_ERR;
        }
    }
    if(context->callbacks.m_reportcb){
        ret = context->callbacks.m_reportcb(CMD_EVENT, data, len, (uint8_t)eaid, tlv_ctx);
        if(ret != SERIAL_NO_ERR){
            if(tlv_ctx){
                qm_tlv_ctx_delete(tlv_ctx);
            }
            return SERIAL_NO_ERR;
        }
    }
#endif

#if CONFIG_QM_SERIAL_EVENT_ONLY_NOTIFY_SUPPORT

    if(tlv_ctx == NULL){
        tlv_ctx = qm_tlv_ctx_creat();
        if(tlv_ctx == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }
    }
    qm_tlv_ctx_arg_set(tlv_ctx, (void*)eaid);
    node = qm_list_node_new((void*)tlv_ctx);
    if(node == NULL){
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_ALLOC_MEM_ERR;
    }
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif

    if(context->event_queue->len < CONFIG_QM_SERIAL_EVENT_MAX_NUM){
        qm_list_rpush(context->event_queue, node);
    }else{
        tmp_node = qm_list_lpop(context->event_queue);
        if(tmp_node){
            qm_tlv_ctx_delete((qm_tlv_ctx_t*)tmp_node->val);
            qm_list_node_destroy(tmp_node);
        }
        qm_list_rpush(context->event_queue, node);
    }
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

    if(context->callbacks.m_reportcb){
        context->callbacks.m_reportcb(CMD_EVENT, NULL, 0, 0, NULL);
    }
#endif
    return SERIAL_NO_ERR; 
}

#endif

#if CONFIG_QM_SERIAL_ACTION_SUPPORT
static int32_t action_unpack(uint8_t *data, uint16_t len)
{
    uint8_t local_id = 0;
    int32_t ret = SERIAL_NO_ERR;
    platform_id_t platform_id = 0;
    serial_context_t *context = qm_serial_context_get();

    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }

    local_id = *data;
    ret = serial_local_id_find(local_id, &platform_id);
    if(ret == SERIAL_NO_ERR){
        if(context->callbacks.m_responsecb){
            context->callbacks.m_responsecb(CMD_ACTION, platform_id, data + 1, len - 1, 0, NULL);
        }
    }
    return SERIAL_NO_ERR;
}
#endif

static int32_t getmode_unpack(uint8_t *data, uint16_t len)
{
    int32_t ret = SERIAL_NO_ERR;
    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }

    ret = serial_getmode_handler(data, len);
    if(ret != 0){
        serial_generate_send_with_code(CMD_GETMOD, 0, SERIAL_COM_ERROR, NULL, 0);
    }
    return SERIAL_NO_ERR;
}

static int32_t setmode_unpack(uint8_t *data, uint16_t len)
{
    int32_t ret = SERIAL_NO_ERR;
    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }

    ret = serial_setmode_handler(data, len);
    if(ret != 0){
        serial_generate_send_with_code(CMD_SETMOD, 0, SERIAL_COM_ERROR, NULL, 0);
    }else{
        serial_generate_send_with_code(CMD_SETMOD, 0, SERIAL_COM_OK, NULL, 0);
    }
    return SERIAL_NO_ERR;
}

static int32_t timeget_unpack(uint8_t *data, uint16_t len)
{
    int32_t ret = SERIAL_NO_ERR;
    serial_time_info_t time_info = {0};
    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }
    time_info.timezone = *data;
    ret = qm_serial_get_time(&time_info);
    if(ret != 0){
        serial_generate_send_with_code(CMD_TIMEGET, 0, SERIAL_COM_ERROR, (uint8_t*)&time_info, sizeof(time_info));
    }else{
        serial_generate_send_with_code(CMD_TIMEGET, 0, SERIAL_COM_OK, (uint8_t*)&time_info, sizeof(time_info));
    }
    return SERIAL_NO_ERR; 
}

static int32_t netconfig_unpack(uint8_t *data, uint16_t len)
{
    network_config_type_t type = 0;
    serial_context_t *context = qm_serial_context_get();
    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }
    type = (network_config_type_t)(*data);
    if(type < NETWORK_CONFIG_START || type > NETWORK_CONFIG_END){
        return SERIAL_DATA_TYPE_ERR;
    }
    serial_generate_send(CMD_NETCONFIG, 0, NULL, 0);
    if(context->callbacks.m_network_configcb){
        context->callbacks.m_network_configcb(type);
    }
    return SERIAL_NO_ERR; 
}

static int32_t prodtst_entry_unpack(uint8_t *data, uint16_t len)
{
    module_prodtst_type_t type = 0;
    serial_context_t *context = qm_serial_context_get();
    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }
    type = (module_prodtst_type_t)(*data);
    if(type <= MODULE_PRODTST_TYPE_START || type >= MODULE_PRODTST_TYPE_END){
        return SERIAL_DATA_TYPE_ERR;
    }
    serial_generate_send(CMD_PRODTST_ENTERY, 0, NULL, 0);
    context->prodtst_type = type;

    if(context->callbacks.m_prodtst_configcb){
        context->callbacks.m_prodtst_configcb(type);
    }
    return SERIAL_NO_ERR; 
}

static int32_t prodtst_status_unpack(uint8_t *data, uint16_t len)
{
    return SERIAL_NO_ERR; 
}

static int32_t mcu_notify_status_unpack(uint8_t *data, uint16_t len)
{
    serial_context_t *context = qm_serial_context_get();
    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }
    serial_generate_send(CMD_MCU_NOTIFY_STATUS, 0, NULL, 0);

    if(context->callbacks.m_mcu_status_notifycb){
        context->callbacks.m_mcu_status_notifycb(*data);
    }
    return SERIAL_NO_ERR; 
}

static int32_t heartbeat_unpack(uint8_t *data, uint16_t len)
{
    uint8_t heartbeat_type = 0;
    serial_context_t *context = qm_serial_context_get();
    
    if(len < 1){
        return SERIAL_PACKET_LEN_ERR;
    }

    heartbeat_type = *data;

    if(context->state == SERIAL_STATE_HEARTBEAT ){
        context->state = SERIAL_STATE_GETDEVICEINFO;
        serial_generate_send(CMD_NOTIFY_STATUS, 0, &context->module_status, 1);
        if(context->module_prodtst_status != MODULE_PRODTST_STATUS_NONE){
            serial_generate_send(CMD_PRODTST_STATUS, 0, &context->module_prodtst_status, 1);
        }
        context->keep_alive_interval = SERIAL_KEPPALIVE_TIMEOUT_DEFAULT_MS;
        qm_utils_time_countdown_ms(&context->next_ping_time, context->keep_alive_interval);
        qm_utils_time_start(&context->process_get_time);

    }else{
        if(heartbeat_type == HEARTBEAT_RESET){
            serial_generate_send(CMD_NOTIFY_STATUS, 0, &context->module_status, 1);
            context->state = SERIAL_STATE_GETDEVICEINFO;
            qm_utils_time_start(&context->process_get_time);
        }else if(heartbeat_type == HEARTBEAT_COMMON){

        }
    }

#if CONFIG_QM_SERIAL_HEARTBEAT_NOTIFY_SUPPORT
    if(context->callbacks.m_heartbeatcb){
        context->callbacks.m_heartbeatcb(heartbeat_type);
    }
#endif
    return SERIAL_NO_ERR;
}

int32_t snapshot_update_clear(qm_tlv_ctx_t *tlv_ctx)
{
    int is_update = 0;
    qm_tlv_block_t *tlv_block = NULL;
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
        qm_tlv_block_is_update(tlv_block, &is_update);
        if(is_update){
            qm_tlv_block_update(tlv_block, 0);
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    return SERIAL_NO_ERR;
}

static int32_t report_unpack(uint8_t *data, uint16_t len)
{
#if CONFIG_QM_SERIAL_REPORT_NOTIFY_SUPPORT
    #define TLV_REPORT_BUF_SIZE  32
    uint8_t sta_buf[TLV_REPORT_BUF_SIZE] = {0};
    uint8_t *alloc_buf = NULL;
    uint8_t *tlv_buf = NULL;
    int count = 0;
    int update_count = 0;
    int tlv_len = 0;
#endif

    int32_t ret = SERIAL_NO_ERR;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    serial_context_t *context = qm_serial_context_get();

    if(len < FRAME_REPORT_MIN_LEN){
        return SERIAL_PACKET_LEN_ERR;
    }

    serial_generate_send(CMD_REPORT, 0, NULL, 0);

    if(context->state != SERIAL_STATE_NORMAL){
        SERIAL_LOG("process error");
        return SERIAL_PROCESS_ERR;
    }

#if CONFIG_QM_SERIAL_LOWPOWER_SUPPORT == 0
    if(qm_serial_module_status_get() != MODULE_CONNECTED){
        SERIAL_LOG("module unconnected");
        return SERIAL_PROCESS_ERR;
    }
#endif

    if(!context->snapshot_ctx){
        SERIAL_LOG("no snapshot");
        return SERIAL_PROCESS_ERR;
    }

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return SERIAL_ALLOC_MEM_ERR;
    }

    ret = qm_tlv_deserialize(tlv_ctx, data, len, QM_BLOCK_TYPE_DTTV);
    if(ret != 0){
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_DATA_TYPE_ERR;  
    }

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    ret = qm_tlv_merge(context->snapshot_ctx, tlv_ctx);
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

    if(ret != 0){
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_DATA_TYPE_ERR;
    }

    if(!context->callbacks.m_reportcb){
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_NO_ERR;  
    }

#if CONFIG_QM_SERIAL_REPORT_NOTIFY_SUPPORT

    count = qm_tlv_ctx_count_get(tlv_ctx);
    update_count = qm_tlv_ctx_update_count_get(context->snapshot_ctx);
    if(count == update_count){
        ret = context->callbacks.m_reportcb(CMD_REPORT, data, len, 0, tlv_ctx);
        if(ret == SERIAL_NO_ERR){
            snapshot_update_clear(context->snapshot_ctx);
        }  
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_NO_ERR;
    }else{
        qm_tlv_ctx_delete(tlv_ctx);
    }

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return SERIAL_ALLOC_MEM_ERR;
    }

    ret = qm_tlv_divide(tlv_ctx, context->snapshot_ctx, QM_TLV_DIVIDE_TYPE_UPDATE);
    if(ret != 0){
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_ALLOC_MEM_ERR;
    }

    qm_tlv_serialize(tlv_ctx, NULL, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);
    if(tlv_len == 0){
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_DATA_TYPE_ERR;
    }

    if(tlv_len > TLV_REPORT_BUF_SIZE){
        alloc_buf = (uint8_t*)qm_malloc(tlv_len);
        if(alloc_buf == NULL){
            qm_tlv_ctx_delete(tlv_ctx);
            return SERIAL_ALLOC_MEM_ERR;
        }

        tlv_buf = alloc_buf;
    }else{
        tlv_buf = sta_buf;
    }

    qm_tlv_serialize(tlv_ctx, tlv_buf, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_UPDATE);

    ret = context->callbacks.m_reportcb(CMD_REPORT, tlv_buf, tlv_len, 0, tlv_ctx);
    if(ret == SERIAL_NO_ERR){
        snapshot_update_clear(context->snapshot_ctx);
    }
    
    if(alloc_buf){
        qm_free(alloc_buf);
        alloc_buf = NULL;
    }
    qm_tlv_ctx_delete(tlv_ctx);

#endif

#if CONFIG_QM_SERIAL_ONLY_REPORT_NOTIFY_SUPPORT
    update_count = qm_tlv_ctx_update_count_get(context->snapshot_ctx);
    if(update_count == 0){
        return SERIAL_NO_ERR;
    }
    context->callbacks.m_reportcb(CMD_REPORT, NULL, 0, 0, NULL);
#endif

    return SERIAL_NO_ERR;
}

static int32_t get_snapshot_unpack(uint8_t *data, uint16_t len)
{
    int32_t ret = SERIAL_NO_ERR;
    serial_context_t *context = qm_serial_context_get();

    if(len < FRAME_SNAPSHOT_MIN_LEN){
        return SERIAL_PACKET_LEN_ERR;
    }

    if(context->state != SERIAL_STATE_GETDEVICESNAPSHOT){
        return SERIAL_PROCESS_ERR;
    }

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif

    if(context->snapshot_ctx){
        qm_tlv_ctx_delete(context->snapshot_ctx);
    }

    context->snapshot_ctx = qm_tlv_ctx_creat();
    if(context->snapshot_ctx == NULL){
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
        qm_serial_mutex_unlock(context->serial_mutex);
#endif
        return SERIAL_ALLOC_MEM_ERR;
    }

    ret = qm_tlv_deserialize(context->snapshot_ctx, data, len, QM_BLOCK_TYPE_DTTV);
    if(ret != 0){
        qm_tlv_ctx_delete(context->snapshot_ctx);
        context->snapshot_ctx = NULL;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
        qm_serial_mutex_unlock(context->serial_mutex);
#endif
        return SERIAL_DATA_TYPE_ERR;
    }

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

    context->state = SERIAL_STATE_NORMAL;

    SERIAL_LOG("need report");

#if CONFIG_QM_SERIAL_REPORT_NOTIFY_SUPPORT
    if(context->callbacks.m_reportcb){
        ret = context->callbacks.m_reportcb(CMD_GET_SNAPSHOT, data, len, 0, context->snapshot_ctx);
        if(ret == SERIAL_NO_ERR){
            snapshot_update_clear(context->snapshot_ctx);
        }
    }
#endif
 
#if CONFIG_QM_SERIAL_ONLY_REPORT_NOTIFY_SUPPORT
    if(context->callbacks.m_reportcb){
        context->callbacks.m_reportcb(CMD_GET_SNAPSHOT, NULL, 0, 0, NULL);
    }
#endif
    return ret;
}

static int32_t get_deviceinfo_unpack(uint8_t *data, uint16_t len)
{
    int32_t ret = 0;
    device_ver_t ver = {0};
    qm_tlv_ctx_t *tlv_ctx = NULL;
    serial_context_t *context = qm_serial_context_get();

    if(len < FRAME_DEVICEINFO_GET_MIN_LEN)
        return SERIAL_PACKET_LEN_ERR;

    if(context->state != SERIAL_STATE_GETDEVICEINFO){
        return SERIAL_PROCESS_ERR;
    }

    ver.main_ver = *(data);
    ver.sub_ver = *(data + 1);
    ver.fix_ver = *(data + 2);

    if(qm_serial_module_status_get() == MODULE_CONNECTED){
        context->state = SERIAL_STATE_GETDEVICESNAPSHOT;
    }else{
    #if CONFIG_QM_SERIAL_LOWPOWER_SUPPORT == 0
        context->state = SERIAL_STATE_NORMAL; 
    #else
        context->state = SERIAL_STATE_GETDEVICESNAPSHOT;
    #endif
    }

    if(len == 3){
        if(context->callbacks.m_device_onlivecb){
            context->callbacks.m_device_onlivecb(&ver, NULL, 0, NULL);
        }
    }else{

        tlv_ctx = qm_tlv_ctx_creat();
        if(tlv_ctx == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }

        ret = qm_tlv_deserialize(tlv_ctx, data + 3, len - 3, QM_BLOCK_TYPE_DTTV);
        if(ret != 0){
            qm_tlv_ctx_delete(tlv_ctx);
            return SERIAL_DATA_TYPE_ERR;
        }

        if(context->callbacks.m_device_onlivecb){
            context->callbacks.m_device_onlivecb(&ver, data + 3, len - 3, tlv_ctx);
        }
        qm_tlv_ctx_delete(tlv_ctx);
    }

    return SERIAL_NO_ERR;
}

serial_context_t *qm_serial_context_get(void)
{
    return g_serial_context;
}

static uint8_t frame_chksum(uint8_t *buffer, uint16_t length)
{
    uint16_t i;
    uint8_t check_sum = 0;
    for (i = 0; i < length; i++){
        check_sum += buffer[i];
    }
    return check_sum;
}

static uint8_t packet_id_get(void)
{
    serial_context_t *serial_context = NULL;
    serial_context = qm_serial_context_get();
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(serial_context->serial_mutex);
#endif
    serial_context->packet_id = (serial_context->packet_id == (MAX_SERIAL_MCU_PACKET_ID)) ? MIN_SERIAL_MCU_PACKET_ID : serial_context->packet_id + 1;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(serial_context->serial_mutex);
#endif
    return  serial_context->packet_id;
}

static serial_data_type_t data_type_get(uint8_t local_id)
{
    if(local_id >= MIN_SERIAL_MCU_PACKET_ID && local_id <= MAX_SERIAL_MCU_PACKET_ID){
        return TYPE_MCU;
    }else if(local_id >= MIN_SERIAL_VOICE_PACKET_ID){
        return TYPE_VOICE;
    }else{
        return TYPE_NULL;
    }
}

static int32_t frame_parse(uint8_t *data, uint16_t len)
{
    int index = 0;
    qm_serial_cmd_table_t *cmd_handler = NULL;
    int32_t ret = SERIAL_NO_ERR;
    frame_header_t *header = NULL;
    if(*(data + len - 1) != frame_chksum(data, len - 1)){
        return SERIAL_CHKSUM_ERR;
    }
    header = (frame_header_t*)data;

    for(index = 0; index < QM_SERIAL_CMD_TABLE_NUM; index++){
        cmd_handler = &g_qm_serial_cmd_table[index];
        if(cmd_handler->cmd != header->cmd){
            continue;
        }
        if(!cmd_handler->unpack){
            continue;
        }
        ret = cmd_handler->unpack(data + FRAME_HEAD_LEN, len - FRAME_HEAD_LEN -1);
    }

    return ret;
}


static void frame_head_init(uint8_t *buffer, frame_cmd_t cmd, uint16_t len)
{
    frame_header_t *head = (frame_header_t *)buffer;

    head->head = cpu_to_be16(FRAME_HEAD);
    head->ver  = PROTOCOL_VER;
    head->cmd  = cmd;
    head->len = cpu_to_be16(len);
}

static int32_t frame_creat(frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t data_len, uint8_t *buf, uint16_t *buf_len)
{
    frame_unit_t *frame_unit = NULL;
    frame_generic_t *frame_generic = NULL;

    switch (cmd){

       case CMD_REPORT:
       case CMD_NETCONFIG:
       case CMD_GET_DEVICEINFO:
       case CMD_GET_SNAPSHOT:
       case CMD_EVENT:
       case CMD_MCU_NOTIFY_STATUS:
#if CONFIG_QM_SERIAL_OTA_SUPPORT
       case CMD_OTA_QUERYCRC:
#endif
       case CMD_PRODTST_ENTERY:

           if(buf && *buf_len >= sizeof(frame_unit_t)){
               frame_unit = (frame_unit_t*)buf;
               frame_head_init(buf, cmd, 0);
               frame_unit->chksum = frame_chksum(buf, sizeof(frame_unit_t) - 1);
           }
           *buf_len = sizeof(frame_unit_t);

       break;

       case CMD_HEARTBEAT:
       case CMD_NOTIFY_STATUS:
#if CONFIG_QM_SERIAL_OTA_SUPPORT
       case CMD_OTA_START:
#endif
       case CMD_PRODTST_STATUS:

           if(buf && *buf_len >= sizeof(frame_generic_t)){
               frame_generic = (frame_generic_t*)buf;
               frame_head_init(buf, cmd, 1);
               frame_generic->data = *data;
               frame_generic->chksum = frame_chksum(buf, sizeof(frame_generic_t) - 1);
           }
           *buf_len = sizeof(frame_generic_t);

       break;

       case CMD_GET:
       case CMD_SET:
#if CONFIG_QM_SERIAL_ACTION_SUPPORT
       case CMD_ACTION:
#endif
           if(buf && *buf_len >= FRAME_MIN_LEN + data_len + 1){
               frame_head_init(buf, cmd, data_len + 1);
               *(buf + FRAME_HEAD_LEN) = packet_id;
               memcpy(buf + FRAME_HEAD_LEN + 1, data, data_len);
               *(buf + FRAME_HEAD_LEN + data_len + 1) = frame_chksum(buf, FRAME_HEAD_LEN + data_len + 1);
           }
           *buf_len = FRAME_MIN_LEN + data_len + 1;

       break;

#if CONFIG_QM_SERIAL_OTA_SUPPORT || CONFIG_SERIAL_MCU_OTA_SUPPORT
#if CONFIG_QM_SERIAL_OTA_SUPPORT
       case CMD_OTA_TRANSFER:
       case CMD_OTA_VERIFY:
#endif
#if CONFIG_SERIAL_MCU_OTA_SUPPORT
       case CMD_MCU_OTA_ENTER:
       case CMD_MCU_OTA_BEGIN:
       case CMD_MCU_OTA_DATA:
       case CMD_MCU_OTA_END:
       case CMD_MCU_OTA_ABORT:
#endif


        if(buf && *buf_len >= FRAME_MIN_LEN + data_len){
           frame_head_init(buf, cmd, data_len);
           if(data && data_len > 0){
               memcpy(buf + FRAME_HEAD_LEN, data, data_len);
           }
           *(buf + FRAME_HEAD_LEN + data_len) = frame_chksum(buf, FRAME_HEAD_LEN + data_len);
        }
        *buf_len = FRAME_MIN_LEN+data_len;
       break;
#endif

       case CMD_GETMOD:
       
       case CMD_SETMOD:
       case CMD_TIMEGET:
       case CMD_SERVICE:
        if(buf && *buf_len >= FRAME_MIN_LEN + data_len + 1){
            frame_head_init(buf, cmd, data_len + 1);
            memcpy(buf + FRAME_HEAD_LEN, &code, 1);
            if(data && data_len > 0){
                memcpy(buf + FRAME_HEAD_LEN + 1, data, data_len);
            }
           *(buf + FRAME_HEAD_LEN + data_len + 1) = frame_chksum(buf, FRAME_HEAD_LEN + data_len + 1);
        }
        *buf_len = FRAME_MIN_LEN + data_len + 1;
        

       break;

       default:
            return SERIAL_CMD_ERR;
       break;
    }

    return SERIAL_NO_ERR;

}

static int32_t serial_frame_info_creat(frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t len, uint8_t resend, uint8_t **raw)
{
    int32_t ret = 0;
    uint8_t *buf = NULL;
    uint16_t packet_len = 0;
    uint16_t raw_len = 0;
    frame_info_t *info = NULL;
    ret = frame_creat(cmd, packet_id, code, data, len, NULL, &packet_len);
    if(ret != SERIAL_NO_ERR){
        return ret;
    }

    raw_len = sizeof(frame_info_t) + packet_len -1;
    buf = qm_serial_malloc(raw_len);
    if(buf == NULL){
        return SERIAL_ALLOC_MEM_ERR;
    } 

    memset(buf, 0, raw_len);

    info = (frame_info_t*)buf;
    info->len = packet_len;
    info->resend = resend;

    ret = frame_creat(cmd, packet_id, code, data, len, info->data, &packet_len);
    if(ret != SERIAL_NO_ERR){
        return ret;
    } 

    *raw = buf;

    return SERIAL_NO_ERR;
}


static uint32_t queue_len_get(qm_list_t *queue)
{
    uint32_t queue_len = 0;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    queue_len = queue->len;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    return queue_len;
}

static int32_t queue_push(qm_list_t *queue, uint8_t *raw)
{
    qm_list_node_t *node = NULL;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
#endif
    node = (qm_list_node_t*)raw;
    qm_list_node_init(node, raw+sizeof(qm_list_node_t));

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    qm_list_rpush(queue, node);
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_sem_signal(context->send_sem);
#endif
    return SERIAL_NO_ERR; 
}

/*
 *   -------------------------------
 *  |  len  | is_resend | PAYLOAD |
 *   ------------------------------
 *    2Bytes    1byte     nByte
 */

#if CONFIG_QM_SERIAL_RESEND_SUPPORT

static int32_t serial_generate_resend(frame_cmd_t cmd, uint8_t packet_id, uint8_t *data, uint16_t len, uint8_t **buf)
{
    uint32_t ret = 0;
    uint8_t *raw = NULL;
    qm_list_t *queue = NULL;
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    queue = context->down_queue;

    if(queue_len_get(queue) >= CONFIG_QM_SERIAL_DOWN_QUEUE_NUM){
        return SERIAL_SEND_BUSY_ERR;
    }

    ret = serial_frame_info_creat(cmd, packet_id, 0, data, len, 1, &raw);
    if(ret != SERIAL_NO_ERR){
        return ret;
    }

    *buf = raw;

    return queue_push(queue, raw);
}

#endif

int32_t serial_generate_send(frame_cmd_t cmd, uint8_t packet_id, uint8_t *data, uint16_t len)
{
    uint32_t ret = 0;
    uint8_t *raw = NULL;
    qm_list_t *queue = NULL;
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    queue = context->send_queue;

    if(queue_len_get(queue) >= CONFIG_QM_SERIAL_SEND_QUEUE_NUM){
        return SERIAL_SEND_BUSY_ERR;
    }

    ret = serial_frame_info_creat(cmd, packet_id, 0, data, len, 0, &raw);
    if(ret != SERIAL_NO_ERR){
        return ret;
    }

    return queue_push(queue, raw);
}

static int32_t serial_generate_send_with_code(frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t len)
{
    uint32_t ret = 0;
    uint8_t *raw = NULL;
    qm_list_t *queue = NULL;
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    queue = context->send_queue;

    if(queue_len_get(queue) >= CONFIG_QM_SERIAL_SEND_QUEUE_NUM){
        return SERIAL_SEND_BUSY_ERR;
    }

    ret = serial_frame_info_creat(cmd, packet_id, code, data, len, 0, &raw);
    if(ret != SERIAL_NO_ERR){
        return ret;
    }

    return queue_push(queue, raw);
}

static uint8_t local_id_get(uint8_t *raw)
{
    return *(raw + FRAME_HEAD_LEN);
}

static int32_t serial_id_info_push(serial_data_type_t type, platform_id_t platform_id, uint8_t local_id, uint8_t *data)
{
    serial_context_t *context = NULL;
    serial_id_info_t *id_info = NULL;
    uint8_t *buf = NULL;
    uint16_t buf_len = 0;
    context = qm_serial_context_get();

    qm_list_node_t *node = NULL;

    buf_len = sizeof(serial_id_info_t)+sizeof(qm_list_node_t);
    buf = qm_serial_malloc(buf_len);
    if(buf == NULL){
        return SERIAL_ALLOC_MEM_ERR;
    }
    node = (qm_list_node_t*)buf;

    memset(buf, 0, buf_len);

    qm_list_node_init(node, buf + sizeof(qm_list_node_t));

    id_info = (serial_id_info_t*)(buf + sizeof(qm_list_node_t));
    id_info->platform_id = platform_id;
    id_info->local_id = local_id;
#if CONFIG_QM_SERIAL_RESEND_SUPPORT
    id_info->retry_count = CONFIG_QM_SERIAL_SEND_RETRY_COUNT;
#else
    id_info->retry_count = 0;
#endif
    id_info->data = data;
    id_info->time = 0; 
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    qm_list_rpush(context->id_queue, node);
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    return SERIAL_NO_ERR;
}

static uint8_t is_serial_id_update(void)
{
    serial_context_t *context = NULL;
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    qm_list_t *list = NULL;
    serial_id_info_t *id_info = 0;
    uint8_t *buf = NULL;
    uint8_t is_update = 0;

    context = qm_serial_context_get();
    list = context->id_queue;

    self.direction = LIST_HEAD;
    self.next = list->head;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif

    node = qm_list_iterator_next(&self);
    while (node) {
        buf = (uint8_t*)node;
        id_info = (serial_id_info_t*)(buf+sizeof(qm_list_node_t));
        if(id_info->is_update){
            is_update = 1;
        }
        node = qm_list_iterator_next(&self);
    }
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

    return is_update;
}

static int32_t serial_id_uptime(uint8_t local_id, uint8_t *info)
{
    serial_context_t *context = NULL;
    serial_id_info_t id_info = {0};
    qm_list_node_t *node = NULL;
    serial_id_info_t *p_id_info = NULL;

    context = qm_serial_context_get();

    id_info.local_id = local_id;

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_find(context->id_queue, &id_info);
    if(node){
        p_id_info = (serial_id_info_t*)node->val;
        p_id_info->is_update = 1;
        p_id_info->data = info;
        p_id_info->is_send = 1;
        p_id_info->time = qm_serial_get_timems() + CONFIG_QM_SERIAL_ACK_TIMEOUT_MS;
    }

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

    return SERIAL_NO_ERR;
}

static int id_match(void *a, void *b)
{
    serial_id_info_t *a_info = (serial_id_info_t*)a;
    serial_id_info_t *b_info = (serial_id_info_t*)b;

    if(a_info->local_id == b_info->local_id){
        return 1;
    }else{
        return 0;
    }
}

static int32_t serial_local_id_find(uint8_t local_id, platform_id_t *platform_id)
{
    qm_list_node_t *node = NULL;
    serial_context_t *context = NULL;
    serial_id_info_t id_info = {0};
    serial_id_info_t *id = NULL;
    context = qm_serial_context_get();

    id_info.local_id = local_id;

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_find(context->id_queue, &id_info);

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    if(node){
        id = (serial_id_info_t*)node->val;
        if(!id->is_send){
            return SERIAL_NO_ERR;
        }
        *platform_id = id->platform_id;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
        qm_serial_mutex_lock(context->serial_mutex);
#endif
        qm_list_remove(context->id_queue, node);
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
        qm_serial_mutex_unlock(context->serial_mutex);
#endif
        if(id->data){
            qm_serial_free(id->data);
        }
        qm_serial_free(node);
    }else{
        return SERIAL_MSG_NUM_ERR;
    }
    return SERIAL_NO_ERR;
}

static int32_t serial_cloud_generate_send(frame_cmd_t cmd, platform_id_t id, uint8_t *data, uint16_t len)
{
    int32_t err = SERIAL_NO_ERR;
    uint8_t local_id = 0;
    uint8_t *buf = NULL;
    local_id = packet_id_get();

#if CONFIG_QM_SERIAL_RESEND_SUPPORT

    err = serial_id_info_push(TYPE_MCU, id, local_id, NULL);
    if(err != SERIAL_NO_ERR){
        return err;
    }

    err = serial_generate_resend(cmd, local_id, data, len, &buf);
    if(err != SERIAL_NO_ERR){
        return err;
    }

#else
    err = serial_id_info_push(TYPE_MCU, id, local_id, NULL);
    if(err != SERIAL_NO_ERR){
        return err;
    }

    err = serial_generate_send(cmd, local_id, data, len);
    if(err != SERIAL_NO_ERR){
        return err;
    }
#endif

    return SERIAL_NO_ERR;
}

#if CONFIG_QM_SERIAL_RESEND_SUPPORT

static int32_t down_queue_send(void)
{
    qm_list_node_t *node = NULL;
    qm_list_t *down_queue = NULL;
    qm_list_t *send_queue = NULL;
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");


    down_queue = context->down_queue;
    send_queue = context->send_queue;

    if(is_serial_id_update()){
        return SERIAL_NO_ERR;
    }

    if(queue_len_get(down_queue) == 0){
        return SERIAL_NO_ERR;
    }

    if(queue_len_get(send_queue) >= CONFIG_QM_SERIAL_SEND_QUEUE_NUM){
        SERIAL_LOG("send queue is full");
        return SERIAL_SEND_BUSY_ERR;
    }

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_lpop(down_queue);

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

    queue_push(send_queue, (uint8_t*)node);

    return SERIAL_NO_ERR;
}

#endif

static int32_t id_check_cycle(serial_context_t *pcontext)
{
    qm_list_t *list = pcontext->id_queue;
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    uint8_t *buf = NULL;
    serial_id_info_t *id_info = NULL;
    uint32_t cur_time = 0;
    int32_t send = 0;

    if (!qm_utils_time_is_expired(&pcontext->id_check_time)) {
    //todo[haunglei]: 防止溢出导致发送间隔无穷大
        if(qm_utils_time_left(&pcontext->id_check_time) > pcontext->id_check_interval) {
            qm_utils_time_countdown_ms(&pcontext->id_check_time, pcontext->id_check_interval);
        }
    //
        return SERIAL_NO_ERR;
    }
    qm_utils_time_countdown_ms(&pcontext->id_check_time, pcontext->id_check_interval);

    if(queue_len_get(pcontext->id_queue) == 0){
        return SERIAL_NO_ERR;
    }

    cur_time = qm_serial_get_timems();

    self.direction = LIST_HEAD;
    self.next = list->head;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(pcontext->serial_mutex);
#endif

    node = qm_list_iterator_next(&self);
    while (node) {
        buf = (uint8_t*)node;
        id_info = (serial_id_info_t*)(buf+sizeof(qm_list_node_t));
        if(id_info->is_update && (cur_time - id_info->time) < (UINT32_MAX / 2) ){
            id_info->is_send = 0;
            id_info->is_update = 0;
            if(id_info->retry_count == 0){
                qm_list_remove(list, node);
                if(id_info->data){
                    qm_serial_free(id_info->data);
                }
                qm_serial_free(node);
                send = 1;
            }else{
                qm_list_rpush(pcontext->send_queue, (qm_list_node_t*)id_info->data);
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
                qm_serial_sem_signal(pcontext->send_sem);
#endif
            }
            id_info->retry_count--;
        }
        node = qm_list_iterator_next(&self);
    }

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(pcontext->serial_mutex);
#endif

#if CONFIG_QM_SERIAL_RESEND_SUPPORT
    if(send){
        down_queue_send();
    }
#endif

    return SERIAL_NO_ERR;
}

static int32_t serial_heartbeat_send(void)
{
    uint8_t status=0;
    status = qm_serial_module_status_get();
    return serial_generate_send(CMD_HEARTBEAT, 0, &status, 1);
}

static int32_t serial_deviceinfo_get_send(void)
{
    return serial_generate_send(CMD_GET_DEVICEINFO, 0, NULL, 0);
}

static int32_t serial_snapshot_get_send(void)
{
    return serial_generate_send(CMD_GET_SNAPSHOT, 0, NULL, 0);
}

int32_t qm_serial_state_set(serial_state_t state)
{
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    context->state = state;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    return SERIAL_NO_ERR;
}

int32_t qm_serial_state_get(serial_state_t *state)
{
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(state, SERIAL_NO_INIT_ERR, LOG_TAG, "state NULL");
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    *state = context->state;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    return SERIAL_NO_ERR;
}

module_status_t qm_serial_module_status_get(void)
{
    module_status_t status;
    serial_context_t *context = NULL;
    context=qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    status = context->module_status;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    return status;
}

int32_t qm_serial_module_status_set(module_status_t status)
{
    int32_t need_notify = 0;
    serial_context_t *context = NULL;
    serial_state_t state = SERIAL_STATE_NONE;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

    if(context->module_status != status){
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
        qm_serial_mutex_lock(context->serial_mutex);
#endif
        context->module_status = status;
        state = context->state;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
        qm_serial_mutex_unlock(context->serial_mutex);
#endif
        need_notify = 1;
    }

    if(state != SERIAL_STATE_HEARTBEAT && need_notify){
        serial_generate_send(CMD_NOTIFY_STATUS, 0, &context->module_status, 1);
    }

#if CONFIG_QM_SERIAL_LOWPOWER_SUPPORT == 0
    if(status == MODULE_CONNECTED && need_notify){
        if(context->state == SERIAL_STATE_NORMAL){
            context->state = SERIAL_STATE_GETDEVICESNAPSHOT;
        }
    }
#endif
    return SERIAL_NO_ERR;
}

int32_t qm_serial_module_prodtst_status_set(module_prodtst_status_t status)
{
    int32_t need_notify = 0;
    serial_context_t *context = NULL;
    serial_state_t state = SERIAL_STATE_NONE;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

    if(context->module_prodtst_status != status){
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
        qm_serial_mutex_lock(context->serial_mutex);
#endif
        context->module_prodtst_status = status;
        state = context->state;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
        qm_serial_mutex_unlock(context->serial_mutex);
#endif
        need_notify = 1;
    }

    if(state != SERIAL_STATE_HEARTBEAT && need_notify){
        serial_generate_send(CMD_PRODTST_STATUS, 0, &context->module_prodtst_status, 1);
    }

    return SERIAL_NO_ERR;
}

int32_t qm_serial_cloud_raw_set(platform_id_t id, uint8_t *data, uint16_t len)
{
#if CONFIG_QM_SERIAL_OTA_SUPPORT
    serial_state_t state;
    qm_serial_state_get(&state);
    if(state == SERIAL_STATE_OTA){
        return SERIAL_PROCESS_ERR;
    }
#endif
    return serial_cloud_generate_send(CMD_SET, id, data, len);
}

int32_t qm_serial_cloud_set(platform_id_t id, qm_tlv_ctx_t *tlv_ctx)
{
    #define TLV_SET_BUF_SIZE  16
    int32_t ret = 0;
    uint8_t sta_buf[TLV_SET_BUF_SIZE] = {0};
    uint8_t *alloc_buf = NULL;
    uint8_t *tlv_buf = NULL;
    int tlv_len = 0;
    QM_RETURN_ON_FALSE(tlv_ctx, SERIAL_PARAM_ERR, LOG_TAG, "tlv_ctx NULL");

#if CONFIG_QM_SERIAL_OTA_SUPPORT
    serial_state_t state;
    qm_serial_state_get(&state);
    if(state == SERIAL_STATE_OTA){
        return SERIAL_PROCESS_ERR;
    }
#endif
    qm_tlv_serialize(tlv_ctx, NULL, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);
    if(tlv_len > TLV_SET_BUF_SIZE){
        alloc_buf = (uint8_t*)qm_malloc(tlv_len);
        if(alloc_buf == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }
        tlv_buf = alloc_buf;
    }else{
        tlv_buf = sta_buf;
    }

    qm_tlv_serialize(tlv_ctx, tlv_buf, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);
    ret = serial_cloud_generate_send(CMD_SET, id, tlv_buf, (uint16_t)tlv_len);
    if(alloc_buf){
        qm_free(alloc_buf);
        alloc_buf = NULL;
    }
    return ret;
}

#if CONFIG_QM_SERIAL_ACTION_SUPPORT
int32_t qm_serial_cloud_raw_action(platform_id_t id, uint8_t *data, uint16_t len)
{
#if CONFIG_QM_SERIAL_OTA_SUPPORT
	serial_state_t state;
	qm_serial_state_get(&state);
	if(state == SERIAL_STATE_OTA){
		return SERIAL_PROCESS_ERR;
	}
#endif
	return serial_cloud_generate_send(CMD_ACTION, id, data, len);
}

int32_t qm_serial_cloud_action(platform_id_t id, uint8_t aid, qm_tlv_ctx_t *tlv_ctx)
{
    #define TLV_ACTION_BUF_SIZE  16
    int32_t ret = 0;
    uint8_t sta_buf[TLV_ACTION_BUF_SIZE] = {0};
    uint8_t *alloc_buf = NULL;
    uint8_t *tlv_buf = NULL;
    int tlv_len = 0;

    if(tlv_ctx == NULL){
        return serial_cloud_generate_send(CMD_ACTION, id, &aid, 1);
    }

    qm_tlv_serialize(tlv_ctx, NULL, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);
    if(tlv_len + 1 > TLV_ACTION_BUF_SIZE){
        alloc_buf = (uint8_t*)qm_malloc(tlv_len + 1);
        if(alloc_buf == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }
        tlv_buf = alloc_buf + 1;
    }else{
        tlv_buf = sta_buf + 1;
    }

    tlv_buf[0] = aid;
    qm_tlv_serialize(tlv_ctx, tlv_buf + 1, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_ALL);
    ret = serial_cloud_generate_send(CMD_ACTION, id, tlv_buf, (uint16_t)(tlv_len + 1));
    if(alloc_buf){
        qm_free(alloc_buf);
        alloc_buf = NULL;
    }
    return ret;
}
#endif

int32_t qm_serial_cloud_raw_get_from_mcu(platform_id_t id, uint8_t *attr_id, uint16_t len)
{
#if CONFIG_QM_SERIAL_OTA_SUPPORT
    serial_state_t state;
    qm_serial_state_get(&state);
    if(state == SERIAL_STATE_OTA){
        return SERIAL_PROCESS_ERR;
    }
#endif
    return serial_cloud_generate_send(CMD_GET, id, attr_id, len);
}

int32_t qm_serial_cloud_get_from_mcu(platform_id_t id, qm_tlv_ctx_t *tlv_ctx)
{
    qm_tlv_block_t *tlv_block = NULL;
    #define TLV_GET_BUF_SIZE  8
    uint8_t sta_buf[TLV_GET_BUF_SIZE] = {0};
    uint8_t *alloc_buf = NULL;
    uint8_t *attri_buf = NULL;
    int count = 0;
    uint16_t index = 0;
    uint16_t attri_id = 0;

    QM_RETURN_ON_FALSE(tlv_ctx, SERIAL_PARAM_ERR, LOG_TAG, "tlv_ctx NULL");

#if CONFIG_QM_SERIAL_OTA_SUPPORT
    serial_state_t state;
    qm_serial_state_get(&state);
    if(state == SERIAL_STATE_OTA){
        return SERIAL_PROCESS_ERR;
    }
#endif

    count = qm_tlv_ctx_count_get(tlv_ctx);
    if(count > TLV_GET_BUF_SIZE){
        alloc_buf = (uint8_t *)qm_serial_malloc(count);
        if(alloc_buf == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }
        attri_buf = alloc_buf;
    }else{
        attri_buf = sta_buf;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
        qm_tlv_block_tag_get(tlv_block, &attri_id);
        attri_buf[index++] = (uint8_t)attri_id;
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    
    return serial_cloud_generate_send(CMD_GET, id, attri_buf, index);
}

qm_tlv_ctx_t *qm_serial_snapshot_ctx_get(void)
{
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    if(context == NULL){
        return NULL;
    }
    return context->snapshot_ctx;
}

int32_t qm_serial_snapshot_get(qm_tlv_ctx_t *tlv_ctx)
{
    int ret = 0;
    uint16_t tag = 0;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    serial_context_t *context = NULL;
    context = qm_serial_context_get();

    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");
    QM_RETURN_ON_FALSE(tlv_ctx, SERIAL_PARAM_ERR, LOG_TAG, "tlv_ctx NULL");

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
        qm_tlv_block_tag_get(tlv_block, &tag);
        m_tlv_block = qm_tlv_block_find(context->snapshot_ctx, tag);
        if(m_tlv_block == NULL){
            ret = SERIAL_PARAM_ERR;
            goto __exit;
        }
        qm_tlv_block_copy(tlv_block, m_tlv_block); 
        if(ret != 0){
            qm_tlv_block_delete(m_tlv_block);
            ret =  SERIAL_ALLOC_MEM_ERR;
            goto __exit;
        } 
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

__exit:
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    return ret;
}

int32_t qm_serial_updated_snapshot_get(qm_tlv_ctx_t *tlv_ctx, uint8_t *count, bool_t copy)
{
    uint8_t index = 0;
    int32_t ret = 0;
    int is_update = 0;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    serial_context_t *context = NULL;
    context = qm_serial_context_get();

    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");
    QM_RETURN_ON_FALSE(count, SERIAL_PARAM_ERR, LOG_TAG, "count NULL");
    QM_RETURN_ON_FALSE(tlv_ctx, SERIAL_PARAM_ERR, LOG_TAG, "tlv_ctx NULL");
    QM_RETURN_ON_FALSE(*count == 0, SERIAL_PARAM_ERR, LOG_TAG, "*count 0");

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    tlv_block = qm_tlv_block_next(context->snapshot_ctx, NULL);
    while(tlv_block && (index < *count)){

        qm_tlv_block_is_update(tlv_block, &is_update);
        if(is_update){
            m_tlv_block = qm_tlv_block_creat();
            if(m_tlv_block == NULL){
                ret = SERIAL_ALLOC_MEM_ERR;
                goto __exit;
            }
            if(copy){
                ret = qm_tlv_block_copy(m_tlv_block, tlv_block);
                if(ret != 0){
                    qm_tlv_block_delete(m_tlv_block);
                    ret =  SERIAL_ALLOC_MEM_ERR;
                    goto __exit;
                }
                qm_tlv_block_update(tlv_block, 0);
            }else{
                m_tlv_block->tag = tlv_block->tag;
            }
            qm_tlv_block_add(tlv_ctx, m_tlv_block);
            index ++;
        }
        tlv_block = qm_tlv_block_next(context->snapshot_ctx, tlv_block);
    }

    *count = index;
__exit:
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    return ret;
}

#if CONFIG_QM_SERIAL_EVENT_ONLY_NOTIFY_SUPPORT
int32_t qm_serial_event_get(uint8_t *eid, qm_tlv_ctx_t *tlv_ctx)
{
    int32_t ret = 0;
    uint32_t m_eid = 0;
    void *arg = NULL;
    qm_list_node_t *node = NULL;
    serial_context_t *context = NULL;
    qm_tlv_ctx_t *m_tlv_ctx = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    qm_tlv_block_t *tlv_block = NULL;

    context = qm_serial_context_get();
    if(context == NULL){
        return SERIAL_NO_INIT_ERR;
    }

    if(eid == NULL){
        return SERIAL_PARAM_ERR;
    }
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_lpop(context->event_queue);
    if(!node){
        ret = SERIAL_PARAM_ERR;
        goto __exit;
    }

    m_tlv_ctx = (qm_tlv_ctx_t*)node->val;
    qm_tlv_ctx_arg_get(m_tlv_ctx, &arg);
    m_eid = (uint32_t)arg;
    *eid = (uint8_t)m_eid;

    tlv_block = qm_tlv_block_next(m_tlv_ctx, NULL);
    while(tlv_block){
        m_tlv_block = qm_tlv_block_creat();
        if(m_tlv_block == NULL){
            ret = SERIAL_ALLOC_MEM_ERR;
            goto __exit;
        }
        ret = qm_tlv_block_copy(m_tlv_block, tlv_block);
        if(ret != 0){
            qm_tlv_block_delete(m_tlv_block);
            ret =  SERIAL_ALLOC_MEM_ERR;
            goto __exit;
        }
        qm_tlv_block_add(tlv_ctx, m_tlv_block);
        tlv_block = qm_tlv_block_next(m_tlv_ctx, tlv_block);
    }

__exit:
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    return ret;
}
#endif

int32_t qm_serial_ext_cmd_ack(frame_cmd_t cmd, uint8_t err_code, uint8_t *data, uint16_t len)
{
    uint8_t *buf = NULL;
    int32_t err = SERIAL_NO_ERR;
    uint8_t code = err_code;
    if(data != NULL && len > 0){
        buf = qm_serial_malloc(len + 1);
        if(buf == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }
        memcpy(buf, &code, 1);
        memcpy(buf + 1, data, len);
        err = serial_generate_send(cmd, 0, buf, len + 1);
        qm_serial_free(buf);
    }else{
        err = serial_generate_send(cmd, 0, &code, 1);
    }

    return err;
}
int32_t qm_serial_init(serial_param_t *serial_param)
{
    serial_context_t *serial_context = NULL;

    QM_RETURN_ON_FALSE(serial_param, SERIAL_PARAM_ERR, LOG_TAG, "serial_param NULL");
    QM_RETURN_ON_FALSE(serial_param->pread_buf, SERIAL_PARAM_ERR, LOG_TAG, "pread_buf NULL");

    serial_context = (serial_context_t*)qm_serial_malloc(sizeof(serial_context_t));
    if(NULL == serial_context){
       SERIAL_LOG("not enough memory.");
       return SERIAL_ALLOC_MEM_ERR;
    }

    memset(serial_context, 0, sizeof(serial_context_t));

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    serial_context->serial_mutex = qm_serial_mutex_init();
    serial_context->send_sem = qm_serial_sem_init(0);
#endif

    serial_context->send_queue = qm_list_new();
    serial_context->id_queue = qm_list_new();

#if CONFIG_QM_SERIAL_RESEND_SUPPORT
    serial_context->down_queue = qm_list_new();
#endif

#if CONFIG_QM_SERIAL_EVENT_ONLY_NOTIFY_SUPPORT
    serial_context->event_queue = qm_list_new();
#endif

    serial_context->tlv_ctx = qm_tlv_ctx_creat();

    serial_context->id_queue->match = id_match;

    qm_utils_time_init(&serial_context->next_ping_time);
    serial_context->keep_alive_interval = SERIAL_KEPPALIVE_TIMEOUT_FAST_MS;
    qm_utils_time_countdown_ms(&serial_context->next_ping_time, serial_context->keep_alive_interval);

    qm_utils_time_init(&serial_context->process_get_time);
    serial_context->process_get_interval = SERIAL_PROCESS_TIMEOUT_MS;
    qm_utils_time_countdown_ms(&serial_context->process_get_time, serial_context->process_get_interval);

    qm_utils_time_init(&serial_context->send_interval_time);
    serial_context->send_interval = CONFIG_QM_SERIAL_SEND_INTERVAL_TIMEOUT_MS;
    qm_utils_time_countdown_ms(&serial_context->send_interval_time, serial_context->send_interval);

    qm_utils_time_init(&serial_context->id_check_time);
    serial_context->id_check_interval = CONFIG_QM_SERIAL_ID_CHECK_TIMEOUT_MS;
    qm_utils_time_countdown_ms(&serial_context->id_check_time, serial_context->id_check_interval);

    serial_context->recv_timeout = CONFIG_QM_SERIAL_RECV_TIMEOUT_MS;

    serial_context->callbacks.m_network_configcb = serial_param->callbacks.m_network_configcb;
    serial_context->callbacks.m_reportcb = serial_param->callbacks.m_reportcb;
    serial_context->callbacks.m_responsecb = serial_param->callbacks.m_responsecb;
    serial_context->callbacks.m_device_onlivecb = serial_param->callbacks.m_device_onlivecb;
    serial_context->callbacks.m_prodtst_configcb = serial_param->callbacks.m_prodtst_configcb;
    serial_context->callbacks.m_mcu_status_notifycb = serial_param->callbacks.m_mcu_status_notifycb;
#if CONFIG_QM_SERIAL_OTA_SUPPORT
    serial_context->callbacks.ota_event_notifycb = serial_param->callbacks.ota_event_notifycb;
#endif
#if CONFIG_QM_SERIAL_HEARTBEAT_NOTIFY_SUPPORT
    serial_context->callbacks.m_heartbeatcb = serial_param->callbacks.m_heartbeatcb;
#endif

#if CONFIG_QM_SERIAL_RAW_SUPPORT
    serial_context->callbacks.m_ramcb = serial_context->callbacks.m_ramcb;
#endif
    serial_context->pread_buf = serial_param->pread_buf;
    serial_context->read_buf_size = serial_param->read_buf_size;

    serial_context->parse_state = PARSE_STATE_NONE;
    serial_context->need_len = 1;
    serial_context->state = SERIAL_STATE_HEARTBEAT;
    serial_context->module_status = MODULE_UNCONFIGURD_NETWORK;
    serial_context->module_prodtst_status = MODULE_PRODTST_STATUS_NONE;
    serial_context->prodtst_type = MODULE_PRODTST_TYPE_NONE;
#if CONFIG_QM_SERIAL_LOWPOWER_SUPPORT
    serial_context->idle = 0;
#endif
    serial_context->handle = qm_serial_uart_init();

    g_serial_context = serial_context;
    
    return SERIAL_NO_ERR;
}

static uint16_t get_packet_len(uint8_t*packet_buffer)
{
    QM_RETURN_ON_FALSE(packet_buffer, 0, LOG_TAG, "packet_buffer NULL");

    return  (be16_to_cpu((packet_buffer[5] << 8) + (packet_buffer[4])));
}

static int serial_recv_cycle(serial_context_t *pcontext, qm_utils_time_t *timer)
{
    int rc = SERIAL_NO_ERR;
    int recv_len = 0;
    uint16_t packet_len = 0;
    uint32_t left_t = qm_utils_time_left(timer);
    uint16_t need_len = pcontext->need_len;

    recv_len = qm_serial_recv(pcontext->handle, pcontext->pread_buf+pcontext->read_pos, need_len, left_t);
    pcontext->read_pos += recv_len;

    switch(pcontext->parse_state){

        case PARSE_STATE_NONE:

            if(pcontext->pread_buf[pcontext->read_pos - 1] != FRAME_HEAD_PART){
                goto clear;
            }else{
                pcontext->parse_state = PARSE_STATE_HEAD;
                pcontext->need_len = 1;
            }

        break;

        case PARSE_STATE_HEAD:
            if(pcontext->need_len <= recv_len){
                if(pcontext->pread_buf[pcontext->read_pos - 1] != FRAME_HEAD_PART){
                    goto clear;
                }else{
                    pcontext->parse_state = PARSE_STATE_VER;
                    pcontext->need_len = 1;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;
        case PARSE_STATE_VER:
            if(pcontext->need_len <= recv_len){
                if(pcontext->pread_buf[pcontext->read_pos - 1] != PROTOCOL_VER){
                    goto clear;
                }else{
                    pcontext->parse_state = PARSE_STATE_LEN;
                    pcontext->need_len = 3;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;
        case PARSE_STATE_LEN:

            if(pcontext->need_len <= recv_len){
                packet_len = get_packet_len(pcontext->pread_buf);
                if(packet_len > (pcontext->read_buf_size - pcontext->read_pos)){
                    SERIAL_LOG("serial recv buffer over flow");
                    goto clear;
                }else{
                    pcontext->parse_state = PARSE_STATE_DATA;
                    pcontext->need_len = packet_len + 1;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;

        case PARSE_STATE_DATA:

            if(pcontext->need_len <= recv_len){
                SERIAL_LOG("serial recv len %d", pcontext->read_pos);
                SERIAL_HEX_LOG("serial recv data", pcontext->pread_buf, pcontext->read_pos);
                rc = frame_parse(pcontext->pread_buf, pcontext->read_pos);
                goto clear;
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;

        default:
            break;
    }

    return rc;

clear:
    pcontext->parse_state = PARSE_STATE_NONE;
    pcontext->read_pos = 0;
    pcontext->need_len = 1;
    return rc;
}

int32_t qm_serial_send_loop(void)
{
    frame_info_t *info = NULL;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    uint32_t left = 0;
#endif
    uint32_t send_queue_len = 0;
    qm_list_node_t *node = NULL;
    serial_context_t *context = NULL;
    uint8_t local_id = 0;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_sem_wait(context->send_sem, 0);
#endif

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    send_queue_len = context->send_queue->len;
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

    if(send_queue_len == 0)
        goto exit;

    if (!qm_utils_time_is_expired(&context->send_interval_time)) {
         goto exit;
    }
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_lpop(context->send_queue);

    send_queue_len = context->send_queue->len;

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif
    if(node){
        info = (frame_info_t*)node;
        qm_serial_send(context->handle, info->data, info->len);
        SERIAL_HEX_LOG("serial send", info->data, info->len);
        if(info->resend){
            local_id = local_id_get(info->data);
            serial_id_uptime(local_id, (uint8_t*)info);
        }else{
            qm_serial_free(info);
        }
    }

    qm_utils_time_countdown_ms(&context->send_interval_time, context->send_interval);

exit:
//todo[haunglei]: 防止溢出导致发送间隔无穷大
    if(qm_utils_time_left(&context->send_interval_time) > context->send_interval) {
        qm_utils_time_countdown_ms(&context->send_interval_time, context->send_interval);
    }
//

#if CONFIG_QM_SERIAL_RESEND_SUPPORT
    down_queue_send();
#endif

#if CONFIG_QM_SERIAL_RTOS_SUPPORT

    if(send_queue_len > 0){
        qm_serial_sem_signal(context->send_sem);
        left = qm_utils_time_left(&context->send_interval_time);
        if (left > 50){
            qm_serial_msleep(left);
        }
        else{
            qm_serial_msleep(50);
        }
    }

#endif

    return SERIAL_NO_ERR;
}

static int32_t serial_keepalive(serial_context_t *pcontext)
{

    /* if there is no ping_timer timeout, then return success */
    if (!qm_utils_time_is_expired(&pcontext->next_ping_time)) {
    //todo[haunglei]: 防止溢出导致发送间隔无穷大
        if(qm_utils_time_left(&pcontext->next_ping_time) > pcontext->keep_alive_interval) {
            qm_utils_time_countdown_ms(&pcontext->next_ping_time, pcontext->keep_alive_interval);
        }
    //
        return SERIAL_NO_ERR;
    }

    /* update to next time sending usart keep-alive */
    qm_utils_time_countdown_ms(&pcontext->next_ping_time, pcontext->keep_alive_interval);
    SERIAL_LOG("send uart ping...");

    /*here send usart heart packet*/
    serial_heartbeat_send();

    return SERIAL_NO_ERR;
}

static int32_t serial_process_cycle(serial_context_t *pcontext)
{
    if(pcontext->state != SERIAL_STATE_GETDEVICEINFO &&
        pcontext->state != SERIAL_STATE_GETDEVICESNAPSHOT){
        return SERIAL_NO_ERR;
    }

    if (!qm_utils_time_is_expired(&pcontext->process_get_time)) {
    //todo[haunglei]: 防止溢出导致发送间隔无穷大
        if(qm_utils_time_left(&pcontext->process_get_time) > pcontext->process_get_interval) {
            qm_utils_time_countdown_ms(&pcontext->process_get_time, pcontext->process_get_interval);
        }
    //
        return SERIAL_NO_ERR;
    }
    qm_utils_time_countdown_ms(&pcontext->process_get_time, pcontext->process_get_interval);
    if(pcontext->state == SERIAL_STATE_GETDEVICEINFO){
        serial_deviceinfo_get_send();
    }else if(pcontext->state == SERIAL_STATE_GETDEVICESNAPSHOT){
        serial_snapshot_get_send();
    }

    return SERIAL_NO_ERR;
}

int32_t qm_serial_recv_loop(void)
{
    int32_t rc = SERIAL_NO_ERR;
    qm_utils_time_t yield_time = {0};
    serial_context_t *context = NULL;
    qm_utils_time_init(&yield_time);

    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

    qm_utils_time_countdown_ms(&yield_time, context->recv_timeout);
    /* Keep usart alive */
#if !CONFIG_QM_SERIAL_DISABLE_HEARTBEAT
    serial_keepalive(context);
#endif

    serial_process_cycle(context);

    id_check_cycle(context);

    /* acquire package in cycle */
    rc = serial_recv_cycle(context, &yield_time);
    if (SERIAL_NO_ERR == rc) {
        /* check list of wait serial ACK to remove node that is ACKED or timeout */

    }
#if CONFIG_QM_SERIAL_OTA_SUPPORT
    qm_serial_ota_process();
#endif
#if CONFIG_SERIAL_MCU_OTA_SUPPORT
    serial_mcu_ota_process();
#endif

    return SERIAL_NO_ERR;
}

#if CONFIG_QM_SERIAL_RAW_SUPPORT
int32_t qm_serial_raw_send(uint8_t *data, uint16_t len)
{
    uint16_t data_len = len;
    uint16_t sum_len = 0;
    uint8_t *buf = NULL;
    qm_list_node_t *node = NULL;
    serial_context_t *context = NULL;
    if(data == NULL || len == 0){
        return SERIAL_PARAM_ERR;
    }
    context = qm_serial_context_get();
    if(context == NULL){
        return SERIAL_NO_INIT_ERR;
    }
    sum_len = data_len + sizeof(qm_list_node_t) + 2;
    buf = qm_serial_malloc(sum_len);
    if(buf == NULL){
        return SERIAL_ALLOC_MEM_ERR;
    }
    node = (qm_list_node_t*)buf;
    qm_list_node_init(node, buf + sizeof(qm_list_node_t));
    memcpy(buf + sizeof(qm_list_node_t), &data_len, 2);
    memcpy(buf + sizeof(qm_list_node_t) + 2, data, len);

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_lock(context->serial_mutex);
#endif
    qm_list_rpush(context->send_queue, node);
#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_mutex_unlock(context->serial_mutex);
#endif

#if CONFIG_QM_SERIAL_RTOS_SUPPORT
    qm_serial_sem_signal(context->send_sem);
#endif

    return SERIAL_NO_ERR;
}
#endif


int32_t qm_serial_recv_timeout_set(uint32_t timeout_ms)
{
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    if(context == NULL){
        return SERIAL_NO_INIT_ERR;
    }

    context->recv_timeout = timeout_ms;
    return SERIAL_NO_ERR;
}


int32_t qm_serial_recv_timeout_default(void)
{
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");
    
    context->recv_timeout = CONFIG_QM_SERIAL_RECV_TIMEOUT_MS;
    return SERIAL_NO_ERR;
}


#if CONFIG_QM_SERIAL_LOWPOWER_SUPPORT
int32_t qm_serial_process_reset(void)
{
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

    context->state = SERIAL_STATE_HEARTBEAT;
    context->keep_alive_interval = SERIAL_KEPPALIVE_TIMEOUT_FAST_MS;
    qm_utils_time_init(&context->next_ping_time);
    return SERIAL_NO_ERR;
}
#endif

serial_state_t serial_state_get(void)
{
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

    return context->state;   
}

static int32_t serial_send_by_attrid(frame_cmd_t cmd, uint8_t *attr_id, uint16_t num, int response, uint8_t packet_id)
{
    #define SERIAL_TLV_BUF_LEN   16
    uint8_t sta_buf[SERIAL_TLV_BUF_LEN] = {0};
    uint8_t *alloc_buf = NULL;
    uint8_t *tlv_buf = NULL;
    int tlv_len = 0;
    int32_t ret = 0;
    serial_context_t *context = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    uint16_t index = 0;

    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

    while(index < num){
        
        tlv_block = qm_tlv_block_find(context->tlv_ctx, (uint16_t)attr_id[index++]);
        if(tlv_block == NULL){
            ret = SERIAL_PARAM_ERR;
            goto __exit;
        }
        qm_tlv_block_serialize(tlv_block);
    }

    qm_tlv_serialize(context->tlv_ctx, NULL, &tlv_len, QM_BLOCK_TYPE_DTTV,QM_TLV_SERIALIZE_TYPE_PARTIAL);

    if(tlv_len <= SERIAL_TLV_BUF_LEN){
        tlv_buf = sta_buf;
    }else{
        alloc_buf = (uint8_t*)qm_serial_malloc(tlv_len);
        if(alloc_buf == NULL){
            ret = SERIAL_ALLOC_MEM_ERR;
            goto __exit;
        }
        tlv_buf = alloc_buf;
    }
    
    qm_tlv_serialize(context->tlv_ctx, tlv_buf, &tlv_len, QM_BLOCK_TYPE_DTTV, QM_TLV_SERIALIZE_TYPE_PARTIAL);

__exit:

    if(response){
        if(ret == 0){
            ret = serial_generate_send_with_code(cmd, packet_id, SERIAL_COM_OK, tlv_buf, tlv_len);
        }else{
            ret = serial_generate_send_with_code(cmd, packet_id, SERIAL_COM_ERROR, NULL, 0);
        }
    }else{
        if(ret == 0){
            ret = serial_generate_send(cmd, 0, tlv_buf, tlv_len);
        }
    }

    if(alloc_buf){
        qm_serial_free(alloc_buf);
        alloc_buf = NULL;
    }

    return ret;
}

static int32_t serial_getmode_handler(uint8_t *attr_id, uint16_t num)
{
    int32_t ret = 0;
    uint16_t index = 0;
    void *arg = NULL;
    serial_context_t *context = qm_serial_context_get();
    qm_tlv_block_t *tlv_block = NULL;
    serial_mode_callbacks_t *mode_cbs = NULL;

    while(index < num){
   
        tlv_block = qm_tlv_block_find(context->tlv_ctx, (uint16_t)attr_id[index]);
        if(tlv_block == NULL){
            return SERIAL_PARAM_ERR;
        }
        qm_tlv_block_arg_get(tlv_block, &arg);
        mode_cbs = (serial_mode_callbacks_t*)arg;
        if(mode_cbs == NULL){
            return SERIAL_PARAM_ERR;
        }

        if(mode_cbs->m_getmode){
            ret = mode_cbs->m_getmode((void*)tlv_block);
            if(ret != 0){
                return ret;
            }
        }else{
            return SERIAL_PARAM_ERR;
        }
        index ++;
    }

    serial_send_by_attrid(CMD_GETMOD, attr_id, num, 1, 0);

    return SERIAL_NO_ERR;
}

static int32_t serial_setmode_handler(uint8_t *data, uint16_t len)
{
    int32_t ret = 0;
    uint16_t tag = 0;
    void *arg = NULL;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    serial_context_t *context = qm_serial_context_get();
    serial_mode_callbacks_t *mode_cbs = NULL;

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return SERIAL_ALLOC_MEM_ERR;
    }

    ret = qm_tlv_deserialize(tlv_ctx, data, len, QM_BLOCK_TYPE_DTTV);
    if(ret != 0){
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_PARAM_ERR;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
        qm_tlv_block_tag_get(tlv_block, &tag);
        m_tlv_block = qm_tlv_block_find(context->tlv_ctx, tag);
        if(m_tlv_block == NULL){
            ret = SERIAL_PARAM_ERR;
            goto __exit;
        }
        qm_tlv_block_arg_get(m_tlv_block, &arg);
        mode_cbs = (serial_mode_callbacks_t*)arg;
        if(mode_cbs == NULL){
            ret = SERIAL_PARAM_ERR;
            goto __exit;
        }
        if(mode_cbs->m_setmode){
            ret = mode_cbs->m_setmode(tlv_block);
            if(ret != 0){
                goto __exit;
            }
        }else{
            ret = SERIAL_PARAM_ERR;
            goto __exit;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }
    
__exit:
    qm_tlv_ctx_delete(tlv_ctx);
    return ret;
}

static int32_t serial_service_handler(uint8_t *data, uint16_t len)
{
    int32_t ret = 0;
    uint16_t tag = 0;
    void *arg = NULL;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    serial_context_t *context = qm_serial_context_get();
    serial_service_callbacks_t *service_cbs = NULL;

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return SERIAL_ALLOC_MEM_ERR;
    }

    ret = qm_tlv_deserialize(tlv_ctx, data, len, QM_BLOCK_TYPE_DTTV);
    if(ret != 0){
        qm_tlv_ctx_delete(tlv_ctx);
        return SERIAL_PARAM_ERR;
    }
    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
   
        qm_tlv_block_tag_get(tlv_block, &tag);
        m_tlv_block = qm_tlv_block_find(context->tlv_ctx, tag);
        if(m_tlv_block == NULL){
            ret = SERIAL_PARAM_ERR;
            goto __exit;
        }
        qm_tlv_block_arg_get(m_tlv_block, &arg);
        service_cbs = (serial_service_callbacks_t*)arg;
        if(service_cbs == NULL){
            ret = SERIAL_PARAM_ERR;
            goto __exit;
        }

        if(service_cbs->inner_fn){
            ret = service_cbs->inner_fn(tlv_block);
            if(ret != 0){
                goto __exit;
            }
        }
        if(service_cbs->extern_fn){
            ret = service_cbs->extern_fn(tlv_block);
            if(ret != 0){
                goto __exit;
            }
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

__exit:
    qm_tlv_ctx_delete(tlv_ctx);
    return ret;
}

static int32_t serial_service_register(module_service_type_t service_type, serial_service_fn fn, int32_t is_inner)
{
    serial_context_t *context = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    serial_service_callbacks_t *service_callbacks = NULL;
    
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");

    tlv_block = qm_tlv_block_find(context->tlv_ctx, (uint16_t)service_type);
    if(tlv_block){
#if CONFIG_QM_TTLV_ARG
        service_callbacks = (serial_service_callbacks_t*)tlv_block->arg;
#endif
    }else{
        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }
        service_callbacks = (serial_service_callbacks_t*)qm_serial_malloc(sizeof(serial_mode_callbacks_t));
        if(service_callbacks == NULL){
            qm_tlv_block_delete(tlv_block);
            return SERIAL_ALLOC_MEM_ERR;
        }
        memset(service_callbacks, 0, sizeof(serial_service_callbacks_t));
        qm_tlv_block_tag_set(tlv_block, (uint16_t)service_type);
        qm_tlv_block_arg_set(tlv_block, (void*)service_callbacks);
        qm_tlv_block_add(context->tlv_ctx, tlv_block);
    }

    if(is_inner){
        service_callbacks->inner_fn = fn;
    }else{
        service_callbacks->extern_fn = fn;
    }
    return SERIAL_NO_ERR;
}

int32_t qm_serial_service_register(module_service_type_t service_type, serial_service_fn fn)
{
    return serial_service_register(service_type, fn, 0);
}

int32_t qm_serial_mode_register(uint8_t attr_id, serial_setmode_fn setmode_fn, serial_getmode_fn getmode_fn)
{
    qm_tlv_block_t *tlv_block = NULL;
    serial_mode_callbacks_t *mode_callbacks = NULL;
    serial_context_t *context = NULL;

    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");


    tlv_block = qm_tlv_block_find(context->tlv_ctx, (uint16_t)attr_id);
    if(tlv_block){
#if CONFIG_QM_TTLV_ARG
        mode_callbacks = (serial_mode_callbacks_t*)tlv_block->arg;
#endif
    }else{
        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            return SERIAL_ALLOC_MEM_ERR;
        }
        mode_callbacks = (serial_mode_callbacks_t*)qm_serial_malloc(sizeof(serial_mode_callbacks_t));
        if(mode_callbacks == NULL){
            qm_tlv_block_delete(tlv_block);
            return SERIAL_ALLOC_MEM_ERR;
        }
        memset(mode_callbacks, 0, sizeof(serial_mode_callbacks_t));

        qm_tlv_block_tag_set(tlv_block, (uint16_t)attr_id);
        qm_tlv_block_arg_set(tlv_block, (void*)mode_callbacks);
        qm_tlv_block_add(context->tlv_ctx, tlv_block);
    }

    mode_callbacks->m_getmode = getmode_fn;
    mode_callbacks->m_setmode = setmode_fn;
    
    return SERIAL_NO_ERR;
}

#if CONFIG_QM_SERIAL_LOWPOWER_SUPPORT

int32_t qm_serial_idle_get(uint8_t *idle)
{
    uint32_t queue_len = 0;
    serial_context_t *context = NULL;
    context = qm_serial_context_get();
    QM_RETURN_ON_FALSE(context, SERIAL_NO_INIT_ERR, LOG_TAG, "context NULL");
    QM_RETURN_ON_FALSE(idle, SERIAL_PARAM_ERR, LOG_TAG, "idle NULL");

    queue_len += queue_len_get(context->send_queue);
#if CONFIG_QM_SERIAL_RESEND_SUPPORT
    queue_len += queue_len_get(context->down_queue);
#endif

    if(queue_len > 0){
        *idle = 1;
    }else{
        *idle = 0;
    }
    return SERIAL_NO_ERR;
}

#endif
