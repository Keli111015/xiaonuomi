#include "opple_serial_protocol.h"
#include "qm_log.h"

#define OPPLE_SERIAL_MSGLEN  256
uint8_t msg_readbuf[OPPLE_SERIAL_MSGLEN] = {0};

static void network_config_callback(opple_network_config_type_t type)
{
    QM_LOGD("test", "network config: %d", type);
}

static void report_callback(opple_frame_cmd_t cmd, uint8_t *data, uint16_t len, void *arg)
{
    qm_tlv_ctx_t *tlv_ctx = (qm_tlv_ctx_t*)arg;
    qm_tlv_block_t *tlv_block = NULL;

    QM_HEX_LOGD("test", "report", data, len);

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);

    while(tlv_block){

        QM_LOGD("test", "key: %d", tlv_block->tag);
        //to do something
        tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
    }

    uint8_t test[] = {0x01, 0x01, 0x01};

    opple_serial_cloud_set(1, test, sizeof(test));

}

static void serial_response_callback(opple_frame_cmd_t cmd, opple_platform_id_t id, uint8_t *data, uint16_t len, void *arg)
{
    QM_LOGD("test", "reponse id: %d", id);
    QM_HEX_LOGD("test", "response", data, len);

    qm_tlv_ctx_t *tlv_ctx = (qm_tlv_ctx_t*)arg;
    qm_tlv_block_t *tlv_block = NULL;

    tlv_block = qm_tlv_block_next(tlv_ctx, NULL);

    if(cmd == OPPLE_CMD_GET){
        while(tlv_block){
            QM_LOGD("test", "key: %d", tlv_block->tag);
            //to do something
            tlv_block = qm_tlv_block_next(tlv_ctx, tlv_block);
        }
    }
}

static int32_t mac_setmode_callback(void *arg)
{
    uint8_t bytes[6] = {0};
    uint8_t len = 6;
    qm_tlv_block_t *tlv_block = (qm_tlv_block_t*)arg;

    //to do something

    qm_tlv_unpack_bytes(tlv_block, bytes, (int*)&len);

    QM_HEX_LOGD("test", "set mac", bytes, len);
    
    return 0;
}


static int32_t mac_getmode_callback(void *arg)
{
    uint8_t bytes[6] = {0};
    qm_tlv_block_t *tlv_block = (qm_tlv_block_t*)arg;

    //to do something

    qm_tlv_pack_bytes(tlv_block, bytes, 6);
    
    return 0;
}

static void serial_send_thread(void *arg)
{
    //QM_LOGD("test", "serial send started");

    while(1)
    {
        opple_serial_send_loop();
    }
}

static void serial_recv_thread(void *arg)
{
    //QM_LOGD("test", "serial recv started");

    while(1)
    {
        opple_serial_recv_loop();
    }
}

static void serial_set_thread(void *arg)
{

    uint8_t data[] = {0x01,0x01,0x02};

    while(1){
        qm_msleep(500);
        opple_serial_cloud_set(0, data, sizeof(data));
    }
}

void opple_serial_test(void)
{
    qm_task_t send_task = {0};
    qm_task_t recv_task = {0};

    opple_serial_param_t serial_param = {0};

    serial_param.pread_buf = msg_readbuf;
    serial_param.read_buf_size = OPPLE_SERIAL_MSGLEN;

    serial_param.callbacks.m_network_configcb = network_config_callback;
    serial_param.callbacks.m_reportcb = report_callback;
    serial_param.callbacks.m_responsecb = serial_response_callback;

    opple_serial_init(&serial_param);

    opple_serial_module_network_status_set(OPPLE_MODULE_CONNECTED);

    opple_serial_mode_register(0xF1, mac_setmode_callback, mac_getmode_callback);
    
    qm_task_new(&send_task, "serial send", serial_send_thread, NULL, 4*1024, 20);
    qm_task_new(&recv_task, "serial recv", serial_recv_thread, NULL, 4*1024, 20);

    qm_task_new(&recv_task, "serial set", serial_set_thread, NULL, 4*1024, 20);

}

