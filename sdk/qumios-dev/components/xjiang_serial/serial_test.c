#include "serial_protocol.h"
#include "serial_platform.h"
#include "serial_common.h"
#include "qm_kernel.h"

static void network_config_callback(network_config_type_t type)
{
    SERIAL_LOG("network config");
}

static void device_onlive_callback(device_ver_t *ver, uint8_t *data, uint16_t len, qm_tlv_ctx_t *tlv_ctx)
{
    SERIAL_LOG("device onlive");
    SERIAL_LOG("device version:%d.%d.%d", ver->main_ver, ver->sub_ver, ver->fix_ver);
}

static int32_t report_callback(frame_cmd_t cmd, uint8_t *data, uint16_t len, uint8_t eid, qm_tlv_ctx_t *tlv_ctx)
{
    SERIAL_LOG("cmd 0x%02X", cmd);
    SERIAL_HEX_LOG("report", data, len);

    return 0;
}

static void serial_response_callback(frame_cmd_t cmd, platform_id_t id, uint8_t *data, uint16_t len, uint8_t aid, qm_tlv_ctx_t *tlv_ctx)
{
    SERIAL_LOG("serial response cmd: %d, id: %u", cmd, id);
}

static int32_t serial_service_callback(qm_tlv_block_t *tlv_block)
{

    //to do somethings
    return 0;
}

static int32_t mem_getmode_callback(qm_tlv_block_t *tlv_block)
{
    uint32_t mem = qm_free_mem_get();
    SERIAL_LOG("free heap size: %u", mem);
    qm_tlv_pack_uint32(tlv_block, mem);
    return 0;
}

static void serial_send_thread(void *arg)
{
    while(1)
    {
        qm_serial_send_loop();
    }
}

static void serial_recv_thread(void *arg)
{
    while(1)
    {
        qm_serial_recv_loop();
    }
}

void qm_serial_test(void)
{
    #define SERIAL_MSGLEN 1024

    qm_task_t send_task = {0};
    qm_task_t recv_task = {0};

    serial_param_t serial_param = {0};
    uint8_t *msg_readbuf = NULL;

    if (NULL == (msg_readbuf = (uint8_t *)qm_malloc(SERIAL_MSGLEN))) {
        SERIAL_LOG("not enough memory");
        return;
    }

    serial_param.pread_buf = msg_readbuf;
    serial_param.read_buf_size = SERIAL_MSGLEN;

    serial_param.callbacks.m_network_configcb = network_config_callback;
    serial_param.callbacks.m_reportcb = report_callback;
    serial_param.callbacks.m_responsecb = serial_response_callback;
    serial_param.callbacks.m_device_onlivecb = device_onlive_callback;

    qm_serial_init(&serial_param);

    qm_serial_service_register(MODULE_SERVICE_LOWPOWER, serial_service_callback);

    qm_serial_mode_register(247, NULL, mem_getmode_callback);

    qm_task_new(&send_task, "serial send", serial_send_thread, NULL, 4*1024, 20);
    qm_task_new(&recv_task, "serial recv", serial_recv_thread, NULL, 4*1024, 20);
}



