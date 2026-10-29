#include "transparent.h"
#include "qm_types.h"
#include "qm_errno.h"
#include "qm_kernel.h"
#include "qm_utils_list.h"
#include "qm_utils_timer.h"
#include "qm_gpio.h"
#include "qm_ringbuf.h"

#include "qm_log.h"

#define LOG_TAG "trans"

#define TRANSPARENT_MAX_PORT_NUM    30
#define TRANSPARENT_BUF_SIZE        128

#define TRANSPARENT_SEND_INTERVAL   50
#define TRANSPARENT_RECV_TIMEOUT    500

#define CONFIG_TRANSPAENT_TASK_PRO  20
#define CONFIG_TRANSPAENT_TASK_SIZE 4096

typedef enum{
    TRANSPARENT_PARSE_STATE_NONE,
    TRANSPARENT_PARSE_STATE_HEAD,
    TRANSPARENT_PARSE_STATE_VER,
    TRANSPARENT_PARSE_STATE_LEN,
    TRANSPARENT_PARSE_STATE_DATA,
}transparent_parse_state_t;

typedef struct 
{
    uint8_t is_use;
    uint16_t port; 
    uint8_t *buff;
    qm_sem_t rx_sem;
    qm_mutex_t rx_mutex;
    qm_ringbuf_t rx_ringbuff;
}transparent_ringbuff_t;

typedef struct {

    qm_mutex_t mutex;

    qm_task_t rx_task;
    uint8_t rx_task_run;

    qm_task_t tx_task;
    uint8_t tx_task_run;

    uint8_t *read_buf;
    uint8_t *pread_buf;
    uint8_t *ext_pread_buf;
    uint32_t read_buf_size;
    uint16_t read_pos;
    uint16_t need_len;
   
    qm_sem_t tx_sem;
    qm_list_t *tx_list; 
    qm_mutex_t tx_mutex;

    transparent_ringbuff_t ringbuff[TRANSPARENT_MAX_PORT_NUM];

    transparent_parse_state_t parse_state;
    int32_t (*write)(void *handle, void *data, uint32_t len);
    int32_t (*readtimeout)(void *handle, void *data, uint32_t len, uint32_t timeout);
    void *handle;
}transparent_ctx_t;

static void transparent_tx_thread(void *arg);
static void transparent_rx_thread(void *arg);
static uint16_t get_packet_len(uint8_t *packet_buffer);
static uint16_t get_packet_port(uint8_t *packet_buffer);

static int32_t transparent_cfg_port_itself_channel(transparent_port_t *port);
static void frame_head_init(uint8_t *buffer, transparent_port_t *port, uint16_t len);
static transparent_ringbuff_t *transparent_ringbuf_index_get(void *handle , uint16_t port);
static transparent_ringbuff_t *transparent_ringbuf_free_get(void *handle , uint16_t port);
static int32_t transparent_parse(transparent_ctx_t *transparent_ctx, uint8_t *data, uint16_t len);
static int32_t transparent_recv_cycle(transparent_ctx_t *transparent_ctx);


static uint16_t get_packet_port(uint8_t *packet_buffer)
{
    return (packet_buffer[3] << 8) + (packet_buffer[4]);
}

static uint16_t get_packet_len(uint8_t *packet_buffer)
{
    return  (packet_buffer[5] << 8) + (packet_buffer[6]);
}

static transparent_ringbuff_t *transparent_ringbuf_free_get(void *handle , uint16_t port)
{
    int index = 0;
    qm_err_t ret = QM_EOK;
    transparent_ringbuff_t *ringbuff = NULL;
    transparent_ctx_t *transparent_ctx = NULL;

    if((handle == NULL)){
        return NULL;
    }

    transparent_ctx = (transparent_ctx_t *)handle;

    qm_mutex_lock(&transparent_ctx->mutex, QM_WAIT_FOREVER); 
    for(index = 0; index < TRANSPARENT_MAX_PORT_NUM; index++){
        ringbuff = &transparent_ctx->ringbuff[index];
        if(!ringbuff->is_use){
            continue;
        }

        if(ringbuff->port == port){
            qm_mutex_unlock(&transparent_ctx->mutex);
            return ringbuff;
        }
    }
    qm_mutex_unlock(&transparent_ctx->mutex);
    
    qm_mutex_lock(&transparent_ctx->mutex, QM_WAIT_FOREVER); 
    for(index = 0; index < TRANSPARENT_MAX_PORT_NUM; index++){
        ringbuff = &transparent_ctx->ringbuff[index];
        if(ringbuff->is_use){
            continue;
        }

        ret = qm_mutex_new(&ringbuff->rx_mutex);
        if (ret != QM_EOK){
            goto _error;
        }

        ret = qm_sem_new(&ringbuff->rx_sem, 0);
        if (ret != QM_EOK){
            goto _error;
        }

        ringbuff->buff = (uint8_t *)qm_malloc(TRANSPARENT_BUF_SIZE);
        if(ringbuff->buff == NULL){
            goto _error;
        }

        ringbuff->port = port;
        ringbuff->is_use = QM_TRUE;
        memset(ringbuff->buff, 0, TRANSPARENT_BUF_SIZE);
        qm_ringbuf_init(&ringbuff->rx_ringbuff, ringbuff->buff, TRANSPARENT_BUF_SIZE);

        qm_mutex_unlock(&transparent_ctx->mutex);
        return ringbuff;
    }

    qm_mutex_unlock(&transparent_ctx->mutex);
    
    return NULL;

_error:
    ringbuff = &transparent_ctx->ringbuff[index];

    qm_mutex_lock(&transparent_ctx->mutex, QM_WAIT_FOREVER); 
    ringbuff->is_use = QM_FALSE;
    if(qm_mutex_is_valid(&ringbuff->rx_mutex)){
        qm_mutex_free(&ringbuff->rx_mutex);
    }

    if(qm_sem_is_valid(&ringbuff->rx_sem)){
        qm_sem_free(&ringbuff->rx_sem);
    }

    qm_ringbuf_flush(&ringbuff->rx_ringbuff);
    if(ringbuff->buff){
        qm_free(ringbuff->buff);
        ringbuff->buff = NULL;
    }
    qm_mutex_unlock(&transparent_ctx->mutex);

    return NULL;    
}

static transparent_ringbuff_t *transparent_ringbuf_index_get(void *handle , uint16_t port)
{
    transparent_ringbuff_t *ringbuff = NULL;
    transparent_ctx_t *transparent_ctx = NULL;

    if((handle == NULL)){
        return NULL;
    }

    transparent_ctx = (transparent_ctx_t *)handle;
    
    qm_mutex_lock(&transparent_ctx->mutex, QM_WAIT_FOREVER); 
    for(int i = 0; i < TRANSPARENT_MAX_PORT_NUM; i++){
        ringbuff = &transparent_ctx->ringbuff[i];
        if(!ringbuff->is_use){
            continue;
        }

        if(ringbuff->port == port){
            qm_mutex_unlock(&transparent_ctx->mutex);
            return ringbuff;
        }
    }
    qm_mutex_unlock(&transparent_ctx->mutex);

    return NULL;
}

static void frame_head_init(uint8_t *buffer, transparent_port_t *port, uint16_t len)
{
    transparent_header_t *head = (transparent_header_t *)buffer;

    head->head = cpu_to_be16(TRANSPARENT_HEADER);
    head->ver  = TRANSPARENT_VER;
    head->len  = cpu_to_be16(len);
    memcpy(&head->port, &port->vaule, sizeof(uint16_t));
    head->port = cpu_to_be16(head->port);
}

int32_t transparent_write(void *handle, transparent_port_t *port, uint8_t *data, uint32_t len)
{
    uint8_t *buf = NULL;
    uint16_t raw_len = 0;
    qm_list_node_t *node = NULL;
    transparent_data_info_t *info = NULL;
    transparent_ctx_t *transparent_ctx = (transparent_ctx_t *)handle;
    if(handle == NULL || port == NULL || data == NULL || len == 0){
        return -QM_EINVAL;
    }
    
    raw_len = sizeof(qm_list_node_t) + sizeof(transparent_data_info_t) + len - 1;
    buf = qm_malloc(raw_len);
    if(buf == NULL){
        return -QM_ENOMEM;
    } 

    memset(buf, 0, raw_len);
    node = (qm_list_node_t *)buf;
    qm_list_node_init(node, buf + sizeof(qm_list_node_t));
    buf = buf + sizeof(qm_list_node_t);

    frame_head_init(buf, port, len);
    info = (transparent_data_info_t *)buf;
    memcpy(info->payload, data, len);

    qm_mutex_lock(&transparent_ctx->tx_mutex, QM_WAIT_FOREVER);
    qm_list_rpush(transparent_ctx->tx_list, node);
    qm_mutex_unlock(&transparent_ctx->tx_mutex);
    
    qm_sem_signal(&transparent_ctx->tx_sem);
    return (len + sizeof(transparent_header_t));
}

static int32_t transparent_cfg_port_itself_channel(transparent_port_t *port)
{
    if(port == NULL){
        return -QM_EINVAL;
    }

    if (port->channel.port5 != TRANSPARENT_INVALUE_PORT){
        port->channel.port5 = TRANSPARENT_DEVICE_ITSELF_PORT;
    }else if (port->channel.port4 != TRANSPARENT_INVALUE_PORT){
        port->channel.port4 = TRANSPARENT_DEVICE_ITSELF_PORT;
    }else if (port->channel.port3 != TRANSPARENT_INVALUE_PORT){
        port->channel.port3 = TRANSPARENT_DEVICE_ITSELF_PORT;
    }else if(port->channel.port2 != TRANSPARENT_INVALUE_PORT){
        port->channel.port2 = TRANSPARENT_DEVICE_ITSELF_PORT;
    }else if(port->channel.port1 != TRANSPARENT_INVALUE_PORT){
        port->channel.port1 = TRANSPARENT_DEVICE_ITSELF_PORT;
    } 

    return QM_EOK;
}

int32_t transparent_write_cfg(void *handle, transparent_port_t *port, uint8_t *data, uint32_t len)
{
    if(handle == NULL || port == NULL || data == NULL || len == 0){
        return -QM_EINVAL;
    }

    transparent_cfg_port_itself_channel(port);
    return transparent_write(handle, port, data, len);
}

int32_t transparent_read_cfg(void *handle,  transparent_port_t *port, uint8_t *data, uint32_t len, uint32_t timeout)
{
    if(handle == NULL || port == NULL || data == NULL || len == 0){
        return -QM_EINVAL;
    }

    transparent_cfg_port_itself_channel(port);
    return transparent_read(handle, port, data, len, timeout); 
}

int32_t transparent_read(void *handle, transparent_port_t *port, uint8_t *data, uint32_t len, uint32_t timeout)
{
    int isempty = 0;
    int temp_len = 0;
    int recv_len = 0;
    uint32_t t_left = 0;
    qm_err_t ret = QM_EOK;
    uint16_t ext_port = 0;
    qm_utils_time_t time = {0};
    transparent_ringbuff_t *ringbuff = NULL; 
    transparent_ctx_t *transparent_ctx = (transparent_ctx_t *)handle;

    if((handle == NULL) || port == NULL || (data == NULL) || (len == 0)){
        return -QM_EINVAL;
    }
    
    if(timeout >= (UINT32_MAX / 2)){
        timeout = (UINT32_MAX / 2);
    }

    memcpy(&ext_port, &port->vaule, sizeof(uint16_t));
    ringbuff = transparent_ringbuf_free_get(transparent_ctx, ext_port);
    if(ringbuff == NULL){
        return -QM_EINVAL;
    }

    qm_utils_time_init(&time);
    qm_utils_time_countdown_ms(&time, timeout);   
    do{
        t_left = qm_utils_time_left(&time);

        qm_mutex_lock(&ringbuff->rx_mutex, QM_WAIT_FOREVER);
        isempty = qm_ringbuf_isempty(&ringbuff->rx_ringbuff);
        qm_mutex_unlock(&ringbuff->rx_mutex);
        if(!isempty){
            qm_mutex_lock(&ringbuff->rx_mutex, QM_WAIT_FOREVER);
            temp_len = qm_ringbuf_pop(&ringbuff->rx_ringbuff, data + recv_len, len - recv_len);
            qm_mutex_unlock(&ringbuff->rx_mutex);
            if(temp_len > 0){
                recv_len += temp_len;
            }else{
                ret = -QM_ERROR;
                break;
            }
        }else{
            if(t_left == 0){
                break;
            }
            ret = qm_sem_wait(&ringbuff->rx_sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }            
    }while ((recv_len < len) && (qm_utils_time_left(&time) > 0));

    return (0 != recv_len) ? recv_len : ret;
}

static void transparent_rx_thread(void *arg)
{
    transparent_ctx_t *transparent_ctx = (transparent_ctx_t *)arg;

    while (transparent_ctx->rx_task_run)
    {
        transparent_recv_cycle(transparent_ctx);
    }
    qm_task_exit(&transparent_ctx->rx_task);
}

static void transparent_tx_thread(void *arg)
{
    uint16_t send_len = 0;
    uint16_t list_len = 0;
    qm_list_node_t *node = NULL;
    transparent_data_info_t *info = NULL;
    transparent_ctx_t *transparent_ctx = (transparent_ctx_t *)arg;

    while (transparent_ctx->tx_task_run)
    {
        qm_sem_wait(&transparent_ctx->tx_sem, QM_WAIT_FOREVER);
      
        qm_mutex_lock(&transparent_ctx->tx_mutex, QM_WAIT_FOREVER);
        list_len = qm_list_len_get(transparent_ctx->tx_list);
        qm_mutex_unlock(&transparent_ctx->tx_mutex);

        if(list_len == 0){
            continue;
        }

        qm_mutex_lock(&transparent_ctx->tx_mutex, QM_WAIT_FOREVER);
        node = qm_list_lpop(transparent_ctx->tx_list);
        list_len = qm_list_len_get(transparent_ctx->tx_list);
        qm_mutex_unlock(&transparent_ctx->tx_mutex); 
        if(node){
            info = (transparent_data_info_t *)qm_list_node_val_get(node);
            if(transparent_ctx->write){
                send_len = sizeof(transparent_header_t) + cpu_to_be16(info->header.len);
                QM_HEX_LOGD(LOG_TAG,"TRANS_UART SEND: ", ((uint8_t *)info), send_len);
                transparent_ctx->write(transparent_ctx->handle, info, send_len);
            }      
            qm_free(node);
            node = NULL;
        }
        if(list_len > 0){
            qm_sem_signal(&transparent_ctx->tx_sem);
        }
    }
    qm_task_exit(&transparent_ctx->tx_task);
}

static int32_t transparent_parse(transparent_ctx_t *transparent_ctx, uint8_t *data, uint16_t len)
{
    uint16_t tmp_len = 0;
    uint16_t tmp_port = 0;
    transparent_ringbuff_t *transparent_ringbuff = NULL; 
    transparent_data_info_t *transparent_data_info = NULL;
    
    transparent_data_info = qm_malloc(sizeof(transparent_data_info_t) + len - 1);
    if(transparent_data_info == NULL){
        return -QM_ENOMEM;
    }

    memcpy(&transparent_data_info->header, data, sizeof(transparent_header_t));
    memcpy(transparent_data_info->payload, data + sizeof(transparent_header_t), len);

    tmp_port = get_packet_port(data);
    tmp_len = cpu_to_be16(transparent_data_info->header.len);
    transparent_ringbuff = transparent_ringbuf_free_get(transparent_ctx, tmp_port);
    if(transparent_ringbuff == NULL){
        qm_free(transparent_data_info);
        return -QM_ENOMEM;
    }

    qm_mutex_lock(&transparent_ringbuff->rx_mutex, QM_WAIT_FOREVER);
    qm_ringbuf_push(&transparent_ringbuff->rx_ringbuff, transparent_data_info->payload, tmp_len);
    qm_mutex_unlock(&transparent_ringbuff->rx_mutex);

    qm_sem_signal(&transparent_ringbuff->rx_sem);
    qm_free(transparent_data_info);
    transparent_data_info = NULL;
    return QM_EOK;
}

static int32_t transparent_recv_cycle(transparent_ctx_t *transparent_ctx)
{
    int ret = QM_EOK;
    int recv_len = 0;
    uint16_t packet_len  = 0;
    uint16_t need_len = transparent_ctx->need_len;

    recv_len = transparent_ctx->readtimeout(transparent_ctx->handle, transparent_ctx->read_buf + transparent_ctx->read_pos, need_len, TRANSPARENT_RECV_TIMEOUT);
    if(recv_len == 0){
        return 0;
    }

    transparent_ctx->read_pos += recv_len;
    switch(transparent_ctx->parse_state){

        case TRANSPARENT_PARSE_STATE_NONE:
            if(transparent_ctx->read_buf[transparent_ctx->read_pos - 1] != TRANSPARENT_HEADER_PART){
                goto clear;
            }else{
                transparent_ctx->parse_state = TRANSPARENT_PARSE_STATE_HEAD;
                transparent_ctx->need_len = 1;
            }

        break;

        case TRANSPARENT_PARSE_STATE_HEAD:
            if(transparent_ctx->need_len <= recv_len){
                if(transparent_ctx->read_buf[transparent_ctx->read_pos - 1] != TRANSPARENT_HEADER_PART){
                    goto clear;
                }else{
                    transparent_ctx->parse_state = TRANSPARENT_PARSE_STATE_VER;
                    transparent_ctx->need_len = 1;
                }
            }else{
                transparent_ctx->need_len = transparent_ctx->need_len - recv_len;
            }

        break;
        case TRANSPARENT_PARSE_STATE_VER:
            if(transparent_ctx->need_len <= recv_len){
                if(transparent_ctx->read_buf[transparent_ctx->read_pos - 1] != TRANSPARENT_VER){
                    goto clear;
                }else{
                    transparent_ctx->parse_state = TRANSPARENT_PARSE_STATE_LEN;
                    transparent_ctx->need_len = 4;
                }
            }else{
                transparent_ctx->need_len = transparent_ctx->need_len - recv_len;
            }

        break;

        case TRANSPARENT_PARSE_STATE_LEN:

            if(transparent_ctx->need_len <= recv_len){
                packet_len = get_packet_len(transparent_ctx->read_buf);
                if(packet_len > (transparent_ctx->read_buf_size - transparent_ctx->read_pos)){
                    goto clear;
                }
                transparent_ctx->parse_state = TRANSPARENT_PARSE_STATE_DATA;
                transparent_ctx->need_len = packet_len;
            }else{
                transparent_ctx->need_len = transparent_ctx->need_len - recv_len;
            }

        break;

        case TRANSPARENT_PARSE_STATE_DATA:

            if(transparent_ctx->need_len <= recv_len){
                QM_HEX_LOGD(LOG_TAG,"TRANS_UART RECV: ",transparent_ctx->read_buf, transparent_ctx->read_pos);
                ret = transparent_parse(transparent_ctx, transparent_ctx->read_buf, transparent_ctx->read_pos);
                goto clear;
            }else{
                transparent_ctx->need_len = transparent_ctx->need_len - recv_len;
            }

        break;

        default:
            break;
    }

    return ret;

clear:
    transparent_ctx->read_pos = 0;
    transparent_ctx->need_len = 1;
    transparent_ctx->read_buf = transparent_ctx->pread_buf;
    transparent_ctx->parse_state = TRANSPARENT_PARSE_STATE_NONE;
    memset(transparent_ctx->read_buf, 0, transparent_ctx->read_buf_size);
    return ret;
}


int32_t transparent_clear(void *handle, transparent_port_t *port)
{
    uint16_t ext_port = 0;
    transparent_ringbuff_t *ringbuff = NULL; 
    transparent_ctx_t *transparent_ctx = (transparent_ctx_t *)handle;

    if(handle == NULL || port == NULL){
        return -QM_EINVAL;
    }

    memcpy(&ext_port, port, sizeof(uint16_t));
    ringbuff = transparent_ringbuf_index_get(transparent_ctx, ext_port);
    if(ringbuff == NULL){
        return -QM_EINVAL;
    }    

    qm_mutex_lock(&ringbuff->rx_mutex, QM_WAIT_FOREVER);
    qm_ringbuf_flush(&ringbuff->rx_ringbuff);
    qm_mutex_unlock(&ringbuff->rx_mutex);

    return QM_EOK;
}
transparent_port_t *transparent_port_new(void)
{
    int i = 0;
    uint16_t value = TRANSPARENT_INVALUE_PORT;
    transparent_port_t *lport = NULL;
    lport = (transparent_port_t*)qm_malloc(sizeof(transparent_port_t));
    if(lport == NULL){
        return NULL;
    }
    memset(lport, 0, sizeof(transparent_port_t));

    for(i = 0; i < TRANSPARENT_MAX_DEEPTH; i++){
        lport->vaule |= value << (3 * i);
    }
    return lport;
}

int transparent_port_free(transparent_port_t *port)
{
    if(port == NULL){
        return -QM_EINVAL;
    }
    qm_free(port);
    return QM_EOK;
}

int transparent_port_add(transparent_port_t *port, uint16_t sub_port)
{
    int i = 0;
    uint8_t mask = TRANSPARENT_INVALUE_PORT;
    if(port == NULL){
        return -QM_EINVAL;
    }

    for(i = 0; i < TRANSPARENT_MAX_DEEPTH; i++){
        if(((port->vaule >> (3 * i)) & mask) == TRANSPARENT_INVALUE_PORT){
            port->vaule &= (~(mask << (3 * i)));
            port->vaule |= (sub_port << (3 * i));
            break;
        }
    }

    return QM_EOK;
}

int32_t transparent_deinit(void *handle)
{
    transparent_ringbuff_t *ringbuff = NULL; 
    transparent_ctx_t *transparent_ctx = (transparent_ctx_t *)handle;

    if(handle == NULL){
        return -QM_EINVAL;
    }

    transparent_ctx->rx_task_run = QM_FALSE;
    transparent_ctx->tx_task_run = QM_FALSE;

    if(transparent_ctx->tx_list){
        qm_list_destroy(transparent_ctx->tx_list);
        transparent_ctx->tx_list = NULL;
    }

    if(qm_sem_is_valid(&transparent_ctx->tx_sem)){
        qm_sem_free(&transparent_ctx->tx_sem);
    }

    if(qm_mutex_is_valid(&transparent_ctx->tx_mutex)){
        qm_mutex_free(&transparent_ctx->tx_mutex);
    }

    for(int i = 0; i < TRANSPARENT_MAX_PORT_NUM; i++){
        ringbuff = &transparent_ctx->ringbuff[i];

        ringbuff->port = 0;
        ringbuff->is_use = QM_FALSE;
        if(qm_mutex_is_valid(&ringbuff->rx_mutex)){
            qm_mutex_free(&ringbuff->rx_mutex);
        }

        if(qm_sem_is_valid(&ringbuff->rx_sem)){
            qm_sem_free(&ringbuff->rx_sem);
        }

        qm_ringbuf_flush(&ringbuff->rx_ringbuff);
        if(ringbuff->buff){
            qm_free(ringbuff->buff);
            ringbuff->buff = NULL;
        }
    }

    if(transparent_ctx->pread_buf){
        qm_free(transparent_ctx->pread_buf);
        transparent_ctx->pread_buf = NULL;
    }

    if (transparent_ctx){
        qm_free(transparent_ctx);
        transparent_ctx = NULL;
    }    

    return QM_EOK;
}
void *transparent_init(transparent_param_t *param)
{
    qm_err_t ret = QM_EOK;
    transparent_ctx_t *transparent_ctx = NULL;

    if(param == NULL){
        return NULL;
    }

    transparent_ctx = (transparent_ctx_t *)qm_malloc(sizeof(transparent_ctx_t));
    if(transparent_ctx == NULL){
        return NULL;
    }
    
    memset(transparent_ctx, 0, sizeof(transparent_ctx_t));
    transparent_ctx->write       = param->write;
    transparent_ctx->readtimeout = param->readtimeout;
    transparent_ctx->handle = param->handle;
    transparent_ctx->parse_state = TRANSPARENT_PARSE_STATE_NONE;
    transparent_ctx->read_pos  = 0;
    transparent_ctx->need_len  = 1;
    transparent_ctx->read_buf_size = TRANSPARENT_BUF_SIZE;
    transparent_ctx->pread_buf = (uint8_t *)qm_malloc(TRANSPARENT_BUF_SIZE);
    memset(transparent_ctx->pread_buf, 0, TRANSPARENT_BUF_SIZE);
    if(transparent_ctx->pread_buf == NULL){
        goto _error;
    }

    ret = qm_mutex_new(&transparent_ctx->mutex);
    if (ret != QM_EOK){
        goto _error;
    }

    ret = qm_sem_new(&transparent_ctx->tx_sem, 0);
    if (ret != QM_EOK){
        goto _error;
    }

    ret = qm_mutex_new(&transparent_ctx->tx_mutex);
    if (ret != QM_EOK){
        goto _error;
    }

    transparent_ctx->tx_list = qm_list_new();
    if(transparent_ctx->tx_list == NULL){
        goto _error;
    }

    memset(&transparent_ctx->ringbuff[0],0, QM_ARRAY_SIZE(transparent_ctx->ringbuff));

    transparent_ctx->read_buf = transparent_ctx->pread_buf;
    
    transparent_ctx->tx_task_run =  QM_TRUE;
    qm_task_new(&transparent_ctx->tx_task, "trans tx", transparent_tx_thread, (void*)transparent_ctx, CONFIG_TRANSPAENT_TASK_SIZE, CONFIG_TRANSPAENT_TASK_PRO);
    
    transparent_ctx->rx_task_run =  QM_TRUE;
    qm_task_new(&transparent_ctx->rx_task, "trans rx", transparent_rx_thread, (void*)transparent_ctx, CONFIG_TRANSPAENT_TASK_SIZE, CONFIG_TRANSPAENT_TASK_PRO);

    return (void*)transparent_ctx;

_error:
    transparent_deinit((void*)transparent_ctx);
    return NULL;
}