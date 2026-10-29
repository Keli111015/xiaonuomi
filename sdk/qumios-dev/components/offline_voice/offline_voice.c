#include "qm.h"
#include "qm_log.h"
#include "qm_tlv.h"

#include "offline_voice.h"
#include "offline_voice_hal.h"
#include "offline_voice_common.h"

#include "serial_protocol.h"

#if CONFIG_LOG_ON
#define LOG_TAG "offline_voice" 
#endif

typedef enum{
    OFFLINE_VOICE_PARSE_STATE_NONE = 0,
    OFFLINE_VOICE_PARSE_STATE_HEAD,
    OFFLINE_VOICE_PARSE_STATE_VER,
    OFFLINE_VOICE_PARSE_STATE_LEN,
    OFFLINE_VOICE_PARSE_STATE_DATA,
}offline_voice_parse_state_t;

#pragma pack(1)
typedef struct
{
    uint16_t head;
    uint8_t  ver;
    uint8_t  cmd;
    uint16_t len;
}offline_voice_frame_header_t;

typedef struct
{
    offline_voice_frame_header_t head;
    uint8_t  frame_id;
    uint8_t  error_code;
    uint8_t  chksum;
}offline_voice_frame_set_t;

typedef struct
{
    offline_voice_frame_header_t head;
    uint8_t  data;
    uint8_t  chksum;
}offline_voice_frame_generic_t;

typedef struct
{
    offline_voice_frame_header_t head;
    uint8_t  data[4];
    uint8_t  chksum;
}offline_voice_factory_generic_t;

#pragma pack()

typedef struct
{
    offline_voice_frame_cmd_t cmd;
    int32_t (*unpack)(uint8_t *data, uint16_t len);
}offline_voice_cmd_table_t;

typedef struct 
{
    void *handle;
    qm_sem_t   voice_sem;
    qm_mutex_t voice_mutex;
    uint8_t *pread_buf;
    uint16_t need_len;
    uint16_t read_pos;
    uint16_t read_buf_size;  /* Specify size of read-buffer in byte */
    offline_voice_parse_state_t parse_state;
    offline_voice_frame_cmd_t cmd;
    offline_voicel_set_fn set_cb;
    offline_voice_get_mode_fn get_cb;
    offline_voice_factory_fn factory_cb;
    uint8_t packet_id;
    uint8_t    buff;
    uint8_t    count;
    uint16_t    wait_time;
    uint8_t    voice_recv_run;
    qm_utils_time_t recv_wait_timer;
    offline_voice_result_t error_code;
}offline_voice_context_t;

static uint8_t read_buf[CONFIG_OFFLINE_VOICE_BUFF_NUM]  = {0};
static offline_voice_context_t  g_offline_voice_context = {0};

static offline_voice_context_t *offline_voice_context_get(void);
static uint16_t get_packet_len(uint8_t*packet_buffer);
static uint8_t frame_chksum(uint8_t *buffer, uint16_t length);
static int32_t offline_voice_set_unpack(uint8_t *data, uint16_t len);
static int32_t offline_voice_getmode_unpack(uint8_t *data, uint16_t len);
static int32_t offline_voice_enterfactory_unpack(uint8_t *data, uint16_t len);
static int32_t offline_voice_send_cycle(offline_voice_context_t *pcontext);
static int32_t offline_voice_recv_cycle(offline_voice_context_t *pcontext, qm_utils_time_t *timer);
static int32_t offline_voice_frame_pack(offline_voice_frame_cmd_t cmd, uint8_t *buff, uint16_t *len, offline_voice_context_t *context);
static int32_t offline_voice_factory_mode(void);

static offline_voice_cmd_table_t g_offline_voice_cmd_table[] = {
    { OFFLINE_VOICE_CMD_SET,               offline_voice_set_unpack,            },
    { OFFLINE_VOICE_CMD_GET_MODE,          offline_voice_getmode_unpack,        },
    { OFFLINE_VOICE_CMD_ENTER_FACTORY,     offline_voice_enterfactory_unpack,   },
};

static offline_voice_context_t *offline_voice_context_get(void)
{
    return &g_offline_voice_context;
}

static uint8_t frame_chksum(uint8_t *buffer, uint16_t length)
{
    if(buffer == NULL || length == 0){
        return 0;
    }

    uint16_t i;
    uint8_t check_sum = 0;
    for (i = 0; i < length; i++){
        check_sum += buffer[i];
    }
    return check_sum;
}

static uint16_t get_packet_len(uint8_t*packet_buffer)
{
    if(packet_buffer == NULL){
        return 0;
    }

    return  (be16_to_cpu((packet_buffer[5] << 8) + (packet_buffer[4])));
}

static int32_t frame_parse(uint8_t *data, uint16_t len)
{
    int index = 0;
    offline_voice_cmd_table_t *cmd_handler = NULL;
    int32_t ret = QM_EOK;
    frame_header_t *header = NULL;
    if(*(data + len - 1) != frame_chksum(data, len - 1)){
        return -1;
    }
    header = (frame_header_t*)data;

    for(index = 0; index < QM_ARRAY_SIZE(g_offline_voice_cmd_table); index++){
        cmd_handler = &g_offline_voice_cmd_table[index];
        if(cmd_handler->cmd != header->cmd || !cmd_handler->unpack){
            continue;
        }
        ret = cmd_handler->unpack(data + OFFLINE_VOICE_FRAME_HEAD_LEN, len - OFFLINE_VOICE_FRAME_HEAD_LEN -1);
    }

    return ret;
}

static int32_t offline_voice_set_unpack(uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }    

    int ret = QM_EOK;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    offline_voice_context_t *context = NULL;
    context = offline_voice_context_get();
    if(context == NULL){
        return -QM_EINVAL;
    }

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return -QM_ENOMEM;
    }

    ret = qm_tlv_deserialize(tlv_ctx, data + 1, len - 1, QM_BLOCK_TYPE_DTTV);
    if(ret != QM_EOK){
        qm_tlv_ctx_delete(tlv_ctx);
        return -QM_EINVAL;  
    }

    if(context->set_cb){
        ret = context->set_cb(*data, data + 1, len - 1, tlv_ctx);
    }

    qm_mutex_lock(&context->voice_mutex, QM_WAIT_FOREVER);
    context->packet_id = *data;
    context->voice_recv_run = QM_TRUE;
    qm_utils_time_countdown_ms(&context->recv_wait_timer, CONFIG_OFFLINE_VOICE_RECV_WAIT_TIMEOUT_MS);
    qm_mutex_unlock(&context->voice_mutex);
    
    qm_tlv_ctx_delete(tlv_ctx);
    return ret;
}

static int32_t offline_voice_getmode_unpack(uint8_t *data, uint16_t len) // now only support get fwname
{
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }    

    int ret = QM_EOK;
    uint16_t index = 0;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    offline_voice_context_t *context = NULL;
    uint8_t error_code = OFFLINE_VOICE_RESULT_OK;
    context = offline_voice_context_get();
    if(context == NULL || context->get_cb == NULL){
        return -QM_EINVAL;
    }

    error_code = *data;
    if(error_code != OFFLINE_VOICE_RESULT_OK){
        context->get_cb(error_code, NULL);
    }

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        error_code = OFFLINE_VOICE_RESULT_FAIL;
        goto _exit;
    }

    ret = qm_tlv_deserialize(tlv_ctx, data + 1, len - 1, QM_BLOCK_TYPE_DTTV);
    if(ret != QM_EOK){
        error_code = OFFLINE_VOICE_RESULT_FAIL;
        goto _exit;
    }

    tlv_block = qm_tlv_block_find(tlv_ctx, OFFLINE_VOICE_GET_MODE_FWNAME);
    if(tlv_block == NULL){
        error_code = OFFLINE_VOICE_RESULT_FAIL;
        goto _exit;
    }

_exit:
    ret = context->get_cb(error_code, tlv_block);
    qm_tlv_ctx_delete(tlv_ctx);
    return ret; 
}

static int32_t offline_voice_enterfactory_unpack(uint8_t *data, uint16_t len)
{
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }    
    
    int ret = QM_EOK;
    offline_voice_context_t *context = NULL;
    uint8_t error_code = OFFLINE_VOICE_RESULT_OK;
    context = offline_voice_context_get();
    if(context == NULL || context->factory_cb == NULL){
        return -QM_EINVAL;
    }

    error_code = *data;
    ret = context->factory_cb(context->handle, error_code);
    return ret; 
}
static void frame_head_init(uint8_t *buffer, offline_voice_frame_cmd_t cmd, uint16_t len)
{
    offline_voice_frame_header_t *head = (offline_voice_frame_header_t *)buffer;

    head->head = cpu_to_be16(OFFLINE_VOICE_FRAME_HEAD);
    head->ver  = OFFLINE_VOICE_PROTOCOL_VER;
    head->cmd  = cmd;
    head->len = cpu_to_be16(len);
}

static int32_t offline_voice_frame_pack(offline_voice_frame_cmd_t cmd, uint8_t *buff, uint16_t *len, offline_voice_context_t *context)
{
    if(context == NULL || buff == NULL|| len == NULL){
        return -QM_EINVAL;
    }

    switch (cmd)
    {
        case OFFLINE_VOICE_CMD_SET:{
            *len = sizeof(offline_voice_frame_set_t);
            offline_voice_frame_set_t *info = (offline_voice_frame_set_t *)buff;
            frame_head_init(buff, cmd, 2);
            info->frame_id = context->packet_id;
            info->error_code = context->error_code;
            info->chksum = frame_chksum(buff, *len - 1);
        }break;
        case OFFLINE_VOICE_CMD_GET_MODE:{
            *len = sizeof(offline_voice_frame_generic_t);
            offline_voice_frame_generic_t *info = (offline_voice_frame_generic_t *)buff;
            frame_head_init(buff, cmd, 1);
            info->data = context->buff;
            info->chksum = frame_chksum(buff, *len - 1);
        }break;
        case OFFLINE_VOICE_CMD_ENTER_FACTORY:{
            *len = sizeof(offline_voice_factory_generic_t);
            offline_voice_factory_generic_t *info = (offline_voice_factory_generic_t *)buff;
            frame_head_init(buff, cmd, 4);
            info->data[0] = context->buff;
            info->data[1] = context->count;
            info->data[2] = (uint8_t)(context->wait_time >> 8);
            info->data[3] = (uint8_t)(context->wait_time);
            info->chksum = frame_chksum(buff, *len - 1);
        }break;
        default:
            break;
    }

    return QM_EOK;
}

static int32_t offline_voice_send_cycle(offline_voice_context_t *pcontext)
{
    int ret = QM_EOK;
    uint16_t len = 0;
    uint8_t buffer[OFFLINE_VOICE_FRAME_LEN] = {0};
    offline_voice_frame_cmd_t cmd = OFFLINE_VOICE_CMD_ERROR;
    if( pcontext == NULL || !pcontext->voice_recv_run || 
        !qm_utils_time_is_expired(&pcontext->recv_wait_timer)){
        return -QM_EINVAL;
    }

    ret = qm_sem_wait(&pcontext->voice_sem, CONFIG_OFFLINE_VOICE_SEND_WAIT_TIMEOUT_MS);
    if(ret != QM_EOK){
        pcontext->error_code = OFFLINE_VOICE_RESULT_TIMEOUT;
    }
    qm_mutex_lock(&pcontext->voice_mutex, QM_WAIT_FOREVER);
    cmd = pcontext->cmd;
    pcontext->voice_recv_run = QM_FALSE;
    offline_voice_frame_pack(cmd, buffer, &len, pcontext);
    qm_mutex_unlock(&pcontext->voice_mutex);   

    return offline_voice_channel_send(pcontext->handle, buffer, len);
}

static int32_t offline_voice_recv_cycle(offline_voice_context_t *pcontext, qm_utils_time_t *timer)
{
    if(pcontext == NULL || timer == NULL){
        return -QM_EINVAL;
    }

    int rc = -QM_EINVAL;
    int recv_len = 0;
    uint16_t packet_len = 0;
    uint32_t left_t = qm_utils_time_left(timer);
    uint16_t need_len = pcontext->need_len;

    recv_len = offline_voice_channel_recv(pcontext->handle, pcontext->pread_buf+pcontext->read_pos, need_len, left_t);
    pcontext->read_pos += recv_len;

    switch(pcontext->parse_state){
        case OFFLINE_VOICE_PARSE_STATE_NONE:

            if(pcontext->pread_buf[pcontext->read_pos - 1] != OFFLINE_VOICE_FRAME_HEAD_PART){
                goto clear;
            }else{
                pcontext->parse_state = OFFLINE_VOICE_PARSE_STATE_HEAD;
                pcontext->need_len = 1;
            }

        break;
        case OFFLINE_VOICE_PARSE_STATE_HEAD:
            if(pcontext->need_len <= recv_len){
                if(pcontext->pread_buf[pcontext->read_pos - 1] != OFFLINE_VOICE_FRAME_HEAD_PART){
                    goto clear;
                }else{
                    pcontext->parse_state = OFFLINE_VOICE_PARSE_STATE_VER;
                    pcontext->need_len = 1;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }
        break;
        case OFFLINE_VOICE_PARSE_STATE_VER:
            if(pcontext->need_len <= recv_len){
                if(pcontext->pread_buf[pcontext->read_pos - 1] != OFFLINE_VOICE_PROTOCOL_VER){
                    goto clear;
                }else{
                    pcontext->parse_state = OFFLINE_VOICE_PARSE_STATE_LEN;
                    pcontext->need_len = 3;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }
        break;
        case OFFLINE_VOICE_PARSE_STATE_LEN:
            if(pcontext->need_len <= recv_len){
                packet_len = get_packet_len(pcontext->pread_buf);
                if(packet_len > (pcontext->read_buf_size - pcontext->read_pos)){
                #if CONFIG_LOG_ON
                    QM_LOGD(LOG_TAG, "serial recv buffer over flow");
                #endif
                    goto clear;
                }else{
                    pcontext->parse_state = OFFLINE_VOICE_PARSE_STATE_DATA;
                    pcontext->need_len = packet_len + 1;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }
        break;
        case OFFLINE_VOICE_PARSE_STATE_DATA:
            if(pcontext->need_len <= recv_len){
            #if CONFIG_LOG_ON
                QM_LOGD(LOG_TAG, "offline_voice recv len %d", pcontext->read_pos);
                QM_HEX_LOGD(LOG_TAG, "offline_voice recv data", pcontext->pread_buf, pcontext->read_pos);
            #endif
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
    pcontext->parse_state = OFFLINE_VOICE_PARSE_STATE_NONE;
    pcontext->read_pos = 0;
    pcontext->need_len = 1;
    return rc;
}

int32_t offline_voice_loop(void)
{
    qm_utils_time_t yield_time = {0};
    offline_voice_context_t *context = NULL;
    qm_utils_time_init(&yield_time);
    context = offline_voice_context_get();
    if(context == NULL){
        return -QM_EINVAL;
    }

    qm_utils_time_countdown_ms(&yield_time, CONFIG_OFFLINE_VOICE_RECV_WAIT_TIMEOUT_MS);
    offline_voice_recv_cycle(context, &yield_time);
    offline_voice_send_cycle(context);
    return QM_EOK;
}

int32_t offline_voice_init(offline_voicel_set_fn *set_cb)
{

    offline_voice_context_t *offline_voice_context = NULL;
    offline_voice_context = offline_voice_context_get();
    if(offline_voice_context == NULL){
        return -QM_EINVAL;
    }

    offline_voice_context->need_len = 1;
    offline_voice_context->pread_buf = read_buf;
    offline_voice_context->read_buf_size = CONFIG_OFFLINE_VOICE_BUFF_NUM;
    offline_voice_context->parse_state = OFFLINE_VOICE_PARSE_STATE_NONE;

    offline_voice_context->set_cb = set_cb;
    offline_voice_context->voice_recv_run = QM_FALSE;
#if CONFIG_OFFLINE_VOICE_RTOS_SUPPORT
    qm_sem_new(&offline_voice_context->voice_sem, 0);
    qm_mutex_new(&offline_voice_context->voice_mutex);
#endif  
    qm_utils_time_init(&offline_voice_context->recv_wait_timer);
#if CONFIG_OFFLINE_VOICE_UART_SUPPORT
    offline_voice_context->handle   = offline_voice_uart_init();
    if(offline_voice_context->handle == NULL){
        goto _error;
    }
#endif
    return  QM_EOK;

_error:
    if(offline_voice_context->handle){
#if CONFIG_OFFLINE_VOICE_UART_SUPPORT
        offline_voice_uart_deinit(offline_voice_context->handle );
#endif
        offline_voice_context->handle = NULL;
    }

    if(offline_voice_context){
#if CONFIG_OFFLINE_VOICE_UART_SUPPORT
        qm_free(offline_voice_context);
#endif
        offline_voice_context = NULL;
    }
    return  -QM_ERROR;
}

int32_t offline_voice_result_report(offline_voice_result_t result)
{
    offline_voice_context_t *context = NULL;
    context = offline_voice_context_get();
    if(context == NULL || !context->voice_recv_run){
        return -QM_EINVAL;
    }
    
    qm_mutex_lock(&context->voice_mutex, QM_WAIT_FOREVER);
    context->error_code = result;
    context->cmd = OFFLINE_VOICE_CMD_SET;
    qm_mutex_unlock(&context->voice_mutex);
    qm_sem_signal(&context->voice_sem);
    return QM_EOK;
}

int32_t offline_voice_get_fwname(offline_voice_get_mode_fn get_cb)
{
    offline_voice_context_t *context = NULL;
    context = offline_voice_context_get();
    if(context == NULL || get_cb == NULL){
        return -QM_EINVAL;
    }      
    qm_mutex_lock(&context->voice_mutex, QM_WAIT_FOREVER);
    context->buff = 0xC8;
    context->get_cb = get_cb;
    context->voice_recv_run = QM_TRUE;
    context->cmd = OFFLINE_VOICE_CMD_GET_MODE;
    qm_mutex_unlock(&context->voice_mutex);
    qm_sem_signal(&context->voice_sem);
    return QM_EOK;
}

int32_t offline_voice_enter_factory(offline_voice_factory_t factory_mode, offline_voice_factory_fn result)
{
    offline_voice_context_t *context = NULL;
    context = offline_voice_context_get();
    if(context == NULL || result == NULL){
        return -QM_EINVAL;
    }      
    qm_mutex_lock(&context->voice_mutex, QM_WAIT_FOREVER);
    context->factory_cb = result;
    context->voice_recv_run = QM_TRUE;
    context->buff = factory_mode.mode;
    context->count = factory_mode.count;
    context->wait_time = factory_mode.wait_time;
    context->cmd = OFFLINE_VOICE_CMD_ENTER_FACTORY;
    qm_mutex_unlock(&context->voice_mutex);
    qm_sem_signal(&context->voice_sem);
    return QM_EOK;
}