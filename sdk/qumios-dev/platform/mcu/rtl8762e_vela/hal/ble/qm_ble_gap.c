#include "qm.h"
#include "qm_ble_gap.h"
#include "qm_utils_timer.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/conn.h>
#include <bluetooth/uuid.h>
#include <bluetooth/gatt.h>
#include <bluetooth/mesh.h>

#include <../../subsys/bluetooth/host/hci_core.h>

#define LOG_TAG "hal"

#define BT_ADV_SCAN_UNIT(_ms) ((_ms) * 8 / 5)

typedef struct 
{
    uint8_t adv_start;
    uint8_t scan_start;
    uint8_t scan_duration;
    qm_gap_ble_cb_t cb;
    qm_utils_time_t scan_timer;
    struct bt_le_ext_adv *g_user_adv;
}qm_ble_gap_hal_t;

static qm_ble_gap_hal_t gap_hal_ctx = {0};

/**
 * @brief 该函数由底层调用，使用例子如下：
 *
 *
 *
 */

void qm_ble_gap_hal_scan_info(const struct bt_le_scan_recv_info *info, struct net_buf_simple *buf)
{
    qm_ble_gap_cb_param_t param = {0};
    if(info == NULL || buf == NULL){
        return;
    }

    if(!gap_hal_ctx.cb || !gap_hal_ctx.scan_start){
        return;
    }

    if(gap_hal_ctx.scan_duration){
        if(qm_utils_time_is_expired(&gap_hal_ctx.scan_timer)){
            gap_hal_ctx.scan_start = QM_FALSE;
            return;
        }
    }

    if(buf->len > (QM_BLE_ADV_DATA_LEN_MAX + QM_BLE_SCAN_RSP_DATA_LEN_MAX)){
        return;
    }

    param.scan_rst.rssi = info->rssi;
    param.scan_rst.adv_data_len = buf->len;
    memcpy(param.scan_rst.bda, info->addr->a.val, 6);
    
    memcpy(param.scan_rst.ble_adv, buf->data, buf->len);

    gap_hal_ctx.cb(QM_GAP_BLE_SCAN_RESULT_EVT, &param);
}

/**
 * @brief           This function is called to occur gap event, such as scan result
 *
 * @param[in]       callback: callback function
 *
 * @return  0 on success, otherwise failured
 *
 */
int qm_ble_gap_register_callback(qm_gap_ble_cb_t callback)
{
    gap_hal_ctx.cb = callback;
    return QM_EOK;
}

/**
 * @brief           This function is called to override the BTA default ADV parameters.
 *
 * @param[in]       adv_data: Pointer to User defined ADV data structure: @ref qm_ble_adv_data_t
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_set_adv_data(qm_ble_adv_data_t *adv_data)
{
    int ret = QM_EOK;
    int index = 0;
    int bt_num = 0;
    struct bt_data bt_ad = {0};
    QM_RETURN_ON_FALSE(adv_data, -QM_EINVAL, LOG_TAG, "raw_data inval");

	if (!gap_hal_ctx.g_user_adv) {

        struct bt_le_adv_param param = {
            .interval_min = BT_ADV_SCAN_UNIT(adv_data->min_interval),
            .interval_max = BT_ADV_SCAN_UNIT(adv_data->max_interval),
            .options = BT_LE_ADV_OPT_USE_IDENTITY,
        };

		ret = bt_le_ext_adv_create(&param, NULL, &gap_hal_ctx.g_user_adv);
		if (ret) {
			printk("adv create failed (err %d)\n", ret);
			return -QM_EINVAL;
		}
	}

	if (atomic_test_bit(gap_hal_ctx.g_user_adv->flags, BT_ADV_ENABLED)) {
        bt_le_ext_adv_stop(gap_hal_ctx.g_user_adv);
	}

    bt_ad.type = BT_DATA_MANUFACTURER_DATA;
    bt_ad.data_len = adv_data->manufacturer_len;
    bt_ad.data = adv_data->p_manufacturer_data;

	ret = bt_le_ext_adv_set_data(gap_hal_ctx.g_user_adv, &bt_ad, 1, NULL, 0);
	if (ret) {
		printk("Unable set adv data\n");
		return -QM_ERROR;
	}

    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT, NULL);
    }

    return QM_EOK;
}




/**
 * @brief           This function is called to set raw advertising data. User need to fill
 *                  ADV data by self.
 *
 * @param[in]       raw_data : raw advertising data
 * @param[in]       raw_data_len : raw advertising data length , less than 31 bytes
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_set_adv_data_raw(uint8_t *raw_data, uint32_t raw_data_len)
{
    int ret = QM_EOK;
    int index = 0;
    int bt_num = 0;
    struct bt_data *bt_ad = NULL;
    QM_RETURN_ON_FALSE(raw_data, -QM_EINVAL, LOG_TAG, "raw_data inval");
    QM_RETURN_ON_FALSE(raw_data_len, -QM_EINVAL, LOG_TAG, "raw_data_len inval");
	
    struct bt_le_adv_param param = {
		.interval_min = BT_ADV_SCAN_UNIT(20),
		.interval_max = BT_ADV_SCAN_UNIT(20),
		.options = BT_LE_ADV_OPT_USE_IDENTITY,
	};

	if (!gap_hal_ctx.g_user_adv) {
		ret = bt_le_ext_adv_create(&param, NULL, &gap_hal_ctx.g_user_adv);
		if (ret) {
			printk("adv create failed (err %d)\n", ret);
			return -QM_EINVAL;
		}
	}

	if (atomic_test_bit(gap_hal_ctx.g_user_adv->flags, BT_ADV_ENABLED)) {
		bt_le_ext_adv_stop(gap_hal_ctx.g_user_adv);
	}

    while(index < raw_data_len)
    {
        index += raw_data[index] + 1;
        bt_num++;
    }
    
    bt_ad = (struct bt_data *)qm_malloc(sizeof(struct bt_data) * bt_num);
    if(bt_ad == NULL){
        return -QM_ENOMEM;
    }
    memset(bt_ad, 0, sizeof(struct bt_data) * bt_num);

    index = 0;
    for(int i = 0; i < bt_num; i++)
    {
        bt_ad[i].data_len = raw_data[index];
        bt_ad[i].type = raw_data[index + 1];
        bt_ad[i].data = &raw_data[index + 2];
        index += raw_data[index] + 1;
    }
    
	ret = bt_le_ext_adv_set_data(gap_hal_ctx.g_user_adv, bt_ad, bt_num, NULL, 0);
	if (ret) {
		printk("Unable set adv data\n");
        qm_free(bt_ad);
		return -QM_ERROR;
	}

    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT, NULL);
    }

    qm_free(bt_ad);
    return QM_EOK;
}


/**
 * @brief           This function is called to start advertising.
 *
 * @param[in]       adv_params: pointer to User defined adv_params data structure: @ref qm_ble_adv_params_t

 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_start_advertising(qm_ble_adv_params_t *adv_params)
{
    int ret = QM_EOK;
    int no_timeout = 0;
    QM_RETURN_ON_FALSE(adv_params, -QM_EINVAL, LOG_TAG, "adv params inval");
    QM_RETURN_ON_FALSE(gap_hal_ctx.g_user_adv, -QM_EINVAL, LOG_TAG, "adv params inval");
    
    if(!adv_params->adv_int_min || !adv_params->adv_int_max){
        no_timeout = QM_TRUE;
        adv_params->adv_int_min = 100; //默认100ms的广播间隔
        adv_params->adv_int_max = 100;
    }

	struct bt_le_adv_param param = {
		.interval_min = BT_ADV_SCAN_UNIT(adv_params->adv_int_min),
		.interval_max = BT_ADV_SCAN_UNIT(adv_params->adv_int_max),
		.options = BT_LE_ADV_OPT_USE_IDENTITY,
	};

	struct bt_le_ext_adv_start_param start = {
		.num_events = 25,
	};

    if(no_timeout){
        start.num_events = 0;
        start.timeout = 0;
    }

	if (atomic_test_bit(gap_hal_ctx.g_user_adv->flags, BT_ADV_ENABLED)) {
		return -EBUSY;
	}

	ret = bt_le_ext_adv_update_param(gap_hal_ctx.g_user_adv, &param);
	if (ret) {
		printk("Unable set adv params\n");
		return -QM_ERROR;
	}

	ret = bt_le_ext_adv_start(gap_hal_ctx.g_user_adv, &start);
	if (ret) {
		printk("Unable start adv\n");
		return ret;
	}

    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_ADV_START_COMPLETE_EVT, NULL);
    }

    return QM_EOK;
}

/**
 * @brief           This function is called to stop advertising.
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_advertising(void)
{
    return QM_EOK;
}

/**
 * @brief           This function is called to set scan parameters
 *
 * @param[in]       scan_params: Pointer to User defined scan_params data structure: @ref qm_ble_scan_params_t
 *
 * @return  0 on success, otherwise failure.
 *
 */

int qm_ble_gap_set_scan_params(qm_ble_scan_params_t *scan_params)
{
    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT, NULL);
    }
    return QM_EOK;
}

/**
 * @brief           This procedure keep the device scanning the peer device which advertising on the air
 *
 * @param[in]       duration: Keeping the scanning time, the unit is second.
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_start_scanning(uint32_t duration)
{
    gap_hal_ctx.scan_start = QM_TRUE;
    gap_hal_ctx.scan_duration = duration;

    if(duration){
        qm_utils_time_countdown_ms(&gap_hal_ctx.scan_timer, duration * 1000);
    }

    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_SCAN_START_COMPLETE_EVT, NULL);
    }
    return QM_EOK;
}

/**
 * @brief    This function call to stop the device scanning the peer device which advertising on the air
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_stop_scanning(void)
{
    gap_hal_ctx.scan_duration = 0;
    gap_hal_ctx.scan_start = QM_FALSE;
   
    if(gap_hal_ctx.cb){
        gap_hal_ctx.cb(QM_GAP_BLE_SCAN_STOP_COMPLETE_EVT, NULL);
    }

    return QM_EOK;
}

/**
 * @brief          This function is called to get local used address and adress type.
 *
 * @param[in]       addr - current local ble address (six bytes)
 *
 * @return  0 on success, otherwise failure.
 *
 */
int qm_ble_gap_get_local_addr(qm_bd_addr_t addr)
{
    size_t count = {1};
    bt_addr_le_t addrs = {0}; 
    bt_id_get(&addrs, &count);

    addr[0] = addrs.a.val[5];
    addr[1] = addrs.a.val[4];
    addr[2] = addrs.a.val[3];
    addr[3] = addrs.a.val[2];
    addr[4] = addrs.a.val[1];
    addr[5] = addrs.a.val[0];

    return QM_EOK;
}

int qm_ble_gap_unregister_callback(qm_gap_ble_cb_t callback)
{
    gap_hal_ctx.cb = NULL;
    return QM_EOK;
}
