#include "qm_config.h"

#if CONFIG_OPPLE_SERIAL_SUPPORT

#include "opple_serial_protocol.h"
#include "opple_serial_platform.h"
#include "opple_serial_common.h"
#include "qm_utils_string.h"
#include "qm_utils_timer.h"
#include "qm_utils_list.h"
#include "qm_tlv.h"

#define OPPLE_KV_VALUE_LEN  16
#define OPPLE_SERIAL_KEY_FORMAT  "opple%d"

static uint8_t frame_chksum(uint8_t *buffer, uint16_t length);
static void frame_head_init(uint8_t *buffer, opple_frame_cmd_t cmd, uint8_t seq, uint8_t len);
static int32_t frame_creat(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t data_len, uint8_t *buf, uint16_t *buf_len);
static int32_t serial_generate_send(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t *data, uint16_t len);
static int32_t serial_generate_send_with_code(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t len);
static int32_t serial_id_info_push(opple_platform_id_t platform_id, uint8_t local_id, uint8_t *data);
static int32_t serial_id_uptime(uint8_t local_id, uint8_t *info);
static int32_t serial_generate_down_send(opple_frame_cmd_t cmd, opple_platform_id_t id, uint8_t *data, uint16_t len, int32_t is_direct);
static int id_match(void *a, void *b);
static int32_t serial_local_id_find(uint8_t local_id, opple_platform_id_t *platform_id);
static int32_t frame_parse(uint8_t *data, uint16_t len);
static int serial_recv_cycle(opple_serial_context_t *pcontext );
static int32_t serial_snapshot_get_send(void);
static int32_t serial_process_cycle(opple_serial_context_t *pcontext);
static int32_t id_check_cycle(opple_serial_context_t *pcontext);
static int32_t down_queue_send(void);
static uint8_t packet_id_get(void);
static int32_t serial_kv_set(uint8_t *data, uint8_t len);
static int32_t serial_kv_get(uint8_t *data, uint8_t len, uint8_t **buf, uint8_t *buf_len);
static int32_t serial_send_by_key(opple_frame_cmd_t cmd, uint8_t *key, uint8_t num, int response, uint8_t packet_id);
static int32_t serial_mode_get_handle(uint8_t *key, uint8_t num, uint8_t packet_id);
static int32_t serial_mode_set_handle(uint8_t *data, uint8_t len);
typedef int32_t (*opple_serial_key_handler)(qm_tlv_block_t *tlv_block);
static int32_t serial_key_register(uint8_t key, opple_serial_key_handler handler, uint8_t *init_data, uint8_t len);
static int32_t serial_key_set(uint8_t key, uint8_t *data, uint8_t len, int32_t force);
static int32_t serial_key_generic_handler(qm_tlv_block_t *tlv_block);
static int32_t serial_key_network_status_handler(qm_tlv_block_t *tlv_block);
static int32_t serial_key_reset_handler(void);
static int32_t serial_key_remote_pair_handler(void);
static int32_t mcu_reboot_status_handler(void);
static int32_t mcu_notify_status_handler(uint8_t *data, uint8_t len);

static opple_serial_context_t *g_serial_context = NULL;

opple_serial_context_t *opple_serial_context_get(void)
{
    return g_serial_context;
}

static uint8_t frame_chksum(uint8_t *buffer, uint16_t length)
{
    uint16_t i = 0;
    uint8_t check_sum = 0;
    do{
        check_sum += buffer[i++];
    }while(i < length);


    return check_sum;
}

static uint8_t packet_id_get(void)
{
    opple_serial_context_t *serial_context = NULL;
    serial_context = opple_serial_context_get();
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(serial_context->serial_mutex);
#endif  
    serial_context->packet_id = (serial_context->packet_id == OPPLE_SEQ_MAX) ? 0 : serial_context->packet_id + 1;
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(serial_context->serial_mutex);
#endif
    return serial_context->packet_id;
}

qm_tlv_ctx_t *opple_serial_snapshot_ctx_get(void)
{
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    if(context == NULL){
        return NULL;
    }    
    return context->snapshot_tlv_ctx;
}

int32_t opple_serial_updated_snapshot_get(qm_tlv_ctx_t *tlv_ctx, uint8_t *count, bool_t copy)
{
    uint8_t index = 0;
    int32_t ret = 0;
    int32_t is_update = 0;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }
    if(tlv_ctx == NULL || count == NULL || *count == 0){
        return OPPLE_SERIAL_PARAM_ERR;
    }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    tlv_block = qm_tlv_block_next(context->snapshot_tlv_ctx, NULL);
    while(tlv_block && (index < *count)){

        qm_tlv_block_is_update(tlv_block, &is_update);
        if(is_update){
            m_tlv_block = qm_tlv_block_creat();
            if(m_tlv_block == NULL){
                ret = OPPLE_SERIAL_ALLOC_MEM_ERR;
                goto __exit;
            }
            if(copy){
                ret = qm_tlv_block_copy(m_tlv_block, tlv_block);
                if(ret != 0){
                    qm_tlv_block_delete(m_tlv_block);
                    ret = OPPLE_SERIAL_ALLOC_MEM_ERR;
                    goto __exit;
                }
                qm_tlv_block_update(tlv_block, 0);
            }else{
                m_tlv_block->tag = tlv_block->tag;
            }
            qm_tlv_block_add(tlv_ctx, m_tlv_block);
            index ++;
        }
        tlv_block = qm_tlv_block_next(context->snapshot_tlv_ctx, tlv_block);
    }

    *count = index;
__exit:
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    return ret;
}

int32_t opple_serial_snapshot_get(qm_tlv_ctx_t *tlv_ctx)
{
    int ret = 0;
    uint16_t tag = 0;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }
    if(tlv_ctx == NULL){
        return OPPLE_SERIAL_PARAM_ERR;
    }

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){
        qm_tlv_block_tag_get(tlv_block, &tag);
        m_tlv_block = qm_tlv_block_find(context->snapshot_tlv_ctx, tag);
        if(m_tlv_block == NULL){
            ret = OPPLE_SERIAL_PARAM_ERR;
            goto __exit;
        }
        qm_tlv_block_copy(tlv_block, m_tlv_block); 
        if(ret != 0){
            ret = OPPLE_SERIAL_ALLOC_MEM_ERR;
            goto __exit;
        } 
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

__exit:
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    return ret;

}

static int32_t frame_parse(uint8_t *data, uint16_t len)
{
    int32_t err = OPPLE_SERIAL_NO_ERR;
    opple_serial_context_t *context = NULL;
    opple_frame_header_t *head = (opple_frame_header_t*)data;
    opple_frame_cmd_t cmd = head->cmd;
    context = opple_serial_context_get();
    opple_platform_id_t platform_id=0;
    uint8_t config_type = 0;
    opple_serial_time_info_t time_info = {0};
    qm_tlv_ctx_t *tlv_ctx = NULL;
    uint8_t kv_len = 0;
    uint8_t *kv = NULL;

    if(*(data + len - 3) != frame_chksum(data + 2, len - 5)){
        return OPPLE_SERIAL_CHKSUM_ERR;
    }

    switch (cmd){

       case OPPLE_CMD_REPORT:

        if(len < OPPLE_FRAME_REPORT_MIN_LEN){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        serial_generate_send(cmd, head->seq, NULL, 0);

        if(context->state != OPPLE_SERIAL_STATE_NORMAL){
            OPPLE_SERIAL_LOG("process error");
            return OPPLE_SERIAL_PROCESS_ERR;
        }
        if(!context->snapshot_tlv_ctx){
            OPPLE_SERIAL_LOG("no snapshot");
            return OPPLE_SERIAL_PROCESS_ERR;
        }

#ifdef CONFIG_OPPLE_SERIAL_REPORT_SUPPORT

        tlv_ctx = qm_tlv_ctx_creat();
        if(tlv_ctx == NULL){
            return OPPLE_SERIAL_ALLOC_MEM_ERR;
        }
        err = qm_tlv_deserialize(tlv_ctx, data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, QM_BLOCK_TYPE_TLV);
        if(err != 0){
            qm_tlv_ctx_delete(tlv_ctx);
            return OPPLE_SERIAL_PARAM_ERR;
        }

        if(context->callbacks.m_reportcb){
            context->callbacks.m_reportcb(cmd, data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, (void*)tlv_ctx);
        }

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_lock(context->serial_mutex);
#endif

        qm_tlv_merge(context->snapshot_tlv_ctx, tlv_ctx);

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_unlock(context->serial_mutex);
#endif
        qm_tlv_ctx_delete(tlv_ctx);

#endif
       break;

       case OPPLE_CMD_SET:
       case OPPLE_CMD_ACTION:

        if(len < OPPLE_FRAME_MIN_LEN){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        err = serial_local_id_find(head->seq, &platform_id);
        if(err != OPPLE_SERIAL_NO_ERR){
            return err;
        }

#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT
        down_queue_send();
#endif

        if(context->callbacks.m_responsecb){
            context->callbacks.m_responsecb(cmd, platform_id, data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, NULL);
        }

       break;

       case OPPLE_CMD_GET:

        if(len < OPPLE_FRAME_MIN_LEN + 3){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        err = serial_local_id_find(head->seq, &platform_id);
        if(err != OPPLE_SERIAL_NO_ERR){
            return err;
        }

        tlv_ctx = qm_tlv_ctx_creat();
        if(tlv_ctx == NULL){
            return OPPLE_SERIAL_ALLOC_MEM_ERR;
        }
        err = qm_tlv_deserialize(tlv_ctx, data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, QM_BLOCK_TYPE_TLV);
        if(err != 0){
            qm_tlv_ctx_delete(tlv_ctx);
            return OPPLE_SERIAL_PARAM_ERR;
        }

        if(context->callbacks.m_responsecb){
            context->callbacks.m_responsecb(cmd, platform_id, data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, (void*)tlv_ctx);
        }

        qm_tlv_ctx_delete(tlv_ctx);

       break;

       case OPPLE_CMD_GET_SNAPSHOT:
        if(len < OPPLE_FRAME_SNAPSHOT_MIN_LEN){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        if(context->state != OPPLE_SERIAL_STATE_GETDEVICESNAPSHOT){
            OPPLE_SERIAL_LOG("process error");
        	return OPPLE_SERIAL_PROCESS_ERR;
        }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_lock(context->serial_mutex);
#endif
        if(context->snapshot_tlv_ctx){
            qm_tlv_ctx_delete(context->snapshot_tlv_ctx);
            context->snapshot_tlv_ctx = NULL;
        }

        tlv_ctx = qm_tlv_ctx_creat();
        if(tlv_ctx == NULL){
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_unlock(context->serial_mutex);
#endif
            return OPPLE_SERIAL_ALLOC_MEM_ERR;
        }
        err = qm_tlv_deserialize(tlv_ctx, data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, QM_BLOCK_TYPE_TLV);
        if(err != 0){
            qm_tlv_ctx_delete(tlv_ctx);
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_unlock(context->serial_mutex);
#endif
            return OPPLE_SERIAL_PARAM_ERR;
        }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_unlock(context->serial_mutex);
#endif
        context->snapshot_tlv_ctx = tlv_ctx;

		context->state = OPPLE_SERIAL_STATE_NORMAL;

        OPPLE_SERIAL_LOG("need report");
        

#ifdef CONFIG_OPPLE_SERIAL_REPORT_SUPPORT
        if(context->callbacks.m_reportcb){
            context->callbacks.m_reportcb(OPPLE_CMD_GET_SNAPSHOT, data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, (void*)tlv_ctx);
        }
#endif
       break;


       case OPPLE_CMD_NETCONFIG:

        if(len < OPPLE_FRAME_MIN_LEN + 1){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }
        config_type = *(data + OPPLE_FRAME_HEAD_LEN);
        if(config_type < OPPLE_NETWORK_CONFIG_START || config_type > OPPLE_NETWORK_CONFIG_END){
            return OPPLE_SERIAL_DATA_TYPE_ERR;
        }

        serial_generate_send(cmd, head->seq, NULL, 0);
        if(context->callbacks.m_network_configcb){
            context->callbacks.m_network_configcb(config_type);
        }

       break;

       case OPPLE_CMD_NOTIFY_STATUS:
       case OPPLE_CMD_BEACON:

        if(len < OPPLE_FRAME_MIN_LEN){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        serial_local_id_find(head->seq, &platform_id);

       break;

       case OPPLE_CMD_GET_STATUS:

        if(len < OPPLE_FRAME_MIN_LEN + 1){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        serial_send_by_key(cmd, data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, 1, head->seq);
        
       break;

       case OPPLE_CMD_TIMEGET:

        if(len < OPPLE_FRAME_MIN_LEN + 1){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }
        time_info.timezone = *(data + OPPLE_FRAME_HEAD_LEN);
        err = opple_serial_get_time(&time_info);
        if(err != 0){
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_ERROR, (uint8_t*)&time_info, sizeof(time_info));
        }else{
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_OK, (uint8_t*)&time_info, sizeof(time_info));
        }

       break;

       case OPPLE_CMD_GETMOD:

        if(len < OPPLE_FRAME_MIN_LEN + 1){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        err = serial_mode_get_handle(data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, head->seq);
        if(err != 0){
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_ERROR, NULL, 0);
        }
       break;

       case OPPLE_CMD_SETMOD:
        if(len < OPPLE_FRAME_MIN_LEN + 3){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        err = serial_mode_set_handle(data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN);
        if(err != 0){
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_ERROR, NULL, 0);
        }else{
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_OK, NULL, 0);
        }

       break;
       
       case OPPLE_CMD_GET_FLASH:
    
        if(len < OPPLE_FRAME_MIN_LEN + 1){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }

        err = serial_kv_get(data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN, &kv, &kv_len);
        if(err != 0){
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_ERROR, NULL, 0);
        }else{
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_OK, kv, kv_len);
            opple_serial_free(kv);
        }

       break;

       case OPPLE_CMD_SET_FLASH:

        if(len < OPPLE_FRAME_MIN_LEN + 3){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }
        err = serial_kv_set(data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN);
        if(err != 0){
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_ERROR, NULL, 0);
        }else{
            serial_generate_send_with_code(cmd, head->seq, OPPLE_SERIAL_COM_OK, NULL, 0);
        }

       break;

       case OPPLE_CMD_MCU_NOTIFY_STATUS:

        if(len < OPPLE_FRAME_MIN_LEN + 3){
            return OPPLE_SERIAL_PACKET_LEN_ERR;
        }
        serial_generate_send(cmd, head->seq, NULL, 0);

        mcu_notify_status_handler(data + OPPLE_FRAME_HEAD_LEN, len - OPPLE_FRAME_MIN_LEN);

       break;

       default:
           break;
    }

    return OPPLE_SERIAL_NO_ERR;
}

static int32_t mcu_notify_status_handler(uint8_t *data, uint8_t len)
{
    int32_t ret = 0;
    uint8_t *buf = NULL;
    int32_t size = 0;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    qm_tlv_block_t *tlv_block = NULL;

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }
    ret = qm_tlv_deserialize(tlv_ctx, data, len, QM_BLOCK_TYPE_TLV);
    if(ret != 0){
        ret = OPPLE_SERIAL_PARAM_ERR;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);

    while(tlv_block){

        qm_tlv_unpack_bytes_direct(tlv_block, &buf, &size);
        ret = serial_key_set(tlv_block->tag, buf, size, 1);
        if(ret != 0){
            goto __exit;
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

__exit:
    qm_tlv_ctx_delete(tlv_ctx);
    return ret;
}

static void frame_head_init(uint8_t *buffer, opple_frame_cmd_t cmd, uint8_t seq, uint8_t len)
{
    opple_frame_header_t *head = (opple_frame_header_t *)buffer;

    head->head = cpu_to_be16(OPPLE_FRAME_HEAD);
    head->ver  = OPPLE_PROTOCOL_VER;
    head->seq = seq;
    head->cmd  = cmd;
    head->len = len;
}

static int32_t frame_creat(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t data_len, uint8_t *buf, uint16_t *buf_len)
{
    opple_frame_unit_t *frame_unit = NULL;
    opple_frame_tail_t *frame_tail = NULL;
    uint8_t len = 0;

    switch (cmd){

       case OPPLE_CMD_REPORT:
       case OPPLE_CMD_NETCONFIG:
       case OPPLE_CMD_GET_SNAPSHOT:
       case OPPLE_CMD_EVENT:

        len = sizeof(opple_frame_unit_t);
        if(buf && *buf_len >= len){
            frame_unit = (opple_frame_unit_t*)buf;
            frame_head_init(buf, cmd, packet_id, len);
            frame_unit->chksum = frame_chksum(buf+2, len-5);
            frame_unit->tail = cpu_to_be16(OPPLE_FRAME_TAIL);
        }
        *buf_len = len;

       break;

       case OPPLE_CMD_NOTIFY_STATUS:
       case OPPLE_CMD_GET:
       case OPPLE_CMD_SET:
       case OPPLE_CMD_ACTION:
       case OPPLE_CMD_BEACON:

        len = OPPLE_FRAME_MIN_LEN + data_len;
        if(buf && *buf_len >= len){
            frame_head_init(buf, cmd, packet_id, len);
            memcpy(buf + OPPLE_FRAME_HEAD_LEN, data, data_len);
            frame_tail = (opple_frame_tail_t*)(buf + len - 3);
            frame_tail->chksum = frame_chksum(buf+2, len-5);
            frame_tail->tail = cpu_to_be16(OPPLE_FRAME_TAIL);
        }
        *buf_len = len;

       break;

       case OPPLE_CMD_GETMOD:
       case OPPLE_CMD_SETMOD:
       case OPPLE_CMD_TIMEGET:
       case OPPLE_CMD_GET_FLASH:
       case OPPLE_CMD_SET_FLASH:
       case OPPLE_CMD_GET_STATUS:

        len = OPPLE_FRAME_MIN_LEN + data_len + 1;
        if(buf && *buf_len >= len){
            frame_head_init(buf, cmd, packet_id, len);
            memcpy(buf + OPPLE_FRAME_HEAD_LEN, &code, 1);
            if(data && data_len > 0){
                memcpy(buf + OPPLE_FRAME_HEAD_LEN + 1, data, data_len);
            }
            frame_tail = (opple_frame_tail_t*)(buf + len - 3);
            frame_tail->chksum = frame_chksum(buf+2, len-5);
            frame_tail->tail = cpu_to_be16(OPPLE_FRAME_TAIL);
        }
        *buf_len = len;
        
       break;

       default:
            return OPPLE_SERIAL_CMD_ERR;
       break;
    }

    return OPPLE_SERIAL_NO_ERR;

}

static int32_t serial_kv_set(uint8_t *data, uint8_t len)
{
    int err = 0;
    uint8_t *bytes = NULL;
    int size = 0;
    char key[16] = {0};
    qm_tlv_ctx_t *tlv_ctx = NULL;
    qm_tlv_block_t *tlv_block = NULL;

    if(data == NULL || len == 0){
        return OPPLE_SERIAL_PARAM_ERR;
    }

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }
    err = qm_tlv_deserialize(tlv_ctx, data, len, QM_BLOCK_TYPE_TLV);
    if(err != 0){
        err = OPPLE_SERIAL_PARAM_ERR;
        goto __exit;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);
    while(tlv_block){

        snprintf(key, 16, OPPLE_SERIAL_KEY_FORMAT, tlv_block->tag);

        qm_tlv_unpack_bytes_direct(tlv_block, &bytes, &size);
        if(bytes && size){
            err = opple_serial_kv_set(key, bytes, size);
            if(err != 0){
                goto __exit;
            }
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

__exit:
    qm_tlv_ctx_delete(tlv_ctx);
    return err;
}

static int32_t serial_kv_get(uint8_t *data, uint8_t len, uint8_t **buf, uint8_t *buf_len)
{
    int err = 0;
    char key[16] = {0};
    uint8_t tag = 0;
    uint8_t key_num = len;
    uint8_t index = 0;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    uint8_t sta_kv_buf[OPPLE_KV_VALUE_LEN] = {0};
    uint8_t *dyn_kv_buf = NULL;
    uint8_t *kv_buf = NULL;
    int kv_len = CONFIG_OPPLE_SERIAL_KV_MAX_SIZE;

    uint8_t *serialize = NULL;
    int serialize_len = 0;

    if(data == NULL || len == 0 || buf_len == NULL){
        return OPPLE_SERIAL_PARAM_ERR;
    }

    if(key_num == 0){
        return OPPLE_SERIAL_PARAM_ERR;
    }

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }

    while(index < key_num){

        tag = *(data + index);
        kv_len = CONFIG_OPPLE_SERIAL_KV_MAX_SIZE;
        snprintf(key, 16, OPPLE_SERIAL_KEY_FORMAT, tag);

        err = opple_serial_kv_get(key, NULL, &kv_len);
        if(err != 0){
            goto __exit;
        }
        if(kv_len > OPPLE_KV_VALUE_LEN){
            dyn_kv_buf = (uint8_t*)opple_serial_malloc(kv_len);
            if(dyn_kv_buf == NULL){
                err = OPPLE_SERIAL_ALLOC_MEM_ERR;
                goto __exit;
            }
            kv_buf = dyn_kv_buf;
        }else{
            kv_buf = sta_kv_buf;
        }

        err = opple_serial_kv_get(key, kv_buf, &kv_len);
        if(err != 0){
            goto __exit;
        }

        tlv_block = qm_tlv_block_creat();
        if(tlv_block == NULL){
            err = OPPLE_SERIAL_ALLOC_MEM_ERR;
            goto __exit;
        }
        tlv_block ->tag = tag;

        err = qm_tlv_pack_bytes(tlv_block, kv_buf, kv_len);
        if(err != 0){
            err = OPPLE_SERIAL_PARAM_ERR;
            qm_tlv_block_delete(tlv_block);
            goto __exit;
        }

        if(dyn_kv_buf){
            qm_free(dyn_kv_buf);
            dyn_kv_buf = NULL;
        }

        qm_tlv_block_add(tlv_ctx, tlv_block);
        index ++;
    }

    qm_tlv_serialize(tlv_ctx, NULL, &serialize_len, QM_BLOCK_TYPE_TLV, QM_TLV_SERIALIZE_TYPE_ALL);
    if(serialize_len == 0){
        err = OPPLE_SERIAL_ALLOC_MEM_ERR;
        goto __exit;
    }
    serialize = (uint8_t*)opple_serial_malloc(serialize_len);
    if(serialize == NULL){
        err = OPPLE_SERIAL_ALLOC_MEM_ERR;
        goto __exit;
    }

    qm_tlv_serialize(tlv_ctx, serialize, &serialize_len, QM_BLOCK_TYPE_TLV, QM_TLV_SERIALIZE_TYPE_ALL);

    *buf = serialize;
    *buf_len = serialize_len;

    qm_tlv_ctx_delete(tlv_ctx);
    
    return err;

__exit:
    if(dyn_kv_buf){
        qm_free(dyn_kv_buf);
        dyn_kv_buf = NULL;
    }
    qm_tlv_ctx_delete(tlv_ctx);
    return err;
}

static int32_t serial_frame_info_creat(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t len, uint8_t resend, uint8_t **raw)
{
    int32_t ret = 0;
    uint8_t *buf = NULL;
    uint16_t packet_len = 0;
    uint16_t raw_len = 0;
    opple_frame_info_t *info = NULL;
    ret = frame_creat(cmd, packet_id, code, data, len, NULL, &packet_len);
    if(ret != OPPLE_SERIAL_NO_ERR){
        return ret;
    }

    raw_len = sizeof(opple_frame_info_t) + packet_len -1;
    buf = (uint8_t*)opple_serial_malloc(raw_len);
    if(buf == NULL){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    } 

    memset(buf, 0, raw_len);

    info = (opple_frame_info_t*)buf;
    info->len = packet_len;
    info->resend = resend;

    ret = frame_creat(cmd, packet_id, code, data, len, info->data, &packet_len);
    if(ret != OPPLE_SERIAL_NO_ERR){
        return ret;
    } 
    *raw = buf;
    return OPPLE_SERIAL_NO_ERR;
}


static uint32_t queue_len_get(qm_list_t *queue)
{
    uint32_t queue_len = 0;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    queue_len = queue->len;
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    return queue_len;
}

static int32_t queue_push(qm_list_t *queue, uint8_t *raw)
{
    qm_list_node_t *node = NULL;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();

    node = (qm_list_node_t*)raw;
    qm_list_node_init(node, raw+sizeof(qm_list_node_t));

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    qm_list_rpush(queue, node);
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_sem_signal(context->send_sem);
#endif
    return OPPLE_SERIAL_NO_ERR; 
}

/*
 *   -------------------------------
 *  |  len  | is_resend | PAYLOAD |
 *   ------------------------------
 *    2Bytes    1byte     nByte
 */

#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT

static int32_t serial_generate_resend(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t *data, uint16_t len, uint8_t **buf, int32_t is_direct)
{
    uint32_t ret = 0;
    uint8_t *raw = NULL;
    qm_list_t *queue = NULL;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();

    if(is_direct){
        if(queue_len_get(context->send_queue) >= CONFIG_OPPLE_SERIAL_SEND_QUEUE_NUM){
            return OPPLE_SERIAL_SEND_BUSY_ERR;
        }
    }else{
        if(queue_len_get(context->down_queue) >= CONFIG_OPPLE_SERIAL_DOWN_QUEUE_NUM){
            return OPPLE_SERIAL_SEND_BUSY_ERR;
        }
    }

    ret = serial_frame_info_creat(cmd, packet_id, 0, data, len, 1, &raw);
    if(ret != OPPLE_SERIAL_NO_ERR){
        return ret;
    }
    *buf = raw;

    if(is_direct){
        queue = context->send_queue;
    }else{
        queue = context->down_queue;
    }

    return queue_push(queue, raw);
}

#endif

static int32_t serial_generate_send(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t *data, uint16_t len)
{
    uint32_t ret = 0;
    uint8_t *raw = NULL;
    qm_list_t *queue = NULL;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    queue = context->send_queue;

    if(queue_len_get(queue) >= CONFIG_OPPLE_SERIAL_SEND_QUEUE_NUM){
        return OPPLE_SERIAL_SEND_BUSY_ERR;
    }

    ret = serial_frame_info_creat(cmd, packet_id, 0, data, len, 0, &raw);
    if(ret != OPPLE_SERIAL_NO_ERR){
        return ret;
    }

    return queue_push(queue, raw);
}

static int32_t serial_generate_send_with_code(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t code, uint8_t *data, uint16_t len)
{
    uint32_t ret = 0;
    uint8_t *raw = NULL;
    qm_list_t *queue = NULL;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    queue = context->send_queue;

    if(queue_len_get(queue) >= CONFIG_OPPLE_SERIAL_SEND_QUEUE_NUM){
        return OPPLE_SERIAL_SEND_BUSY_ERR;
    }

    ret = serial_frame_info_creat(cmd, packet_id, code, data, len, 0, &raw);
    if(ret != OPPLE_SERIAL_NO_ERR){
        return ret;
    }

    return queue_push(queue, raw);
}

static int32_t serial_id_info_push(opple_platform_id_t platform_id, uint8_t local_id, uint8_t *data)
{
    opple_serial_context_t *context = NULL;
    opple_serial_id_info_t *id_info = NULL;
    uint8_t *buf = NULL;
    uint16_t buf_len = 0;
    context = opple_serial_context_get();

    qm_list_node_t *node = NULL;

    buf_len = sizeof(opple_serial_id_info_t) + sizeof(qm_list_node_t);
    buf = (uint8_t*)opple_serial_malloc(buf_len);
    if(buf == NULL){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }
    node = (qm_list_node_t*)buf;

    memset(buf, 0, buf_len);

    qm_list_node_init(node, buf + sizeof(qm_list_node_t));

    id_info = (opple_serial_id_info_t*)(buf + sizeof(qm_list_node_t));
    id_info->platform_id = platform_id;
    id_info->local_id = local_id;
#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT
    id_info->retry_count = CONFIG_OPPLE_SERIAL_SEND_RETRY_COUNT;
#else
    id_info->retry_count = 0;
#endif
    id_info->data = data;
    id_info->time = 0; 
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    qm_list_rpush(context->id_queue, node);
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    return OPPLE_SERIAL_NO_ERR;
}

static uint8_t is_serial_id_update(void)
{
    opple_serial_context_t *context = NULL;
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    qm_list_t *list = NULL;
    opple_serial_id_info_t *id_info = NULL;
    uint8_t *buf = NULL;
    uint8_t is_update = 0;

    context = opple_serial_context_get();
    list = context->id_queue;

    self.direction = LIST_HEAD;
    self.next = list->head;
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif

    node = qm_list_iterator_next(&self);
    while (node) {
        buf = (uint8_t*)node;
        id_info = (opple_serial_id_info_t*)(buf+sizeof(qm_list_node_t));
        if(id_info->is_update){
            is_update = 1;
            break;
        }
        node = qm_list_iterator_next(&self);
    }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif

    return is_update;
}

static int32_t serial_id_uptime(uint8_t local_id, uint8_t *info)
{
    opple_serial_context_t *context = NULL;
    opple_serial_id_info_t id_info = {0};
    qm_list_node_t *node = NULL;
    opple_serial_id_info_t *p_id_info = NULL;

    context = opple_serial_context_get();

    id_info.local_id = local_id;

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_find(context->id_queue, &id_info);
    if(node){
        p_id_info = (opple_serial_id_info_t*)node->val;
        p_id_info->is_update = 1;
        p_id_info->data = info;
        p_id_info->is_send = 1;
        p_id_info->time = opple_serial_get_timems() + CONFIG_OPPLE_SERIAL_ACK_TIMEOUT_MS;
    }

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif

    return OPPLE_SERIAL_NO_ERR;
}

static int id_match(void *a, void *b)
{
    opple_serial_id_info_t *a_info = (opple_serial_id_info_t*)a;
    opple_serial_id_info_t *b_info = (opple_serial_id_info_t*)b;

    if(a_info->local_id == b_info->local_id){
        return 1;
    }else{
        return 0;
    }
}

static int32_t serial_local_id_find(uint8_t local_id, opple_platform_id_t *platform_id)
{
    qm_list_node_t *node = NULL;
    opple_serial_context_t *context = NULL;
    opple_serial_id_info_t id_info = {0};
    opple_serial_id_info_t *id = NULL;
    context = opple_serial_context_get();

    id_info.local_id = local_id;

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_find(context->id_queue, &id_info);

    if(node){
        id = (opple_serial_id_info_t*)node->val;
        if(!id->is_send){
            #if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
            opple_serial_mutex_unlock(context->serial_mutex);
            #endif
            return OPPLE_SERIAL_NO_ERR;
        }
        *platform_id = id->platform_id;
        qm_list_remove(context->id_queue, node);

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_unlock(context->serial_mutex);
#endif
        if(id->data){
            opple_serial_free(id->data);
        }
        opple_serial_free(node);

        return OPPLE_SERIAL_NO_ERR;
    }else{
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_unlock(context->serial_mutex);
#endif
        return OPPLE_SERIAL_MSG_NUM_ERR;
    }
}

static int32_t serial_generate_down_send(opple_frame_cmd_t cmd, opple_platform_id_t id, uint8_t *data, uint16_t len, int32_t is_direct)
{
    int32_t err = OPPLE_SERIAL_NO_ERR;
    uint8_t local_id = 0;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    uint8_t *buf = NULL;
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }

    local_id = packet_id_get();

#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT

    err = serial_id_info_push(id, local_id, NULL);
    if(err != OPPLE_SERIAL_NO_ERR){
        return err;
    }

    err = serial_generate_resend(cmd, local_id, data, len, &buf, is_direct);
    if(err != OPPLE_SERIAL_NO_ERR){
        return err;
    }

#else

    err = serial_id_info_push(id, local_id, NULL);
    if(err != OPPLE_SERIAL_NO_ERR){
        return err;
    }

    err = serial_generate_send(cmd, local_id, data, len);
    if(err != OPPLE_SERIAL_NO_ERR){
        return err;
    }

#endif

    return OPPLE_SERIAL_NO_ERR;
}

static int32_t serial_send_by_key(opple_frame_cmd_t cmd, uint8_t *key, uint8_t num, int response, uint8_t packet_id)
{
    #define SERIAL_NOTIFY_BUF_LEN   8
    int32_t need_free = 0;
    int32_t tlv_len = 0;
    uint8_t buf[SERIAL_NOTIFY_BUF_LEN] = {0};
    uint8_t *alloc_buf = NULL;
    uint8_t index = 0;
    int32_t ret = 0;
    qm_tlv_block_t *tlv_block = NULL;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }

    if(key == NULL || num == 0){
        return OPPLE_SERIAL_PARAM_ERR;
    }

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif

    while(index < num){
        tlv_block = qm_tlv_block_find(context->tlv_ctx, (uint16_t)key[index]);
        if(tlv_block){
            qm_tlv_block_serialize(tlv_block);
        }else{
            ret = OPPLE_SERIAL_PARAM_ERR;
            goto __exit;
        }

        index ++;
    }

    qm_tlv_serialize(context->tlv_ctx, NULL, &tlv_len, QM_BLOCK_TYPE_TLV, QM_TLV_SERIALIZE_TYPE_PARTIAL);
    if(tlv_len <= SERIAL_NOTIFY_BUF_LEN){
        alloc_buf = buf;
    }else{
        alloc_buf = (uint8_t*)opple_serial_malloc(tlv_len);
        if(alloc_buf == NULL){
            ret = OPPLE_SERIAL_COM_MEM_ERROR;
            goto __exit;
        }
        need_free = 1;
    }

    qm_tlv_serialize(context->tlv_ctx, alloc_buf, &tlv_len, QM_BLOCK_TYPE_TLV, QM_TLV_SERIALIZE_TYPE_PARTIAL);

__exit:

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif

    if(response){
        if(ret == 0){
            ret = serial_generate_send_with_code(cmd, packet_id, OPPLE_SERIAL_COM_OK, alloc_buf, tlv_len);
        }else{
            ret = serial_generate_send_with_code(cmd, packet_id, OPPLE_SERIAL_COM_ERROR, NULL, 0);
        }
    }else{
        if(ret == 0){
            ret = serial_generate_down_send(cmd, 0, alloc_buf, tlv_len, 1);
        }
    }

    if(need_free){
        opple_serial_free(alloc_buf);
        alloc_buf = NULL;
    }

    return ret;
}

#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT

static int32_t down_queue_send(void)
{
    qm_list_node_t *node = NULL;
    qm_list_t *down_queue = NULL;
    qm_list_t *send_queue = NULL;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();

    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }

    down_queue = context->down_queue;
    send_queue = context->send_queue;

    if(is_serial_id_update()){
        return OPPLE_SERIAL_NO_ERR;
    }

    if(queue_len_get(down_queue) == 0){
        return OPPLE_SERIAL_NO_ERR;
    }

    if(queue_len_get(send_queue) >= CONFIG_OPPLE_SERIAL_SEND_QUEUE_NUM){
        OPPLE_SERIAL_LOG("send queue is full");
        return OPPLE_SERIAL_SEND_BUSY_ERR;
    }

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_lpop(down_queue);

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif

    queue_push(send_queue, (uint8_t*)node);

    return OPPLE_SERIAL_NO_ERR; 
}

#endif

static int32_t id_check_cycle(opple_serial_context_t *pcontext)
{
    qm_list_t *list = pcontext->id_queue;
    qm_list_iterator_t self;
    qm_list_node_t *node = NULL;
    uint8_t *buf = NULL;
    opple_serial_id_info_t *id_info = NULL;
    uint32_t cur_time = 0;
    int32_t send = 0;

    if (!qm_utils_time_is_expired(&pcontext->id_check_time)) {
        return OPPLE_SERIAL_NO_ERR;
    }
    qm_utils_time_countdown_ms(&pcontext->id_check_time, pcontext->id_check_interval);

    if(queue_len_get(pcontext->id_queue) == 0){
        return OPPLE_SERIAL_NO_ERR;
    }

    cur_time = opple_serial_get_timems();

    self.direction = LIST_HEAD;
    self.next = list->head;
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(pcontext->serial_mutex);
#endif

    node = qm_list_iterator_next(&self);
    while (node) {
        buf = (uint8_t*)node;
        id_info = (opple_serial_id_info_t*)(buf+sizeof(qm_list_node_t));
        if(id_info->is_update && (cur_time - id_info->time) < (UINT32_MAX / 2) ){
            id_info->is_send = 0;
            id_info->is_update = 0;
            if(id_info->retry_count == 0){
                qm_list_remove(list, node);
                if(id_info->data){
                    opple_serial_free(id_info->data);
                }
                opple_serial_free(node);
                send = 1;
            }else{
                qm_list_rpush(pcontext->send_queue, (qm_list_node_t*)id_info->data);
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
                opple_serial_sem_signal(pcontext->send_sem);
#endif
            }
            id_info->retry_count--;
        }
        node = qm_list_iterator_next(&self);
    }

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(pcontext->serial_mutex);
#endif

#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT
    if(send){
        down_queue_send();
    }
#endif

    return OPPLE_SERIAL_NO_ERR;
}

static int32_t serial_snapshot_get_send(void)
{
    return serial_generate_send(OPPLE_CMD_GET_SNAPSHOT, packet_id_get(), NULL, 0);
}

int32_t opple_serial_state_set(opple_serial_state_t state)
{
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    context->state = state;
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    return OPPLE_SERIAL_NO_ERR;
}

int32_t opple_serial_state_get(opple_serial_state_t *state)
{
    opple_serial_context_t *context = NULL;
	if(state == NULL){
		return OPPLE_SERIAL_PARAM_ERR;
	}
    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    *state = context->state;
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    return OPPLE_SERIAL_NO_ERR;
}

opple_module_network_status_t opple_serial_module_network_status_get(void)
{
    opple_module_network_status_t status;
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    status = context->network_status;
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    return status;
}


static int32_t serial_key_network_status_handler(qm_tlv_block_t *tlv_block)
{
    uint8_t key = 0;
    int32_t size = 0;
    int32_t need_notify = 0;
    opple_serial_context_t *context = NULL;
    opple_serial_network_info_t *network_info = NULL;
    context = opple_serial_context_get();

    qm_tlv_unpack_bytes_direct(tlv_block, (uint8_t**)&network_info, &size);

    if(context->network_status != network_info->network_status){
    #if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_lock(context->serial_mutex);
    #endif
        context->network_status = network_info->network_status;
    #if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
        opple_serial_mutex_unlock(context->serial_mutex);
    #endif
        need_notify = 1;
    }
    
    if(need_notify){
        key = OPPLE_MODULE_STATUS_KEY_NETWORK;
        serial_send_by_key(OPPLE_CMD_NOTIFY_STATUS, &key, 1, 0, 0);
    }
    return OPPLE_SERIAL_NO_ERR;
}

static int32_t serial_key_reset_handler(void)
{
    uint8_t key = OPPLE_MODULE_STATUS_KEY_RESET;
    serial_send_by_key(OPPLE_CMD_NOTIFY_STATUS, &key, 1, 0, 0);
    return OPPLE_SERIAL_NO_ERR;
}

static int32_t serial_key_remote_pair_handler(void)
{
    uint8_t key = OPPLE_MODULE_STATUS_KEY_REMOTE_PAIR;
    serial_send_by_key(OPPLE_CMD_NOTIFY_STATUS, &key, 1, 0, 0);
    return OPPLE_SERIAL_NO_ERR;
}

static int32_t mcu_reboot_status_handler(void)
{
    opple_serial_context_t *context = NULL;
    context = opple_serial_context_get();
        context->state = OPPLE_SERIAL_STATE_GETDEVICESNAPSHOT;
    return OPPLE_SERIAL_NO_ERR;
}

static int32_t serial_key_generic_handler(qm_tlv_block_t *tlv_block)
{
    switch(tlv_block->tag){

        case OPPLE_MODULE_STATUS_KEY_NETWORK:
            return serial_key_network_status_handler(tlv_block);
        break;

        case OPPLE_MODULE_STATUS_KEY_RESET:
            return serial_key_reset_handler();
        break;

        case OPPLE_MODULE_STATUS_KEY_REMOTE_PAIR:
            
            return serial_key_remote_pair_handler();
        break;

        case OPPLE_MCU_STATUS_KEY_REBOOT:
            return mcu_reboot_status_handler();
        break;

        default:
        break;
    }
    return OPPLE_SERIAL_NO_ERR;
}

int32_t opple_serial_module_network_status_set(opple_module_network_status_t network_status)
{
    uint8_t mac[6] = {0};
    opple_serial_network_info_t network_info = {0};

    network_info.network_status = network_status;

    opple_serial_wifi_mac_get(mac);
    memcpy(network_info.mac, mac, 6);

    network_info.rssi = opple_serial_wifi_rssi_get();

    return serial_key_set(OPPLE_MODULE_STATUS_KEY_NETWORK, (uint8_t*)&network_info, sizeof(opple_serial_network_info_t), 0);
}

int32_t opple_serial_module_remote_pair_status_set(opple_remote_pair_status_t pair_status)
{
    return serial_key_set(OPPLE_MODULE_STATUS_KEY_REMOTE_PAIR, (uint8_t*)&pair_status, sizeof(uint8_t), 1);
}

int32_t opple_serial_module_reset(void)
{
    uint8_t status = 0x01;
    return serial_key_set(OPPLE_MODULE_STATUS_KEY_RESET, (uint8_t*)&status, sizeof(uint8_t), 1);
}


static int32_t serial_key_register(uint8_t key, opple_serial_key_handler handler, uint8_t *init_data, uint8_t len)
{
    int32_t ret = 0;
    qm_tlv_block_t *tlv_block = NULL;
    opple_serial_context_t *context = NULL;

    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }

    tlv_block = qm_tlv_block_creat();
    if(!tlv_block){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }

    tlv_block->tag = key;
    tlv_block->arg = (void*)handler;

    if(init_data && len > 0){
        ret = qm_tlv_pack_bytes(tlv_block, init_data, len);
        if(ret != 0){
            qm_tlv_block_delete(tlv_block);
            return OPPLE_SERIAL_ALLOC_MEM_ERR;
        }
    }
    qm_tlv_block_add(context->tlv_ctx, tlv_block);

    return OPPLE_SERIAL_NO_ERR;
}

static int32_t serial_key_set(uint8_t key, uint8_t *data, uint8_t len, int32_t force)
{
    int32_t ret = 0;
    uint8_t *buf = NULL;
    int32_t size = 0;
    opple_serial_context_t *context = NULL;

    qm_tlv_block_t *tlv_block = NULL;
    opple_serial_key_handler handler = NULL;

    context = opple_serial_context_get();

    tlv_block = qm_tlv_block_find(context->tlv_ctx, (uint16_t)key);
    if(tlv_block == NULL){
        return OPPLE_SERIAL_PARAM_ERR;
    }

    handler = (opple_serial_key_handler)tlv_block->arg; 

    if(!force && tlv_block->len == len){
        qm_tlv_unpack_bytes_direct(tlv_block, &buf, &size);
        if(0 == memcmp(buf, data, len)){
            return OPPLE_SERIAL_NO_ERR;
        }
    }

    ret = qm_tlv_pack_bytes(tlv_block, data, len);
    if(ret != 0){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }
    if(handler){
        ret = handler(tlv_block);
    }

    return OPPLE_SERIAL_NO_ERR;
}

int32_t opple_serial_cloud_set(opple_platform_id_t id, uint8_t *data, uint8_t len)
{
    return serial_generate_down_send(OPPLE_CMD_SET, id, data, len, 0);
}

int32_t opple_serial_beacon_set(opple_platform_id_t id, uint8_t *data, uint8_t len)
{
    return serial_generate_down_send(OPPLE_CMD_BEACON, id, data, len, 1);
}

int32_t opple_serial_cloud_action(opple_platform_id_t id, uint8_t *data, uint8_t len)
{
	return serial_generate_down_send(OPPLE_CMD_ACTION, id, data, len, 0);
}

int32_t opple_serial_cloud_get(opple_platform_id_t id, uint8_t *data, uint8_t len)
{
    return serial_generate_down_send(OPPLE_CMD_GET, id, data, len, 0);
}
 
static int32_t serial_mode_get_handle(uint8_t *key, uint8_t num, uint8_t packet_id)
{
    int32_t ret = 0;
    int32_t err = 0;
    uint8_t index = 0;
    qm_tlv_block_t *tlv_block = NULL;
    opple_serial_context_t *context = NULL;
    opple_serial_mode_callbacks_t *mode_callbacks = NULL;

    if(key == NULL && num == 0){
        return OPPLE_SERIAL_PARAM_ERR;
    }

    context = opple_serial_context_get();
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    while(index < num){

        tlv_block = qm_tlv_block_find(context->tlv_ctx, (uint16_t)key[index]);
        if(tlv_block == NULL){
            err = OPPLE_SERIAL_PARAM_ERR;
            goto __exit;
        }

        mode_callbacks = (opple_serial_mode_callbacks_t*)tlv_block->arg;
        if(mode_callbacks){
            if(mode_callbacks->m_getmode){
                ret = mode_callbacks->m_getmode((void*)tlv_block);
                if(ret != 0){
                    err = OPPLE_SERIAL_PARAM_ERR;
                    goto __exit;
                }
            }
        }

        index ++;
    }
    
__exit:
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    if(err != 0){
        return err;
    }

    serial_send_by_key(OPPLE_CMD_GETMOD, key, num, 1, packet_id);   

    return OPPLE_SERIAL_NO_ERR;
}

static int32_t serial_mode_set_handle(uint8_t *data, uint8_t len)
{
    int ret = 0;
    qm_tlv_ctx_t *tlv_ctx = NULL;
    qm_tlv_block_t *tlv_block = NULL;
    qm_tlv_block_t *m_tlv_block = NULL;
    opple_serial_context_t *context = NULL;
    opple_serial_mode_callbacks_t *mode_callbacks = NULL;

    if(data == NULL || len == 0){
        return OPPLE_SERIAL_PARAM_ERR;
    }

    context = opple_serial_context_get();

    tlv_ctx = qm_tlv_ctx_creat();
    if(tlv_ctx == NULL){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }

    ret = qm_tlv_deserialize(tlv_ctx, data, len, QM_BLOCK_TYPE_TLV);
    if(ret != 0){
        qm_tlv_ctx_delete(tlv_ctx);
        return OPPLE_SERIAL_PARAM_ERR;
    }

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);

    while(tlv_block){

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif  
        m_tlv_block = qm_tlv_block_find(context->tlv_ctx, (uint16_t)tlv_block->tag);
        if(m_tlv_block == NULL){
            ret =  OPPLE_SERIAL_PARAM_ERR;
        #if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
            opple_serial_mutex_unlock(context->serial_mutex);
        #endif
            break;
        }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif

        mode_callbacks = (opple_serial_mode_callbacks_t*)m_tlv_block->arg;
        if(mode_callbacks){
            if(mode_callbacks->m_setmode){
                ret = mode_callbacks->m_setmode((void*)tlv_block);
                if(ret != 0){
                    ret =  OPPLE_SERIAL_PARAM_ERR;
                    break;
                }
            }
        }
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

    qm_tlv_ctx_delete(tlv_ctx);
    return ret;
}

int32_t opple_serial_mode_register(uint8_t key, opple_serial_setmode_fn setmode_fn, opple_serial_getmode_fn getmode_fn)
{
    qm_tlv_block_t *tlv_block = NULL;
    opple_serial_context_t *context = NULL;
    opple_serial_mode_callbacks_t *mode_callbacks = NULL;

    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }

    mode_callbacks = (opple_serial_mode_callbacks_t*)qm_malloc(sizeof(opple_serial_mode_callbacks_t));
    if(mode_callbacks == NULL){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }

    memset(mode_callbacks, 0, sizeof(opple_serial_mode_callbacks_t));

    mode_callbacks->m_getmode = getmode_fn;
    mode_callbacks->m_setmode = setmode_fn;

    tlv_block = qm_tlv_block_creat();
    if(tlv_block == NULL){
        qm_free(mode_callbacks);
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }

    tlv_block->tag = key;
    tlv_block->arg = (void*)mode_callbacks;

    qm_tlv_block_add(context->tlv_ctx, tlv_block);

    return OPPLE_SERIAL_NO_ERR;
}

int32_t opple_serial_ext_cmd_ack(opple_frame_cmd_t cmd, uint8_t packet_id, uint8_t err_code, uint8_t *data, uint16_t len)
{
    uint8_t *buf = NULL;
    int32_t err = OPPLE_SERIAL_NO_ERR;
    uint8_t code = err_code;
    if(data != NULL && len >0){
        buf = (uint8_t*)opple_serial_malloc(len+1);
        if(buf == NULL){
            return OPPLE_SERIAL_ALLOC_MEM_ERR;
        }
        memcpy(buf, &code, 1);
        memcpy(buf+1, data, len);
        err = serial_generate_send(cmd, packet_id, buf, len+1);
        opple_serial_free(buf);
    }else{
        err = serial_generate_send(cmd, packet_id, &code, 1);
    }
    return err;
}

int32_t opple_serial_init(opple_serial_param_t *serial_param)
{
    opple_serial_network_info_t network_info = {0};
    opple_serial_context_t *serial_context = NULL;
    if(serial_param == NULL || serial_param->pread_buf == NULL){
        return OPPLE_SERIAL_PARAM_ERR;
    }

    serial_context = (opple_serial_context_t*)opple_serial_malloc(sizeof(opple_serial_context_t));
    if(NULL == serial_context){
        return OPPLE_SERIAL_ALLOC_MEM_ERR;
    }

    memset(serial_context, 0, sizeof(opple_serial_context_t));

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    serial_context->serial_mutex = opple_serial_mutex_init();
    serial_context->send_sem = opple_serial_sem_init(0);
#endif

    serial_context->send_queue = qm_list_new();
    serial_context->id_queue = qm_list_new();

#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT
    serial_context->down_queue = qm_list_new();
#endif

    serial_context->id_queue->match = id_match;

    qm_utils_time_init(&serial_context->process_get_time);
    serial_context->process_get_interval = OPPLE_SERIAL_PROCESS_TIMEOUT_MS;
    qm_utils_time_countdown_ms(&serial_context->process_get_time, serial_context->process_get_interval);

    qm_utils_time_init(&serial_context->send_interval_time);
    serial_context->send_interval = CONFIG_OPPLE_SERIAL_SEND_INTERVAL_TIMEOUT_MS;
    qm_utils_time_countdown_ms(&serial_context->send_interval_time, serial_context->send_interval);

    qm_utils_time_init(&serial_context->id_check_time);
    serial_context->id_check_interval = CONFIG_OPPLE_SERIAL_ID_CHECK_TIMEOUT_MS;
    qm_utils_time_countdown_ms(&serial_context->id_check_time, serial_context->id_check_interval);

    serial_context->callbacks.m_network_configcb = serial_param->callbacks.m_network_configcb;
    serial_context->callbacks.m_reportcb = serial_param->callbacks.m_reportcb;
    serial_context->callbacks.m_responsecb = serial_param->callbacks.m_responsecb;

    serial_context->pread_buf = serial_param->pread_buf;
    serial_context->read_buf_size = serial_param->read_buf_size;

    serial_context->parse_state = OPPLE_PARSE_STATE_HEAD;
    serial_context->need_len = 1;
    serial_context->is_start_recv = 0;
    serial_context->recv_timeout_count = 0;
    serial_context->packet_id = OPPLE_SEQ_MAX;
    serial_context->state = OPPLE_SERIAL_STATE_GETDEVICESNAPSHOT;
    serial_context->network_status = OPPLE_MODULE_STATUS_NONE;

    serial_context->tlv_ctx = qm_tlv_ctx_creat();

    serial_context->snapshot_tlv_ctx = qm_tlv_ctx_creat();

    serial_context->handle = opple_serial_uart_init();

    g_serial_context = serial_context;

    network_info.network_status = serial_context->network_status;
    serial_key_register(OPPLE_MODULE_STATUS_KEY_NETWORK, serial_key_generic_handler, (uint8_t*)&network_info, sizeof(network_info));
    serial_key_register(OPPLE_MODULE_STATUS_KEY_RESET, serial_key_generic_handler, NULL, 0);
    serial_key_register(OPPLE_MODULE_STATUS_KEY_REMOTE_PAIR, serial_key_generic_handler, NULL, 0);
    serial_key_register(OPPLE_MCU_STATUS_KEY_REBOOT, serial_key_generic_handler, NULL, 0);
    
    return OPPLE_SERIAL_NO_ERR;
}

static int serial_recv_cycle(opple_serial_context_t *pcontext)
{
    int rc = OPPLE_SERIAL_NO_ERR;
    int recv_len = 0;
    uint16_t packet_len;
    uint32_t left_t = CONFIG_OPPLE_SERIAL_RECV_TIMEOUT_MS;
    uint16_t need_len = pcontext->need_len;

    recv_len = opple_serial_recv(pcontext->handle, pcontext->pread_buf+pcontext->read_pos, need_len, left_t);
    if(recv_len == 0){
        if(pcontext->is_start_recv){
            pcontext->recv_timeout_count++;
            if(pcontext->recv_timeout_count >= 3){
                goto clear;
            }
        }else{
            return OPPLE_SERIAL_NO_ERR;
        }
    }   
    
    pcontext->read_pos += recv_len;
    
    switch(pcontext->parse_state){

        case OPPLE_PARSE_STATE_HEAD:

            if(pcontext->pread_buf[pcontext->read_pos-1] != OPPLE_FRAME_HEAD_PART){
                goto clear;
            }else{
                pcontext->is_start_recv = QM_TRUE;
                pcontext->parse_state = OPPLE_PARSE_STATE_HEAD_1;
                pcontext->need_len = 1;
            }

        break;

        case OPPLE_PARSE_STATE_HEAD_1:
            if(pcontext->need_len <= recv_len){
                if(pcontext->pread_buf[pcontext->read_pos-1] != OPPLE_FRAME_HEAD_1_PART){
                    goto clear;
                }else{
                    pcontext->parse_state = OPPLE_PARSE_STATE_LEN;
                    pcontext->need_len = 1;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;
        case OPPLE_PARSE_STATE_VER:
            if(pcontext->need_len <= recv_len){
                if( OPPLE_VER_GET(pcontext->pread_buf[pcontext->read_pos-1]) != OPPLE_PROTOCOL_VER){
                    goto clear;
                }else{
                    pcontext->parse_state = OPPLE_PARSE_STATE_DATA;
                    pcontext->need_len = pcontext->packet_len - 4 -2;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;
        case OPPLE_PARSE_STATE_LEN:

            if(pcontext->need_len <= recv_len){
                packet_len = pcontext->pread_buf[pcontext->read_pos-1];
                if(packet_len > (pcontext->read_buf_size - pcontext->read_pos)){
                    OPPLE_SERIAL_LOG("serial recv buffer over flow");
                    goto clear;
                }else if(packet_len < OPPLE_FRAME_MIN_LEN){
                    OPPLE_SERIAL_LOG("serial recv buffer is too short");
                    goto clear;
                }else{
                    pcontext->parse_state = OPPLE_PARSE_STATE_VER;
                    pcontext->packet_len = packet_len;
                    pcontext->need_len = 1;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;

        case OPPLE_PARSE_STATE_DATA:

            if(pcontext->need_len <= recv_len){
                pcontext->parse_state = OPPLE_PARSE_STATE_TAIL;
                pcontext->need_len = 1;
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;

        case OPPLE_PARSE_STATE_TAIL:
            if(pcontext->need_len <= recv_len){
                if(pcontext->pread_buf[pcontext->read_pos-1] != OPPLE_FRAME_TAIL_PART){
                    goto clear;
                }else{
                    pcontext->parse_state = OPPLE_PARSE_STATE_TAIL_1;
                    pcontext->need_len = 1;
                }

            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }

        break;

        case OPPLE_PARSE_STATE_TAIL_1:

            if(pcontext->need_len <= recv_len){
                if(pcontext->pread_buf[pcontext->read_pos-1] != OPPLE_FRAME_TAIL_1_PART){
                    goto clear;
                }else{
                    OPPLE_SERIAL_LOG("opple serial recv len %d", pcontext->read_pos);
                    OPPLE_SERIAL_HEX_LOG("opple serial recv data", pcontext->pread_buf, pcontext->read_pos);
                    rc = frame_parse(pcontext->pread_buf, pcontext->read_pos);
                    goto clear;
                }
            }else{
                pcontext->need_len = pcontext->need_len - recv_len;
            }
        break;

        default:
            break;
    }

    return rc;

clear:
    pcontext->is_start_recv = QM_FALSE;
    pcontext->recv_timeout_count = 0;
    pcontext->parse_state = OPPLE_PARSE_STATE_HEAD;
    pcontext->read_pos = 0;
    pcontext->need_len = 1;
    return rc;
}

int32_t opple_serial_send_loop(void)
{
    uint32_t left = 0;
    opple_frame_info_t *info = NULL;
    uint32_t send_queue_len = 0;
    qm_list_node_t *node = NULL;
    opple_serial_context_t *context = NULL;
    opple_frame_header_t *frame_header = NULL;

    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_sem_wait(context->send_sem, 0);
#endif

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    send_queue_len = context->send_queue->len;
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif

    if(send_queue_len == 0){
        goto exit;
    }

    if (!qm_utils_time_is_expired(&context->send_interval_time)) {
         goto exit;
    }
#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_lock(context->serial_mutex);
#endif
    node = qm_list_lpop(context->send_queue);

    send_queue_len = context->send_queue->len;

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT
    opple_serial_mutex_unlock(context->serial_mutex);
#endif
    if(node){
        info = (opple_frame_info_t*)node;
        opple_serial_send(context->handle, info->data, info->len);
        OPPLE_SERIAL_HEX_LOG("serial send", info->data, info->len);
        if(info->resend){
            frame_header = (opple_frame_header_t*)info->data;
            serial_id_uptime(frame_header->seq, (uint8_t*)info);
        }else{
            opple_serial_free(info);
        }
    }

    qm_utils_time_countdown_ms(&context->send_interval_time, context->send_interval);

exit:

#ifdef CONFIG_OPPLE_SERIAL_RESEND_SUPPORT
    down_queue_send();
#endif

#if CONFIG_OPPLE_SERIAL_RTOS_SUPPORT

    if(send_queue_len > 0){
        left = qm_utils_time_left(&context->send_interval_time);
        if(left > context->send_interval || left <= 0){
            qm_msleep(50);
        }else{
            qm_msleep(left);
        }
        opple_serial_sem_signal(context->send_sem);
    }

#endif

    return OPPLE_SERIAL_NO_ERR;
}

static int32_t serial_process_cycle(opple_serial_context_t *pcontext)
{
    if(pcontext->state != OPPLE_SERIAL_STATE_GETDEVICESNAPSHOT){
        return OPPLE_SERIAL_NO_ERR;
    }

    if (!qm_utils_time_is_expired(&pcontext->process_get_time)) {
        return OPPLE_SERIAL_NO_ERR;
    }
    qm_utils_time_countdown_ms(&pcontext->process_get_time, pcontext->process_get_interval);
    
    if(pcontext->state == OPPLE_SERIAL_STATE_GETDEVICESNAPSHOT){
        serial_snapshot_get_send();
    }

    return OPPLE_SERIAL_NO_ERR;
}

int32_t opple_serial_recv_loop(void)
{
    int32_t rc = OPPLE_SERIAL_NO_ERR;
    uint32_t left_t = 0;
    opple_serial_context_t *context = NULL;

    context = opple_serial_context_get();
    if(context == NULL){
        return OPPLE_SERIAL_NO_INIT_ERR;
    }

    serial_process_cycle(context);

    id_check_cycle(context);

    /* acquire package in cycle */
    rc = serial_recv_cycle(context);
    if (OPPLE_SERIAL_NO_ERR == rc) {
        /* check list of wait serial ACK to remove node that is ACKED or timeout */

    }
    return OPPLE_SERIAL_NO_ERR;
}

#endif
