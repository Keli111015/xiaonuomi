/*
 * NimBLE implementation of QUMI qm_ble_hal_ble (replaces Bluedroid qm_ble_hal_ble.c).
 * For ESP32-P4 + esp-hosted VHCI: CONFIG_BT_NIMBLE_ENABLED=y, Bluedroid off.
 */
#include "qm.h"
#include "qm_ble_hal_ble.h"
#include "qm_ble_bzopt.h"
#include "qm_ble_common.h"
#include "qm_ble_gap.h"
#include "qm_log.h"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
#include "esp_hosted.h"
#endif

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_uuid.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"
#include "host/ble_store.h"

#define TAG "qm_ble_nimble"

#define BT_ADV_SCAN_UNIT(_ms) (((_ms)*8) / 5)

#ifndef BLE_UUID_QMS_SERVICE
#define BLE_UUID_QMS_SERVICE 0xFEB7
#endif

typedef struct {
    qms_bt_init_t *init_param;
    uint16_t conn_handle;
    uint16_t rc_vh;
    uint16_t wc_vh;
    uint16_t ic_vh;
    uint16_t wwnrc_vh;
    uint16_t nc_vh;
    void (*txdone)(uint8_t res);
    uint8_t nimble_up;
    uint8_t adv_active;
} qm_nimble_ctx_t;

static qm_nimble_ctx_t g_nb;
/** Set after nimble host task is up; avoids double nimble_port_deinit. */
static uint8_t s_qm_nimble_stack_ready;

void ble_store_config_init(void);

/* Mirrors Bluedroid hal naming for txdone path */
static struct {
    uint16_t conn_id;
    void (*txdone)(uint8_t res);
} g_ble_hal_ctx;

static const ble_uuid16_t g_uuid_svc = BLE_UUID16_INIT(BLE_UUID_QMS_SERVICE);
static const ble_uuid16_t g_uuid_rc = BLE_UUID16_INIT(BLE_UUID_QMS_RC);
static const ble_uuid16_t g_uuid_wc = BLE_UUID16_INIT(BLE_UUID_QMS_WC);
static const ble_uuid16_t g_uuid_ic = BLE_UUID16_INIT(BLE_UUID_QMS_IC);
static const ble_uuid16_t g_uuid_wwnrc = BLE_UUID16_INIT(BLE_UUID_QMS_WWNRC);
static const ble_uuid16_t g_uuid_nc = BLE_UUID16_INIT(BLE_UUID_QMS_NC);

static int qm_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg);
static int qm_gap_event(struct ble_gap_event *event, void *arg);
static void qm_ble_hs_sync_cb(void);
static void qm_host_task(void *param);

static const struct ble_gatt_svc_def g_qm_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &g_uuid_svc.u,
        .characteristics =
            (struct ble_gatt_chr_def[]){
                {.uuid = &g_uuid_rc.u,
                 .access_cb = qm_gatt_access,
                 .flags = BLE_GATT_CHR_F_READ},
                {.uuid = &g_uuid_wc.u,
                 .access_cb = qm_gatt_access,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE},
                {.uuid = &g_uuid_ic.u,
                 .access_cb = qm_gatt_access,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_INDICATE},
                {.uuid = &g_uuid_wwnrc.u,
                 .access_cb = qm_gatt_access,
                 .flags =
                     BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP},
                {.uuid = &g_uuid_nc.u,
                 .access_cb = qm_gatt_access,
                 .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY},
                {
                    0,
                },
            },
    },
    {
        0,
    },
};

static void qm_register_cb(struct ble_gatt_register_ctxt *ctxt, void *arg)
{
    uint16_t u;

    (void)arg;
    if (ctxt->op != BLE_GATT_REGISTER_OP_CHR) {
        return;
    }
    u = ble_uuid_u16(ctxt->chr.chr_def->uuid);
    switch (u) {
    case BLE_UUID_QMS_RC:
        g_nb.rc_vh = ctxt->chr.val_handle;
        break;
    case BLE_UUID_QMS_WC:
        g_nb.wc_vh = ctxt->chr.val_handle;
        break;
    case BLE_UUID_QMS_IC:
        g_nb.ic_vh = ctxt->chr.val_handle;
        break;
    case BLE_UUID_QMS_WWNRC:
        g_nb.wwnrc_vh = ctxt->chr.val_handle;
        break;
    case BLE_UUID_QMS_NC:
        g_nb.nc_vh = ctxt->chr.val_handle;
        break;
    default:
        break;
    }
}

static void call_on_write(struct os_mbuf *om, on_char_write_t fn)
{
    uint16_t om_len;
    uint8_t tmp[512];

    if (!om || !fn) {
        return;
    }
    om_len = OS_MBUF_PKTLEN(om);
    if (om_len == 0 || om_len > sizeof(tmp)) {
        return;
    }
    if (ble_hs_mbuf_to_flat(om, tmp, sizeof(tmp), &om_len) != 0) {
        return;
    }
    (void)fn(tmp, om_len);
}

static int qm_gatt_access(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    (void)conn_handle;
    (void)attr_handle;
    (void)arg;

    if (g_nb.init_param == NULL) {
        return BLE_ATT_ERR_UNLIKELY;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        if (ble_uuid_cmp(ctxt->chr->uuid, &g_uuid_rc.u) == 0 && g_nb.init_param->rc.on_read) {
            uint8_t buf[32];
            size_t n = g_nb.init_param->rc.on_read(buf, sizeof(buf));
            if (n > 0 && os_mbuf_append(ctxt->om, buf, n) != 0) {
                return BLE_ATT_ERR_INSUFFICIENT_RES;
            }
            return 0;
        }
        return BLE_ATT_ERR_READ_NOT_PERMITTED;
    }

    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        if (ble_uuid_cmp(ctxt->chr->uuid, &g_uuid_wc.u) == 0 && g_nb.init_param->wc.on_write) {
            call_on_write(ctxt->om, g_nb.init_param->wc.on_write);
            return 0;
        }
        if (ble_uuid_cmp(ctxt->chr->uuid, &g_uuid_wwnrc.u) == 0 && g_nb.init_param->wwnrc.on_write) {
            call_on_write(ctxt->om, g_nb.init_param->wwnrc.on_write);
            return 0;
        }
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static void qm_subscribe_dispatch(uint16_t val_handle, uint8_t cur_notify, uint8_t cur_indicate)
{
    if (g_nb.init_param == NULL) {
        return;
    }
    if (val_handle == g_nb.nc_vh && g_nb.init_param->nc.on_ccc_change) {
        if (cur_notify) {
            g_nb.init_param->nc.on_ccc_change(QMS_CCC_VALUE_NOTIFY);
        } else {
            g_nb.init_param->nc.on_ccc_change(QMS_CCC_VALUE_NONE);
        }
    }
    if (val_handle == g_nb.ic_vh && g_nb.init_param->ic.on_ccc_change) {
        if (cur_indicate) {
            g_nb.init_param->ic.on_ccc_change(QMS_CCC_VALUE_INDICATE);
        } else {
            g_nb.init_param->ic.on_ccc_change(QMS_CCC_VALUE_NONE);
        }
    }
}

static int qm_gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status == 0) {
            g_nb.adv_active = 0;
            g_nb.conn_handle = event->connect.conn_handle;
            g_ble_hal_ctx.conn_id = event->connect.conn_handle;
            if (g_nb.init_param && g_nb.init_param->on_connected) {
                g_nb.init_param->on_connected();
            }
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        g_nb.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        g_ble_hal_ctx.conn_id = 0xFFFF;
        if (g_nb.init_param && g_nb.init_param->on_disconnected) {
            g_nb.init_param->on_disconnected();
        }
        return 0;

    case BLE_GAP_EVENT_SUBSCRIBE:
        qm_subscribe_dispatch(event->subscribe.attr_handle, event->subscribe.cur_notify,
                              event->subscribe.cur_indicate);
        return 0;

    case BLE_GAP_EVENT_NOTIFY_TX:
        if (event->notify_tx.indication && g_ble_hal_ctx.txdone) {
            /* For indications, NimBLE sends status=0 when queued, then BLE_HS_EDONE on ack. */
            if (event->notify_tx.status == 0) {
                return 0;
            }
            g_ble_hal_ctx.txdone((event->notify_tx.status == BLE_HS_EDONE) ? QB_SUCCESS : event->notify_tx.status);
            g_ble_hal_ctx.txdone = NULL;
        }
        return 0;

    default:
        return 0;
    }
}

static void qm_ble_hs_reset(int reason)
{
    (void)reason;
    QM_LOGI(TAG, "HS reset reason=%d", reason);
}

static void qm_ble_hs_sync_cb(void)
{
    g_nb.nimble_up = 1;
    QM_LOGI(TAG, "host synced");
}

static void qm_host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

static void qm_wait_hs(void)
{
    int n = 0;
    while (!ble_hs_synced() && n < 500) {
        vTaskDelay(pdMS_TO_TICKS(10));
        n++;
    }
}

qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init)
{
    int rc;

    s_qm_nimble_stack_ready = 0;
    memset(&g_nb, 0, sizeof(g_nb));
    memset(&g_ble_hal_ctx, 0, sizeof(g_ble_hal_ctx));
    g_nb.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    g_nb.init_param = qms_init;

#if CONFIG_ESP_HOSTED_ENABLE_BT_NIMBLE
    if (esp_hosted_connect_to_slave() != ESP_OK) {
        ESP_LOGW(TAG, "esp_hosted_connect_to_slave failed");
    }
#endif

    rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed %d", rc);
        return QMS_ERR_STACK_FAIL;
    }

    ble_hs_cfg.reset_cb = qm_ble_hs_reset;
    ble_hs_cfg.sync_cb = qm_ble_hs_sync_cb;
    ble_hs_cfg.gatts_register_cb = qm_register_cb;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    ble_svc_gap_init();
    ble_svc_gatt_init();

    rc = ble_gatts_count_cfg(g_qm_svcs);
    if (rc != 0) {
        return QMS_ERR_STACK_FAIL;
    }
    rc = ble_gatts_add_svcs(g_qm_svcs);
    if (rc != 0) {
        return QMS_ERR_STACK_FAIL;
    }

    ble_store_config_init();
    nimble_port_freertos_init(qm_host_task);
    qm_wait_hs();
    s_qm_nimble_stack_ready = 1;
    return QM_EOK;
}

qms_err_t qm_ble_stack_deinit(void)
{
    if (!s_qm_nimble_stack_ready) {
        return QMS_ERR_SUCCESS;
    }
    s_qm_nimble_stack_ready = 0;

    g_nb.init_param = NULL;
    g_nb.nimble_up    = 0;

    if (g_nb.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_nb.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    g_nb.adv_active = 0;
    nimble_port_stop();
    vTaskDelay(pdMS_TO_TICKS(200));

    ble_svc_gap_deinit();
    ble_svc_gatt_deinit();

    nimble_port_deinit();
    memset(&g_nb, 0, sizeof(g_nb));
    memset(&g_ble_hal_ctx, 0, sizeof(g_ble_hal_ctx));
    g_nb.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    struct os_mbuf *om;

    if (g_nb.conn_handle == BLE_HS_CONN_HANDLE_NONE || g_nb.nc_vh == 0) {
        return QMS_ERR_INVALID_ADV_DATA;
    }
    om = ble_hs_mbuf_from_flat(p_data, length);
    if (om == NULL) {
        return QMS_ERR_MEM_FAIL;
    }
    if (ble_gatts_notify_custom(g_nb.conn_handle, g_nb.nc_vh, om) != 0) {
        return QMS_ERR_GATT_NOTIFY_FAIL;
    }
    return QM_EOK;
}

qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    struct os_mbuf *om;
    int r;

    if (g_nb.conn_handle == BLE_HS_CONN_HANDLE_NONE || g_nb.ic_vh == 0) {
        return QMS_ERR_INVALID_ADV_DATA;
    }
    g_ble_hal_ctx.txdone = txdone;
    om = ble_hs_mbuf_from_flat(p_data, length);
    if (om == NULL) {
        return QMS_ERR_MEM_FAIL;
    }
    r = ble_gatts_indicate_custom(g_nb.conn_handle, g_nb.ic_vh, om);
    if (r != 0) {
        g_ble_hal_ctx.txdone = NULL;
        return QMS_ERR_GATT_INDICATE_FAIL;
    }
    return QM_EOK;
}

void qm_ble_disconnect(uint8_t reason)
{
    (void)reason;
    if (g_nb.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ble_gap_terminate(g_nb.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    return (qms_err_t)qm_ble_gap_get_local_addr(mac);
}

#ifdef EN_LONG_MTU
int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    if (att_mtu == NULL) {
        return -1;
    }
    if (g_nb.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        *att_mtu = ble_att_mtu(g_nb.conn_handle);
    } else {
        *att_mtu = 23;
    }
    return QM_EOK;
}
#endif

static int build_adv_payload(const qms_adv_init_t *adv, uint8_t *adv_buf, int *adv_len, uint8_t *rsp_buf,
                             int *rsp_len)
{
    uint8_t flag = 0;
    size_t index = 0;
    size_t scan_index;
    const uint8_t srv[] = {0xB7, 0xFE};

    if (!adv || !adv_buf || !rsp_buf || !adv_len || !rsp_len) {
        return -1;
    }

    if (adv->flag & QMS_AD_GENERAL) {
        flag |= 0x02;
    }
    if (adv->flag & QMS_AD_NO_BREDR) {
        flag |= 0x04;
    }
    if (adv->flag & QMS_AD_LIMITED) {
        flag |= 0x01;
    }

    if (index + 3 > 31) {
        return -1;
    }
    adv_buf[index++] = 2;
    adv_buf[index++] = 0x01;
    adv_buf[index++] = flag;

    if (index + 4 > 31) {
        return -1;
    }
    adv_buf[index++] = (uint8_t)(sizeof(uint16_t) + 1);
    adv_buf[index++] = 0x03;
    adv_buf[index++] = srv[0];
    adv_buf[index++] = srv[1];

    if (adv->name.name == NULL) {
        return -1;
    }
    {
        size_t nlen = strlen(adv->name.name);
        if (index + nlen + 2 > 31) {
            return -1;
        }
        adv_buf[index++] = (uint8_t)(nlen + 1);
        adv_buf[index++] = (adv->name.ntype == QMS_ADV_NAME_SHORT) ? 0x08 : 0x09;
        memcpy(&adv_buf[index], adv->name.name, nlen);
        index += nlen;
    }

    scan_index = index;
    *rsp_len = 0;
    if (adv->vdata.len != 0) {
        if (adv->vdata.len + 2 > (size_t)31) {
            return -1;
        }
        rsp_buf[(*rsp_len)++] = (uint8_t)(adv->vdata.len + 1);
        rsp_buf[(*rsp_len)++] = 0xFF;
        memcpy(&rsp_buf[*rsp_len], adv->vdata.data, adv->vdata.len);
        *rsp_len += (int)adv->vdata.len;
    }

    *adv_len = (int)scan_index;
    return 0;
}

qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    uint8_t abuf[31];
    uint8_t rbuf[31];
    int alen = 0;
    int rlen = 0;
    struct ble_gap_adv_params adv_params;
    int rc;

    qm_wait_hs();
    if (!g_nb.nimble_up) {
        return QMS_ERR_ADV_FAIL;
    }

    if (build_adv_payload(adv, abuf, &alen, rbuf, &rlen) != 0) {
        return QMS_ERR_INVALID_ADV_DATA;
    }

    rc = ble_gap_adv_set_data(abuf, alen);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_set_data rc=%d", rc);
        return QMS_ERR_ADV_FAIL;
    }
    rc = ble_gap_adv_rsp_set_data(rlen ? rbuf : NULL, rlen);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_rsp_set_data rc=%d", rc);
        return QMS_ERR_ADV_FAIL;
    }

    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    adv_params.itvl_min = BT_ADV_SCAN_UNIT(50);
    adv_params.itvl_max = BT_ADV_SCAN_UNIT(100);
    adv_params.channel_map = 0x07;

    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER, &adv_params, qm_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_adv_start rc=%d", rc);
        return QMS_ERR_ADV_FAIL;
    }
    g_nb.adv_active = 1;
    if (adv->name.name) {
        ble_svc_gap_device_name_set(adv->name.name);
    }
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_advertising_stop(void)
{
    int rc = 0;
    if(g_nb.adv_active){
        rc = ble_gap_adv_stop();
    }
    g_nb.adv_active = 0;
    return (rc == 0) ? QMS_ERR_SUCCESS : QMS_ERR_STOP_ADV_FAIL;
}
