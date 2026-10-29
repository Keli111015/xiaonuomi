/* Avoid the legacy JieLi bool typedef colliding with QUMI's stdbool types. */
#define BOOL_DEFINE_CONFLICT
#include <stdbool.h>

/*
 * qm_ble_hal_ble.c - QUMI BLE GATT HAL for JieLi (WL82/AC79) Platform
 *
 * Uses JieLi BTStack public headers directly. Do NOT re-declare any BTStack
 * functions with extern — most BLE ops are macros defined in ble_api.h.
 *
 * GATT profile glue is provided by hal/ble/qm_ble_gap.c.
 * Callbacks match the real qms_bt_init_t struct from qm_ble_hal_ble.h.
 */

#include "system/includes.h"
#include "system/timer.h"   /* sys_timeout_add, sys_timeout_del */
#include "app_config.h"

/* BTStack public headers (order matters — includes typedef.h internally) */
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "btcontroller_modules.h"
#include "bt_common.h"
#include "btstack/le/le_common_define.h"  /* ATT_CTRL_BLOCK_SIZE */
#include "btstack/le/ble_api.h"           /* BLE_CMD_*, ble_user_cmd_prepare */
#include "btstack/le/att.h"               /* att_server_init, att_get_ccc_config, etc. */
#include "btstack/le/le_user.h"           /* hci_event_callback_set */
#include "btstack/btstack_event.h"        /* att_event_handle_value_indication_complete_* */
#include "ble_user.h"                    /* ble_state_e */
#include "generic/typedef.h"             /* u8/u16 */
#include "syscfg_id.h"                   /* CFG_BT_MAC_ADDR */

/* QUMI headers */
#include "qm_ble_hal_ble.h"
#include "qm_ble_hal_os.h"
#include "qm_log.h"

/* QUMI GATT profile declarations and handle map. The profile bytes are
 * defined by the JieLi glue in hal/ble/qm_ble_gap.c. */
extern const uint8_t profile_data[];

#define ATT_CHARACTERISTIC_2A00_01_VALUE_HANDLE   0x0003
#define QUMI_RC_VALUE_HANDLE                     0x0006
#define QUMI_WC_VALUE_HANDLE                     0x0008
#define QUMI_IC_VALUE_HANDLE                     0x000A
#define QUMI_IC_CCC_HANDLE                       0x000B
#define QUMI_WWNRC_VALUE_HANDLE                  0x000D
#define QUMI_NC_VALUE_HANDLE                     0x000F
#define QUMI_NC_CCC_HANDLE                       0x0010

#include "le_common.h"       /* att_server_init glue */

/* ------------------------------------------------------------------ */
/* QUMI FEB7 profile handle map (the profile bytes are owned by
 * hal/ble/qm_ble_gap.c):
 *   QUMI_RC_VALUE_HANDLE    = 0x0006  (FEA6 Read)
 *   QUMI_WC_VALUE_HANDLE    = 0x0008  (FEA7 Write)
 *   QUMI_IC_VALUE_HANDLE    = 0x000A  (FEA8 Indicate)
 *   QUMI_IC_CCC_HANDLE      = 0x000B  (FEA8 CCC)
 *   QUMI_WWNRC_VALUE_HANDLE = 0x000D  (FEA9 Write-Without-Response)
 *   QUMI_NC_VALUE_HANDLE    = 0x000F  (FEAA Notify)
 *   QUMI_NC_CCC_HANDLE      = 0x0010  (FEAA CCC)
 * Keep these values synchronized with profile_data[] in qm_ble_gap.c.
 * ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* ATT buffer: "att_send need_size >= 6+1+188" when MTU~185 negotiated.
 * Layout: [ATT_CTRL_BLOCK_SIZE | ATT_LOCAL_PAYLOAD_SIZE | ATT_SEND_CBUF_SIZE]
 * Use 247 (max) for payload like ble_rcsp_module to satisfy any MTU.
 * ------------------------------------------------------------------ */
#define ATT_LOCAL_PAYLOAD_SIZE  247   /* max payload, satisfies 6+1+188 */
#define ATT_SEND_CBUF_SIZE      512
#define ATT_RAM_BUFSIZE     (ATT_CTRL_BLOCK_SIZE + ATT_LOCAL_PAYLOAD_SIZE + ATT_SEND_CBUF_SIZE)
static u8 att_ram_buffer[ATT_RAM_BUFSIZE] __attribute__((aligned(4)));

/* ------------------------------------------------------------------ */
/* Internal state                                                       */
/* ------------------------------------------------------------------ */
static qms_bt_init_t     *g_qms_init  = NULL;
static volatile u16       g_con_handle = 0;
static u8                 s_link_connected = 0;
static u8                 s_connected_notified = 0;
/* Logical provisioning ADV. Stays set while a phone is connected so
 * disconnect can reopen advertising, matching the working JL HAL. */
static u8                 s_is_advertising = 0;
/* Pending indication txdone: call when ATT_EVENT_HANDLE_VALUE_INDICATION_COMPLETE */
static void (*s_pending_txdone)(uint8_t res) = NULL;
static u8                 s_indication_pending = 0;

/* ------------------------------------------------------------------ */
/* Prepared Write (Long Write) buffer                                   */
/* When the phone sends a GATT write larger than MTU-3 (e.g. encrypted  */
/* APINFO = 196 bytes on MTU 185), the ATT layer splits it into         */
/* prepared-write chunks.  We accumulate the chunks here and deliver    */
/* the full payload on EXECUTE.                                         */
/* ------------------------------------------------------------------ */
#define PREP_WRITE_BUF_SIZE  512  /* max assembled long write payload */
static struct {
    uint8_t  buf[PREP_WRITE_BUF_SIZE];
    uint16_t len;
    uint16_t handle;   /* ATT handle of the ongoing prepared write */
} s_prep_write = { .len = 0, .handle = 0 };

/* Match the working JL demo: do not update conn params on CONNECT.
 * Request after MTU + 500ms; if the phone rejects, try the next row. */
static const struct conn_update_param_t s_conn_param_table[] = {
    {16, 24, 10, 600},   /* 20–30ms, 6s */
    {12, 28, 10, 600},   /* 15–35ms */
    {8,  20, 10, 600},   /* 10–25ms */
};
#define S_CONN_PARAM_CNT  (sizeof(s_conn_param_table) / sizeof(s_conn_param_table[0]))
#define CONN_PARAM_UPDATE_EN  1
static u8 s_conn_param_idx = 0;
static u16 s_conn_param_timer = 0;
static u8 s_pending_disc_reason = 0x13;
static uint16_t s_att_mtu = ATT_LOCAL_PAYLOAD_SIZE; /* updated on MTU exchange */

/* Implemented by the JieLi BLE stack.  These belong to the low-level
 * profile setup, rather than to an application event path. */
extern void ble_stack_gatt_role(u8 role);
void qm_ble_handle_disconnect(void);

static void cancel_conn_param_timer(void)
{
    if (s_conn_param_timer) {
        sys_timeout_del(s_conn_param_timer);
        s_conn_param_timer = 0;
    }
}

static void request_conn_param_update(hci_con_handle_t conn)
{
#if CONN_PARAM_UPDATE_EN
    if (conn && s_conn_param_idx < S_CONN_PARAM_CNT) {
        struct conn_update_param_t *p = (void *)&s_conn_param_table[s_conn_param_idx];
        ble_user_cmd_prepare(BLE_CMD_REQ_CONN_PARAM_UPDATE, 2, conn, p);
        QM_BLE_LOGI(QM_BLE_LOG_CONTROL,
                    "conn param req #%d min=%u max=%u",
                    s_conn_param_idx, p->interval_min, p->interval_max);
    }
#else
    (void)conn;
#endif
}

static void delayed_conn_param_cb(void *priv)
{
    (void)priv;
    s_conn_param_timer = 0;
    if (g_con_handle) {
        request_conn_param_update(g_con_handle);
    }
}

static int parse_hci_disconnect(const uint8_t *packet, uint16_t size,
                                u16 *handle, u8 *reason)
{
    if (!packet || !handle || !reason || size < 5) {
        return -1;
    }
    /* Standard HCI: [0]=0x05 [1]=len [2]=status [3..4]=handle [5]=reason.
     * JieLi callback size is often 5 (reason byte not included). */
    *handle = (u16)(packet[3] | ((u16)packet[4] << 8));
    *reason = (size >= 6) ? packet[5] : 0x08;
    return 0;
}

/* Detect connection from ATT callbacks when HCI_EVENT_LE_META is not delivered.
 * Calls BLE_CMD_ATT_SEND_INIT here so the ATT buffer is ready before the
 * first CCC write arrives (avoids "att_send need_size" warning on MTU exchange). */
static void ensure_connected(hci_con_handle_t conn)
{
    if (conn && (!s_link_connected || conn != g_con_handle)) {
        g_con_handle = conn;
        s_link_connected = 1;
        s_connected_notified = 0;
        s_conn_param_idx = 0;
        cancel_conn_param_timer();
        ble_user_cmd_prepare(BLE_CMD_ATT_SEND_INIT, 4, g_con_handle,
                             att_ram_buffer, ATT_RAM_BUFSIZE, ATT_LOCAL_PAYLOAD_SIZE);
        QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                    "phone connected handle=0x%04x", g_con_handle);
    }
    if (s_link_connected && !s_connected_notified &&
        g_qms_init && g_qms_init->on_connected) {
        s_connected_notified = 1;
        g_qms_init->on_connected();
    }
}

/* ------------------------------------------------------------------ */
/* ATT Read callback                                                    */
/* ------------------------------------------------------------------ */
static uint16_t att_read_callback(hci_con_handle_t conn, uint16_t att_handle,
                                  uint16_t offset, uint8_t *buffer, uint16_t buffer_size)
{
    (void)offset;
    ensure_connected(conn);

    if (att_handle == QUMI_RC_VALUE_HANDLE) {
        if (g_qms_init && g_qms_init->rc.on_read && buffer) {
            return g_qms_init->rc.on_read(buffer, buffer_size);
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Deliver a complete write payload to the appropriate QUMI handler.   */
/* Used for both normal writes and assembled long writes.               */
/* ------------------------------------------------------------------ */
static void deliver_write(uint16_t att_handle, uint8_t *data, uint16_t len)
{
    if (!g_qms_init || (len != 0 && !data)) return;

    if (att_handle == QUMI_WC_VALUE_HANDLE) {
        if (g_qms_init->wc.on_write)
            g_qms_init->wc.on_write(data, len);
    } else if (att_handle == QUMI_WWNRC_VALUE_HANDLE) {
        if (g_qms_init->wwnrc.on_write)
            g_qms_init->wwnrc.on_write(data, len);
    } else if (att_handle == QUMI_NC_CCC_HANDLE) {
        if (len < 2) return;
        uint16_t cfg = (uint16_t)data[1] << 8 | data[0];
        att_set_ccc_config(att_handle, cfg);
        if (g_qms_init->nc.on_ccc_change)
            g_qms_init->nc.on_ccc_change(cfg);
    } else if (att_handle == QUMI_IC_CCC_HANDLE) {
        if (len < 2) return;
        uint16_t cfg = (uint16_t)data[1] << 8 | data[0];
        att_set_ccc_config(att_handle, cfg);
        if (g_qms_init->ic.on_ccc_change)
            g_qms_init->ic.on_ccc_change(cfg);
    }
}

/* ------------------------------------------------------------------ */
/* ATT Write callback                                                   */
/* ------------------------------------------------------------------ */
static int att_write_callback(hci_con_handle_t conn, uint16_t att_handle,
                              uint16_t transaction_mode, uint16_t offset,
                              uint8_t *buffer, uint16_t buffer_size)
{
    ensure_connected(conn);
    if (!g_qms_init) return 0;

    switch (transaction_mode) {

    case ATT_TRANSACTION_MODE_ACTIVE:
        /* Prepared write chunk — accumulate into buffer */
        if (buffer_size != 0 && !buffer) {
            QM_BLE_LOGW(QM_BLE_LOG_DOWNLINK,
                        "prep write has NULL data");
            break;
        }
        if (offset == 0) {
            /* First chunk: reset buffer, record target handle */
            s_prep_write.len    = 0;
            s_prep_write.handle = att_handle;
        }
        if (s_prep_write.handle == att_handle &&
            offset <= PREP_WRITE_BUF_SIZE &&
            buffer_size <= (PREP_WRITE_BUF_SIZE - offset)) {
            memcpy(s_prep_write.buf + offset, buffer, buffer_size);
            if (offset + buffer_size > s_prep_write.len)
                s_prep_write.len = offset + buffer_size;
        } else {
            QM_BLE_LOGW(QM_BLE_LOG_DOWNLINK,
                        "prep write overflow or handle mismatch");
            s_prep_write.len = 0;
            s_prep_write.handle = 0;
        }
        break;

    case ATT_TRANSACTION_MODE_EXECUTE:
        /* All chunks received — deliver assembled payload */
        if (s_prep_write.len > 0) {
            QM_BLE_LOGI(QM_BLE_LOG_DOWNLINK,
                        "long write complete handle=0x%04x len=%d",
                        s_prep_write.handle, s_prep_write.len);
            deliver_write(s_prep_write.handle, s_prep_write.buf, s_prep_write.len);
            s_prep_write.len    = 0;
            s_prep_write.handle = 0;
        }
        else {
            s_prep_write.handle = 0;
        }
        break;

    case ATT_TRANSACTION_MODE_CANCEL:
        /* Prepared write cancelled — discard buffer */
        s_prep_write.len    = 0;
        s_prep_write.handle = 0;
        break;

    case ATT_TRANSACTION_MODE_VALIDATE:
        /* Validation is separate from delivery.  The QUMI transport must
         * receive a prepared write only after EXECUTE. */
        break;

    case ATT_TRANSACTION_MODE_NONE:
        /* Normal write. */
        if (buffer_size == 0 || buffer) {
            deliver_write(att_handle, buffer, buffer_size);
        }
        break;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* HCI / BTStack packet handler                                         */
/* ------------------------------------------------------------------ */
/* NOT static — registered by the QUMI BLE HAL during profile setup. */
void qm_ble_cbk_packet_handler(uint8_t packet_type, uint16_t channel,
                                uint8_t *packet, uint16_t size)
{
    (void)channel;
    
    if (packet_type != HCI_EVENT_PACKET || !packet || size < 2) return;

    uint8_t hci_evt = hci_event_packet_get_type(packet);

    switch (hci_evt) {

    case HCI_EVENT_LE_META:
        if (size < 4) break;
        switch (hci_event_le_meta_get_subevent_code(packet)) {
        case HCI_SUBEVENT_LE_CONNECTION_COMPLETE:
        case HCI_SUBEVENT_LE_ENHANCED_CONNECTION_COMPLETE: {
            if (size < 6) break;
            u8 status = packet[3];
            if (status == 0) {
                ensure_connected(hci_subevent_le_connection_complete_get_connection_handle(packet));
            } else {
                QM_BLE_LOGE("connection failed status=0x%02x", status);
            }
            break;
        }
        }
        break;

    case HCI_EVENT_DISCONNECTION_COMPLETE: {
        u16 disc_handle = 0;
        u8 reason = 0x08;

        if (parse_hci_disconnect(packet, size, &disc_handle, &reason) != 0) {
            break;
        }
        QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                    "disconnected handle=0x%04x reason=0x%02x",
                    disc_handle, reason);
        if (s_link_connected || (g_con_handle != 0 && g_con_handle == disc_handle) ||
            (g_con_handle != 0 && size < 6)) {
            s_pending_disc_reason = reason;
            qm_ble_handle_disconnect();
        }
        break;
    }

    case ATT_EVENT_HANDLE_VALUE_INDICATION_COMPLETE: {
        if (size < 7) break;
        u16 attr_h = att_event_handle_value_indication_complete_get_attribute_handle(packet);
        if (attr_h == QUMI_IC_VALUE_HANDLE && s_pending_txdone) {
            u8 st = att_event_handle_value_indication_complete_get_status(packet);
            QM_BLE_LOGD(QM_BLE_LOG_UPLINK,
                        "indication ACK handle=0x%04x status=%d %s",
                        attr_h, st, st ? "FAIL" : "OK");
            s_pending_txdone(st ? 1u : 0u);
            s_pending_txdone = NULL;
            s_indication_pending = 0;
        } else if (attr_h == QUMI_IC_VALUE_HANDLE) {
            s_indication_pending = 0;
        }
        break;
    }

    case ATT_EVENT_MTU_EXCHANGE_COMPLETE: {
        if (size < 6) break;
        int att_mtu = att_event_mtu_exchange_complete_get_MTU(packet);
        if (att_mtu < 23) att_mtu = 23;
        if (att_mtu > (ATT_LOCAL_PAYLOAD_SIZE + 3)) {
            att_mtu = ATT_LOCAL_PAYLOAD_SIZE + 3;
        }
        int payload = att_mtu - 3;
        s_att_mtu = (uint16_t)att_mtu;
        ble_user_cmd_prepare(BLE_CMD_ATT_MTU_SIZE, 1, payload);
        if (g_con_handle) {
            ble_user_cmd_prepare(BLE_CMD_ATT_SEND_INIT, 4, g_con_handle,
                                 att_ram_buffer, ATT_RAM_BUFSIZE, ATT_LOCAL_PAYLOAD_SIZE);
        }
        QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                    "MTU exchanged att_mtu=%d payload=%d", att_mtu, payload);
#if CONN_PARAM_UPDATE_EN
        if (s_conn_param_timer == 0 && g_con_handle) {
            s_conn_param_timer = sys_timeout_add(NULL, delayed_conn_param_cb, 500);
        }
#endif
        break;
    }

    case L2CAP_EVENT_CONNECTION_PARAMETER_UPDATE_RESPONSE: {
        if (size < 6) break;
        u16 result = (u16)(packet[4] | (packet[5] << 8));
        QM_BLE_LOGI(QM_BLE_LOG_CONTROL,
                    "conn param update response: %s",
                    result ? "REJECTED" : "ACCEPTED");
        if (result != 0 && g_con_handle) {
            s_conn_param_idx++;
            if (s_conn_param_idx < S_CONN_PARAM_CNT) {
                request_conn_param_update(g_con_handle);
            }
        }
        break;
    }

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Public HAL API                                                       */
/* ------------------------------------------------------------------ */

void qm_ble_hal_event_setup(void)
{
    hci_event_callback_set(qm_ble_cbk_packet_handler);
    le_l2cap_register_packet_handler(qm_ble_cbk_packet_handler);
}

void qm_ble_hal_profile_setup(void)
{
    /* This is the QUMI BLE stack initialization point.  Keeping role and
     * event registration here removes the dependency on bt_music.c events. */
    ble_stack_gatt_role(0);

    /* Register ATT read/write callbacks AND the packet handler together,
     * exactly as the SDK does in every ble_profile_init(). */
    att_server_init(profile_data, att_read_callback, att_write_callback);
    att_server_register_packet_handler(qm_ble_cbk_packet_handler);
    qm_ble_hal_event_setup();
}

qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init)
{
    if (!qms_init) return -QM_EINVAL;
    g_qms_init = qms_init;
    /* The controller can accept a connection before the QUMI service object
     * is created. Complete the deferred connected notification here. */
    if (s_link_connected && !s_connected_notified && g_qms_init->on_connected) {
        s_connected_notified = 1;
        g_qms_init->on_connected();
    }

    /* ATT server and packet handler are already registered in
     * qm_ble_hal_profile_setup() (called from ble_profile_init during BT init).
     * Here we just store the QUMI callback struct. */
    QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                "stack init OK (callbacks registered in profile init)");
    return QM_EOK;
}

/* Called from the BLE compatibility exit hook and from the packet handler. */
void qm_ble_handle_disconnect(void)
{
    if (s_link_connected || g_con_handle) {
        s_indication_pending = 0;
        if (s_pending_txdone) {
            s_pending_txdone(1);
            s_pending_txdone = NULL;
        }
        QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                    "disconnect cleanup handle=0x%04x", g_con_handle);
        g_con_handle = 0;
        s_link_connected = 0;
        s_connected_notified = 0;
        s_conn_param_idx = 0;
        cancel_conn_param_timer();
        s_att_mtu = ATT_LOCAL_PAYLOAD_SIZE;
        s_prep_write.len = 0;
        s_prep_write.handle = 0;
        ble_user_cmd_prepare(BLE_CMD_ATT_SEND_INIT, 4, 0, 0, 0, 0);
        if (g_qms_init && g_qms_init->on_disconnected) {
            g_qms_init->on_disconnected(s_pending_disc_reason);
        }
        if (s_is_advertising) {
            qm_ble_advertising_start(NULL);
        }
    }
}

qms_err_t qm_ble_stack_deinit(void)
{
    qm_ble_advertising_stop();
    cancel_conn_param_timer();
    if (s_pending_txdone) {
        s_pending_txdone(1);
        s_pending_txdone = NULL;
    }
    s_indication_pending = 0;
    s_prep_write.len = 0;
    s_prep_write.handle = 0;
    s_conn_param_idx = 0;
    if (g_con_handle) {
        ble_user_cmd_prepare(BLE_CMD_ATT_SEND_INIT, 4, 0, 0, 0, 0);
    }
    g_qms_init   = NULL;
    g_con_handle = 0;
    s_link_connected = 0;
    s_connected_notified = 0;
    s_att_mtu = ATT_LOCAL_PAYLOAD_SIZE;
    return QM_EOK;
}

qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    if (!g_con_handle || !p_data || length == 0 || length > ATT_LOCAL_PAYLOAD_SIZE) return -QM_ERROR;
    int ret = ble_user_cmd_prepare(BLE_CMD_ATT_SEND_DATA, 4, QUMI_NC_VALUE_HANDLE, p_data, length, ATT_OP_AUTO_READ_CCC);
    return (ret == 0) ? QM_EOK : -QM_ERROR;
}

qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    if (!g_con_handle || !p_data || length == 0 || length > ATT_LOCAL_PAYLOAD_SIZE || s_indication_pending) {
        return -QM_ERROR;
    }
    s_indication_pending = 1;
    s_pending_txdone = txdone;
    int ret = ble_user_cmd_prepare(BLE_CMD_ATT_SEND_DATA, 4, QUMI_IC_VALUE_HANDLE, p_data, length, ATT_OP_AUTO_READ_CCC);
    if (ret != 0) {
        if (txdone) txdone(1);
        s_pending_txdone = NULL;
        s_indication_pending = 0;
        return -QM_ERROR;
    }
    /* txdone will be called when ATT_EVENT_HANDLE_VALUE_INDICATION_COMPLETE arrives */
    return QM_EOK;
}

void qm_ble_disconnect(uint8_t reason)
{
    (void)reason;
    if (g_con_handle) {
        ble_op_disconnect(g_con_handle);
    }
}

static qms_adv_init_t g_qm_adv = {0};
static char g_qm_adv_name[25];

static unsigned char g_adv_buf[31];
static unsigned char g_rsp_buf[31];

qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    if (adv) {
        memcpy(&g_qm_adv, adv, sizeof(qms_adv_init_t));
        memset(g_qm_adv_name, 0, sizeof(g_qm_adv_name));
        if (adv->name.name) {
            size_t copy_len = strlen(adv->name.name);
            if (copy_len >= sizeof(g_qm_adv_name)) copy_len = sizeof(g_qm_adv_name) - 1;
            memcpy(g_qm_adv_name, adv->name.name, copy_len);
        }
    }
    g_qm_adv.name.name = g_qm_adv_name;

    int offset = 0;

    if (s_is_advertising) {
        ble_op_adv_enable(0);
        s_is_advertising = 0;
    }

    /* Advertising interval: 800 * 0.625ms = 500ms */
    if (ble_op_set_adv_param(800, ADV_IND, ADV_CHANNEL_ALL) != 0) {
        return QMS_ERR_ADV_FAIL;
    }

    /* Construct ADV data: Flags + Service UUID + Local Name */
    memset(g_adv_buf, 0, 31);
    u8 adv_flags = 0x06;
    offset += make_eir_packet_val(&g_adv_buf[offset], offset, HCI_EIR_DATATYPE_FLAGS, adv_flags, 1);
    offset += make_eir_packet_val(&g_adv_buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS, 0xFEB7, 2);
    
    int name_len = strlen(g_qm_adv.name.name);
    int name_capacity = (int)sizeof(g_adv_buf) - offset - 2;
    if (name_capacity < 0) name_capacity = 0;
    if (name_len > name_capacity) {
        name_len = name_capacity;
        g_qm_adv.name.ntype = QMS_ADV_NAME_SHORT;
        QM_BLE_LOGW(QM_BLE_LOG_CONTROL,
                    "advertising name truncated to %d bytes", name_len);
    }
    if (name_len > 0) {
        u8 name_type = (g_qm_adv.name.ntype == QMS_ADV_NAME_SHORT || name_len < (int)strlen(g_qm_adv.name.name))
                       ? HCI_EIR_DATATYPE_SHORTENED_LOCAL_NAME : HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME;
        offset += make_eir_packet_data(&g_adv_buf[offset], offset, name_type, (void *)g_qm_adv.name.name, name_len);
    } else {
        const char *default_name = "JL-Qumi";
        offset += make_eir_packet_data(&g_adv_buf[offset], offset, HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME, (void *)default_name, strlen(default_name));
    }
    
    QM_BLE_LOGD(QM_BLE_LOG_PAYLOAD, "ADV data len=%d", offset);
    QM_BLE_LOG_HEX(QM_BLE_LOG_PAYLOAD, "ADV data", g_adv_buf, offset);
    if (ble_op_set_adv_data(offset, g_adv_buf) != 0) {
        return QMS_ERR_ADV_FAIL;
    }

    /* Construct Scan Response: Manufacturer Specific Data (vdata contains PID and MAC) */
    offset = 0;
    memset(g_rsp_buf, 0, 31);
    if (g_qm_adv.vdata.len > 0) {
        uint16_t vdata_len = g_qm_adv.vdata.len;
        if (vdata_len > sizeof(g_qm_adv.vdata.data)) {
            vdata_len = sizeof(g_qm_adv.vdata.data);
        }
        QM_BLE_LOGD(QM_BLE_LOG_PAYLOAD, "vendor data len=%d", vdata_len);
        QM_BLE_LOG_HEX(QM_BLE_LOG_PAYLOAD, "vendor data",
                       g_qm_adv.vdata.data, vdata_len);
        int vdata_capacity = (int)sizeof(g_rsp_buf) - offset - 2;
        if (vdata_capacity < 0) vdata_capacity = 0;
        if (vdata_len > (uint16_t)vdata_capacity) {
            vdata_len = (uint16_t)vdata_capacity;
            QM_BLE_LOGW(QM_BLE_LOG_CONTROL,
                        "advertising vendor data truncated to %d bytes",
                        vdata_len);
        }
        offset += make_eir_packet_data(&g_rsp_buf[offset], offset, HCI_EIR_DATATYPE_MANUFACTURER_SPECIFIC_DATA, g_qm_adv.vdata.data, (u8)vdata_len);
    }
    
    QM_BLE_LOGD(QM_BLE_LOG_PAYLOAD, "scan response data len=%d", offset);
    QM_BLE_LOG_HEX(QM_BLE_LOG_PAYLOAD, "scan response data",
                   g_rsp_buf, offset);
    if (ble_op_set_rsp_data(offset, g_rsp_buf) != 0) {
        return QMS_ERR_ADV_FAIL;
    }

    int err = ble_op_adv_enable(1);
    QM_BLE_LOGI(QM_BLE_LOG_CONTROL,
                "ble_op_adv_enable(1) returned %d", err);
    s_is_advertising = (err == 0);
    if (err != 0) {
        QM_BLE_LOGE("adv start failed: %d", err);
        return QMS_ERR_ADV_FAIL;
    }
    QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                "adv start custom [%s]", g_qm_adv.name.name);
    return QM_EOK;
}

qms_err_t qm_ble_advertising_stop(void)
{
    if (s_is_advertising) {
        /* Use ble_op_adv_enable(0) directly — NOT bt_ble_adv_enable(0).
         * bt_ble_adv_enable() calls advertisements_setup_init() on enable=1,
         * which would overwrite our custom ADV payload. For disable, both
         * call ble_op_adv_enable(0) internally, but using it directly avoids
         * any future changes to the wrapper. */
        int err = ble_op_adv_enable(0);
        s_is_advertising = 0;
        if (err != 0) {
            return QMS_ERR_STOP_ADV_FAIL;
        }
        QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE, "adv stop");
    }
    return QM_EOK;
}

qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    extern const unsigned char *bt_get_mac_addr(void);
    const unsigned char *local_mac;

    if (!mac) {
        return -QM_EINVAL;
    }

    local_mac = bt_get_mac_addr();
    if (!local_mac) {
        return -QM_ERROR;
    }

    memcpy(mac, local_mac, 6);
    return QM_EOK;
}

int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    *att_mtu = s_att_mtu;
    return QM_EOK;
}

// STUB: Missing from QUMI SDK, required for linking when AUTH is enabled
// We place it in the JL platform HAL rather than modifying core SDK files.
int auth_calc_adv_sign(uint32_t seq, uint8_t *sign)
{
    (void)seq;
    if (sign) {
        memset(sign, 0, 4);
    }
    QM_BLE_LOGE("secure advertising sign is not supported");
    return -QM_ERROR;
}
