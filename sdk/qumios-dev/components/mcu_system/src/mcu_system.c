#include "mcu_system.h"
#include "mcu_config.h"
#include "mcu_protocol.h"
#include "mcu_platform.h"

#include "qm.h"

#define MCU_REAL_TYPE_ID(type_id)     ( type_id&0x7F )

typedef struct {
    unsigned int size;
    unsigned char  full;
    unsigned int readpoint;
    unsigned int writepoint;
    unsigned char buffer[MCU_FRAME_MIN_LEN + CONFIG_MCU_RECV_BUF_LMT];
}mcu_ringbuf_t;

typedef struct
{
    unsigned char module_status;
    unsigned short need_len;
    unsigned int read_pos;
    unsigned int read_buf_size;  /* Specify size of read-buffer in byte */
    unsigned char parse_state;
    unsigned char pread_buf[MCU_FRAME_MIN_LEN + CONFIG_MCU_RECV_BUF_LMT]; //数据处理缓存
    mcu_ringbuf_t rx_ringbuf;
}mcu_system_context_t;

static int ringbuf_init(mcu_ringbuf_t *ringbuf, unsigned int size);
static int ringbuf_pop(mcu_ringbuf_t *ringbuf, unsigned char *data, int len);
static int ringbuf_push(mcu_ringbuf_t *ringbuf, unsigned char *data, int len);

static int mcu_system_spec_msg_pack(mcu_system_spec_msg_t *msg, unsigned char *data, int len);
static int mcu_system_spec_msg_unpack(mcu_system_spec_msg_t *msg, unsigned char *data, int *len);

static unsigned char frame_chksum(unsigned char *buffer, int length);
static unsigned short set_tx_buffer(unsigned short dest, const unsigned char *src, unsigned short len);
static void generate_send(mcu_frame_cmd_t cmd, unsigned short len);

static int set_pack(unsigned char *data, int len);
static int set_unpack(unsigned char *data, int len);

static int heart_pack(unsigned char *data, int len);
static int heart_unpack(unsigned char *data, int len);

static int get_devinfo_pack(unsigned char *data, int len);
static int get_devinfo_unpack(unsigned char *data, int len);

static int get_snapshot_pack(unsigned char *data, int len);
static int get_snapshot_unpack(unsigned char *data, int len);

typedef struct
{
    mcu_frame_cmd_t cmd;
    int (*pack)(unsigned char *data, int len);
    int (*unpack)(unsigned char *data, int len);
}mcu_system_cmd_table_t;

static const unsigned char devinfo[] = CONFIG_MCU_DEVINFO; //不包含MCU版本号
static const unsigned char attr_snapshot[] = CONFIG_MCU_ATTR_TABLE;

static mcu_system_context_t g_system_context = {0};
static unsigned char system_tx_buf[MCU_FRAME_MIN_LEN + CONFIG_MCU_DATA_PROCESS_LMT]= {0};   //发送缓存

static mcu_system_cmd_table_t g_mcu_system_cmd_table[] = {
    {MCU_CMD_SET,  set_pack, set_unpack},
    {MCU_CMD_HEARTBEAT, heart_pack, heart_unpack},
    {MCU_CMD_GET_DEVICEINFO,  get_devinfo_pack, get_devinfo_unpack},
    {MCU_CMD_GET_SNAPSHOT,  get_snapshot_pack, get_snapshot_unpack},
};

static mcu_system_cmd_table_t *mcu_system_cmd_table_handler_get(mcu_frame_cmd_t recv_cmd)
{
    int i = 0;
    for(i = 0; i < MCU_ARRAY_SIZE(g_mcu_system_cmd_table); i++){
        if(g_mcu_system_cmd_table[i].cmd == recv_cmd){
            return &g_mcu_system_cmd_table[i];
        }
    }
    return CONFIG_MCU_NULL;
}

static int mcu_system_spec_msg_type_len(mcu_frame_msg_type_t type)
{
    type = (mcu_frame_msg_type_t)MCU_REAL_TYPE_ID(type);

    if(type <= MCU_FRAME_MSG_TYPE_UINT8){
        return sizeof(unsigned char);
    }

    if((type >= MCU_FRAME_MSG_TYPE_INT16 && type <= MCU_FRAME_MSG_TYPE_UINT16) || 
        (type >= MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT16 && type <= MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT16) ||
        (type >= MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT16 && type <= MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT16)){

        return sizeof(unsigned short);
    }


    if((type >= MCU_FRAME_MSG_TYPE_INT32 && type <= MCU_FRAME_MSG_TYPE_UINT32) || 
        (type >= MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT32 && type <= MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT32) ||
        (type >= MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT32 && type <= MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT32)){

        return sizeof(unsigned int);
    }

    
    if( (type == MCU_FRAME_MSG_TYPE_STRING) || (type <= MCU_FRAME_MSG_TYPE_ARRAY) || 
        (type == MCU_FRAME_MSG_TYPE_STRING_ARRAY)){
        return 0;
    }

    return -1;
}

static int mcu_system_spec_msg_pack(mcu_system_spec_msg_t *msg, unsigned char *data, int len)
{
    switch (msg->data_type)
    {
        case MCU_FRAME_MSG_TYPE_BOOL:
            mcu_platform_memcpy(&msg->data.boolean, data, len);
        break;
        case MCU_FRAME_MSG_TYPE_INT8:
            mcu_platform_memcpy(&msg->data.int8_Value, data, len);
        break;
        case MCU_FRAME_MSG_TYPE_UINT8:
            mcu_platform_memcpy(&msg->data.uint8_Value, data, len);
        break;
        case MCU_FRAME_MSG_TYPE_INT16:
        case MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT16:
        case MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT16:
            mcu_platform_memcpy(&msg->data.int16_Value, data, len);
            msg->data.int16_Value = MCU_SWAB16(msg->data.int16_Value);
        break;
        case MCU_FRAME_MSG_TYPE_UINT16:
        case MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT16:
        case MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT16:
            mcu_platform_memcpy(&msg->data.uint16_Value, data, len);
            msg->data.uint16_Value = MCU_SWAB16(msg->data.uint16_Value);
        break;
        case MCU_FRAME_MSG_TYPE_INT32:
        case MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT32:
        case MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT32:
            mcu_platform_memcpy(&msg->data.int32_Value, data, len);
            msg->data.int32_Value = MCU_SWAB32(msg->data.int32_Value);
        break;
        case MCU_FRAME_MSG_TYPE_UINT32:
        case MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT32:
        case MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT32:
            mcu_platform_memcpy(&msg->data.uint32_Value, data, len);
            msg->data.uint32_Value = MCU_SWAB32(msg->data.uint32_Value);
        break;
        case MCU_FRAME_MSG_TYPE_STRING:
            msg->data.string.str = data;
            msg->data.string.length = len;
        break;
        case MCU_FRAME_MSG_TYPE_ARRAY:
        case MCU_FRAME_MSG_TYPE_STRING_ARRAY:
            msg->data.array.byte = data;
            msg->data.array.length = len;
        break;
        default:
        break;
    }
    return MCU_SERIAL_PARAM_ERR;
}

static int mcu_system_spec_msg_unpack(mcu_system_spec_msg_t *msg, unsigned char *data, int *len)
{
    int index = 0;
    int type_len = 0;
    unsigned short tmp = 0;

    if(data){
        *(data + index) = msg->data_type;
        *(data + index + 1) = msg->attr_id;
    }
    index += 2;

    type_len = mcu_system_spec_msg_type_len(msg->data_type);
    if(type_len == 0){
        if((msg->data_type == MCU_FRAME_MSG_TYPE_STRING)){
            type_len = msg->data.string.length;
            tmp = MCU_SWAB16(msg->data.string.length);
        }else{
            type_len = msg->data.array.length;
            tmp = MCU_SWAB16(msg->data.array.length);
        }

        if(data){
            mcu_platform_memcpy(data + index, &tmp, 2); 
        }
        index += 2;
    }

    if(data){
        switch (msg->data_type)
        {
            case MCU_FRAME_MSG_TYPE_BOOL:
                mcu_platform_memcpy(data + index, &msg->data.boolean, type_len);
            break;
            case MCU_FRAME_MSG_TYPE_INT8:
                mcu_platform_memcpy(data + index, &msg->data.int8_Value, type_len);
            break;
            case MCU_FRAME_MSG_TYPE_UINT8:
                mcu_platform_memcpy(data + index, &msg->data.uint8_Value, type_len);
            break;
            case MCU_FRAME_MSG_TYPE_INT16:
            case MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT16:
            case MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT16:
                msg->data.int16_Value = MCU_SWAB16(msg->data.int16_Value);
                mcu_platform_memcpy(data + index, &msg->data.int16_Value, type_len);
            break;
            case MCU_FRAME_MSG_TYPE_UINT16:
            case MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT16:
            case MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT16:
                msg->data.uint16_Value = MCU_SWAB16(msg->data.uint16_Value);
                mcu_platform_memcpy(data + index, &msg->data.uint16_Value, type_len);
            break;
            case MCU_FRAME_MSG_TYPE_INT32:
            case MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT32:
            case MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT32:
                msg->data.int32_Value = MCU_SWAB32(msg->data.int32_Value);
                mcu_platform_memcpy(data + index, &msg->data.int32_Value, type_len);
            break;
            case MCU_FRAME_MSG_TYPE_UINT32:
            case MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT32:
            case MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT32:
                msg->data.uint32_Value = MCU_SWAB32(msg->data.uint32_Value);
                mcu_platform_memcpy(data + index, &msg->data.uint32_Value, type_len);
            break;
            case MCU_FRAME_MSG_TYPE_STRING:
                mcu_platform_memcpy(data + index, msg->data.string.str, type_len);
            break;
            case MCU_FRAME_MSG_TYPE_ARRAY:
            case MCU_FRAME_MSG_TYPE_STRING_ARRAY:
                mcu_platform_memcpy(data + index, msg->data.array.byte, type_len);
            break;
            default:
            break;
        }
    }

    index += type_len;
    *len = index;
    
    return MCU_SERIAL_PARAM_ERR;
}

static int set_pack(unsigned char *data, int len)
{
    unsigned short send_len = 0;
    send_len = set_tx_buffer(0, data, len);
    generate_send(MCU_CMD_SET, send_len);
    return MCU_SERIAL_NO_ERR;
}

static int set_unpack(unsigned char *data, int len)
{   
    int index = 1;
    int data_len = 0;
    int type_len = 0;
    unsigned short tmp = 0;
    unsigned char type = 0;
    unsigned char packet_id = 0;
    unsigned int left_msg_num = 0;
    mcu_system_cmd_table_t *cmd_table;
    mcu_system_spec_msg_t spec_msg = {0};

    if(data == CONFIG_MCU_NULL || len == 0){
        return MCU_SERIAL_PARAM_ERR;
    }

    packet_id = *data;
    cmd_table = mcu_system_cmd_table_handler_get(MCU_CMD_SET);
    if(cmd_table && cmd_table->pack){
        cmd_table->pack(&packet_id, sizeof(unsigned char));
    }

    while(index < len){
        type = *(data + index);
        type_len = mcu_system_spec_msg_type_len((mcu_frame_msg_type_t)type);
        if(type_len < 0){
            break;
        }
        index++;

        if(len - index < 1){
            break;
        }
        index++;

        if(type_len == 0){
            if(len - index < 2){
                break;
            }
            mcu_platform_memcpy(&tmp, data + index, sizeof(unsigned short));
            data_len = MCU_SWAB16(tmp);
            index += 2;
        }else{
            data_len = type_len;
        }

        if(len - index < data_len){
            break;
        }

        left_msg_num++;
        index += data_len;
    }

    index = 1;
    while(index < len){
        mcu_platform_memset(&spec_msg, 0, sizeof(mcu_system_spec_msg_t));
        type = *(data + index);
        type_len = mcu_system_spec_msg_type_len((mcu_frame_msg_type_t)type);
        if(type_len < 0){
            break;
        }
        index++;
        spec_msg.data_type = type;

        if(len - index < 1){
            break;
        }
        spec_msg.attr_id = *(data + index);
        index++;

        if(type_len == 0){
            if(len - index < 2){
                break;
            }
            mcu_platform_memcpy(&tmp, data + index, sizeof(unsigned short));
            data_len = ((tmp & 0x00ff) << 8) | ((tmp & 0xff00) >> 8);
            index += 2;
        }else{
            data_len = type_len;
        }

        if(len - index < data_len){
            break;
        }

        mcu_system_spec_msg_pack(&spec_msg, data + index, data_len);

        left_msg_num++;
        index += data_len;
        mcu_protocol_set_handler(&spec_msg, (--left_msg_num));
    }

    return MCU_SERIAL_NO_ERR;
}

static int heart_pack(unsigned char *data, int len)
{
    unsigned short send_len = 0;
    send_len = set_tx_buffer(0, data, len);
    generate_send(MCU_CMD_HEARTBEAT, send_len);
    return MCU_SERIAL_NO_ERR;
}

static int heart_unpack(unsigned char *data, int len)
{   
    unsigned char hex_heartbeat;
    mcu_system_cmd_table_t *cmd_table;

    if(data == CONFIG_MCU_NULL || len == 0){
        return MCU_SERIAL_PARAM_ERR;
    }

    hex_heartbeat = (unsigned char)mcu_protocol_get_heartbeat_status();

    cmd_table = mcu_system_cmd_table_handler_get(MCU_CMD_HEARTBEAT);
    if(cmd_table && cmd_table->pack){
        cmd_table->pack(&hex_heartbeat, sizeof(unsigned char));
    }

    return MCU_SERIAL_NO_ERR;
}

static int get_devinfo_pack(unsigned char *data, int len)
{
    unsigned char ver;
    unsigned short dst = 0;

    ver = CONFIG_MCU_MAIN_VER;
    dst = set_tx_buffer(dst, &ver, sizeof(unsigned char));

    ver = CONFIG_MCU_SUB_VER;
    dst = set_tx_buffer(dst, &ver, sizeof(unsigned char));

    ver = CONFIG_MCU_FIX_VER;
    dst = set_tx_buffer(dst, &ver, sizeof(unsigned char));

    dst = set_tx_buffer(dst, data, len);

    generate_send(MCU_CMD_GET_DEVICEINFO, dst);

    return MCU_SERIAL_NO_ERR;
}

static int get_devinfo_unpack(unsigned char *data, int len)
{
    mcu_system_cmd_table_t *cmd_table = mcu_system_cmd_table_handler_get(MCU_CMD_GET_DEVICEINFO);
    if(cmd_table && cmd_table->pack){
        cmd_table->pack((unsigned char *)devinfo, sizeof(devinfo));
    }
    return MCU_SERIAL_NO_ERR;
}

static int get_snapshot_pack(unsigned char *data, int len)
{
    int dest_len = 0;
    int send_len = 0;
    unsigned char *obj;
    mcu_system_spec_msg_t spec_msg = {0};

    for (int i = 0; i < len; i++)
    {
        obj = (unsigned char *)system_tx_buf + MCU_DATA_START + dest_len;
        mcu_platform_memset(&spec_msg, 0, sizeof(mcu_system_spec_msg_t));
        
        spec_msg.attr_id = data[i];
        mcu_protocol_get_attr(&spec_msg);
        
        mcu_system_spec_msg_unpack(&spec_msg, obj, &send_len);
        dest_len += send_len;
    }

    generate_send(MCU_CMD_GET_SNAPSHOT, dest_len);

    return MCU_SERIAL_NO_ERR;
}

static int get_snapshot_unpack(unsigned char *data, int len)
{
    mcu_system_cmd_table_t *cmd_table = mcu_system_cmd_table_handler_get(MCU_CMD_GET_SNAPSHOT);
    if(cmd_table && cmd_table->pack){
        cmd_table->pack((unsigned char *)attr_snapshot, MCU_ARRAY_SIZE(attr_snapshot));
    }
    return MCU_SERIAL_NO_ERR;
}

/**
 * @brief  协议初始化函数
 * @param  Null
 * @return Null
 * @note   在MCU初始化代码中调用该函数
 */
void mcu_system_init(void)
{
    g_system_context.read_pos = 0;
    g_system_context.need_len = 1;
    g_system_context.parse_state = MCU_HEAD_FIRST;
    ringbuf_init(&g_system_context.rx_ringbuf, MCU_FRAME_MIN_LEN + CONFIG_MCU_DATA_PROCESS_LMT);

    mcu_platform_init();
}

static unsigned short get_packet_len(unsigned char *packet_buffer)
{
    return  (MCU_SWAB16((packet_buffer[5] << 8) + (packet_buffer[4])));
}

static int frame_parse(unsigned char *data, unsigned short len)
{
    mcu_system_cmd_table_t *cmd_table;

    if(*(data + len - 1) != frame_chksum(data, len - 1)){
        return MCU_SERIAL_CHKSUM_ERR;
    }

    QM_HEX_LOGD("1", "RECV:", data, len);

    cmd_table = mcu_system_cmd_table_handler_get(data[MCU_FRAME_TYPE]);
    if(cmd_table && cmd_table->unpack){
        cmd_table->unpack(data + MCU_FRAME_HEAD_LEN, len - MCU_FRAME_HEAD_LEN - 1);
    }
    return MCU_SERIAL_NO_ERR;
}

static int recv_cycle(void)
{
    int recv_len = 0;
    int rc = MCU_SERIAL_RX_IDLE;
    unsigned short packet_len = 0;
    unsigned short need_len = g_system_context.need_len;

    recv_len = ringbuf_pop(&g_system_context.rx_ringbuf, g_system_context.pread_buf+g_system_context.read_pos, need_len);
    if(recv_len == 0){
        return MCU_SERIAL_RX_IDLE;
    }

    rc = MCU_SERIAL_RX_BUSY;
    g_system_context.read_pos += recv_len;

    switch(g_system_context.parse_state){

        case MCU_HEAD_FIRST:

            if(g_system_context.pread_buf[g_system_context.read_pos - 1] != MCU_FRAME_HEAD_PART){
                goto clear;
            }else{
                g_system_context.parse_state = MCU_HEAD_SECOND;
                g_system_context.need_len = 1;
            }

        break;

        case MCU_HEAD_SECOND:
            if(g_system_context.need_len <= recv_len){
                if(g_system_context.pread_buf[g_system_context.read_pos - 1] != MCU_FRAME_HEAD_PART){
                    goto clear;
                }else{
                    g_system_context.parse_state = MCU_PROTOCOL_VERSION;
                    g_system_context.need_len = 1;
                }
            }else{
                g_system_context.need_len = g_system_context.need_len - recv_len;
            }

        break;

        case MCU_PROTOCOL_VERSION:

            if(g_system_context.need_len <= recv_len){
                if(g_system_context.pread_buf[g_system_context.read_pos - 1] != MCU_PROTOCOL_VER){
                    goto clear;
                }else{
                    g_system_context.parse_state = MCU_LENGTH_HIGH;
                    g_system_context.need_len = 3;
                }
            }else{
                g_system_context.need_len = g_system_context.need_len - recv_len;
            }

        break;

        case MCU_LENGTH_HIGH:

            if(g_system_context.need_len <= recv_len){
                packet_len = get_packet_len(g_system_context.pread_buf);
                if(packet_len > (g_system_context.read_buf_size - g_system_context.read_pos)){
                    goto clear;
                }else{
                    g_system_context.parse_state = MCU_DATA_START;
                    g_system_context.need_len = packet_len + 1;
                }
            }else{
                g_system_context.need_len = g_system_context.need_len - recv_len;
            }

        break;

        case MCU_DATA_START:

            if(g_system_context.need_len <= recv_len){
                frame_parse(g_system_context.pread_buf, g_system_context.read_pos);
                goto clear;
            }else{
                g_system_context.need_len = g_system_context.need_len - recv_len;
            }

        break;

        default:
            break;
    }

    return rc;

clear:
    g_system_context.parse_state = MCU_HEAD_FIRST;
    g_system_context.read_pos = 0;
    g_system_context.need_len = 1;
    return MCU_SERIAL_RX_IDLE;
}

/**
 * @brief  协议数据处理进程
 * @param  Null
 * @return Null
 * @note   在MCU主函数while循环中调用该函数
 */
void mcu_system_yield(void)
{
    int rc = MCU_SERIAL_RX_IDLE;

    do{
       
       rc = recv_cycle();

    } while (rc != MCU_SERIAL_RX_IDLE);
}

/**
 * @brief  配网通知
 * @param  Null
 * @return Null
 * @note   在MCU主函数while循环中调用该函数
 */
void mcu_system_network_config_notify(mcu_network_config_type_t config_type)
{
    unsigned short dest_len = 0;
    unsigned char type = (unsigned char)config_type;

    dest_len = set_tx_buffer(0, &type, sizeof(unsigned char));
    generate_send(MCU_CMD_NETCONFIG, dest_len);

}

/**
 * @brief  时间获取请求
 * @param  Null
 * @return Null
 * @note   Null
 */
int mcu_system_gettime_request(unsigned char timezone)
{
    unsigned short dest_len = 0;

    dest_len = set_tx_buffer(0, &timezone, sizeof(unsigned char));
    generate_send(MCU_CMD_TIMEGET, dest_len);

    return MCU_SERIAL_NO_ERR;
}

/**
 * @brief  服务设置接口
 * @param[in] {msg} 上报数据格式，详见 mcu_system_spec_msg_t
 * @param[in] {msg_num} 上报数据数量
 * @return Null
 * @note   Null
 */
int mcu_system_set_service(const mcu_system_spec_msg_t *msg, int msg_num)
{
    int dest_len = 0;
    int send_len = 0;
    unsigned char *obj;

    if(!msg || !msg_num){
        return MCU_SERIAL_PARAM_ERR;
    }

    for (int i = 0; i < msg_num; i++)
    {
        obj = (unsigned char *)system_tx_buf + MCU_DATA_START + dest_len;
        mcu_system_spec_msg_unpack((mcu_system_spec_msg_t *)&msg[i], obj, &send_len);
        dest_len += send_len;
    }
    generate_send(MCU_CMD_SERVICE, dest_len);

    return MCU_SERIAL_NO_ERR;
}


/**
 * @brief  属性上报
 * @param[in] {msg} 上报数据格式，详见 mcu_system_spec_msg_t
 * @param[in] {msg_num} 上报数据数量
 * @return Null
 * @note   Null
 */
int mcu_system_report(const mcu_system_spec_msg_t *msg, int msg_num)
{
    int dest_len = 0;
    int send_len = 0;
    unsigned char *obj;

    if(!msg || !msg_num){
        return MCU_SERIAL_PARAM_ERR;
    }

    for (int i = 0; i < msg_num; i++)
    {
        obj = (unsigned char *)system_tx_buf + MCU_DATA_START + dest_len;
        mcu_system_spec_msg_unpack((mcu_system_spec_msg_t *)&msg[i], obj, &send_len);
        dest_len += send_len;
    }
    generate_send(MCU_CMD_REPORT, dest_len);

    return MCU_SERIAL_NO_ERR;
}


static unsigned char frame_chksum(unsigned char *buffer, int length)
{
    int i;
    unsigned char check_sum = 0;
    for (i = 0; i < length; i++){
        check_sum += buffer[i];
    }
    return check_sum;
}

/**
 * @brief  写入tx_buffer
 * @param[in] {dest} 目标地址
 * @param[in] {src} 源地址
 * @param[in] {len} 数据长度
 * @return 写入结束的缓存地址
 */
static unsigned short set_tx_buffer(unsigned short dest, const unsigned char *src, unsigned short len)
{
    if(src == CONFIG_MCU_NULL || len == 0){
        return dest;
    }
    
    unsigned char *obj = (unsigned char *)system_tx_buf + MCU_DATA_START + dest;
    
    mcu_platform_memcpy(obj, src, len);
    
    dest += len;

    return dest;
}

/**
 * @brief  发送一帧数据
 * @param[in] {fr_type} 帧类型
 * @param[in] {fr_ver} 帧版本
 * @param[in] {len} 数据长度
 * @return Null
 */
static void generate_send(mcu_frame_cmd_t cmd, unsigned short len)
{
    unsigned char check_sum = 0;
    
    system_tx_buf[MCU_HEAD_FIRST] = MCU_FRAME_HEAD_PART;
    system_tx_buf[MCU_HEAD_SECOND] = MCU_FRAME_HEAD_PART;
    system_tx_buf[MCU_PROTOCOL_VERSION] = MCU_PROTOCOL_VER;
    system_tx_buf[MCU_FRAME_TYPE] = cmd;
    system_tx_buf[MCU_LENGTH_HIGH] = len >> 8;
    system_tx_buf[MCU_LENGTH_LOW] = len & 0xff;
    
    len += MCU_FRAME_MIN_LEN;
    check_sum = frame_chksum((unsigned char *)system_tx_buf, len - 1);
    system_tx_buf[len - 1] = check_sum;
    
    mcu_platform_transmit_output((unsigned char *)system_tx_buf, len);
}

void mcu_system_spec_msg_new_boolean(mcu_system_spec_msg_t *value, unsigned char new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_BOOL;
    mcu_platform_memcpy(&value->data.boolean, &new_value, sizeof(unsigned char));
}

void mcu_system_spec_msg_new_int8(mcu_system_spec_msg_t *value, signed char new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_INT8;
    mcu_platform_memcpy(&value->data.int8_Value, &new_value, sizeof(signed char));
}

void mcu_system_spec_msg_new_uint8(mcu_system_spec_msg_t *value, unsigned char new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_UINT8;
    mcu_platform_memcpy(&value->data.uint8_Value, &new_value, sizeof(signed char));
}

void mcu_system_spec_msg_new_int16(mcu_system_spec_msg_t *value, signed short new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_INT16;
    mcu_platform_memcpy(&value->data.int16_Value, &new_value, sizeof(signed short));
    value->data.int16_Value = MCU_SWAB16(value->data.int16_Value);
}

void mcu_system_spec_msg_new_uint16(mcu_system_spec_msg_t *value, unsigned short new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_UINT16;
    mcu_platform_memcpy(&value->data.int16_Value, &new_value, sizeof(unsigned short));
    value->data.int16_Value = MCU_SWAB16(value->data.int16_Value);
}

void mcu_system_spec_msg_new_int32(mcu_system_spec_msg_t *value, int new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_INT32;
    mcu_platform_memcpy(&value->data.int32_Value, &new_value, sizeof(int));
    value->data.int32_Value = MCU_SWAB32(value->data.int32_Value);
}

void mcu_system_spec_msg_new_uint32(mcu_system_spec_msg_t *value, unsigned int new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_UINT32;
    mcu_platform_memcpy(&value->data.uint32_Value, &new_value, sizeof(unsigned int));
    value->data.uint32_Value = MCU_SWAB32(value->data.uint32_Value);
}

void mcu_system_spec_msg_new_float_one_int16(mcu_system_spec_msg_t *value, signed short new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT16;
    mcu_platform_memcpy(&value->data.int16_Value, &new_value, sizeof(signed short));
    value->data.int16_Value = MCU_SWAB16(value->data.int16_Value);
}

void mcu_system_spec_msg_new_float_two_int16(mcu_system_spec_msg_t *value, signed short new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT16;
    mcu_platform_memcpy(&value->data.int16_Value, &new_value, sizeof(signed short));
    value->data.int16_Value = MCU_SWAB16(value->data.int16_Value);
}

void mcu_system_spec_msg_new_float_one_int32(mcu_system_spec_msg_t *value, int new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_FLOAT_ONE_INT32;
    mcu_platform_memcpy(&value->data.int32_Value, &new_value, sizeof(int));
    value->data.int32_Value = MCU_SWAB32(value->data.int32_Value);
}

void mcu_system_spec_msg_new_float_two_int32(mcu_system_spec_msg_t *value, int new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_FLOAT_TWO_INT32;
    mcu_platform_memcpy(&value->data.int32_Value, &new_value, sizeof(int));
    value->data.int32_Value = MCU_SWAB32(value->data.int32_Value);
}

void mcu_system_spec_msg_new_float_one_uint16(mcu_system_spec_msg_t *value, unsigned short new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT16;
    mcu_platform_memcpy(&value->data.uint16_Value, &new_value, sizeof(signed short));
    value->data.uint16_Value = MCU_SWAB16(value->data.uint16_Value);
}

void mcu_system_spec_msg_new_float_two_uint16(mcu_system_spec_msg_t *value, unsigned short new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT16;
    mcu_platform_memcpy(&value->data.uint16_Value, &new_value, sizeof(signed short));
    value->data.uint16_Value = MCU_SWAB16(value->data.uint16_Value);
}

void mcu_system_spec_msg_new_float_one_uint32(mcu_system_spec_msg_t *value, unsigned int new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_FLOAT_ONE_UINT32;
    mcu_platform_memcpy(&value->data.uint32_Value, &new_value, sizeof(unsigned int));
    value->data.uint32_Value = MCU_SWAB32(value->data.uint32_Value);
}

void mcu_system_spec_msg_new_float_two_uint32(mcu_system_spec_msg_t *value, unsigned int new_value)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_FLOAT_TWO_UINT32;
    mcu_platform_memcpy(&value->data.uint32_Value, &new_value, sizeof(unsigned int));
    value->data.uint32_Value = MCU_SWAB32(value->data.uint32_Value);
}

void mcu_system_spec_msg_new_array(mcu_system_spec_msg_t *value, const unsigned char *new_value, unsigned int length)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_ARRAY;
    value->data.array.byte = new_value;
    value->data.array.length = length;
}

void mcu_system_spec_msg_new_nstring(mcu_system_spec_msg_t *value, const unsigned char *new_value, unsigned int length)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_STRING;
    value->data.string.str = new_value;
    value->data.string.length = length;
}

void mcu_system_spec_msg_new_stringarray(mcu_system_spec_msg_t *value, const unsigned char *new_value, unsigned int length)
{
    if(!value){
        return;
    }

    value->data_type = MCU_FRAME_MSG_TYPE_STRING_ARRAY;
    value->data.array.byte = new_value;
    value->data.array.length = length;
}


static int ringbuf_init(mcu_ringbuf_t *ringbuf, unsigned int size)
{
    if(ringbuf ==  CONFIG_MCU_NULL  || size == 0){
        return 0;
    }   

    ringbuf->full = 0;
    ringbuf->readpoint  = 0;
    ringbuf->writepoint = 0;
    ringbuf->size       = size;
    mcu_platform_memset(ringbuf->buffer, 0, ringbuf->size);

    return 0;
}

static int ringbuf_push(mcu_ringbuf_t *ringbuf, unsigned char *data, int len)
{
    int i = 0;

    if(ringbuf ==  CONFIG_MCU_NULL || data ==  CONFIG_MCU_NULL || len == 0){
        return 0;
    }   

    if (len > ringbuf->size) {
        return -1;
    }

    for (i = 0; i < len; i++) {
        if (((ringbuf->writepoint + 1) % ringbuf->size) == ringbuf->readpoint) {
            ringbuf->full = 1;
            return i;
        } else {
            if (ringbuf->writepoint < (ringbuf->size - 1)) {
                ringbuf->writepoint++;
            } else {
                ringbuf->writepoint = 0;
            }
            ringbuf->buffer[ringbuf->writepoint] = data[i];
        }
    }

    return i;
}

static int ringbuf_pop(mcu_ringbuf_t *ringbuf, unsigned char *data, int len)
{
    int i = 0;

    if(ringbuf ==  CONFIG_MCU_NULL || data ==  CONFIG_MCU_NULL || len == 0){
        return 0;
    }   

    for (i = 0; i < len; i++) {
        if (ringbuf->writepoint == ringbuf->readpoint) {
            break;
        } else {
            if (ringbuf->readpoint == (ringbuf->size - 1)) {
                ringbuf->readpoint = 0;
            } else {
                ringbuf->readpoint++;
            }
            data[i] = ringbuf->buffer[ringbuf->readpoint];
            ringbuf->full = 0;
        }
    }
    return i;
}

/**
 * @brief  接收多个字节数据暂存处理
 * @param[in] {value} 要接收的数据的源地址
 * @param[in] {data_len} 要接收的数据的数据长度
 * @return Null
 * @note   1、在MCU串口处理函数中调用该函数,并将接收到的数据作为参数传入
 *         2、数据由MCU_SDK处理,用户请勿再另行处理
 *         3、该接口内容已实现，请勿更改
 */
void mcu_platform_receive_buff_input(unsigned char *value, unsigned short data_len)
{
    ringbuf_push(&g_system_context.rx_ringbuf, value, data_len);
}
