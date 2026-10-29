#include "qm.h"
#include "qm_ble_gap.h"
#include "qm_ble_common.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"

#include "le_smartbox_module.h"

#define LOG_TAG "qm_ble_hal"

#include "le/ble_api.h"
#include "le_common.h"

#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)

/*
    //////////////////////////////////////////////////////
    //
    // 0x00b0 PRIMARY_SERVICE  feb7
    //
    //////////////////////////////////////////////////////
    0x0a, 0x00, 0x02, 0x00, 0xb0, 0x00, 0x00, 0x28, 0xb7, 0xfe,

      CHARACTERISTIC,  fea6, READ | DYNAMIC, 
    // 0x00b1 CHARACTERISTIC fea6 READ  | DYNAMIC 
    0x0d, 0x00, 0x02, 0x00, 0xb1, 0x00, 0x03, 0x28, 0x02, 0xb2, 0x00, 0xa6, 0xfe,
    // 0x00b2 VALUE fea6 READ | DYNAMIC  
    0x08, 0x00, 0x02, 0x01, 0xb2, 0x00, 0xa6, 0xfe,

      CHARACTERISTIC,  fea7, WRITE, 
    // 0x00b3 CHARACTERISTIC fea7 WRITE 
    0x0d, 0x00, 0x02, 0x00, 0xb3, 0x00, 0x03, 0x28, 0x08, 0xb4, 0x00, 0xa7, 0xfe,
    // 0x00b4 VALUE fea7 WRITE | READ | DYNAMIC  
    0x08, 0x00, 0x0A, 0x01, 0xb4, 0x00, 0xa7, 0xfe,

      CHARACTERISTIC,  fea9, READ | WEIRE NO RESPONSE, 
    // 0x00b5 CHARACTERISTIC fea9 WRITE 
    0x0d, 0x00, 0x02, 0x00, 0xb5, 0x00, 0x03, 0x28, 0x06, 0xb6, 0x00, 0xa9, 0xfe,
    // 0x00b6 VALUE fea7 WRITE | READ |  WEIRE NO RESPONSE  
    0x08, 0x00, 0x06, 0x01, 0xb6, 0x00, 0xa9, 0xfe,

      CHARACTERISTIC,  feaa READ | Notify, 
    // 0x00b7 CHARACTERISTIC feaa Notify 
    0x0d, 0x00, 0x02, 0x00, 0xb7, 0x00, 0x03, 0x28, 0x12, 0xb8, 0x00, 0xaa, 0xfe,
    // 0x00b8 VALUE feaa Notify
    0x08, 0x00, 0x12, 0x00, 0xb8, 0x00, 0xaa, 0xfe,
    // 0x00b9 CLIENT_CHARACTERISTIC_CONFIGURATION 
    0x0a, 0x00, 0x0a, 0x01, 0xb9, 0x00, 0x02, 0x29, 0x00, 0x00,

      CHARACTERISTIC,  fea8, READ | Indicate, 
    // 0x00ba CHARACTERISTIC fea8 WRITE 
    0x0d, 0x00, 0x02, 0x00, 0xba, 0x00, 0x03, 0x28, 0x22, 0xb8, 0x00, 0xa8, 0xfe,
    // 0x00bb VALUE fea8 WRITE | READ |  Indicate  
    0x08, 0x00, 0x22, 0x00, 0xbb, 0x00, 0xa8, 0xfe,
    // 0x00bc CLIENT_CHARACTERISTIC_CONFIGURATION 
    0x0a, 0x00, 0x0a, 0x01, 0xbc, 0x00, 0x02, 0x29, 0x00, 0x00,

    //////////////////////////////////////////////////////

*/
#define XJIANG_READ_HANDLE              (0x00B2)
#define XJIANG_WC_HANDLE                (0x00B4)
#define XJIANG_WWNRC_HANDLE             (0x00B6)

#define XJIANG_NOTIFY_HANDLE            (0x00B8)
#define XJIANG_NOTIFY_CFG_HANDLE        (0x00B9)

#define XJIANG_INDICATE_HANDLE          (0x00BB)
#define XJIANG_INDICATE_CFG_HANDLE      (0x00BC)


typedef struct 
{

    void (*txdone)(uint8_t res);
    uint8_t connect;

    uint16_t ntf_cccd_config;
    uint16_t ind_cccd_config;

    on_char_read_t rc_read;
    on_char_write_t wc_write;
    on_char_ccc_change_t ic_ccc_change;
    on_char_write_t wwnrc_write;
    on_char_ccc_change_t nc_ccc_change;
    connected_callback_t on_connected;
    disconnected_callback_t on_disconnected;
}qm_ble_hal_ctx_t;

static qm_ble_hal_ctx_t g_ble_hal_ctx = {0};

void xjiang_ble_status_callback(ble_state_e state)
{
    switch (state)
    {
        case BLE_ST_CONNECT:
            g_ble_hal_ctx.connect = QM_TRUE;
            if(g_ble_hal_ctx.on_connected){
                g_ble_hal_ctx.on_connected();
            }
        break;
        case BLE_ST_DISCONN:
            g_ble_hal_ctx.connect = QM_FALSE;
            if(g_ble_hal_ctx.on_disconnected){
                g_ble_hal_ctx.on_disconnected();
            }
        break;
        default:
        break;
    }
}

static int app_read_callback_fn(ble_operation_handle_t* op_handle, uint16_t offset, void* buf, u16 len)
{
    printf("\n-gatt read[%04X]:", op_handle->handle);

    int length = 6;
    uint8_t read_buff[6] = {0};
    uint16_t handle = 0;

    switch (op_handle->handle)
    {
        case XJIANG_NOTIFY_CFG_HANDLE:
            length = 2;
            memcpy(read_buff, &g_ble_hal_ctx.ntf_cccd_config, 2);
        break;

        case XJIANG_INDICATE_CFG_HANDLE:
            length = 2;
            memcpy(read_buff, &g_ble_hal_ctx.ind_cccd_config, 2);
        break;
        
        case XJIANG_READ_HANDLE:
            g_ble_hal_ctx.rc_read(read_buff, length);
        break;

        default:
        break;
    }

    if(buf){
        memcpy(buf, read_buff, length);
    }
    put_buf(read_buff, length);

    return length;
}

static int app_write_callback_fn(ble_operation_handle_t* op_handle, uint16_t offset, void* buf, u16 len)
{
    printf("\n-gatt write[%04X]:", op_handle->handle);
    put_buf(buf, len);
    switch (op_handle->handle)
    {
        case XJIANG_NOTIFY_CFG_HANDLE:
            memcpy(&g_ble_hal_ctx.ntf_cccd_config, buf, len);
            g_ble_hal_ctx.nc_ccc_change(g_ble_hal_ctx.ntf_cccd_config); //todo ,need modify
        break;

        case XJIANG_INDICATE_CFG_HANDLE:
            memcpy(&g_ble_hal_ctx.ind_cccd_config, buf, len);
            if(g_ble_hal_ctx.ic_ccc_change){
                g_ble_hal_ctx.ic_ccc_change(g_ble_hal_ctx.ind_cccd_config); //todo ,need modify
            }
        break;

        case XJIANG_WC_HANDLE:
            if(g_ble_hal_ctx.wc_write){
                g_ble_hal_ctx.wc_write((void *)buf, len);
            }
        break;

        case XJIANG_WWNRC_HANDLE:
            if(g_ble_hal_ctx.wc_write){
                g_ble_hal_ctx.wwnrc_write((void *)buf, len);
            }
        break;

        default:
        break;
    }
    return QM_EOK;
}

qms_err_t qm_ble_stack_deinit(void)
{
    if(g_ble_hal_ctx.connect == QM_TRUE){
        qm_ble_disconnect(0);
    }
    qm_ble_advertising_stop();
    memset(&g_ble_hal_ctx, 0, sizeof(g_ble_hal_ctx));
    return QM_EOK;
}

/**
 * API to initialize ble stack.
 * @parma[in]  qms_init  Bluetooth stack init parmaters.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init)
{
    g_ble_hal_ctx.on_connected = qms_init->on_connected;
    g_ble_hal_ctx.on_disconnected = qms_init->on_disconnected;

     /* rc */
    g_ble_hal_ctx.rc_read = qms_init->rc.on_read;

    /* wc */
    g_ble_hal_ctx.wc_write = qms_init->wc.on_write;

    /* ic */
    g_ble_hal_ctx.ic_ccc_change = qms_init->ic.on_ccc_change;

    /* wwnrc */
    g_ble_hal_ctx.wwnrc_write = qms_init->wwnrc.on_write;

    /* nc */
    g_ble_hal_ctx.nc_ccc_change = qms_init->nc.on_ccc_change;


    ble_op_set_adv_param((100 * 8 / 5), ADV_IND, 0x07);
    ble_set_read_callback(NULL, app_read_callback_fn);
    ble_set_write_callback(NULL, app_write_callback_fn);

    return QMS_ERR_SUCCESS;
}

/**
 * API to send data via QM's Notify Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @return     0 on success, error code if failure.
 */
qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    //
    u32 ret = APP_BLE_NO_ERROR;
    QM_HEX_LOGD("1", "notify ", p_data, length);
    printf("\n-gatt notify:");
    put_buf(p_data, length);

    ret = ble_user_cmd_prepare(BLE_CMD_ATT_SEND_DATA, 4, XJIANG_NOTIFY_HANDLE, p_data, length, ATT_OP_NOTIFY);
    if (ret == BLE_BUFFER_FULL) {
        ret = APP_BLE_BUFF_FULL;
    }

    if (ret) {
        printf("app_send_fail:%d !!!!!!\n", ret);
    }
    return QMS_ERR_SUCCESS;
}


/**
 * API to send data via QM's Indicate Characteristics.
 * @parma[in]  p_data  data buffer.
 * @parma[in]  length  data length.
 * @parma[in]  txdone  txdone callback.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    u32 ret = APP_BLE_NO_ERROR;
    printf("\n-gatt indication:");
    put_buf(p_data, length);

    ret = ble_user_cmd_prepare(BLE_CMD_ATT_SEND_DATA, 4, XJIANG_NOTIFY_HANDLE, p_data, length, ATT_OP_INDICATE);
    if (ret == BLE_BUFFER_FULL) {
        ret = APP_BLE_BUFF_FULL;
    }

    if (ret) {
        printf("app_send_fail:%d !!!!!!\n", ret);
    }

    txdone(0);

    return QMS_ERR_SUCCESS;
}

/**
 * API to disconnect BLE connection.
 * @param[in]  reason  the reason to disconnect the connection.
 */
void qm_ble_disconnect(uint8_t reason)
{
    ble_app_disconnect();
}

/**
 * API to start bluetooth advertising.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    uint16_t index = 0;
    uint8_t adv_data[32] = {0};
    uint8_t srv[] = {0xB7, 0xFE};

    adv_data[index++] = 2;
    adv_data[index++]=  0x01;
    adv_data[index++] = adv->flag;

    adv_data[index++] = sizeof(uint16_t) + 1;
    adv_data[index++] = 0x03; //BT_DATA_UUID16_ALL
    adv_data[index++] = srv[0];
    adv_data[index++] = srv[1];

    if (adv->vdata.len != 0) {
        adv_data[index++]     = adv->vdata.len + 1;
        adv_data[index++] = 0xFF; // BT_DATA_MANUFACTURER_DATA
        memcpy(&adv_data[index], adv->vdata.data, adv->vdata.len);
    } 
    index += adv->vdata.len;
    ble_op_set_adv_data(index, adv_data);
    
    index = 0;
    adv_data[index++] = strlen(adv->name.name) + 1;
    switch (adv->name.ntype) {
        case QMS_ADV_NAME_SHORT:
            adv_data[index++]  = 0x06;
            break;
        case QMS_ADV_NAME_FULL:
            adv_data[index++]  = 0x09;
            break;
        default:
            return QMS_ERR_INVALID_ADV_DATA;
    }
    memcpy(&adv_data[index], adv->name.name, strlen(adv->name.name));
    index += strlen(adv->name.name);
    ble_op_set_rsp_data(index, adv_data);

    ble_op_adv_enable(1);
    return QM_EOK;
}

/**
 * API to stop bluetooth advertising.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_advertising_stop(void)
{
    ble_op_adv_enable(0);

    return QM_EOK;
}

/**
 * API to start bluetooth advertising.
 * @parma[out]  mac  the uint8_t[BD_ADDR_LEN] space the save the mac address.
 * @return     0 on success, erro code if failure.
 */
qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    int addr_len = 6;
    uint8_t str_mac[6] = {0};
    
#if YT_BT_BLE_ENABLE
    le_controller_get_mac((void *)str_mac);
#else
	qm_kv_get("102", str_mac, &addr_len);  //BLE MAC KV ADDR
#endif

    mac[0] = str_mac[5];
    mac[1] = str_mac[4];
    mac[2] = str_mac[3];
    mac[3] = str_mac[2];
    mac[4] = str_mac[1];
    mac[5] = str_mac[0];
    
    return QM_EOK;
}

#ifdef EN_LONG_MTU
/**
 * API to obtain ble link MTU, if ble stack didn't provide, use DEFAULT_ATT_MTU .
 * @parma[out]  mac  the uint8_t[BD_ADDR_LEN] space the save the mac address.
 * @return     0 on success, erro code if failure.
 */

int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    *att_mtu = QB_GATT_MTU_SIZE_MAX;
    return QM_EOK;
}
#endif