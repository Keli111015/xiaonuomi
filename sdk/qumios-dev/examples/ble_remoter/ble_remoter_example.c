#include "qm.h"
#include "ble_remoter.h"

static void pair_recv(ble_remoter_cmd_type_t cmd_type, uint8_t opcode, ble_remoter_payload_t *payload);
static void comm1_recv(ble_remoter_cmd_type_t cmd_type, uint8_t opcode, ble_remoter_payload_t *payload);
static void comm2_recv(ble_remoter_cmd_type_t cmd_type, uint8_t opcode, ble_remoter_payload_t *payload);

static ble_remoter_cmd_table_t g_cmd_table[] = {
    {BLE_REMOTE_CMD_TYPE_PAIR, 0x01, pair_recv},
    {BLE_REMOTE_CMD_TYPE_COMM, 0x02, comm1_recv},
    {BLE_REMOTE_CMD_TYPE_COMM, 0x10, comm2_recv},    
};

static void pair_recv(ble_remoter_cmd_type_t cmd_type, uint8_t opcode, ble_remoter_payload_t *payload)
{
    QM_LOGD("1", "pair_recv !!");
}

static void comm1_recv(ble_remoter_cmd_type_t cmd_type, uint8_t opcode, ble_remoter_payload_t *payload)
{
    QM_LOGD("1", "comm1_recv @@");
}

static void comm2_recv(ble_remoter_cmd_type_t cmd_type, uint8_t opcode, ble_remoter_payload_t *payload)
{
    QM_LOGD("1", "comm2_recv ##");
}

void qm_application_start(void)
{
    ble_remoter_params_t params = {

        .comm_param = {
            .fmsk = 0,
            .pid = 0,
            .subtype = 1,
        },
        
        .pair_param = {
            .enable = QM_TRUE,
            .pair_window = 10 * 1000,
        },

    };
    ble_remoter_init(&params, g_cmd_table, QM_ARRAY_SIZE(g_cmd_table));
}
