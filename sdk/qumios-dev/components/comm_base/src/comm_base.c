#include "comm_base.h"
#include "qm_work.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_ringbuf.h"
#include "qm_utils_list.h"
#include "qm_utils_timer.h"
#include "qm_event.h"

QM_EVENT_DEFINE_BASE(COMM_BASE_EVENT);

#define COMM_BASE_SUB_EVENT_DATA_RECV       0x0001
#define COMM_BASE_SUB_EVENT_DATA_SEND       0x0002
#define COMM_BASE_SUB_EVENT_DATA_ACK_SEND   0x0003

typedef struct {
    int send_count;
    uint32_t timeout;
}comm_base_send_info_t;

typedef struct {
    int cmd;
    int id;
    uint8_t *data;
    uint8_t len;
    uint8_t send_count;
    uint32_t timeout;
    qm_work_t handle;
}comm_base_record_info_t;

typedef struct {
    int cmd;
    comm_base_dir_t dir;
    unpack_fn unpack;
    pack_fn pack;
}comm_base_cmd_ops_t;

typedef struct {
    int cmd_num;
    comm_base_cmd_ops_t cmd_ops[CONFIG_COMM_BASE_CMD_NUM];
}comm_base_cmd_t;

typedef struct {
    recv_len_fn recv_len_get;
    recv_check_fn recv_check;
}comm_base_recv_ops_t;

typedef struct{
    int cur_step;
    int step_num;
    comm_base_recv_ops_t ops[CONFIG_COMM_BASE_RECV_STEP_MAX_NUM];
}comm_base_recv_t;

typedef struct {

    comm_base_param_t param;
    comm_base_cmd_t cmd;
    comm_base_recv_t recv;

    qm_utils_time_t recv_timer;
    int need_recv_len;
    int read_len;
    int recv_len;
    uint8_t recv_buf[CONFIG_COMM_BASE_RECV_BUF_MAX_LEN];

    qm_list_t *record_list;
}comm_base_ctx_t;

static comm_base_ctx_t g_comm_base_ctx = {0};


static int comm_base_recv_handler(uint8_t *data, int len);

static int serial_generic_send(uint8_t *data, int len, int send_count, uint32_t timeout);
static int serial_generic_ack_send(uint8_t *data, int len);
static int comm_base_generic_resend(comm_base_record_info_t *record_info);
static comm_base_record_info_t *comm_base_record_info_push(comm_base_record_info_t *record_info);
static void comm_base_generic_send_timeout(void *arg);
static comm_base_record_info_t* comm_base_record_info_find(int id);
static int comm_base_record_delete(comm_base_record_info_t *record_info);
static int comm_base_record_info_remove_by_id(int id);

static void comm_base_generic_send_timeout(void *arg)
{
    qm_err_t ret = QM_EOK;
    comm_base_event_info_t event_info = {0};
    comm_base_record_info_t *l_record_info = (comm_base_record_info_t*)arg;

    event_info.info.cmd = l_record_info->cmd;
    event_info.info.id = l_record_info->id;
    event_info.info.data = l_record_info->data;
    event_info.info.len = l_record_info->len;

    if(l_record_info->send_count == 0){
        event_info.event = COMM_BASE_EVENT_NO_ACK;
        g_comm_base_ctx.param.notify(&event_info);
        comm_base_record_info_remove_by_id(l_record_info->id);
    }else if(l_record_info->send_count > 0){
        event_info.event = COMM_BASE_EVENT_RESEND;
        g_comm_base_ctx.param.notify(&event_info);
        ret = comm_base_generic_resend(l_record_info);
        if(ret != QM_EOK){
            goto __exit;
        }
    }
    return;
    
__exit:
    comm_base_record_info_remove_by_id(l_record_info->id);
}

static int comm_base_generic_resend(comm_base_record_info_t *record_info)
{
    qm_err_t ret = QM_EOK;
    if(record_info == NULL){
        return -QM_EINVAL;
    }
    ret = qm_post_delayed_action(&record_info->handle, comm_base_generic_send_timeout, (void*)record_info, record_info->timeout);
    if(ret != QM_EOK){
        return ret;
    }
    g_comm_base_ctx.param.send(record_info->data, record_info->len);
    record_info->send_count--;
    return QM_EOK;
}

static int serial_generic_send(uint8_t *data, int len, int send_count, uint32_t timeout)
{
    qm_err_t ret = QM_EOK;
    comm_base_record_info_t *p_record_info = NULL;
    comm_base_record_info_t record_info = {0};
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }

    record_info.data = (uint8_t*)qm_malloc(len);
    if(record_info.data == NULL){
        ret = -QM_ENOMEM;
        goto __exit;
    }  
    memcpy(record_info.data, data, len);
    record_info.len = len;
    record_info.send_count = (uint8_t)send_count;
    record_info.timeout = timeout;

    record_info.id = g_comm_base_ctx.param.id_get(data, len);
    record_info.cmd = g_comm_base_ctx.param.cmd_get(data, len);
    
    p_record_info = comm_base_record_info_push(&record_info);
    if(p_record_info == NULL){
        goto __exit;
    }
    
    ret = qm_post_delayed_action(&p_record_info->handle, comm_base_generic_send_timeout, (void*)p_record_info, p_record_info->timeout);
    if(ret != QM_EOK){
        goto __exit;
    }   
    p_record_info->send_count--;
    g_comm_base_ctx.param.send(record_info.data, record_info.len);

    return QM_EOK;

__exit:
    if(record_info.data){
        qm_free(record_info.data);
        record_info.data = NULL;
    }
    qm_cancel_delayed_action(&record_info.handle);
    return ret;
}

static comm_base_record_info_t* comm_base_record_info_find(int id)
{
    qm_list_iterator_t *iter = NULL;
    qm_list_node_t *node = NULL;
    comm_base_record_info_t *l_record_info = NULL;

    if (NULL == (iter = qm_list_iterator_new(g_comm_base_ctx.record_list, LIST_HEAD))) {
        return NULL;
    }

    for (;;) {
        node = qm_list_iterator_next(iter);
        if (NULL == node) {
            break;
        }
        l_record_info = (comm_base_record_info_t*)qm_list_node_val_get(node);
        if (NULL == l_record_info) {
            continue;
        }
        if (l_record_info->id == id) {
            qm_list_iterator_destroy(iter);
            return l_record_info;
        } 
    }
    qm_list_iterator_destroy(iter);
    return NULL;
}

static int comm_base_record_info_remove_by_id(int id)
{
    qm_list_node_t *node = NULL;
    comm_base_record_info_t *l_record_info = NULL;

    l_record_info = comm_base_record_info_find(id);
    if(l_record_info == NULL){
        return -QM_EINVAL;
    }
    
    comm_base_record_delete(l_record_info);
    
    node = (qm_list_node_t*)((uint8_t*)l_record_info - sizeof(qm_list_node_t));
    qm_list_remove(g_comm_base_ctx.record_list, node);
    qm_free(node);
    node = NULL;
    
    return QM_EOK;
}

static int comm_base_record_delete(comm_base_record_info_t *record_info)
{
    if(record_info == NULL){
        return -QM_EINVAL;
    }
    
    qm_cancel_delayed_action(&record_info->handle);

    if(record_info->data){
        qm_free(record_info->data);
        record_info->data = NULL;
    }
    return QM_EOK;
}

static comm_base_record_info_t *comm_base_record_info_push(comm_base_record_info_t *record_info)
{
    qm_list_node_t *node = NULL;
    comm_base_record_info_t *l_record_info = NULL;
    node = qm_list_node_extra_new(sizeof(comm_base_record_info_t));
    if(node == NULL){
        return NULL;
    }
    l_record_info = (comm_base_record_info_t*)qm_list_node_val_get(node);
    memcpy(l_record_info, record_info, sizeof(comm_base_record_info_t));

    qm_list_rpush(g_comm_base_ctx.record_list, node);
    return l_record_info;
}

static int serial_generic_ack_send(uint8_t *data, int len)
{
    g_comm_base_ctx.param.send(data, len);
    return QM_EOK;
}

static void comm_base_event_callback(void *event_handler_arg, qm_event_base_t event_base,
                                     int32_t event_id, void *event_data, size_t event_data_size)
{
    comm_base_send_info_t *send_info = NULL;

    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);

    switch(event_id){

        case COMM_BASE_SUB_EVENT_DATA_RECV:

            comm_base_recv_handler(event_data, event_data_size);

        break;

        case COMM_BASE_SUB_EVENT_DATA_SEND:

            send_info = (comm_base_send_info_t*)event_data;

            serial_generic_send((uint8_t *)event_data + sizeof(comm_base_send_info_t),
                                event_data_size - sizeof(comm_base_send_info_t),
                                send_info->send_count, send_info->timeout);

        break;

        case COMM_BASE_SUB_EVENT_DATA_ACK_SEND:

            serial_generic_ack_send(event_data, event_data_size);

        break;

        default:
            break;
    }
}

static comm_base_cmd_ops_t *comm_base_cmd_ops_get(int cmd)
{
    int i = 0;
    int num = g_comm_base_ctx.cmd.cmd_num;
    for(i = 0; i < num; i++){
        if(g_comm_base_ctx.cmd.cmd_ops[i].cmd == cmd){
            return &g_comm_base_ctx.cmd.cmd_ops[i];
        }
    }
    return NULL;
}

static int comm_base_recv_handler(uint8_t *data, int len)
{
    int id = 0;
    int cmd = 0;
    qm_err_t ret = QM_EOK;
    int remain_len = 0;
    recv_len_fn recv_len_get = NULL;
    recv_check_fn recv_check = NULL;
    comm_base_cmd_ops_t *cmd_ops = NULL;

    if(g_comm_base_ctx.recv.cur_step && qm_utils_time_is_expired(&g_comm_base_ctx.recv_timer)){
        g_comm_base_ctx.recv.cur_step = 0;
        g_comm_base_ctx.read_len = 0;
        g_comm_base_ctx.recv_len = 0;
    }

    if(g_comm_base_ctx.recv_len + len > CONFIG_COMM_BASE_RECV_BUF_MAX_LEN){
        return -QM_EINVAL;
    }
    memcpy(g_comm_base_ctx.recv_buf + g_comm_base_ctx.recv_len, data, len);
    g_comm_base_ctx.recv_len += len;
    
    while(1){

        recv_len_get = g_comm_base_ctx.recv.ops[g_comm_base_ctx.recv.cur_step].recv_len_get;
        recv_check = g_comm_base_ctx.recv.ops[g_comm_base_ctx.recv.cur_step].recv_check;

        g_comm_base_ctx.need_recv_len = recv_len_get();

        remain_len = g_comm_base_ctx.recv_len - g_comm_base_ctx.read_len;
        if(g_comm_base_ctx.need_recv_len > remain_len){
            qm_utils_time_countdown_ms(&g_comm_base_ctx.recv_timer, g_comm_base_ctx.param.recv_timeout);
            break;
        }
        ret = recv_check(g_comm_base_ctx.recv_buf + g_comm_base_ctx.read_len, g_comm_base_ctx.need_recv_len);
        g_comm_base_ctx.read_len += g_comm_base_ctx.need_recv_len;
        if(ret != QM_EOK){
            goto __init;
        }

        g_comm_base_ctx.recv.cur_step++;

        if(g_comm_base_ctx.recv.cur_step == g_comm_base_ctx.recv.step_num){

            ret = g_comm_base_ctx.param.check(g_comm_base_ctx.recv_buf, g_comm_base_ctx.read_len);
            if(ret != QM_EOK){
                goto __init;
            }

            cmd = g_comm_base_ctx.param.cmd_get(g_comm_base_ctx.recv_buf, g_comm_base_ctx.read_len);
        
            cmd_ops = comm_base_cmd_ops_get(cmd);
            if(cmd_ops == NULL){
                ret = -QM_EINVAL;
                goto __init;
            }

            id = g_comm_base_ctx.param.id_get(g_comm_base_ctx.recv_buf, g_comm_base_ctx.read_len);

            if(cmd_ops->dir == COMM_BASE_DIR_DOWN){
                ret = comm_base_record_info_remove_by_id(id);
                if(ret != QM_EOK){
                    goto __init;
                }
            }

            ret = cmd_ops->unpack(g_comm_base_ctx.recv_buf, g_comm_base_ctx.read_len);
            if(ret != QM_EOK){
                goto __init;
            }
__init:
            remain_len = g_comm_base_ctx.recv_len - g_comm_base_ctx.read_len;
            memcpy(g_comm_base_ctx.recv_buf, g_comm_base_ctx.recv_buf + g_comm_base_ctx.read_len, remain_len);
            g_comm_base_ctx.recv_len = remain_len;
            g_comm_base_ctx.read_len = 0;
            g_comm_base_ctx.recv.cur_step = 0;
        }
    }

    return QM_EOK;
}

int comm_base_deinit(void)
{
    qm_err_t ret = QM_EOK;
    ret = qm_event_handler_unregister(COMM_BASE_EVENT, QM_EVENT_ANY_ID,
                                           comm_base_event_callback, NULL);
    if(ret != QM_EOK){
        return ret;
    }

    if(g_comm_base_ctx.record_list){
        qm_list_destroy(g_comm_base_ctx.record_list);
        g_comm_base_ctx.record_list = NULL;
    }

    return ret;
}

int comm_base_init(comm_base_param_t *param)
{
    qm_err_t ret = QM_EOK;

    ret = qm_event_handler_register(COMM_BASE_EVENT, QM_EVENT_ANY_ID,
                                         comm_base_event_callback, NULL);
    if(ret != QM_EOK){
        return ret;
    }

    g_comm_base_ctx.record_list = qm_list_new();
    if(g_comm_base_ctx.record_list == NULL){
        return -QM_ENOMEM;
    }
    memcpy(&g_comm_base_ctx.param, param, sizeof(comm_base_param_t));

    qm_utils_time_init(&g_comm_base_ctx.recv_timer);

    return QM_EOK;
}

int comm_base_data_push_from_isr(uint8_t *data, int len)
{
    qm_err_t ret = QM_EOK;
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }
    ret = qm_event_isr_post(COMM_BASE_EVENT, COMM_BASE_SUB_EVENT_DATA_RECV, data, (uint32_t)len);
    if(ret != QM_EOK){
        return ret;
    }
    return QM_EOK;
}

int comm_base_data_push(uint8_t *data, int len)
{
    qm_err_t ret = QM_EOK;
    if(data == NULL || len == 0){
        return -QM_EINVAL;
    }
    ret = qm_event_post(COMM_BASE_EVENT, COMM_BASE_SUB_EVENT_DATA_RECV, data, (uint32_t)len, QM_NO_WAIT);
    if(ret != QM_EOK){
        return ret;
    }
    return QM_EOK;
}

int comm_base_cmd_register(int cmd, comm_base_dir_t dir, pack_fn pack, unpack_fn unpack)
{
    int cmd_num = 0;
    comm_base_cmd_ops_t *cmd_ops = NULL;
    if(pack == NULL || unpack == NULL){
        return -QM_EINVAL;
    }

    if(dir != COMM_BASE_DIR_DOWN && dir != COMM_BASE_DIR_UP){
        return -QM_EINVAL;
    }

    if(g_comm_base_ctx.cmd.cmd_num >= CONFIG_COMM_BASE_CMD_NUM){
        return -QM_EFULL;
    }

    cmd_num = g_comm_base_ctx.cmd.cmd_num;
    cmd_ops = &g_comm_base_ctx.cmd.cmd_ops[cmd_num];
    
    cmd_ops->dir = dir;
    cmd_ops->cmd = cmd;
    cmd_ops->pack = pack;
    cmd_ops->unpack = unpack;
    g_comm_base_ctx.cmd.cmd_num++;
    return QM_EOK;
}

int comm_base_recv_register(int step, recv_len_fn recv_len, recv_check_fn recv_check)
{
    comm_base_recv_ops_t *recv_ops = NULL;
    if(recv_len == NULL || recv_check == NULL){
        return -QM_EINVAL;
    }

    if(step <= 0 || step > CONFIG_COMM_BASE_RECV_STEP_MAX_NUM){
        return -QM_EINVAL;
    }

    recv_ops = &g_comm_base_ctx.recv.ops[step-1];
    
    recv_ops->recv_check = recv_check;
    recv_ops->recv_len_get = recv_len;
    g_comm_base_ctx.recv.step_num++;

    return QM_EOK;
}

int comm_base_cmd_send(int cmd, int send_count, uint32_t timeout, void *arg)
{
    int len = 0;
    int buf_len = 0;
    uint8_t *buf = NULL;
    uint8_t *pack_buf = NULL;
    qm_err_t ret = QM_EOK;
    uint16_t sub_event = 0;
    comm_base_cmd_ops_t *cmd_ops = NULL;
    comm_base_send_info_t *send_info = NULL;

    if(send_count == 0 || timeout == 0){
        return -QM_EINVAL;
    }

    cmd_ops = comm_base_cmd_ops_get(cmd);
    if(cmd_ops == NULL){
        return -QM_EINVAL;
    }

    ret = cmd_ops->pack(NULL, &len, arg);
    if(ret != QM_EOK){
        return ret;
    }

    if(cmd_ops->dir == COMM_BASE_DIR_DOWN){
        buf_len = len + sizeof(comm_base_send_info_t);
    }else{
        buf_len = len;
    }

    buf = (uint8_t*)qm_malloc(buf_len);
    if(buf == NULL){
        return -QM_ENOMEM;
    }

    if(cmd_ops->dir == COMM_BASE_DIR_UP){
        pack_buf = buf;
        sub_event = COMM_BASE_SUB_EVENT_DATA_ACK_SEND;
    }else{
        send_info = (comm_base_send_info_t*)buf;
        pack_buf = (uint8_t *)(buf + sizeof(comm_base_send_info_t));
        sub_event = COMM_BASE_SUB_EVENT_DATA_SEND;
        send_info->timeout = timeout;
        send_info->send_count = send_count;
    }

    ret = cmd_ops->pack(pack_buf, &len, arg);
    if(ret != QM_EOK){
        goto __exit;
    }

    ret = qm_event_post(COMM_BASE_EVENT, sub_event, buf, (uint32_t)buf_len, QM_NO_WAIT);

__exit:
    if(buf){
        qm_free(buf);
        buf = NULL;
    }
    return ret;
}
