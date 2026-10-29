/*
 * Linux BLE HAL for QUMI provision:
 * - Advertising via hcitool (RTL8723DS + rtk_hciattach)
 * - GATT via L2CAP ATT CID 4 (minimal ATT server)
 * - Classic piscan to grab shared antenna; rewrite adv after enable (RTK overwrite)
 */
#include "qm_ble_hal_ble.h"

#include "qm.h"
#include "qm_ble_bzopt.h"
#include "qm_ble_gap.h"
#include "qm_log.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/l2cap.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>

#define LOG_TAG "linux_qm_ble"
#define ATT_CID 4
#define ATT_MTU_DEFAULT 23
#define ATT_MTU_MAX 247

#define ATT_OP_ERROR_RSP 0x01
#define ATT_OP_MTU_REQ 0x02
#define ATT_OP_MTU_RSP 0x03
#define ATT_OP_FIND_INFO_REQ 0x04
#define ATT_OP_FIND_INFO_RSP 0x05
#define ATT_OP_READ_BY_TYPE_REQ 0x08
#define ATT_OP_READ_BY_TYPE_RSP 0x09
#define ATT_OP_READ_REQ 0x0A
#define ATT_OP_READ_RSP 0x0B
#define ATT_OP_READ_BY_GROUP_REQ 0x10
#define ATT_OP_READ_BY_GROUP_RSP 0x11
#define ATT_OP_WRITE_REQ 0x12
#define ATT_OP_WRITE_RSP 0x13
#define ATT_OP_WRITE_CMD 0x52
#define ATT_OP_HANDLE_NOTIFY 0x1B
#define ATT_OP_HANDLE_IND 0x1D
#define ATT_OP_HANDLE_CNF 0x1E

#define ATT_ERR_INVALID_HANDLE 0x01
#define ATT_ERR_NOT_SUPPORTED 0x06
#define ATT_ERR_INVALID_OFFSET 0x07
#define ATT_ERR_ATTR_NOT_FOUND 0x0A

#define UUID_PRIMARY_SERVICE 0x2800
#define UUID_CHARACTERISTIC 0x2803
#define UUID_CCC 0x2902

enum {
    H_SVC = 0x0001,
    H_RC_DECL = 0x0002,
    H_RC_VAL = 0x0003,
    H_WC_DECL = 0x0004,
    H_WC_VAL = 0x0005,
    H_IC_DECL = 0x0006,
    H_IC_VAL = 0x0007,
    H_IC_CCC = 0x0008,
    H_WWNRC_DECL = 0x0009,
    H_WWNRC_VAL = 0x000A,
    H_NC_DECL = 0x000B,
    H_NC_VAL = 0x000C,
    H_NC_CCC = 0x000D,
    H_END = 0x000D,
};

typedef struct {
    qms_bt_init_t *init_param;
    pthread_t listen_tid;
    pthread_t refresh_tid;
    int listen_fd;
    int att_fd;
    int running;
    int advertising;
    uint16_t mtu;
    uint16_t ic_ccc;
    uint16_t nc_ccc;
    uint8_t adv_data[32];
    uint8_t adv_len;
    uint8_t scan_data[32];
    uint8_t scan_len;
    void (*ind_txdone)(uint8_t res);
    pthread_mutex_t lock;
} linux_ble_ctx_t;

static linux_ble_ctx_t g_ble;

static int run_shell(const char *cmd)
{
    int ret = system(cmd);
    if (ret != 0) {
        QM_LOGW(LOG_TAG, "cmd fail(%d): %s", ret, cmd);
    }
    return ret;
}

static void put_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xff);
    p[1] = (uint8_t)(v >> 8);
}

static uint16_t get_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int hex_dump_cmd(char *out, size_t out_len, const uint8_t *data, uint8_t len)
{
    size_t n = 0;
    uint8_t i;

    out[0] = '\0';
    for (i = 0; i < len && n + 4 < out_len; i++) {
        int w = snprintf(out + n, out_len - n, "%02X ", data[i]);
        if (w < 0) {
            return -1;
        }
        n += (size_t)w;
    }
    return 0;
}

static int apply_adv_payload(void)
{
    char cmd[512];
    char hex[160];

    /* Grab antenna on RTL8723DS shared path */
    (void)run_shell("hciconfig hci0 piscan >/dev/null 2>&1");
    (void)run_shell("hcitool cmd 0x03 0x006d 01 00 >/dev/null 2>&1");

    (void)run_shell("hcitool cmd 0x08 0x000a 00 >/dev/null 2>&1");
    (void)run_shell("hcitool cmd 0x08 0x0006 20 00 40 00 00 00 00 00 00 00 00 00 00 07 00 >/dev/null 2>&1");

    if (hex_dump_cmd(hex, sizeof(hex), g_ble.adv_data, g_ble.adv_len) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "hcitool cmd 0x08 0x0008 %02X %s >/dev/null 2>&1",
                 g_ble.adv_len, hex);
        (void)run_shell(cmd);
    }
    if (g_ble.scan_len > 0 &&
        hex_dump_cmd(hex, sizeof(hex), g_ble.scan_data, g_ble.scan_len) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "hcitool cmd 0x08 0x0009 %02X %s >/dev/null 2>&1",
                 g_ble.scan_len, hex);
        (void)run_shell(cmd);
    }

    (void)run_shell("hcitool cmd 0x08 0x000a 01 >/dev/null 2>&1");

    /* Fight Realtek overwrite of adv data right after enable */
    usleep(50000);
    if (hex_dump_cmd(hex, sizeof(hex), g_ble.adv_data, g_ble.adv_len) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "hcitool cmd 0x08 0x0008 %02X %s >/dev/null 2>&1",
                 g_ble.adv_len, hex);
        (void)run_shell(cmd);
    }
    if (g_ble.scan_len > 0 &&
        hex_dump_cmd(hex, sizeof(hex), g_ble.scan_data, g_ble.scan_len) == 0) {
        snprintf(cmd, sizeof(cmd),
                 "hcitool cmd 0x08 0x0009 %02X %s >/dev/null 2>&1",
                 g_ble.scan_len, hex);
        (void)run_shell(cmd);
    }
    return 0;
}

static void *adv_refresh_thread(void *arg)
{
    (void)arg;
    while (g_ble.running) {
        sleep(2);
        pthread_mutex_lock(&g_ble.lock);
        if (g_ble.advertising && g_ble.att_fd < 0) {
            char cmd[512];
            char hex[160];
            if (hex_dump_cmd(hex, sizeof(hex), g_ble.adv_data, g_ble.adv_len) == 0) {
                snprintf(cmd, sizeof(cmd),
                         "hcitool cmd 0x08 0x0008 %02X %s >/dev/null 2>&1",
                         g_ble.adv_len, hex);
                (void)run_shell(cmd);
            }
        }
        pthread_mutex_unlock(&g_ble.lock);
    }
    return NULL;
}

static void att_log_pdu(const char *dir, const uint8_t *pdu, size_t len)
{
    char hex[48];
    size_t n = 0;
    size_t i;
    size_t dump = (len > 16) ? 16 : len;

    hex[0] = '\0';
    if (pdu == NULL) {
        QM_LOGI(LOG_TAG, "ATT %s len=%u (null)", dir, (unsigned)len);
        return;
    }
    for (i = 0; i < dump && n + 4 < sizeof(hex); i++) {
        int w = snprintf(hex + n, sizeof(hex) - n, "%02X ", pdu[i]);
        if (w < 0) {
            break;
        }
        n += (size_t)w;
    }
    QM_LOGI(LOG_TAG, "ATT %s op=0x%02X len=%u %s%s",
            dir, pdu[0], (unsigned)len, hex, (len > 16) ? "..." : "");
}

static int att_send(const uint8_t *buf, size_t len)
{
    ssize_t n;

    if (buf == NULL || len < 1) {
        return -1;
    }
    if (g_ble.att_fd < 0) {
        QM_LOGW(LOG_TAG, "ATT tx fail fd<0 op=0x%02X len=%u", buf[0], (unsigned)len);
        return -1;
    }
    n = write(g_ble.att_fd, buf, len);
    if (n != (ssize_t)len) {
        QM_LOGW(LOG_TAG, "ATT tx fail op=0x%02X len=%u n=%d errno=%d %s",
                buf[0], (unsigned)len, (int)n, errno, strerror(errno));
        return -1;
    }
    att_log_pdu("tx", buf, len);
    return 0;
}

static void att_error(uint8_t op, uint16_t handle, uint8_t err)
{
    uint8_t rsp[5];

    rsp[0] = ATT_OP_ERROR_RSP;
    rsp[1] = op;
    put_le16(rsp + 2, handle);
    rsp[4] = err;
    (void)att_send(rsp, sizeof(rsp));
}

static qms_char_init_t *char_by_value_handle(uint16_t handle)
{
    if (g_ble.init_param == NULL) {
        return NULL;
    }
    if (handle == H_RC_VAL) {
        return &g_ble.init_param->rc;
    }
    if (handle == H_WC_VAL) {
        return &g_ble.init_param->wc;
    }
    if (handle == H_IC_VAL) {
        return &g_ble.init_param->ic;
    }
    if (handle == H_WWNRC_VAL) {
        return &g_ble.init_param->wwnrc;
    }
    if (handle == H_NC_VAL) {
        return &g_ble.init_param->nc;
    }
    return NULL;
}

static void handle_mtu_req(const uint8_t *pdu, size_t len)
{
    uint8_t rsp[3];
    uint16_t mtu;

    if (len < 3) {
        return;
    }
    mtu = get_le16(pdu + 1);
    if (mtu < ATT_MTU_DEFAULT) {
        mtu = ATT_MTU_DEFAULT;
    }
    if (mtu > ATT_MTU_MAX) {
        mtu = ATT_MTU_MAX;
    }
    g_ble.mtu = mtu;
    rsp[0] = ATT_OP_MTU_RSP;
    put_le16(rsp + 1, mtu);
    (void)att_send(rsp, sizeof(rsp));
}

static void handle_read_by_group(const uint8_t *pdu, size_t len)
{
    uint16_t start, end, uuid;
    uint8_t rsp[12];

    if (len < 7) {
        return;
    }
    start = get_le16(pdu + 1);
    end = get_le16(pdu + 3);
    uuid = get_le16(pdu + 5);
    if (uuid != UUID_PRIMARY_SERVICE) {
        att_error(ATT_OP_READ_BY_GROUP_REQ, start, ATT_ERR_ATTR_NOT_FOUND);
        return;
    }
    if (start > H_SVC || end < H_SVC) {
        att_error(ATT_OP_READ_BY_GROUP_REQ, start, ATT_ERR_ATTR_NOT_FOUND);
        return;
    }
    rsp[0] = ATT_OP_READ_BY_GROUP_RSP;
    rsp[1] = 6;
    put_le16(rsp + 2, H_SVC);
    put_le16(rsp + 4, H_END);
    put_le16(rsp + 6, BLE_UUID_QMS_SERVICE);
    (void)att_send(rsp, 8);
}

static void handle_read_by_type(const uint8_t *pdu, size_t len)
{
    uint16_t start, end, uuid;
    uint8_t rsp[ATT_MTU_MAX];
    size_t n = 2;
    size_t max_pdu;

    if (len < 7) {
        return;
    }
    start = get_le16(pdu + 1);
    end = get_le16(pdu + 3);
    uuid = get_le16(pdu + 5);
    if (uuid != UUID_CHARACTERISTIC) {
        att_error(ATT_OP_READ_BY_TYPE_REQ, start, ATT_ERR_ATTR_NOT_FOUND);
        return;
    }

    max_pdu = g_ble.mtu ? g_ble.mtu : ATT_MTU_DEFAULT;
    if (max_pdu > sizeof(rsp)) {
        max_pdu = sizeof(rsp);
    }

    rsp[0] = ATT_OP_READ_BY_TYPE_RSP;
    rsp[1] = 7; /* handle(2)+prop(1)+val_handle(2)+uuid16(2) */

    struct {
        uint16_t decl;
        uint8_t prop;
        uint16_t val;
        uint16_t uuid16;
    } chars[] = {
        {H_RC_DECL, QMS_GATT_CHRC_READ, H_RC_VAL, BLE_UUID_QMS_RC},
        {H_WC_DECL, QMS_GATT_CHRC_READ | QMS_GATT_CHRC_WRITE, H_WC_VAL, BLE_UUID_QMS_WC},
        {H_IC_DECL, QMS_GATT_CHRC_INDICATE, H_IC_VAL, BLE_UUID_QMS_IC},
        {H_WWNRC_DECL, QMS_GATT_CHRC_WRITE_WITHOUT_RESP, H_WWNRC_VAL, BLE_UUID_QMS_WWNRC},
        {H_NC_DECL, QMS_GATT_CHRC_NOTIFY, H_NC_VAL, BLE_UUID_QMS_NC},
    };
    size_t i;
    int found = 0;

    for (i = 0; i < sizeof(chars) / sizeof(chars[0]); i++) {
        if (chars[i].decl < start || chars[i].decl > end) {
            continue;
        }
        /* Default ATT MTU is 23. Packing 4 chars = 30 bytes, iOS rejects it. */
        if (n + 7 > max_pdu) {
            break;
        }
        put_le16(rsp + n, chars[i].decl);
        rsp[n + 2] = chars[i].prop;
        put_le16(rsp + n + 3, chars[i].val);
        put_le16(rsp + n + 5, chars[i].uuid16);
        n += 7;
        found = 1;
    }
    if (!found) {
        att_error(ATT_OP_READ_BY_TYPE_REQ, start, ATT_ERR_ATTR_NOT_FOUND);
        return;
    }
    QM_LOGI(LOG_TAG, "ATT read_by_type mtu=%u packed=%u",
            (unsigned)max_pdu, (unsigned)n);
    (void)att_send(rsp, n);
}

static void handle_find_info(const uint8_t *pdu, size_t len)
{
    uint16_t start, end;
    uint8_t rsp[16];
    size_t n = 2;

    if (len < 5) {
        return;
    }
    start = get_le16(pdu + 1);
    end = get_le16(pdu + 3);
    rsp[0] = ATT_OP_FIND_INFO_RSP;
    rsp[1] = 0x01; /* 16-bit UUID format */

    if (start <= H_IC_CCC && end >= H_IC_CCC) {
        put_le16(rsp + n, H_IC_CCC);
        put_le16(rsp + n + 2, UUID_CCC);
        n += 4;
    }
    if (start <= H_NC_CCC && end >= H_NC_CCC) {
        put_le16(rsp + n, H_NC_CCC);
        put_le16(rsp + n + 2, UUID_CCC);
        n += 4;
    }
    if (n == 2) {
        att_error(ATT_OP_FIND_INFO_REQ, start, ATT_ERR_ATTR_NOT_FOUND);
        return;
    }
    (void)att_send(rsp, n);
}

static void handle_read(const uint8_t *pdu, size_t len)
{
    uint16_t handle;
    uint8_t rsp[ATT_MTU_MAX];
    qms_char_init_t *ch;
    size_t vlen = 0;

    if (len < 3) {
        return;
    }
    handle = get_le16(pdu + 1);
    rsp[0] = ATT_OP_READ_RSP;

    if (handle == H_IC_CCC) {
        put_le16(rsp + 1, g_ble.ic_ccc);
        (void)att_send(rsp, 3);
        return;
    }
    if (handle == H_NC_CCC) {
        put_le16(rsp + 1, g_ble.nc_ccc);
        (void)att_send(rsp, 3);
        return;
    }

    ch = char_by_value_handle(handle);
    if (ch == NULL || ch->on_read == NULL) {
        att_error(ATT_OP_READ_REQ, handle, ATT_ERR_INVALID_HANDLE);
        return;
    }
    vlen = ch->on_read(rsp + 1, (uint16_t)(g_ble.mtu - 1));
    (void)att_send(rsp, 1 + vlen);
}

static void handle_write(const uint8_t *pdu, size_t len, int with_rsp)
{
    uint16_t handle;
    const uint8_t *value;
    size_t vlen;
    qms_char_init_t *ch;

    if (len < 3) {
        return;
    }
    handle = get_le16(pdu + 1);
    value = pdu + 3;
    vlen = len - 3;

    if (handle == H_IC_CCC || handle == H_NC_CCC) {
        uint16_t ccc = (vlen >= 2) ? get_le16(value) : 0;
        qms_ccc_value_t cv = QMS_CCC_VALUE_NONE;

        if (ccc == 0x0001) {
            cv = QMS_CCC_VALUE_NOTIFY;
        } else if (ccc == 0x0002) {
            cv = QMS_CCC_VALUE_INDICATE;
        }
        if (handle == H_IC_CCC) {
            g_ble.ic_ccc = ccc;
            if (g_ble.init_param && g_ble.init_param->ic.on_ccc_change) {
                g_ble.init_param->ic.on_ccc_change(cv);
            }
        } else {
            g_ble.nc_ccc = ccc;
            if (g_ble.init_param && g_ble.init_param->nc.on_ccc_change) {
                g_ble.init_param->nc.on_ccc_change(cv);
            }
        }
        if (with_rsp) {
            uint8_t rsp = ATT_OP_WRITE_RSP;
            (void)att_send(&rsp, 1);
        }
        return;
    }

    ch = char_by_value_handle(handle);
    if (ch == NULL || ch->on_write == NULL) {
        if (with_rsp) {
            att_error(ATT_OP_WRITE_REQ, handle, ATT_ERR_INVALID_HANDLE);
        }
        return;
    }
    (void)ch->on_write((void *)value, (uint16_t)vlen);
    if (with_rsp) {
        uint8_t rsp = ATT_OP_WRITE_RSP;
        (void)att_send(&rsp, 1);
    }
}

static void process_att(const uint8_t *pdu, size_t len)
{
    if (len < 1) {
        QM_LOGW(LOG_TAG, "ATT rx empty");
        return;
    }
    att_log_pdu("rx", pdu, len);
    switch (pdu[0]) {
    case ATT_OP_MTU_REQ:
        handle_mtu_req(pdu, len);
        break;
    case ATT_OP_READ_BY_GROUP_REQ:
        handle_read_by_group(pdu, len);
        break;
    case ATT_OP_READ_BY_TYPE_REQ:
        handle_read_by_type(pdu, len);
        break;
    case ATT_OP_FIND_INFO_REQ:
        handle_find_info(pdu, len);
        break;
    case ATT_OP_READ_REQ:
        handle_read(pdu, len);
        break;
    case ATT_OP_WRITE_REQ:
        handle_write(pdu, len, 1);
        break;
    case ATT_OP_WRITE_CMD:
        handle_write(pdu, len, 0);
        break;
    case ATT_OP_HANDLE_CNF:
        if (g_ble.ind_txdone) {
            void (*cb)(uint8_t) = g_ble.ind_txdone;
            g_ble.ind_txdone = NULL;
            cb(0);
        }
        break;
    default:
        att_error(pdu[0], 0x0000, ATT_ERR_NOT_SUPPORTED);
        break;
    }
}

static void *att_listen_thread(void *arg)
{
    struct sockaddr_l2 addr;
    struct sockaddr_l2 peer;
    socklen_t peer_len;

    (void)arg;

    g_ble.listen_fd = socket(PF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (g_ble.listen_fd < 0) {
        QM_LOGE(LOG_TAG, "l2cap socket: %s", strerror(errno));
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.l2_family = AF_BLUETOOTH;
    addr.l2_cid = htobs(ATT_CID);
    addr.l2_bdaddr_type = BDADDR_LE_PUBLIC;
    bacpy(&addr.l2_bdaddr, BDADDR_ANY);

    if (bind(g_ble.listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        QM_LOGE(LOG_TAG, "l2cap bind: %s", strerror(errno));
        close(g_ble.listen_fd);
        g_ble.listen_fd = -1;
        return NULL;
    }
    if (listen(g_ble.listen_fd, 1) < 0) {
        QM_LOGE(LOG_TAG, "l2cap listen: %s", strerror(errno));
        close(g_ble.listen_fd);
        g_ble.listen_fd = -1;
        return NULL;
    }

    QM_LOGI(LOG_TAG, "ATT listen ready (CID 4)");

    while (g_ble.running) {
        uint8_t buf[ATT_MTU_MAX];
        ssize_t n;

        peer_len = sizeof(peer);
        g_ble.att_fd = accept(g_ble.listen_fd, (struct sockaddr *)&peer, &peer_len);
        if (g_ble.att_fd < 0) {
            if (!g_ble.running) {
                break;
            }
            continue;
        }

        g_ble.mtu = ATT_MTU_DEFAULT;
        g_ble.ic_ccc = 0;
        g_ble.nc_ccc = 0;
        {
            const uint8_t *b = peer.l2_bdaddr.b;

            QM_LOGI(LOG_TAG, "BLE connected peer=%02X:%02X:%02X:%02X:%02X:%02X type=%u cid=%u",
                    b[5], b[4], b[3], b[2], b[1], b[0],
                    (unsigned)peer.l2_bdaddr_type,
                    (unsigned)btohs(peer.l2_cid));
        }
        if (g_ble.init_param && g_ble.init_param->on_connected) {
            g_ble.init_param->on_connected();
        }

        while (g_ble.running) {
            n = read(g_ble.att_fd, buf, sizeof(buf));
            if (n <= 0) {
                QM_LOGI(LOG_TAG, "ATT read n=%d errno=%d %s",
                        (int)n, errno, strerror(errno));
                break;
            }
            process_att(buf, (size_t)n);
        }

        close(g_ble.att_fd);
        g_ble.att_fd = -1;
        QM_LOGI(LOG_TAG, "BLE disconnected");
        if (g_ble.init_param && g_ble.init_param->on_disconnected) {
            g_ble.init_param->on_disconnected();
        }
    }

    return NULL;
}

qms_err_t qm_ble_stack_init(qms_bt_init_t *qms_init)
{
    uint8_t mac[6];

    if (qms_init == NULL) {
        return QMS_ERR_STACK_FAIL;
    }

    if (qm_ble_gap_get_local_addr(mac) != QM_EOK) {
        QM_LOGE(LOG_TAG, "hci0 not ready");
        return QMS_ERR_STACK_FAIL;
    }

    memset(&g_ble, 0, sizeof(g_ble));
    g_ble.init_param = qms_init;
    g_ble.listen_fd = -1;
    g_ble.att_fd = -1;
    g_ble.mtu = ATT_MTU_DEFAULT;
    g_ble.running = 1;
    pthread_mutex_init(&g_ble.lock, NULL);

    if (pthread_create(&g_ble.listen_tid, NULL, att_listen_thread, NULL) != 0) {
        g_ble.running = 0;
        return QMS_ERR_STACK_FAIL;
    }
    if (pthread_create(&g_ble.refresh_tid, NULL, adv_refresh_thread, NULL) != 0) {
        QM_LOGW(LOG_TAG, "adv refresh thread create failed");
    }

    QM_LOGI(LOG_TAG, "stack init ok");
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_stack_deinit(void)
{
    g_ble.running = 0;
    g_ble.advertising = 0;
    (void)run_shell("hcitool cmd 0x08 0x000a 00 >/dev/null 2>&1");
    if (g_ble.att_fd >= 0) {
        close(g_ble.att_fd);
        g_ble.att_fd = -1;
    }
    if (g_ble.listen_fd >= 0) {
        close(g_ble.listen_fd);
        g_ble.listen_fd = -1;
    }
    pthread_join(g_ble.listen_tid, NULL);
    pthread_join(g_ble.refresh_tid, NULL);
    pthread_mutex_destroy(&g_ble.lock);
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_send_notification(uint8_t *p_data, uint16_t length)
{
    uint8_t buf[ATT_MTU_MAX];

    if (p_data == NULL || g_ble.att_fd < 0 || !(g_ble.nc_ccc & 0x0001)) {
        return QMS_ERR_GATT_NOTIFY_FAIL;
    }
    if (length + 3 > g_ble.mtu) {
        length = g_ble.mtu - 3;
    }
    buf[0] = ATT_OP_HANDLE_NOTIFY;
    put_le16(buf + 1, H_NC_VAL);
    memcpy(buf + 3, p_data, length);
    return att_send(buf, 3 + length) == 0 ? QMS_ERR_SUCCESS : QMS_ERR_GATT_NOTIFY_FAIL;
}

qms_err_t qm_ble_send_indication(uint8_t *p_data, uint16_t length, void (*txdone)(uint8_t res))
{
    uint8_t buf[ATT_MTU_MAX];

    if (p_data == NULL || g_ble.att_fd < 0 || !(g_ble.ic_ccc & 0x0002)) {
        return QMS_ERR_GATT_INDICATE_FAIL;
    }
    if (length + 3 > g_ble.mtu) {
        length = g_ble.mtu - 3;
    }
    g_ble.ind_txdone = txdone;
    buf[0] = ATT_OP_HANDLE_IND;
    put_le16(buf + 1, H_IC_VAL);
    memcpy(buf + 3, p_data, length);
    if (att_send(buf, 3 + length) != 0) {
        g_ble.ind_txdone = NULL;
        return QMS_ERR_GATT_INDICATE_FAIL;
    }
    return QMS_ERR_SUCCESS;
}

void qm_ble_disconnect(uint8_t reason)
{
    (void)reason;
    if (g_ble.att_fd >= 0) {
        close(g_ble.att_fd);
        g_ble.att_fd = -1;
    }
}

qms_err_t qm_ble_advertising_start(qms_adv_init_t *adv)
{
    uint8_t *p;
    size_t name_len;

    if (adv == NULL || adv->name.name == NULL) {
        return QMS_ERR_INVALID_ADV_DATA;
    }

    name_len = strlen(adv->name.name);
    if (name_len > 18) {
        name_len = 18;
    }

    /* Adv: Flags + UUID FEB7 + Complete Name */
    p = g_ble.adv_data;
    p[0] = 2;
    p[1] = 0x01;
    p[2] = 0x06;
    p[3] = 3;
    p[4] = 0x03;
    p[5] = 0xB7;
    p[6] = 0xFE;
    p[7] = (uint8_t)(1 + name_len);
    p[8] = 0x09;
    memcpy(p + 9, adv->name.name, name_len);
    g_ble.adv_len = (uint8_t)(9 + name_len);

    /* Scan rsp: manufacturer specific */
    g_ble.scan_len = 0;
    if (adv->vdata.len > 0 && adv->vdata.len <= MAX_VENDOR_DATA_LEN) {
        g_ble.scan_data[0] = (uint8_t)(adv->vdata.len + 1);
        g_ble.scan_data[1] = 0xFF;
        memcpy(g_ble.scan_data + 2, adv->vdata.data, adv->vdata.len);
        g_ble.scan_len = (uint8_t)(2 + adv->vdata.len);
    }

    pthread_mutex_lock(&g_ble.lock);
    g_ble.advertising = 1;
    apply_adv_payload();
    pthread_mutex_unlock(&g_ble.lock);

    QM_LOGI(LOG_TAG, "adv start name=%s", adv->name.name);
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_advertising_stop(void)
{
    pthread_mutex_lock(&g_ble.lock);
    g_ble.advertising = 0;
    (void)run_shell("hcitool cmd 0x08 0x000a 00 >/dev/null 2>&1");
    pthread_mutex_unlock(&g_ble.lock);
    return QMS_ERR_SUCCESS;
}

qms_err_t qm_ble_get_mac(uint8_t *mac)
{
    if (mac == NULL) {
        return QMS_ERR_STACK_FAIL;
    }
    return (qm_ble_gap_get_local_addr(mac) == QM_EOK) ?
           QMS_ERR_SUCCESS : QMS_ERR_STACK_FAIL;
}

#ifdef EN_LONG_MTU
int qm_ble_get_att_mtu(uint16_t *att_mtu)
{
    if (att_mtu == NULL) {
        return -QM_EINVAL;
    }
    *att_mtu = g_ble.mtu ? g_ble.mtu : ATT_MTU_MAX;
    return QM_EOK;
}
#endif
