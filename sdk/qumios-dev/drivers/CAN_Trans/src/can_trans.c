#include "qm.h"
#include "qm_work.h"
#include "can_trans.h"
#include "qm_utils_list.h"
#include "qm_utils_timer.h"
#include "qm_ringbuf.h"
#include "data_channel.h"
#include "qm_gpio.h"

#define LOG_TAG "can_trans"

/**
 * Event base used by CAN_Trans internal dispatch.
 */
QM_EVENT_DEFINE_BASE(CAN_TRANS_EVENT);

#define CAN_TRANS_TIMEOUT_MS    (50)

#define CAN_TRANS_AT_TIMEOUT_MS   (200)

#define CAN_TRANS_PACKET_ID_SIZE    (0x7F)

#define CAN_TRANS_DATA_SIZE         (8)
#define CAN_TRANS_HEAD_SIZE         (1)
#define CAN_TRANS_HEAD_EXT_SIZE     (2)

typedef struct
{
#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
    qm_sem_t rx_sem;
    qm_mutex_t rx_mutex;
#endif
    can_trans_channel_t tran_channel;

    uint8_t *data;
    uint8_t *pdata;
    uint16_t buff_len;
    uint8_t  sta_data[CONFIG_CAN_TRANS_RECV_BUFF_LEN];

    uint16_t data_size;

    qm_ringbuf_t ringbuf;
    uint16_t packet_num;
}can_trans_rx_node_t;

typedef struct
{
    uint8_t data_size;
    uint8_t long_packet;
    uint16_t packet_num;
    can_trans_channel_t tran_channel;
    uint8_t pdata[CAN_TRANS_DATA_SIZE];
}can_trans_tx_node_t;

typedef struct
{
    uint8_t     is_use;
    qm_gpio_dev_t io_pins[CAN_TRANS_ADDR_IO_NUM];
}can_trans_addr_io_pin_t;

typedef struct 
{
    qm_at_server_t *at_handle;
    qm_can_dev_t can_dev;

    can_trans_addr_t device_addr;

    qm_mutex_t rx_mutex;
    qm_list_t rx_list;

    uint8_t tx_busy;
    qm_list_t tx_list;
    qm_mutex_t tx_mutex;

    qm_task_t rx_task;

    can_trans_recv_fn recv_fn;

    void *dc_client;
    void *dc_server;
    
}can_trans_ctx_t;

static can_trans_ctx_t *g_tran_ctx = NULL;
static can_trans_addr_io_pin_t g_tran_addr_pins = {0};  

static int can_at_send(void *handle, char *buf, int size)
{
    return data_channel_write(g_tran_ctx->dc_server, (uint8_t *)buf, size, 0);
}

static int can_at_recv(void *handle, char *buf, int size, int timeout)
{
    return data_channel_read(g_tran_ctx->dc_server, (uint8_t*)buf, size, timeout);
}

static int list_match(void *a, void *b)
{
    can_trans_channel_t *a_handle = (can_trans_channel_t*)a;
    can_trans_rx_node_t *b_handle = (can_trans_rx_node_t*)b;

    if(!memcmp(&b_handle->tran_channel, a_handle, sizeof(can_trans_channel_t))){
        return QM_TRUE;
    }
    return QM_FALSE;
}

static int can_trans_identifier_set(uint32_t *identifier,can_trans_channel_t *channel)
{
    uint32_t tmp_identifier = 0;
    can_trans_addr_t *trans_addr = NULL;

    QM_RETURN_ON_FALSE(channel, -QM_EINVAL, LOG_TAG, "channel inval !!");
    QM_RETURN_ON_FALSE(identifier, -QM_EINVAL, LOG_TAG, "identifier inval !!");

    trans_addr = &channel->tran_dst_addr;
    tmp_identifier = ((trans_addr->dev_addr) | (trans_addr->dev_type << 4));
    
    trans_addr = &channel->tran_src_addr;

    tmp_identifier = ((tmp_identifier << 8) | (trans_addr->dev_addr) | (trans_addr->dev_type << 4));

    tmp_identifier = ((tmp_identifier << 8) | channel->dev_channel);
    
    *identifier = tmp_identifier;

    return QM_EOK;
}

static int can_trans_recv_port_get(uint32_t identifier, can_trans_channel_t *channel)
{
    can_trans_addr_t *trans_addr = NULL;

    QM_RETURN_ON_FALSE(channel, -QM_EINVAL, LOG_TAG, "channel inval !!");
    
    channel->dev_channel = identifier & 0xFF;

    trans_addr = &channel->tran_src_addr;

    identifier = identifier >> 8;
    trans_addr->dev_addr = (identifier) & 0x0F;
    trans_addr->dev_type = (identifier >> 4) & 0x0F;

    identifier = identifier >> 8;
    trans_addr = &channel->tran_dst_addr;
    trans_addr->dev_addr = (identifier) & 0x0F;
    trans_addr->dev_type = (identifier >> 4) & 0x0F;

    return QM_EOK;
}

static int can_trans_recv_data_creat(can_trans_rx_node_t *rx_node, qm_can_message_t *rx_message)
{
    uint8_t data_index = 1;
    uint16_t packet_id  = 0;
    uint32_t recv_len = 0;

    QM_RETURN_ON_FALSE(rx_node, -QM_EINVAL, LOG_TAG, "rx node inval");
    QM_RETURN_ON_FALSE(rx_message, -QM_EINVAL, LOG_TAG, "rx_message inval");
    
    packet_id = (rx_message->data[0] & CAN_TRANS_PACKET_ID_SIZE);

    if(((rx_message->data[0] >> 7) & 0xFF)){
        packet_id = (packet_id << 8) | rx_message->data[1];
        data_index++;
    }

    recv_len = ( (packet_id + 1) * (CAN_TRANS_DATA_SIZE - data_index));

    if(recv_len > CONFIG_CAN_TRANS_RECV_BUFF_LEN && rx_node->buff_len < recv_len){
    
        if(rx_node->pdata){
            qm_free(rx_node->pdata);
        }

        rx_node->pdata = (uint8_t *)qm_malloc(recv_len);
        if(rx_node->pdata == NULL){
            return -QM_EINVAL;
        }
        memset(rx_node->pdata, 0, recv_len);
        
        rx_node->buff_len  = recv_len;
        rx_node->data = rx_node->pdata;

        qm_ringbuf_init(&rx_node->ringbuf, rx_node->data, recv_len);
    }

    rx_node->packet_num = packet_id + 1;
    return QM_EOK;
}

static qm_list_node_t *can_trans_recv_node_creat(can_trans_channel_t trans_channel)
{

    qm_list_node_t *node = NULL;
    can_trans_rx_node_t *rx_node = NULL;

    node = qm_list_node_extra_new(sizeof(can_trans_rx_node_t));
    if(node == NULL){
        return NULL;
    }
    rx_node = (can_trans_rx_node_t *)qm_list_node_val_get(node);

#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
    qm_sem_new(&rx_node->rx_sem, 0);
    qm_mutex_new(&rx_node->rx_mutex);
#endif

    rx_node->data = rx_node->sta_data;
    rx_node->buff_len = CONFIG_CAN_TRANS_RECV_BUFF_LEN;
    memcpy(&rx_node->tran_channel, &trans_channel, sizeof(can_trans_channel_t));
    
    qm_ringbuf_init(&rx_node->ringbuf, rx_node->data, CONFIG_CAN_TRANS_RECV_BUFF_LEN);

    return node;
}

static void handle_prod_can(uint8_t channel, uint8_t *data, int len)
{
    int ret = QM_EOK;
    uint8_t restart_count = 5;
    uint32_t read_len = 0;
    uint32_t client_recv_len= 0;
    uint8_t ch = 0, last_ch = 0;
    uint8_t str_cmd[CONFIG_CAN_TRANS_RECV_BUFF_LEN] = {0};
    uint8_t client_recv_cmd[CONFIG_CAN_TRANS_RECV_BUFF_LEN] = {0};

    can_trans_channel_t trans_channel = {
        .dev_channel = channel,

        .tran_src_addr = {
            .dev_type = g_tran_ctx->device_addr.dev_type,
            .dev_addr = g_tran_ctx->device_addr.dev_addr,
        },

        .tran_dst_addr = {
            .dev_type = CAN_TRANS_DEV_BOX,
            .dev_addr = 0,
        },
    };

    QM_LOGD(LOG_TAG, "enter mem_free %d exec_cmd %s", qm_free_mem_get(), data);

    data_channel_write(g_tran_ctx->dc_client, data, len, 0);

    while(1){
        ch = 0;
        last_ch = 0;
        read_len = 0;
        memset(str_cmd, 0, CONFIG_CAN_TRANS_RECV_BUFF_LEN);
        while (1)
        {
            ret = data_channel_read(g_tran_ctx->dc_client, &ch, 1, CAN_TRANS_AT_TIMEOUT_MS);
            if(ret <= 0){
                if(restart_count--){
                    continue;
                }
                goto _next;
            }

            restart_count = 3;
            
            if (read_len <= CONFIG_CAN_TRANS_RECV_BUFF_LEN - 2){
                str_cmd[read_len++] = ch;
            }

            if ((ch == '\n' && last_ch == '\r')){
                break;
            }

            last_ch = ch;
        }

        if(client_recv_len + read_len < CONFIG_CAN_TRANS_RECV_BUFF_LEN){
            memcpy(client_recv_cmd + client_recv_len, str_cmd, read_len);
            client_recv_len += read_len;
        } 

        if(memcmp(str_cmd, "OK\r\n", strlen("OK\r\n")) == 0){
            ret = QM_EOK;
            break;
        }else if(strstr((const char *)str_cmd, "ERROR\r\n")){
            ret = -QM_ERROR;
            break;
        }  
    }

_next:

    if(!client_recv_len){
        QM_LOGW(LOG_TAG, "client_recv_len == 0!!");
        return;
    }


    QM_LOGD(LOG_TAG, "mem_free %d ret %d exec_cmd %s", qm_free_mem_get(), ret, client_recv_cmd);

    can_trans_data_send(trans_channel, client_recv_cmd, client_recv_len);
}

static int can_trans_recv_callback(can_trans_rx_node_t *rx_node)
{
    int recv_len = 0;
    uint8_t sta_data[CONFIG_CAN_TRANS_RECV_BUFF_LEN] = {0};

    QM_RETURN_ON_FALSE(g_tran_ctx, -QM_EINVAL, LOG_TAG, "not init !!");
    QM_RETURN_ON_FALSE(rx_node, -QM_EINVAL, LOG_TAG, "rx inval !!");

    if(g_tran_ctx->recv_fn == NULL && g_tran_ctx->at_handle == NULL){
        return -QM_EINVAL;
    }

    if(rx_node->data_size > CONFIG_CAN_TRANS_RECV_BUFF_LEN){
        return -QM_EFULL;
    }
#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
    qm_mutex_lock(&rx_node->rx_mutex, QM_WAIT_FOREVER);
#endif
    recv_len = qm_ringbuf_pop(&rx_node->ringbuf, sta_data, rx_node->data_size);
    rx_node->data_size -= recv_len;
#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
    qm_mutex_unlock(&rx_node->rx_mutex);
#endif

#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_UART
    if(g_tran_ctx->recv_fn && rx_node->tran_channel.dev_channel){
        g_tran_ctx->recv_fn(rx_node->tran_channel, sta_data, recv_len);
    }
#endif

#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_UART
    if(g_tran_ctx->at_handle && !rx_node->tran_channel.dev_channel){
        handle_prod_can(rx_node->tran_channel.dev_channel, sta_data, recv_len); 
    }
#else
    if(g_tran_ctx->at_handle && rx_node->tran_channel.dev_channel){
        handle_prod_can(rx_node->tran_channel.dev_channel, sta_data, recv_len); 
    }
#endif


    return QM_EOK;
}

static int can_trans_recv_handler(void *arg)
{
    int ret = QM_EOK;
    uint8_t data_index = 1;
    uint16_t packet_id  = 0;
    qm_list_node_t *node = NULL;
    can_trans_rx_node_t *rx_node = NULL;
    can_trans_channel_t trans_channel = {0};
    qm_can_message_t *rx_message = (qm_can_message_t *)arg;

    QM_RETURN_ON_FALSE(rx_message, -QM_EINVAL, LOG_TAG, "rx inval !!");

    can_trans_recv_port_get(rx_message->identifier, &trans_channel);

    packet_id = (rx_message->data[0] & CAN_TRANS_PACKET_ID_SIZE);

    if(((rx_message->data[0] >> 7) & 0xFF)){
        packet_id = (packet_id << 8) | rx_message->data[1];
        data_index++;
    }

    qm_mutex_lock(&g_tran_ctx->rx_mutex, QM_WAIT_FOREVER);
    node = qm_list_find(&g_tran_ctx->rx_list, (void *)&trans_channel);
    if(node == NULL){
        node = can_trans_recv_node_creat(trans_channel);
        if(node == NULL){
            qm_mutex_unlock(&g_tran_ctx->rx_mutex);
            return -QM_ENOMEM;
        }
        qm_list_rpush(&g_tran_ctx->rx_list, node);
    }
    qm_mutex_unlock(&g_tran_ctx->rx_mutex);

    rx_node = (can_trans_rx_node_t *)qm_list_node_val_get(node);
    if(!rx_node->packet_num){
        can_trans_recv_data_creat(rx_node, rx_message);
    }

#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
    qm_mutex_lock(&rx_node->rx_mutex, QM_WAIT_FOREVER);
#endif
    if(rx_node->packet_num != packet_id + 1){
        QM_LOGE(LOG_TAG, "RX(%04X) id %d need %d", rx_message->identifier, packet_id, rx_node->packet_num - 1);
    #if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
        qm_mutex_unlock(&rx_node->rx_mutex);
    #endif
        goto __error;
    }

    rx_node->data_size += qm_ringbuf_push(&rx_node->ringbuf, &rx_message->data[data_index], rx_message->data_length - data_index);
#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
    qm_mutex_unlock(&rx_node->rx_mutex);
#endif  
    if((--rx_node->packet_num) == 0){

    #if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
        qm_sem_signal(&rx_node->rx_sem);
    #endif
        can_trans_recv_callback(rx_node);
    }

    return QM_EOK;

__error:
#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
    qm_mutex_lock(&rx_node->rx_mutex, QM_WAIT_FOREVER);
#endif
    rx_node->data_size = 0;
    rx_node->packet_num  = 0;
    qm_ringbuf_flush(&rx_node->ringbuf);
#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
    qm_mutex_unlock(&rx_node->rx_mutex);
#endif
    return -QM_ERROR;
}

static void can_trans_rx_thread(void *arg)
{
    int ret = QM_EOK;
    qm_can_message_t rx_message = {0};

    while (1)
    {
        memset(&rx_message, 0, sizeof(qm_can_message_t));
        ret = qm_can_read(&g_tran_ctx->can_dev, &rx_message, QM_WAIT_FOREVER);
        if(ret != QM_EOK || !rx_message.data_length){
            continue;
        }
        can_trans_recv_handler(&rx_message);
    }
}

static int transport_tx(void)
{
    qm_can_message_t tx_message = {0};
    qm_utils_time_t tx_timeout = {0};
    can_trans_tx_node_t *tx_node = NULL;
    qm_list_node_t *node = NULL;
    qm_list_t *tx_list = &g_tran_ctx->tx_list;

    qm_utils_time_countdown_ms(&tx_timeout, CAN_TRANS_TIMEOUT_MS);

    while(qm_utils_time_left(&tx_timeout))
    {
    #if CONFIG_QM_OS_SUPPORT
        qm_mutex_lock(&g_tran_ctx->tx_mutex, QM_WAIT_FOREVER);
    #endif

        node = qm_list_lpop(tx_list);
        if(node == NULL){
            g_tran_ctx->tx_busy = QM_FALSE;
            goto __exit;
        }


    #if CONFIG_QM_OS_SUPPORT
        qm_mutex_unlock(&g_tran_ctx->tx_mutex);
    #endif
        tx_node = (can_trans_tx_node_t *)qm_list_node_val_get(node);

        tx_message.extd = 1;
        tx_message.data_length = tx_node->data_size;
        memcpy(&tx_message.data, tx_node->pdata, tx_node->data_size);
        can_trans_identifier_set(&tx_message.identifier, &tx_node->tran_channel);

        qm_can_write(&g_tran_ctx->can_dev, &tx_message, 0);

        qm_list_node_destroy(node);
    }
    
    if(tx_list->len){
        qm_event_post(CAN_TRANS_EVENT, CAN_TRANS_SUB_EVENT_DATA_SEND, NULL, 0, QM_NO_WAIT);
    }

    return QM_EOK;

__exit:
    qm_mutex_unlock(&g_tran_ctx->tx_mutex);
    return -QM_EEMPTY;
}

static void qm_event_notify(void *event_handler_arg, qm_event_base_t event_base,
                            int32_t event_id, void *event_data, size_t event_data_size)
{   
    QM_UNUSED_ARG(event_handler_arg);
    QM_UNUSED_ARG(event_base);
    QM_UNUSED_ARG(event_data);
    QM_UNUSED_ARG(event_data_size);

    switch(event_id){
        case CAN_TRANS_SUB_EVENT_DATA_SEND:
            transport_tx();
        break;
        default:
        break;
    }
}

static int can_trans_io_init(uint8_t index, uint8_t io_pin)
{
    int ret = QM_EOK;
    qm_gpio_dev_t *gpio_dev = NULL;

    QM_RETURN_ON_FALSE(index < CAN_TRANS_ADDR_IO_NUM, -QM_EINVAL, LOG_TAG, "already init!!");

    gpio_dev = &g_tran_addr_pins.io_pins[index];

    gpio_dev->port = io_pin;
    gpio_dev->config.mode = QM_GPIO_MODE_INPUT;
    gpio_dev->config.pull_en = QM_GPIO_FLOATING;

    ret = qm_gpio_init(gpio_dev);
    if(ret != QM_EOK){
        QM_LOGE(LOG_TAG, "CAN TRAN IO INIT FAILED %d !! ", io_pin);
        return ret;
    }

    return QM_EOK;
}

static int can_trans_io_deinit(void)
{
    int index = 0;
    int ret = QM_EOK;

    for (index = 0; index < CAN_TRANS_ADDR_IO_NUM; index++)
    {
        ret = qm_gpio_deinit(&g_tran_addr_pins.io_pins[index]);
        if(ret != QM_EOK){
            QM_LOGE(LOG_TAG, "CAN TRAN IO INIT FAILED %d !! ", index);
            return ret;
        }
    }

    return QM_EOK;
}

static uint8_t can_trans_io_to_dev_type(void)
{
    int index = 0;
    uint8_t dev_addr = 0;
    uint8_t get_level = 0;

    for (index = 0; index < CAN_TRANS_ADDR_IO_NUM; index++)
    {
        get_level = qm_gpio_get_level(&g_tran_addr_pins.io_pins[index]);
    #if CONFIG_CAN_TRANS_IO_READ_TOGGLE
        get_level = !get_level;
    #endif
        dev_addr = ((dev_addr << 1) | get_level);
    }
    
    return dev_addr;
}

int can_trans_addr_io_register(uint8_t dev_io1, uint8_t dev_io2, uint8_t dev_io3)
{
    int ret = QM_EOK;
    QM_RETURN_ON_FALSE(g_tran_ctx == NULL, -QM_EINVAL, LOG_TAG, "already init!!");

    g_tran_addr_pins.is_use = QM_TRUE;
    ret = can_trans_io_init(0, dev_io1);
    if(ret != QM_EOK){
        goto __error;
    }

    ret = can_trans_io_init(1, dev_io2);
    if(ret != QM_EOK){
        goto __error;
    }

    ret = can_trans_io_init(2, dev_io3);
    if(ret != QM_EOK){
        goto __error;
    }

    return QM_EOK;

__error:

    QM_LOGE(LOG_TAG, "CAN TRAN IO REGISTER FAILED !! ");
    return ret;
}

int can_trans_init(can_trans_param_t *param)
{   
    int ret = QM_EOK;
    uint32_t identifier = 0;
    can_trans_channel_t trans_channel = {0};

    QM_RETURN_ON_FALSE(param, -QM_EINVAL, LOG_TAG, "param inval!!");
    QM_RETURN_ON_FALSE(g_tran_ctx == NULL, -QM_EINVAL, LOG_TAG, "already init!!");

    g_tran_ctx = (can_trans_ctx_t *)qm_malloc(sizeof(can_trans_ctx_t));
    if(g_tran_ctx == NULL){
        return -QM_ENOMEM;
    }
    memset(g_tran_ctx, 0, sizeof(can_trans_ctx_t));

    ret = qm_mutex_new(&g_tran_ctx->tx_mutex);
    if(ret != QM_EOK){
        goto __error;
    }

    ret = qm_mutex_new(&g_tran_ctx->rx_mutex);
    if(ret != QM_EOK){
        goto __error;
    }

    if(g_tran_addr_pins.is_use == QM_TRUE){
        param->device_addr.dev_addr =  can_trans_io_to_dev_type();
        can_trans_io_deinit();
    }

    g_tran_ctx->can_dev.can_config.tx_pin = param->tx_pin;
    g_tran_ctx->can_dev.can_config.rx_pin = param->rx_pin;
    g_tran_ctx->can_dev.can_config.clock  = param->can_clock;
    g_tran_ctx->can_dev.can_config.mode  = QM_CAN_MODE_NORMAL;

    memcpy(&g_tran_ctx->device_addr, &param->device_addr, sizeof(can_trans_addr_t));
    memcpy(&trans_channel.tran_dst_addr, &param->device_addr, sizeof(can_trans_addr_t));

    can_trans_identifier_set(&identifier, &trans_channel);
    g_tran_ctx->can_dev.filter_config.acceptance_code = identifier;
    g_tran_ctx->can_dev.filter_config.acceptance_mask = 0xFFFF;

    ret = qm_can_init(&g_tran_ctx->can_dev);
    if(ret != QM_EOK){
        goto __error;
    }

    g_tran_ctx->rx_list.match = list_match;

    qm_event_handler_register(CAN_TRANS_EVENT, QM_EVENT_ANY_ID, qm_event_notify, NULL);
    qm_task_new(&g_tran_ctx->rx_task, "can task", can_trans_rx_thread, NULL, CONFIG_CAN_TRANS_TASK_SIZE, CONFIG_CAN_TRANS_TASK_PRIO);
    return ret;

__error:

    if(qm_mutex_is_valid(&g_tran_ctx->tx_mutex)){
        qm_mutex_free(&g_tran_ctx->tx_mutex);
    }

    qm_free(g_tran_ctx);
    g_tran_ctx = NULL;

    return ret;
}

static int can_trans_data_total_id_get(uint8_t *is_long, int *packet_id, uint8_t *data, int length)
{
    int total_num = 0;
    uint8_t packet_size = CAN_TRANS_DATA_SIZE - CAN_TRANS_HEAD_SIZE;


    total_num = ((length % packet_size != 0) || (length < packet_size)) ? 
                (length / packet_size) : 
                (length / packet_size - 1); 

    if(total_num > CAN_TRANS_PACKET_ID_SIZE){
        *is_long = QM_TRUE;
        packet_size = CAN_TRANS_DATA_SIZE - CAN_TRANS_HEAD_EXT_SIZE;
        total_num = ((length % packet_size != 0) || (length < packet_size)) ? 
                    (length / packet_size) : 
                    (length / packet_size - 1); 
    }

    *packet_id = total_num;

    return QM_EOK;
}

static int can_trans_data_total_id_set(uint8_t is_long, uint16_t packet_id, uint8_t *data, int length)
{
    if(!is_long){
        data[0] = packet_id;
    }else{
        packet_id = cpu_to_be16(packet_id);
        memcpy(data, &packet_id, sizeof(uint16_t));
        data[0] |= 0x80;
    }

    return QM_EOK;
}

#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_BOX
int can_trans_data_flush(can_trans_channel_t can_channel)
{
    uint32_t identifier = 0;
    qm_list_node_t *node = NULL;
    can_trans_rx_node_t *rx_node = NULL;
    qm_list_t *rx_list = &g_tran_ctx->rx_list;

    can_trans_identifier_set(&identifier, &can_channel);

    qm_mutex_lock(&g_tran_ctx->rx_mutex, QM_WAIT_FOREVER);
    node = qm_list_find(rx_list, (void *)&can_channel);
    if(node == NULL){
        node = can_trans_recv_node_creat(can_channel);
        if(node == NULL){
            qm_mutex_unlock(&g_tran_ctx->rx_mutex);
            return -QM_ENOMEM;
        }
        qm_list_rpush(&g_tran_ctx->rx_list, node);
    }
    rx_node = (can_trans_rx_node_t *)qm_list_node_val_get(node);

    qm_mutex_unlock(&g_tran_ctx->rx_mutex);


    qm_mutex_lock(&rx_node->rx_mutex, QM_WAIT_FOREVER);
    rx_node->data_size = 0;
    qm_ringbuf_flush(&rx_node->ringbuf);
    qm_mutex_unlock(&rx_node->rx_mutex);

    return QM_EOK;
}

int can_trans_data_read(can_trans_channel_t can_channel, uint8_t *data, int length, int timeout)
{
    int ret = QM_EOK;
    int wait = 0;
    int is_empty = 0;
    uint32_t t_left = 0;
    qm_utils_time_t time = {0};

    int recv_length = 0;
    int left_length = 0;

    qm_list_node_t *node = NULL;
    can_trans_rx_node_t *rx_node = NULL;
    qm_list_t *rx_list = &g_tran_ctx->rx_list;
    
    qm_mutex_lock(&g_tran_ctx->rx_mutex, QM_WAIT_FOREVER);
    node = qm_list_find(rx_list, (void *)&can_channel);
    if(node == NULL){
        node = can_trans_recv_node_creat(can_channel);
        if(node == NULL){
            qm_mutex_unlock(&g_tran_ctx->rx_mutex);
            return -QM_ENOMEM;
        }
        qm_list_rpush(&g_tran_ctx->rx_list, node);
    }
    qm_mutex_unlock(&g_tran_ctx->rx_mutex);

    rx_node = (can_trans_rx_node_t *)qm_list_node_val_get(node);

    if(timeout == QM_WAIT_FOREVER){
        wait = 1;
        t_left = QM_WAIT_FOREVER;
    }else{
        qm_utils_time_init(&time);
        qm_utils_time_countdown_ms(&time, timeout);  
    }

    do{
        if(!wait){
            t_left = qm_utils_time_left(&time);
            if(t_left == 0){
                break;
            }
        }

        qm_mutex_lock(&rx_node->rx_mutex, QM_WAIT_FOREVER);
        is_empty = qm_ringbuf_isempty(&rx_node->ringbuf);
        qm_mutex_unlock(&rx_node->rx_mutex);

        if(is_empty){
            ret = qm_sem_wait(&rx_node->rx_sem, t_left);
            if(ret != QM_EOK){
                ret = 0;
                break;
            }
        }

        qm_mutex_lock(&rx_node->rx_mutex, QM_WAIT_FOREVER);
        recv_length = qm_ringbuf_pop(&rx_node->ringbuf, data + left_length, length - left_length);
        rx_node->data_size -= recv_length;
        qm_mutex_unlock(&rx_node->rx_mutex);

        left_length += recv_length;

    }while ((left_length < length));

    return (left_length ? left_length : -QM_ETIMEOUT);
}
#endif

int can_trans_data_send(can_trans_channel_t can_channel, uint8_t *data, int length)
{
    int packet_id  = 0;
    uint8_t tx_busy = 0;
    int total_addr  = 0;
    uint8_t long_packet = 0;
    uint16_t packet_addr  = 0;
    qm_list_node_t *node = NULL;
    can_trans_tx_node_t *tx_node = NULL;
    qm_list_t *tx_list = &g_tran_ctx->tx_list;

    can_trans_data_total_id_get(&long_packet, &packet_id, data, length);

    if(long_packet){
        packet_addr = CAN_TRANS_HEAD_EXT_SIZE;
    }else{
        packet_addr = CAN_TRANS_HEAD_SIZE;
    }

    qm_mutex_lock(&g_tran_ctx->tx_mutex, QM_WAIT_FOREVER);
    while (packet_id >= 0)
    {
        node = qm_list_node_extra_new(sizeof(can_trans_tx_node_t));
        if(node == NULL){
            break;
        }
        tx_node = (can_trans_tx_node_t *)qm_list_node_val_get(node);

        if(length >= (CAN_TRANS_DATA_SIZE - packet_addr)){
            tx_node->data_size = CAN_TRANS_DATA_SIZE;
        }else{
            tx_node->data_size = length + packet_addr;
        }

        tx_node->long_packet = long_packet;
        memcpy(&tx_node->tran_channel, &can_channel, sizeof(can_trans_channel_t));
        can_trans_data_total_id_set(long_packet, (uint16_t)packet_id, tx_node->pdata, tx_node->data_size);
        memcpy(&tx_node->pdata[packet_addr], data + total_addr, tx_node->data_size - packet_addr);

        qm_list_rpush(tx_list, node);

        packet_id--;
        length -= (CAN_TRANS_DATA_SIZE - packet_addr);
        total_addr += (tx_node->data_size - packet_addr);
    }

    tx_busy = g_tran_ctx->tx_busy;
    g_tran_ctx->tx_busy = QM_TRUE;

    qm_mutex_unlock(&g_tran_ctx->tx_mutex);

    if(!tx_busy){
        qm_event_post(CAN_TRANS_EVENT, CAN_TRANS_SUB_EVENT_DATA_SEND, NULL, 0, QM_NO_WAIT);
    }

    return QM_EOK;
}

#if CONFIG_CAN_DEVICE_SUPPORT == CAN_TRANS_DEV_UART

int can_trans_recv_register(can_trans_recv_fn recv_fn)
{
    QM_RETURN_ON_FALSE(g_tran_ctx, -QM_EINVAL, LOG_TAG, "not init!!");
    QM_RETURN_ON_FALSE(recv_fn, -QM_EINVAL, LOG_TAG, "callback inval!!");

    g_tran_ctx->recv_fn = recv_fn;

    return QM_EOK;
}

#endif

int can_trans_at_server_register(can_trans_addr_t src_addr, qm_at_cmd_t *cmd_tbl, uint16_t cmd_num)
{
    qm_at_server_param_t param = {0};
    can_trans_addr_t *trans_channel = NULL;

    QM_RETURN_ON_FALSE(g_tran_ctx, -QM_EINVAL, LOG_TAG, "not init!!");
    QM_RETURN_ON_FALSE(cmd_tbl, -QM_EINVAL, LOG_TAG, "cmd_tbl inval!!");
    QM_RETURN_ON_FALSE(cmd_num, -QM_EINVAL, LOG_TAG, "cmd_num inval!!");


    g_tran_ctx->dc_client = data_channel_client_creat();
    g_tran_ctx->dc_server = data_channel_server_creat(CONFIG_CAN_TRANS_RECV_BUFF_LEN, CONFIG_CAN_TRANS_RECV_BUFF_LEN);
    data_channel_connect(g_tran_ctx->dc_server, g_tran_ctx->dc_client);

    param.handle = (void *)g_tran_ctx->dc_server;
    param.channel_type = 0;
    param.recv = can_at_recv;
    param.send = can_at_send;
    param.recv_buf_len = CONFIG_CAN_TRANS_RECV_BUFF_LEN;

    g_tran_ctx->at_handle = qm_at_server_init(&param);
    if(g_tran_ctx->at_handle == NULL){
        return -QM_EINIT;
    }

    qm_at_register_commands(g_tran_ctx->at_handle, cmd_tbl, cmd_num);

    return QM_EOK;
}

int can_trnas_deinit(void)
{
    return QM_EOK;
}
