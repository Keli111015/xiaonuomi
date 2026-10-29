#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"
#include "hci_driver.h"
#include "bluetooth.h"
#include "gatt.h"
#include "ble_lib_api.h"
#include "hci_core.h"
#include <aos/kernel.h>
#include "../../../../../include/aos/list.h"

struct k_delayed_work {
    struct k_work work;
    struct k_work_q *work_q;
    k_timer_t timer;
};

static struct bt_gatt_ccc_cfg ais_ic_ccc_cfg[BT_GATT_CCC_MAX] = {};
static struct bt_gatt_ccc_cfg ais_nc_ccc_cfg[BT_GATT_CCC_MAX] = {};
static bool deinit_ble = false;

static struct bt_conn *g_conn = NULL;
static qms_bt_init_t * bt_init_info = NULL;

static void (*g_indication_txdone)(uint8_t res);
static struct bt_gatt_indicate_params *ind_params;
static struct k_delayed_work ind_work;
static uint8_t ind_data[244] = {0};
static uint16_t ind_len = 0;
static uint8_t pre_vdata_len = 0;
static uint8_t pre_vdata[MAX_VENDOR_DATA_LEN] = {0};

void qm_ble_disconnect(uint8_t reason)
{
    uint8_t zreason = 0;

    if (!g_conn) {
        return;
    }

    switch (reason) {
        case QMS_BT_REASON_REMOTE_USER_TERM_CONN:
            zreason = BT_HCI_ERR_REMOTE_USER_TERM_CONN;
            break;
        default:
            zreason = BT_HCI_ERR_UNSPECIFIED;
            break;
    }

    bt_conn_disconnect(g_conn, zreason);

}


static void connected(struct bt_conn *conn, uint8_t err)
{
    if (err) {
        printf("Connection failed (err %u)\n", err);
    } else {
        printf("Connected\n");
        g_conn = conn;
        if (bt_init_info && (bt_init_info->on_connected)) {
            bt_init_info->on_connected();
        }
    }
}

static void disconnected(struct bt_conn *conn, u8_t reason)
{
    printf("Disconnected (reason %u)\n", reason);
    g_conn = NULL;

    pre_vdata_len = 0;
    memset(pre_vdata, 0, MAX_VENDOR_DATA_LEN);
    if(deinit_ble)
    {
        bt_disable();
        deinit_ble = false;
    }

    if (bt_init_info && (bt_init_info->on_disconnected)) {
        bt_init_info->on_disconnected();
    }
}


static void ais_nc_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                   uint16_t                   value)
{
    qms_ccc_value_t val;

    switch (value) {
        case BT_GATT_CCC_NOTIFY:
            printf("CCC cfg changed to NOTIFY (%d).\r\n", value);
            val = QMS_CCC_VALUE_NOTIFY;
            break;
        default:
            printf("%s CCC cfg changed to %d.\r\n", __func__, value);
            val = QMS_CCC_VALUE_NONE;
            break;
    }

    if (bt_init_info && bt_init_info->nc.on_ccc_change) {
        bt_init_info->nc.on_ccc_change(val);
    }
}

static void ais_ic_ccc_cfg_changed(const struct bt_gatt_attr *attr,
                                   uint16_t                   value)
{
    qms_ccc_value_t val;

    switch (value) {
        case BT_GATT_CCC_INDICATE:
            printf("CCC cfg changed to INDICATE (%d).\r\n", value);
            val = QMS_CCC_VALUE_INDICATE;
            break;
        default:
            printf("%s CCC cfg changed to %d.\r\n", __func__, value);
            val = QMS_CCC_VALUE_NONE;
            break;
    }

    if (bt_init_info && bt_init_info->ic.on_ccc_change) {
        bt_init_info->ic.on_ccc_change(val);
    }
}

static struct bt_conn_cb conn_callbacks = {
    .connected    = connected,
    .disconnected = disconnected,
};

static ssize_t read_ais_rc(struct bt_conn *           conn,
                           const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset)
{
    ssize_t ret = 0;

    printf("%s length to read: %d, offset: %d\r\n", __func__, len, offset);
    if (bt_init_info && bt_init_info->rc.on_read) {
        ret = bt_init_info->rc.on_read(buf, len);
    }

    return ret;
}

static ssize_t read_ais_wc(struct bt_conn *           conn,
                           const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset)
{
    ssize_t ret = 0;

    printf("%s length to read: %d, offset: %d\r\n", __func__, len, offset);
    if (bt_init_info && bt_init_info->wc.on_read) {
        ret = bt_init_info->wc.on_read(buf, len);
    }

    return ret;
}

static ssize_t write_ais_wc(struct bt_conn *           conn,
                            const struct bt_gatt_attr *attr, const void *buf,
                            uint16_t len, uint16_t offset, uint8_t flags)
{
    ssize_t ret = 0;

    if (bt_init_info && bt_init_info->wc.on_write) {
        ret = bt_init_info->wc.on_write(buf, len);
    }

    return ret;
}

static ssize_t read_ais_ic(struct bt_conn *           conn,
                           const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset)
{
    ssize_t ret = 0;

    printf("%s length to read: %d, offset: %d\r\n", __func__, len, offset);
    if (bt_init_info && bt_init_info->ic.on_read) {
        ret = bt_init_info->ic.on_read(buf, len);
    }

    return ret;
}

static ssize_t read_ais_wwnrc(struct bt_conn *           conn,
                              const struct bt_gatt_attr *attr, void *buf,
                              uint16_t len, uint16_t offset)
{
    ssize_t ret = 0;

    printf("%s length to read: %d, offset: %d\r\n", __func__, len, offset);
    if (bt_init_info && bt_init_info->wwnrc.on_read) {
        ret = bt_init_info->wwnrc.on_read(buf, len);
    }

    return ret;
}

static ssize_t write_ais_wwnrc(struct bt_conn *           conn,
                               const struct bt_gatt_attr *attr, const void *buf,
                               uint16_t len, uint16_t offset, uint8_t flags)
{
    ssize_t ret = 0;

    if (bt_init_info && bt_init_info->wwnrc.on_write) {
        ret = bt_init_info->wwnrc.on_write(buf, len);
    }

    return ret;
}

static ssize_t read_ais_nc(struct bt_conn *           conn,
                           const struct bt_gatt_attr *attr, void *buf,
                           uint16_t len, uint16_t offset)
{
    ssize_t ret = 0;

    printf("%s length to read: %d, offset: %d\r\n", __func__, len, offset);
    if (bt_init_info && bt_init_info->nc.on_read) {
        ret = bt_init_info->nc.on_read(buf, len);
    }

    return ret;
}

static struct bt_gatt_attr *  ais_attrs = NULL;
static struct bt_gatt_service ais_svc;

struct bt_uuid *qm_bt_prisvc_uuid = BT_UUID_GATT_PRIMARY;
struct bt_uuid *qm_bt_chrc_uuid   = BT_UUID_GATT_CHRC;
struct bt_uuid *qm_bt_ccc_uuid    = BT_UUID_GATT_CCC;

static int setup_ais_service_attr(struct bt_gatt_attr *attr,
                                  struct bt_uuid *     uuid)
{
    attr->uuid      = qm_bt_prisvc_uuid;
    attr->perm      = BT_GATT_PERM_READ;
    attr->read      = bt_gatt_attr_read_service;
    attr->user_data = (void *)uuid;
    return 0;
}

static int setup_ais_char_attr(struct bt_gatt_attr *attr, struct bt_uuid *uuid,
                               uint8_t prop)
{
    struct bt_gatt_chrc *chrc = NULL;

    chrc = (struct bt_gatt_chrc *)aos_malloc(sizeof(struct bt_gatt_chrc));
    if (!chrc) {
        printf("%s malloc failed\r\n", __func__);
        return -1;
    }

    chrc->uuid       = uuid;
    chrc->properties = prop;
    chrc->value_handle = 0;
    
    attr->uuid      = qm_bt_chrc_uuid;
    attr->perm      = BT_GATT_PERM_READ;
    attr->read      = bt_gatt_attr_read_chrc;
    attr->user_data = (void *)chrc;

    return 0;
}

static int setup_ais_char_desc_attr(struct bt_gatt_attr *attr,
                                    struct bt_uuid *uuid, uint8_t perm,
                                    void *read, void *write, void *userdata)
{
    attr->uuid      = uuid;
    attr->perm      = perm;
    attr->read      = read;
    attr->write     = write;
    attr->user_data = userdata;
    return 0;
}

static int setup_ais_char_ccc_attr(struct bt_gatt_attr *   attr,
                                   struct bt_gatt_ccc_cfg *cfg, size_t cfg_len,
                                   void *cfg_handler)
{
    struct _bt_gatt_ccc *ccc = NULL;

    ccc = (struct _bt_gatt_ccc *)aos_malloc(sizeof(struct _bt_gatt_ccc));
    memset(ccc, 0, sizeof(struct _bt_gatt_ccc));

    if (!ccc) {
        printf("%s malloc failed.\r\n", __func__);
        return -1;
    }
    //ccc->cfg         = cfg;
    //ccc->cfg_len     = cfg_len;
    ccc->cfg_changed = cfg_handler;

    attr->uuid      = qm_bt_ccc_uuid;
    attr->perm      = BT_GATT_PERM_READ | BT_GATT_PERM_WRITE;
    attr->read      = bt_gatt_attr_read_ccc;
    attr->write     = bt_gatt_attr_write_ccc;
    attr->user_data = (void *)ccc;

    return 0;
}

enum
{
    SVC_ATTR_IDX = 0,
    RC_CHRC_ATTR_IDX,
    RC_DESC_ATTR_IDX,
    WC_CHRC_ATTR_IDX,
    WC_DESC_ATTR_IDX,
    IC_CHRC_ATTR_IDX,
    IC_DESC_ATTR_IDX,
    IC_CCC_ATTR_IDX,
    WWNRC_CHRC_ATTR_IDX,
    WWNRC_DESC_ATTR_IDX,
    NC_CHRC_ATTR_IDX,
    NC_DESC_ATTR_IDX,
    NC_CCC_ATTR_IDX,
    /* Add more entry here if necessary */
    AIS_ATTR_NUM,
};

static void bt_enable_cb(int err)
{
}

qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    memcpy(ind_data, p_data, length);
    ind_len = length;
    g_indication_txdone = txdone;
    k_delayed_work_submit(&ind_work, 100);
    return 0; 
}

static void indicate_cb(struct bt_conn *conn, const struct bt_gatt_attr *attr, uint8_t err)
{
    aos_free(ind_params);
    g_indication_txdone(err);
}

static void ble_hal_send_indication(struct k_work *work)
{
    ind_params = aos_malloc(sizeof(struct bt_gatt_indicate_params));
    if (ind_params == NULL) {
        return QMS_ERR_MEM_FAIL;
    }

    ind_params->attr = &ais_attrs[IC_DESC_ATTR_IDX];
    ind_params->func = indicate_cb;
    ind_params->data = ind_data;
    ind_params->len  = ind_len;
    ind_params->uuid = NULL;

    int err = bt_gatt_indicate(g_conn, ind_params);
    if (err) {
	    aos_free(ind_params);
        return QMS_ERR_GATT_INDICATE_FAIL;
    }
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_stack_init(qms_bt_init_t *info)
{
    uint32_t         attr_cnt = AIS_ATTR_NUM, size;
    qms_char_init_t *c;

    deinit_ble = false;

    bt_init_info = info;

#if 0
    ble_controller_init(0);
    hci_driver_init();
    bt_enable(bt_enable_cb);
#endif

    size      = attr_cnt * sizeof(struct bt_gatt_attr);
    ais_attrs = (struct bt_gatt_attr *)aos_malloc(size);
    if (!ais_attrs) {
        printf("%s %d memory allocate failed.\r\n", __func__, __LINE__);
        return QMS_ERR_MEM_FAIL;
    }

    memset(ais_attrs, 0, size);

   /* AIS primary service */
    setup_ais_service_attr(&ais_attrs[SVC_ATTR_IDX], (struct bt_uuid *)info->uuid_svc);

    /* rc */
    c = &(info->rc);
    setup_ais_char_attr(&ais_attrs[RC_CHRC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->prop);
    setup_ais_char_desc_attr(&ais_attrs[RC_DESC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->perm,
                             read_ais_rc, NULL, NULL);

    /* wc */
    c = &(info->wc);
    setup_ais_char_attr(&ais_attrs[WC_CHRC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->prop);
    setup_ais_char_desc_attr(&ais_attrs[WC_DESC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->perm,
                             read_ais_wc, write_ais_wc, NULL);

    /* ic */
    c = &(info->ic);
    setup_ais_char_attr(&ais_attrs[IC_CHRC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->prop);
    setup_ais_char_desc_attr(&ais_attrs[IC_DESC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->perm,
                             read_ais_ic, NULL, NULL);
    setup_ais_char_ccc_attr(&ais_attrs[IC_CCC_ATTR_IDX], ais_ic_ccc_cfg,
                            sizeof(ais_ic_ccc_cfg) / sizeof(ais_ic_ccc_cfg[0]),
                            ais_ic_ccc_cfg_changed);

    /* wwnrc */
    c = &(info->wwnrc);
    setup_ais_char_attr(&ais_attrs[WWNRC_CHRC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->prop);
    setup_ais_char_desc_attr(&ais_attrs[WWNRC_DESC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->perm,
                             read_ais_wwnrc, write_ais_wwnrc, NULL);

    /* nc */
    c = &(info->nc);
    setup_ais_char_attr(&ais_attrs[NC_CHRC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->prop);
    setup_ais_char_desc_attr(&ais_attrs[NC_DESC_ATTR_IDX], (struct bt_uuid *)c->uuid, c->perm,
                             read_ais_nc, NULL, NULL);
    setup_ais_char_ccc_attr(&ais_attrs[NC_CCC_ATTR_IDX], ais_nc_ccc_cfg,
                            sizeof(ais_nc_ccc_cfg) / sizeof(ais_nc_ccc_cfg[0]),
                            ais_nc_ccc_cfg_changed);

    memset(&ais_svc, 0, sizeof(ais_svc));
    ais_svc.attrs      = ais_attrs;
    ais_svc.attr_count = attr_cnt;

    bt_conn_cb_register(&conn_callbacks);
    bt_gatt_service_register(&ais_svc);

    k_delayed_work_init(&ind_work, ble_hal_send_indication);

    return QMS_ERR_SUCCESS;

}

qms_err_t qm_ble_stack_deinit(void)
{
    if (ais_attrs) {
        aos_free(ais_attrs);
    }
    
    if(g_conn)
    {
        deinit_ble = true;
        ble_disconnect(QMS_BT_REASON_REMOTE_USER_TERM_CONN);

    }
    else
    {
        /* Free other memory here when necessary. */
        bt_disable();    /* code */
    }
    
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    return bt_gatt_notify(NULL, &ais_attrs[NC_DESC_ATTR_IDX],
                          (const void *)p_data, length);
}

qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    int            err;
    uint8_t        flag = 0, srv[] = { 0xb7, 0xfe }, ad_len = 3, sd_len = 1;
    struct bt_data ad[ad_len];
    struct bt_data sd[sd_len];
    struct bt_le_adv_param param;

    if((pre_vdata_len == adv->vdata.len) && (memcmp(pre_vdata, adv->vdata.data, adv->vdata.len) == 0))
    {
        printf("%s, return\r\n", __func__);
        return 0;
    } else {
        printf("%s, continue\r\n", __func__);
        pre_vdata_len = adv->vdata.len;
        memcpy(pre_vdata, adv->vdata.data, adv->vdata.len);
        #if 0
	    bt_le_adv_stop();
        #endif
    }

    param.id = 0;
    param.options = (BT_LE_ADV_OPT_CONNECTABLE |BT_LE_ADV_OPT_ONE_TIME);
    param.interval_min = 0x00a0;
    param.interval_max = 0x00f0;
    
    if (adv->flag & QMS_AD_GENERAL) {
        flag |= BT_LE_AD_GENERAL;
    }else if (adv->flag & QMS_AD_NO_BREDR) {
        flag |= BT_LE_AD_NO_BREDR;
    }else if(adv->flag & QMS_AD_LIMITED){
        flag |= BT_LE_AD_LIMITED;
    }

    ad[0].type     = BT_DATA_FLAGS;
    ad[0].data     = &flag;
    ad[0].data_len = 1;

    ad[1].type     = BT_DATA_UUID16_ALL;
    ad[1].data     = srv;
    ad[1].data_len = sizeof(srv);

    if (adv->vdata.len != 0) {
        ad[2].type     = BT_DATA_MANUFACTURER_DATA;
        ad[2].data_len = adv->vdata.len;
        ad[2].data     = adv->vdata.data;
    } else {
        ad_len--;
    }

    switch (adv->name.ntype) {
        case QMS_ADV_NAME_SHORT:
            sd[0].type = BT_DATA_NAME_SHORTENED;
            break;
        case QMS_ADV_NAME_FULL:
            sd[0].type = BT_DATA_NAME_COMPLETE;
            break;
        default:
            printf("Invalid adv name type.\r\n");
            return QMS_ERR_INVALID_ADV_DATA;
    }

    if (adv->name.name == NULL) {
        printf("Invalid adv device name.\r\n");
        return QMS_ERR_INVALID_ADV_DATA;
    }

    sd[0].data     = adv->name.name;
    sd[0].data_len = strlen(adv->name.name);

#if 0
    err = bt_le_adv_start(&param, ad, ad_len, sd, sd_len);
    if (err) {
        printf("Advertising failed to start (err %d)\n", err);
        return QMS_ERR_ADV_FAIL;
    }
#endif

    return 0;
}

qms_err_t qm_ble_advertising_stop(void)
{
    int ret = 0;
#if 0
    ret = bt_le_adv_stop();
#endif
    return ret ? QMS_ERR_STOP_ADV_FAIL : 0;
}

qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    qms_err_t    err;
    bt_addr_le_t laddr;
    int i = 0;

    err = bt_get_local_public_address(&laddr);
    if (err != QMS_ERR_SUCCESS) {
        printf("Failed to get local addr.\r\n");
    } else {
        for(i = 0; i < 6; i++){
            mac[i] = laddr.a.val[5-i];
        }
        printf("Local addr got (%02x:%02x:%02x:%02x:%02x:%02x).\n", mac[0],
               mac[1], mac[2], mac[3], mac[4], mac[5]);
    }

    return err;
}


#ifdef EN_QM_BLE_LONG_MTU
int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    if(att_mtu == NULL || g_conn == NULL){
        BZ_LOG_E("Failed to get ble connection\r\n");
        return -1;
    }
    *att_mtu = bt_gatt_get_mtu(g_conn);
    return 0;
}
#endif



