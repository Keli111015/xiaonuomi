#include "qm.h"

#include <string.h>
#include <stdio.h>

#include <os/os.h>
#include <components/bluetooth/bk_ble.h>
#include <components/bluetooth/bk_dm_bluetooth.h>

#include "qm_ble_gap.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"

#define QM_BLE_PRF_ID           10
#define QM_BLE_GATT_MTU         247
#define QM_BLE_SYNC_TIMEOUT_MS  4000
#define QM_BLE_ADV_UNIT(ms)     (((ms) * 8) / 5)

#define ADV_TYPE_FLAGS                      (0x01)
#define ADV_TYPE_LOCAL_NAME                 (0x09)
#define ADV_TYPE_SERVICE_UUIDS_16BIT        (0x14)
#define ADV_TYPE_SERVICE_DATA               (0x16)
#define ADV_TYPE_MANUFACTURER_SPECIFIC      (0xFF)

#define DECL_PRIMARY_SERVICE_128  {0x00, 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define DECL_CHARACTERISTIC_128   {0x03, 0x28, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define DESC_CLIENT_CHAR_CFG_128  {0x02, 0x29, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}

enum {
    QM_IDX_SVC,
    QM_IDX_RC_DECL,
    QM_IDX_RC_VAL,
    QM_IDX_WC_DECL,
    QM_IDX_WC_VAL,
    QM_IDX_IC_DECL,
    QM_IDX_IC_VAL,
    QM_IDX_IC_CCC,
    QM_IDX_WWNRC_DECL,
    QM_IDX_WWNRC_VAL,
    QM_IDX_NC_DECL,
    QM_IDX_NC_VAL,
    QM_IDX_NC_CCC,
    QM_IDX_NB,
};

typedef struct {
    uint8_t stack_ok;
    uint8_t db_ok;
    uint8_t conn_idx;
    uint16_t att_idx_nc;
    uint16_t att_idx_ic;
    qms_bt_init_t *init_param;
    beken_semaphore_t sync_sem;
    void (*ind_txdone)(uint8_t res);
} qm_ble_hal_ctx_t;

static qm_ble_hal_ctx_t g_ble;

static ble_attm_desc_t g_qm_ble_db[QM_IDX_NB] = {
    [QM_IDX_SVC] = {DECL_PRIMARY_SERVICE_128, BK_BLE_PERM_SET(RD, ENABLE), 0, 0},
    [QM_IDX_RC_DECL] = {DECL_CHARACTERISTIC_128, BK_BLE_PERM_SET(RD, ENABLE), 0, 0},
    [QM_IDX_RC_VAL] = {{BLE_UUID_QMS_RC & 0xFF, BLE_UUID_QMS_RC >> 8}, BK_BLE_PERM_SET(RD, ENABLE),
                       BK_BLE_PERM_SET(RI, ENABLE) | BK_BLE_PERM_SET(UUID_LEN, UUID_16), QM_BLE_GATT_MTU},
    [QM_IDX_WC_DECL] = {DECL_CHARACTERISTIC_128, BK_BLE_PERM_SET(RD, ENABLE), 0, 0},
    [QM_IDX_WC_VAL] = {{BLE_UUID_QMS_WC & 0xFF, BLE_UUID_QMS_WC >> 8},
                       BK_BLE_PERM_SET(WRITE_REQ, ENABLE) | BK_BLE_PERM_SET(RD, ENABLE),
                       BK_BLE_PERM_SET(RI, ENABLE) | BK_BLE_PERM_SET(UUID_LEN, UUID_16), QM_BLE_GATT_MTU},
    [QM_IDX_IC_DECL] = {DECL_CHARACTERISTIC_128, BK_BLE_PERM_SET(RD, ENABLE), 0, 0},
    [QM_IDX_IC_VAL] = {{BLE_UUID_QMS_IC & 0xFF, BLE_UUID_QMS_IC >> 8}, BK_BLE_PERM_SET(IND, ENABLE),
                       BK_BLE_PERM_SET(RI, ENABLE) | BK_BLE_PERM_SET(UUID_LEN, UUID_16), QM_BLE_GATT_MTU},
    [QM_IDX_IC_CCC] = {DESC_CLIENT_CHAR_CFG_128,
                       BK_BLE_PERM_SET(RD, ENABLE) | BK_BLE_PERM_SET(WRITE_REQ, ENABLE), 0, 0},
    [QM_IDX_WWNRC_DECL] = {DECL_CHARACTERISTIC_128, BK_BLE_PERM_SET(RD, ENABLE), 0, 0},
    /* WWNRC: Write Without Response (same as ESP WRITE_NR). */
    [QM_IDX_WWNRC_VAL] = {{BLE_UUID_QMS_WWNRC & 0xFF, BLE_UUID_QMS_WWNRC >> 8},
                          BK_BLE_PERM_SET(WRITE_COMMAND, ENABLE),
                          BK_BLE_PERM_SET(UUID_LEN, UUID_16), QM_BLE_GATT_MTU},
    [QM_IDX_NC_DECL] = {DECL_CHARACTERISTIC_128, BK_BLE_PERM_SET(RD, ENABLE), 0, 0},
    [QM_IDX_NC_VAL] = {{BLE_UUID_QMS_NC & 0xFF, BLE_UUID_QMS_NC >> 8}, BK_BLE_PERM_SET(NTF, ENABLE),
                       BK_BLE_PERM_SET(RI, ENABLE) | BK_BLE_PERM_SET(UUID_LEN, UUID_16), QM_BLE_GATT_MTU},
    [QM_IDX_NC_CCC] = {DESC_CLIENT_CHAR_CFG_128,
                       BK_BLE_PERM_SET(RD, ENABLE) | BK_BLE_PERM_SET(WRITE_REQ, ENABLE), 0, 0},
};

static void qm_ble_signal_sync(void)
{
    if (g_ble.sync_sem != NULL) {
        rtos_set_semaphore(&g_ble.sync_sem);
    }
}

static void qm_ble_handle_ccc_write(uint16_t att_idx, const uint8_t *value, uint16_t len,
                                  qms_char_init_t *ch)
{
    uint16_t ccc;

    if (len < 2 || ch == NULL || ch->on_ccc_change == NULL) {
        return;
    }

    ccc = (uint16_t)(value[0] | (value[1] << 8));
    if (ccc == 0x0001) {
        ch->on_ccc_change(QMS_CCC_VALUE_NOTIFY);
    } else if (ccc == 0x0002) {
        ch->on_ccc_change(QMS_CCC_VALUE_INDICATE);
    } else {
        ch->on_ccc_change(QMS_CCC_VALUE_NONE);
    }
}

static void qm_ble_notice_cb(ble_notice_t notice, void *param)
{
    switch (notice) {
    case BLE_5_STACK_OK:
        g_ble.stack_ok = 1;
        break;

    case BLE_5_CREATE_DB: {
        ble_create_db_t *cd = (ble_create_db_t *)param;

        if (cd && cd->prf_id == QM_BLE_PRF_ID && cd->status == BK_ERR_BLE_SUCCESS) {
            g_ble.db_ok = 1;
            g_ble.att_idx_nc = QM_IDX_NC_VAL;
            g_ble.att_idx_ic = QM_IDX_IC_VAL;
        }
        qm_ble_signal_sync();
        break;
    }

    case BLE_5_CONNECT_EVENT: {
        ble_conn_ind_t *c = (ble_conn_ind_t *)param;

        if (c) {
            g_ble.conn_idx = c->conn_idx;
            QM_LOGI("qm ble", "CONNECT idx=%u", (unsigned)c->conn_idx);
            if (g_ble.init_param && g_ble.init_param->on_connected) {
                g_ble.init_param->on_connected();
            }
        }
        break;
    }

    case BLE_5_DISCONNECT_EVENT: {
        g_ble.conn_idx = 0xFF;
        g_ble.ind_txdone = NULL;
        QM_LOGI("qm ble", "DISCONNECT");
        if (g_ble.init_param && g_ble.init_param->on_disconnected) {
            g_ble.init_param->on_disconnected();
        }
        break;
    }

    case BLE_5_TX_DONE: {
        /* Indication confirm — must drive transport/async pipeline. */
        if (g_ble.ind_txdone) {
            void (*cb)(uint8_t res) = g_ble.ind_txdone;

            g_ble.ind_txdone = NULL;
            QM_LOGI("qm ble", "IND TX_DONE");
            cb(0);
        }
        break;
    }

    case BLE_5_WRITE_EVENT: {
        ble_write_req_t *w = (ble_write_req_t *)param;

        if (w == NULL || w->prf_id != QM_BLE_PRF_ID || g_ble.init_param == NULL) {
            break;
        }

        QM_LOGI("qm ble", "WRITE att=%u len=%u", (unsigned)w->att_idx, (unsigned)w->len);

        if (w->att_idx == QM_IDX_WC_VAL && g_ble.init_param->wc.on_write) {
            g_ble.init_param->wc.on_write(w->value, w->len);
        } else if (w->att_idx == QM_IDX_WWNRC_VAL && g_ble.init_param->wwnrc.on_write) {
            g_ble.init_param->wwnrc.on_write(w->value, w->len);
        } else if (w->att_idx == QM_IDX_NC_CCC) {
            qm_ble_handle_ccc_write(w->att_idx, w->value, w->len, &g_ble.init_param->nc);
        } else if (w->att_idx == QM_IDX_IC_CCC) {
            qm_ble_handle_ccc_write(w->att_idx, w->value, w->len, &g_ble.init_param->ic);
        }
        break;
    }

    case BLE_5_READ_EVENT: {
        ble_read_req_t *r = (ble_read_req_t *)param;
        static uint8_t s_read_buf[QM_BLE_GATT_MTU];
        uint16_t n = 0;

        if (r == NULL || r->prf_id != QM_BLE_PRF_ID || g_ble.init_param == NULL) {
            break;
        }

        QM_LOGI("qm ble", "READ att=%u", (unsigned)r->att_idx);

        if (r->att_idx == QM_IDX_RC_VAL && g_ble.init_param->rc.on_read) {
            n = (uint16_t)g_ble.init_param->rc.on_read(s_read_buf, sizeof(s_read_buf));
            bk_ble_read_response_value(r->conn_idx, n, s_read_buf, r->prf_id, r->att_idx);
        } else if (r->att_idx == QM_IDX_WC_VAL && g_ble.init_param->wc.on_read) {
            n = (uint16_t)g_ble.init_param->wc.on_read(s_read_buf, sizeof(s_read_buf));
            bk_ble_read_response_value(r->conn_idx, n, s_read_buf, r->prf_id, r->att_idx);
        } else {
            /* CCC / empty: still respond so peer does not stall. */
            bk_ble_read_response_value(r->conn_idx, 0, s_read_buf, r->prf_id, r->att_idx);
        }
        break;
    }

    default:
        break;
    }
}

qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init)
{
    struct bk_ble_db_cfg db_cfg;
    ble_err_t ret;

    if (qms_init == NULL) {
        return QMS_ERR_STACK_FAIL;
    }

    memset(&g_ble, 0, sizeof(g_ble));
    g_ble.conn_idx = 0xFF;
    g_ble.init_param = qms_init;

    if (g_ble.sync_sem == NULL) {
        rtos_init_semaphore(&g_ble.sync_sem, 1);
    }

    if (bk_bluetooth_init() != BT_OK) {
        return QMS_ERR_STACK_FAIL;
    }

    bk_ble_set_notice_cb(qm_ble_notice_cb);

    memset(&db_cfg, 0, sizeof(db_cfg));
    db_cfg.att_db = g_qm_ble_db;
    db_cfg.att_db_nb = QM_IDX_NB;
    db_cfg.prf_task_id = QM_BLE_PRF_ID;
    db_cfg.start_hdl = 0;
    db_cfg.svc_perm = BK_BLE_PERM_SET(SVC_UUID_LEN, UUID_16);
    db_cfg.uuid[0] = BLE_UUID_QMS_SERVICE & 0xFF;
    db_cfg.uuid[1] = BLE_UUID_QMS_SERVICE >> 8;

    ret = bk_ble_create_db(&db_cfg);
    if (ret != BK_ERR_BLE_SUCCESS) {
        return QMS_ERR_STACK_FAIL;
    }

    if (rtos_get_semaphore(&g_ble.sync_sem, QM_BLE_SYNC_TIMEOUT_MS) != kNoErr || !g_ble.db_ok) {
        return QMS_ERR_STACK_FAIL;
    }

    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_stack_deinit(void)
{
    bk_bluetooth_deinit();
    g_ble.init_param = NULL;
    g_ble.db_ok = 0;
    g_ble.stack_ok = 0;
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    if (p_data == NULL || length == 0 || g_ble.conn_idx == 0xFF) {
        return QMS_ERR_GATT_NOTIFY_FAIL;
    }

    if (bk_ble_send_noti_value(g_ble.conn_idx, length, p_data, QM_BLE_PRF_ID, g_ble.att_idx_nc) !=
        BK_ERR_BLE_SUCCESS) {
        return QMS_ERR_GATT_NOTIFY_FAIL;
    }
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    if (p_data == NULL || length == 0 || g_ble.conn_idx == 0xFF) {
        return QMS_ERR_GATT_INDICATE_FAIL;
    }

    g_ble.ind_txdone = txdone;

    if (bk_ble_send_ind_value(g_ble.conn_idx, length, p_data, QM_BLE_PRF_ID, g_ble.att_idx_ic) !=
        BK_ERR_BLE_SUCCESS) {
        g_ble.ind_txdone = NULL;
        return QMS_ERR_GATT_INDICATE_FAIL;
    }
    return QMS_ERR_SUCCESS;
}

void qm_ble_disconnect(uint8_t reason)
{
    (void)reason;

    if (g_ble.conn_idx != 0xFF) {
        bk_ble_disconnect(g_ble.conn_idx);
    }
}

qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    uint8_t *adv_buf;
    uint16_t index = 0;
    uint16_t scan_index;
    uint8_t flag = 0x06;
    uint8_t srv[2];
    qm_ble_adv_params_t adv_params;

    if (adv == NULL || adv->name.name == NULL) {
        return QMS_ERR_INVALID_ADV_DATA;
    }

    adv_buf = (uint8_t *)qm_malloc(128);
    if (adv_buf == NULL) {
        return QMS_ERR_MEM_FAIL;
    }

    srv[0] = BLE_UUID_QMS_SERVICE & 0xFF;
    srv[1] = BLE_UUID_QMS_SERVICE >> 8;

    adv_buf[index++] = 2;
    adv_buf[index++] = ADV_TYPE_FLAGS;
    adv_buf[index++] = flag;

    adv_buf[index++] = 3;
    adv_buf[index++] = 0x03;
    adv_buf[index++] = srv[0];
    adv_buf[index++] = srv[1];

    adv_buf[index++] = (uint8_t)(strlen(adv->name.name) + 1);
    adv_buf[index++] = (adv->name.ntype == QMS_ADV_NAME_SHORT) ? 0x08 : 0x09;
    memcpy(&adv_buf[index], adv->name.name, strlen(adv->name.name));
    index += (uint16_t)strlen(adv->name.name);

    bk_ble_appm_set_dev_name((uint8_t)strlen(adv->name.name), (uint8_t *)adv->name.name);

    qm_ble_gap_set_adv_data_raw(adv_buf, index);
    scan_index = index;

    if (adv->vdata.len > 0) {
        adv_buf[index++] = (uint8_t)(adv->vdata.len + 1);
        adv_buf[index++] = 0xFF;
        memcpy(&adv_buf[index], adv->vdata.data, adv->vdata.len);
        index += adv->vdata.len;
    }

    qm_ble_gap_set_scan_rsp_data_raw(adv_buf + scan_index, index - scan_index);

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.adv_int_min = QM_BLE_ADV_UNIT(50);
    adv_params.adv_int_max = QM_BLE_ADV_UNIT(100);

    if (qm_ble_gap_start_advertising(&adv_params) != QM_EOK) {
        qm_free(adv_buf);
        return QMS_ERR_ADV_FAIL;
    }

    qm_free(adv_buf);
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_advertising_stop(void)
{
    return (qm_ble_gap_stop_advertising() == QM_EOK) ? QMS_ERR_SUCCESS : QMS_ERR_STOP_ADV_FAIL;
}

qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    return (qm_ble_gap_get_local_addr(mac) == QM_EOK) ? QMS_ERR_SUCCESS : QMS_ERR_STACK_FAIL;
}

int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    if (att_mtu == NULL) {
        return -1;
    }

    *att_mtu = QM_BLE_GATT_MTU;
    return 0;
}
