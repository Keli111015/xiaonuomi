/*
 * qm_ble_gap.c - QUMI BLE GAP HAL for JieLi (WL82/AC79) Platform
 *
 * Maps QUMI GAP operations to JieLi BTStack ble_op_* macros.
 * Reference: esp32_combo/qumi/platform/mcu/esp32/hal/ble/qm_ble_gap.c
 *
 * Key JieLi BLE API (all macros in btstack/le/ble_api.h):
 *   ble_op_set_adv_param(interval, adv_type, channel_map)
 *   ble_op_set_adv_data(len, data)    - set advertising data
 *   ble_op_set_rsp_data(len, data)    - set scan response data
 *   ble_op_adv_enable(en)             - start/stop advertising
 *
 * All JieLi BLE operations are synchronous, so completion callbacks
 * are fired immediately after the operation.
 *
 * NOTE: BLE scanning (qm_ble_gap_start_scanning etc.) is not needed
 * for provisioning where the device acts as a peripheral/advertiser.
 */

#define BOOL_DEFINE_CONFLICT

/* QUMI defines bool through <stdbool.h>; make it available before the
 * JieLi headers while suppressing their legacy bool typedef. */
#include <stdbool.h>
#include "qm_ble_gap.h"
#include "qm_ble_log.h"

/*
 * ADV_IND and ADV_CHANNEL_ALL are defined in btstack/le/le_common_define.h.
 * bt_common.h must come first (provides base BT types).
 */
#include "system/includes.h"
#include "init.h"
#include "app_config.h"
#include "btstack/btstack_task.h"
#include "btstack/bluetooth.h"
#include "third_party/common/ble_user.h"
#include "btstack/le/sm.h"
#include "btstack/le/le_user.h"
#include "btcontroller_modules.h"
#include "ble/hci_ll.h"
#include "bt_common.h"
#include "btstack/le/le_common_define.h" /* ADV_IND, ADV_CHANNEL_ALL */
#include "btstack/le/ble_api.h"          /* ble_op_set_adv_data/rsp_data/param, ble_op_adv_enable */
#include "generic/typedef.h"             /* u8, u16 */
#include "qm_errno.h"

#if defined CONFIG_BT_ENABLE
static int qm_jl_btstack_init(void)
{
    return btstack_init();
}

late_initcall(qm_jl_btstack_init);
#endif

/* The SDK exposes the local BR/EDR name implementation from user_cfg.c but
 * does not publish its declaration in the BLE headers. */
extern const char *bt_get_local_name(void);

/* GAP event callback registered by QUMI core */
static qm_gap_ble_cb_t s_gap_cb = NULL;

/* ------------------------------------------------------------------ */
/* Callback registration                                               */
/* ------------------------------------------------------------------ */

int qm_ble_gap_register_callback(qm_gap_ble_cb_t callback)
{
    s_gap_cb = callback;
    return 0;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    (void)callback;
    s_gap_cb = NULL;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Advertising data                                                    */
/* ------------------------------------------------------------------ */

/**
 * Set structured advertising data.
 * qm_ble_advertising_start() (in qm_ble_hal_ble.c) builds and sets the
 * full ADV packet directly, so the generic structured GAP entry point is not
 * supported on this platform.
 */
int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    (void)adv_data;
    /* Structured GAP data is assembled by qm_ble_advertising_start().
     * Returning success here would silently discard the caller's payload. */
    return -QM_EINVAL;
}

/**
 * Set raw advertising payload.
 * Called by qm_ble_advertising_start() on ESP32 reference; on JL we
 * handle this by calling ble_op_set_adv_data directly, then fire the
 * completion callback synchronously (JL BLE ops are synchronous).
 */
int qm_ble_gap_set_adv_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    if (!raw_data || raw_data_len == 0 || raw_data_len > 31) {
        return -QM_EINVAL;
    }
    if (ble_op_set_adv_data((u8)raw_data_len, raw_data) != 0) {
        return -QM_ERROR;
    }
    if (s_gap_cb) {
        s_gap_cb(QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, NULL);
    }
    return 0;
}

/**
 * Set raw scan response payload.
 */
int qm_ble_gap_set_scan_rsp_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    if (!raw_data || raw_data_len == 0 || raw_data_len > 31) {
        return -QM_EINVAL;
    }
    if (ble_op_set_rsp_data((u8)raw_data_len, raw_data) != 0) {
        return -QM_ERROR;
    }
    if (s_gap_cb) {
        s_gap_cb(QM_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT, NULL);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Advertising control                                                 */
/* ------------------------------------------------------------------ */

/**
 * Start advertising.
 * Uses adv_int_min from params as the advertising interval (JieLi unit:
 * 0.625 ms, same as Bluetooth spec). Falls back to 800 (500 ms) if NULL.
 *
 * NOTE: Do NOT call bt_ble_adv_enable(1) here — that wrapper calls
 * advertisements_setup_init() which would overwrite the raw ADV data
 * already staged via qm_ble_gap_set_adv_data_raw.
 */
int qm_ble_gap_start_advertising(qm_ble_adv_params_t *adv_params)
{
    u16 interval = 800; /* default 500 ms */
    if (adv_params && adv_params->adv_int_min) {
        interval = (u16)adv_params->adv_int_min;
    }
    if (interval < 0x20 || interval > 0x4000 ||
        (adv_params && adv_params->adv_int_max &&
         adv_params->adv_int_max < interval)) {
        return -QM_EINVAL;
    }
    if (ble_op_set_adv_param(interval, ADV_IND, ADV_CHANNEL_ALL) != 0) {
        return -QM_ERROR;
    }
    if (ble_op_adv_enable(1) != 0) {
        return -QM_ERROR;
    }
    if (s_gap_cb) {
        s_gap_cb(QM_GAP_BLE_ADV_START_COMPLETE_EVT, NULL);
    }
    return 0;
}

/**
 * Stop advertising.
 */
int qm_ble_gap_stop_advertising(void)
{
    if (ble_op_adv_enable(0) != 0) {
        return -QM_ERROR;
    }
    if (s_gap_cb) {
        s_gap_cb(QM_GAP_BLE_ADV_STOP_COMPLETE_EVT, NULL);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Scanning (not needed for peripheral-only provisioning)              */
/* ------------------------------------------------------------------ */

int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    (void)scan_params;
    return -1; /* Not supported — device is peripheral only */
}

int qm_ble_gap_start_scanning(uint32_t duration)
{
    (void)duration;
    return -1;
}

int qm_ble_gap_stop_scanning(void)
{
    return -1;
}

/* ------------------------------------------------------------------ */
/* Local address                                                       */
/* ------------------------------------------------------------------ */

int qm_ble_gap_get_local_addr(qm_bd_addr_t addr)
{
    extern const unsigned char *bt_get_mac_addr(void);
    const unsigned char *mac;

    if (!addr) {
        return -1;
    }

    mac = bt_get_mac_addr();
    if (!mac) {
        memset(addr, 0, 6);
        return -1;
    }

    memcpy(addr, mac, 6);
    return 0;
}

// -------------------------------------------------------------------------
// QUMI GATT profile_data[] - binary GATT database
// DEFINED HERE (single definition). qm_ble_hal_ble.c references it as extern.
// ble_get_gatt_profile_data() below returns it to the JL BTStack.
// -------------------------------------------------------------------------
const uint8_t profile_data[] = {
    // 0x0001 PRIMARY_SERVICE 0x1800 (GAP)
    0x0a, 0x00, 0x02, 0x00, 0x01, 0x00, 0x00, 0x28, 0x00, 0x18,
    // 0x0002 CHARACTERISTIC 2A00 READ | DYNAMIC
    0x0d, 0x00, 0x02, 0x00, 0x02, 0x00, 0x03, 0x28, 0x02, 0x03, 0x00, 0x00, 0x2a,
    // 0x0003 VALUE 2A00 READ | DYNAMIC
    0x08, 0x00, 0x02, 0x01, 0x03, 0x00, 0x00, 0x2a,

    // 0x0004 PRIMARY_SERVICE 0xFEB7 (QUMI IoT Service)
    0x0a, 0x00, 0x02, 0x00, 0x04, 0x00, 0x00, 0x28, 0xB7, 0xFE,

    // 0x0005 CHARACTERISTIC FEA6 READ | DYNAMIC  (RC)
    0x0d, 0x00, 0x02, 0x00, 0x05, 0x00, 0x03, 0x28, 0x02, 0x06, 0x00, 0xA6, 0xFE,
    // 0x0006 VALUE FEA6 READ | DYNAMIC
    0x08, 0x00, 0x02, 0x01, 0x06, 0x00, 0xA6, 0xFE,

    // 0x0007 CHARACTERISTIC FEA7 WRITE | DYNAMIC  (WC)
    0x0d, 0x00, 0x02, 0x00, 0x07, 0x00, 0x03, 0x28, 0x08, 0x08, 0x00, 0xA7, 0xFE,
    // 0x0008 VALUE FEA7 WRITE | DYNAMIC
    0x08, 0x00, 0x08, 0x01, 0x08, 0x00, 0xA7, 0xFE,

    // 0x0009 CHARACTERISTIC FEA8 INDICATE | DYNAMIC  (IC)
    0x0d, 0x00, 0x02, 0x00, 0x09, 0x00, 0x03, 0x28, 0x20, 0x0a, 0x00, 0xA8, 0xFE,
    // 0x000A VALUE FEA8 INDICATE | DYNAMIC
    0x08, 0x00, 0x20, 0x00, 0x0a, 0x00, 0xA8, 0xFE,
    // 0x000B CCC (FEA8)
    0x0a, 0x00, 0x0a, 0x01, 0x0b, 0x00, 0x02, 0x29, 0x00, 0x00,

    // 0x000C CHARACTERISTIC FEA9 WRITE_WITHOUT_RESPONSE | DYNAMIC  (WWNRC)
    0x0d, 0x00, 0x02, 0x00, 0x0c, 0x00, 0x03, 0x28, 0x04, 0x0d, 0x00, 0xA9, 0xFE,
    // 0x000D VALUE FEA9 WRITE_WITHOUT_RESPONSE | DYNAMIC
    0x08, 0x00, 0x04, 0x01, 0x0d, 0x00, 0xA9, 0xFE,

    // 0x000E CHARACTERISTIC FEAA NOTIFY | DYNAMIC  (NC)
    0x0d, 0x00, 0x02, 0x00, 0x0e, 0x00, 0x03, 0x28, 0x10, 0x0f, 0x00, 0xAA, 0xFE,
    // 0x000F VALUE FEAA NOTIFY | DYNAMIC
    0x08, 0x00, 0x10, 0x00, 0x0f, 0x00, 0xAA, 0xFE,
    // 0x0010 CCC (FEAA)
    0x0a, 0x00, 0x0a, 0x01, 0x10, 0x00, 0x02, 0x29, 0x00, 0x00,

    // END
    0x00, 0x00,
};

/*
 * ble_get_gatt_profile_data() - REQUIRED by JL BTStack.
 *
 * The BTStack calls this function when a BLE connection is established to
 * obtain the GATT profile binary. Without this function the linker uses a
 * weak default that returns NULL, causing an axi_rd_inv crash when the ATT
 * layer tries to walk the profile table on the first connection.
 *
 * SDK reference: JieLi standard BLE transfer-data profile implementation.
 */
u8 *ble_get_gatt_profile_data(u16 *len)
{
    *len = (u16)sizeof(profile_data);
    return (u8 *)profile_data;
}

#if (TCFG_BLE_DEMO_SELECT == DEF_BLE_DEMO_TRANS_DATA)

/* MTU for BLE ATT layer.
 *
 * Keep this overridable for Android/iOS interoperability A/B tests. 185 is
 * the iOS-like baseline; pass -DQM_BLE_ATT_LOCAL_MTU_SIZE=200 to restore the
 * previous JieLi default without editing this file.
 */
#ifndef QM_BLE_ATT_LOCAL_MTU_SIZE
#define QM_BLE_ATT_LOCAL_MTU_SIZE 200
#endif
#define ATT_LOCAL_MTU_SIZE QM_BLE_ATT_LOCAL_MTU_SIZE

/* Called by JieLi BLE task after BTStack is ready */
extern void qm_ble_hal_profile_setup(void);

extern void le_device_db_init(void);

void ble_profile_init(void)
{
    QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE, "BLE profile init");

    /* Initialize LE device database — REQUIRED by BTStack.
     * Without this call, BTStack has no device DB and crashes with
     * axi_rd_inv when a BLE connection is established.
     * SDK reference: every ble_profile_init in apps/common/ble/ calls this. */
    le_device_db_init();

    /* Host channel classification: all 40 BLE channels available.
     * Required for controller to respond to LL_CHANNEL_MAP_REQ from phone.
     * Without this, phone may retry indefinitely and eventually disconnect. */
    {
        static const u8 channel_map[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0x1F };
        ll_hci_set_host_channel_classification((u8 *)channel_map);
    }

    /* Match the working JL HAL: no MITM, allow bonding. */
    ble_sm_setup_init(
        IO_CAPABILITY_NO_INPUT_NO_OUTPUT,
        SM_AUTHREQ_BONDING,
        7,
        TCFG_BLE_SECURITY_EN
    );

    /* Set default MTU */
    ble_vendor_set_default_att_mtu(ATT_LOCAL_MTU_SIZE);

    /* Register QUMI GATT profile + ATT read/write + packet handler.
     * All three must be done together in ble_profile_init, matching the SDK. */
    qm_ble_hal_profile_setup();
}

/* Advertising setup - called by JieLi BLE task before advertising starts */
void advertisements_setup_init(void)
{
    static unsigned char adv_buf[31];
    static unsigned char rsp_buf[31];
    int offset = 0;

    /* Advertising interval: 800 * 0.625ms = 500ms */
    ble_op_set_adv_param(800, ADV_IND, ADV_CHANNEL_ALL);

    /* ADV data: Flags + QUMI UUID (FEB7) */
    offset = 0;
    offset += make_eir_packet_val(&adv_buf[offset], offset,
                                  HCI_EIR_DATATYPE_FLAGS, 0x06, 1);
    offset += make_eir_packet_val(&adv_buf[offset], offset,
                                  HCI_EIR_DATATYPE_COMPLETE_16BIT_SERVICE_UUIDS, 0xFEB7, 2);
    ble_op_set_adv_data(offset, adv_buf);

    /* Scan response: device name */
    const char *name = bt_get_local_name();
    if (!name) {
        name = "JL-Qumi";
    }
    int name_len = strlen(name);
    if (name_len > (int)sizeof(rsp_buf) - 2) {
        name_len = sizeof(rsp_buf) - 2;
    }
    offset = 0;
    offset += make_eir_packet_data(&rsp_buf[offset], offset,
                                   name_len < (int)strlen(name) ? HCI_EIR_DATATYPE_SHORTENED_LOCAL_NAME : HCI_EIR_DATATYPE_COMPLETE_LOCAL_NAME,
                                   (void *)name, name_len);
    ble_op_set_rsp_data(offset, rsp_buf);
}

#endif /* TCFG_BLE_DEMO_SELECT */

/* Provide the standard JL SDK BLE hooks required by this target. */
void ble_sm_setup_init(io_capability_t io_type, u8 auth_req, uint8_t min_key_size, u8 security_en)
{
    sm_init();
    sm_set_io_capabilities(io_type);
    sm_set_authentication_requirements(auth_req);
    sm_set_encryption_key_size_range(min_key_size, 16);
    sm_set_request_security(security_en);
}

void bt_ble_adv_enable(u8 enable)
{
    // The JL BT stack calls this natively on boot.
    // We MUST initialize parameters here to prevent a baseband crash ("already destroy"),
    // but QUMI will immediately overwrite this payload via qm_ble_advertising_start().
    if (enable) {
        advertisements_setup_init();
    }
    ble_op_adv_enable(enable);
}

/* Compatibility entry point retained for older JieLi application paths.  The
 * active QUMI path registers handlers from qm_ble_hal_profile_setup(). */
extern void qm_ble_hal_event_setup(void);

void bt_ble_init(void)
{
    QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                "BLE compatibility wrapper init");
    qm_ble_hal_event_setup();
}

void bt_ble_exit(void)
{
    QM_BLE_LOGI(QM_BLE_LOG_LIFECYCLE,
                "BLE compatibility wrapper exit");
    /* Stop QUMI advertising state as well; otherwise the disconnect cleanup
     * below may immediately restart advertising while the stack is shutting
     * down. */
    extern int qm_ble_advertising_stop(void);
    qm_ble_advertising_stop();
    bt_ble_adv_enable(0);

    /* Notify QUMI of disconnect when BLE module is shut down */
    extern void qm_ble_handle_disconnect(void);
    qm_ble_handle_disconnect();
}
